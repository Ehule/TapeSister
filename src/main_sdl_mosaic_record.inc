/* REC TILE shares the input/synth recorder with the REC bank, but owns a
   destination in the arrangement. No bank swap or selected-tile mutation. */
static int arm_external_capture_to(SDL_AudioDeviceID,SDL_AudioDeviceID *,AudioState *,
                                  ExternalInputState *,TsUiState *,TsInstrument *,int);
static void cancel_external_capture(SDL_AudioDeviceID,SDL_AudioDeviceID,
                                    ExternalInputState *,TsUiState *);
static void stop_external_capture_early(SDL_AudioDeviceID,SDL_AudioDeviceID,
                                        ExternalInputState *,TsUiState *);

static int mosaic_record_has_space(const TsMosaic *m)
{
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(!m->events[i].id)return 1;
    return 0;
}

static int install_mosaic_take(SDL_AudioDeviceID device,TsUiState *ui,
                               const ExternalInputState *input,const float *data,
                               size_t frames,uint32_t rate,uint8_t channels,
                               char *error,size_t size)
{
    TsMosaic *m=ui->mosaic;
    if(!m || m->epoch!=input->mosaic_epoch) {
        snprintf(error,size,"Mosaic changed during the take; recording was not added to a different scene");return 0;
    }
    if(!mosaic_record_has_space(m)) {
        snprintf(error,size,"Mosaic is full; remove a card and import the archived WAV");return 0;
    }
    if(!data || frames<2 || !rate || !ts_sample_dimensions(frames,channels,NULL,NULL)) {
        snprintf(error,size,"Invalid Mosaic recording");return 0;
    }
    TsSample sample={.data=(float *)data,.frames=frames,.sample_rate=rate,.channels=channels};
    snprintf(sample.name,sizeof(sample.name),"REC %s %llu",
        atomic_load_explicit(&input->record_source,memory_order_acquire)==TS_RECORD_SOURCE_SYNTH?"SYNTH":"INPUT",
        (unsigned long long)m->next_id);
    /* Allocate and derive waveform peaks on the event thread before publishing
       the event. The audio callback only traverses sources held by events. */
    TsMosaicSource *source=ts_mosaic_source(m,&sample,error,size);
    if(!source)return 0;
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(m);
    TsMosaicEvent *event=ts_mosaic_add(m,source,input->mosaic_start,input->mosaic_x);
    uint64_t id=event?event->id:0;
    if(event)event->looping=0;
    if(device)SDL_UnlockAudioDevice(device);
    if(!id) {snprintf(error,size,"Mosaic card could not be added");return 0;}
    /* An open card editor keeps its selection and document. The new take is
       still available in the arrangement and Events source pages. */
    if(!ui->mosaic_editing) {
        memset(ui->mosaic_selection,0,sizeof(ui->mosaic_selection));ui->mosaic_selected=id;
    }
    return 1;
}

static int mosaic_record_tile_event(const SDL_Event *event,SDL_Window *window,
                                    SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,
                                    AudioState *audio,ExternalInputState *input,
                                    TsUiState *ui,TsInstrument *instrument)
{
    if(!ui->mosaic_open || ui_dialog_open(ui) || ui->midi_learn_active)return 0;
    if(event->type==SDL_KEYDOWN && event->key.windowID==SDL_GetWindowID(window) &&
       event->key.keysym.sym==SDLK_ESCAPE && input->mosaic_recording) {
        cancel_external_capture(device,*input_device,input,ui);return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN || event->button.windowID!=SDL_GetWindowID(window) ||
       (event->button.button!=SDL_BUTTON_LEFT && event->button.button!=SDL_BUTTON_RIGHT))return 0;
    int x,y;logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(x<TS_MOSAIC_REC_TILE_X || x>=TS_MOSAIC_REC_TILE_X+TS_MOSAIC_REC_TILE_W ||
       y<TS_MOSAIC_REC_TILE_Y || y>=TS_MOSAIC_REC_TILE_Y+19)return 0;
    if(external_capture_busy(input) && !input->mosaic_recording) {
        snprintf(ui->status,sizeof(ui->status),"REC BANK IS BUSY - FINISH OR CANCEL THAT TAKE FIRST");return 1;
    }
    if(event->button.button==SDL_BUTTON_RIGHT) {
        if(external_capture_busy(input)) {
            snprintf(ui->status,sizeof(ui->status),"FINISH OR CANCEL THE TAKE BEFORE CHANGING SOURCE");return 1;
        }
        ui->record_source=ui->record_source==TS_RECORD_SOURCE_EXT?TS_RECORD_SOURCE_SYNTH:TS_RECORD_SOURCE_EXT;
        atomic_store_explicit(&input->record_source,ui->record_source,memory_order_release);
        snprintf(ui->status,sizeof(ui->status),"REC TILE SOURCE: %s / LEFT-CLICK TO ARM / RIGHT-CLICK TO CHANGE",
            ui->record_source==TS_RECORD_SOURCE_SYNTH?"SYNTH INTERNAL":"EXT INPUT (CONFIGURED DEVICE/CHANNEL)");
    } else if(input->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING)
        stop_external_capture_early(device,*input_device,input,ui);
    else if(input->recorder.state==TS_EXTERNAL_CAPTURE_ARMED)
        cancel_external_capture(device,*input_device,input,ui);
    else if(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED)
        snprintf(ui->status,sizeof(ui->status),"FINISHING MOSAIC TAKE");
    else if(audio->capture.state!=TS_CAPTURE_IDLE || audio->sister.capture.state!=TS_CAPTURE_IDLE)
        snprintf(ui->status,sizeof(ui->status),"FINISH THE CURRENT TILE CAPTURE BEFORE ARMING MOSAIC");
    else (void)arm_external_capture_to(device,input_device,audio,input,ui,instrument,1);
    return 1;
}
