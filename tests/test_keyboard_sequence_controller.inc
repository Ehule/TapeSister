static void sequence_click(int x, int y, SDL_Keymod modifiers)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEBUTTONDOWN; event.button.button = SDL_BUTTON_LEFT;
    event.button.windowID = SDL_GetWindowID(window);
    event.button.x = x; event.button.y = y;
    SDL_SetModState(modifiers);
    assert(keyboard_sequence_event(&event, window, device, &audio, &ui, &instrument, &fm, 44100));
    event.type = SDL_MOUSEBUTTONUP;
    (void)keyboard_sequence_event(&event, window, device, &audio, &ui, &instrument, &fm, 44100);
    SDL_SetModState(KMOD_NONE);
}

static void sequence_select(int note)
{
    int found = 0;
    for (int y = 368; y >= 330 && !found; --y)
    for (int x = 10; x < 622 && !found; ++x)
        if (ts_ui_key_from_point_for_base(x, y, ts_ui_keyboard_base_note(&ui)) == note) {
            sequence_click(x, y, KMOD_CTRL); found = 1;
        }
    assert(found);
}

static void test_keyboard_sequence_controller(void)
{
    for (int route = 0; route < 4; ++route) {
        if (route < 3) ts_sister_runtime_disable(&audio.sister);
        else {
            char error[160];
            assert(ts_sister_runtime_enable(&audio.sister, 44100, 2, 2, 1, error, sizeof(error)));
        }
        reset_route(route);
        set_keyboard_octave(device, &audio, &ui, 4);
        ui.keyboard_sequence_open = 1; ui.keyboard_sequence_edit = 0;
        TsKeyboardSequenceSettings settings = audio.keyboard_sequence.settings;
        settings.count = 0; settings.seconds = .04; settings.gate = .8;
        ts_keyboard_sequence_set(&audio.keyboard_sequence, &settings);
        toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(0, 0); click_note(4, 1);
        assert(voices() == 2);
        sequence_click(340, 260, KMOD_NONE); /* Copy the manual chord without stealing it. */
        assert(audio.keyboard_sequence.settings.count == 2 && voices() == 2);
        sequence_select(7); sequence_select(10);
        assert(audio.keyboard_sequence.settings.count == 4 && voices() == 2);
        sequence_click(36, 260, KMOD_NONE);
        assert(audio.keyboard_sequence.running && voices() == 2);
        assert(audio.keyboard_sequence.source->fm == (route == 1));
        assert((audio.keyboard_sequence.source->group != NULL) == (route >= 2));
        sequence_click(36, 260, KMOD_NONE);
        assert(!audio.keyboard_sequence.running && voices() == 2);
        assert(power_audio_energy() > .01); /* Manual HOLD survives ARP Stop. */
        sequence_click(36, 260, KMOD_NONE);
        assert(audio.keyboard_sequence.running && voices() == 2);
        click_note(0, 0); click_note(4, 0); assert(!voices());
        double energy = power_audio_energy();
        if (energy <= .01) fprintf(stderr, "Silent sequence route=%d\n", route);
        assert(energy > .01); /* Sequence reaches the real master callback. */
        double dry = 0;
        for (int i = 0; i < 3000; ++i) {
            TsAudioBuses buses = {0}; TsStereoFrame synth = {0};
            runtime_note_render(&audio, &buses, &synth);
            dry += audio.keyboard_dry.l * audio.keyboard_dry.l + audio.keyboard_dry.r * audio.keyboard_dry.r;
            if (route == 1) assert(buses.tile_performance.l == 0 && buses.fm.l == synth.l);
        }
        assert(dry > .01);
        /* Controls and range changes do not alter its selected absolute pitches. */
        set_keyboard_octave(device, &audio, &ui, 3);
        sequence_select(12); /* Remove C4 at its new position. */
        assert(audio.keyboard_sequence.settings.count == 3);
        set_keyboard_octave(device, &audio, &ui, 4);
        TsMidiEvent midi = {0}; midi.action = TS_MIDI_ACTION_NOTE_ON;
        assert(ts_note_event_midi(&midi.note, 64, 100, 2));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 1);
        sequence_click(36, 260, KMOD_NONE); /* ARP Stop leaves MIDI alone. */
        assert(!audio.keyboard_sequence.running && voices() == 1);
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(!voices()); assert(power_audio_energy() < .0001);
        sequence_click(36, 260, KMOD_NONE); assert(audio.keyboard_sequence.running);
        stop_all_force(device, &audio, &ui); assert(!audio.keyboard_sequence.running);
        assert(power_audio_energy() < .0001);
    }
    /* The shared LOOP cannot create a root drone while ARP owns its playback. */
    ts_sister_runtime_disable(&audio.sister);
    reset_route(0); ui.keyboard_sequence_open = 1;
    toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, 1);
    sequence_click(36, 260, KMOD_NONE); assert(audio.keyboard_sequence.running && !audio.playing);
    refresh_workbench_loop(device, &audio, &ui, &instrument); assert(!audio.playing);
    sequence_click(36, 260, KMOD_NONE);
    refresh_workbench_loop(device, &audio, &ui, &instrument); assert(audio.playing); /* Explicit Loop Lock resumes. */
    stop_all_force(device, &audio, &ui);
    /* A refreshed source is swapped safely; clearing it stops further triggers. */
    reset_route(0); ui.keyboard_sequence_open = 1;
    toggle_fm_hold(device, &audio, &ui, &instrument);
    sequence_click(36, 260, KMOD_NONE); assert(audio.keyboard_sequence.running);
    TsKeyboardSequenceSource *before = audio.keyboard_sequence.source;
    ++instrument.current.visual_revision;
    assert(keyboard_sequence_prepare(device, &audio, &ui, &instrument, &fm, 44100, 0));
    assert(audio.keyboard_sequence.source != before && audio.keyboard_sequence.running);
    ui.fm_open = 1;
    assert(keyboard_sequence_prepare(device, &audio, &ui, &instrument, &fm, 44100, 0));
    assert(audio.keyboard_sequence.source->fm && audio.keyboard_sequence.running);
    assert(!keyboard_sequence_prepare(device, &audio, &ui, &instrument, NULL, 44100, 0));
    assert(!audio.keyboard_sequence.running);
    ui.fm_open = 0;
    /* QWERTY edit consumes repeats without creating manual voices; dialogs and
       capture staging retain their original event ownership. */
    ui.keyboard_sequence_edit = 1;
    sequence_click(280, 260, KMOD_NONE);
    SDL_Event key = {0};
    key.type = SDL_KEYDOWN; key.key.windowID = SDL_GetWindowID(window);
    key.key.keysym.sym = SDLK_z;
    assert(keyboard_sequence_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
    assert(audio.keyboard_sequence.settings.count == 1 && !voices());
    key.key.repeat = 1;
    assert(keyboard_sequence_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
    assert(audio.keyboard_sequence.settings.count == 1 && !voices());
    key.key.repeat = 0;
    ui.config_open = 1;
    assert(!keyboard_sequence_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
    ui.config_open = 0;
    audio.capture.state = TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    assert(!keyboard_sequence_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
    audio.capture.state = TS_CAPTURE_IDLE;
    ui.keyboard_sequence_edit = 0;
    assert(!keyboard_sequence_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
    /* ARP Stop also leaves click-launched tile layers alone. */
    assert(ts_performance_toggle_tile(&audio.tile_launchers, &instrument, 0, 0, 44100) == TS_PERFORMANCE_TILE_STARTED);
    audio.tile_launcher_active = 1;
    sequence_click(36, 260, KMOD_NONE);
    assert(audio.keyboard_sequence.running);
    sequence_click(36, 260, KMOD_NONE);
    assert(!audio.keyboard_sequence.running && ts_performance_count(&audio.tile_launchers) == 1);
    assert(power_audio_energy() > .01);
    stop_all_force(device, &audio, &ui);
    /* Native UI capture with ordered white and black keys and a running step. */
    sequence_click(280, 260, KMOD_NONE);
    sequence_select(0); sequence_select(7); sequence_select(3); sequence_select(10);
    audio.keyboard_sequence.settings.mode = TS_KEYBOARD_SEQUENCE_ORDER;
    audio.keyboard_sequence.settings.seconds = .25;
    ts_keyboard_sequence_set(&audio.keyboard_sequence, &audio.keyboard_sequence.settings);
    sequence_click(36, 260, KMOD_NONE); assert(power_audio_energy() > .01);
    keyboard_sequence_ui(&audio, &ui); ui.keyboard_sequence_edit = 1;
    const char *shot = getenv("TS_TEST_KEYBOARD_SEQUENCE_PPM");
    if (shot) {
        static TsFramebuffer fb;
        ts_ui_render(&fb, &ui, &instrument); assert(ts_ui_write_ppm(&fb, shot));
    }
    stop_all_force(device, &audio, &ui);
    ts_keyboard_sequence_source_free(ts_keyboard_sequence_source(&audio.keyboard_sequence, NULL));
    ui.keyboard_sequence_open = 0; ui.keyboard_sequence_edit = 0;
}
