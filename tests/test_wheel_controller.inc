static SDL_Event wheel_fixture(double amount, int x, int y)
{
    SDL_Event e; SDL_zero(e); e.type = SDL_MOUSEWHEEL;
    e.wheel.windowID = SDL_GetWindowID(window); e.wheel.y = (int)amount;
    e.wheel.timestamp = 100;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    e.wheel.preciseY = (float)amount;
#endif
#if SDL_VERSION_ATLEAST(2, 26, 0)
    e.wheel.mouseX = x; e.wheel.mouseY = y;
#else
    SDL_WarpMouseInWindow(window, x, y);
#endif
    return e;
}

static void test_wheel_pipeline(void)
{
    SDL_Event e;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    for (int i = 0; i < 4; ++i) {
        e = wheel_fixture(.25, 40, 340);
        e.wheel.y = i == 3; /* Integer delta supplied by SDL's accumulator. */
        assert(wheel_event_prepare(&e));
        assert(e.wheel.y == (i == 3));
        assert(wheel_event_delta(&e, 0) == .25);
    }
#endif
    e = wheel_fixture(3, 40, 340); e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    assert(wheel_event_prepare(&e));
    assert(e.wheel.y == -3 && e.wheel.direction == SDL_MOUSEWHEEL_NORMAL);
    e = wheel_fixture(0, 40, 340); assert(!wheel_event_prepare(&e));
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    e = wheel_fixture(.25, 40, 340);
    for (int i = 0; i < 7; ++i) { SDL_Event next = e; next.wheel.y = (i + 2) % 4 == 0; assert(SDL_PushEvent(&next) == 1); }
    SDL_Event key = {0}; key.type = SDL_KEYUP; key.key.windowID = SDL_GetWindowID(window);
    assert(SDL_PushEvent(&key) == 1);
    SDL_Event next = e; assert(SDL_PushEvent(&next) == 1);
    wheel_event_coalesce(&e);
    assert(wheel_event_delta(&e, 0) == 2 && e.wheel.y == 2);
    assert(SDL_PeepEvents(&next,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT) == 1 && next.type == SDL_KEYUP);
    assert(SDL_PeepEvents(&next,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT) == 1 && next.type == SDL_MOUSEWHEEL);
    /* A burst preserves all movement with bounded work per dispatch. */
    for (int i = 0; i < 1000; ++i) {
        next = wheel_fixture(.25, 40, 340); next.wheel.y = (i + 1) % 4 == 0; assert(SDL_PushEvent(&next) == 1);
    }
    int batches = 0, detents = 0; double movement = 0;
    while (SDL_PeepEvents(&e,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT) == 1) {
        wheel_event_coalesce(&e); movement += wheel_event_delta(&e,0); detents += e.wheel.y; ++batches;
    }
    assert(batches == 32 && movement == 250 && detents == 250);
    /* Never merge opposite directions, different positions, or different windows. */
    for (int variation = 0; variation < 3; ++variation) {
        e = wheel_fixture(1, 40, 340); next = e;
        if (!variation) { next.wheel.y = -1; next.wheel.preciseY = -1; }
        else if (variation == 1) ++next.wheel.mouseX;
        else ++next.wheel.windowID;
        assert(SDL_PushEvent(&next) == 1); wheel_event_coalesce(&e);
        assert(wheel_event_delta(&e, 0) == 1);
        assert(SDL_PeepEvents(&next,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT) == 1);
    }
#endif
    /* Each fifth-detent packet must move the canvas immediately, including
       the first four packets for which SDL's integer count is still zero. */
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_SetModState(KMOD_NONE);
    ImportController canvas_import = {0};
    canvas_import.decoded.sample = instrument.current; /* Read-only borrowed sample. */
    for (int source = 0; source < 3; ++source)
    for (int direction = -1; direction <= 1; direction += 2) {
        ui.audition_source = source == 1 ? TS_AUDITION_PARENT : TS_AUDITION_CURRENT;
        const ImportController *import = source == 2 ? &canvas_import : NULL;
        size_t frames = source == 1 ? instrument.parent.frames : instrument.current.frames;
        assert(frames > 1000);
        size_t *first = source == 1 ? &ui.parent_view_first : source == 2 ?
            &ui.import_preview_view_first : &instrument.view_first;
        size_t *last = source == 1 ? &ui.parent_view_last : source == 2 ?
            &ui.import_preview_view_last : &instrument.view_last;
        *first = frames / 4; *last = frames * 3 / 4;
        size_t initial = *last - *first;
        for (int i = 0; i < 5; ++i) {
            size_t before = *last - *first;
            e = wheel_fixture(.2, 320, 120); e.wheel.y = i == 4;
            if (direction < 0) e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
            wheel_event_coalesce(&e); wheel_event_prepare(&e);
            assert(canvas_zoom_wheel(&e, &ui, &instrument, import, 320));
            assert(direction > 0 ? *last - *first < before : *last - *first > before);
            /* Cursor anchor stays fixed within frame rounding. */
            double ratio = source == 2 ? (320.0 - 36) / TS_IMPORT_PREVIEW_COLUMNS : .5;
            double anchor = frames / 4 + initial * ratio;
            assert(fabs((*first + (*last - *first) * ratio) - anchor) < 5);
        }
        size_t fractional_span = *last - *first;
        *first = frames / 4; *last = frames * 3 / 4;
        e = wheel_fixture(direction, 320, 120);
        wheel_event_prepare(&e);
        assert(canvas_zoom_wheel(&e, &ui, &instrument, import, 320));
        assert(fabs((double)(*last - *first) - fractional_span) < 5);
        /* Fine packets must not turn pan/selection gestures into zoom. */
        for (int modifier = 0; modifier < 3; ++modifier) {
            SDL_SetModState(modifier == 0 ? KMOD_SHIFT : modifier == 1 ? KMOD_ALT : KMOD_CTRL);
            e = wheel_fixture(.2, 320, 120);
            if (modifier == 2 && import) continue; /* Import retains Ctrl+zoom. */
            size_t before = *last - *first;
            assert(!canvas_zoom_wheel(&e, &ui, &instrument, import, 320));
            assert(*last - *first == before);
        }
        SDL_SetModState(KMOD_NONE);
        e = wheel_fixture(0, 320, 120);
        assert(!canvas_zoom_wheel(&e, &ui, &instrument, import, 320));
        e.wheel.x = 1; e.wheel.preciseX = 1;
        assert(!canvas_zoom_wheel(&e, &ui, &instrument, import, 320));
    }
    ui.audition_source = TS_AUDITION_CURRENT;
#endif
    reset_route(0); ui.keyboard_sequence_open = 0;
    toggle_fm_hold(device, &audio, &ui, &instrument); click_note(0, 0); assert(voices() == 1);
    for (int view = 0; view < 2; ++view) {
        ui.fm_open = view;
        int base = ts_ui_keyboard_base_note(&ui);
        SDL_SetModState(KMOD_SHIFT);
        for (int i = 0; i < 4; ++i) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
            e = wheel_fixture(.25, 40, 340);
#else
            e = wheel_fixture(i == 3, 40, 340);
#endif
            e.wheel.y = i == 3;
            wheel_event_prepare(&e);
            assert(keyboard_pointer_event(&e,window,device,&audio,&ui,&instrument,&fm,44100));
            assert(ts_ui_keyboard_base_note(&ui) == base + (i == 3));
            assert(voices() == 1);
        }
        e = wheel_fixture(-1, 40, 340); wheel_event_prepare(&e);
        assert(keyboard_pointer_event(&e,window,device,&audio,&ui,&instrument,&fm,44100));
        assert(ts_ui_keyboard_base_note(&ui) == base && voices() == 1);
    }
    /* SDL already accumulates high-resolution packets into integer detents.
       Keep scrolling after the first note, even while the pointer trembles
       within the keyboard or individual packets arrive slowly. */
    for (int view = 0; view < 2; ++view) for (int arp = 0; arp < 2; ++arp)
    for (int direction = -1; direction <= 1; direction += 2)
    for (int scale = 1; scale <= 2; ++scale) {
        ui.fm_open = view; ui.keyboard_sequence_open = arp;
        SDL_SetModState(KMOD_SHIFT);
        SDL_SetWindowSize(window, 640 * scale, 400 * scale);
        set_keyboard_octave(device, &audio, &ui, 4);
        int base = ts_ui_keyboard_base_note(&ui);
        for (int i = 0; i <= 24; ++i) {
            e = wheel_fixture(direction * (i ? .25 : 1), (40 + i % 2) * scale, 340 * scale);
            e.wheel.y = direction * (!i || i % 4 == 0);
            e.wheel.timestamp = 100 + i * 600;
            wheel_event_coalesce(&e);
            wheel_event_prepare(&e);
            assert(!keyboard_sequence_event(&e,window,device,&audio,&ui,&instrument,&fm,44100));
            assert(keyboard_pointer_event(&e,window,device,&audio,&ui,&instrument,&fm,44100));
            assert(ts_ui_keyboard_base_note(&ui) == base + direction * (1 + i / 4));
            assert(voices() == 1);
        }
    }
    SDL_SetWindowSize(window, 640, 400);
    ui.keyboard_sequence_open = 0;
    SDL_SetModState(KMOD_NONE); stop_all_force(device, &audio, &ui); ui.fm_open = 0;
    /* EQ also uses fractional delta and the queued position, not live cursor. */
    master_eq_open(device, &audio, &ui);
    float before = ui.config.master_eq.band[ui.master_eq_band].frequency;
    e = wheel_fixture(.25, 40, 310);
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_WarpMouseInWindow(window, 610, 390);
#endif
    wheel_event_prepare(&e);
    assert(master_eq_event(&e,window,device,&audio,&ui,&eq_panel));
#if SDL_VERSION_ATLEAST(2, 0, 18)
    assert(fabs(ui.config.master_eq.band[ui.master_eq_band].frequency - before * pow(2,.25*.05)) < .001);
#endif
    assert(!ui.master_eq_drag); ui.master_eq_open = 0;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}
