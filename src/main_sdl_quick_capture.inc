/* A take is independent of Current and of the playing source group. Reuse
   Mosaic's streaming recorder; only its publication destination differs. */
static const char *quick_capture_source_name(int source)
{
    return source==TS_RECORD_SOURCE_DRY?"DRY":source==TS_RECORD_SOURCE_EXT?"EXT":
           source==TS_RECORD_SOURCE_SYNTH?"FM":"OUT";
}

static int quick_capture_reserve(ExternalInputState *input,TsUiState *ui,
    TsInstrument *instrument,TsSamplePages *pages,char *error,size_t size)
{
    int fresh=0;
    if(!mosaic_bank_destination(pages,instrument,&input->quick_page,&input->quick_slot,&fresh,error,size))return 0;
    ui->sample_page_count=(int)ts_sample_pages_count(pages);
    return 1;
}

static int quick_capture_empty_page(const TsInstrument *bank,const TsInstrument *current)
{
    if(!bank || bank==current || bank->current.data)return 0;
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(bank->bank[i].occupied || bank->bank[i].locked)return 0;
    return 1;
}

static void quick_capture_button(SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,
    AudioState *audio,ExternalInputState *input,TsUiState *ui,TsInstrument *instrument,TsSamplePages *pages)
{
    if(input->quick_recording) {
        if(input->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING)
            stop_external_capture_early(device,*input_device,input,ui);
        else snprintf(ui->status,sizeof(ui->status),"FINISHING CAPTURE / PLAYBACK CONTINUES");
        return;
    }
    if(external_capture_busy(input) || audio->capture.state!=TS_CAPTURE_IDLE ||
       audio->sister.capture.state!=TS_CAPTURE_IDLE || ui->sister_capture_active ||
       ui->file_record_state==TS_PERFORMANCE_FILE_RECORDING || ui->file_record_state==TS_PERFORMANCE_FILE_STOPPING) {
        snprintf(ui->status,sizeof(ui->status),"FINISH OR CANCEL THE CURRENT RECORDING FIRST");return;
    }
    if(!pages || !pages->active_live || ui->external_record_bank || ui->mosaic_editing) {
        snprintf(ui->status,sizeof(ui->status),"RETURN TO SAMPLE TILES TO START CAPTURE");return;
    }
    char error[160];
    if(!quick_capture_reserve(input,ui,instrument,pages,error,sizeof(error))) {
        snprintf(ui->status,sizeof(ui->status),"CAPTURE: %.148s",error);return;
    }
    (void)arm_external_capture_to(device,input_device,audio,input,ui,instrument,2);
}

static void finalize_quick_capture(SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,
    AudioState *audio,ExternalInputState *input,TsUiState *ui,TsInstrument *instrument,TsSamplePages *pages)
{
    TsExternalRecorder *r=&input->recorder;
    ts_performance_recorder_request_stop(r->stream);
    if(input->mosaic_writer) {SDL_WaitThread(input->mosaic_writer,NULL);input->mosaic_writer=NULL;}
    int failed=ts_performance_recorder_state(r->stream)==TS_PERFORMANCE_FILE_FAILED || r->stream_overrun;
    TsSample sample={0};char error[160]="";
    int ok=ts_performance_recorder_load(r->stream,&sample,error,sizeof(error));
    TsInstrument *staged=NULL;
    if(ok) {
        snprintf(sample.name,sizeof(sample.name),"CAP %s",quick_capture_source_name(atomic_load(&input->record_source)));
        staged=malloc(sizeof(*staged));
        if(!staged) {ok=0;snprintf(error,sizeof(error),"Not enough memory to prepare the tile");}
        else {ts_instrument_init(staged);ok=ts_instrument_import_sample(staged,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error));}
    }
    ts_sample_free(&sample);
    if(ok) {
        TsInstrument *bank=ts_sample_pages_page_mut(pages,instrument,(size_t)input->quick_page);
        /* A reserved destination is never allowed to overwrite a later edit. */
        if(!bank || bank->bank[input->quick_slot].occupied || bank->bank[input->quick_slot].locked ||
           (bank->selected_slot==input->quick_slot && !quick_capture_empty_page(bank,instrument))) {
            ok=quick_capture_reserve(input,ui,instrument,pages,error,sizeof(error));
            bank=ok?ts_sample_pages_page_mut(pages,instrument,(size_t)input->quick_page):NULL;
        }
        if(ok) {
            int empty_page=quick_capture_empty_page(bank,instrument) && input->quick_slot==staged->selected_slot;
            staged->bank[staged->selected_slot].relation=TS_FAMILY_CAPTURED;
            staged->bank[staged->selected_slot].parent_slot=-1;
            if(device)SDL_LockAudioDevice(device);
            if(empty_page) {pages->pages[input->quick_page]=staged;staged=bank;}
            else {
                bank->bank[input->quick_slot]=staged->bank[staged->selected_slot];
                memset(&staged->bank[staged->selected_slot],0,sizeof(TsBankSlot));
            }
            if(device)SDL_UnlockAudioDevice(device);
            ui->captured_tile_page=input->quick_page;ui->captured_tile_slot=input->quick_slot;
            ui->captured_tile_until_ms=SDL_GetTicks()+8000u;
        }
    }
    if(staged){ts_instrument_free(staged);free(staged);}
    SDL_AudioDeviceID source_device=recorder_device(device,*input_device,input);
    if(source_device)SDL_LockAudioDevice(source_device);
    ts_external_recorder_free(r);
    if(source_device)SDL_UnlockAudioDevice(source_device);
    input->quick_recording=ui->quick_capture_active=0;
    ui->quick_capture_page=ui->quick_capture_slot=-1;
    if(atomic_load(&input->record_source)==TS_RECORD_SOURCE_EXT) {
        ts_input_ownership_release(&input->ownership,TS_INPUT_CONSUMER_RECORD_ACTIVE);
        char ignored[160];sync_external_input_consumers(input_device,audio,input,&ui->config,ignored,sizeof(ignored));
    }
    sync_external_capture_ui(device,*input_device,input,ui);
    if(ok)snprintf(ui->status,sizeof(ui->status),"%s / SAMPLE %02d:%02d / WAV SAVED / PLAYBACK CONTINUES",
        failed?"PARTIAL TAKE KEPT":"CAPTURE KEPT",input->quick_page+1,input->quick_slot+1);
    else snprintf(ui->status,sizeof(ui->status),"WAV RETAINED / TILE FAILED: %.127s",error);
    /* No selection, voice reset, group membership or automatic launch here. */
}

static int quick_capture_event(const SDL_Event *event,SDL_Window *window,
    SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,AudioState *audio,
    ExternalInputState *input,TsUiState *ui,TsInstrument *instrument,TsSamplePages *pages)
{
    if(ui->external_record_bank || ui->midi_learn_active)return 0;
    int x,y;
    if(event->type==SDL_KEYDOWN && event->key.windowID==SDL_GetWindowID(window) &&
       event->key.keysym.sym==SDLK_ESCAPE && input->quick_recording && !ui_dialog_open(ui) &&
       !ui->canvas_gesture.active && !ui->selecting && !ui->tape_dragging) {
        cancel_external_capture(device,*input_device,input,ui);return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN || event->button.windowID!=SDL_GetWindowID(window))return 0;
    logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(input->quick_recording && x>=544 && x<630 && y>=382 && y<398) {
        quick_capture_button(device,input_device,audio,input,ui,instrument,pages);return 1;
    }
    int fm_capture=ui->fm_open && !ui->mosaic_editing && x>=456 && x<536 && y>=382 && y<398 &&
        !ui_blocking_dialog_open_except_workspaces(ui) && !ui->fm_bank_choice_open && !ui->fm_full_choice_open;
    if((ui_dialog_open(ui) && !fm_capture) || ui->mosaic_open || ui->tracker_open ||
       ui->show_recipes || ui->show_ingredients)return 0;
    if(!ui->show_keyboard && input->quick_recording && ui->sample_page==input->quick_page &&
       ts_ui_bank_slot_from_point(x,y)==input->quick_slot) {
        snprintf(ui->status,sizeof(ui->status),"TILE %02d:%02d RESERVED FOR CAPTURE",input->quick_page+1,input->quick_slot+1);return 1;
    }
    if(!fm_capture && (ui->show_keyboard ? !(x>=298 && x<374 && y>=313 && y<330) :
       !ts_ui_capture_button_from_point(x,y)))return 0;
    if(event->button.button==SDL_BUTTON_RIGHT) {
        if(input->quick_recording || ui->capture_state!=TS_CAPTURE_IDLE) {
            snprintf(ui->status,sizeof(ui->status),"FINISH CAPTURE BEFORE CHANGING ITS SOURCE");return 1;
        }
        if(SDL_GetModState()&KMOD_SHIFT) {
            int seconds=ui->config.quick_capture_seconds;
            ui->config.quick_capture_seconds=seconds==0?10:seconds==10?30:seconds==30?60:seconds==60?300:0;
        } else {
            int source=ui->config.quick_capture_source;
            ui->config.quick_capture_source=source==TS_RECORD_SOURCE_OUTPUT?TS_RECORD_SOURCE_DRY:
                source==TS_RECORD_SOURCE_DRY?TS_RECORD_SOURCE_EXT:source==TS_RECORD_SOURCE_EXT?TS_RECORD_SOURCE_SYNTH:TS_RECORD_SOURCE_OUTPUT;
        }
        char length[24];
        if(ui->config.quick_capture_seconds)snprintf(length,sizeof(length),"%d SEC",ui->config.quick_capture_seconds);
        else snprintf(length,sizeof(length),"UNTIL STOP");
        snprintf(ui->status,sizeof(ui->status),"CAP %s / %s / RIGHT: SOURCE / SHIFT+RIGHT: LENGTH",
            quick_capture_source_name(ui->config.quick_capture_source),length);
        char error[160];if(!ts_config_save(&ui->config,config_file_path(),error,sizeof(error)))
            snprintf(ui->status,sizeof(ui->status),"CAPTURE SETTING ACTIVE / SAVE FAILED: %.119s",error);
        return 1;
    }
    if(event->button.button!=SDL_BUTTON_LEFT)return 0;
    if(audio->capture.state!=TS_CAPTURE_IDLE)capture_button(device,audio,ui,instrument,audio->output_rate);
    else quick_capture_button(device,input_device,audio,input,ui,instrument,pages);
    return 1;
}
