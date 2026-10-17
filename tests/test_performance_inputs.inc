static void perf_input_click(int x,int y,int button)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=performance_transport.id;
    e.button.x=x;e.button.y=y;e.button.button=button;e.button.clicks=1;
    assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));
    e.type=SDL_MOUSEBUTTONUP;assert(performance_event(&e,window,0,&a,&u,&pages,&bank,NULL,48000));
}
static void perf_input_shot(const char *directory,const char *name)
{
    if(!directory)return;
    performance_transport.pressed=performance_transport.flash=0;
    performance_transport.painted=SDL_GetTicks()-100;performance_update(0,&a);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.bmp",directory,name);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,PERF_W,PERF_H,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
    assert(!SDL_RenderReadPixels(performance_transport.renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch));
    assert(!SDL_SaveBMP(s,path));SDL_FreeSurface(s);
}
static void test_performance_inputs(const char *directory)
{
    static ExternalInputState input;ts_performance_inputs_init(&input.performance);
    ts_input_monitor_init(&input.monitor);ts_input_activity_init(&input.activity);
    ts_external_recorder_init(&input.recorder);
    a.performance_inputs=&input.performance;performance_show(&u,&a);
    ts_performance_mixer_default(&a.sister.performance_mixer);
    input.channels=26;input.sample_rate=48000;input.range=(TsAudioChannelRange){1,0};
    ts_performance_inputs_prepare(&input.performance,26,48000,512);
    performance_transport.input_channels=26;performance_transport.input_range=input.range;
    snprintf(performance_transport.input_label,sizeof(performance_transport.input_label),"26-CHANNEL DRIVER TEST");
    assert(!performance_transport.tuner_on);
    perf_input_click(632,98,SDL_BUTTON_LEFT);assert(a.sister.performance_mixer.input_enabled[0]);
    perf_input_click(724,98,SDL_BUTTON_LEFT);assert(a.sister.performance_mixer.input_gain[0]==0);
    perf_input_click(790,98,SDL_BUTTON_RIGHT);assert(a.sister.performance_mixer.input_gain[0]==1);
    uint64_t hash=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    performance_input_gain(0,&a,0,.5f);assert(hash!=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence));
    for(int i=0;i<4;++i)perf_input_click(920,280,SDL_BUTTON_LEFT);
    assert(performance_transport.input_page==4);
    perf_input_click(632,128,SDL_BUTTON_LEFT);assert(a.sister.performance_mixer.input_enabled[25]);
    performance_input_gain(0,&a,25,0); /* Tuner must still see raw input. */
    performance_transport.tuner_channel=24;perf_input_click(920,350,SDL_BUTTON_LEFT);
    assert(performance_transport.tuner_channel==25);
    perf_input_click(650,350,SDL_BUTTON_LEFT);assert(performance_transport.tuner_on);
    float capture[256*26];
    for(int block=0;block<70;++block) {
        for(int f=0;f<256;++f)for(int ch=0;ch<26;++ch)
            capture[f*26+ch]=ch==25?.3f*sinf((float)((block*256+f)*2*M_PI*220*pow(2,7./1200)/48000)):0;
        external_input_callback(&input,(Uint8 *)capture,sizeof(capture));
        ts_performance_inputs_analyze(&input.performance,&performance_transport.tuner);
    }
    assert(performance_transport.tuner.reading.valid && performance_transport.tuner.reading.note==57);
    assert(fabsf(performance_transport.tuner.reading.cents-7)<2);
    performance_transport.tuner_tick=performance_transport.tuner_signal=SDL_GetTicks();
    perf_input_shot(directory,"performance-inputs-wide");
    /* Preserve a missing source choice rather than silently tune another port. */
    ts_performance_inputs_prepare(&input.performance,6,48000,512);
    performance_transport.input_channels=6;performance_transport.input_page=0;
    assert(!performance_input_available(25));
    perf_input_click(920,350,SDL_BUTTON_LEFT);assert(performance_input_available(performance_transport.tuner_channel));
    performance_transport.tuner_channel=0;ts_performance_inputs_tuner(&input.performance,1,0);
    snprintf(performance_transport.input_label,sizeof(performance_transport.input_label),"6-CHANNEL INPUT TEST / SYNTHETIC A3");
    a.sister.performance_mixer.input_enabled[1]=1;
    a.sister.performance_mixer.input_gain[0]=a.sister.performance_mixer.input_gain[1]=.8f;
    ts_performance_inputs_set(&input.performance,&a.sister.performance_mixer);
    for(int block=0;block<70;++block) {
        for(int f=0;f<256;++f)for(int ch=0;ch<6;++ch)
            capture[f*6+ch]=ch==0?.3f*sinf((float)((block*256+f)*2*M_PI*220*pow(2,7./1200)/48000)):0;
        ts_performance_inputs_capture(&input.performance,capture,256,6,48000,1);
        ts_performance_inputs_analyze(&input.performance,&performance_transport.tuner);
    }
    performance_transport.tuner_tick=performance_transport.tuner_signal=SDL_GetTicks();
    perf_input_shot(directory,"performance-inputs-tuner");
    perf_input_click(650,350,SDL_BUTTON_LEFT);assert(!performance_transport.tuner_on && !(atomic_load(&input.performance.tuner_request)&255));
    perf_input_shot(directory,"performance-inputs-tuner-off");
    /* The actual output callback receives live inputs through MAIN MIX, with
       no dependence on a transport or legacy record-monitor switch. */
    a.mosaic->playing=0;ts_tapehead_stop();ts_note_bank_clear(&a.notes);
    TsMatrixControls saved=a.sister.matrix.controls,direct;ts_matrix_default(&direct);direct.enabled=1;
    for(int i=0;i<TS_MATRIX_ROWS;++i)memset(direct.row[i].matrix_gain,0,sizeof(direct.row[i].matrix_gain));
    direct.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_NODES]=1000;
    assert(ts_matrix_set(&a.sister.matrix,&direct,48000));
    ts_performance_mixer_default(&a.sister.performance_mixer);a.sister.performance_mixer.input_enabled[0]=1;
    ts_performance_inputs_set(&input.performance,&a.sister.performance_mixer);
    float raw[512*6]={0},out[512*4];for(int f=0;f<512;++f)raw[f*6]=.25f;
    a.output_device_channels=4;
    ts_insert_duplex_block(&a.sister.insert,1,raw,512,6);audio_callback(&a,(Uint8 *)out,sizeof(out));
    assert(out[2000]>.05f && fabsf(out[2000]-out[2001])<1e-5f && out[2002]==0 && out[2003]==0);
    performance_input_gain(0,&a,0,0);
    ts_insert_duplex_block(&a.sister.insert,1,raw,512,6);audio_callback(&a,(Uint8 *)out,sizeof(out));
    ts_insert_duplex_block(&a.sister.insert,1,raw,512,6);audio_callback(&a,(Uint8 *)out,sizeof(out));
    assert(fabsf(out[2000])<1e-6f && fabsf(out[2001])<1e-6f);
    direct.row[TS_MATRIX_NODES].matrix_gain[TS_MATRIX_MAIN]=1000;
    assert(ts_matrix_set(&a.sister.matrix,&direct,48000));
    ts_insert_duplex_block(&a.sister.insert,1,raw,512,6);audio_callback(&a,(Uint8 *)out,sizeof(out));
    assert(out[2000]>.05f); /* F9 raw input routes are independent of the live mixer. */
    ts_insert_duplex_block(&a.sister.insert,0,NULL,0,0);assert(ts_matrix_set(&a.sister.matrix,&saved,48000));
    perf_input_click(650,350,SDL_BUTTON_LEFT);assert(performance_transport.tuner_on);
    SDL_Event close={0};close.type=SDL_WINDOWEVENT;close.window.windowID=performance_transport.id;close.window.event=SDL_WINDOWEVENT_CLOSE;
    assert(performance_event(&close,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(!performance_transport.tuner_on && !(atomic_load(&input.performance.tuner_request)&255));
    a.performance_inputs=NULL;
}
