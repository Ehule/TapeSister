/* Exercise named stereo endpoints through the actual controller/lifecycle and
   callbacks. No physical interface is present in this fixture. */
static const char *mock_insert_names[2][3]={{"Out 1-2 (MOTU M Series)","Out 3-4 (MOTU M Series)","Virtual 8"},
    {"In 1-2 (MOTU M Series)","In 5-6 (MOTU M Series)","Virtual 8 Return"}};
static SDL_AudioSpec mock_insert_specs[2];
static unsigned mock_insert_rate[2],mock_insert_buffer[2],mock_insert_channels;
static int mock_insert_no_metadata, mock_insert_no_enumeration;
static int mock_insert_open_count[2],mock_insert_close_count[2],mock_insert_fail[2];
static int mock_insert_count(int capture){(void)capture;return mock_insert_no_enumeration?-1:3;}
static const char *mock_insert_name(int index,int capture){return mock_insert_names[capture][index];}
static int mock_insert_spec(int index,int capture,SDL_AudioSpec *spec)
{(void)capture;if(mock_insert_no_metadata)return -1;SDL_zero(*spec);spec->channels=index==2?8:2;spec->freq=44100;spec->format=AUDIO_F32SYS;return 0;}
static SDL_AudioDeviceID mock_insert_open(const char *name,int capture,const SDL_AudioSpec *wanted,SDL_AudioSpec *got,int flags)
{
    assert(name && *name);assert(flags&SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    ++mock_insert_open_count[capture];
    if(mock_insert_fail[capture]){SDL_SetError("simulated endpoint unavailable");return 0;}
    *got=*wanted;
    if(mock_insert_channels)got->channels=(Uint8)mock_insert_channels;
    if(mock_insert_rate[capture])got->freq=(int)mock_insert_rate[capture];
    if(mock_insert_buffer[capture])got->samples=(Uint16)mock_insert_buffer[capture];
    mock_insert_specs[capture]=*got;return (SDL_AudioDeviceID)(101+capture);
}
static void mock_insert_close(SDL_AudioDeviceID id){assert(id==101 || id==102);++mock_insert_close_count[id-101];}
static void mock_insert_pause(SDL_AudioDeviceID id,int paused){assert(id==101 || id==102);assert(!paused);}

static void test_insert_devices(void)
{
    InsertDeviceApi saved_api=insert_device_api;
    TsConfig saved_config=ui.config;TsSisterParameters saved_parameters=audio.sister.parameters;
    TsUiState *saved_ui=ts_live_ui;TsAudioEndpoint saved_output=ts_output_endpoint;
    char saved_active[TS_CONFIG_PATH_MAX];snprintf(saved_active,sizeof(saved_active),"%s",ts_active_output);
    insert_device_api=(InsertDeviceApi){mock_insert_count,mock_insert_name,mock_insert_spec,
        mock_insert_open,mock_insert_close,mock_insert_pause};
    ts_live_ui=&ui;
    snprintf(ui.config.audio_output_device,sizeof(ui.config.audio_output_device),"%s",mock_insert_names[0][0]);
    snprintf(ui.config.record_input_device,sizeof(ui.config.record_input_device),"%s",mock_insert_names[1][0]);
    snprintf(ts_active_output,sizeof(ts_active_output),"%s",mock_insert_names[0][0]);
    snprintf(ui.config.insert_send_device,sizeof(ui.config.insert_send_device),"%s",mock_insert_names[0][1]);
    snprintf(ui.config.insert_return_device,sizeof(ui.config.insert_return_device),"%s",mock_insert_names[1][1]);
    ui.config.audio_buffer_frames=256;ui.config.insert=(TsInsertControls){1,0,0,0};
    ts_sister_runtime_set_insert(&audio.sister,&ui.config.insert);
    ts_audio_endpoint_opened(&ts_output_endpoint,ts_real_output,mock_insert_names[0][0],0,44100,2,256,"F32");
    ui.router_open=1;insert_panel_open(&ui);char error[160]="";
    ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));
    assert(!error[0] && insert_endpoints[0].device==101 && insert_endpoints[1].device==102);
    assert(audio.sister.insert.separate_send && atomic_load(&audio.sister.insert.dedicated_return));
    assert(audio.sister.insert.send_channels==2 && atomic_load(&audio.sister.insert.input_channels)==2);
    assert(ts_insert_send_available(&audio.sister.insert) && !ts_insert_reserved_port(&audio.sister.insert));
    /* Dedicated RETURN neither consumes nor reserves the ordinary live input. */
    float unrelated[2]={.8f,-.6f};
    TsStereoFrame ext=ts_insert_external_frame(&audio.sister.insert,unrelated,2,TS_INPUT_CHANNEL_STEREO);
    assert(ext.l==.8f && ext.r==-.6f);
    unsigned cursor=atomic_load(&audio.sister.insert.return_monitor.write_index);
    ts_insert_capture(&audio.sister.insert,unrelated,1,2);
    assert(atomic_load(&audio.sister.insert.return_monitor.write_index)==cursor);
    ts_insert_capture_prepare(&audio.sister.insert,0,0,0);
    assert(atomic_load(&audio.sister.insert.input_channels)==2);
    /* Local pair 1/2 remains usable on an endpoint whose physical name is 5-6. */
    assert(router_mouse(SDL_MOUSEBUTTONDOWN,350,158));assert(ui.insert_return_choice==0);
    assert(router_mouse(SDL_MOUSEBUTTONUP,350,158));
    reset_route(0);ts_sister_runtime_disable(&audio.sister);
    TsSisterParameters p=audio.sister.parameters;p.prism.enabled=0;p.fx.enabled=0;
    ts_sister_runtime_set_parameters(&audio.sister,&p);
    ui.config.router.solo=TS_ROUTER_INSERT+1;router_commit(device,&audio,&ui);
    click_note(0,0);float master[512],sent[512]={0},returned[512];double energy=0;
    for(int block=0;block<48;++block) {
        for(int i=0;i<256;++i){returned[i*2]=-sent[i*2+1]*.5f;returned[i*2+1]=sent[i*2]*.75f;}
        mock_insert_specs[1].callback(mock_insert_specs[1].userdata,(Uint8 *)returned,sizeof(returned));
        audio_callback(&audio,(Uint8 *)master,sizeof(master));
        mock_insert_specs[0].callback(mock_insert_specs[0].userdata,(Uint8 *)sent,sizeof(sent));
        for(int i=0;i<512;++i){assert(isfinite(master[i]) && isfinite(sent[i]));energy+=fabs(master[i]);}
    }
    assert(energy>.01);
    /* The callback's actual burst size, not CFG's request, sizes both bridges. */
    assert(atomic_load(&audio.sister.insert.output_buffer_frames)==256);
    assert(atomic_load(&audio.sister.insert.return_monitor.prime_frames)==512);
    ts_audio_insert_diagnostics(&audio,&ui,0);
    assert(ui.insert_rates[0]==(unsigned)mock_insert_specs[0].freq);
    assert(ui.insert_rates[1]==(unsigned)mock_insert_specs[1].freq);
    assert(ui.insert_buffers[1]==mock_insert_specs[1].samples);
    insert_controller_shot("TS_TEST_INSERT_DEVICES_PPM");
    int opens=mock_insert_open_count[0]+mock_insert_open_count[1];
    ts_audio_insert_devices_sync(&audio,&ui.config,0,error,sizeof(error));
    assert(mock_insert_open_count[0]+mock_insert_open_count[1]==opens);
    /* Unplug SEND: active Insert goes silent, choices persist, no default retry. */
    SDL_Event event={0};event.type=SDL_AUDIODEVICEREMOVED;event.adevice.which=101;
    ts_audio_insert_device_event(&event);assert(!ts_insert_send_available(&audio.sister.insert));
    assert(ts_real_output==device && ui.config.insert_send_device[0]);
    ts_audio_insert_devices_sync(&audio,&ui.config,0,error,sizeof(error));
    assert(mock_insert_open_count[0]+mock_insert_open_count[1]==opens);
    event.type=SDL_AUDIODEVICEADDED;ts_audio_insert_device_event(&event);
    ts_audio_insert_devices_sync(&audio,&ui.config,0,error,sizeof(error));
    assert(ts_insert_send_available(&audio.sister.insert));
    /* Explicit open failure never substitutes another endpoint. */
    mock_insert_fail[1]=1;ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));
    assert(!insert_endpoints[1].device && !atomic_load(&audio.sister.insert.input_channels) && error[0]);
    mock_insert_fail[1]=0;ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));assert(!error[0]);
    snprintf(ui.config.insert_send_device,sizeof(ui.config.insert_send_device),"%s",mock_insert_names[0][0]);
    ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));
    assert(!insert_endpoints[0].device && strstr(error,"MATCHES MASTER"));
    /* Device selection resets local pairs; a separate 8-channel device offers 7/8. */
    snprintf(ui.insert_send_device,sizeof(ui.insert_send_device),"%s",mock_insert_names[0][1]);
    ts_audio_insert_cycle_device(&ui,0,1);assert(!strcmp(ui.insert_send_device,"Virtual 8"));
    ui.insert_send_choice=3;assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,158));assert(ui.insert_send_choice==4);
    assert(router_mouse(SDL_MOUSEBUTTONUP,40,158));
    /* Choosing the shared capture by name reuses it rather than opening it twice. */
    snprintf(ui.config.insert_return_device,sizeof(ui.config.insert_return_device),"%s",ui.config.record_input_device);
    ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));
    assert(!insert_endpoints[1].device && !atomic_load(&audio.sister.insert.dedicated_return));
    /* Missing/unsupported pre-open metadata must not block a valid endpoint. */
    snprintf(ui.config.insert_send_device,sizeof(ui.config.insert_send_device),"%s",mock_insert_names[0][1]);
    snprintf(ui.config.insert_return_device,sizeof(ui.config.insert_return_device),"%s",mock_insert_names[1][1]);
    mock_insert_no_metadata=1;
    ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));
    assert(!error[0] && insert_endpoints[0].device && insert_endpoints[1].device);
    mock_insert_no_metadata=0;mock_insert_channels=16;
    ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));
    assert(!error[0] && audio.sister.insert.send_channels==16);
    assert(atomic_load(&audio.sister.insert.input_channels)==16);
    snprintf(ui.insert_return_device,sizeof(ui.insert_return_device),"%s",mock_insert_names[1][1]);
    assert(ts_audio_insert_choice_channels(&ui,1)==8);
    mock_insert_channels=0;mock_insert_no_enumeration=1;
    ts_audio_insert_cycle_device(&ui,0,1);assert(!ui.insert_send_device[0]);
    mock_insert_no_enumeration=0;
    /* Negotiated rates/buffers can differ from the request and each other. */
    snprintf(ui.config.insert_send_device,sizeof(ui.config.insert_send_device),"%s",mock_insert_names[0][1]);
    snprintf(ui.config.insert_return_device,sizeof(ui.config.insert_return_device),"%s",mock_insert_names[1][1]);
    mock_insert_rate[0]=48000;mock_insert_rate[1]=96000;
    mock_insert_buffer[0]=128;mock_insert_buffer[1]=256;
    ts_audio_insert_devices_sync(&audio,&ui.config,1,error,sizeof(error));assert(!error[0]);
    ts_audio_insert_diagnostics(&audio,&ui,1);
    assert(ui.insert_rates[0]==48000 && ui.insert_rates[1]==96000);
    assert(ui.insert_buffers[0]==128 && ui.insert_buffers[1]==256);
    assert(!ui.insert_gaps[0] && !ui.insert_gaps[1]);
    memset(mock_insert_rate,0,sizeof(mock_insert_rate));memset(mock_insert_buffer,0,sizeof(mock_insert_buffer));
    ts_audio_insert_devices_suspend();insert_audio_owner=NULL;insert_device_api=saved_api;
    ts_output_endpoint=saved_output;snprintf(ts_active_output,sizeof(ts_active_output),"%s",saved_active);
    ts_live_ui=saved_ui;ui.config=saved_config;ui.insert_device_status[0]=0;
    ts_insert_io_mode(&audio.sister.insert,0,0);ts_sister_runtime_set_insert(&audio.sister,&ui.config.insert);
    ts_sister_runtime_set_parameters(&audio.sister,&saved_parameters);
    ui.router_open=ui.insert_open=0;reset_route(0);router_commit(device,&audio,&ui);
}
