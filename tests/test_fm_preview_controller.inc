static void finish_fm_preview(AudioState *a,TsUiState *u,TsSample *preview)
{
    u->fm_preview_due=SDL_GetTicks();
    Uint32 deadline=SDL_GetTicks()+10000;
    while(u->fm_preview_pending || u->fm_preview_job) {
        assert(!SDL_TICKS_PASSED(SDL_GetTicks(),deadline));
        poll_fm_preview(0,a,u,preview);SDL_Delay(1);
    }
}

static uint64_t fm_waveform_hash(TsUiState *u,TsInstrument *inst)
{
    static TsFramebuffer fb;ts_ui_render(&fb,u,inst);
    uint64_t hash=1469598103934665603ull;
    for(int y=63;y<109;++y)for(int x=22;x<618;++x)
        hash=(hash^fb.pixels[y*TS_UI_WIDTH+x])*1099511628211ull;
    return hash;
}

static void test_fm_preview_updates(void)
{
    AudioState *a=routing_audio(0);a->mosaic->playing=0;
    TsUiState *u=calloc(1,sizeof(*u));TsInstrument *inst=calloc(1,sizeof(*inst));
    MosaicController *c=calloc(1,sizeof(*c));assert(u && inst && c);
    ts_ui_init(u);u->mosaic=a->mosaic;ts_instrument_init(inst);
    TsSample preview={0},expected={0};char error[160];
    assert(ts_instrument_generate(inst,TS_GENERATOR_FM,777,error,sizeof(error)));
    workspace_open_fm(0,a,u,inst,c,&preview);
    /* An intentionally quiet source used to trap the button in Unison because
       the UI rolled back the toggle when strict tile rendering rejected it. */
    u->fm_patch.active_mask=0;u->fm_patch.transient_mix=0;u->fm_patch.drone_mode=1;
    TsFmPatch original=u->fm_patch;
    toggle_fm_unison_workspace(u,inst);finish_fm_preview(a,u,&preview);
    assert(ts_fm_sample_is_usable(&preview));u->fm_voice_bank=1;
    for(int i=6;i<12;++i) {
        assert(ts_fm_step_control(&u->fm_patch,TS_FM_PAGE_PITCH,i,i&1?1:-1,0));
        request_fm_preview(u,inst);
    }
    toggle_fm_unison_workspace(u,inst);
    assert(!u->fm_voice_bank && !memcmp(&original,&u->fm_patch,sizeof(original)));
    finish_fm_preview(a,u,&preview);
    assert(ts_sample_peak(&preview)==0 && strstr(u->fm_message,"QUIET"));
    uint64_t quiet_wave=fm_waveform_hash(u,inst);
    toggle_fm_unison_workspace(u,inst);finish_fm_preview(a,u,&preview);
    assert(ts_fm_sample_is_usable(&preview) && fm_waveform_hash(u,inst)!=quiet_wave);

    TsNoteEvent note;assert(ts_note_event_qwerty(&note,0,60));
    a->notes.workbench_loop=1;
    assert(ts_note_bank_start_sample_event(&a->notes,&preview,&inst->tuning,&note,1,48000)==TS_NOTE_STARTED);
    const TsNoteVoice *held=ts_note_bank_display_voice(&a->notes);assert(held && held->sample==&preview);
    /* All-off must replace both the cached picture and the held synth's buffer. */
    for(int i=0;i<12;++i)toggle_fm_voice_workspace(u,inst,i);
    finish_fm_preview(a,u,&preview);
    assert(!u->fm_patch.active_mask && ts_sample_peak(&preview)==0);
    assert(fm_waveform_hash(u,inst)==quiet_wave);
    assert(ts_note_bank_latched_synth_count(&a->notes)==1);
    for(int i=0;i<128;++i){TsStereoFrame f=ts_note_bank_read_stereo(&a->notes);assert(f.l==0 && f.r==0);}

    /* Complete an obsolete render before polling: it must never publish over
       more recent toggles, even while the next render is waiting to start. */
    uint64_t quiet_hash=ts_sample_hash(&preview);
    uint32_t revision=u->waveform_revisions[TS_UI_WAVEFORM_FM];
    toggle_fm_voice_workspace(u,inst,6);u->fm_preview_due=SDL_GetTicks();
    poll_fm_preview(0,a,u,&preview);FmPreviewJob *stale=u->fm_preview_job;assert(stale);
    toggle_fm_voice_workspace(u,inst,6);toggle_fm_voice_workspace(u,inst,11);
    u->fm_preview_due=SDL_GetTicks()+10000;
    Uint32 deadline=SDL_GetTicks()+10000;
    while(!SDL_AtomicGet(&stale->done)){assert(!SDL_TICKS_PASSED(SDL_GetTicks(),deadline));SDL_Delay(1);}
    poll_fm_preview(0,a,u,&preview);
    assert(!u->fm_preview_job && u->fm_preview_pending);
    assert(ts_sample_hash(&preview)==quiet_hash && u->waveform_revisions[TS_UI_WAVEFORM_FM]==revision);
    finish_fm_preview(a,u,&preview);
    const TsTuning unity={TS_KEYBOARD_BASE_NOTE,0};
    assert(ts_fm_render_preview(&expected,&u->fm_patch,TS_FM_LOGIC_SECONDS,
        (float)ts_tuning_frequency(&unity),44100,inst->generator.seed^0x50524556u,error,sizeof(error)));
    assert(ts_sample_hash(&preview)==ts_sample_hash(&expected));
    assert(ts_fm_sample_is_usable(&preview) && fm_waveform_hash(u,inst)!=quiet_wave);
    double energy=0;for(int i=0;i<1024;++i)energy+=fabsf(ts_note_bank_read_stereo(&a->notes).l);
    assert(energy>1 && ts_note_bank_latched_synth_count(&a->notes)==1);

    /* Parking still retains edits on the same tile, but not a deleted/replaced
       source or an identically sounding tile in another slot. */
    TsFmPatch edited=u->fm_patch;
    workspace_park_fm(u,c);workspace_open_fm(0,a,u,inst,c,&preview);
    assert(!memcmp(&edited,&u->fm_patch,sizeof(edited)));
    assert(ts_instrument_copy_selected(inst,1,error,sizeof(error)));
    workspace_park_fm(u,c);assert(ts_instrument_select_bank(inst,1,error,sizeof(error)));
    workspace_open_fm(0,a,u,inst,c,&preview);assert(u->fm_patch.structure!=TS_FM_STRUCTURE_UNISON);
    assert(ts_instrument_apply_fm_patch(inst,&edited,error,sizeof(error)));
    begin_fm_workspace(0,a,u,inst,&preview);assert(u->fm_patch.structure==TS_FM_STRUCTURE_UNISON);
    workspace_park_fm(u,c);
    assert(ts_instrument_bank_clear(inst,inst->selected_slot,error,sizeof(error)));
    workspace_open_fm(0,a,u,inst,c,&preview);
    assert(u->fm_patch.structure!=TS_FM_STRUCTURE_UNISON && !u->fm_patch.has_unison_source);
    TsFmSeedSequence seeds;ts_fm_seed_sequence_init(&seeds,987);
    assert(ts_instrument_create_selected_fresh(inst,&seeds,NULL,error,sizeof(error)));
    toggle_fm_unison_workspace(u,inst);workspace_park_fm(u,c);
    assert(ts_instrument_bank_clear(inst,inst->selected_slot,error,sizeof(error)));
    assert(ts_instrument_create_selected_fresh(inst,&seeds,NULL,error,sizeof(error)));
    TsGeneratorRecipe recipe=current_fm_workspace_recipe(inst);TsFmPatch fresh;
    ts_fm_patch_from_recipe(&recipe,&fresh);
    workspace_open_fm(0,a,u,inst,c,&preview);
    assert(!u->fm_preview_job && !u->fm_preview_pending && !memcmp(&fresh,&u->fm_patch,sizeof(fresh)));
    request_fm_preview(u,inst);u->fm_preview_due=SDL_GetTicks();poll_fm_preview(0,a,u,&preview);
    assert(u->fm_preview_job);close_fm_workspace(0,a,u,&preview);
    assert(!u->fm_preview_job && !u->fm_preview_pending && !preview.data);
    ts_sample_free(&expected);ts_instrument_free(inst);free(inst);free(u);free(c);routing_free(a);
}

static void test_basic_create_controller(void)
{
    AudioState *a=routing_audio(0);a->mosaic->playing=0;
    TsUiState *u=calloc(1,sizeof(*u));TsInstrument *inst=calloc(1,sizeof(*inst));
    PortalController *portal=calloc(1,sizeof(*portal));assert(u && inst && portal);
    ts_ui_init(u);u->mosaic=a->mosaic;ts_instrument_init(inst);portal_init(portal,&u->portal);
    TsFmSeedSequence seeds;ts_fm_seed_sequence_init(&seeds,999);portal->create_seeds=&seeds;
    TsSample preview={0};char error[160];
    /* Recreate the user's tile three: one second of exact silence, no genome,
       following a previous FM tile. It must not inherit that tile's patch. */
    assert(ts_instrument_create_basic(inst,TS_FM_WAVE_SAW,error,sizeof(error)));
    assert(ts_instrument_select_bank(inst,2,error,sizeof(error)));
    assert(ts_instrument_activate_silence(inst,44100,44100,error,sizeof(error)));
    const char *residue_project=SDL_getenv("TS_TEST_FM_RESIDUE_PROJECT");
    if(residue_project) {
        assert(ts_instrument_load_recipe(inst,residue_project,error,sizeof(error)));
        assert(ts_instrument_select_bank(inst,2,error,sizeof(error)));
        assert(inst->current.frames==44100 && ts_sample_peak(&inst->current)==0);
    }
    begin_fm_workspace(0,a,u,inst,&preview);
    assert(!u->fm_patch.active_mask && u->fm_patch.filter_mode==TS_FM_FILTER_CLEAN);
    assert(ts_sample_peak(&preview)==0);
    toggle_fm_voice_workspace(u,inst,0);finish_fm_preview(a,u,&preview);
    assert(ts_sample_peak(&preview)>.7f && u->fm_patch.waveforms[0]==TS_FM_WAVE_SINE);
    u->fm_patch.transient_mix=.6f;u->fm_patch.drone_mode=0;
    toggle_fm_voice_workspace(u,inst,0);finish_fm_preview(a,u,&preview);
    assert(ts_sample_peak(&preview)==0);
    close_fm_workspace(0,a,u,&preview);
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;
    const int waveforms[]={TS_FM_WAVE_SINE,TS_FM_WAVE_SQUARE,TS_FM_WAVE_SAW,TS_FM_WAVE_TRIANGLE};
    SDL_SetModState(KMOD_SHIFT);
    for(int n=0;n<5;++n) {
        assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
        assert(inst->selected_slot==2 && inst->generator.fm_patch.waveforms[0]==waveforms[n%4]);
        assert(inst->generator.fm_patch.active_mask==1 && u->basic_create_index==(n+1)%4);
        assert(strstr(u->status,"SHIFT-CREATE NEXT"));
        assert(ts_sample_hash(&inst->current)==ts_sample_hash(&portal->create_original));
    }
    uint64_t hash=ts_sample_hash(&inst->current);int next=u->basic_create_index;
    inst->bank[2].locked=1;
    assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
    assert(strstr(u->status,"FAILED") && u->basic_create_index==next && ts_sample_hash(&inst->current)==hash);
    inst->bank[2].locked=0;
    assert(ts_instrument_apply_smear(inst,.4f,error,sizeof(error)));
    event.button.button=SDL_BUTTON_MIDDLE;
    /* A manual edit invalidates restore instead of discarding it. */
    assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
    assert(strstr(u->status,"CURRENT EDITS KEPT"));
    event.button.button=SDL_BUTTON_LEFT;
    assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
    inst->has_selection=1;inst->selection_first=4410;inst->selection_last=22050;
    assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
    assert(strstr(u->status,"IN SELECTION") && inst->has_selection);
    begin_fm_workspace(0,a,u,inst,&preview);
    assert(u->fm_patch.waveforms[0]==TS_FM_WAVE_SAW && u->fm_patch.filter_mode==TS_FM_FILTER_CLEAN);
    close_fm_workspace(0,a,u,&preview);
    /* Ordinary Create stays on its existing FM path and leaves the cycle alone. */
    SDL_SetModState(KMOD_NONE);inst->has_selection=0;next=u->basic_create_index;
    assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
    assert(u->basic_create_index==next && strstr(u->status,"FRESH FM"));
    /* The same gesture belongs to the open Mosaic event, and is committed
       through its existing explicit destination choice on returning. */
    MosaicController *mc=calloc(1,sizeof(*mc));assert(mc);portal->mosaic=mc;
    TsMosaicSource *source=ts_mosaic_source(u->mosaic,&inst->current,error,sizeof(error));assert(source);
    TsMosaicEvent *owner=ts_mosaic_add(u->mosaic,source,0,0);assert(owner);
    assert(mosaic_enter(0,a,u,inst,mc,owner->id));
    SDL_SetModState(KMOD_SHIFT);
    assert(portal_create_event(&event,100,214,0,a,u,inst,portal,NULL));
    hash=ts_sample_hash(&inst->current);
    assert(inst->generator.fm_patch.filter_mode==TS_FM_FILTER_CLEAN);
    assert(portal->create_event==owner->id && ts_sample_hash(&portal->create_original)==hash);
    assert(!mosaic_leave(0,a,u,inst,mc) && u->mosaic_edit_choice);
    assert(mosaic_resolve_edit(0,a,u,inst,mc,3));
    assert(owner->source->hash==hash && !mc->active);
    SDL_SetModState(KMOD_NONE);
    portal_free(portal);free(portal);
    mosaic_controller_free(mc);free(mc);
    ts_instrument_free(inst);free(inst);free(u);routing_free(a);
}
