/* One offline job per open editor. Input is cloned before rendering, UI changes
   stay in controls, and publication swaps owned buffers under a short lock. */
typedef struct {
    SDL_Thread *thread;SDL_atomic_t done;
    TsInstrument *document;TsBankSlot owner_bank;
    const float *owner_parent,*owner_current;uint32_t owner_generation,owner_revision;int owner_slot;
    TsSample dry;TsMasterEqControls requested;
    int phase,ok,preview;unsigned saved_rate;int saved_spectrum;
    char error[160];
} CanvasEqController;
static CanvasEqController canvas_eq;
static int canvas_eq_worker(void *opaque)
{
    CanvasEqController *c=opaque;c->ok=0;
    if(c->phase==1) {
        TsMasterEqControls flat;ts_master_eq_default(&flat);
        c->ok=ts_instrument_render_tile_eq(&c->dry,c->document,&flat,c->error,sizeof(c->error));
    } else c->ok=ts_instrument_set_tile_eq(c->document,&c->requested,c->error,sizeof(c->error));
    SDL_AtomicSet(&c->done,1);return 0;
}
static void canvas_eq_stop(SDL_AudioDeviceID device,AudioState *audio)
{
    if(device)SDL_LockAudioDevice(device);
    if(audio->sample==&canvas_eq.dry){audio->playing=0;audio->sample=NULL;}
    audio->canvas_eq_preview=0;canvas_eq.preview=0;
    if(device)SDL_UnlockAudioDevice(device);
}
static void canvas_eq_close(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!ui->canvas_eq_open && !canvas_eq.document && !canvas_eq.thread)return;
    canvas_eq_stop(device,audio);
    if(canvas_eq.thread)SDL_WaitThread(canvas_eq.thread,NULL);
    if(canvas_eq.document){ts_instrument_free(canvas_eq.document);free(canvas_eq.document);}
    ts_sample_free(&canvas_eq.dry);
    ui->master_eq_rate=canvas_eq.saved_rate;ui->master_eq_spectrum_off=canvas_eq.saved_spectrum;
    memset(&canvas_eq,0,sizeof(canvas_eq));ui->canvas_eq_open=ui->canvas_eq_busy=ui->master_eq_open=ui->master_eq_drag=0;
}
static void canvas_eq_open(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,const TsInstrument *instrument)
{
    if(!instrument->current.data || ui->canvas_eq_open || instrument->selected_slot<0 || instrument->selected_slot>=TS_BANK_SLOT_COUNT)return;
    int slot=instrument->selected_slot;
    if(slot>=0 && instrument->bank[slot].locked){snprintf(ui->status,sizeof(ui->status),"UNLOCK THIS TILE BEFORE EDITING EQ.");return;}
    memset(&canvas_eq,0,sizeof(canvas_eq));canvas_eq.phase=1;
    canvas_eq.owner_slot=slot;canvas_eq.owner_bank=instrument->bank[slot];
    canvas_eq.owner_parent=instrument->parent.data;canvas_eq.owner_current=instrument->current.data;
    canvas_eq.owner_generation=instrument->generation;canvas_eq.owner_revision=instrument->current.visual_revision;
    canvas_eq.saved_rate=ui->master_eq_rate;canvas_eq.saved_spectrum=ui->master_eq_spectrum_off;
    ui->canvas_eq=instrument->tile_eq;if(ui->canvas_eq.band[0].frequency==0)ts_master_eq_default(&ui->canvas_eq);
    ui->canvas_eq_open=ui->master_eq_open=1;ui->canvas_eq_busy=1;
    ui->master_eq_drag=ui->master_eq_presets_open=ui->master_eq_reset_pending=0;ui->master_eq_spectrum_off=1;
    ui->master_eq_rate=instrument->current.sample_rate;
    if(device)SDL_LockAudioDevice(device);
    ts_master_eq_init(&audio->canvas_eq);ts_master_eq_set(&audio->canvas_eq,&ui->canvas_eq);
    ts_master_eq_prepare(&audio->canvas_eq,(unsigned)audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    canvas_eq.document=malloc(sizeof(*canvas_eq.document));
    if(canvas_eq.document)ts_instrument_init(canvas_eq.document);
    if(!canvas_eq.document || !ts_instrument_clone(canvas_eq.document,instrument,canvas_eq.error,sizeof(canvas_eq.error))) {
        canvas_eq_close(device,audio,ui);snprintf(ui->status,sizeof(ui->status),"CANNOT PREPARE EQ DOCUMENT - TILE UNCHANGED");return;
    }
    canvas_eq.thread=SDL_CreateThread(canvas_eq_worker,"canvas-eq",&canvas_eq);
    if(!canvas_eq.thread){canvas_eq_close(device,audio,ui);snprintf(ui->status,sizeof(ui->status),"CANNOT START EQ RENDER");}
    else snprintf(ui->status,sizeof(ui->status),"TILE EQ: PREPARING AUDITION / ORIGINAL AUDIO IS KEPT");
}
static void canvas_eq_preview(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(canvas_eq.thread || !canvas_eq.dry.data)return;
    if(canvas_eq.preview){canvas_eq_stop(device,audio);return;}
    if(device)SDL_LockAudioDevice(device);
    audio->sample=&canvas_eq.dry;audio->playing=1;audio->bank_slot=-1;
    audio->position=0;audio->range_start=0;audio->range_end=canvas_eq.dry.frames;
    audio->looping=1;audio->loop_mode=TS_LOOP_FORWARD;audio->loop_direction=1;audio->loop_intro=0;audio->crossfade_frames=0;
    audio->step=(double)canvas_eq.dry.sample_rate/audio->output_rate;audio->pitch=1;
    audio->attack_frame=0;audio->attack_frames=ts_audition_attack_frames(audio->output_rate,5);
    audio->canvas_eq_preview=1;canvas_eq.preview=1;
    ts_source_route_set(&audio->audition_route,(TsSourceRoute){0},audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),"EQ AUDITION: SPACE TO STOP / APPLY KEEPS AN EDITABLE CURVE");
}
static void canvas_eq_apply(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(canvas_eq.thread || !canvas_eq.document)return;
    canvas_eq_stop(device,audio);canvas_eq.requested=ui->canvas_eq;canvas_eq.requested.solo_band=0;
    canvas_eq.phase=2;SDL_AtomicSet(&canvas_eq.done,0);ui->canvas_eq_busy=1;
    canvas_eq.thread=SDL_CreateThread(canvas_eq_worker,"canvas-eq-apply",&canvas_eq);
    if(!canvas_eq.thread){ui->canvas_eq_busy=0;snprintf(ui->status,sizeof(ui->status),"CANNOT START EQ RENDER - SETTINGS KEPT");}
}
static void canvas_eq_poll(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsInstrument *instrument)
{
    if(!ui->canvas_eq_open || !canvas_eq.thread || !SDL_AtomicGet(&canvas_eq.done))return;
    SDL_WaitThread(canvas_eq.thread,NULL);canvas_eq.thread=NULL;ui->canvas_eq_busy=0;
    if(!canvas_eq.ok){snprintf(ui->status,sizeof(ui->status),"EQ RENDER FAILED: %.130s",canvas_eq.error);return;}
    if(canvas_eq.phase==1){snprintf(ui->status,sizeof(ui->status),"TILE EQ: SPACE AUDITIONS / APPLY COMMITS / ESC CANCELS");return;}
    if(instrument->selected_slot!=canvas_eq.owner_slot || instrument->generation!=canvas_eq.owner_generation ||
       instrument->parent.data!=canvas_eq.owner_parent || instrument->current.data!=canvas_eq.owner_current ||
       instrument->current.visual_revision!=canvas_eq.owner_revision ||
       memcmp(&instrument->bank[canvas_eq.owner_slot],&canvas_eq.owner_bank,sizeof(canvas_eq.owner_bank))) {
        canvas_eq_close(device,audio,ui);
        snprintf(ui->status,sizeof(ui->status),"EQ NOT APPLIED: TILE CHANGED WHILE THE EDITOR WAS OPEN. REOPEN EQ TO CONTINUE.");return;
    }
    if(!runtime_note_preserve_workspace(device,audio,ui))return;
    TsInstrument *doc=canvas_eq.document;int slot=instrument->selected_slot;
    if(device)SDL_LockAudioDevice(device);
    TsSample old=instrument->current;instrument->current=doc->current;doc->current=old;
    TsBankSlot old_slot=instrument->bank[slot];instrument->bank[slot]=doc->bank[slot];doc->bank[slot]=old_slot;
    instrument->tile_eq=doc->tile_eq;
    memcpy(instrument->undo,doc->undo,sizeof(instrument->undo));memcpy(instrument->redo,doc->redo,sizeof(instrument->redo));
    instrument->undo_count=doc->undo_count;instrument->redo_count=doc->redo_count;
    runtime_note_sync(audio,instrument,audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    ts_ui_waveform_cache_invalidate(ui,TS_UI_WAVEFORM_MAIN);canvas_eq_close(device,audio,ui);
    snprintf(ui->status,sizeof(ui->status),ui->mosaic_editing?"INSTANCE EQ APPLIED - RETURN TO MOSAIC TO CHOOSE UPDATE INSTANCE / ALL / NEW":"TILE EQ APPLIED - CTRL+Z TO UNDO / EQ REOPENS THE SAME CURVE");
}
static int canvas_eq_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,SisterWindow *sister,const TsInstrument *instrument)
{
    int own=event_window_id(event)==SDL_GetWindowID(window),click=event->type==SDL_MOUSEBUTTONDOWN;
    int x=-1,y=-1;if(click && own)logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(!ui->canvas_eq_open) {
        if(click && own && event->button.button==SDL_BUTTON_LEFT && x>=568 && x<596 && y>=43 && y<60 &&
           !ui->mosaic_open && !ui->tracker_open && !ui->fm_open && !ui_dialog_open(ui) && !ui->master_eq_open && !ui->router_open) {
            canvas_eq_open(device,audio,ui,instrument);return 1;
        }
        return 0;
    }
    if(event->type==SDL_QUIT)return 0;
    if(event->type==SDL_KEYUP)return 0; /* Release held notes. */
    if(!own)return event->type!=SDL_WINDOWEVENT;
    if(canvas_eq.thread)return event->type!=SDL_WINDOWEVENT;
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_SPACE) {
        if(!event->key.repeat)canvas_eq_preview(device,audio,ui);return 1;
    }
    if(click && y>=337 && y<368) {
        if(x>=20 && x<140)canvas_eq_preview(device,audio,ui);
        else if(x>=392 && x<494)canvas_eq_apply(device,audio,ui);
        else if(x>=510 && x<618)canvas_eq_close(device,audio,ui);
        return 1;
    }
    /* Reuse the five-band curve's drag/wheel/reset/preset controls. Temporarily
       bind their control value, while commit targets only the audition EQ. */
    if(click && (y<38 || y>=379))return 1;
    if(click && x>=312 && x<388 && y>=44 && y<68)return 1;
    TsMasterEqControls master=ui->config.master_eq;ui->config.master_eq=ui->canvas_eq;
    canvas_eq_handling=1;master_eq_event(event,window,device,audio,ui,sister);canvas_eq_handling=0;
    ui->canvas_eq=ui->config.master_eq;ui->config.master_eq=master;
    if(!ui->master_eq_open)canvas_eq_close(device,audio,ui);
    return event->type!=SDL_WINDOWEVENT;
}
