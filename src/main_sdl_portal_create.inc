/* CREATE's source generator remains synchronous as before; CDP runs in the
   existing cancellable Portal worker. Completion is subject to normal stale
   tile checks and cannot replace an in-progress edit gesture. */
static int portal_create_event(const SDL_Event *event,int x,int y,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsInstrument *instrument,
    PortalController *c,TransformController *transform)
{
    int click=event->type==SDL_MOUSEBUTTONDOWN && x>=85 && x<167 && y>=205 && y<228;
    int escape=event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE;
    if(c->create_roll && !ui_dialog_open(ui) && (escape || click)) {
        portal_cancel(c);c->quick_apply=0;
        snprintf(ui->status,sizeof(ui->status),"CANCELLING CDP ROLL - FRESH FM KEPT");return 1;
    }
    if(!click || ui_dialog_open(ui))return 0;
    if(event->button.button==SDL_BUTTON_RIGHT) {
        ui->cdp_create=!ui->cdp_create;
        snprintf(ui->status,sizeof(ui->status),"%s - RIGHT-CLICK CREATE TO SWITCH",ui->cdp_create?
            "CREATE + CDP: FRESH FM WITH A ROLLED PROCESS":"CREATE: FRESH FM");return 1;
    }
    if(event->button.button!=SDL_BUTTON_LEFT || !ui->cdp_create)return 0;
    if(!c->create_seeds || c->worker || transform->worker || portal_edit_gesture_active(ui) ||
       ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active) {
        snprintf(ui->status,sizeof(ui->status),"FINISH CURRENT EDIT, CAPTURE OR RENDER BEFORE CREATE + CDP");return 1;
    }
    if(instrument->has_selection && instrument->current.data &&
       (instrument->selection_last<=instrument->selection_first ||
        instrument->selection_last-instrument->selection_first<instrument->current.sample_rate/10u)) {
        snprintf(ui->status,sizeof(ui->status),"CREATE + CDP NEEDS A SELECTION OF AT LEAST 100 MS");return 1;
    }
    TsCdpRuntime runtime;ts_cdp_runtime_init(&runtime);char error[160];char *base=SDL_GetBasePath();
    int available=ts_cdp_runtime_discover(&runtime,ui->config.cdp_bin_path,base,error,sizeof(error));
    SDL_free(base);
    if(!available) {snprintf(ui->status,sizeof(ui->status),"CREATE + CDP: %.140s",error);return 1;}
    uint32_t seed=0;
    if(!generate_family_candidate(device,audio,ui,instrument,c->create_seeds,0,0,0,&seed))return 1;
    c->create_ready=0;c->selection_scope=instrument->has_selection;
    TsPortalUi *p=&ui->portal;
    if(!portal_source(device,audio,ui,instrument,c)) {
        snprintf(ui->status,sizeof(ui->status),"FRESH FM KEPT; CDP: %.130s",p->message);return 1;
    }
    TsPortalRecipe r;
    if(!ts_portal_create_recipe(seed,&c->source,&r,error,sizeof(error))) {
        snprintf(ui->status,sizeof(ui->status),"FRESH FM KEPT; %.140s",error);return 1;
    }
    portal_load_recipe(p,&r);p->macro_view=0;p->parameter_scroll=0;
    p->selected_tab=p->selected_slot=-1;p->number_focus=-1;
    portal_preview(device,audio,ui,c);
    c->create_roll=c->quick_apply=c->worker!=NULL;
    c->create_transform=transform;ui->cdp_creating=c->create_roll;
    if(c->create_roll)
        snprintf(ui->status,sizeof(ui->status),"CREATING %.39s - CLICK CANCEL CDP OR ESC; RECIPE RETAINED IN PORTAL",r.name);
    else snprintf(ui->status,sizeof(ui->status),"FRESH FM KEPT; CDP: %.130s",p->message);
    return 1;
}
