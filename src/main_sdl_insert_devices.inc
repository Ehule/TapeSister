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
    if(channels<2 || channels>TS_INSERT_MAX_CHANNELS) {
        snprintf(error,size,"INSERT %s: DEVICE UNAVAILABLE OR NO 2-8 CHANNEL LAYOUT",side?"RETURN":"SEND");
        ts_audio_endpoint_open_failed(&e->endpoint,error);return 0;
    }
    SDL_AudioSpec wanted;SDL_zero(wanted);SDL_zero(e->spec);
    wanted.freq=audio->output_rate;wanted.channels=(Uint8)channels;wanted.format=AUDIO_F32SYS;
    wanted.samples=(Uint16)buffer;wanted.callback=insert_endpoint_callback;wanted.userdata=e;e->insert=&audio->sister.insert;
    e->device=insert_device_api.open(name,side,&wanted,&e->spec,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE|SDL_AUDIO_ALLOW_SAMPLES_CHANGE|SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if(!e->device || e->spec.format!=AUDIO_F32SYS || e->spec.channels<2 || e->spec.channels>8 || e->spec.freq<=0) {
        snprintf(error,size,"INSERT %s: %.120s",side?"RETURN":"SEND",
            e->device?"DEVICE MUST PROVIDE 2-8 FLOAT CHANNELS":SDL_GetError());
        if(e->device)insert_device_api.close(e->device);
        e->device=0;ts_audio_endpoint_open_failed(&e->endpoint,error);return 0;
    }
    if(ts_real_output)SDL_LockAudioDevice(ts_real_output);
    if(side)ts_insert_capture_prepare_from(e->insert,e->spec.channels,(unsigned)e->spec.freq,e->spec.samples,1);
    else ts_insert_send_prepare(e->insert,e->spec.channels,(unsigned)audio->output_rate,
                               e->spec.samples>buffer?e->spec.samples:buffer);
    if(ts_real_output)SDL_UnlockAudioDevice(ts_real_output);
    ts_audio_endpoint_opened(&e->endpoint,e->device,name,0,(unsigned)e->spec.freq,e->spec.channels,e->spec.samples,"F32");
    diagnostic_log("INSERT %s: %s / %u channels / %d Hz",side?"RETURN":"SEND",name,e->spec.channels,e->spec.freq);
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
    if(name[0])return insert_device_channels(name,side);
    if(!side)return ts_audio_insert_offered_channels();
    const char *shared=ts_configured_input_name();
    return shared?insert_device_channels(shared,1):ts_audio_capture_preferred_channels(NULL);
}

static void ts_audio_insert_cycle_device(TsUiState *ui,int side,int direction)
{
    char *choice=side?ui->insert_return_device:ui->insert_send_device;
    int count=insert_device_api.count(side),index=0;
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
