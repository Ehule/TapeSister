/* Same note/controller paths as the application. Compare actual audio before
   and after changing only the view, then exercise the explicit view mode. */
static void test_canvas_play_view(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    TsSample sample;char error[160];
    ts_instrument_init(&instrument);ts_ui_init(&ui);ts_sample_init(&sample);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_sister_runtime_init(&audio.sister);ts_capture_init(&audio.capture);audio.output_rate=44100;audio.bank_slot=-1;
    sample.frames=4096;sample.channels=2;sample.sample_rate=44100;sample.data=malloc(8192*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i){sample.data[2*i]=.3f*sinf(i*.047f)+.1f*sinf(i*.123f);sample.data[2*i+1]=-sample.data[2*i];}
    assert(ts_instrument_import_sample(&instrument,&sample,0,0,sample.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    uint64_t hash=ts_sample_hash(&instrument.current);int undo=instrument.undo_count;
    ui.show_keyboard=1;ui.config.voice_attack_ms=0;instrument.loop_crossfade_ms=0;
    SDL_Event pointer={0};pointer.type=SDL_MOUSEBUTTONDOWN;pointer.button.button=SDL_BUTTON_LEFT;
    pointer.button.windowID=SDL_GetWindowID(window);pointer.button.x=25;pointer.button.y=368;
    TsMidiEvent midi={0};midi.action=TS_MIDI_ACTION_NOTE_ON;
    assert(ts_note_event_midi(&midi.note,60,127,1));
    SDL_SetModState(KMOD_NONE);
#define VIEW_NOTE(ORIGIN) do {if((ORIGIN)==0)begin_note(device,&audio,&ui,&instrument,0,44100,0);else if((ORIGIN)==1)handle_midi_event(device,&audio,&ui,&instrument,NULL,&midi,44100);else assert(keyboard_pointer_event(&pointer,window,device,&audio,&ui,&instrument,NULL,44100));}while(0)
    for(int hold=0;hold<2;++hold)for(int zoomed=0;zoomed<2;++zoomed)for(int selection=0;selection<2;++selection)for(int origin=0;origin<3;++origin) {
        stop_all_force(device,&audio,&ui);ui.play_view=0;instrument.has_loop=0;
        ts_instrument_show_all(&instrument);instrument.has_selection=selection;
        instrument.selection_first=512;instrument.selection_last=3072;
        if(zoomed)assert(ts_instrument_zoom_view(&instrument,2300,.5f,.25f));
        if(hold)toggle_fm_hold(device,&audio,&ui,&instrument);
        else toggle_workbench_loop(device,&audio,&ui,&instrument,44100,0);
        VIEW_NOTE(origin);const TsNoteVoice *v=ts_note_bank_display_voice(&audio.notes);assert(v && v->looping);
        size_t first=selection?512:0,last=selection?3072:sample.frames;
        assert(v->range_first==first && v->range_last==last);
        for(int i=0;i<157;++i)(void)ts_note_bank_read_stereo(&audio.notes);
        TsNoteBank reference=audio.notes;
        assert(ts_instrument_zoom_view(&instrument,1700,.4f,.5f));
        assert(ts_instrument_pan_view(&instrument,53));
        refresh_workbench_loop(device,&audio,&ui,&instrument);
        assert(v->range_first==first && v->range_last==last && !audio.playing);
        assert(fabs(v->position-ts_note_bank_display_voice(&reference)->position)<1e-9);
        for(int i=0;i<9000;++i) {
            TsStereoFrame a=ts_note_bank_read_stereo(&audio.notes),b=ts_note_bank_read_stereo(&reference);
            assert(fabsf(a.l-b.l)<1e-6f && fabsf(a.r-b.r)<1e-6f && a.r==-a.l);
        }
        assert(ts_note_bank_count(&audio.notes)==1);
    }
    /* Standalone LOOP and locked LOOP also ignore zoom and retain phase. */
    for(int persistent=0;persistent<2;++persistent) {
        stop_all_force(device,&audio,&ui);instrument.has_selection=0;
        toggle_workbench_loop(device,&audio,&ui,&instrument,44100,persistent);
        assert(audio.playing && audio.range_start==0 && audio.range_end==sample.frames);
        audio.position=1023.25;
        ts_instrument_zoom_view(&instrument,1200,.5f,.5f);ts_instrument_pan_view(&instrument,10);
        refresh_workbench_loop(device,&audio,&ui,&instrument);
        assert(audio.position==1023.25 && audio.range_start==0 && audio.range_end==sample.frames);
    }
    /* PLAY VIEW is a range toggle, independent of looping. It never adds a
       transport voice; MIDI and pointer notes follow the same moving range. */
    for(int origin=0;origin<3;++origin) {
        stop_all_force(device,&audio,&ui);ui.play_view=0;instrument.has_selection=0;
        instrument.view_first=1024;instrument.view_last=2048;
        toggle_play_view(device,&audio,&ui,&instrument);assert(ui.play_view && !audio.playing);
        VIEW_NOTE(origin);const TsNoteVoice *v=ts_note_bank_display_voice(&audio.notes);
        assert(v && !v->looping && v->range_first==1024 && v->range_last==2048);
        toggle_fm_hold(device,&audio,&ui,&instrument);assert(v->latched && v->looping);
        TsNoteEvent release;
        if(origin==1)assert(ts_note_event_midi(&release,60,127,1));else assert(ts_note_event_qwerty(&release,0,60));
        ts_note_bank_release_event(&audio.notes,&release);assert(v->active);
        assert(ts_instrument_zoom_view(&instrument,1536,.5f,.5f));
        assert(ts_instrument_pan_view(&instrument,100));refresh_workbench_loop(device,&audio,&ui,&instrument);
        assert(v->range_first==instrument.view_first && v->range_last==instrument.view_last && !audio.playing);
        for(int i=0;i<2000;++i){TsStereoFrame f=ts_note_bank_read_stereo(&audio.notes);assert(isfinite(f.l) && f.r==-f.l);}
        size_t first=instrument.view_first,last=instrument.view_last;
        assert(ts_ui_wave_action_from_point(420,300)==TS_UI_WAVE_ACTION_SELECT_VIEW);
        select_current_view(device,&audio,&ui,&instrument);
        assert(!ui.play_view && instrument.has_selection && instrument.selection_first==first && instrument.selection_last==last);
        TsNoteBank reference=audio.notes;
        ts_instrument_show_all(&instrument);refresh_workbench_loop(device,&audio,&ui,&instrument);
        assert(v->range_first==first && v->range_last==last && v->active);
        for(int i=0;i<2000;++i) {
            TsStereoFrame a=ts_note_bank_read_stereo(&audio.notes),b=ts_note_bank_read_stereo(&reference);
            assert(fabsf(a.l-b.l)<1e-6f && fabsf(a.r-b.r)<1e-6f);
        }
        toggle_fm_hold(device,&audio,&ui,&instrument);assert(!ts_note_bank_count(&audio.notes));
    }
    /* Normal saved loops retain priority over selection and zoom. Explicit
       PLAY VIEW uses the view and the base direction, never an out-of-view attack. */
    for(int mode=0;mode<TS_LOOP_MODE_COUNT;++mode) {
        stop_all_force(device,&audio,&ui);ui.play_view=0;instrument.has_loop=1;
        instrument.loop_first=256;instrument.loop_last=768;instrument.loop_mode=(TsLoopMode)mode;
        instrument.has_selection=1;instrument.selection_first=1000;instrument.selection_last=2000;
        instrument.view_first=2200;instrument.view_last=2800;
        VIEW_NOTE(0);const TsNoteVoice *v=ts_note_bank_display_voice(&audio.notes);
        assert(v->range_first==256 && v->range_last==768);
        toggle_play_view(device,&audio,&ui,&instrument);
        assert(v->range_first==2200 && v->range_last==2800 && !v->loop_intro);
        assert(v->position>=2200 && v->position<2800 && v->loop_mode==ts_loop_base_mode((TsLoopMode)mode));
        for(int i=0;i<1500;++i) {
            TsStereoFrame f=ts_note_bank_read_stereo(&audio.notes);
            /* Position has already advanced; the next read wraps that step. */
            double played=v->position-v->step*v->direction;
            assert(played>=2200 && played<2800 && isfinite(f.l) && f.r==-f.l);
        }
        toggle_play_view(device,&audio,&ui,&instrument);assert(v->range_first==256 && v->range_last==768);
        /* HOLD refresh must not clamp a pending forward/reverse boundary step. */
        toggle_fm_hold(device,&audio,&ui,&instrument);
        TsNoteBank reference=audio.notes;
        for(int i=0;i<1800;++i) {
            ts_instrument_pan_view(&instrument,i%2?1:-1);
            refresh_workbench_loop(device,&audio,&ui,&instrument);
            TsStereoFrame a=ts_note_bank_read_stereo(&audio.notes),b=ts_note_bank_read_stereo(&reference);
            assert(fabsf(a.l-b.l)<1e-6f && fabsf(a.r-b.r)<1e-6f);
        }
    }
    /* PLAY ALL / PLAY SEL deliberately leave view mode and normal LOOP.
       Explicit one-shot view audition (Space) snapshots the visible range. */
    stop_all_force(device,&audio,&ui);ui.play_view=0;instrument.has_loop=0;instrument.loop_mode=TS_LOOP_FORWARD;
    instrument.has_selection=0;instrument.view_first=1100;instrument.view_last=2200;
    toggle_play_view(device,&audio,&ui,&instrument);toggle_workbench_loop(device,&audio,&ui,&instrument,44100,0);
    assert(audio.range_start==1100 && audio.range_end==2200);
    assert(ts_instrument_pan_view(&instrument,50));refresh_workbench_loop(device,&audio,&ui,&instrument);
    assert(audio.range_start==1150 && audio.range_end==2250);
    play_canvas_range(device,&audio,&ui,&instrument,TS_AUDITION_ALL,44100);
    assert(!ui.play_view && !ui.workbench_loop_active && !audio.looping && audio.range_end==sample.frames);
    toggle_play_view(device,&audio,&ui,&instrument);
    begin_audition(device,&audio,&ui,&instrument,TS_AUDITION_DISPLAYED,1,44100);
    assert(!audio.looping && audio.range_start==1150 && audio.range_end==2250);
    ts_instrument_show_all(&instrument);refresh_workbench_loop(device,&audio,&ui,&instrument);
    assert(audio.range_start==1150 && audio.range_end==2250);
    select_current_view(device,&audio,&ui,&instrument);
    play_canvas_range(device,&audio,&ui,&instrument,TS_AUDITION_SELECTION,44100);
    assert(!ui.play_view && !audio.looping && audio.range_start==0 && audio.range_end==sample.frames);
    assert(ts_sample_hash(&instrument.current)==hash && instrument.undo_count==undo);
    /* Native output, including the compact clear confirmation in bank view. */
    if(getenv("TS_TEST_SUSTAIN_SHOTS")) {
        SDL_Window *shot_window=SDL_CreateWindow("Canvas view mode",0,0,1920,1200,SDL_WINDOW_HIDDEN);assert(shot_window);
        SDL_Renderer *renderer=SDL_CreateRenderer(shot_window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
        SDL_Texture *base=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
        SDL_Texture *textures[2]={0};TsUiWaveformDetail details[2]={0};static TsFramebuffer fb;
        instrument.has_selection=0;instrument.view_first=768;instrument.view_last=1536;
        toggle_play_view(device,&audio,&ui,&instrument);
        render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,"canvas-play-view");
        select_current_view(device,&audio,&ui,&instrument);ts_instrument_show_all(&instrument);
        render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,"canvas-select-view");
        ui.show_keyboard=0;ui.bank_clear_armed=1;
        render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,"canvas-view-toolbar");
        for(int i=0;i<2;++i){SDL_DestroyTexture(textures[i]);ts_ui_waveform_detail_free(&details[i]);}
        SDL_DestroyTexture(base);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(shot_window);
    }
    /* Source view uses its own coordinates; SEL VIEW intersects the crop and
       leaves an unrelated existing selection intact when no overlap exists. */
    stop_all_force(device,&audio,&ui);ui.play_view=0;
    ts_instrument_set_selection(&instrument,1000,3000);
    assert(ts_instrument_crop_selection(&instrument,error,sizeof(error)));
    ui.audition_source=TS_AUDITION_PARENT;ui.parent_view_first=1200;ui.parent_view_last=1800;
    toggle_play_view(device,&audio,&ui,&instrument);VIEW_NOTE(0);
    const TsNoteVoice *v=ts_note_bank_display_voice(&audio.notes);
    assert(v->source==TS_AUDITION_PARENT && v->range_first==1200 && v->range_last==1800);
    select_current_view(device,&audio,&ui,&instrument);
    assert(!ui.play_view && instrument.selection_first==200 && instrument.selection_last==800 && ui.audition_source==TS_AUDITION_PARENT);
    ui.parent_view_first=0;ui.parent_view_last=500;select_current_view(device,&audio,&ui,&instrument);
    assert(instrument.selection_first==200 && instrument.selection_last==800 && strstr(ui.status,"OUTSIDE"));
    ui.parent_view_first=800;ui.parent_view_last=3200;select_current_view(device,&audio,&ui,&instrument);
    assert(instrument.selection_first==0 && instrument.selection_last==instrument.current.frames);
    TsAuditionPlan plan;assert(ts_audition_plan(&instrument,TS_AUDITION_PARENT,TS_AUDITION_WORKBENCH_LOOP,&plan));
    assert(plan.first==1000 && plan.last==3000);
    stop_all_force(device,&audio,&ui);ts_sample_free(&sample);ts_instrument_free(&instrument);
    ts_capture_free(&audio.capture);ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_sister_runtime_free(&audio.sister);
#undef VIEW_NOTE
    puts("Canvas playback: zoom independence, explicit PLAY VIEW, SEL VIEW, MIDI/pointer/QWERTY and loop directions passed");
}
