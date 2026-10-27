/* Explicit benchmark: the real callback, real embedded FT2 and dummy SDL I/O.
   No physical driver or timing gate. An optional original backend can run in
   the same process, alternating callback order and checking every output bit. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
static int tapesister_application_main(int,char **);
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
#include <time.h>
#ifdef TS_PATTERN_COMPARE
extern void ts_tapehead_pattern_install_reference(void);
extern void ts_tapehead_pattern_install(void);
#endif

typedef struct {
    int cards,total,frames,blocks,rate,tracks,notes,edits,silent,fx,master_bpm;
    double seek;
    const char *capture;
} PatternCase;
typedef struct {
    AudioState *audio;
    TsUiState *ui;
    TsInstrument *bank;
    TsSamplePages *pages;
    TsMosaic *mosaic;
    float *out;
} PatternFixture;
typedef struct {
    double *wall,*cpu,sum_wall,sum_cpu,first_wall,first_cpu,control_cpu;
    unsigned cpu_late,wall_late;
    uint64_t hash;
    double energy;
} PatternTiming;
static double cpu_seconds(void)
{
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    assert(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user));
    ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)*1e-7;
#else
    struct timespec t;assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));
    return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static int compare_double(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static uint64_t hash_bytes(uint64_t hash,const void *data,size_t n)
{const unsigned char *p=data;for(size_t i=0;i<n;++i)hash=(hash^p[i])*UINT64_C(1099511628211);return hash;}
static void dummy(void *p,Uint8 *out,int n){(void)p;memset(out,0,n);}
static void select_backend(int reference)
{
#ifdef TS_PATTERN_COMPARE
    if(reference)ts_tapehead_pattern_install_reference();else ts_tapehead_pattern_install();
#else
    assert(!reference);
#endif
}
static PatternFixture fixture(const PatternCase *c,SDL_Window *window)
{
    PatternFixture f={0};char error[256];
    f.audio=calloc(1,sizeof(*f.audio));f.ui=calloc(1,sizeof(*f.ui));
    f.bank=calloc(1,sizeof(*f.bank));f.pages=calloc(1,sizeof(*f.pages));
    assert(f.audio&&f.ui&&f.bank&&f.pages);
    AudioState *a=f.audio;ts_ui_init(f.ui);ts_instrument_init(f.bank);
    assert(ts_sample_pages_init(f.pages,error,sizeof(error)));
    ts_sister_runtime_init(&a->sister);ts_note_bank_init(&a->notes);
    ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_capture_init(&a->capture);ts_keyboard_sequence_init(&a->keyboard_sequence);
    ts_tracker_playback_init(&a->tracker);ts_audio_mixer_init(&a->mixer);tapeLinkReaderInit(&a->live_link);
    a->fm_output_gain=a->fm_output_target=1;a->output_rate=c->rate;a->output_device_channels=8;
    a->output_range=a->input_range=(TsAudioChannelRange){1,0};ts_speakers_default(&a->speakers);
    assert(ts_sister_runtime_reconfigure(&a->sister,c->rate,2,error,sizeof(error)));
    if(c->master_bpm) {
        a->sister.clock_controls.enabled=1;a->sister.clock_controls.bpm=(unsigned)c->master_bpm;
        a->sister.clock_controls.tempo_mask=TS_CLOCK_TRACKER|TS_CLOCK_ARP;
    }
    if(c->fx){
        TsSisterParameters p=a->sister.parameters;
        p.prism.enabled=1;p.prism.mode=TS_PRISM_HARMONIC;p.prism.lenses=16;
        p.fx.enabled=1;p.fx.slot_count=4;
        p.fx.slot[0]=(TsSisterFxSlotControls){TS_SISTER_FX_DISTORTION,1,TS_SISTER_FX_PLACE_POST,0,.3f,.5f,.5f,.35f};
        p.fx.slot[1]=(TsSisterFxSlotControls){TS_SISTER_FX_DELAY,1,TS_SISTER_FX_PLACE_POST,0,.4f,.45f,.5f,.4f};
        p.fx.slot[2]=(TsSisterFxSlotControls){TS_SISTER_FX_REVERB,1,TS_SISTER_FX_PLACE_POST,0,.6f,.6f,.5f,.35f};
        p.fx.slot[3]=(TsSisterFxSlotControls){TS_SISTER_FX_GRAIN,1,TS_SISTER_FX_PLACE_POST,0,.3f,.4f,.5f,.25f};
        ts_sister_runtime_set_parameters(&a->sister,&p);
    }
    ts_realtime_diagnostics_init(&a->realtime_diagnostics);a->realtime_diagnostics_enabled=1;
    a->realtime_counter_frequency=SDL_GetPerformanceFrequency();
    /* init() installs the current backend, even when already initialized. */
    TsTapeHeadHost host={.window=window};assert(ts_tapehead_init(&host,c->rate,error,sizeof(error)));
    assert(ts_instrument_activate_silence(f.bank,4096,c->rate,error,sizeof(error)));
    for(size_t i=0;i<f.bank->current.frames;++i)f.bank->current.data[i]=.05f*sinf((float)i*.033f);
    assert(ts_instrument_sync_selected(f.bank,error,sizeof(error)));
    f.bank->has_loop=f.bank->bank[0].has_loop=1;
    f.bank->loop_last=f.bank->bank[0].loop_last=f.bank->current.frames;
    TsPatternId id;assert(ts_sister_tracker_add_pattern(&f.pages->tracker,256,&id,error,sizeof(error)));
    assert(ts_sister_tracker_insert_order(&f.pages->tracker,0,id,error,sizeof(error)));
    f.pages->tracker.channel_count=(uint8_t)c->tracks;f.pages->tracker.bpm=125;f.pages->tracker.ticks_per_line=6;
    uint8_t alias;TsTileId tile=f.bank->bank[0].tile_id;
    assert(ts_sister_tracker_bind_tile(&f.pages->tracker,tile,&alias,error,sizeof(error)));
    if(!c->silent)for(int row=0;row<256;row+=4)for(int lane=0;lane<c->tracks;++lane)
        f.pages->tracker.patterns[0]->cells[row][lane]=(TsTrackerCell){.tile_id=tile,.note_kind=TS_TRACKER_NOTE_PITCH,
            .note=(uint8_t)(48+(lane%8)*3),.has_volume=1,.volume=24,.fx_command='4',.fx_value=0x47};
    assert(ts_tapehead_sync(f.pages,f.bank,c->rate,error,sizeof(error)));
    f.ui->show_keyboard=1;f.ui->workbench_loop_active=1;f.ui->audition_source=TS_AUDITION_CURRENT;
    f.out=calloc((size_t)c->frames*8,sizeof(float));assert(f.out);
    return f;
}
static void arrange(PatternFixture *f,const PatternCase *c)
{
    char error[256];f->mosaic=ts_mosaic_create();assert(f->mosaic);
    f->audio->mosaic=f->ui->mosaic=f->mosaic;
    TsMosaicSource *source=ts_pattern_source(f->mosaic,f->pages,f->bank,f->pages->tracker.patterns[0]->id,error,sizeof(error));
    assert(source&&source->pattern->prepared);
    for(int i=0;i<c->total;++i){
        TsMosaicEvent *e=ts_mosaic_add(f->mosaic,source,i<c->cards?0:1000+i,i*20);assert(e);
        e->duration=1000;e->gain=.15f;e->pan=(i%3-1)*.35f;e->note_count=c->notes;
        for(int n=0;n<c->notes;++n)e->notes[n]=48+i%3*7+n*3;
        if(i%3==1){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[3]=650;}
    }
    f->mosaic->playing=1;
}
static void edit(PatternFixture *f,const PatternCase *c,SDL_Window *window,int block)
{
    ts_output_userdata=f->audio;
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_z;
    if(block%32==0){
        assert(keyboard_note_key_event(&e,window,TS_AUDIO_LOGICAL_OUTPUT,f->audio,f->ui,f->bank,NULL,c->rate));
        assert(ts_note_bank_count(&f->audio->notes)>0);
    }else release_note(TS_AUDIO_LOGICAL_OUTPUT,f->audio,f->ui,0);
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT,"benchmark Mosaic mix edit");
    f->mosaic->events[0].pan=block%32?.4f:-.4f;f->mosaic->events[0].gain=block%32?.2f:.1f;
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
}
static void report(const char *backend,const PatternCase *c,PatternFixture *f,PatternTiming *t)
{
    TsRealtimeDiagnosticsSnapshot h;assert(ts_realtime_diagnostics_get(&f->audio->realtime_diagnostics,&h));
    qsort(t->cpu,c->blocks,sizeof(double),compare_double);qsort(t->wall,c->blocks,sizeof(double),compare_double);
    printf("%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.9g,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%016llx\n",
        backend,c->cards,c->total,c->frames,c->rate,c->blocks,c->tracks,c->notes,c->fx,c->edits,c->silent,c->master_bpm,c->seek,
        t->sum_cpu/c->blocks,t->cpu[c->blocks/2],t->cpu[(c->blocks-1)*95/100],t->cpu[(c->blocks-1)*99/100],t->cpu[c->blocks-1],
        t->sum_wall/c->blocks,t->wall[(c->blocks-1)*95/100],t->wall[(c->blocks-1)*99/100],t->wall[c->blocks-1],
        t->cpu_late,t->wall_late,t->first_cpu,t->first_wall,t->control_cpu*1000,h.control_worst_microseconds/1000,
        h.control_wait_worst_microseconds/1000,h.control_worst_site?h.control_worst_site:"none",(unsigned long long)t->hash);
    if(!c->silent)assert(t->energy>0);
    assert(!ts_pattern_engine_overflow(f->mosaic->pattern_engine));
}
static void dispose(PatternFixture *f)
{
    ts_mosaic_free(f->mosaic);ts_sample_pages_free(f->pages);ts_instrument_free(f->bank);project_audio_free(f->audio);
    free(f->audio);free(f->ui);free(f->bank);free(f->pages);free(f->out);
}
int main(int argc,char **argv)
{
    PatternCase c={.cards=8,.frames=256,.blocks=384,.rate=48000,.tracks=8,.notes=1,.seek=-1};
    for(int i=1;i<argc;++i){
#define NUMBER(name,field) if(!strcmp(argv[i],name)&&i+1<argc)c.field=atoi(argv[++i]);else
        NUMBER("--cards",cards) NUMBER("--total",total) NUMBER("--frames",frames) NUMBER("--blocks",blocks)
        NUMBER("--rate",rate) NUMBER("--tracks",tracks) NUMBER("--notes",notes)
        NUMBER("--master-bpm",master_bpm)
        if(!strcmp(argv[i],"--seek")&&i+1<argc)c.seek=atof(argv[++i]);
        else if(!strcmp(argv[i],"--capture")&&i+1<argc)c.capture=argv[++i];
        else if(!strcmp(argv[i],"--edits"))c.edits=1;
        else if(!strcmp(argv[i],"--silent"))c.silent=1;
        else if(!strcmp(argv[i],"--fx"))c.fx=1;
        else {fprintf(stderr,"Unknown/incomplete option: %s\n",argv[i]);return 2;}
#undef NUMBER
    }
    if(!c.total)c.total=c.cards;
    if(c.cards<1||c.total<c.cards||c.total>1024||c.frames<32||c.frames>2048||c.blocks<2||c.blocks>100000||c.rate<8000||c.rate>192000||c.tracks<2||c.tracks>32||c.tracks%2||c.notes<1||c.notes>5||
       (c.master_bpm && (c.master_bpm<32 || c.master_bpm>255)))return 2;
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER));
    SDL_Window *window=SDL_CreateWindow("Pattern callback benchmark",0,0,640,400,SDL_WINDOW_HIDDEN);assert(window);
    int count=1;
#ifdef TS_PATTERN_COMPARE
    count=2;
#endif
    PatternFixture fixtures[2]={0};PatternTiming timings[2]={0};
    /* Initialize both fixtures before selecting their independent engines. */
    for(int k=0;k<count;++k)fixtures[k]=fixture(&c,window);
    for(int k=0;k<count;++k){
        select_backend(k);arrange(&fixtures[k],&c);
        timings[k].cpu=calloc(c.blocks,sizeof(double));timings[k].wall=calloc(c.blocks,sizeof(double));
        timings[k].hash=UINT64_C(14695981039346656037);assert(timings[k].cpu&&timings[k].wall);
    }
    SDL_AudioSpec want={0},got={0};want.freq=c.rate;want.format=AUDIO_F32SYS;want.channels=8;want.samples=(Uint16)c.frames;want.callback=dummy;
    ts_real_output=ts_native_open(NULL,0,&want,&got,0);assert(ts_real_output);
    size_t values=(size_t)c.frames*8,bytes=values*sizeof(float);
    for(int b=0;b<64;++b)for(int k=0;k<count;++k){select_backend(k);audio_callback(fixtures[k].audio,(Uint8*)fixtures[k].out,(int)bytes);}
    for(int k=0;k<count;++k){if(c.seek>=0)ts_mosaic_seek(fixtures[k].mosaic,c.seek);ts_realtime_diagnostics_reset(&fixtures[k].audio->realtime_diagnostics);}
    FILE *capture=c.capture?fopen(c.capture,"wb"):NULL;if(c.capture)assert(capture);
    double deadline=1000.0*c.frames/c.rate;
    for(int b=0;b<c.blocks;++b){
        for(int pass=0;pass<count;++pass){
            int k=count==1?0:(b&1?pass:1-pass);PatternFixture *f=&fixtures[k];PatternTiming *t=&timings[k];
            select_backend(k);
            if(c.edits&&b%16==0){double at=cpu_seconds();edit(f,&c,window,b);t->control_cpu+=cpu_seconds()-at;}
            double cpu=cpu_seconds();uint64_t wall=SDL_GetPerformanceCounter();
            audio_callback(f->audio,(Uint8*)f->out,(int)bytes);
            t->wall[b]=1000.0*(SDL_GetPerformanceCounter()-wall)/SDL_GetPerformanceFrequency();t->cpu[b]=1000.0*(cpu_seconds()-cpu);
            if(!b){t->first_cpu=t->cpu[b];t->first_wall=t->wall[b];}
            t->sum_cpu+=t->cpu[b];t->sum_wall+=t->wall[b];t->cpu_late+=t->cpu[b]>=deadline;t->wall_late+=t->wall[b]>=deadline;
            for(size_t i=0;i<values;++i){assert(isfinite(f->out[i]));t->energy+=fabs(f->out[i]);}
            t->hash=hash_bytes(t->hash,f->out,bytes);
        }
        if(count==2)assert(!memcmp(fixtures[0].out,fixtures[1].out,bytes));
        if(capture)assert(fwrite(fixtures[0].out,sizeof(float),values,capture)==values);
    }
    if(capture)assert(!fclose(capture));
    puts("backend,cards,total,frames,rate,blocks,tracks,notes,fx,edits,silent,master_bpm,seek,cpu_avg_ms,cpu_p50_ms,cpu_p95_ms,cpu_p99_ms,cpu_max_ms,wall_avg_ms,wall_p95_ms,wall_p99_ms,wall_max_ms,cpu_over,wall_over,first_cpu_ms,first_wall_ms,control_cpu_ms,max_hold_ms,max_wait_ms,control_site,hash");
    for(int k=0;k<count;++k){
        select_backend(k);report(k?"reference":"cached",&c,&fixtures[k],&timings[k]);
        dispose(&fixtures[k]);free(timings[k].cpu);free(timings[k].wall);
    }
    SDL_CloseAudioDevice(ts_real_output);ts_real_output=0;ts_output_userdata=NULL;ts_tapehead_close();
    SDL_DestroyWindow(window);SDL_Quit();return 0;
}
