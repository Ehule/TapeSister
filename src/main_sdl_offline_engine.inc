/* Same callback as live FILE OUT, driven by frames instead of a device clock.
   All globals belonging to the embedded tracker live only in this child. */
static int offline_put(FILE *f,uint32_t v,int bytes)
{ while(bytes--){if(fputc(v&255,f)==EOF)return 0;v>>=8;}return 1; }
static int offline_wav_header(FILE *f,const OfflineOptions *o,uint64_t frames)
{
    uint32_t bytes=(uint32_t)(frames*2*(o->bits/8));int floating=o->bits==32;
    return fwrite("RIFF",1,4,f)==4 && offline_put(f,bytes+(floating?48:36),4) &&
        fwrite("WAVEfmt ",1,8,f)==8 && offline_put(f,16,4) &&
        offline_put(f,floating?3:1,2) && offline_put(f,2,2) && offline_put(f,o->rate,4) &&
        offline_put(f,o->rate*2*(o->bits/8),4) && offline_put(f,2*(o->bits/8),2) && offline_put(f,o->bits,2) &&
        (!floating || (fwrite("fact",1,4,f)==4 && offline_put(f,4,4) && offline_put(f,(uint32_t)frames,4))) &&
        fwrite("data",1,4,f)==4 && offline_put(f,bytes,4);
}
static int offline_parent_alive(unsigned long parent)
{
    if(!parent)return 1;
#ifdef _WIN32
    HANDLE h=OpenProcess(SYNCHRONIZE,FALSE,(DWORD)parent);if(!h)return 0;
    int alive=WaitForSingleObject(h,0)==WAIT_TIMEOUT;CloseHandle(h);return alive;
#else
    return getppid()==(pid_t)parent;
#endif
}
static int offline_render_engine(const OfflineOptions *o,const char *job,unsigned long parent)
{
    static AudioState a;
    static TsUiState u;
    static TsInstrument bank,record;
    static TsSamplePages pages;
    static TsSisterProjectState state;
    SDL_Window *window=NULL;FILE *file=NULL;float *hardware=NULL;
    char error[192]={0};OfflineProgress progress={0};int result=1,initialized=0;
#define OFFLINE_REQUIRE(condition, message) do { if(!(condition)){if(!*error)snprintf(error,sizeof(error),"%s",message);goto finish;} }while(0)
    OFFLINE_REQUIRE(offline_valid(o,error,sizeof(error)),"Invalid render settings.");
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    /* No SDL audio subsystem, sound device, MIDI, live link or visualizer. */
    OFFLINE_REQUIRE(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0,SDL_GetError());initialized=1;
    window=SDL_CreateWindow("Offline",0,0,640,400,SDL_WINDOW_HIDDEN);
    OFFLINE_REQUIRE(window,SDL_GetError());
    ts_ui_init(&u);ts_instrument_init(&bank);ts_instrument_init(&record);
    ts_note_bank_init(&a.notes);ts_performance_init(&a.performance);ts_performance_init(&a.tile_launchers);
    ts_keyboard_sequence_init(&a.keyboard_sequence);ts_tracker_playback_init(&a.tracker);
    ts_audio_mixer_init(&a.mixer);ts_capture_init(&a.capture);ts_sister_runtime_init(&a.sister);
    a.output_rate=(int)o->rate;a.output_device_channels=o->channels;a.output_range=o->output_range;a.speakers=o->speakers;
    a.mixer.program_gain=o->program_gain;a.mixer.master_gain=o->master_gain;
    OFFLINE_REQUIRE(ts_sample_pages_init(&pages,error,sizeof(error)),"Cannot allocate sample pages.");
    TsTapeHeadHost pattern_host={.window=window};
    OFFLINE_REQUIRE(ts_tapehead_init(&pattern_host,o->rate,error,sizeof(error)),"Cannot initialize pattern playback.");
    pages.mosaic=ts_mosaic_create();OFFLINE_REQUIRE(pages.mosaic,"Cannot allocate Mosaic.");a.mosaic=pages.mosaic;
    OFFLINE_REQUIRE(ts_sample_pages_load_project(&pages,&bank,&record,o->project,error,sizeof(error)),"Cannot load project.");
    int present=0;
    OFFLINE_REQUIRE(ts_sister_project_state_load(&state,o->project,o->rate,&present,error,sizeof(error)),"Cannot load processing state.");
    OFFLINE_REQUIRE(ts_sister_runtime_reconfigure(&a.sister,o->rate,2,error,sizeof(error)),"Cannot prepare effects.");
    OFFLINE_REQUIRE(ts_spatial_prepare(&a.sister.spatial,o->rate),"Cannot prepare spatial processor.");
    OFFLINE_REQUIRE(ts_sister_project_state_apply(&state,&a.sister,&bank),"Cannot restore processing state.");
    OFFLINE_REQUIRE(ts_sister_runtime_set_page(&a.sister,pages.active_page,&bank),"Cannot prepare Sister tiles.");
    ts_sister_runtime_configure_limiter(&a.sister,o->limiter_enabled,o->limiter_ceiling,o->limiter_lookahead,o->limiter_release);
    if(o->sister_enabled) {
        OFFLINE_REQUIRE(ts_sister_runtime_enable(&a.sister,o->rate,2,2,state.parameters.buffer_seconds,error,sizeof(error)),"Cannot enable Sister Machine.");
        ts_sister_runtime_set_rolling(&a.sister,o->sister_rolling);
    }
    a.sister.master_output_gain=(TsSisterRamp){.current=o->output_gain,.target=o->output_gain};
    memcpy(a.sister.performance_gain_current,state.performance_mixer.gain,sizeof(a.sister.performance_gain_current));
    /* A fresh render has empty effect histories. Restored controls start at
       their captured levels, avoiding accidental multi-minute fade-ins. */
    ts_sister_post_fx_sync_controls(&a.sister.post_fx,&state.parameters.fx);
    if(o->fx_snapshot) {
        memcpy(a.sister.post_fx.slot,o->fx_slot,sizeof(o->fx_slot));
        a.sister.post_fx.master_engage=o->fx_master;
    }
    if(o->eq_run){ts_master_eq_morph_recall(&a.sister.master_eq,0);ts_master_eq_morph_start(&a.sister.master_eq);}
    if(o->router_run)ts_router_sequence_play(&a.sister.router);
    for(int i=0;i<TS_ROUTER_COUNT;++i)if(o->router_timer[i])ts_router_timer_start(&a.sister.router,i,o->router_timer[i]);
    if(o->prism_run) {TsPrismControls c=a.sister.prism.controls;c.matrix_run=1;ts_prism_set_controls(&a.sister.prism,&c);}
    if(o->spatial_morph || o->spatial_rise) {
        TsSpatialControls c=a.sister.spatial.controls;c.morph_trigger=o->spatial_morph;c.rise_trigger=o->spatial_rise;
        ts_spatial_set(&a.sister.spatial,&c);
    }
    /* The project may contain running tracker jumps/loops and Mosaic repeat.
       Duration, not guessed pattern lengths, is the authoritative stop time. */
    if(o->sources&OFFLINE_MOSAIC) {
        OFFLINE_REQUIRE(ts_mosaic_end(a.mosaic)>0,"Mosaic has no cards.");
        ts_mosaic_seek(a.mosaic,0);a.mosaic->playing=1;
    }
    if(o->sources&OFFLINE_TRACKER) {
        TsTapeHeadHost host={.window=window};
        OFFLINE_REQUIRE(ts_tapehead_init(&host,o->rate,error,sizeof(error)),"Cannot initialize TrackSister.");
        OFFLINE_REQUIRE(ts_tapehead_sync(&pages,&bank,o->rate,error,sizeof(error)),"Cannot load TrackSister.");
        OFFLINE_REQUIRE(ts_tapehead_transport_locked(2) && ts_tapehead_transport_locked(1),"Cannot start TrackSister.");
    }
    hardware=calloc(512u*o->channels,sizeof(float));OFFLINE_REQUIRE(hardware,"Cannot allocate output block.");
    file=fopen(o->partial,"wbx");OFFLINE_REQUIRE(file,"Cannot create temporary WAV beside destination.");
    uint64_t body=(uint64_t)llround(o->seconds*o->rate),total=body+(uint64_t)llround(o->tail*o->rate);
    OFFLINE_REQUIRE(offline_wav_header(file,o,total),"Cannot write WAV header.");
    progress.total=total;progress.state=1;snprintf(progress.message,sizeof(progress.message),"Rendering native stereo mix...");
    float stereo[1024];Uint32 last=SDL_GetTicks()-100;int stopped=0;
    a.offline_stereo=stereo;
    for(uint64_t at=0;at<total;) {
        unsigned frames=(unsigned)((total-at)>512?512:total-at);
        if(at<body && at+frames>body)frames=(unsigned)(body-at);
        if(at>=body && !stopped){ts_tapehead_stop();a.mosaic->playing=0;ts_mosaic_live_stop(a.mosaic,0);stopped=1;}
        if(SDL_GetTicks()-last>=100) {
            char cancel=0;
            if(!offline_parent_alive(parent) || (job && offline_read_blob(job,"cancel",&cancel,1))) {
                snprintf(error,sizeof(error),"Cancelled.");result=3;goto finish;
            }
            progress.frames=at;if(job)offline_write_blob(job,"progress",&progress,sizeof(progress));last=SDL_GetTicks();
        }
        audio_callback(&a,(Uint8*)hardware,(int)(frames*o->channels*sizeof(float)));
        unsigned char pcm[4096];size_t count=0;
        for(unsigned i=0;i<frames*2;++i) {
            float value=stereo[i];OFFLINE_REQUIRE(isfinite(value),"Non-finite audio encountered; no output published.");
            uint32_t encoded;
            if(o->bits==32)memcpy(&encoded,&value,4);
            else encoded=(uint32_t)(int32_t)llround(fmax(-1,fmin(1,value))*8388607.0);
            for(unsigned b=0;b<o->bits/8;++b){pcm[count++]=(unsigned char)(encoded&255);encoded>>=8;}
        }
        OFFLINE_REQUIRE(fwrite(pcm,1,count,file)==count,"WAV write failed (disk full or destination unavailable).");
        at+=frames;progress.frames=at;
    }
    {int close_error=fclose(file);file=NULL;OFFLINE_REQUIRE(!close_error,"Could not finish WAV.");}
    progress.state=2;snprintf(progress.message,sizeof(progress.message),"Render complete.");result=0;
finish:
    if(file)fclose(file);
    if(result){remove(o->partial);progress.state=result==3?3:-1;snprintf(progress.message,sizeof(progress.message),"%.191s",error);}
    if(job)offline_write_blob(job,"progress",&progress,sizeof(progress));
    else if(result)fprintf(stderr,"%s\n",progress.message);
    free(hardware);ts_tapehead_close();ts_sister_runtime_free(&a.sister);
    ts_mosaic_free(pages.mosaic);pages.mosaic=NULL;ts_sample_pages_free(&pages);
    ts_instrument_free(&bank);ts_instrument_free(&record);
    if(window)SDL_DestroyWindow(window);if(initialized)SDL_Quit();return result;
#undef OFFLINE_REQUIRE
}
static int offline_dispatch(int argc,char **argv)
{
    if(argc<2 || strcmp(argv[1],"--offline-worker"))return -1;
    if(argc!=4)return 2;
    OfflineOptions options;
    if(!offline_read_blob(argv[2],"options",&options,sizeof(options)))return 2;
    return offline_render_engine(&options,argv[2],strtoul(argv[3],NULL,10));
}
