/* Named auxiliary endpoints use the same SDL backend and TsAudioEndpoint
   lifecycle as Master/capture. They never fall back to another device. */
typedef struct {
    int (*count)(int);
    const char *(*name)(int,int);
    int (*spec)(int,int,SDL_AudioSpec *);
    SDL_AudioDeviceID (*open)(const char *,int,const SDL_AudioSpec *,SDL_AudioSpec *,int);
    void (*close)(SDL_AudioDeviceID);
    void (*pause)(SDL_AudioDeviceID,int);
} InsertDeviceApi;
static InsertDeviceApi insert_device_api = {SDL_GetNumAudioDevices,SDL_GetAudioDeviceName,
    SDL_GetAudioDeviceSpec,SDL_OpenAudioDevice,SDL_CloseAudioDevice,SDL_PauseAudioDevice};

typedef struct {
    TsAudioEndpoint endpoint;
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    TsInsert *insert;
} InsertEndpoint;
static InsertEndpoint insert_endpoints[2];
static AudioState *insert_audio_owner;
static int insert_devices_dirty=1,insert_devices_initialized;
static SDL_AudioDeviceID insert_master_seen;
static int insert_rate_seen,insert_buffer_seen,insert_wanted_seen;
static char insert_names_seen[4][TS_CONFIG_PATH_MAX];

static unsigned insert_device_channels(const char *name,int capture)
{
    for(int i=0;i<insert_device_api.count(capture);++i) {
        const char *candidate=insert_device_api.name(i,capture);
        if(!candidate || strcmp(name,candidate))continue;
        SDL_AudioSpec spec;SDL_zero(spec);
        if(insert_device_api.spec(i,capture,&spec)==0)return spec.channels;
    }
    return 0;
}

static void insert_endpoint_callback(void *userdata,Uint8 *stream,int bytes)
{
    InsertEndpoint *e=userdata;
    unsigned channels=e->spec.channels;
    size_t frames=channels?((size_t)bytes/(sizeof(float)*channels)):0;
    if(e==&insert_endpoints[0]) {
        memset(stream,0,(size_t)bytes);
        ts_insert_render_send(e->insert,(float *)stream,frames,channels,(unsigned)e->spec.freq);
    } else ts_insert_capture_from(e->insert,(const float *)stream,frames,channels,1);
}

static void insert_endpoint_close(int side)
{
    InsertEndpoint *e=&insert_endpoints[side];
    if(e->device)insert_device_api.close(e->device); /* joins this callback */
    e->device=0;
    if(insert_audio_owner) {
        if(ts_real_output)SDL_LockAudioDevice(ts_real_output);
        if(side)ts_insert_capture_prepare_from(&insert_audio_owner->sister.insert,0,0,0,1);
        else ts_insert_send_prepare(&insert_audio_owner->sister.insert,0,
                                    (unsigned)insert_audio_owner->output_rate,0);
        if(ts_real_output)SDL_UnlockAudioDevice(ts_real_output);
    }
    ts_audio_endpoint_closed(&e->endpoint);
}

static void ts_audio_insert_devices_suspend(void)
{
    insert_endpoint_close(0);insert_endpoint_close(1);insert_devices_dirty=1;
}

static void ts_audio_insert_device_event(const SDL_Event *event)
{
    if(event->type==SDL_AUDIODEVICEADDED)insert_devices_dirty=1;
    if(event->type!=SDL_AUDIODEVICEREMOVED)return;
    int side=event->adevice.iscapture!=0;
    InsertEndpoint *e=&insert_endpoints[side];
    if(!e->device || e->device!=event->adevice.which)return;
    insert_endpoint_close(side);
    ts_audio_endpoint_removed(&e->endpoint,"INSERT device disconnected");
    /* Keep the choice; retry only on device-added, configuration or APPLY. */
}

static int insert_endpoint_open(int side,AudioState *audio,const char *name,unsigned buffer,char *error,size_t size)
{
    InsertEndpoint *e=&insert_endpoints[side];
    unsigned channels=insert_device_channels(name,side);
    ts_audio_endpoint_configure(&e->endpoint,name);
    ts_audio_endpoint_begin_open(&e->endpoint,0);
    /* Metadata is optional (JACK/ALSA/virtual endpoints may omit it). The
       successful open is authoritative; never replace an explicit name. */
    if (!channels) channels=2;
    SDL_AudioSpec wanted;SDL_zero(wanted);SDL_zero(e->spec);
    wanted.freq=audio->output_rate;wanted.channels=(Uint8)channels;wanted.format=AUDIO_F32SYS;
    wanted.samples=(Uint16)buffer;wanted.callback=insert_endpoint_callback;wanted.userdata=e;e->insert=&audio->sister.insert;
    e->device=insert_device_api.open(name,side,&wanted,&e->spec,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE|SDL_AUDIO_ALLOW_SAMPLES_CHANGE|SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if(!e->device || e->spec.format!=AUDIO_F32SYS || e->spec.channels<2 || e->spec.samples==0 || e->spec.freq<=0) {
        snprintf(error,size,"INSERT %s: %.120s",side?"RETURN":"SEND",
            e->device?"DEVICE MUST PROVIDE STEREO FLOAT AUDIO":SDL_GetError());
        if(e->device)insert_device_api.close(e->device);
        e->device=0;ts_audio_endpoint_open_failed(&e->endpoint,error);return 0;
    }
    if(ts_real_output)SDL_LockAudioDevice(ts_real_output);
    if(side)ts_insert_capture_prepare_from(e->insert,e->spec.channels,(unsigned)e->spec.freq,e->spec.samples,1);
    else ts_insert_send_prepare(e->insert,e->spec.channels,(unsigned)audio->output_rate,
                               ts_output_endpoint.buffer_frames?ts_output_endpoint.buffer_frames:buffer);
    if(ts_real_output)SDL_UnlockAudioDevice(ts_real_output);
    ts_audio_endpoint_opened(&e->endpoint,e->device,name,0,(unsigned)e->spec.freq,e->spec.channels,e->spec.samples,"F32");
    diagnostic_log("INSERT %s: %s / %u channels / %d Hz / %u frames",side?"RETURN":"SEND",name,e->spec.channels,e->spec.freq,e->spec.samples);
    insert_device_api.pause(e->device,0);
    return 1;
}

static void ts_audio_insert_devices_sync(void *state,const TsConfig *config,int force,char *error,size_t size)
{
    AudioState *audio=state;
    if(!insert_devices_initialized) {
        for(int side=0;side<2;++side)ts_audio_endpoint_init(&insert_endpoints[side].endpoint,
            side?TS_AUDIO_ENDPOINT_CAPTURE:TS_AUDIO_ENDPOINT_OUTPUT,0);
        insert_devices_initialized=1;
    }
    const char *names[]={config->insert_send_device,config->insert_return_device,
                         config->audio_output_device,config->record_input_device};
    int wanted=audio->sister.insert.controls.send_pair!=0 || (ts_live_ui && ts_live_ui->insert_open);
    int changed=force || insert_devices_dirty || insert_audio_owner!=audio ||
        insert_master_seen!=ts_real_output ||
        insert_rate_seen!=audio->output_rate || insert_buffer_seen!=config->audio_buffer_frames ||
        insert_wanted_seen!=wanted;
    for(int i=0;i<4;++i)if(strcmp(names[i],insert_names_seen[i]))changed=1;
    if(!changed)return;
    insert_audio_owner=audio;
    insert_endpoint_close(0);insert_endpoint_close(1);
    insert_devices_dirty=0;insert_master_seen=ts_real_output;
    insert_rate_seen=audio->output_rate;insert_buffer_seen=config->audio_buffer_frames;insert_wanted_seen=wanted;
    for(int i=0;i<4;++i)ts_copy_name(insert_names_seen[i],sizeof(insert_names_seen[i]),names[i]);
    error[0]=0;
    char shared_input[TS_CONFIG_PATH_MAX],master[TS_CONFIG_PATH_MAX];
    ts_audio_active_device_name(shared_input,sizeof(shared_input),config->record_input_device[0]?config->record_input_device:NULL,1);
    if(ts_output_endpoint.real_id==ts_real_output && ts_output_endpoint.active_name[0])
        ts_copy_name(master,sizeof(master),ts_output_endpoint.active_name);
    else ts_audio_active_device_name(master,sizeof(master),ts_active_output[0]?ts_active_output:NULL,0);
    int separate=config->insert_send_device[0]!=0;
    int dedicated=config->insert_return_device[0] && strcmp(config->insert_return_device,shared_input);
    if(ts_real_output)SDL_LockAudioDevice(ts_real_output);
    if(ts_real_capture)SDL_LockAudioDevice(ts_real_capture);
    if(audio->sister.insert.separate_send!=separate || atomic_load(&audio->sister.insert.dedicated_return)!=dedicated)
        ts_insert_io_mode(&audio->sister.insert,separate,dedicated);
    if(!dedicated && ts_real_capture && ts_capture_endpoint.state==TS_AUDIO_CONNECTION_ACTIVE)
        ts_insert_capture_prepare(&audio->sister.insert,ts_capture_endpoint.channels,
            ts_capture_endpoint.sample_rate,ts_capture_endpoint.buffer_frames);
    if(ts_real_capture)SDL_UnlockAudioDevice(ts_real_capture);
    if(ts_real_output)SDL_UnlockAudioDevice(ts_real_output);
    if(!wanted || !ts_real_output || audio->output_rate<=0)return;
    if(separate) {
        if(!strcmp(config->insert_send_device,master))
            snprintf(error,size,"SEND MATCHES MASTER; CHOOSE MASTER SPARES OR ANOTHER DEVICE");
        else insert_endpoint_open(0,audio,config->insert_send_device,(unsigned)config->audio_buffer_frames,error,size);
    }
    if(dedicated) {
        char capture_error[160]="";
        if(!insert_endpoint_open(1,audio,config->insert_return_device,(unsigned)config->audio_buffer_frames,capture_error,sizeof(capture_error)) && !error[0])
            snprintf(error,size,"%s",capture_error);
    }
}

static unsigned ts_audio_insert_choice_channels(const TsUiState *ui,int side)
{
    const char *name=side?ui->insert_return_device:ui->insert_send_device;
    if(name[0]) {
        const InsertEndpoint *e=&insert_endpoints[side];
        unsigned n=e->device && !strcmp(name,e->endpoint.configured_name)?
            e->spec.channels:insert_device_channels(name,side);
        if (!n) n=2; /* Allow first-pair selection and an explicit open attempt. */
        return n>TS_INSERT_MAX_CHANNELS?TS_INSERT_MAX_CHANNELS:n;
    }
    if(!side)return ts_audio_insert_offered_channels();
    const char *shared=ts_configured_input_name();
    unsigned n=ts_real_capture?ts_capture_endpoint.channels:
        (shared?insert_device_channels(shared,1):ts_audio_capture_preferred_channels(NULL));
    if (!n) n=2;
    return n>TS_INSERT_MAX_CHANNELS?TS_INSERT_MAX_CHANNELS:n;
}

static void ts_audio_insert_cycle_device(TsUiState *ui,int side,int direction)
{
    char *choice=side?ui->insert_return_device:ui->insert_send_device;
    int count=insert_device_api.count(side),index=0;
    if (count<0) count=0;
    for(int i=0;i<count;++i) {
        const char *name=insert_device_api.name(i,side);
        if(name && !strcmp(choice,name))index=i+1;
    }
    index=(index+direction+count+1)%(count+1);
    const char *name=index?insert_device_api.name(index-1,side):"";
    ts_copy_name(choice,TS_CONFIG_PATH_MAX,name?name:"");
    if(side)ui->insert_return_choice=0;
    else ui->insert_send_choice=index?1:0;
    ui->insert_offered_outputs=ts_audio_insert_choice_channels(ui,0);
}

static void ts_audio_insert_diagnostics(void *state,TsUiState *ui,int log)
{
    AudioState *audio=state;
    ui->insert_master_rate=ts_real_output?ts_output_endpoint.sample_rate:0;
    ui->insert_master_buffer=ts_real_output?ts_output_endpoint.buffer_frames:0;
    if(log && ts_jack_selected) diagnostic_log("JACK Master worker_xruns=%u Input worker_xruns=%u",
        ts_native_worker_xruns(ts_real_output),ts_native_worker_xruns(ts_real_capture));
    for(int side=0;side<2;++side) {
        int separate=side?atomic_load(&audio->sister.insert.dedicated_return):
            ui->config.insert_send_device[0]!=0;
        const TsAudioEndpoint *endpoint=separate?&insert_endpoints[side].endpoint:
            (side?&ts_capture_endpoint:&ts_output_endpoint);
        int open=separate?insert_endpoints[side].device!=0:
            (side?ts_real_capture!=0:ts_real_output!=0);
        const TsInputMonitor *monitor=side?&audio->sister.insert.return_monitor:
            &audio->sister.insert.send_monitor;
        TsInputMonitorDiagnostics d={0};
        unsigned rate=atomic_load(&monitor->input_rate);
        ui->insert_rates[side]=open?endpoint->sample_rate:0;
        ui->insert_buffers[side]=open?endpoint->buffer_frames:0;
        if(open && (side || separate))ts_input_monitor_get_diagnostics(monitor,&d);
        ui->insert_queue_ms[side]=rate?1000.0f*d.prime_frames/rate:0;
        ui->insert_gaps[side]=d.underrun_count;ui->insert_drops[side]=d.dropped_frame_count;
        if(log)diagnostic_log(
            "INSERT %s rate=%u buffer=%u ring_rate=%u target_ms=%.2f occupancy=%u "
            "prime=%u underruns=%u dropped=%u callbacks=%u frames=%u max_block=%u correction_ppm=%d jack_worker_xruns=%u",
            side?"RETURN":"SEND",ui->insert_rates[side],ui->insert_buffers[side],rate,
            ui->insert_queue_ms[side],d.occupancy_frames,d.prime_frames,d.underrun_count,
            d.dropped_frame_count,d.capture_callback_count,d.captured_frame_count,
            d.largest_capture_block_frames,d.correction_ppm,
            ts_native_worker_xruns(separate?insert_endpoints[side].device:(side?ts_real_capture:ts_real_output)));
    }
}
