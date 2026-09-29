/* Exercise the real SDL handlers and voice ownership, not just key lookup. */
static SDL_Event extended_pointer(Uint32 type, int note)
{
    SDL_Event e; SDL_zero(e); e.type = type;
    int x = 5, y = 300, found = note < 0;
    for (int yy = 368; yy >= 330 && !found; --yy)
    for (int xx = 10; xx < 622 && !found; ++xx)
        if (ts_ui_key_from_point_for_base(xx, yy, ts_ui_keyboard_base_note(&ui)) == note) {
            x = xx; y = yy; found = 1;
        }
    assert(found);
    if (type == SDL_MOUSEMOTION) {
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.state = SDL_BUTTON_LMASK; e.motion.x = x; e.motion.y = y;
    } else {
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
    }
    return e;
}

static void extended_pointer_send(SDL_Event e)
{
    (void)keyboard_pointer_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100);
    tick();
}

static SDL_Event extended_key(SDL_Keycode key)
{
    SDL_Event e; SDL_zero(e); e.type = SDL_KEYDOWN;
    e.key.windowID = SDL_GetWindowID(window); e.key.keysym.sym = key;
    return e;
}

static void test_extended_keyboard(void)
{
    const SDL_Keycode lower[] = {SDLK_z,SDLK_s,SDLK_x,SDLK_d,SDLK_c,SDLK_v,SDLK_g,
        SDLK_b,SDLK_h,SDLK_n,SDLK_j,SDLK_m,SDLK_COMMA,SDLK_l,SDLK_PERIOD,SDLK_SEMICOLON,SDLK_SLASH};
    const SDL_Keycode upper[] = {SDLK_q,SDLK_2,SDLK_w,SDLK_3,SDLK_e,SDLK_r,SDLK_5,
        SDLK_t,SDLK_6,SDLK_y,SDLK_7,SDLK_u,SDLK_i,SDLK_9,SDLK_o,SDLK_0,SDLK_p,
        SDLK_LEFTBRACKET,SDLK_EQUALS,SDLK_RIGHTBRACKET,SDLK_RETURN};
    for (int i = 0; i < 17; ++i) {
        TsNoteEvent e;
        assert(ts_note_event_qwerty(&e, note_for_key(lower[i]), 60));
        assert(e.midi_note == 60 + i);
    }
    for (int i = 0; i < 21; ++i) {
        TsNoteEvent e;
        assert(ts_note_event_qwerty(&e, note_for_key(upper[i]), 60));
        assert(e.midi_note == 72 + i);
    }
    TsNoteEvent invalid;
    assert(!ts_note_event_qwerty(&invalid, 38, 60));
    assert(!ts_note_event_qwerty(&invalid, TS_KEYBOARD_POINTER_FIRST + 33, 60));
    assert(!ts_note_event_qwerty(&invalid, 32, 96));
    assert(!ts_note_event_qwerty(&invalid, -1, 60));
    assert(!ts_note_event_qwerty(&invalid, 32, INT_MAX));
    for (int route = 0; route < 4; ++route) {
        reset_route(route); set_keyboard_octave(device, &audio, &ui, 4);
        SDL_SetModState(KMOD_NONE);
        ts_note_bank_set_sustain(&audio.notes, 0);
        ts_performance_set_sustain(&audio.performance, 0);
        ts_performance_set_sustain(&audio.sister.performance, 0);
        ui.keyboard_sustain = 0;
        SDL_Event key = extended_key(SDLK_q);
        assert(keyboard_note_key_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
        key = extended_key(SDLK_COMMA);
        assert(keyboard_note_key_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(voices() == 2); /* Same pitch, two physical owners. */
        extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONDOWN, 12));
        assert(ui.keyboard_pointer_drag && voices() == 3);
        for (int i = 0; i < TS_KEYBOARD_NOTE_COUNT; ++i) {
            extended_pointer_send(extended_pointer(SDL_MOUSEMOTION, i));
            assert(voices() == 3 && ui.mouse_note == TS_KEYBOARD_POINTER_FIRST + i);
            extended_pointer_send(extended_pointer(SDL_MOUSEMOTION, i));
            assert(voices() == 3); /* Same key is not another note-on. */
        }
        TsKeyboardMask lit = ts_note_bank_visible_mask(&audio.notes, 60) |
            ts_performance_visible_mask(&audio.performance, 60) |
            ts_performance_visible_mask(&audio.sister.performance, 60);
        assert(lit == ((UINT64_C(1) << 12) | (UINT64_C(1) << 32)));
        extended_pointer_send(extended_pointer(SDL_MOUSEMOTION, -1));
        assert(voices() == 2 && ui.mouse_note == -1 && ui.keyboard_pointer_drag);
        extended_pointer_send(extended_pointer(SDL_MOUSEMOTION, 0));
        assert(voices() == 3);
        extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONUP, -1));
        assert(voices() == 2 && !ui.keyboard_pointer_drag);
        key.type = SDL_KEYUP; extended_pointer_send(key);
        assert(voices() == 1); /* Releasing comma leaves Q held. */
        key = extended_key(SDLK_q); key.type = SDL_KEYUP; extended_pointer_send(key);
        assert(!voices());
        for (int cancel = 0; cancel < 4; ++cancel) {
            extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONDOWN, 32));
            assert(voices() == 1);
            SDL_Event e = extended_pointer(SDL_MOUSEMOTION, 31);
            if (cancel == 0) e.motion.state = 0;
            if (cancel == 1) { e.type = SDL_WINDOWEVENT; e.window.windowID = SDL_GetWindowID(window); e.window.event = SDL_WINDOWEVENT_FOCUS_LOST; }
            if (cancel < 2) extended_pointer_send(e);
            else if (cancel == 2) shift_keyboard_range(device, &audio, &ui, 1);
            else stop_all_force(device, &audio, &ui);
            assert(!voices() && !ui.keyboard_pointer_drag && ui.mouse_note == -1);
            set_keyboard_octave(device, &audio, &ui, 4);
        }
        /* High note releases still work after the octave puts it outside MIDI. */
        key = extended_key(SDLK_RETURN);
        assert(keyboard_note_key_event(&key, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(voices() == 1);
        set_keyboard_octave(device, &audio, &ui, 7);
        key.type = SDL_KEYUP; extended_pointer_send(key);
        assert(!voices());
    }
    reset_route(0); set_keyboard_octave(device, &audio, &ui, 4);
    SDL_Event key = extended_key(SDLK_RETURN);
    ui.config_open = 1;
    assert(!keyboard_note_key_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    ui.config_open = 0; ui.fm_open = 1; ui.fm_bank_choice_open = 1;
    assert(!keyboard_note_key_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    ui.fm_bank_choice_open = 0;
    key.key.keysym.mod = KMOD_ALT;
    assert(!keyboard_note_key_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    key = extended_key(SDLK_a);
    assert(!keyboard_note_key_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    ui.fm_open = 0; ui.show_keyboard = 0; key = extended_key(SDLK_0);
    assert(!keyboard_note_key_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    ui.show_keyboard = 1;
    /* ARP editing resolves lower-row aliases to pitch, with no drag or audition. */
    ui.keyboard_sequence_open = ui.keyboard_sequence_edit = 1;
    key = extended_key(SDLK_COMMA);
    assert(keyboard_sequence_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    assert(audio.keyboard_sequence.settings.count == 1 && audio.keyboard_sequence.settings.notes[0] == 72);
    key = extended_key(SDLK_q);
    assert(keyboard_sequence_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    assert(!audio.keyboard_sequence.settings.count && !voices());
    SDL_Event click = extended_pointer(SDL_MOUSEBUTTONDOWN, 32);
    assert(keyboard_sequence_event(&click,window,device,&audio,&ui,&instrument,&fm,44100));
    assert(audio.keyboard_sequence.settings.notes[0] == 92 && !ui.keyboard_pointer_drag);
    set_keyboard_octave(device, &audio, &ui, 7);
    key = extended_key(SDLK_RETURN);
    assert(keyboard_sequence_event(&key,window,device,&audio,&ui,&instrument,&fm,44100));
    assert(audio.keyboard_sequence.settings.count == 1);
    set_keyboard_octave(device, &audio, &ui, 4);
    ui.keyboard_sequence_open = ui.keyboard_sequence_edit = 0;
    ts_keyboard_sequence_init(&audio.keyboard_sequence);
    /* Staging can address bit 32 without wrapping onto C4. */
    audio.capture.state = TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    SDL_SetModState(KMOD_SHIFT);
    extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONDOWN, 32));
    assert(audio.capture.staged_notes == (UINT64_C(1) << 32) && !ui.keyboard_pointer_drag);
    assert(ts_capture_shift_staged_notes(&audio.capture, 1) == (UINT64_C(1) << 31));
    assert(ts_capture_shift_staged_notes(&audio.capture, -1) == (UINT64_C(1) << 32));
    assert(ts_capture_shift_staged_notes(&audio.capture, -1) == 0);
    audio.capture.state = TS_CAPTURE_IDLE;
    assert(ts_note_bank_start_staged_chord(&audio.notes, &instrument, &instrument.tuning,
        TS_AUDITION_CURRENT, UINT64_C(1) << 32, 60, 44100) == 1);
    assert(ts_note_bank_visible_mask(&audio.notes, 60) == (UINT64_C(1) << 32));
    ts_note_bank_clear(&audio.notes);
    assert(ts_performance_trigger_staged(&audio.performance, &instrument, 1u,
        UINT64_C(1) << 32, 60, 44100) == 1);
    assert(ts_performance_visible_mask(&audio.performance, 60) == (UINT64_C(1) << 32));
    ts_performance_clear(&audio.performance);
    SDL_SetModState(KMOD_NONE);
    toggle_fm_hold(device,&audio,&ui,&instrument);
    extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONDOWN, 0));
    assert(!ui.keyboard_pointer_drag && voices() == 1);
    extended_pointer_send(extended_pointer(SDL_MOUSEMOTION, 7));
    assert(voices() == 1); /* HOLD clicks still build deliberate chords. */
    extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONUP, -1));
    toggle_fm_hold(device,&audio,&ui,&instrument);
    assert(!voices());
    SDL_SetModState(KMOD_SHIFT);
    extended_pointer_send(extended_pointer(SDL_MOUSEBUTTONDOWN, 4));
    assert(!ui.keyboard_pointer_drag && voices() == 1);
    extended_pointer_send(extended_pointer(SDL_MOUSEMOTION, 7));
    assert(voices() == 1);
    stop_all_force(device,&audio,&ui);
    SDL_SetModState(KMOD_NONE);
}
