/* REC TILE publishes an arrangement card and an ordinary Sample tile without
   swapping the active bank or changing the selected editor. */
static int arm_external_capture_to(SDL_AudioDeviceID,SDL_AudioDeviceID *,AudioState *,
                                  ExternalInputState *,TsUiState *,TsInstrument *,int);
static void cancel_external_capture(SDL_AudioDeviceID,SDL_AudioDeviceID,
                                    ExternalInputState *,TsUiState *);
static void stop_external_capture_early(SDL_AudioDeviceID,SDL_AudioDeviceID,
                                        ExternalInputState *,TsUiState *);

static TsBankSlot *mosaic_bank_destination(TsSamplePages *,TsInstrument *,int *,int *,int *,char *,size_t);

static int mosaic_record_has_space(const TsMosaic *m)
{
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(!m->events[i].id)return 1;
    return 0;
}

static int install_mosaic_take(SDL_AudioDeviceID device,TsUiState *ui,
                               TsInstrument *instrument,TsSamplePages *pages,
                               const ExternalInputState *input,const float *data,
                               size_t frames,uint32_t rate,uint8_t channels,
                               int *bank_page,int *bank_slot,char *error,size_t size)
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
        atomic_load_explicit(&input->record_source,memory_order_acquire)==TS_RECORD_SOURCE_OUTPUT?"OUTPUT":
        atomic_load_explicit(&input->record_source,memory_order_acquire)==TS_RECORD_SOURCE_DRY?"DRY":
        atomic_load_explicit(&input->record_source,memory_order_acquire)==TS_RECORD_SOURCE_SYNTH?"SYNTH":"INPUT",
        (unsigned long long)m->next_id);
    /* Allocate and derive waveform peaks on the event thread before publishing
       the event. The audio callback only traverses sources held by events. */
    TsMosaicSource *source=ts_mosaic_source(m,&sample,error,size);
    if(!source)return 0;
    TsInstrument *staged=malloc(sizeof(*staged));
    if(!staged){snprintf(error,size,"Not enough memory for the Sample tile");return 0;}
    ts_instrument_init(staged);
    if(!ts_instrument_import_sample(staged,&sample,0,0,0,TS_LOOP_FORWARD,error,size)) {
        ts_instrument_free(staged);free(staged);return 0;
    }
    int fresh_bank=0;
    TsBankSlot *destination=mosaic_bank_destination(pages,instrument,
        bank_page,bank_slot,&fresh_bank,error,size);
    if(!destination){ts_instrument_free(staged);free(staged);return 0;}
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(m);
    TsMosaicEvent *event=ts_mosaic_add(m,source,input->mosaic_start,input->mosaic_x);
    uint64_t id=event?event->id:0;
    if(event) {
        event->looping=0;
        if(fresh_bank) {
            *pages->pages[*bank_page]=*staged;ts_instrument_init(staged);
        } else {
            *destination=staged->bank[staged->selected_slot];
            memset(&staged->bank[staged->selected_slot],0,sizeof(*destination));
        }
    }
    if(device)SDL_UnlockAudioDevice(device);
    ts_instrument_free(staged);free(staged);
    if(!id) {snprintf(error,size,"Mosaic card could not be added");return 0;}
    if(pages)ui->sample_page_count=(int)ts_sample_pages_count(pages);
    /* An open card editor keeps its selection and document. The new take is
       also available through the normal Sample pages. */
    if(!ui->mosaic_editing) {
        memset(ui->mosaic_selection,0,sizeof(ui->mosaic_selection));ui->mosaic_selected=id;
    }
    return 1;
}

static void finalize_mosaic_stream(SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,
    AudioState *audio,ExternalInputState *input,TsUiState *ui,TsInstrument *instrument,TsSamplePages *pages)
{
    TsExternalRecorder *r=&input->recorder;
    ts_performance_recorder_request_stop(r->stream);
    if(input->mosaic_writer) {SDL_WaitThread(input->mosaic_writer,NULL);input->mosaic_writer=NULL;}
    TsSample sample={0};char error[160];int page=-1,slot=-1;
    int failed=ts_performance_recorder_state(r->stream)==TS_PERFORMANCE_FILE_FAILED || r->stream_overrun;
    int quiet=r->silence_stopped;
    int ok=ts_performance_recorder_load(r->stream,&sample,error,sizeof(error));
    if(ok)ok=install_mosaic_take(device,ui,instrument,pages,input,sample.data,sample.frames,sample.sample_rate,sample.channels,
        &page,&slot,error,sizeof(error));
    ts_sample_free(&sample);
    /* COMPLETED prevents callback writes. File I/O and sample construction
       above are outside the audio lock; the WAV survives a failed card import. */
    SDL_AudioDeviceID source_device=recorder_device(device,*input_device,input);
    if(source_device)SDL_LockAudioDevice(source_device);
    ts_external_recorder_free(r);
    if(source_device)SDL_UnlockAudioDevice(source_device);
    input->mosaic_recording=ui->mosaic_recording=0;
    if(atomic_load_explicit(&input->record_source,memory_order_acquire)==TS_RECORD_SOURCE_EXT) {
        ts_input_ownership_release(&input->ownership,TS_INPUT_CONSUMER_RECORD_ACTIVE);
        char ignored[160];sync_external_input_consumers(input_device,audio,input,&ui->config,ignored,sizeof(ignored));
    }
    sync_external_capture_ui(device,*input_device,input,ui);
    if(!ok)snprintf(ui->status,sizeof(ui->status),"MOSAIC WAV RETAINED / %.130s",error);
    else if(failed)snprintf(ui->status,sizeof(ui->status),"RECORDING STOPPED: DISK/QUEUE ERROR / PARTIAL TAKE ARCHIVED / SAMPLE %02d:%02d",page+1,slot+1);
    else snprintf(ui->status,sizeof(ui->status),"%s / MOSAIC + SAMPLE %02d:%02d / ARCHIVED",quiet?"SILENCE AUTO STOP":"TAKE KEPT",page+1,slot+1);
    show_overlay(ui,failed?"RECORDING INTERRUPTED":ok?"REC TAKE KEPT":"WAV RETAINED",1200);
}

static void mosaic_record_length_save(TsUiState *ui)
{
    char error[160];
    if(!ts_config_save(&ui->config,config_file_path(),error,sizeof(error)))
        snprintf(ui->status,sizeof(ui->status),"CONFIG SAVE FAILED: %.130s",error);
}

static int mosaic_record_length_event(const SDL_Event *event,SDL_Window *window,
                                      ExternalInputState *input,TsUiState *ui)
{
    int x=0,y=0;
    if(ui->mosaic_record_length_drag && event->type==SDL_KEYDOWN &&
       event->key.windowID==SDL_GetWindowID(window) && event->key.keysym.sym==SDLK_ESCAPE) {
        ui->config.mosaic_record_seconds=ui->mosaic_record_length_before;
        ui->mosaic_record_length_drag=0;return 1;
    }
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
        if(ui->mosaic_record_length_drag)mosaic_record_length_save(ui);
        ui->mosaic_record_length_drag=0;return 0;
    }
    if(event->type==SDL_MOUSEMOTION) {
        if(!ui->mosaic_record_length_drag || event->motion.windowID!=SDL_GetWindowID(window))return 0;
        logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    } else if(event->type==SDL_MOUSEBUTTONUP) {
        if(!ui->mosaic_record_length_drag || event->button.windowID!=SDL_GetWindowID(window))return 0;
        ui->mosaic_record_length_drag=0;mosaic_record_length_save(ui);return 1;
    } else if(event->type==SDL_MOUSEBUTTONDOWN) {
        if(event->button.windowID!=SDL_GetWindowID(window))return 0;
        logical_mouse(window,event->button.x,event->button.y,&x,&y);
        /* The compact navigation buttons end above this status line. */
        if(y>=20 && y<32 && x>=164 && x<494)return 1;
    } else if(event->type==SDL_MOUSEWHEEL) {
        if(event->wheel.windowID!=SDL_GetWindowID(window))return 0;
#if SDL_VERSION_ATLEAST(2,26,0)
        logical_mouse(window,event->wheel.mouseX,event->wheel.mouseY,&x,&y);
#else
        int rx,ry;SDL_GetMouseState(&rx,&ry);logical_mouse(window,rx,ry,&x,&y);
#endif
    } else return 0;
    if(!ui->mosaic_record_length_drag && (y<56 || y>=66 || x<118 || x>=TS_MOSAIC_LENGTH_X+TS_MOSAIC_LENGTH_W))return 0;
    if(external_capture_busy(input)) {
        ui->mosaic_record_length_drag=0;
        snprintf(ui->status,sizeof(ui->status),"FINISH THE TAKE BEFORE CHANGING ITS LENGTH");return 1;
    }
    int seconds=ui->config.mosaic_record_seconds;
    if(event->type==SDL_MOUSEWHEEL) {
        int wheel=event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event->wheel.y:event->wheel.y;
        if(!wheel)return 1;
        if(!ts_ui_wheel_guard_accept(&ui->wheel_guard,WHEEL_TARGET_SISTER+0x300,SDL_GetTicks()))return 1;
        if(!seconds)seconds=wheel<0?3600:0;
        else {seconds+=(SDL_GetModState()&KMOD_SHIFT?1:10)*wheel;if(seconds>3600)seconds=0;else if(seconds<10)seconds=10;}
    } else if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_RIGHT)seconds=0;
    else if(event->type==SDL_MOUSEMOTION || (event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT)) {
        if(!ui->mosaic_record_length_drag)ui->mosaic_record_length_before=ui->config.mosaic_record_seconds;
        seconds=ts_mosaic_record_length_seconds((x-TS_MOSAIC_LENGTH_X)/(float)(TS_MOSAIC_LENGTH_W-1));
        ui->mosaic_record_length_drag=1;
    } else return 1;
    ui->config.mosaic_record_seconds=seconds;
    snprintf(ui->status,sizeof(ui->status),seconds?"CARD LENGTH %02d:%02d / WHEEL 10S / SHIFT 1S / RIGHT: INF":"UNLIMITED / SILENCE STOP %dS AT %d DBFS",
        seconds?seconds/60:ui->config.mosaic_silence_seconds,seconds?seconds%60:ui->config.mosaic_silence_db);
    if(!ui->mosaic_record_length_drag)mosaic_record_length_save(ui);
    return 1;
}

static int mosaic_record_tile_event(const SDL_Event *event,SDL_Window *window,
                                    SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,
                                    AudioState *audio,ExternalInputState *input,
                                    TsUiState *ui,TsInstrument *instrument)
{
    if(!ui->mosaic_open || ui_dialog_open(ui) || ui->midi_learn_active){ui->mosaic_record_length_drag=0;return 0;}
    if(mosaic_record_length_event(event,window,input,ui))return 1;
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
        ui->mosaic_record_source=ui->mosaic_record_source==TS_RECORD_SOURCE_DRY?TS_RECORD_SOURCE_OUTPUT:
            ui->mosaic_record_source==TS_RECORD_SOURCE_OUTPUT?TS_RECORD_SOURCE_EXT:TS_RECORD_SOURCE_DRY;
        snprintf(ui->status,sizeof(ui->status),"REC TILE: %s / LEFT-CLICK TO RECORD / RIGHT-CLICK TO CHANGE",
            ui->mosaic_record_source==TS_RECORD_SOURCE_OUTPUT?"OUTPUT (WHAT YOU HEAR)":
            ui->mosaic_record_source==TS_RECORD_SOURCE_DRY?"DRY KEYBOARD (BEFORE PRISM/FX)":"EXT INPUT (CONFIGURED DEVICE/CHANNEL)");
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
