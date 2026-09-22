/* Control-thread coordination of shared capture and named auxiliary endpoints.
   No device opens occur in a callback. */
static void insert_sync(SDL_AudioDeviceID device,SDL_AudioDeviceID *capture_device,
    AudioState *audio,ExternalInputState *input,TsUiState *ui)
{
    int apply=ui->insert_apply_pending;
    ui->insert_apply_pending=0;
    if(apply) {
        int recording=audio->capture.state!=TS_CAPTURE_IDLE ||
            external_capture_busy(input) ||
            (audio->sister_file_recorder &&
             ts_performance_recorder_state(audio->sister_file_recorder)!=TS_PERFORMANCE_FILE_IDLE);
        if(recording) {
            snprintf(ui->status,sizeof(ui->status),"FINISH RECORDING BEFORE CHANGING INSERT PORTS");
            apply=0;
        } else {
            TsInsertControls c=ui->config.insert;
            c.send_pair=ui->insert_send_choice;c.return_pair=ui->insert_return_choice;
            snprintf(ui->config.insert_send_device,sizeof(ui->config.insert_send_device),"%s",ui->insert_send_device);
            snprintf(ui->config.insert_return_device,sizeof(ui->config.insert_return_device),"%s",ui->insert_return_device);
            if(device)SDL_LockAudioDevice(device);
            ts_sister_runtime_set_insert(&audio->sister,&c);
            ui->config.insert=audio->sister.insert.controls;
            if(device)SDL_UnlockAudioDevice(device);
            snprintf(ui->status,sizeof(ui->status),"INSERT PORTS APPLIED; USE ROUTER BYPASS TO CONNECT / AUDITION");
        }
    }
    ts_audio_insert_devices_sync(audio,&ui->config,apply,ui->insert_device_status,sizeof(ui->insert_device_status));
    if(apply && ui->insert_device_status[0])snprintf(ui->status,sizeof(ui->status),"%s",ui->insert_device_status);
    int wanted=!atomic_load(&audio->sister.insert.dedicated_return) &&
        (audio->sister.insert.controls.send_pair!=0 || ui->insert_open);
    unsigned reserved=ts_insert_reserved_port(&audio->sister.insert);
    if(atomic_load_explicit(&audio->sister.insert.monitor_port_ack,memory_order_acquire)!=reserved) {
        /* Discard EXT frames queued before reservation. Playback gates EXT
           until this completes, including after a project load. */
        if(device)SDL_LockAudioDevice(device);
        if(*capture_device)SDL_LockAudioDevice(*capture_device);
        ts_input_monitor_discard(&input->monitor);
        atomic_store_explicit(&audio->sister.insert.monitor_port_ack,reserved,memory_order_release);
        if(*capture_device)SDL_UnlockAudioDevice(*capture_device);
        if(device)SDL_UnlockAudioDevice(device);
    }
    int requested=external_input_requested(input,TS_INPUT_CONSUMER_INSERT);
    if(wanted!=requested || apply) {
        char error[160];
        external_input_request(input,TS_INPUT_CONSUMER_INSERT,wanted);
        if(!sync_external_input_consumers(capture_device,audio,input,&ui->config,error,sizeof(error)))
            snprintf(ui->status,sizeof(ui->status),"INSERT RETURN: %.120s",error);
    }
}
