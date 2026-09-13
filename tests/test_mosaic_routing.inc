static void sister_key(SDL_Window *window,SDL_Keycode sym,SDL_Keymod mod)
{
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=sister.window_id;e.key.keysym.sym=sym;e.key.keysym.mod=mod;
    dispatch(&e,window);
}
static void test_workspace_playback(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=ui.fm_open=0;
    ts_note_bank_clear(&audio.notes);ts_note_bank_set_sustain(&audio.notes,0);
    TsNoteEvent sample_note,fm_note;
    assert(ts_note_event_qwerty(&sample_note,0,ts_ui_keyboard_base_note(&ui)));
    assert(ts_note_event_qwerty(&fm_note,2,ts_ui_keyboard_base_note(&ui)));
    assert(ts_note_bank_start_tuned_event(&audio.notes,&instrument,&instrument.tuning,
        TS_AUDITION_CURRENT,&sample_note,0,48000)==TS_NOTE_STARTED);
    TsNoteBank held=audio.notes;
    key(window,SDLK_BACKQUOTE,KMOD_NONE);
    assert(ui.mosaic_open && !scene->playing && !memcmp(&held,&audio.notes,sizeof(held)));
    key(window,SDLK_BACKQUOTE,KMOD_NONE);
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);
    assert(ui.fm_open && !memcmp(&held,&audio.notes,sizeof(held)));
    assert(ts_note_bank_start_sample_event(&audio.notes,&workspace_preview,&instrument.tuning,
        &fm_note,0,48000)==TS_NOTE_STARTED);
    held=audio.notes;
    const float *preview=workspace_preview.data;
    key(window,SDLK_BACKQUOTE,KMOD_NONE);
    assert(ui.mosaic_open && !ui.fm_open && !scene->playing);
    assert(!memcmp(&held,&audio.notes,sizeof(held)) && workspace_preview.data==preview);
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *event=ts_mosaic_add(scene,source,0,0);assert(event);event->duration=6;
    key(window,SDLK_SPACE,KMOD_NONE);assert(scene->playing && ts_note_bank_count(&audio.notes)==2);
    double tile_energy=0,fm_energy=0;
    for(int n=0;n<4096;++n) {
        TsAudioBuses buses;TsStereoFrame capture;ts_audio_buses_clear(&buses);
        runtime_note_render(&audio,&buses,&capture);
        tile_energy+=fabsf(buses.tile_performance.l);fm_energy+=fabsf(buses.fm.l);
    }
    assert(scene->time>0 && tile_energy>1 && fm_energy>1);
    key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(!scene->playing && ts_note_bank_count(&audio.notes)==2);
    /* A key released over the arrangement must reach its original sample/FM voice. */
    SDL_Event up={0};up.type=SDL_KEYUP;up.key.windowID=SDL_GetWindowID(window);up.key.keysym.sym=SDLK_z;
    assert(dispatch(&up,window) && ts_note_bank_count(&audio.notes)==1);
    up.key.keysym.sym=SDLK_x;
    assert(dispatch(&up,window) && ts_note_bank_count(&audio.notes)==0);
    /* Latches survive a physical release, and parking/reopening keeps their preview alive. */
    assert(ts_note_bank_start_sample_event(&audio.notes,&workspace_preview,&instrument.tuning,
        &fm_note,1,48000)==TS_NOTE_STARTED);
    assert(dispatch(&up,window) && ts_note_bank_latched_synth_count(&audio.notes)==1);
    held=audio.notes;
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);key(window,SDLK_BACKQUOTE,KMOD_SHIFT);
    assert(!ui.fm_open && !ui.mosaic_open && workspace_preview.data==preview);
    assert(!memcmp(&held,&audio.notes,sizeof(held)));
    close_fm_workspace(0,&audio,&ui,&workspace_preview);mosaic.fm_parked=0;
    ts_mosaic_free(scene);ui.mosaic=audio.mosaic=saved;
}
static void test_workspace_routes(SDL_Window *window)
{
    ui.mosaic_open=0;ui.fm_open=0;
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.fm_open && !ui.mosaic_open);
    ui.fm_patch.ratios[0]=1.75f;ts_fm_toggle_unison(&ui.fm_patch);
    TsFmPatch patch=ui.fm_patch;const float *preview=workspace_preview.data;
    key(window,SDLK_p,KMOD_CTRL|KMOD_SHIFT);assert(ui.portal.open && !ui.fm_open);
    key(window,SDLK_p,KMOD_CTRL|KMOD_SHIFT);assert(!ui.portal.open && ui.fm_open);
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(ui.mosaic_open && !ui.fm_open);
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(!ui.mosaic_open && !ui.fm_open);
    assert(workspace_preview.data==preview && !memcmp(&patch,&ui.fm_patch,sizeof(patch)));
    key(window,SDLK_BACKQUOTE,KMOD_NONE);
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.fm_open && !ui.mosaic_open);
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(!ui.fm_open && !ui.mosaic_open);
    sister.window=SDL_CreateWindow("Sister transitions",0,0,640,400,SDL_WINDOW_HIDDEN);assert(sister.window);
    sister.window_id=SDL_GetWindowID(sister.window);
    for(int underlying=0;underlying<3;++underlying) {
        ui.mosaic_open=underlying==1;ui.fm_open=underlying==2;
        sister_window_show(&sister,&ui.palette);sister_key(window,SDLK_BACKQUOTE,KMOD_NONE);
        assert(ui.mosaic_open && !ui.fm_open && !sister.model.visible);
        sister_window_show(&sister,&ui.palette);sister_key(window,SDLK_BACKQUOTE,KMOD_SHIFT);
        assert(ui.fm_open && !ui.mosaic_open && !sister.model.visible);
        sister_window_show(&sister,&ui.palette);sister_key(window,SDLK_BACKQUOTE,KMOD_SHIFT);
        assert(ui.fm_open && !sister.model.visible); /* Focus FM, don't close hidden FM. */
    }
    sister_window_fullscreen(&sister,1);
    assert(sister.screen_filling && !(SDL_GetWindowFlags(sister.window)&SDL_WINDOW_FULLSCREEN));
    SDL_Rect display; assert(SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(sister.window),&display)==0);
    int width,height;SDL_GetWindowSize(sister.window,&width,&height);
    assert(width==display.w && height==display.h);
    application_window_focus(window,&sister);
    assert(SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP)==0);
    for(int n=0;n<3;++n) {
        key(window,SDLK_TAB,KMOD_NONE);
        assert(sister.model.visible && !(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED));
        SDL_Event repeat={0};repeat.type=SDL_KEYDOWN;repeat.key.windowID=sister.window_id;
        repeat.key.keysym.sym=SDLK_TAB;repeat.key.repeat=1;
        assert(workspace_tab_event(&repeat,window,&sister,&ui) && sister.model.visible);
        sister_key(window,SDLK_TAB,KMOD_NONE);
        assert(!sister.model.visible && (SDL_GetWindowFlags(window)&SDL_WINDOW_SHOWN));
    }
    SDL_SetWindowFullscreen(window,0);
    sister_window_fullscreen(&sister,0);
    assert(!sister.screen_filling && !(SDL_GetWindowFlags(sister.window)&SDL_WINDOW_BORDERLESS));
    SDL_HideWindow(window);sister_window_show(&sister,&ui.palette);
    sister_key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(!sister.model.visible && (SDL_GetWindowFlags(window)&SDL_WINDOW_SHOWN));
    sister_window_show(&sister,&ui.palette);sister.model.fallout_lfo_open=1;
    sister_key(window,SDLK_ESCAPE,KMOD_NONE);assert(sister.model.visible && !sister.model.fallout_lfo_open);
    sister_key(window,SDLK_ESCAPE,KMOD_NONE);assert(!sister.model.visible);
    close_fm_workspace(0,&audio,&ui,&workspace_preview);
    mosaic.fm_parked=0;
    ui.mosaic_open=1;
    uint64_t owner=ui.mosaic->events[0].id;
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,owner));
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(ui.mosaic_open && !mosaic.active);
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,owner));
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.fm_open && mosaic.active==owner);
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(ui.mosaic_open && !mosaic.active);
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(!ui.fm_open && !ui.mosaic_open);
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.fm_open && mosaic.active==owner);
    for(size_t i=0;i<instrument.current.frames*instrument.current.channels;++i)instrument.current.data[i]*=.9f;
    ++instrument.current.visual_revision;
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(ui.mosaic_edit_choice && !ui.fm_open);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_edit_choice && ui.fm_open && mosaic.active==owner);
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(ui.mosaic_edit_choice);
    key(window,SDLK_u,KMOD_NONE);assert(!ui.mosaic_edit_choice && ui.mosaic_open && !mosaic.active && !ui.fm_open);
    key(window,SDLK_BACKQUOTE,KMOD_NONE);assert(!ui.fm_open && !ui.mosaic_open);
    key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.fm_open && mosaic.active==owner);
    close_fm_workspace(0,&audio,&ui,&workspace_preview);mosaic_leave(0,&audio,&ui,&instrument,&mosaic);
    SDL_DestroyWindow(sister.window);sister.window=NULL;sister.window_id=0;
    mosaic.fm_parked=0;
}

static AudioState *routing_audio(int powered)
{
    AudioState *a=calloc(1,sizeof(*a));assert(a);
    ts_note_bank_init(&a->notes);ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_capture_init(&a->capture);ts_sister_runtime_init(&a->sister);ts_audio_mixer_init(&a->mixer);
    a->output_rate=48000;a->bank_slot=-1;a->mosaic=ts_mosaic_create();assert(a->mosaic);
    char error[160];assert(ts_sister_runtime_reconfigure(&a->sister,48000,2,error,sizeof(error)));
    TsMosaicSource *s=ts_mosaic_source(a->mosaic,&instrument.current,error,sizeof(error));assert(s);
    TsMosaicEvent *e=ts_mosaic_add(a->mosaic,s,0,0);assert(e);e->duration=60;
    ts_mosaic_seek(a->mosaic,.1);a->mosaic->playing=1;
    if(powered) {
        assert(ts_sister_runtime_enable(&a->sister,48000,2,2,.1,error,sizeof(error)));
        ts_sister_runtime_set_sources(&a->sister,TS_SISTER_SOURCE_TILES);
        ts_sister_runtime_set_monitor(&a->sister,1);
    }
    TsSisterParameters p=a->sister.parameters;
    p.head1_time_ms=1;p.head1_level=1;p.head2_level=p.head3_level=0;
    p.head1_feedback=p.head2_feedback=0;p.headroom=1;p.monitor_dry=0;p.monitor_wet=1;
    p.fx.enabled=0;p.fx.master_transition=ts_sister_fx_transition_normalized(10);
    p.fx.fallout.enabled=0;
    ts_sister_runtime_set_parameters(&a->sister,&p);
    return a;
}
static void routing_free(AudioState *a)
{
    ts_mosaic_free(a->mosaic);ts_sister_runtime_free(&a->sister);ts_capture_free(&a->capture);
    ts_performance_free(&a->performance);ts_performance_free(&a->tile_launchers);free(a);
}
static void test_mosaic_master_routes(void)
{
    for(int powered=0;powered<2;++powered) {
        AudioState *wet=routing_audio(powered),*dry=routing_audio(powered);
        SisterWindow *panel=calloc(1,sizeof(*panel));assert(panel);
        panel->model.parameters=wet->sister.parameters;
        TsSisterParameters *p=&panel->model.parameters;
        p->fx.enabled=1;p->fx.slot[0].mix=1;p->fx.slot[0].parameter_a=.9f;
        p->fx.slot[0].placement=TS_SISTER_FX_PLACE_POST;
        p->fx.fallout.enabled=1;p->fx.fallout.mix=1;p->fx.fallout.noise=.8f;p->fx.fallout.feedback=.3f;
        p->fx.fallout.master_transition=ts_sister_fallout_transition_normalized(10);
        ts_sister_runtime_set_parameters(&wet->sister,p);
        float w[512],d[512];double difference=0;
        for(int block=0;block<24;++block) {
            audio_callback(wet,(Uint8 *)w,sizeof(w));audio_callback(dry,(Uint8 *)d,sizeof(d));
            for(int i=0;i<512;++i)difference+=fabsf(w[i]-d[i]);
        }
        assert(difference>1);
        TsSisterUiHit hit={.action=TS_SISTER_UI_ACTION_FX_TOGGLE,.index=TS_SISTER_UI_FX_MASTER};
        sister_apply_action(0,wet,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
        assert(!wet->sister.parameters.fx.enabled && wet->sister.parameters.fx.fallout.enabled);
        /* Give the configured 10 ms bypass transition time to complete. */
        for(int block=0;block<24;++block) {
            audio_callback(wet,(Uint8 *)w,sizeof(w));audio_callback(dry,(Uint8 *)d,sizeof(d));
        }
        assert(ts_sister_post_fx_master_engage(&wet->sister.post_fx)==0);
        /* Ordinary input must be exactly dry. Sister's recorded FX feedback
           needs a buffer washout, but the insert itself must stop contributing. */
        for(int block=0;block<200;++block) {
            audio_callback(wet,(Uint8 *)w,sizeof(w));audio_callback(dry,(Uint8 *)d,sizeof(d));
        }
        for(int i=0;i<512;++i)assert(fabsf(w[i]-d[i])<.00001f);
        assert(wet->sister.fallout_feedback_previous.l==0 && wet->sister.fallout_feedback_previous.r==0);
        hit.action=TS_SISTER_UI_ACTION_FX_SLOT_TOGGLE;hit.index=0;
        sister_apply_action(0,wet,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
        for(int block=0;block<16;++block) {
            audio_callback(wet,(Uint8 *)w,sizeof(w));audio_callback(dry,(Uint8 *)d,sizeof(d));
        }
        for(int i=0;i<512;++i)assert(fabsf(w[i]-d[i])<.00001f);
        hit.action=TS_SISTER_UI_ACTION_FX_TOGGLE;hit.index=TS_SISTER_UI_FX_MASTER;
        sister_apply_action(0,wet,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
        for(int block=0;block<16;++block)audio_callback(wet,(Uint8 *)w,sizeof(w));
        assert(ts_sister_post_fx_master_engage(&wet->sister.post_fx)==1 && wet->sister.parameters.fx.fallout.enabled);
        sister_apply_action(0,wet,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
        /* All sources stopped, master off: no noise or hidden effect return. */
        wet->mosaic->playing=0;ts_sister_runtime_set_monitor(&wet->sister,0);
        for(int block=0;block<16;++block)audio_callback(wet,(Uint8 *)w,sizeof(w));
        for(int i=0;i<512;++i)assert(w[i]==0);
        routing_free(wet);routing_free(dry);free(panel);
    }
}
