/* Exercise controller locks against callbacks on another thread. The older
   mute tests invoked the renderer synchronously with device=0. This uses the
   production ASIO adapter and its bounded lock retries, with a fake driver. */
extern void fixture_asio_tick(float *out,float *in);
typedef struct {
    SDL_sem *request,*done;
    atomic_int quit,blocks;
    double energy;
} MosaicAsioFixture;

static void mosaic_asio_render(void *userdata,Uint8 *stream,int bytes)
{
    TsMosaic *m=((AudioState *)userdata)->mosaic;float *out=(float *)stream;
    int frames=bytes/(4*(int)sizeof(float));memset(stream,0,(size_t)bytes);
    ts_mosaic_begin_block(m,(unsigned)frames);
    for(int i=0;i<frames;++i) {
        TsStereoFrame f=ts_mosaic_read_routed(m,48000,NULL);
        out[i*4]=f.l;out[i*4+1]=f.r;
    }
}
static int mosaic_asio_thread(void *userdata)
{
    MosaicAsioFixture *f=userdata;float out[128*4],in[128*16]={0};
    for(;;) {
        SDL_SemWait(f->request);if(atomic_load(&f->quit))break;
        int blocks=atomic_load(&f->blocks);
        for(int b=0;b<blocks;++b) {
            fixture_asio_tick(out,in);f->energy=0;
            for(int i=0;i<128*4;++i){assert(isfinite(out[i]));f->energy+=fabs(out[i]);}
        }
        SDL_SemPost(f->done);
    }
    return 0;
}
static void mosaic_asio_key(SDL_Window *window,MosaicController *c,TsInstrument *bank,
    PortalController *portal,SisterWindow *sister,TransformController *transform,
    SDL_Keycode key,SDL_Keymod mod)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.sym=key;e.key.keysym.mod=mod;
    assert(mosaic_event(&e,window,TS_AUDIO_LOGICAL_OUTPUT,&audio,&ui,bank,c,portal,sister,transform,48000));
    e.type=SDL_KEYUP;
    assert(mosaic_event(&e,window,TS_AUDIO_LOGICAL_OUTPUT,&audio,&ui,bank,c,portal,sister,transform,48000));
    assert(!ts_control_timing.depth);
}
static void test_mosaic_asio_mute(SDL_Window *window,const char *path)
{
    MosaicController *c=calloc(1,sizeof(*c));TsInstrument *bank=calloc(1,sizeof(*bank));
    PortalController *portal=calloc(1,sizeof(*portal));SisterWindow *sister=calloc(1,sizeof(*sister));
    TransformController *transform=calloc(1,sizeof(*transform));assert(c && bank && portal && sister && transform);
    ts_instrument_init(bank);TsMosaic *m=ts_mosaic_create();assert(m);
    audio.mosaic=ui.mosaic=m;ui.config_open=0;ui.mosaic_open=1;
    TsSample sample={0};sample.sample_rate=48000;sample.channels=1;sample.frames=128;
    sample.data=malloc(sample.frames*sizeof(float));assert(sample.data);
    char error[160];TsMosaicSource *source[2];
    for(int n=0;n<2;++n) {
        for(size_t i=0;i<sample.frames;++i)sample.data[i]=.125f*(n+1);
        source[n]=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source[n]);
    }
    ts_sample_free(&sample);
    for(int i=0;i<1020;++i) {
        TsMosaicEvent *e=ts_mosaic_add(m,source[i%2],0,0);assert(e);
        e->duration=1;e->gain=.001f;
    }
    ts_realtime_diagnostics_init(&audio.realtime_diagnostics);
    audio.realtime_diagnostics_enabled=1;audio.realtime_counter_frequency=SDL_GetPerformanceFrequency();
    ts_output_desired.callback=mosaic_asio_render;
    ui.config.audio_backend=TS_AUDIO_BACKEND_ASIO;
    ui.config.audio_output_device[0]=ui.config.record_input_device[0]=0;
    ui.config.audio_output_range=(TsAudioChannelRange){1,4};ui.config.audio_input_range=(TsAudioChannelRange){1,0};
    assert(ts_audio_main_config_save(&ui.config,path,error,sizeof(error)));
    mosaic_poll(TS_AUDIO_LOGICAL_OUTPUT,&ui,bank,c);
    ts_audio_lock_device(TS_AUDIO_LOGICAL_OUTPUT);m->playing=m->repeat=1;ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    MosaicAsioFixture f={0};atomic_init(&f.quit,0);atomic_init(&f.blocks,16);
    f.request=SDL_CreateSemaphore(0);f.done=SDL_CreateSemaphore(0);assert(f.request && f.done);
    SDL_Thread *thread=SDL_CreateThread(mosaic_asio_thread,"mosaic-asio",&f);assert(thread);
    for(int cycle=0;cycle<3;++cycle) {
        mosaic_select_only(&ui,m->events[0].id);
        for(int stage=0;stage<4;++stage) {
            /* Start callbacks first so the real UI mutations can overlap. */
            atomic_store(&f.blocks,stage==2?400:16);SDL_SemPost(f.request);
            if(stage==2)mosaic_asio_key(window,c,bank,portal,sister,transform,SDLK_a,KMOD_CTRL);
            if(stage)mosaic_asio_key(window,c,bank,portal,sister,transform,SDLK_m,KMOD_SHIFT);
            assert(SDL_SemWaitTimeout(f.done,10000)==0);
            /* Render a settled block after the UI action even if a fast worker
               finished before it. Four buffers also complete the mute fade. */
            ts_audio_lock_device(TS_AUDIO_LOGICAL_OUTPUT);double before=m->time;ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
            atomic_store(&f.blocks,4);SDL_SemPost(f.request);assert(SDL_SemWaitTimeout(f.done,10000)==0);
            assert(stage==2?f.energy==0:f.energy>0);
            ts_audio_lock_device(TS_AUDIO_LOGICAL_OUTPUT);
            double expected=fmod(before+512.0/48000,1);
            double delta=fabs(m->time-expected);
            assert(m->playing && fmin(delta,fabs(delta-1))<1e-8);
            for(int i=0;i<1020;++i)assert(m->events[i].muted==(stage==2 || (stage==1 && i%2==0)));
            ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
        }
    }
    atomic_store(&f.quit,1);SDL_SemPost(f.request);SDL_WaitThread(thread,NULL);
    SDL_DestroySemaphore(f.request);SDL_DestroySemaphore(f.done);
    assert(!ts_control_timing.depth);
    ts_audio_session_close();ts_output_desired.callback=silence;
    ui.mosaic=audio.mosaic=NULL;ts_mosaic_free(m);ts_instrument_free(bank);
    free(c);free(bank);free(portal);free(sister);free(transform);
    puts("Mosaic ASIO: threaded 1020-card mute/unmute, key releases and loop clocks passed");
}
