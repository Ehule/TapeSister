#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/clean_output.h"
#include "tapesister/note_bank.h"
#include "tapesister/performance.h"
#include "tapesister/keyboard_sequence.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define NEAR(a,b) assert(fabsf((a)-(b))<.00001f)
static const TsSourceRoute pair={TS_SOURCE_PAIR,2,3,0,0};
static void matrix(void)
{
    TsSourceRouteVoice v={0};TsSourceRouteMix m={0};TsStereoFrame input={.2f,-.4f},main;
    ts_source_route_set(&v,(TsSourceRoute){0},48000);
    main=ts_source_route_frame(&v,input,&m);assert(!memcmp(&input,&main,sizeof(input))&&!m.mask);
    TsSourceRouteVoice panned={0};TsSourceRouteMix dry={0};
    ts_source_route_set(&panned,(TsSourceRoute){.pan=-100,.width=-100},48000);
    TsStereoFrame audible=ts_source_route_frame(&panned,input,&dry);
    NEAR(audible.l,-.1f);NEAR(audible.r,0);
    NEAR(audible.l+dry.reference.l,input.l);NEAR(audible.r+dry.reference.r,input.r);
    NEAR(dry.tape_input.l,0);NEAR(dry.tape_input.r,0); /* Main pan stays on its serial path. */
    TsSourceRoute inherit={.mode=TS_SOURCE_INHERIT};
    assert(ts_source_route_resolve(pair,inherit).mode==TS_SOURCE_PAIR);
    assert(ts_source_route_resolve(pair,(TsSourceRoute){0}).mode==TS_SOURCE_MAIN);
    ts_source_route_set(&v,pair,48000);
    for(int i=0;i<240;++i) {
        ts_source_route_mix_init(&m,NULL);main=ts_source_route_frame(&v,input,&m);
        NEAR(main.l+m.speaker[2],input.l);NEAR(main.r+m.speaker[3],input.r);
        NEAR(main.l+m.tape_input.l,input.l);NEAR(main.r+m.tape_input.r,input.r);
    }
    assert(!v.remaining);NEAR(main.l,0);NEAR(m.speaker[2],.2f);NEAR(m.speaker[3],-.4f);
    NEAR(m.tape_input.l,input.l);NEAR(m.tape_input.r,input.r);
    TsSourceRoute r=pair;r.width=-100;r.pan=100;memset(&v,0,sizeof(v));ts_source_route_set(&v,r,48000);
    ts_source_route_mix_init(&m,NULL);ts_source_route_frame(&v,input,&m);
    NEAR(m.speaker[2],0);NEAR(m.speaker[3],-.1f);
    r=(TsSourceRoute){TS_SOURCE_SPEAKER,15,0,0,0};memset(&v,0,sizeof(v));ts_source_route_set(&v,r,48000);
    ts_source_route_mix_init(&m,NULL);ts_source_route_frame(&v,input,&m);NEAR(m.speaker[15],-.1f);
    r=pair;r.second=2;assert(!ts_source_route_valid(&r,0));r=pair;r.pan=101;assert(!ts_source_route_valid(&r,0));
    TsSourceRouteHandoff h={0};ts_source_route_handoff(&h,&m,1,5);ts_source_route_mix_init(&m,NULL);
    ts_source_route_handoff(&h,&m,1,5);NEAR(m.speaker[15],-.1f);
    for(int i=0;i<6;++i){ts_source_route_mix_init(&m,NULL);ts_source_route_handoff(&h,&m,0,5);}
    assert(!h.last.mask);NEAR(m.speaker[15],0);
}
static void hardware(void)
{
    TsSpatialControls c;ts_spatial_default(&c);c.output[2]=2;c.output[3]=3;TsCleanOutput guard={0};
    TsSourceRouteVoice v={0};ts_source_route_set(&v,pair,48000);
    TsSourceRouteMix m={0};m.available=ts_clean_output_available(&c,4,0);m.check_outputs=1;
    assert(m.available==15);ts_source_route_frame(&v,(TsStereoFrame){.2f,.4f},&m);
    float out[4]={0};ts_clean_output_mix(&guard,&m,&c,.5f,out,4,0,48000);
    NEAR(out[0],0);NEAR(out[1],0);NEAR(out[2],.1f);NEAR(out[3],.2f);
    /* Whole-pair fallback leaves an unrelated available speaker intact. */
    ts_source_route_mix_init(&m,NULL);m.check_outputs=1;m.available=ts_clean_output_available(&c,2,0);
    ts_source_route_frame(&v,(TsStereoFrame){.2f,.4f},&m);
    TsSourceRouteVoice other={0};ts_source_route_set(&other,(TsSourceRoute){TS_SOURCE_SPEAKER,0,0,0,0},48000);
    ts_source_route_frame(&other,(TsStereoFrame){.1f,.1f},&m);
    memset(out,0,sizeof(out));ts_clean_output_mix(&guard,&m,&c,1,out,2,0,48000);
    NEAR(out[0],.3f);NEAR(out[1],.4f);assert(m.missing==12);
    assert(ts_clean_output_available(&c,4,1)==3);c.output[1]=0;assert(!(ts_clean_output_available(&c,4,0)&3));
    /* Reserved Insert outputs survive the final linked guard. */
    out[0]=2;out[1]=-2;out[2]=.7f;out[3]=-.8f;m=(TsSourceRouteMix){.mask=1,.check_outputs=1};
    ts_clean_output_mix(&guard,&m,&c,1,out,4,1,48000);NEAR(out[0],.98f);NEAR(out[2],.7f);NEAR(out[3],-.8f);
}
static void voices_and_storage(void)
{
    TsInstrument *i=malloc(sizeof(*i)),*loaded=malloc(sizeof(*loaded));assert(i&&loaded);
    ts_instrument_init(i);ts_instrument_init(loaded);char error[200];
    assert(ts_instrument_activate_silence(i,512,48000,error,sizeof(error)));
    for(int f=0;f<512;++f)i->current.data[f]=.25f;
    assert(ts_instrument_sync_selected(i,error,sizeof(error)));i->bank[0].output_route=pair;
    assert(ts_instrument_copy_selected(i,1,error,sizeof(error)));assert(i->bank[1].output_route.mode==TS_SOURCE_PAIR);
    assert(ts_instrument_save_recipe(i,"source-route.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(loaded,"source-route.tsr",error,sizeof(error)));
    assert(!memcmp(&loaded->bank[0].output_route,&pair,sizeof(pair)));
    TsNoteBank n;ts_note_bank_init(&n);ts_note_bank_set_attack_ms(&n,0);
    assert(ts_note_bank_start(&n,i,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    TsSourceRouteMix m={0};TsStereoFrame main={0};ts_note_bank_read_routed(&n,&main,NULL,NULL,&m);
    NEAR(main.l,0);assert(m.speaker[2]>0&&m.speaker[3]>0);
    ts_note_bank_clear(&n);
    /* Hundreds of voices include expanded routing coefficients. Keep this
       single-use fixture off MinGW's 2 MiB stack, including during TSR loads. */
    static TsPerformanceBank p;ts_performance_init(&p);ts_performance_set_attack_ms(&p,0);
    assert(ts_performance_trigger_group(&p,i,1,0,60,0,48000)==1);
    m=(TsSourceRouteMix){0};TsStereoFrame raw;main=ts_performance_read_routed(&p,&raw,&m);
    NEAR(main.l,0);assert(m.speaker[2]>0&&raw.l>0);ts_performance_clear(&p);
    assert(!p.render_limit && p.activity[0].tile==i->bank[0].tile_id && p.activity[0].source>0 && p.activity[0].send>0);
    TsKeyboardSequence seq;ts_keyboard_sequence_init(&seq);TsKeyboardSequenceSource source={0};
    source.count=1;
    source.voices[0]=n.voices[0]; /* Install an independent immutable template below. */
    source.voices[0].sample=&i->current;source.voices[0].range_last=512;source.voices[0].active=1;
    source.voices[0].step=1;source.voices[0].gain=1;source.voices[0].direction=1;
    ts_source_route_set(&source.voices[0].output_route,pair,48000);
    TsKeyboardSequenceSettings settings=seq.settings;settings.count=1;settings.notes[0]=60;settings.seconds=1;settings.gate=1;
    ts_keyboard_sequence_set(&seq,&settings);ts_keyboard_sequence_source(&seq,&source);assert(ts_keyboard_sequence_play(&seq));
    double energy=0;for(int f=0;f<300;++f){m=(TsSourceRouteMix){0};main=ts_keyboard_sequence_read_routed(&seq,48000,&m);NEAR(main.l,0);energy+=fabsf(m.monitor.l);}
    assert(energy>0);ts_keyboard_sequence_stop(&seq);
    ts_instrument_free(i);ts_instrument_free(loaded);free(i);free(loaded);remove("source-route.tsr");
}
static void activity(void)
{
    TsSourceRouteVoice v={0};TsSourceRoute r={.matrix_enabled=1,.width=-100};
    r.matrix_gain[0]=1000;ts_source_route_set(&v,r,48000);TsSourceRouteMix mix={0};
    ts_source_route_frame(&v,(TsStereoFrame){.2f,-.2f},&mix);
    NEAR(v.activity_source,.2f);NEAR(v.activity_send,0); /* Mono cancellation. */
    ts_source_route_frame(&v,(TsStereoFrame){.1f,.1f},&mix);
    NEAR(v.activity_source,.2f);NEAR(v.activity_send,.1f); /* Retain short transients. */
    memset(&v,0,sizeof(v));r.matrix_gain[0]=0;ts_source_route_set(&v,r,48000);
    ts_source_route_frame(&v,(TsStereoFrame){.3f,.3f},&mix);
    NEAR(v.activity_source,.3f);NEAR(v.activity_send,0); /* An unpatched source still has input. */
}
static void sparse_accumulation(void)
{
    /* Dense oracle covers all three mask words, dense routing, ignored bus data,
       negative handoff gains and adding a mix to itself. */
    uint64_t random=UINT64_C(0x54a285e9da713f01);
    const float gains[]={0,1,-1,.37f,2};
    for(int trial=0;trial<300;++trial) {
        TsSourceRouteMix from={0},to={0};
        random=random*UINT64_C(6364136223846793005)+1;from.graph_mask=random;
        random=random*UINT64_C(6364136223846793005)+1;from.graph_mask_high=random;
        random=random*UINT64_C(6364136223846793005)+1;from.graph_mask_top=random;
        if(trial==0)from.graph_mask=from.graph_mask_high=from.graph_mask_top=0;
        if(trial==1)from.graph_mask=from.graph_mask_high=from.graph_mask_top=UINT64_MAX;
        if(trial==2){from.graph_mask=UINT64_C(1)<<63;from.graph_mask_high=3;from.graph_mask_top=1;}
        if(trial==3){from.graph_mask=from.graph_mask_high=0;from.graph_mask_top=1;}
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
            from.graph[d]=(TsStereoFrame){(d-30)*.017f,(30-d)*.023f};
            to.graph[d]=(TsStereoFrame){(d-12)*.031f,(12-d)*.019f};
        }
        if(trial&1)to=from;
        TsSourceRouteMix want=to;
        const TsSourceRouteMix *input=trial&1?&to:&from;
        float gain=gains[trial%5];
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
            int active=d<64?!!(input->graph_mask&(UINT64_C(1)<<d)):
                d<128?!!(input->graph_mask_high&(UINT64_C(1)<<(d-64))):
                !!(input->graph_mask_top&(UINT64_C(1)<<(d-128)));
            if(active) {
                want.graph[d].l+=input->graph[d].l*gain;
                want.graph[d].r+=input->graph[d].r*gain;
            }
        }
        uint64_t low=to.graph_mask|input->graph_mask,high=to.graph_mask_high|input->graph_mask_high;
        uint64_t top=to.graph_mask_top|input->graph_mask_top;
        ts_source_route_add(&to,input,gain);
        assert(!memcmp(to.graph,want.graph,sizeof(to.graph)));
        assert(to.graph_mask==low && to.graph_mask_high==high && to.graph_mask_top==top);
    }
    TsSourceRouteMix mix,expected;memset(&expected,0,sizeof(expected));memset(&mix,0x7f,sizeof(mix));
    mix.available=0x91;mix.check_outputs=1;expected.available=0x91;expected.check_outputs=1;
    ts_source_route_mix_init(&mix,&mix);
    assert(!memcmp(&mix,&expected,sizeof(mix)));
    ts_source_route_mix_init(&mix,NULL);expected.available=expected.check_outputs=0;
    assert(!memcmp(&mix,&expected,sizeof(mix)));
}
static void silent_routing(void)
{
    TsSourceRoute routes[4]={{0},pair,pair,{.matrix_enabled=1}};
    routes[1].pan=-30;routes[1].width=-70;
    routes[2].mix_enabled=1;routes[2].clean_level=43;
    for(int b=0;b<TS_SOURCE_SENDS;++b)routes[2].send_level[b]=10+b*17;
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)routes[3].matrix_gain[d]=600;
    const TsStereoFrame zeros[]={{0,0},{-0.f,0},{0,-0.f},{-0.f,-0.f}};
    for(int route=0;route<4;++route)for(int z=0;z<4;++z) {
        TsSourceRouteVoice fast={0};ts_source_route_set(&fast,routes[route],48000);
        TsSourceRouteVoice ordinary=fast;ordinary.remaining=1;
        /* The one-frame, zero-distance ramp forces the arithmetic path. */
        TsSourceRouteMix got={.available=1,.check_outputs=1},want=got;
        TsStereoFrame a=ts_source_route_frame(&fast,zeros[z],&got);
        TsStereoFrame b=ts_source_route_frame(&ordinary,zeros[z],&want);
        NEAR(a.l,b.l);NEAR(a.r,b.r);
        assert(got.graph_mask==want.graph_mask && got.graph_mask_high==want.graph_mask_high &&
               got.graph_mask_top==want.graph_mask_top);
        assert(got.send_mask==want.send_mask && got.mask==want.mask && got.missing==want.missing);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d){NEAR(got.graph[d].l,want.graph[d].l);NEAR(got.graph[d].r,want.graph[d].r);}
        for(int sp=0;sp<TS_SOURCE_SPEAKERS;++sp){NEAR(got.speaker[sp],want.speaker[sp]);NEAR(got.matrix_speaker[sp],want.matrix_speaker[sp]);}
        for(int send=0;send<TS_SOURCE_SENDS;++send){NEAR(got.send[send].l,want.send[send].l);NEAR(got.send[send].r,want.send[send].r);}
        NEAR(got.reference.l,want.reference.l);NEAR(got.reference.r,want.reference.r);
        NEAR(got.tape_input.l,want.tape_input.l);NEAR(got.tape_input.r,want.tape_input.r);
        NEAR(got.monitor.l,want.monitor.l);NEAR(got.monitor.r,want.monitor.r);
        NEAR(got.fallback.l,want.fallback.l);NEAR(got.fallback.r,want.fallback.r);
        ts_source_route_set(&fast,routes[(route+1)%4],48000);
        for(unsigned frame=0;frame<240;++frame) {
            TsSourceRouteMix mix={0};unsigned before=fast.remaining;assert(before);
            ts_source_route_frame(&fast,(TsStereoFrame){0,0},&mix);
            assert(fast.remaining==before-1);
        }
        assert(!fast.remaining && !memcmp(fast.matrix,fast.target,sizeof(fast.matrix)));
    }
}
static void extended_output_ownership(void)
{
    /* A settled silent source owns exactly its route, including at each mask
       boundary. A high output must never alias a lower output's bit. */
    const int destinations[]={63,64,127,128,TS_MATRIX_DESTINATIONS-1};
    for(unsigned at=0;at<sizeof(destinations)/sizeof(destinations[0]);++at) {
        int d=destinations[at];TsSourceRoute r={.matrix_enabled=1};r.matrix_gain[d]=1000;
        TsSourceRouteVoice voice={0};ts_source_route_set(&voice,r,48000);
        TsSourceRouteMix silent={0},signal={0},sum={0};
        ts_source_route_frame(&voice,(TsStereoFrame){0,0},&silent);
        uint64_t low=d<64?UINT64_C(1)<<d:0;
        uint64_t high=d>=64 && d<128?UINT64_C(1)<<(d-64):0;
        uint64_t top=d>=128?UINT64_C(1)<<(d-128):0;
        assert(silent.graph_mask==low && silent.graph_mask_high==high && silent.graph_mask_top==top);
        ts_source_route_frame(&voice,(TsStereoFrame){.25f,-.5f},&signal);
        assert(signal.graph_mask==low && signal.graph_mask_high==high && signal.graph_mask_top==top);
        ts_source_route_add(&sum,&silent,1);ts_source_route_add(&sum,&signal,1);
        for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
            NEAR(sum.graph[i].l,i==d?.25f:0);NEAR(sum.graph[i].r,i==d?-.5f:0);
        }
    }
}
static void reusable_mix(void)
{
    TsSourceRouteMix from={0},to={0},expected={0};
    for(int frame=0;frame<4*TS_MATRIX_DESTINATIONS;++frame) {
        ts_source_route_mix_reset(&from);
        int d=frame%TS_MATRIX_DESTINATIONS;
        if(frame&1) {
            from.graph[d]=(TsStereoFrame){-.375f,.1875f};
            ts_source_route_graph_mark(&from,(unsigned)d);
        }
        if(frame%5==0)for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
            from.graph[i]=(TsStereoFrame){i*.001f,-i*.002f};
            ts_source_route_graph_mark(&from,(unsigned)i);
        }
        from.available=frame&65535;from.check_outputs=1;
        ts_source_route_mix_reuse(&to,&from);
        ts_source_route_graph_copy(&to,&from);
        expected=from;
        float gain=(frame%7)*.25f;
        for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
            expected.graph[i].l*=gain;expected.graph[i].r*=gain;
        }
        ts_source_route_graph_scale(&to,gain);
        assert(!memcmp(&to,&expected,sizeof(to)));
        ts_source_route_mix_reuse(&to,&to);
        memset(&expected,0,sizeof(expected));expected.available=from.available;expected.check_outputs=1;
        assert(!memcmp(&to,&expected,sizeof(to)));
    }
}
int main(void){reusable_mix();matrix();hardware();voices_and_storage();activity();sparse_accumulation();silent_routing();extended_output_ownership();puts("Source routing: coefficients, ramps, output ownership, voices, signal activity, sparse/silent sums and persistence passed");return 0;}
