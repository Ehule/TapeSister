/* Spacebar's three canvas audition paths must use the same matrix bus as
   QWERTY notes. External browser previews retain their own audition path. */
static void tile_audition_routing(void)
{
    SDL_AudioSpec spec={0};spec.freq=48000;spec.format=AUDIO_F32SYS;spec.channels=4;spec.samples=512;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);
    ts_real_output=device;
    TsMatrixControls saved=a.sister.matrix.controls,c;ts_matrix_default(&c);c.enabled=1;
    assert(ts_matrix_connect(&c,TS_MATRIX_TILES,TS_MATRIX_MAIN,0)==1);
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    TsSourceRoute saved_route=bank.bank[0].output_route;bank.bank[0].output_route=(TsSourceRoute){0};
    u.audition_source=TS_AUDITION_CURRENT;u.config.voice_attack_ms=0;
    double energy[4];
    begin_note(device,&a,&u,&bank,12,48000,0);render(energy);
    for(int ch=0;ch<4;++ch)assert(energy[ch]==0);
    stop_all_force(device,&a,&u);
    bank.has_playhead=1;bank.playhead_frame=0;
    assert(ts_capture_arm_channels(&a.capture,1,2048,48000,2,error,sizeof(error)));
    assert(ts_capture_set_source(&a.capture,0,error,sizeof(error)));
    assert(ts_capture_trigger(&a.capture,error,sizeof(error)));
    begin_playhead_audition(device,&a,&u,&bank,48000);render(energy);
    for(int ch=0;ch<4;++ch)assert(energy[ch]==0);
    assert(a.capture.state==TS_CAPTURE_COMPLETED && a.capture.recorded_frames==2048);
    for(int f=64;f<2048;++f) {
        assert(fabsf(a.capture.buffer[f*2]-.12f)<.00001f);
        assert(fabsf(a.capture.buffer[f*2+1]-.24f)<.00001f);
    }
    ts_capture_free(&a.capture);ts_capture_init(&a.capture);
    stop_all_force(device,&a,&u);
    /* A non-main bus destination must carry all tile audition ranges. */
    assert(ts_matrix_connect(&c,TS_MATRIX_TILES,ts_matrix_output_destination(1),1000)==1);
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    begin_note(device,&a,&u,&bank,12,48000,0);render(energy);
    assert(energy[0]==0 && energy[1]==0 && energy[2]>1 && energy[3]>energy[2]);
    stop_all_force(device,&a,&u);
    bank.has_selection=1;bank.selection_first=0;bank.selection_last=8192;
    const TsAuditionRange ranges[]={TS_AUDITION_ALL,TS_AUDITION_SELECTION,TS_AUDITION_LOOP};
    for(unsigned i=0;i<sizeof(ranges)/sizeof(ranges[0]);++i) {
        begin_audition(device,&a,&u,&bank,ranges[i],1,48000);render(energy);
        assert(energy[0]==0 && energy[1]==0 && energy[2]>1 && energy[3]>energy[2]);
        stop_all_force(device,&a,&u);
    }
    /* Explicit tile routes still bypass the bus; an empty route is silent. */
    bank.bank[0].output_route=(TsSourceRoute){.matrix_enabled=1};
    bank.bank[0].output_route.matrix_gain[ts_matrix_output_destination(1)]=1000;
    assert(ts_matrix_connect(&c,TS_MATRIX_TILES,ts_matrix_output_destination(1),0)==1);
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    begin_playhead_audition(device,&a,&u,&bank,48000);render(energy);
    assert(energy[0]==0 && energy[1]==0 && energy[2]>1 && energy[3]>energy[2]);
    stop_all_force(device,&a,&u);
    bank.bank[0].output_route=(TsSourceRoute){.matrix_enabled=1};
    begin_playhead_audition(device,&a,&u,&bank,48000);render(energy);
    for(int ch=0;ch<4;++ch)assert(energy[ch]==0);
    /* Browser audio must not inherit the preceding tile audition's bus. */
    a.browser_voice=(TsNoteVoice){.sample=&bank.current,.range_last=8192,
        .step=1,.pitch=1,.gain=1,.direction=1,.active=1};
    render(energy);assert(energy[0]>1 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    memset(&a.browser_voice,0,sizeof(a.browser_voice));stop_all_force(device,&a,&u);
    /* The ordinary stereo preview still works with the matrix disabled. */
    bank.bank[0].output_route=(TsSourceRoute){0};c.enabled=0;
    assert(ts_matrix_set(&a.sister.matrix,&c,48000));
    begin_playhead_audition(device,&a,&u,&bank,48000);render(energy);
    assert(energy[0]>1 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    stop_all_force(device,&a,&u);
    bank.bank[0].output_route=saved_route;bank.has_selection=bank.has_playhead=0;
    assert(ts_matrix_set(&a.sister.matrix,&saved,48000));
    ts_real_output=0;SDL_CloseAudioDevice(device);
}
