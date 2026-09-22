/* Real keyboard gestures and the between-event refresh that used to add C4. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static AudioState audio;
static TsUiState ui;
static TsInstrument instrument;
static TsSample fm;
static SDL_Window *window;
static SDL_AudioDeviceID device;

static int voices(void)
{
    return ts_note_bank_count(&audio.notes) +
           ts_performance_count(&audio.performance) +
           ts_performance_count(&audio.sister.performance);
}

static void tick(void)
{
    refresh_workbench_loop(device, &audio, &ui, &instrument);
    for (int i = 0; i < 256; ++i) {
        TsStereoFrame f = ts_note_bank_read_stereo(&audio.notes);
        TsStereoFrame p = ts_performance_read_stereo(&audio.performance, NULL);
        TsStereoFrame s = ts_performance_read_stereo(&audio.sister.performance, NULL);
        assert(isfinite(f.l + f.r + p.l + p.r + s.l + s.r));
    }
}

static void click_note_preview(int note, int shifted, const TsSample *preview)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.windowID = SDL_GetWindowID(window);
    /* Find the actual key hit area rather than assuming a white-key width. */
    int found = 0;
    for (int y = 368; y >= 300 && !found; --y)
    for (int x = 0; x < 640 && !found; ++x)
        if (ts_ui_key_from_point_for_base(x, y,
                                         ts_ui_keyboard_base_note(&ui)) == note) {
            event.button.x = x;
            event.button.y = y;
            found = 1;
        }
    assert(found);
    SDL_SetModState(shifted ? KMOD_SHIFT : KMOD_NONE);
    assert(keyboard_pointer_event(&event, window, device, &audio, &ui,
                                  &instrument, preview, 44100));
    tick();
    event.type = SDL_MOUSEBUTTONUP;
    (void)keyboard_pointer_event(&event, window, device, &audio, &ui,
                                 &instrument, preview, 44100);
    tick();
}

static void click_note(int note, int shifted)
{
    click_note_preview(note, shifted, &fm);
}

static void reset_route(int route)
{
    stop_all_force(device, &audio, &ui);
    ui.fm_open = route == 1;
    ui.show_keyboard = 1;
    audio.performance_group_latched = route == 2;
    audio.performance_source_mask = route == 2 ? 1u : 0u;
    ts_sister_runtime_set_sources(&audio.sister, route == 3 ? TS_SISTER_SOURCE_TILES : 0u);
    ts_sister_runtime_clear_source_mask(&audio.sister);
    assert(ts_sister_runtime_set_source_slot(&audio.sister, &instrument, 0, 1));
}

static void test_chords(void)
{
    /* Every mix of ordinary/Shift presses and releases, with either note last.
       Modes: HOLD alone, LOOP armed on notes, standalone LOOP taken over by notes. */
    for (int loop = 0; loop < 3; ++loop)
    for (int route = 0; route < 4; ++route)
    for (int shift = 0; shift < 16; ++shift)
    for (int last = 0; last < 2; ++last) {
        reset_route(route);
        if (loop == 2) {
            ui.fm_open = 0;
            toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, 0);
            ui.fm_open = route == 1;
            assert(audio.playing);
        }
        toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(0, shift & 1); /* C4 */
        click_note(4, shift & 2); /* E4 */
        assert(voices() == 2 && !audio.playing);
        if (loop == 1)
            toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, 0);
        if (loop == 0 && route == 0 && shift == 5 && last == 0) {
            static PortalController portal;
            TsFmSeedSequence seeds;
            portal_init(&portal, &ui.portal);
            ts_fm_seed_sequence_init(&seeds, 999);
            portal.create_seeds = &seeds;
            SDL_Event create = {0};
            create.type = SDL_MOUSEBUTTONDOWN;
            create.button.button = SDL_BUTTON_LEFT;
            for (int wave = 0; wave < 4; ++wave) {
                SDL_SetModState(KMOD_SHIFT);
                assert(portal_create_event(&create, 100, 214, device, &audio,
                                           &ui, &instrument, &portal, NULL));
                tick();
                assert(ui.keyboard_hold && voices() == 2 && !audio.playing);
            }
            portal_free(&portal);
        }
        click_note(last ? 0 : 4, shift & 4);
        assert(voices() == 1 && !audio.playing);
        click_note(last ? 4 : 0, shift & 8);
        if (voices() || audio.playing)
            fprintf(stderr, "Stuck final note: loop=%d route=%d shift=%d last=%s voices=%d transport=%d\n",
                    loop, route, shift, last ? "E4" : "C4", voices(), audio.playing);
        assert(!voices() && !audio.playing);
        for (int frame = 0; frame < 3; ++frame) tick();
        assert(!voices() && !audio.playing && ui.keyboard_hold);
        /* Clearing the chord leaves HOLD/LOOP ready for the next performance. */
        click_note(0, 0);
        assert(voices() == 1 && !audio.playing);
        toggle_fm_hold(device, &audio, &ui, &instrument);
        tick();
        assert(!voices() && !audio.playing);
    }
}

static void test_loop_transport(void)
{
    /* SDL can deliver a start and release before the next UI refresh. FM
       must take over the standalone voice immediately, just like tile notes. */
    reset_route(0);
    toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, 0);
    ui.fm_open = 1;
    toggle_fm_hold(device, &audio, &ui, &instrument);
    begin_fm_note(device, &audio, &ui, &instrument, &fm, 4, 44100, 1);
    assert(!audio.playing && voices() == 1);
    begin_fm_note(device, &audio, &ui, &instrument, &fm, 4, 44100, 0);
    tick();
    assert(!audio.playing && !voices());
    for (int locked = 0; locked < 2; ++locked) {
        reset_route(0);
        toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, locked);
        audio.position = 200;
        tick();
        assert(audio.playing && audio.position == 200);
        if (locked) {
            toggle_fm_hold(device, &audio, &ui, &instrument);
            click_note(0, 1);
            assert(voices() == 1 && !audio.playing);
            click_note(0, 0);
            assert(!voices() && audio.playing); /* Explicit lock resumes. */
            stop_all(device, &audio, &ui);
            tick();
            assert(audio.playing && ui.workbench_loop_persistent);
            toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, 1);
        } else toggle_workbench_loop(device, &audio, &ui, &instrument, 44100, 0);
        tick();
        assert(!audio.playing && !ui.workbench_loop_active);
    }
}

static void test_trigger_identity(void)
{
    for (int route = 0; route < 4; ++route) {
        reset_route(route);
        toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(0, 0);
        click_note(4, 1);
        TsMidiEvent midi = {0};
        midi.action = TS_MIDI_ACTION_NOTE_ON;
        assert(ts_note_event_midi(&midi.note, 60, 100, 0));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(ts_note_event_midi(&midi.note, 60, 100, 1));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 4);
        click_note(0, 1);
        assert(voices() == 3); /* Same-pitch MIDI notes belong to other triggers. */
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 2); /* Channel 0 is still held. */
        assert(ts_note_event_midi(&midi.note, 60, 100, 0));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 1);
        click_note(4, 0);
        assert(!voices() && !audio.playing);
    }
}

static void test_route_change(void)
{
    for (int from = 0; from < 4; ++from)
    for (int to = 0; to < 4; ++to)
    for (int last = 0; last < 2; ++last) {
        if (from == to) continue;
        reset_route(from);
        toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(0, 0);
        click_note(4, 1);
        click_note(last ? 0 : 4, 0);
        assert(voices() == 1);
        /* Changing TILES routing while a drone plays must not orphan its
           original owner. Turning Prism/Sister on can change this route. */
        ui.fm_open = to == 1;
        audio.performance_group_latched = to == 2;
        audio.performance_source_mask = to == 2 ? 1u : 0u;
        ts_sister_runtime_set_sources(&audio.sister,
                                       to == 3 ? TS_SISTER_SOURCE_TILES : 0u);
        click_note(last ? 4 : 0, 1);
        if (voices()) fprintf(stderr, "Orphaned latch: route=%d->%d last=%s voices=%d\n",
                              from, to, last ? "E4" : "C4", voices());
        assert(!voices() && !audio.playing);
    }
}

static void assert_keyboard_silent(void)
{
    assert(!voices());
    for (int i = 0; i < 256; ++i) {
        TsStereoFrame f = ts_note_bank_read_stereo(&audio.notes);
        TsStereoFrame p = ts_performance_read_stereo(&audio.performance, NULL);
        TsStereoFrame s = ts_performance_read_stereo(&audio.sister.performance, NULL);
        assert(f.l == 0 && f.r == 0 && p.l == 0 && p.r == 0 && s.l == 0 && s.r == 0);
    }
}

static void test_shifted_hold_release(void)
{
    const int shifts[] = {12, -12, 1, -1};
    for (int route = 0; route < 4; ++route)
    for (int held = 0; held < 2; ++held)
    for (int move = 0; move < 4; ++move)
    for (int note = 0; note < 24; ++note) {
        int visible_note = note - shifts[move];
        if (visible_note < 0 || visible_note >= 24) continue;
        reset_route(route);
        set_keyboard_octave(device, &audio, &ui, 4);
        if (held) toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(note, !held);
        assert(voices() == 1);
        if (move < 2) set_keyboard_octave(device, &audio, &ui, move ? 3 : 5);
        else shift_keyboard_range(device, &audio, &ui, shifts[move]);
        int base = ts_ui_keyboard_base_note(&ui);
        uint32_t lit = ts_note_bank_visible_mask(&audio.notes, base) |
                       ts_performance_visible_mask(&audio.performance, base) |
                       ts_performance_visible_mask(&audio.sister.performance, base);
        assert(lit == (1u << visible_note));
        click_note(visible_note, !held);
        if (voices()) fprintf(stderr, "Held pitch failed to release: route=%d shift=%d note=%d voices=%d\n",
                              route, shifts[move], note, voices());
        assert_keyboard_silent();
    }
    set_keyboard_octave(device, &audio, &ui, 4);
}

static void test_fm_release_without_preview(void)
{
    const TsSample empty = {0};
    for (int route = 0; route < 4; ++route)
    for (int missing = 0; missing < 2; ++missing) {
        reset_route(route);
        toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(0, 0);
        assert(voices() == 1);
        ui.fm_open = 1;
        click_note_preview(0, 0, missing ? NULL : &empty);
        assert_keyboard_silent();
        click_note_preview(4, 0, missing ? NULL : &empty);
        assert_keyboard_silent(); /* An unavailable preview cannot start a note. */
    }
}

static void test_shifted_hold_isolation(void)
{
    for (int route = 0; route < 4; ++route) {
        reset_route(route);
        set_keyboard_octave(device, &audio, &ui, 4);
        toggle_fm_hold(device, &audio, &ui, &instrument);
        click_note(12, 0); /* QWERTY C5 plus the same pitch on two MIDI channels. */
        TsMidiEvent midi = {0};
        midi.action = TS_MIDI_ACTION_NOTE_ON;
        assert(ts_note_event_midi(&midi.note, 72, 100, 0));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(ts_note_event_midi(&midi.note, 72, 100, 1));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 3);
        set_keyboard_octave(device, &audio, &ui, 5);
        click_note(12, 0); /* Original key position now starts C6. */
        assert(voices() == 4);
        click_note(0, 0); /* Only QWERTY C5 is released. */
        assert(voices() == 3);
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 2);
        assert(ts_note_event_midi(&midi.note, 72, 100, 0));
        handle_midi_event(device, &audio, &ui, &instrument, &fm, &midi, 44100);
        assert(voices() == 1);
        click_note(12, 0); /* C6 remained held throughout. */
        assert_keyboard_silent();
    }
    set_keyboard_octave(device, &audio, &ui, 4);
}

static void test_key_up_after_octave_change(void)
{
    for (int route = 0; route < 4; ++route) {
        reset_route(route);
        set_keyboard_octave(device, &audio, &ui, 4);
        if (ui.fm_open) begin_fm_note(device, &audio, &ui, &instrument, &fm, 12, 44100, 0);
        else begin_note(device, &audio, &ui, &instrument, 12, 44100, 0);
        assert(voices() == 1);
        set_keyboard_octave(device, &audio, &ui, 5);
        release_note(device, &audio, &ui, 12); /* Release the original physical key. */
        assert_keyboard_silent();
    }
    set_keyboard_octave(device, &audio, &ui, 4);
}

static void test_sister_prepared_power(void)
{
    static SisterWindow panel;
    static ExternalInputState external;
    reset_route(0);
    ts_sister_runtime_disable(&audio.sister);
    ts_sister_ui_model_init(&panel.model,&ui.config);
    ts_performance_recorder_init(&panel.performance_recorder);
    ts_input_ownership_init(&external.ownership);
    panel.model.parameters=audio.sister.parameters;
    ui.config.sister_buffer_seconds=1;
    toggle_fm_hold(device,&audio,&ui,&instrument);
    click_note(0,0);click_note(4,1);
    assert(ui.keyboard_hold && voices()==2);
#define ACTION(action,index,value) sister_apply_action(device,&audio,&ui,&instrument,&panel,NULL,&external, \
    (TsSisterUiHit){action,index,value},44100,2)
    ACTION(TS_SISTER_UI_ACTION_SOURCE_TILES,0,0);
    ACTION(TS_SISTER_UI_ACTION_SOURCE_FM,1,0);
    ACTION(TS_SISTER_UI_ACTION_SOURCE_EXT,2,0);
    ACTION(TS_SISTER_UI_ACTION_SOURCE_PREVIEW,3,0);
    ACTION(TS_SISTER_UI_ACTION_SOURCE_TAPEHEAD,4,0);
    assert(audio.sister.source_switches==31 && !external.ownership.requests);
    ACTION(TS_SISTER_UI_ACTION_MONITOR,0,0);
    ACTION(TS_SISTER_UI_ACTION_ROLL,0,0);
    ACTION(TS_SISTER_UI_ACTION_HOLD,0,0);
    assert(audio.sister.monitor_enabled && !audio.sister.rolling && audio.sister.held);
    ACTION(TS_SISTER_UI_ACTION_PARAMETER,TS_SISTER_UI_PARAM_H1_LEVEL,.7f);
    assert(fabsf(audio.sister.parameters.head1_level-.7f)<.001f);
    TsSisterParameters prepared=audio.sister.parameters;
    ACTION(TS_SISTER_UI_ACTION_POWER,0,0);
    assert(audio.sister.enabled && ui.keyboard_hold && voices()==2);
    assert(audio.sister.source_switches==31 && audio.sister.monitor_enabled);
    assert(!audio.sister.rolling && audio.sister.held);
    assert(audio.sister.parameters.head1_level==prepared.head1_level);
    assert(external.ownership.requests & TS_INPUT_CONSUMER_SISTER_EXT);
    assert(audio.notes.workbench_loop && audio.sister.performance.keyboard_loop);
    TsSisterRoutingSnapshot snapshot;
    assert(ts_sister_runtime_get_snapshot(&audio.sister,&snapshot));
    assert(snapshot.source_switches==31 && snapshot.monitor_enabled);
    /* Note ownership remains releasable after the power/routing handoff. */
    click_note(4,0);click_note(0,1);assert(!voices() && ui.keyboard_hold);
    ACTION(TS_SISTER_UI_ACTION_POWER,0,0);
    assert(!audio.sister.enabled && ui.keyboard_hold && !external.ownership.requests);
    assert(audio.sister.source_switches==31 && audio.sister.monitor_enabled && audio.sister.held);
#undef ACTION
}

#include "test_keyboard_power.inc"
#include "test_keyboard_sequence_controller.inc"
#include "test_master_eq_controller.inc"
#include "test_performance_polish.inc"
#include "test_router_controller.inc"
#include "test_insert_controller.inc"
#include "test_insert_devices.inc"

int main(void)
{
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER));
    SDL_AudioSpec spec = {0};
    spec.freq = 44100; spec.channels = 2; spec.format = AUDIO_F32SYS; spec.samples = 256;
    device = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    assert(device); ts_real_output = device;
    window = SDL_CreateWindow("Keyboard hold", 0, 0, 640, 400, 0);
    assert(window);
    ts_ui_init(&ui); ts_instrument_init(&instrument);
    ts_note_bank_init(&audio.notes); ts_performance_init(&audio.performance);
    ts_keyboard_sequence_init(&audio.keyboard_sequence);
    ts_performance_init(&audio.tile_launchers); ts_sister_runtime_init(&audio.sister);
    ts_capture_init(&audio.capture); audio.output_rate = 44100;
    ts_audio_mixer_init(&audio.mixer);audio.fm_output_gain=1;
    char error[160];
    assert(ts_sister_runtime_enable(&audio.sister, 44100, 2, 2, 1.0, error, sizeof(error)));
    assert(ts_instrument_create_basic(&instrument, TS_FM_WAVE_SINE, error, sizeof(error)));
    assert(ts_sample_clone(&fm, &instrument.current, error, sizeof(error)));
    test_chords();
    test_fm_release_without_preview();
    test_shifted_hold_release();
    test_shifted_hold_isolation();
    test_key_up_after_octave_change();
    test_route_change();
    test_trigger_identity();
    test_loop_transport();
    test_sister_prepared_power();
    test_keyboard_power_audio();
    test_keyboard_sequence_controller();
    test_performance_polish();
    test_router_controller();
    test_insert_controller();
    test_duplex_callback_unity();
    test_insert_devices();
    test_master_eq_controller();
    stop_all_force(device, &audio, &ui);
    ts_sample_free(&fm); ts_instrument_free(&instrument);
    ts_performance_free(&audio.performance); ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister); ts_capture_free(&audio.capture);
    SDL_DestroyWindow(window); SDL_CloseAudioDevice(device); ts_real_output = 0; SDL_Quit();
    puts("HOLD/Shift-click, Create, and final-note loop regression tests passed");
    return 0;
}
