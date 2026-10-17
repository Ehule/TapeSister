#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/routing_matrix.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
static int calls[TS_MATRIX_ROWS];
static TsStereoFrame received[TS_MATRIX_ROWS];
static TsStereoFrame identity(void *ctx,int n,TsStereoFrame input)
{(void)ctx;++calls[n];received[n]=input;return input;}
static void near(float a,float b){assert(fabsf(a-b)<.0001f);}
static void graph(void)
{
    memset(calls,0,sizeof(calls));
    TsRoutingMatrix m;ts_matrix_init(&m);TsMatrixControls c=m.controls;c.enabled=1;
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_PRISM,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_SISTER,500)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_NODES+1,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_PRISM,1000)==0);
    assert(ts_matrix_set(&m,&c,48000));
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={{.2f,.4f}};
    TsSourceRouteMix sums;
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    for(int i=0;i<TS_MATRIX_NODES;++i)assert(calls[i]==300);
    near(received[TS_MATRIX_PRISM].l,.2f);near(received[TS_MATRIX_SISTER].r,.2f);
    near(m.output[0].l,.3f);near(m.output[1].r,.4f);
    /* Every processor output is usable as another processor's input. */
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_MAIN,0)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_FALLOUT,1000)==1);
    assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    near(received[TS_MATRIX_FALLOUT].r,.4f);near(m.output[0].l,.3f);
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_PRISM,1000)==0);
    c.feedback=1;assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_PRISM,1000)==1);
    assert(c.row[TS_MATRIX_PRISM].matrix_gain[TS_MATRIX_PRISM]==500);
    assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<5000;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);assert(isfinite(m.output[0].l));assert(m.output[0].r<2);}
    ts_matrix_cut_feedback(&c);assert(!c.delayed[TS_MATRIX_PRISM]);assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    near(received[TS_MATRIX_PRISM].l,.2f);
    /* A hardware return is independent of send assignment and can feed EQ. */
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_EQ,0)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_NODES+1,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES+2,TS_MATRIX_EQ,1000)==1);
    snprintf(c.input_name[2],TS_MATRIX_NAME,"Vulture Return");snprintf(c.output_name[1],TS_MATRIX_NAME,"Vulture Send");
    assert(ts_matrix_set(&m,&c,48000));live[2]=(TsStereoFrame){.1f,-.15f};
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    near(m.output[0].r,-.15f);near(m.output[1].l,.5f);
    c.row[TS_MATRIX_NODES].width=-100;
    FILE *f=tmpfile();assert(f&&ts_matrix_write(f,&c));rewind(f);TsMatrixControls copy;ts_matrix_default(&copy);
    char line[512];while(fgets(line,sizeof(line),f)){char *v=strchr(line,'=');assert(v);*v++=0;v[strcspn(v,"\r\n")]=0;assert(ts_matrix_read(&copy,line,v)==1);}fclose(f);
    assert(ts_matrix_valid(&copy));assert(!memcmp(&copy,&c,sizeof(c)));
    copy.row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_EQ]=1000;assert(!ts_matrix_valid(&copy));
    assert(ts_matrix_read(&copy,"Matrix.In0","A name that is much too long for a port")<0);
}
static void runtime(void)
{
    static TsSisterRuntime r;char error[200];ts_sister_runtime_init(&r);
    assert(ts_sister_runtime_reconfigure(&r,48000,2,error,sizeof(error)));
    ts_sister_runtime_configure_limiter(&r,0,-1,1,50);
    TsMatrixControls c=r.matrix.controls;c.enabled=1;assert(ts_matrix_set(&r.matrix,&c,48000));
    TsSisterSourceFrames s={.fm={.15f,.25f}};uint64_t clock=r.prism.clock;
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,.15f);near(r.matrix.output[0].r,.25f);
    assert(r.prism.clock-clock==300); /* Shared instance: exactly once per sample. */
    c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=0;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_PRISM]=1000;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,0); /* An off wet return does not duplicate dry. */
    TsSisterParameters parameters=r.parameters;parameters.prism.enabled=1;parameters.prism.mix=1;
    ts_sister_runtime_set_parameters(&r,&parameters);double energy=0;clock=r.prism.clock;
    for(int i=0;i<60000;++i){ts_sister_runtime_process_frame(&r,&s);energy+=fabsf(r.matrix.output[0].l);}
    assert(energy>1 && r.prism.clock-clock==60000);
    c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=1000;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_PRISM]=0;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    assert(ts_sister_runtime_enable(&r,48000,2,2,1,error,sizeof(error)));
    c=r.matrix.controls;c.enabled=1;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_SISTER]=1000;
    assert(ts_matrix_set(&r.matrix,&c,48000));clock=r.prism.clock;
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    assert(r.prism.clock-clock==300);near(r.last_frame.input.l,.15f);
    assert(r.machine.buffer.data[100*2]!=0); /* No hidden TRACK/EXT source gate. */
    ts_sister_runtime_free(&r);
}
static void physical_inputs(void)
{
    TsInsert *io=malloc(sizeof(*io));assert(io);ts_insert_init(io);
    ts_insert_capture_prepare(io,6,48000,512);ts_insert_begin_output_block(io,512);
    atomic_store(&io->matrix_active,1);static float samples[4096*6];
    for(int f=0;f<4096;++f)for(int ch=0;ch<6;++ch)samples[f*6+ch]=(ch+1)*.1f;
    ts_insert_capture(io,samples,4096,6);TsStereoFrame pairs[4];
    for(int f=0;f<500;++f)ts_insert_matrix_read(io,pairs);
    near(pairs[0].l,.1f);near(pairs[0].r,.2f);near(pairs[2].l,.5f);near(pairs[2].r,.6f);near(pairs[3].l,0);
    assert(!io->controls.send_pair); /* No special insert reservation. */
    ts_insert_duplex_block(io,1,samples,4096,6);ts_insert_matrix_read(io,pairs);
    near(pairs[2].l,.5f);near(pairs[2].r,.6f);
    ts_insert_free(io);free(io);
}
static void tracker_bus(void)
{
    static TsSisterRuntime r;char error[200];ts_sister_runtime_init(&r);
    assert(ts_sister_runtime_reconfigure(&r,48000,2,error,sizeof(error)));
    ts_sister_runtime_configure_limiter(&r,0,-1,1,50);
    TsMatrixControls c=r.matrix.controls;c.enabled=1;
    assert(TS_MATRIX_COMPRESSOR==15 && TS_MATRIX_SIDECHAIN==16 && TS_MATRIX_SHARED_DESTINATIONS==17 && TS_MATRIX_INSTANCE_DESTINATIONS==49 && TS_MATRIX_AMBISONICS==49 && TS_MATRIX_SPEAKER_BASE==50 && TS_MATRIX_DESTINATIONS==66);
    assert(ts_matrix_set(&r.matrix,&c,48000));
    TsSisterSourceFrames s={.tracker={.2f,-.3f},.preview={.05f,.07f},.fm={.1f,.12f}};
    /* Explicit per-track destinations arrive separately from the group mix. */
    s.matrix_graph[TS_MATRIX_NODES+2]=(TsStereoFrame){.04f,.08f};
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,.35f);near(r.matrix.output[0].r,-.11f);
    c.row[TS_MATRIX_TRACKER].matrix_gain[TS_MATRIX_MAIN]=0;
    c.row[TS_MATRIX_TRACKER].matrix_gain[TS_MATRIX_NODES+1]=500;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,.15f);near(r.matrix.output[0].r,.19f);
    near(r.matrix.output[1].l,.1f);near(r.matrix.output[1].r,-.15f);
    near(r.matrix.output[2].l,.04f);near(r.matrix.output[2].r,.08f);
    assert(r.matrix.output_peak[TS_MATRIX_TRACKER]>.29f);
    /* A group can feed the pedalboard and compressor detector independently. */
    c.row[TS_MATRIX_TRACKER].matrix_gain[TS_MATRIX_NODES+1]=0;
    c.row[TS_MATRIX_TRACKER].matrix_gain[TS_MATRIX_BOARD]=1000;
    c.row[TS_MATRIX_TRACKER].matrix_gain[TS_MATRIX_SIDECHAIN]=500;c.board_insert=1;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    for(int i=0;i<10000;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,.35f);near(r.matrix.output[0].r,-.11f);
    near(r.matrix.sidechain.l,.1f);near(r.matrix.sidechain.r,-.15f);
    /* The new row persists; older projects retain the former unity-to-Main sound. */
    TsSisterProjectState *state=calloc(1,sizeof(*state)),*copy=calloc(1,sizeof(*copy));assert(state && copy);
    ts_sister_project_state_init(state,48000);state->matrix=c;
    int present=0;
    assert(ts_sister_project_state_save_file(state,"tracker-bus-project.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(copy,"tracker-bus-project.ini",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&copy->matrix,&c,sizeof(c)));
    FILE *from=fopen("tracker-bus-project.ini","r"),*old=fopen("tracker-bus-old.ini","w");assert(from && old);
    char line[2048];while(fgets(line,sizeof(line),from)) {
        if(!strncmp(line,"Matrix.Row16=",13))continue;
        if(!strncmp(line,"Version=",8))fputs("Version=37\n",old);else fputs(line,old);
    }
    fclose(from);fclose(old);
    assert(ts_sister_project_state_load_file(copy,"tracker-bus-old.ini",48000,&present,error,sizeof(error)) && present);
    TsMatrixControls defaults;ts_matrix_default(&defaults);
    assert(!memcmp(&copy->matrix.row[TS_MATRIX_TRACKER],&defaults.row[TS_MATRIX_TRACKER],sizeof(TsSourceRoute)));
    for(int i=0;i<TS_MATRIX_TRACKER;++i)assert(!memcmp(&copy->matrix.row[i],&c.row[i],sizeof(TsSourceRoute)));
    remove("tracker-bus-project.ini");remove("tracker-bus-old.ini");free(state);free(copy);ts_sister_runtime_free(&r);
}
static void import_serial_chain(void)
{
    static TsSisterRuntime legacy,edited;char error[200];
    for(int reversed=0;reversed<2;++reversed) {
        TsSisterRuntime *both[]={&legacy,&edited};
        for(int i=0;i<2;++i) {
            TsSisterRuntime *r=both[i];ts_sister_runtime_init(r);
            assert(ts_sister_runtime_reconfigure(r,48000,2,error,sizeof(error)));
            ts_sister_runtime_configure_limiter(r,0,-1,1,50);
            TsSisterParameters p=r->parameters;
            p.prism.enabled=1;p.prism.lenses=8;p.prism.mix=.25f;p.prism.dry_level=.75f;
            p.fx.enabled=1;p.fx.fallout.enabled=1;p.fx.fallout.mix=.2f;
            p.fx.slot[0]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_DELAY,.enabled=1,
                .placement=TS_SISTER_FX_PLACE_POST,.mix=.3f,.parameter_a=.02f,.parameter_b=.4f};
            ts_sister_runtime_set_parameters(r,&p);
            if(reversed){TsRouterControls c=r->router.controls;ts_router_move(&c,3,0);ts_sister_runtime_set_router(r,&c);}
        }
        for(int f=0;f<48000;++f) {
            if(f==8000) {
                TsMatrixControls c;assert(ts_sister_runtime_matrix_import(&edited,&c,error,sizeof(error)));
                assert(c.serial_mask&(1u<<TS_MATRIX_PRISM));
                assert(ts_matrix_set(&edited.matrix,&c,48000));
                for(int row=0;row<TS_MATRIX_ROWS;++row)assert(!edited.matrix.voice[row].remaining);
                FILE *file=tmpfile();assert(file&&ts_matrix_write(file,&c));rewind(file);
                TsMatrixControls copy;ts_matrix_default(&copy);char line[512];
                while(fgets(line,sizeof(line),file)){char *v=strchr(line,'=');assert(v);*v++=0;v[strcspn(v,"\r\n")]=0;assert(ts_matrix_read(&copy,line,v)==1);}
                fclose(file);assert(!memcmp(&copy,&c,sizeof(c)));
            }
            TsStereoFrame raw={1.2f*sinf(f*.032f),.4f*cosf(f*.027f)};
            TsSisterSourceFrames s={.mosaic={raw.l*.8f,raw.r*.8f},.matrix_program_gain=.8f};
            TsStereoFrame input={fmaxf(-1,fminf(1,raw.l))*.8f,raw.r*.8f};
            ts_sister_runtime_process_frame(&legacy,&s);
            TsStereoFrame want=ts_sister_runtime_process_ordinary_post_fx(&legacy,input);
            want=ts_sister_runtime_process_output(&legacy,want);
            TsSisterRuntimeFrame frame=ts_sister_runtime_process_frame(&edited,&s);
            TsStereoFrame got=edited.matrix.controls.enabled?frame.monitor_return:
                ts_sister_runtime_process_ordinary_post_fx(&edited,input);
            got=ts_sister_runtime_process_output(&edited,got);
            assert(fabsf(want.l-got.l)<.000002f && fabsf(want.r-got.r)<.000002f);
        }
        TsMatrixControls untouched=edited.matrix.controls,c=untouched;
        edited.enabled=1;assert(!ts_sister_runtime_matrix_import(&edited,&c,error,sizeof(error)));
        assert(!memcmp(&c,&untouched,sizeof(c)));edited.enabled=0;
        edited.router.controls.send_mask=1;assert(!ts_sister_runtime_matrix_import(&edited,&c,error,sizeof(error)));
        assert(!memcmp(&c,&untouched,sizeof(c)));
        for(int i=0;i<2;++i)ts_sister_runtime_free(both[i]);
    }
}

static void terminal_destinations(void)
{
    TsSourceRoute route={.matrix_enabled=1};
    route.matrix_gain[TS_MATRIX_AMBISONICS]=1000;
    route.matrix_gain[TS_MATRIX_SPEAKER_BASE+14]=1000;
    route.matrix_gain[TS_MATRIX_SPEAKER_BASE+15]=500;
    TsSourceRouteVoice voice={0};ts_source_route_set(&voice,route,48000);
    TsSourceRouteMix sums,copy;ts_source_route_mix_init(&sums,NULL);ts_source_route_mix_init(&copy,NULL);
    ts_source_route_frame(&voice,(TsStereoFrame){.2f,.4f},&sums);
    assert(sums.graph_mask_high==3 && sums.graph_mask==(UINT64_C(1)<<TS_MATRIX_AMBISONICS));
    ts_source_route_add(&copy,&sums,.5f);
    near(copy.graph[TS_MATRIX_SPEAKER_BASE+15].r,.1f);
    TsRoutingMatrix m;ts_matrix_init(&m);TsMatrixControls c=m.controls;c.enabled=1;
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_SPEAKER_BASE+15,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_AMBISONICS,1000)==1);
    assert(!c.delayed[TS_MATRIX_MAIN]);assert(ts_matrix_set(&m,&c,48000));
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={{0}};
    ts_matrix_process(&m,&copy,live,identity,NULL);
    near(m.spatial_input.l,.1f);near(m.speaker_output[14],.15f);near(m.speaker_output[15],.075f);
    ts_matrix_cut_feedback(&c);assert(c.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_SPEAKER_BASE+15]==1000);
    TsSourceRouteHandoff h={0};ts_source_route_handoff(&h,&sums,0,64);
    ts_source_route_mix_init(&sums,NULL);ts_source_route_handoff(&h,&sums,1,64);
    near(sums.graph[65].r,.2f);
}
/* Preserve the established deterministic order, including dual ordinary/key
   dependencies, while replacing its expensive candidate-by-candidate scan. */
static int reference_order(const TsMatrixControls *c,int *order)
{
    uint64_t done=0;
    for(int pos=0;pos<TS_MATRIX_PROCESSORS;++pos) {
        int found=-1;
        for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
            int n=ts_matrix_processor(k),incoming=0;
            if(done&(UINT64_C(1)<<n))continue;
            for(int j=0;j<TS_MATRIX_PROCESSORS;++j) {
                int s=ts_matrix_processor(j);if(done&(UINT64_C(1)<<s))continue;
                for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
                    int parent=d==TS_MATRIX_SIDECHAIN?TS_MATRIX_COMPRESSOR:
                        d>=TS_MATRIX_EFFECT_SC_BASE && d<TS_MATRIX_INSTANCE_DESTINATIONS?
                            TS_MATRIX_EFFECT_BASE+d-TS_MATRIX_EFFECT_SC_BASE:d;
                    if(parent==n && c->row[s].matrix_gain[d] && !(c->delayed[s]&ts_matrix_feedback_bit(d)))incoming=1;
                }
            }
            if(!incoming){found=n;break;}
        }
        if(found<0)return 0;
        order[pos]=found;done|=UINT64_C(1)<<found;
    }
    return 1;
}
static unsigned graph_random(unsigned *state)
{return *state=*state*1664525u+1013904223u;}
static void ordering_parity(void)
{
    TsRoutingMatrix m;ts_matrix_init(&m);unsigned random=19;
    for(int iteration=0;iteration<80;++iteration) {
        TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_COMPRESSOR)==i);
        for(int s=0;s<TS_MATRIX_ROWS;++s)memset(c.row[s].matrix_gain,0,sizeof(c.row[s].matrix_gain));
        int nodes[TS_MATRIX_PROCESSORS],expected[TS_MATRIX_PROCESSORS];
        for(int i=0;i<TS_MATRIX_PROCESSORS;++i)nodes[i]=ts_matrix_processor(i);
        for(int i=TS_MATRIX_PROCESSORS-1;i>0;--i) {
            int j=(int)(graph_random(&random)%(unsigned)(i+1)),swap=nodes[i];nodes[i]=nodes[j];nodes[j]=swap;
        }
        for(int s=0;s<TS_MATRIX_PROCESSORS;++s)for(int d=s+1;d<TS_MATRIX_PROCESSORS;++d) {
            if(graph_random(&random)%3)continue;
            c.row[nodes[s]].matrix_gain[nodes[d]]=500;
            int key=ts_matrix_sidechain_for(nodes[d]);
            if(key>=0)c.row[nodes[s]].matrix_gain[key]=300;
        }
        assert(reference_order(&c,expected));assert(ts_matrix_set(&m,&c,48000));
        assert(!memcmp(expected,m.order,sizeof(expected)));
        /* An unapproved self-cycle is still rejected without changing state. */
        c.row[nodes[0]].matrix_gain[nodes[0]]=250;
        assert(!reference_order(&c,expected));assert(!ts_matrix_set(&m,&c,48000));
        c.feedback=1;c.delayed[nodes[0]]=UINT64_C(1)<<nodes[0];
        assert(reference_order(&c,expected));assert(ts_matrix_set(&m,&c,48000));
        assert(!memcmp(expected,m.order,sizeof(expected)));
    }
}
static void feedback_history_activation(void)
{
    TsRoutingMatrix m;ts_matrix_init(&m);TsMatrixControls c=m.controls;c.enabled=1;
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={{0}};
    assert(ts_matrix_set(&m,&c,48000));
    for(int cycle=0;cycle<3;++cycle) {
        c.feedback=1;assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_MAIN,250)==1);
        assert(ts_matrix_set(&m,&c,48000));
        for(int i=0;i<TS_MATRIX_DELAY*2;++i) {
            TsSourceRouteMix sums={0};sums.graph[TS_MATRIX_MAIN]=(TsStereoFrame){.4f,-.3f};
            ts_matrix_process(&m,&sums,live,identity,NULL);
            near(received[TS_MATRIX_MAIN].l,.4f+(i<TS_MATRIX_DELAY?0:.25f*tanhf(.4f)));
            near(received[TS_MATRIX_MAIN].r,-.3f+(i<TS_MATRIX_DELAY?0:.25f*tanhf(-.3f)));
        }
        ts_matrix_cut_feedback(&c);assert(ts_matrix_set(&m,&c,48000));
        for(int i=0;i<300;++i) {
            TsSourceRouteMix sums={0};sums.graph[TS_MATRIX_MAIN]=(TsStereoFrame){.9f,.7f};
            ts_matrix_process(&m,&sums,live,identity,NULL);
            near(received[TS_MATRIX_MAIN].l,.9f);near(received[TS_MATRIX_MAIN].r,.7f);
        }
    }
}
int main(void){ordering_parity();feedback_history_activation();graph();terminal_destinations();runtime();physical_inputs();tracker_bus();import_serial_chain();puts("Unified routing graph, TrackSister bus, shared DSP, serial import audio parity, loop isolation, hardware returns and persistence passed");}
