static SDL_atomic_t save_test_callbacks;
static unsigned save_test_timeout=15000;
static double save_test_active_peak;
static void SDLCALL save_test_callback(void *data,Uint8 *stream,int length)
{audio_callback(data,stream,length);SDL_AtomicAdd(&save_test_callbacks,1);}
static void worker_poll(SDL_AudioDeviceID device)
{
    run_pending_file_operation(device,&audio,&ui,&bank,&selection,&importer,&pages,&record,0,&file,NULL);
}
static void worker_finish(SDL_AudioDeviceID device)
{
    Uint32 until=SDL_GetTicks()+save_test_timeout;static TsFramebuffer fb;
    while(project_save_worker && !SDL_TICKS_PASSED(SDL_GetTicks(),until)) {
        Uint64 begin=SDL_GetPerformanceCounter();worker_poll(device);ts_ui_render(&fb,&ui,&bank);
        double ms=1000.0*(SDL_GetPerformanceCounter()-begin)/SDL_GetPerformanceFrequency();save_test_active_peak=fmax(save_test_active_peak,ms);
        SDL_Delay(5);
    }
    assert(!project_save_worker && !ui.file_busy && !file.active);
}
static void test_project_save_worker(SDL_Window *window)
{
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(!SDL_InitSubSystem(SDL_INIT_AUDIO));
    SDL_AudioSpec want={0},got={0};want.freq=48000;want.channels=2;want.samples=512;
    want.format=AUDIO_F32SYS;want.callback=save_test_callback;want.userdata=&audio;
    SDL_AudioDeviceID device=ts_audio_open_device(NULL,0,&want,&got,0);assert(device && ts_real_output);
    audio.output_rate=got.freq;audio.output_device_channels=got.channels;
    audio.realtime_counter_frequency=SDL_GetPerformanceFrequency();audio.realtime_diagnostics_enabled=1;
    ts_audio_pause_device(device,0);
    file=(PendingFileOperation){.active=1,.presented=1,.mode=TS_BROWSER_SAVE_RECIPE};
    char name[96];snprintf(name,sizeof(name),"save-worker-%llu.tsr",(unsigned long long)SDL_GetPerformanceCounter());
    assert(ts_sample_pages_bundle_project_path(name,file.path,sizeof(file.path),error,sizeof(error)));
    Uint64 save_started=SDL_GetPerformanceCounter();
    SDL_AtomicSet(&save_test_gate,1);worker_poll(device);
    assert(project_save_worker && ui.file_busy && file.active);
    PendingFileOperation repeated=file;queue_active_project_save(&ui,&repeated);
    assert(!memcmp(&repeated,&file,sizeof(file))); /* repeated Save never starts another job */
    int callbacks=SDL_AtomicGet(&save_test_callbacks);double peak=0;
    static TsFramebuffer fb;
    for(int i=0;i<8;++i) {
        Uint64 begin=SDL_GetPerformanceCounter();worker_poll(device);ts_ui_render(&fb,&ui,&bank);
        double elapsed=1000.0*(SDL_GetPerformanceCounter()-begin)/SDL_GetPerformanceFrequency();peak=fmax(peak,elapsed);
        SDL_Event event;SDL_zero(event);event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_DELETE;
        uint64_t hash=project_hash();assert(project_save_event(&event,window,device,&audio,&ui));assert(project_hash()==hash);
        SDL_Delay(10);
    }
    assert(SDL_AtomicGet(&save_test_callbacks)>callbacks);
    const char *shot=getenv("TS_TEST_SAVE_SCREENSHOT");if(shot)assert(ts_ui_write_ppm(&fb,shot));
    SDL_AtomicSet(&save_test_gate,0);worker_finish(device);
    printf("actual worker initial save: %.3f seconds\n",(double)(SDL_GetPerformanceCounter()-save_started)/SDL_GetPerformanceFrequency());fflush(stdout);
    assert(project_save_completed());assert(ui.saved_state_hash==project_hash());
    FILE *journal=fopen(file.path,"rb");assert(journal);fclose(journal);
    if(save_test_timeout==15000) {
        /* A state change after snapshot capture must remain dirty; saving an
           older snapshot must not authorize Exit or New to discard it. */
        SDL_AtomicSet(&save_test_gate,1);queue_active_project_save(&ui,&file);
        ui.exit_after_save=1;worker_poll(device);assert(project_save_worker);
        audio.sister.selected_preset_modified^=1;
        SDL_AtomicSet(&save_test_gate,0);worker_finish(device);
        assert(ui.project_modified && ui.exit_confirm_open && ui.exit_has_unsaved && !ui.exit_after_save);
        audio.sister.selected_preset_modified^=1;ui.exit_confirm_open=0;ui.project_modified=0;
    }
    /* Close during a second save cancels, joins, and returns to the unsaved
       confirmation. The original valid save remains loadable. */
    ++bank.family_sequence;
    SDL_AtomicSet(&save_test_gate,1);queue_active_project_save(&ui,&file);worker_poll(device);
    assert(project_save_worker);char log_path[2048];snprintf(log_path,sizeof(log_path),"%s",project_save_worker->journal_path);
    SDL_Event close;SDL_zero(close);close.type=SDL_QUIT;assert(project_save_event(&close,window,device,&audio,&ui));
    assert(project_save_worker->close_requested && SDL_AtomicGet(&project_save_worker->cancel));
    SDL_AtomicSet(&save_test_gate,0);worker_finish(device);
    assert(ui.exit_confirm_open && ui.exit_has_unsaved && !ui.exit_after_save);
    journal=fopen(log_path,"rb");assert(journal);char line[4096];int result=0;
    while(fgets(line,sizeof(line),journal))if(strstr(line,"RESULT\tCANCELLED"))result=1;
    fclose(journal);assert(result);ui.exit_confirm_open=0;
    assert(ts_instrument_validate_recipe(ui.project_path,error,sizeof(error)));
    /* Failure after launching a worker also releases the busy/exit flags with
       the browser closed. This used to leave exit-after-save armed. */
    file=(PendingFileOperation){.active=1,.presented=1,.mode=TS_BROWSER_SAVE_RECIPE};
    snprintf(file.path,sizeof(file.path),"/__missing_save_parent__/missing/missing.tsr");
    ui.exit_after_save=1;worker_poll(device);assert(project_save_worker);
    snprintf(log_path,sizeof(log_path),"%s",project_save_worker->journal_path);
    worker_finish(device);assert(!ui.exit_after_save);
    journal=fopen(log_path,"rb");assert(journal);result=0;
    while(fgets(line,sizeof(line),journal))if(strstr(line,"RESULT\tFAILED"))result=1;
    fclose(journal);assert(result);
    TsRealtimeDiagnosticsSnapshot diagnostics;
    assert(ts_realtime_diagnostics_get(&audio.realtime_diagnostics,&diagnostics));
    assert(diagnostics.control_count>=2);
    printf("save locks: max hold %.3f ms, max acquisition wait %.3f ms; site %s\n",
        diagnostics.control_worst_microseconds/1000,diagnostics.control_wait_worst_microseconds/1000,
        diagnostics.control_worst_site?diagnostics.control_worst_site:"none");
    ts_audio_pause_device(device,1);ts_audio_close_device(device);
    printf("save worker: UI peak %.3f ms (during I/O %.3f ms); %d dummy audio callbacks; repeated Save, snapshot identity, Close/cancel and closed-browser failure passed\n",peak,save_test_active_peak,SDL_AtomicGet(&save_test_callbacks));
}

static void profile_project_library(const char *path)
{
    assert(load_instrument(0,&audio,&ui,&bank,&pages,&record,0,path));
    ui.mosaic_open=1;ui.tracker_open=0;ui.mosaic_scroll=0;ui.mosaic_scale=4;
    static TsFramebuffer fb;
    TsSamplePages limited=pages;limited.page_count=1;limited.active_page=0;
    for(int mode=0;mode<2;++mode) {
        double sum=0,peak=0;uint64_t hash=0;
        for(int frame=0;frame<200;++frame) {
            Uint64 start=SDL_GetPerformanceCounter();
            hash=paged_project_fingerprint(mode?&limited:&pages,&bank,&record,&audio.sister,&audio.keyboard_sequence,1);
            ts_ui_render(&fb,&ui,&bank);
            double ms=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
            sum+=ms;peak=fmax(peak,ms);
        }
        printf("Mosaic CPU paint + dirty fingerprint: %s, mean %.3f ms, peak %.3f ms, hash %llu\n",
            mode?"one bank view":"full imported library",sum/200,peak,(unsigned long long)hash);
    }
    Uint64 start=SDL_GetPerformanceCounter();
    begin_exit_confirmation(0,&audio,&ui,&bank,&pages,&record,0);
    printf("large project Close confirmation: %.3f ms\n",1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency());
}
