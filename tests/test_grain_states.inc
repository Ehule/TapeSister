static void test_grain_state_dsp(void)
{
    static TsSisterPostFxEngine engine;TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine,1000));ts_sister_fx_controls_default(&controls);
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i)controls.slot[i]=ts_sister_fx_empty_slot();
    controls.enabled=1;TsSisterFxSlotControls *slot=&controls.slot[0];slot->type=TS_SISTER_FX_GRAIN;slot->mix=.8f;
    slot->parameter_a=.2f;slot->parameter_b=.3f;slot->parameter_c=.25f;ts_grain_state_capture(slot,0);
    slot->parameter_a=.8f;slot->parameter_b=.9f;slot->parameter_c=.75f;ts_grain_state_capture(slot,1);
    slot->grain_states.seconds=2;assert(ts_grain_state_recall(slot,0,0));
    ts_sister_post_fx_sync_controls(&engine,&controls);
    for(int i=0;i<500;++i)ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);
    TsSisterGrainState *grain=&engine.grain[0][4];float *buffer=grain->data;size_t history=grain->history_frames;
    assert(ts_grain_state_recall(slot,1,0));ts_sister_post_fx_set_controls(&engine,&controls);
    for(int i=0;i<500;++i){TsStereoFrame f=ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);assert(isfinite(f.l) && isfinite(f.r));}
    assert(grain->pitch_current>.33f && grain->pitch_current<.39f && grain->state_remaining==1500);
    assert(grain->data==buffer && grain->history_frames>=history);
    float audible=grain->pitch_current;assert(ts_grain_state_recall(slot,0,0));ts_sister_post_fx_set_controls(&engine,&controls);
    ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);
    assert(fabsf(grain->pitch_current-audible)<.001f && grain->state_remaining==1999);
    for(int i=0;i<2200;++i)ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);
    assert(fabsf(grain->pitch_current-.25f)<.001f && !grain->state_remaining);
    assert(ts_grain_state_recall(slot,1,0));ts_sister_post_fx_set_controls(&engine,&controls);
    ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);
    slot->parameter_c=.5f;ts_sister_post_fx_set_controls(&engine,&controls);
    ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);assert(!grain->state_remaining);
    assert(ts_grain_state_changed(slot));ts_grain_state_capture(slot,1);assert(!ts_grain_state_changed(slot));
    /* Editing interrupts a long performance glide, even when recalling the
       same target. Keep dezippering and the existing grain history. */
    assert(ts_grain_state_recall(slot,0,0));ts_sister_post_fx_set_controls(&engine,&controls);
    ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);assert(grain->state_remaining==1999);
    assert(ts_grain_state_recall(slot,0,1));ts_sister_post_fx_set_controls(&engine,&controls);
    ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);assert(!grain->state_remaining && grain->data==buffer);
    for(int i=0;i<200;++i)ts_sister_post_fx_process(&engine,3,(TsStereoFrame){.1f,.2f},0);
    assert(fabsf(grain->pitch_current-.25f)<.001f && slot->grain_states.seconds==2);
    ts_sister_post_fx_free(&engine);
}
static void test_grain_state_ui(void)
{
    TsSisterParameters saved=sister.model.parameters;
    sister.model.fx_scroll=0;sister.model.fx_page=1;
    TsSisterFxSlotControls *slot=&sister.model.parameters.fx.slot[1];sister_fx_slot_select_type(slot,TS_SISTER_FX_GRAIN);
    slot->grain_states=(TsGrainStates){0};
    int pitch_parameter=TS_SISTER_UI_SLOT_PARAMETER(1,3);
    slot->parameter_c=sister_parameter_wheel_normalized(&sister.model.parameters,pitch_parameter,7,0);
    assert(fabsf(ts_sister_grain_pitch_semitones(slot->parameter_c)-7)<1e-5f);
    slot->parameter_c=sister_parameter_wheel_normalized(&sister.model.parameters,pitch_parameter,1,1);
    assert(fabsf(ts_sister_grain_pitch_semitones(slot->parameter_c)-7.1f)<1e-5f);
    slot->parameter_c=.5f;
    int y=TS_SISTER_UI_FX_TOP+TS_SISTER_UI_FX_ROW_STEP+32;
    assert(ts_sister_ui_hit_test_model(&sister.model,395,y).action==TS_SISTER_UI_ACTION_GRAIN_STATE);
    assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));assert(slot->grain_states.captured==1);
    float captured=slot->parameter_c;slot->parameter_c=.75f;
    assert(ts_grain_state_changed(slot));assert(mouse(SDL_MOUSEBUTTONDOWN,417,y));assert(slot->grain_states.captured==3);
    assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));assert(slot->parameter_c==captured && slot->grain_states.selected==1);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_RIGHT;e.button.x=395;e.button.y=y;
    assert(sister_pedalboard_event(0,&audio,&ui,&sister,&e));assert(slot->grain_states.editing);
    ts_sister_ui_render(&frame,&sister.model,&ui.palette);assert(frame.pixels[y*TS_UI_WIDTH+395]==0xff4387deu); /* blue */
    slot->parameter_c=.6f;
    ts_sister_ui_render(&frame,&sister.model,&ui.palette);assert(frame.pixels[y*TS_UI_WIDTH+395]==0xffe5a53au); /* amber */
    uint32_t recall=slot->grain_states.recall_serial;
    assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));
    assert(slot->grain_states.value[0][2]==.6f && !ts_grain_state_changed(slot) && !slot->grain_states.editing);
    assert(slot->grain_states.recall_serial==recall && slot->grain_states.value[1][2]==.75f);
    ts_sister_ui_render(&frame,&sister.model,&ui.palette);assert(frame.pixels[y*TS_UI_WIDTH+395]==0xff4ac16cu); /* green */
    /* A direct live edit is amber too, and saves with the same plain click. */
    slot->parameter_c=.65f;assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));assert(slot->grain_states.value[0][2]==.65f);
    assert(mouse(SDL_MOUSEBUTTONDOWN,417,y));assert(slot->parameter_c==.75f);
    assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));assert(slot->parameter_c==.65f);
    /* Unchanged blue exits editing on save; Shift still explicitly overwrites
       any state, and middle clearing keeps the other saved sounds. */
    assert(sister_pedalboard_event(0,&audio,&ui,&sister,&e));assert(slot->grain_states.editing);
    assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));assert(!slot->grain_states.editing && slot->parameter_c==.65f);
    slot->parameter_c=.6f;SDL_SetModState(KMOD_SHIFT);assert(mouse(SDL_MOUSEBUTTONDOWN,395,y));SDL_SetModState(KMOD_NONE);
    assert(slot->grain_states.value[0][2]==.6f);
    e.button.x=439;e.button.button=SDL_BUTTON_RIGHT;assert(sister_pedalboard_event(0,&audio,&ui,&sister,&e));
    assert(slot->parameter_c==.6f && slot->grain_states.editing && slot->grain_states.captured==3);
    slot->parameter_c=.8f;assert(mouse(SDL_MOUSEBUTTONDOWN,439,y));assert(slot->grain_states.captured==7);
    e.button.button=SDL_BUTTON_MIDDLE;assert(sister_pedalboard_event(0,&audio,&ui,&sister,&e));
    assert(slot->grain_states.captured==3 && slot->parameter_c==.8f && !slot->grain_states.selected);
    e.button.button=SDL_BUTTON_RIGHT;
    assert(mouse(SDL_MOUSEBUTTONDOWN,619,y));assert(slot->grain_states.seconds>599);
    assert(mouse(SDL_MOUSEBUTTONUP,619,y) && !sister.grain_time_drag);
    e.button.x=519;assert(sister_pedalboard_event(0,&audio,&ui,&sister,&e));assert(slot->grain_states.seconds==0);
    /* Storage follows the slot when moved, and time/values survive both a
       project save and a user pedalboard preset. Runtime command IDs do not. */
    slot->grain_states.seconds=240;
    static TsSisterProjectState project,loaded;ts_sister_project_state_init(&project,48000);
    project.parameters=sister.model.parameters;
    assert(ts_sister_project_state_save_file(&project,"grain-state-test.ini",error,sizeof(error)));
    int present=0;assert(ts_sister_project_state_load_file(&loaded,"grain-state-test.ini",48000,&present,error,sizeof(error)) && present);
    assert(loaded.parameters.fx.slot[1].grain_states.seconds==240 && loaded.parameters.fx.slot[1].grain_states.captured==3);
    assert(loaded.parameters.fx.slot[1].grain_states.value[0][2]==.6f && !loaded.parameters.fx.slot[1].grain_states.recall_serial);
    remove("grain-state-test.ini");
    static TsSisterPresetBank presets,restored;ts_sister_preset_bank_init(&presets,48000);
    assert(ts_sister_preset_save_new(&presets,"GRAIN STATES",&project.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&presets,"grain-preset-test.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(&restored,"grain-preset-test.ini",48000,error,sizeof(error)));
    assert(restored.entries[restored.count-1].parameters.fx.slot[1].grain_states.value[0][2]==.6f);
    remove("grain-preset-test.ini");
    sister_apply_action(0,&audio,&ui,&bank,&sister,NULL,NULL,(TsSisterUiHit){.action=TS_SISTER_UI_ACTION_FX_SLOT_MOVE,.index=1<<8},48000,2);
    assert(sister.model.parameters.fx.slot[0].grain_states.captured==3);
    if(getenv("TS_GRAIN_SCREENSHOT")) {
        slot=&sister.model.parameters.fx.slot[0];ts_grain_state_capture(slot,2);ts_grain_state_recall(slot,2,1);
        snprintf(sister.model.status,sizeof(sister.model.status),"GRAIN STATES / RIGHT EDIT / CLICK BLUE OR AMBER TO SAVE / MIDDLE CLEAR");
        ts_sister_ui_render(&frame,&sister.model,&ui.palette);assert(ts_ui_write_ppm(&frame,getenv("TS_GRAIN_SCREENSHOT")));
    }
    sister.model.parameters=saved;ts_sister_runtime_set_parameters(&audio.sister,&saved);
}
