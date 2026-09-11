/* CREATE has independent dice: left rolls FM, right rolls CDP from the retained
   clean waveform, middle returns to it. Workers still own immutable snapshots. */
static int portal_create_matches(const PortalController *c,const TsUiState *ui,const TsInstrument *instrument)
{
    if(!c->create_original.data || c->create_event!=ui->mosaic_editing)return 0;
    if(c->create_event) {
        if(!ui->mosaic || c->create_epoch!=ui->mosaic->epoch || !ts_mosaic_find(ui->mosaic,c->create_event))return 0;
    } else if(c->create_slot!=instrument->selected_slot || c->create_page!=ui->sample_page)return 0;
    uint64_t hash=ts_sample_hash(&instrument->current);
    return hash==c->create_hash || hash==ts_sample_hash(&c->create_original);
}
static int portal_create_keep(PortalController *c,TsUiState *ui,const TsInstrument *instrument)
{
    char error[160];TsSample next={0};
    if(!ts_sample_clone(&next,&instrument->current,error,sizeof(error))) {
        snprintf(ui->status,sizeof(ui->status),"COULD NOT KEEP CREATE SOURCE: %.120s",error);return 0;
    }
    ts_sample_free(&c->create_original);c->create_original=next;c->create_hash=ts_sample_hash(&next);
    c->create_event=ui->mosaic_editing;c->create_epoch=ui->mosaic?ui->mosaic->epoch:0;
    c->create_slot=instrument->selected_slot;c->create_page=ui->sample_page;
    c->create_selection=instrument->has_selection;c->create_first=instrument->selection_first;c->create_last=instrument->selection_last;
    c->create_loop=instrument->has_loop;c->create_loop_first=instrument->loop_first;c->create_loop_last=instrument->loop_last;
    c->create_loop_mode=instrument->loop_mode;c->create_crossfade=instrument->loop_crossfade_ms;
    c->create_tuning=instrument->tuning;c->create_audible_tuning=instrument->audible_tuning;
    ts_fm_seed_sequence_init(&c->create_dice,c->create_hash^UINT64_C(0x43445044494345));
    return 1;
}
static int portal_create_restore(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                                  TsInstrument *instrument,PortalController *c)
{
    if(!portal_create_matches(c,ui,instrument)) {
        snprintf(ui->status,sizeof(ui->status),"CREATE SOURCE CHANGED - CURRENT EDITS KEPT");return 0;
    }
    if(mosaic_same_audio(&instrument->current,&c->create_original))return 1;
    char error[160];lock_edit(device,audio);
    int ok=ts_instrument_apply_rendered_replacement(instrument,&c->create_original,0,instrument->current.frames,error,sizeof(error));
    if(ok) {
        instrument->has_selection=c->create_selection;instrument->selection_first=c->create_first;instrument->selection_last=c->create_last;
        instrument->has_loop=c->create_loop;instrument->loop_first=c->create_loop_first;instrument->loop_last=c->create_loop_last;
        instrument->loop_mode=c->create_loop_mode;instrument->loop_crossfade_ms=c->create_crossfade;
        instrument->tuning=c->create_tuning;instrument->audible_tuning=c->create_audible_tuning;
        snprintf(instrument->current.name,sizeof(instrument->current.name),"%s",c->create_original.name);
    }
    unlock_edit(device,audio,ui,instrument);
    if(!ok){snprintf(ui->status,sizeof(ui->status),"CREATE RESTORE FAILED: %.130s",error);return 0;}
    c->create_hash=ts_sample_hash(&instrument->current);ui->cdp_create=0;
    if(c->mosaic)mosaic_commit(device,ui,instrument,c->mosaic);
    ts_ui_reset_parent_view(ui,instrument->current.frames);return 1;
}
static int portal_create_event(const SDL_Event *event,int x,int y,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsInstrument *instrument,
    PortalController *c,TransformController *transform)
{
    int click=event->type==SDL_MOUSEBUTTONDOWN && x>=85 && x<167 && y>=205 && y<228 && !ui->mosaic_open;
    int escape=event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE;
    if(ui_dialog_open(ui))return 0;
    if(c->create_roll && escape) {
        portal_cancel(c);c->quick_apply=c->create_retry=0;
        snprintf(ui->status,sizeof(ui->status),"CANCELLING CDP ROLL - FRESH FM KEPT");return 1;
    }
    if(!click)return 0;
    int button=event->button.button;
    if(button!=SDL_BUTTON_LEFT && button!=SDL_BUTTON_RIGHT && button!=SDL_BUTTON_MIDDLE)return 0;
    if((transform && transform->worker) || portal_edit_gesture_active(ui) ||
       ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active ||
       (c->worker && !c->create_roll)) {
        snprintf(ui->status,sizeof(ui->status),"FINISH CURRENT EDIT, CAPTURE OR RENDER BEFORE CREATE");return 1;
    }
    if(c->create_roll) {
        portal_cancel(c);c->quick_apply=0;c->create_retry=button==SDL_BUTTON_RIGHT;
        if(c->create_retry) {
            snprintf(ui->status,sizeof(ui->status),"REROLLING CDP FROM THE SAME CREATE SOURCE");return 1;
        }
    }
    if(button==SDL_BUTTON_MIDDLE) {
        ui->cdp_create=0;c->create_ready=0;
        if(portal_create_restore(device,audio,ui,instrument,c)) {
            if(!c->worker)portal_invalidate(device,audio,&ui->portal,c);
            snprintf(ui->status,sizeof(ui->status),"ORIGINAL CREATE RESTORED - LEFT: FM / RIGHT: CDP / MIDDLE: RESTORE");
        }
        return 1;
    }
    if(button==SDL_BUTTON_LEFT) {
        if(!c->create_seeds)return 0;
        if(generate_family_candidate(device,audio,ui,instrument,c->create_seeds,0,0,0,NULL)) {
            c->create_ready=0;ui->cdp_create=0;
            if(portal_create_keep(c,ui,instrument))
                snprintf(ui->status,sizeof(ui->status),"FRESH FM - LEFT CREATE: NEW FM / RIGHT: CDP VARIATION / MIDDLE: RESTORE FM");
        }
        return 1;
    }
    if(!instrument->current.data || instrument->current.frames<2) {
        snprintf(ui->status,sizeof(ui->status),"LEFT-CLICK CREATE FOR A WAVEFORM FIRST");return 1;
    }
    TsCdpRuntime runtime;ts_cdp_runtime_init(&runtime);char error[160];char *base=SDL_GetBasePath();
    int available=ts_cdp_runtime_discover(&runtime,ui->config.cdp_bin_path,base,error,sizeof(error));SDL_free(base);
    if(!available){snprintf(ui->status,sizeof(ui->status),"CREATE + CDP: %.140s",error);return 1;}
    /* A different tile or a later manual edit starts a new comparison source.
       A reroll of our own result always returns to the retained clean sample. */
    if(!portal_create_matches(c,ui,instrument) && !portal_create_keep(c,ui,instrument))return 1;
    if(c->create_selection && (c->create_last<=c->create_first ||
       c->create_last-c->create_first<c->create_original.sample_rate/10u)) {
        snprintf(ui->status,sizeof(ui->status),"CREATE + CDP NEEDS A SELECTION OF AT LEAST 100 MS");return 1;
    }
    if(!portal_create_restore(device,audio,ui,instrument,c))return 1;
    c->create_ready=0;c->selection_scope=c->create_selection;
    TsPortalUi *p=&ui->portal;
    if(!portal_source(device,audio,ui,instrument,c)) {
        snprintf(ui->status,sizeof(ui->status),"FRESH FM KEPT; CDP: %.130s",p->message);return 1;
    }
    TsPortalRecipe r;
    if(!ts_portal_create_recipe(ts_fm_seed_sequence_next(&c->create_dice),&c->source,&r,error,sizeof(error))) {
        snprintf(ui->status,sizeof(ui->status),"FRESH FM KEPT; %.140s",error);return 1;
    }
    portal_load_recipe(p,&r);p->macro_view=0;p->parameter_scroll=0;
    p->selected_tab=p->selected_slot=-1;p->number_focus=-1;
    portal_preview(device,audio,ui,c);
    c->create_roll=c->quick_apply=c->worker!=NULL;
    c->create_transform=transform;ui->cdp_creating=c->create_roll;ui->cdp_create=c->create_roll;
    if(c->create_roll)snprintf(ui->status,sizeof(ui->status),"CREATING %.39s - RIGHT: REROLL / MIDDLE: RESTORE FM",r.name);
    else snprintf(ui->status,sizeof(ui->status),"FRESH FM KEPT; CDP: %.130s",p->message);
    return 1;
}
