static void sequence_click(int x, int y, SDL_Keymod modifiers)
{
    SDL_Event event;SDL_zero(event);
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

static void test_keyboard_sequence_transport(void)
{
    static SisterWindow sister;
    ts_sister_ui_model_init(&sister.model, &ui.config);
    sister.window = SDL_CreateWindow("ARP transport", 0, 0, 640, 400, 0);
    assert(sister.window);
    sister.window_id = SDL_GetWindowID(sister.window);
    reset_route(0);
    ui.keyboard_sequence_open = ui.keyboard_sequence_edit = 0;
    TsKeyboardSequenceSettings settings = audio.keyboard_sequence.settings;
    settings.count = 3; settings.notes[0] = 60; settings.notes[1] = 64; settings.notes[2] = 67;
    settings.seconds = .04; settings.loop = 1;
    ts_keyboard_sequence_set(&audio.keyboard_sequence, &settings);
    SDL_Event key;SDL_zero(key); key.type = SDL_KEYDOWN; key.key.keysym.sym = SDLK_SPACE;
    key.key.windowID = SDL_GetWindowID(window); key.key.keysym.mod = KMOD_SHIFT;
#define TRANSPORT() keyboard_sequence_transport_event(&key, window, device, &audio, &ui, \
                                                      &sister, &instrument, &fm, 44100)
    /* It works with the controls hidden and from every performance workspace. */
    ui.show_keyboard = 0;
    for (int surface = 0; surface < 5; ++surface) {
        ui.master_eq_open = surface == 1; ui.mosaic_open = surface == 2;
        ui.fm_open = surface == 3;
        key.key.windowID = surface == 4 ? sister.window_id : SDL_GetWindowID(window);
        assert(TRANSPORT() && audio.keyboard_sequence.running);
        assert(audio.keyboard_sequence.source->fm == ui.fm_open);
        assert(power_audio_energy() > .01);
        key.key.repeat = 1;
        assert(TRANSPORT() && audio.keyboard_sequence.running);
        key.key.repeat = 0;
        assert(TRANSPORT() && !audio.keyboard_sequence.running);
    }
    ui.master_eq_open = ui.mosaic_open = ui.fm_open = 0;
    key.key.windowID = SDL_GetWindowID(window);
    /* Plain Space, other chords, foreign windows and modal editing keep ownership. */
    const SDL_Keymod modifiers[] = {KMOD_NONE, KMOD_SHIFT | KMOD_CTRL,
                                   KMOD_SHIFT | KMOD_ALT, KMOD_SHIFT | KMOD_GUI};
    for (size_t i = 0; i < sizeof(modifiers) / sizeof(*modifiers); ++i) {
        key.key.keysym.mod = modifiers[i]; assert(!TRANSPORT());
    }
    key.key.keysym.mod = KMOD_SHIFT;
    key.key.windowID = sister.window_id + 100; assert(!TRANSPORT());
    key.key.windowID = SDL_GetWindowID(window);
    int *blockers[] = {&ui.config_open, &ui.portal.open, &ui.import_preview_open,
        &ui.mosaic_edit_choice, &ui.exit_confirm_open, &ui.file_busy, &ui.midi_learn_active,
        &sister.model.preset_manage_open, &sister.model.fallout_lfo_open};
    for (size_t i = 0; i < sizeof(blockers) / sizeof(*blockers); ++i) {
        *blockers[i] = 1; assert(!TRANSPORT() && !audio.keyboard_sequence.running); *blockers[i] = 0;
    }
    ui.fm_open = ui.fm_bank_choice_open = 1; assert(!TRANSPORT());
    ui.fm_bank_choice_open = 0; ui.fm_full_choice_open = 1; assert(!TRANSPORT());
    ui.fm_open = ui.fm_full_choice_open = 0;
    ui.renaming_bank_slot = 0; assert(!TRANSPORT()); ui.renaming_bank_slot = -1;
    /* Shortcut Stop preserves manual HOLD and MIDI voices, just like the button. */
    ui.show_keyboard = 1;
    toggle_fm_hold(device, &audio, &ui, &instrument); click_note(0, 0);
    TsMidiEvent midi = {0}; midi.action = TS_MIDI_ACTION_NOTE_ON;
    assert(ts_note_event_midi(&midi.note, 67, 100, 2));
    handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
    assert(voices() == 2);
    assert(TRANSPORT() && audio.keyboard_sequence.running && voices() == 2);
    assert(TRANSPORT() && !audio.keyboard_sequence.running && voices() == 2);
    assert(power_audio_energy() > .01);
    stop_all_force(device, &audio, &ui);
    settings.count = 0; ts_keyboard_sequence_set(&audio.keyboard_sequence, &settings);
    assert(TRANSPORT() && !audio.keyboard_sequence.running && !voices());
    assert(strstr(ui.status, "SELECT KEYS"));
    SDL_DestroyWindow(sister.window); sister.window = NULL;
#undef TRANSPORT
}

static void test_keyboard_sequence_tile_change(void)
{
    char error[160];
    reset_route(0);
    assert(ts_instrument_copy_selected(&instrument, 1, error, sizeof(error)));
    /* Give the second tile different audio, then return to the first. */
    for (size_t i = 0; i < instrument.current.frames * instrument.current.channels; ++i)
        instrument.current.data[i] *= .5f;
    ++instrument.current.visual_revision;
    assert(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    TsKeyboardSequenceSettings settings = audio.keyboard_sequence.settings;
    settings.count = 3; settings.notes[0] = 60; settings.notes[1] = 64; settings.notes[2] = 67;
    settings.mode = TS_KEYBOARD_SEQUENCE_ORDER; settings.seconds = .04; settings.loop = 1;
    ts_keyboard_sequence_set(&audio.keyboard_sequence, &settings);
    int play_on_select = ui.play_on_select;
    for (int loop = 0; loop < 3; ++loop)
    for (int select = 0; select < 2; ++select) {
        reset_route(0);
        assert(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
        ui.play_on_select = select;
        if (loop) toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, loop == 2);
        keyboard_sequence_toggle_playback(device, &audio, &ui, &instrument, &fm, 44100);
        assert(power_audio_energy() > .01);
        TsKeyboardSequence *s = &audio.keyboard_sequence;
        int cursor = s->cursor, note = s->current_note;
        double elapsed = s->elapsed;
        uint64_t old_hash = ts_sample_hash(&s->source->sample);
        ts_ui_select_panel(&ui, TS_UI_PANEL_SAMPLE_TILES);
        lock_edit(device, &audio);
        assert(ts_ui_execute_bank_action(&instrument, 1, TS_UI_BANK_ACTION_AUDITION, error, sizeof(error)));
        unlock_edit(device, &audio, &ui, &instrument);
        sample_bank_audition_selected(device, &audio, &ui, &instrument, 1, 44100);
        refresh_workbench_loop(device, &audio, &ui, &instrument);
        assert(s->running && !audio.playing && !audio.tile_launcher_active);
        assert(ui.workbench_loop_active == (loop != 0));
        assert(keyboard_sequence_prepare(device, &audio, &ui, &instrument, &fm, 44100, 0));
        assert(s->cursor == cursor && s->current_note == note && s->elapsed == elapsed);
        assert(!memcmp(&settings, &s->settings, sizeof(settings)));
        assert(ts_sample_hash(&s->source->sample) != old_hash);
        assert(ts_sample_hash(&s->source->sample) == ts_sample_hash(&instrument.current));
        assert(power_audio_energy() > .01);
        stop_all_force(device, &audio, &ui); assert(!s->running);
    }
    /* After ARP stops, the normal click-to-launch behavior returns. */
    ui.play_on_select = 1;
    sample_bank_audition_selected(device, &audio, &ui, &instrument, 1, 44100);
    assert(audio.tile_launcher_active);
    stop_all_force(device, &audio, &ui);
    ui.play_on_select = play_on_select;
    assert(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    ui.show_keyboard = ui.keyboard_sequence_open = 1;
}

static void test_keyboard_sequence_projects(void)
{
    reset_route(0); ui.keyboard_sequence_open = 1; ui.keyboard_sequence_edit = 0;
    TsKeyboardSequence *s = &audio.keyboard_sequence;
    TsKeyboardSequenceBank original = ts_keyboard_sequence_export(s), bank;
    ts_keyboard_sequence_bank_default(&bank); ts_keyboard_sequence_set_bank(s, &bank);
    TsSamplePages pages; TsInstrument record; char error[160];
    assert(ts_sample_pages_init(&pages, error, sizeof(error))); ts_instrument_init(&record);
    const char *path = "test-arp-slots/test-arp-slots.tsr";
    for (int slot = 0; slot < TS_KEYBOARD_SEQUENCE_SLOTS; ++slot) {
        sequence_click(25 + slot * 37, 205, KMOD_NONE);
        assert(s->bank.selected == slot && !s->running);
        TsKeyboardSequenceSettings settings = s->settings;
        settings.count = 3; settings.notes[0] = 60 + slot; settings.notes[1] = 48 + slot; settings.notes[2] = 72 + slot;
        settings.mode = slot % TS_KEYBOARD_SEQUENCE_MODE_COUNT; settings.loop = slot % 2;
        settings.seconds = .23 + slot; settings.gate = .25 + slot / 32.;
        settings.volume = slot / 10.; settings.lfo_enabled = slot % 2;
        settings.lfo_seconds = 7 + slot; settings.lfo_depth = slot / 16.;
        ts_keyboard_sequence_set(s, &settings);
    }
    TsKeyboardSlotSequence chain = {1, TS_KEYBOARD_SEQUENCE_UP, 1, .05};
    ts_keyboard_sequence_set_slot_sequence(s, &chain);
    bank = ts_keyboard_sequence_export(s);
    keyboard_sequence_toggle_playback(device, &audio, &ui, &instrument, &fm, 44100);
    assert(s->running && power_audio_energy() > .01);
    PendingFileOperation pending = {0}; pending.active = 1; pending.mode = TS_BROWSER_SAVE_RECIPE;
    snprintf(pending.path, sizeof(pending.path), "%s", path);
    run_pending_file_operation(device, &audio, &ui, &instrument, NULL, NULL, &pages, &record, 0, &pending);
    assert(!pending.active && s->running && !strcmp(ui.project_path, path));
    assert(strstr(ui.status, "SAVED TSR PROJECT"));
    uint64_t saved = paged_project_state_hash(&pages, &instrument, &record, &audio.sister, s);
    assert(saved == ui.saved_state_hash);
    for (int i = 0; i < 4410; ++i) ts_keyboard_sequence_read(s, 44100);
    assert(s->slot_running && s->active_slot != s->bank.selected);
    assert(paged_project_state_hash(&pages, &instrument, &record, &audio.sister, s) == saved);
    /* Live selection immediately restarts the chosen slot without stealing HOLD. */
    toggle_fm_hold(device, &audio, &ui, &instrument); click_note(0, 0); assert(voices() == 1);
    for (int fm_view = 0; fm_view < 2; ++fm_view) {
        ui.fm_open = fm_view;
        sequence_click(25 + (fm_view + 2) * 37, 205 + (fm_view ? TS_ARP_PANEL_FM_OFFSET : 0), KMOD_NONE);
        assert(s->running && s->bank.selected == fm_view + 2 && s->elapsed == 0 && voices() == 1);
        ts_keyboard_sequence_read(s, 44100); assert(s->current_note >= 0);
    }
    ui.fm_open = 0;
    TsKeyboardSequenceSettings changed = s->settings; changed.volume = 1.91;
    ts_keyboard_sequence_set(s, &changed);
    assert(paged_project_state_hash(&pages, &instrument, &record, &audio.sister, s) != saved);
    assert(load_instrument(device, &audio, &ui, &instrument, &pages, &record, 0, path));
    assert(!s->running && s->source == NULL && s->elapsed == 0 && s->current_note == -1);
    assert(s->bank.selected == 15 && ui.keyboard_sequence_slot == 15 && ui.keyboard_sequence_occupied == 0xffff);
    assert(!s->slot_running && s->bank.sequence.enabled && s->bank.sequence.seconds == .05);
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) {
        const TsKeyboardSequenceSettings *a = &s->bank.slot[i], *b = &bank.slot[i];
        assert(a->count == b->count && !memcmp(a->notes, b->notes, sizeof(a->notes)));
        assert(a->mode == b->mode && a->loop == b->loop && a->seconds == b->seconds && a->gate == b->gate);
        assert(a->volume == b->volume && a->lfo_enabled == b->lfo_enabled && a->lfo_seconds == b->lfo_seconds && a->lfo_depth == b->lfo_depth);
    }
    assert(paged_project_state_hash(&pages, &instrument, &record, &audio.sister, s) == ui.saved_state_hash);
    keyboard_sequence_toggle_playback(device, &audio, &ui, &instrument, &fm, 44100);
    assert(s->running && power_audio_energy() > .01); /* Fresh source prepared on explicit PLAY. */
    FILE *file = fopen("test-arp-slots/sister-state.ini", "wb"); assert(file);
    fputs("TapeSister Sister Project State\nVersion=25\nPageCount=1\nActivePage=0\n", file); fclose(file);
    assert(load_instrument(device, &audio, &ui, &instrument, &pages, &record, 0, path));
    assert(!s->running && !s->settings.count && s->settings.volume == 1 && s->bank.selected == 0);
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) assert(!s->bank.slot[i].count);
    assert(!s->bank.sequence.enabled && s->bank.sequence.seconds == 10);
    /* Missing sidecar also restores defaults, never the prior project's bank. */
    ts_keyboard_sequence_set_bank(s, &bank); remove("test-arp-slots/sister-state.ini");
    assert(load_instrument(device, &audio, &ui, &instrument, &pages, &record, 0, path));
    assert(!s->settings.count && s->bank.selected == 0);
    ts_keyboard_sequence_set_bank(s, &original); keyboard_sequence_ui(&audio, &ui);
    ts_sample_pages_free(&pages); ts_instrument_free(&record);
}

static void test_keyboard_sequence_wheel(void)
{
    reset_route(0); ui.keyboard_sequence_open = 1;
    TsKeyboardSequence *s = &audio.keyboard_sequence;
    TsKeyboardSequenceBank original = ts_keyboard_sequence_export(s);
    const int xs[] = {40, 300, 40, 320, 500, 250};
    const int ys[] = {290, 290, 230, 230, 230, 174};
    for (int view = 0; view < 2; ++view) for (int slider = 0; slider < 6; ++slider) {
        ui.fm_open = view;
        TsKeyboardSequenceSettings settings; ts_keyboard_sequence_settings_default(&settings);
        ts_keyboard_sequence_set(s, &settings);
        TsKeyboardSlotSequence outer = {0, 0, 1, 10};
        ts_keyboard_sequence_set_slot_sequence(s, &outer);
        double *value = slider == 0 ? &s->settings.seconds : slider == 1 ? &s->settings.gate :
            slider == 2 ? &s->settings.volume : slider == 3 ? &s->settings.lfo_seconds :
            slider == 4 ? &s->settings.lfo_depth : &s->bank.sequence.seconds;
        double initial = *value;
        SDL_Event e; SDL_zero(e); e.type = SDL_MOUSEWHEEL;
        e.wheel.windowID = SDL_GetWindowID(window);
        int x = xs[slider], y = ys[slider] + (view ? TS_ARP_PANEL_FM_OFFSET : 0);
        SDL_WarpMouseInWindow(window, x, y);
#if SDL_VERSION_ATLEAST(2, 26, 0)
        e.wheel.mouseX = x; e.wheel.mouseY = y;
#endif
        /* Zero vertical and horizontal-only events must never act as clicks. */
        assert(keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(*value == initial && !ui.keyboard_sequence_drag);
        e.wheel.x = 1;
        assert(keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(*value == initial && !ui.keyboard_sequence_drag);
        e.wheel.x = 0; e.wheel.y = 1;
        assert(keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        int timing = slider == 0 || slider == 3 || slider == 5;
        double expected = timing ? initial / 1.1 : initial + .05;
        assert(fabs(*value - expected) < 1e-10 && !ui.keyboard_sequence_drag);
        e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        assert(keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(fabs(*value - initial) < 1e-10);
#if SDL_VERSION_ATLEAST(2, 0, 18)
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL; e.wheel.y = 0; e.wheel.preciseY = .25f;
        SDL_SetModState(KMOD_SHIFT);
        assert(keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        expected = timing ? initial * pow(1.01, -.25) : initial + .0025;
        assert(fabs(*value - expected) < 1e-10 && !ui.keyboard_sequence_drag);
        e.wheel.preciseY = -.25f;
        assert(keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(fabs(*value - initial) < 1e-10);
        SDL_SetModState(KMOD_NONE);
#endif
        /* A wheel event must not leave a drag for subsequent pointer motion. */
        SDL_zero(e); e.type = SDL_MOUSEMOTION; e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = x + 10; e.motion.y = y;
        assert(!keyboard_sequence_event(&e, window, device, &audio, &ui, &instrument, &fm, 44100));
        assert(fabs(*value - initial) < 1e-10 && !ui.keyboard_sequence_drag);
    }
    ui.fm_open = 0;
    ts_keyboard_sequence_set_bank(s, &original); keyboard_sequence_ui(&audio, &ui);
}

static void test_keyboard_bank_editing(void)
{
    reset_route(0); ui.keyboard_sequence_open = 1;
    toggle_fm_hold(device, &audio, &ui, &instrument); click_note(0, 0); assert(voices() == 1);
    TsKeyboardSequence *s = &audio.keyboard_sequence;
    TsKeyboardSequenceBank original = ts_keyboard_sequence_export(s), empty;
    ts_keyboard_sequence_bank_default(&empty);
    for (int view = 0; view < 2; ++view) {
        ts_keyboard_sequence_set_bank(s, &empty);
        ui.fm_open = view; int dy = view ? TS_ARP_PANEL_FM_OFFSET : 0;
        ui.keyboard_sequence_clipboard_valid = 0;
        sequence_click(100, 144 + dy, KMOD_NONE); assert(!s->settings.count);
        TsKeyboardSequenceSettings pattern = s->settings;
        pattern.count = 3; pattern.notes[0] = 67; pattern.notes[1] = 59; pattern.notes[2] = 74;
        pattern.mode = TS_KEYBOARD_SEQUENCE_ORDER; pattern.seconds = .31; pattern.gate = .42;
        pattern.volume = .63; pattern.lfo_enabled = 1; pattern.lfo_seconds = 7; pattern.lfo_depth = .81;
        ts_keyboard_sequence_set(s, &pattern);
        sequence_click(30, 144 + dy, KMOD_NONE);
        assert(ui.keyboard_sequence_clipboard_valid && !ts_keyboard_sequence_active(s));
        sequence_click(62, 205 + dy, KMOD_NONE);
        sequence_click(100, 144 + dy, KMOD_NONE);
        assert(!ts_keyboard_sequence_active(s) && ui.keyboard_sequence_edit);
        assert(!memcmp(&s->settings, &pattern, sizeof(pattern)));
        pattern.notes[0] = 65; ts_keyboard_sequence_set(s, &pattern);
        assert(s->bank.slot[0].notes[0] == 67 && ui.keyboard_sequence_clipboard.notes[0] == 67);
        keyboard_sequence_toggle_playback(device, &audio, &ui, &instrument, &fm, 44100);
        TsKeyboardSlotSequence chain = {1, TS_KEYBOARD_SEQUENCE_UP, 1, .05};
        ts_keyboard_sequence_set_slot_sequence(s, &chain);
        for (int i = 0; i < 3000; ++i) ts_keyboard_sequence_read(s, 44100);
        int destination = s->active_slot;
        assert(destination != s->bank.selected); /* Paste targets the live highlight. */
        sequence_click(100, 144 + dy, KMOD_NONE);
        assert(s->running && s->slot_running && s->active_slot == destination);
        assert(s->elapsed == 0 && s->slot_elapsed == 0 && s->lfo_phase == 0);
        assert(s->settings.notes[0] == 67 && s->bank.selected == destination);
        int manual_voices = voices();
        sequence_click(180, 144 + dy, KMOD_NONE);
        assert(ui.keyboard_sequence_clear_deadline && s->running && s->bank.order_count == 2);
        sequence_click(30, 144 + dy, KMOD_NONE); /* Another control cancels confirmation. */
        assert(!ui.keyboard_sequence_clear_deadline && s->bank.order_count == 2);
        sequence_click(180, 144 + dy, KMOD_NONE);
        ui.keyboard_sequence_clear_deadline = SDL_GetTicks() - 1;
        sequence_click(180, 144 + dy, KMOD_NONE); /* Expired click only re-arms. */
        assert(s->bank.order_count == 2);
        sequence_click(180, 144 + dy, KMOD_NONE);
        assert(!ts_keyboard_sequence_active(s) && !s->bank.sequence.enabled);
        assert(!s->bank.order_count && !s->settings.count && s->active_slot == 0);
        assert(s->bank.sequence.seconds == .05 && s->bank.sequence.loop == 1);
        assert(voices() == manual_voices && ui.keyboard_sequence_clipboard_valid);
        for (int i = 0; i < 16; ++i) assert(!s->bank.slot[i].count && s->bank.slot[i].volume == 1);
        for (int i = 0; i < 500; ++i) ts_keyboard_sequence_read(s, 44100);
        assert(!s->running && !s->slot_running && !s->last.l && !s->last.r);
        sequence_click(100, 144 + dy, KMOD_NONE); /* Clipboard survives CLEAR ALL. */
        assert(s->settings.count == 3 && !ts_keyboard_sequence_active(s));
        assert(s->bank.order_count == 1 && s->bank.order[0] == 0);
    }
    stop_all_force(device, &audio, &ui);
    ui.fm_open = 0;
    ts_keyboard_sequence_set_bank(s, &original); keyboard_sequence_ui(&audio, &ui);
}

static void test_keyboard_slot_controls(void)
{
    reset_route(0); ui.keyboard_sequence_open = 1; ui.keyboard_sequence_edit = 0;
    TsKeyboardSequence *s = &audio.keyboard_sequence;
    TsKeyboardSequenceBank original = ts_keyboard_sequence_export(s), bank;
    ts_keyboard_sequence_bank_default(&bank); ts_keyboard_sequence_set_bank(s, &bank);
    TsKeyboardSequenceSettings settings = s->settings;
    settings.count = 3; settings.notes[0] = 67; settings.notes[1] = 71; settings.notes[2] = 74;
    settings.seconds = .2; ts_keyboard_sequence_set(s, &settings);
    toggle_fm_hold(device, &audio, &ui, &instrument); click_note(0, 0); assert(voices() == 1);
    keyboard_sequence_toggle_playback(device, &audio, &ui, &instrument, &fm, 44100);
    assert(power_audio_energy() > .01);
    for (int view = 0; view < 2; ++view) {
        ui.fm_open = view; int dy = view ? TS_ARP_PANEL_FM_OFFSET : 0;
        double elapsed = s->elapsed, phase = s->lfo_phase;
        int copied_pitch = s->settings.notes[0];
        ui.keyboard_sequence_edit = 0;
        sequence_click(25 + (view + 1) * 37, 205 + dy, KMOD_NONE);
        assert(s->active_slot == view + 1 && s->elapsed == elapsed && s->lfo_phase == phase);
        assert(ui.keyboard_sequence_edit && s->settings.notes[0] == copied_pitch && voices() == 1);
        settings = s->settings; settings.notes[0] += view + 1;
        ts_keyboard_sequence_set(s, &settings);
        /* Top row: enable/disable without interrupting held notes, order and loop. */
        sequence_click(30, 174 + dy, KMOD_NONE); assert(s->slot_running);
        sequence_click(125, 174 + dy, KMOD_NONE); assert(s->bank.sequence.mode == view + 1);
        sequence_click(445, 174 + dy, KMOD_NONE); assert(s->bank.sequence.loop == view);
        ui.config.arp_slot_max_seconds = 1200;
        sequence_click(425, 174 + dy, KMOD_NONE); assert(s->bank.sequence.seconds > 1100);
        sequence_click(226, 174 + dy, KMOD_NONE); assert(s->bank.sequence.seconds == .05);
        assert(voices() == 1);
        sequence_click(202, 260 + dy, KMOD_NONE); /* Reset applies the new 50 ms hold. */
        ui.keyboard_sequence_open = 0;
        for (int i = 0; i < 4410; ++i) ts_keyboard_sequence_read(s, 44100);
        ui.keyboard_sequence_open = 1; keyboard_sequence_ui(&audio, &ui);
        assert(ui.keyboard_sequence_slot == s->active_slot);
        sequence_click(30, 174 + dy, KMOD_NONE); assert(!s->slot_running);
        /* Restart inner playback if the outer one-pass cycle has just completed. */
        if (!s->running) keyboard_sequence_toggle_playback(device, &audio, &ui, &instrument, &fm, 44100);
    }
    stop_all_force(device, &audio, &ui);
    ui.fm_open = 0; ui.config.arp_slot_max_seconds = 240;
    ts_keyboard_sequence_set_bank(s, &original); keyboard_sequence_ui(&audio, &ui);
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
        /* The newly covered row belongs to ARP, not the controls underneath. */
        sequence_click(12, ui.fm_open ? TS_ARP_PANEL_FM_Y : TS_ARP_PANEL_Y, KMOD_NONE);
        if (ui.fm_open) sequence_click(12, TS_ARP_PANEL_FM_Y + TS_ARP_PANEL_FM_H - 1, KMOD_NONE);
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
        /* ARP mute leaves the held drone's actual samples and voice ownership
           unchanged, including FM's dry/SYNTH paths and ensemble sources. */
        int offset = ui.fm_open ? TS_ARP_PANEL_FM_OFFSET : 0;
        sequence_click(20, 230 + offset, KMOD_NONE);
        assert(audio.keyboard_sequence.settings.volume == 0 && audio.keyboard_sequence.running);
        assert(power_audio_energy() > .01 && voices() == 2);
        static TsNoteBank reference_notes;
        static TsPerformanceBank reference_performance;
        reference_notes = audio.notes; reference_performance = audio.performance;
        for (int i = 0; i < 256; ++i) {
            TsStereoFrame sample = {0}, synth = {0}, f = {0};
            ts_note_bank_read_buses(&reference_notes, &sample, &f, &synth);
            TsStereoFrame p = ts_performance_read_stereo(&reference_performance, NULL);
            TsAudioBuses buses = {0}; TsStereoFrame capture = {0};
            runtime_note_render(&audio, &buses, &capture);
            assert(fabs(audio.keyboard_dry.l - (sample.l + p.l + f.l * audio.fm_output_gain)) < .000001);
            assert(fabs(audio.keyboard_dry.r - (sample.r + p.r + f.r * audio.fm_output_gain)) < .000001);
            assert(capture.l == synth.l && capture.r == synth.r);
        }
        double elapsed = audio.keyboard_sequence.elapsed;
        sequence_click(67, 230 + offset, KMOD_NONE);
        assert(audio.keyboard_sequence.settings.volume == .5 && audio.keyboard_sequence.elapsed == elapsed);
        sequence_click(240, 230 + offset, KMOD_NONE);
        sequence_click(548, 230 + offset, KMOD_NONE);
        assert(audio.keyboard_sequence.settings.lfo_enabled && audio.keyboard_sequence.settings.lfo_depth == .5);
        assert(audio.keyboard_sequence.elapsed == elapsed && voices() == 2);
        ui.keyboard_sequence_open = 0;
        double phase = audio.keyboard_sequence.lfo_phase;
        assert(power_audio_energy() > .01 && audio.keyboard_sequence.lfo_phase != phase);
        ui.keyboard_sequence_open = 1;
        sequence_click(240, 230 + offset, KMOD_NONE);
        sequence_click(114, 230 + offset, KMOD_NONE);
        assert(audio.keyboard_sequence.settings.volume == 1 && !audio.keyboard_sequence.settings.lfo_enabled);
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
    SDL_Event key;SDL_zero(key);
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
    test_keyboard_sequence_transport();
    test_keyboard_sequence_tile_change();
    test_keyboard_sequence_projects();
    test_keyboard_slot_controls();
    test_keyboard_bank_editing();
    test_keyboard_sequence_wheel();
    /* Native UI capture with ordered white and black keys and a running step. */
    sequence_click(280, 260, KMOD_NONE);
    sequence_select(0); sequence_select(7); sequence_select(3); sequence_select(10);
    audio.keyboard_sequence.settings.mode = TS_KEYBOARD_SEQUENCE_ORDER;
    audio.keyboard_sequence.settings.seconds = .25;
    audio.keyboard_sequence.settings.volume = .65;
    audio.keyboard_sequence.settings.lfo_enabled = 1;
    audio.keyboard_sequence.settings.lfo_seconds = 4;
    audio.keyboard_sequence.settings.lfo_depth = .6;
    ts_keyboard_sequence_set(&audio.keyboard_sequence, &audio.keyboard_sequence.settings);
    /* Populate a few independent slots in the native screenshot. */
    for (int i = 1; i <= 3; ++i) {
        ts_keyboard_sequence_select_slot(&audio.keyboard_sequence, i);
        TsKeyboardSequenceSettings slot = audio.keyboard_sequence.settings;
        slot.count = 3; slot.notes[0] = 60 + i; slot.notes[1] = 67 + i; slot.notes[2] = 72 + i;
        slot.mode = i; slot.seconds = .25 * (i + 1);
        ts_keyboard_sequence_set(&audio.keyboard_sequence, &slot);
    }
    ts_keyboard_sequence_select_slot(&audio.keyboard_sequence, 0);
    TsKeyboardSlotSequence chain = {1, TS_KEYBOARD_SEQUENCE_UP_DOWN, 1, 10};
    ts_keyboard_sequence_set_slot_sequence(&audio.keyboard_sequence, &chain);
    sequence_click(36, 260, KMOD_NONE); assert(power_audio_energy() > .01);
    keyboard_sequence_ui(&audio, &ui); ui.keyboard_sequence_edit = 1;
    ui.overlay[0] = 0; /* The normal event loop expires the earlier Save overlay. */
    const char *shot = getenv("TS_TEST_KEYBOARD_SEQUENCE_PPM");
    if (shot) {
        static TsFramebuffer fb;
        size_t first = instrument.view_first, last = instrument.view_last;
        instrument.view_first = 0; instrument.view_last = instrument.current.sample_rate / 20;
        ts_ui_render(&fb, &ui, &instrument); assert(ts_ui_write_ppm(&fb, shot));
        instrument.view_first = first; instrument.view_last = last;
    }
    const char *fm_shot = getenv("TS_TEST_KEYBOARD_SEQUENCE_FM_PPM");
    if (fm_shot) {
        static TsFramebuffer fb;
        ui.fm_open = 1; ui.fm_preview_sample = &fm;
        ts_ui_render(&fb, &ui, &instrument); assert(ts_ui_write_ppm(&fb, fm_shot));
        ui.fm_open = 0; ui.fm_preview_sample = NULL;
    }
    stop_all_force(device, &audio, &ui);
    audio.keyboard_sequence.settings.volume = 1;
    audio.keyboard_sequence.settings.lfo_enabled = 0;
    ts_keyboard_sequence_set(&audio.keyboard_sequence, &audio.keyboard_sequence.settings);
    ts_keyboard_sequence_source_free(ts_keyboard_sequence_source(&audio.keyboard_sequence, NULL));
    ui.keyboard_sequence_open = 0; ui.keyboard_sequence_edit = 0;
}
