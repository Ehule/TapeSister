static atomic_int snapshot_done;
static int snapshot_writer(void *unused)
{
    (void)unused;
    for(unsigned frame=1;frame<=2000;++frame) {
        while(atomic_load_explicit(&audio.matrix_activity.state,memory_order_acquire)!=1)SDL_Delay(0);
        audio.sister.processed_frames=frame;
        audio.sister.matrix.output_peak[0]=(float)frame;
        audio.sister.matrix.hardware_peak[0]=(float)frame;
        matrix_audio_publish(&audio);
    }
    atomic_store_explicit(&snapshot_done,1,memory_order_release);return 0;
}
static void snapshot_delivery(void)
{
    /* A synthetic available endpoint lets the UI exercise its running-device
       path. No native lock should ever be called by passive refresh. */
    assert(!ts_real_output);ts_real_output=123;
    ts_output_userdata=&audio;audio.realtime_diagnostics_enabled=1;
    audio.realtime_counter_frequency=SDL_GetPerformanceFrequency();
    ts_realtime_diagnostics_init(&audio.realtime_diagnostics);
    uint64_t frame=audio.sister.processed_frames;
    float peak=audio.sister.matrix.output_peak[0];
    audio.mosaic=pages.mosaic;
    atomic_store(&audio.matrix_activity.state,0);matrix_window.activity_ready=0;matrix_window.activity_frame=0;
    SDL_Thread *thread=SDL_CreateThread(snapshot_writer,"matrix snapshots",NULL);assert(thread);
    Uint32 start=SDL_GetTicks();unsigned received=0;
    while(!atomic_load_explicit(&snapshot_done,memory_order_acquire) || received<2000) {
        mx_capture_activity(TS_AUDIO_LOGICAL_OUTPUT,&audio,&pages,&bank,50);
        received=(unsigned)matrix_window.activity_frame;
        if(received)assert(matrix_window.peaks[0]==(float)received);
        assert(SDL_GetTicks()-start<20000);
        SDL_Delay(0);
    }
    SDL_WaitThread(thread,NULL);
    assert(!atomic_load(&audio.realtime_diagnostics.control_count));
    /* READY data stays immutable even if audio continues before the UI reads. */
    audio.matrix_activity.requested_epoch=mx_activity_epoch;
    atomic_store_explicit(&audio.matrix_activity.state,1,memory_order_release);
    matrix_audio_publish(&audio);
    static MatrixActivitySnapshot saved;saved=audio.matrix_activity.value;
    ++audio.sister.processed_frames;matrix_audio_publish(&audio);
    assert(!memcmp(&saved,&audio.matrix_activity.value,sizeof(saved)));
    /* An edit supersedes a previously requested control snapshot. */
    int enabled=matrix_window.controls.enabled;
    ++mx_activity_epoch;matrix_window.controls.enabled=!enabled;
    mx_capture_activity(TS_AUDIO_LOGICAL_OUTPUT,&audio,&pages,&bank,50);
    assert(matrix_window.controls.enabled==!enabled);matrix_window.controls.enabled=enabled;
    /* Reused card slots cannot inherit an earlier card's meter. */
    TsMosaic *old=matrix_window.mosaic,*scene=ts_mosaic_create();assert(scene);
    matrix_window.mosaic=audio.mosaic=scene;scene->events[0].id=111;
    audio.mixer.program_gain=audio.sister.performance_gain_current[2]=1;
    scene->voices[0].output_route.activity_source=.75f;
    audio.matrix_activity.requested_epoch=mx_activity_epoch;
    atomic_store(&audio.matrix_activity.state,1);matrix_audio_publish(&audio);
    assert(audio.matrix_activity.value.card_source[0]==.75f);
    scene->events[0].id=222;
    mx_capture_activity(TS_AUDIO_LOGICAL_OUTPUT,&audio,&pages,&bank,50);
    assert(!matrix_window.card_source[0]);
    matrix_window.mosaic=old;audio.mosaic=pages.mosaic;ts_mosaic_free(scene);
    /* Paused output still refreshes controls without waiting for a callback. */
    ts_output_paused=1;audio.sister.matrix.controls.enabled=!enabled;
    mx_capture_activity(TS_AUDIO_LOGICAL_OUTPUT,&audio,&pages,&bank,50);
    assert(matrix_window.controls.enabled==!enabled);
    audio.sister.matrix.controls.enabled=matrix_window.controls.enabled=enabled;ts_output_paused=0;
    atomic_store(&audio.matrix_activity.state,0);matrix_window.activity_ready=0;
    audio.sister.processed_frames=frame;audio.sister.matrix.output_peak[0]=peak;
    audio.sister.matrix.hardware_peak[0]=0;
    audio.realtime_diagnostics_enabled=0;ts_output_userdata=NULL;ts_real_output=0;
}
