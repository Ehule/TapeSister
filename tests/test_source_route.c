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
    TsSourceRoute inherit={.mode=TS_SOURCE_INHERIT};
    assert(ts_source_route_resolve(pair,inherit).mode==TS_SOURCE_PAIR);
    assert(ts_source_route_resolve(pair,(TsSourceRoute){0}).mode==TS_SOURCE_MAIN);
    ts_source_route_set(&v,pair,48000);
    for(int i=0;i<240;++i) {
        ts_source_route_mix_init(&m,NULL);main=ts_source_route_frame(&v,input,&m);
        NEAR(main.l+m.speaker[2],input.l);NEAR(main.r+m.speaker[3],input.r);
    }
    assert(!v.remaining);NEAR(main.l,0);NEAR(m.speaker[2],.2f);NEAR(m.speaker[3],-.4f);
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
    TsPerformanceBank p;ts_performance_init(&p);ts_performance_set_attack_ms(&p,0);
    assert(ts_performance_trigger_group(&p,i,1,0,60,0,48000)==1);
    m=(TsSourceRouteMix){0};TsStereoFrame raw;main=ts_performance_read_routed(&p,&raw,&m);
    NEAR(main.l,0);assert(m.speaker[2]>0&&raw.l>0);ts_performance_clear(&p);
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
int main(void){matrix();hardware();voices_and_storage();puts("Source routing: coefficients, ramps, output ownership, voices and persistence passed");return 0;}
