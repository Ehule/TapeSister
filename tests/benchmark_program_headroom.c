/* Controlled, silent-device investigation of the actual callback. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
static int tapesister_application_main(int,char **);
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

enum { RATE=48000, BLOCK=256, MEASURE=48000, WARM=48000 };
typedef enum { LEGACY, MATRIX, IMPORTED, IMPORT_FLOAT, SISTER_BYPASS } Mode;
typedef struct { const char *name;Mode mode;float level,out_gain;int trim,tones,limiter,direct; } Case;
typedef struct { AudioState *a;TsInstrument *bank;TsSample *sample; } Fixture;
static TsSample tone(float level,int hz)
{
    TsSample s={.frames=RATE,.sample_rate=RATE,.channels=2};
    s.data=calloc(RATE*2,sizeof(float));assert(s.data);
    for(int i=0;i<RATE;++i){float x=level*(float)sin(2*M_PI*hz*i/RATE);s.data[2*i]=x;s.data[2*i+1]=x*.5f;}
    return s;
}
static Fixture make_fixture(const Case *c)
{
    Fixture f={0};char error[256];f.a=calloc(1,sizeof(*f.a));f.bank=calloc(1,sizeof(*f.bank));f.sample=calloc(3,sizeof(*f.sample));assert(f.a&&f.bank&&f.sample);
    AudioState *a=f.a;ts_note_bank_init(&a->notes);ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_capture_init(&a->capture);ts_keyboard_sequence_init(&a->keyboard_sequence);ts_tracker_playback_init(&a->tracker);
    ts_sister_runtime_init(&a->sister);ts_audio_mixer_init(&a->mixer);tapeLinkReaderInit(&a->live_link);
    ts_instrument_init(f.bank);a->output_rate=RATE;a->output_device_channels=2;a->output_range=(TsAudioChannelRange){1,0};
    a->fm_output_gain=a->fm_output_target=1;ts_speakers_default(&a->speakers);
    assert(ts_sister_runtime_reconfigure(&a->sister,RATE,2,error,sizeof(error)));
    ts_sister_runtime_configure_limiter(&a->sister,c->limiter,-1,1,50);
    ts_sister_runtime_set_master_output_gain(&a->sister,c->out_gain);
    for(int i=0;i<4;++i)a->sister.performance_mixer.gain[i]=a->sister.performance_gain_current[i]=1;
    TsSisterParameters p=a->sister.parameters;p.prism.enabled=0;p.fx.fallout.enabled=0;
    p.fx.enabled=c->trim;p.fx.master_transition=ts_sister_fx_transition_normalized(.01f);
    p.fx.slot_count=1;p.fx.slot[0]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_DELAY,.enabled=1,
        .placement=TS_SISTER_FX_PLACE_POST,.gain_db=c->trim?-12:0,.mix=0,.parameter_a=.2f,.parameter_b=0};
    p.head1_feedback=p.head2_feedback=0;p.head2_level=p.head3_level=0;p.head1_level=1;
    p.head1_time_ms=1;p.headroom=1;p.monitor_dry=0;p.monitor_wet=1;
    ts_sister_runtime_set_parameters(&a->sister,&p);ts_sister_post_fx_sync_controls(&a->sister.post_fx,&p.fx);
    if(c->mode==SISTER_BYPASS){
        assert(ts_sister_runtime_enable(&a->sister,RATE,2,2,.1,error,sizeof(error)));
        ts_sister_runtime_set_sources(&a->sister,TS_SISTER_SOURCE_TILES|TS_SISTER_SOURCE_PREVIEW);
        ts_sister_runtime_set_monitor(&a->sister,1);
        TsRouterControls router=a->sister.router.controls;router.bypass_mask|=1u<<TS_ROUTER_SISTER;
        ts_sister_runtime_set_router(&a->sister,&router);
    }
    if(c->mode==MATRIX||c->mode==IMPORTED||c->mode==IMPORT_FLOAT){
        TsMatrixControls graph;
        if(c->mode==MATRIX){
            ts_matrix_default(&graph);graph.enabled=1;
            if(c->trim){graph.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EQ]=0;
                graph.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_BOARD]=1000;
                graph.row[TS_MATRIX_BOARD].matrix_gain[TS_MATRIX_MAIN]=0;
                graph.row[TS_MATRIX_BOARD].matrix_gain[TS_MATRIX_EQ]=1000;graph.board_insert=1;}
        }else {assert(ts_sister_runtime_matrix_import(&a->sister,&graph,error,sizeof(error)));
            if(c->mode==IMPORT_FLOAT)graph.main_headroom=TS_MATRIX_HEADROOM_FLOAT;}
        assert(ts_matrix_set(&a->sister.matrix,&graph,RATE));
    }
    f.sample[0]=tone(c->level,c->tones?1300:1000);f.sample[1]=tone(c->level,1000);f.sample[2]=tone(c->level,c->tones?1700:1000);
    assert(ts_instrument_import_sample(f.bank,&f.sample[1],1,0,RATE,TS_LOOP_FORWARD,error,sizeof(error)));
    TsSourceRoute route={.matrix_enabled=1};route.matrix_gain[TS_MATRIX_EQ]=1000;
    if(c->direct)f.bank->bank[f.bank->selected_slot].output_route=route;
    ts_source_route_set(&a->audition_route,c->direct?route:(TsSourceRoute){0},RATE);
    TsNoteEvent note;assert(ts_note_event_qwerty(&note,0,60));
    assert(ts_note_bank_start_tuned_event(&a->notes,f.bank,&f.bank->tuning,TS_AUDITION_CURRENT,&note,0,RATE)==TS_NOTE_STARTED);
    a->sample=&f.sample[0];a->playing=a->looping=1;a->step=1;a->loop_direction=1;a->range_end=RATE;
    a->mosaic=ts_mosaic_create();assert(a->mosaic);a->mosaic->gain=1;
    TsMosaicSource *source=ts_mosaic_source(a->mosaic,&f.sample[2],error,sizeof(error));assert(source);
    TsMosaicEvent *event=ts_mosaic_add(a->mosaic,source,0,0);assert(event);event->duration=10;event->looping=1;event->gain=1;
    if(c->direct)event->output_route=route;
    /* Begin at the requested OUT level, as a settled project. A live fader
       ramp from unity would exercise the hardware guard during warm-up. */
    a->sister.master_output_gain.current=a->sister.master_output_gain.target=c->out_gain;
    a->sister.master_output_gain.remaining=0;
    a->mosaic->playing=1;return f;
}
static double residual_db(const float *out,int ch,int tones)
{
    double energy=0,explained=0;int frequencies[]={1000,1300,1700};
    for(int i=0;i<MEASURE;++i)energy+=(double)out[2*i+ch]*out[2*i+ch];
    for(int n=0;n<(tones?3:1);++n){double s=0,c=0;
        for(int i=0;i<MEASURE;++i){double angle=2*M_PI*frequencies[n]*i/RATE;
            s+=out[2*i+ch]*sin(angle);c+=out[2*i+ch]*cos(angle);}
        explained+=2*(s*s+c*c)/MEASURE;
    }
    return 10*log10(fmax(1e-16,(energy-explained)/fmax(energy,1e-30)));
}
static void dispose(Fixture *f)
{
    ts_mosaic_free(f->a->mosaic);project_audio_free(f->a);ts_instrument_free(f->bank);
    for(int i=0;i<3;++i)ts_sample_free(&f->sample[i]);free(f->sample);free(f->a);free(f->bank);
}
int main(int argc,char **argv)
{
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    const Case cases[]={
        {"quiet_legacy",LEGACY,.2f,.25f,0,0,0,0},{"quiet_matrix",MATRIX,.2f,.25f,0,0,0,0},
        {"hot_legacy",LEGACY,.5f,.25f,0,0,0,0},{"hot_matrix",MATRIX,.5f,.25f,0,0,0,0},
        {"hot_import",IMPORTED,.5f,.25f,0,0,0,0},{"hot_import_float",IMPORT_FLOAT,.5f,.25f,0,0,0,0},
        {"trim_legacy",LEGACY,.5f,.25f,1,0,0,0},{"trim_matrix",MATRIX,.5f,.25f,1,0,0,0},
        {"trim_import",IMPORTED,.5f,.25f,1,0,0,0},{"trim_import_float",IMPORT_FLOAT,.5f,.25f,1,0,0,0},
        {"trim_sister_bypass",SISTER_BYPASS,.5f,.25f,1,0,0,0},
        {"tones_legacy",LEGACY,.5f,.25f,0,1,0,0},{"tones_matrix",MATRIX,.5f,.25f,0,1,0,0},
        {"hot_import_direct_eq",IMPORTED,.5f,.25f,0,0,0,1},
        {"hot_matrix_limiter",MATRIX,.5f,1,0,0,1,0},{"hot_matrix_guard",MATRIX,.5f,1,0,0,0,0}
    };
    float *out=calloc(MEASURE*2,sizeof(float));assert(out);float warm[BLOCK*2];
    puts("case,source_peak,out_gain,post_trim_db,tones,limiter,output_peak_l,output_peak_r,residual_l_db,residual_r_db,flat_peak_percent,main_in_meter,main_out_meter,router_in_meter,limiter_input_meter,hardware_guard_gain,sister_overloads");
    for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k){const Case *c=&cases[k];Fixture f=make_fixture(c);
        for(int at=0;at<WARM;at+=BLOCK){int n=SDL_min(BLOCK,WARM-at);audio_callback(f.a,(Uint8*)warm,n*2*sizeof(float));}
        assert(f.a->playing && f.a->mosaic->playing && ts_note_bank_count(&f.a->notes)==1 && f.a->audition_route.ready);
        assert(f.a->audition_route.activity_source>c->level*.99f);
        for(int at=0;at<MEASURE;at+=BLOCK){int n=SDL_min(BLOCK,MEASURE-at);audio_callback(f.a,(Uint8*)(out+2*at),n*2*sizeof(float));}
        float peak[2]={0};for(int i=0;i<MEASURE*2;++i){assert(isfinite(out[i])&&fabsf(out[i])<=1.00001f);peak[i%2]=fmaxf(peak[i%2],fabsf(out[i]));}
        if(c->level==.2f)assert(fabsf(peak[0]-.12f)<1e-6f);
        double residual[2]={residual_db(out,0,c->tones),residual_db(out,1,c->tones)};
        if(c->level==.5f && (c->mode==LEGACY || c->mode==IMPORTED) && !c->direct) {
            assert(residual[0]>-30 && residual[0]<-10);assert(residual[1]<-95);
        } else if(!c->limiter && c->out_gain<1)assert(residual[0]<-95 && residual[1]<-95);
        if(c->limiter)assert(peak[0]<=powf(10,-1.f/20)+1e-6f);
        if(!strcmp(c->name,"hot_matrix_guard"))assert(fabsf(peak[0]-.98f)<1e-6f);
        int flat=0;for(int i=0;i<MEASURE;++i)flat+=fabsf(fabsf(out[2*i])-peak[0])<1e-6f;
        printf("%s,%.6f,%.6f,%d,%d,%d,%.9g,%.9g,%.6f,%.6f,%.6f,%.9g,%.9g,%.9g,%.9g,%.9g,%llu\n",c->name,c->level,c->out_gain,c->trim?-12:0,c->tones,c->limiter,peak[0],peak[1],residual[0],residual[1],100.0*flat/MEASURE,
            f.a->sister.matrix.input_peak[TS_MATRIX_MAIN],f.a->sister.matrix.output_peak[TS_MATRIX_MAIN],f.a->sister.router.input_peak[TS_ROUTER_PRISM],f.a->sister.limiter_input_peak,f.a->clean_output.limit,(unsigned long long)f.a->sister.machine.overload_count);
        if(argc>1){char name[512],error[256];snprintf(name,sizeof(name),"%s/%s.wav",argv[1],c->name);
            TsSample sample={.data=out,.frames=MEASURE,.sample_rate=RATE,.channels=2};assert(ts_sample_save_wav32f(&sample,name,error,sizeof(error)));}
        dispose(&f);
    }
    free(out);SDL_Quit();return 0;
}
