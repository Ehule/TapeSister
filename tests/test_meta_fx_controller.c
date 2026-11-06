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
static TsInstrument bank;
static TsSamplePages pages;
static SisterWindow sister;
static void click(int x, int y, Uint8 button) {
    int width, height;
    SDL_GetWindowSize(meta_fx_window.window, &width, &height);
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.windowID = meta_fx_window.id;
    e.button.x = x * width / 640;
    e.button.y = y * height / 400;
    e.button.button = button;
    assert(meta_fx_event(&e, 0, &audio, &ui, &sister, &pages, &bank));
    e.type = SDL_MOUSEBUTTONUP;
    if (meta_fx_window.visible)
        assert(meta_fx_event(&e, 0, &audio, &ui, &sister, &pages, &bank));
}
static void drag(int x, int y, int dx) {
    int width, height;
    SDL_GetWindowSize(meta_fx_window.window, &width, &height);
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.windowID = meta_fx_window.id;
    e.button.x = x * width / 640;
    e.button.y = y * height / 400;
    e.button.button = SDL_BUTTON_LEFT;
    assert(meta_fx_event(&e, 0, &audio, &ui, &sister, &pages, &bank));
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = meta_fx_window.id;
    e.motion.x = (x + dx) * width / 640;
    e.motion.y = y * height / 400;
    e.motion.state = SDL_BUTTON_LMASK;
    assert(meta_fx_event(&e, 0, &audio, &ui, &sister, &pages, &bank));
    e.type = SDL_MOUSEBUTTONUP;
    e.button.windowID = meta_fx_window.id;
    assert(meta_fx_event(&e, 0, &audio, &ui, &sister, &pages, &bank));
}
static void shot(const char *path) {
    meta_fx_window.painted = SDL_GetTicks() - 40;
    meta_fx_paint(&ui);
    SDL_Surface *surface =
        SDL_CreateRGBSurfaceFrom(meta_fx_window.fb.pixels, 640, 400, 32, 2560, 0xff0000, 0xff00, 0xff, 0);
    assert(surface && !SDL_SaveBMP(surface, path));
    SDL_FreeSurface(surface);
}
int main(int argc, char **argv) {
    SDL_SetMainReady();
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER));
    ts_ui_init(&ui);
    ts_instrument_init(&bank);
    char error[256];
    assert(ts_sample_pages_init(&pages, error, sizeof(error)));
    ts_sister_runtime_init(&audio.sister);
    audio.output_rate = 48000;
    audio.output_device_channels = 2;
    ts_sister_ui_model_init(&sister.model, &ui.config);
    TsMatrixControls c = audio.sister.matrix.controls;
    c.enabled = 1;
    int slot = ts_matrix_effect_add(&c, TS_MATRIX_FX_META);
    assert(slot >= 0);
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects, &c, 48000));
    assert(ts_matrix_set(&audio.sister.matrix, &c, 48000));
    matrix_window.controls = ui.config.matrix = c;
    meta_fx_open(c.effect[slot].id);
    assert(meta_fx_window.visible);
    TsMetaControls *rack = &matrix_window.controls.effect[slot].meta;
    char target[80];
    snprintf(target, sizeof(target), "matrix.meta.%u.0", c.effect[slot].id);
    midi_learn_set_active(&ui, &sister, 1);
    click(50, 44, SDL_BUTTON_LEFT);
    assert(!strcmp(ui.midi_learn_pending, target));
    midi_learn_set_active(&ui, &sister, 0);
    assert(ts_midi_target_is_continuous(target));
    assert(meta_fx_matrix_midi(0, &audio, &ui, target, .37f));
    assert(fabsf(midi_target_current_value(target, &ui, &sister) - .37f) < 1e-6);
    float before = rack->macro[0];
    drag(70, 44, 50);
    assert(rack->macro[0] > before &&
           audio.sister.matrix.controls.effect[slot].meta.macro[0] == rack->macro[0]);
    SDL_Event key;
    SDL_zero(key);
    key.type = SDL_KEYDOWN;
    key.key.windowID = meta_fx_window.id;
    key.key.keysym.sym = SDLK_q;
    assert(!meta_fx_event(&key, 0, &audio, &ui, &sister, &pages, &bank));
    key.type = SDL_KEYUP;
    assert(!meta_fx_event(&key, 0, &audio, &ui, &sister, &pages, &bank));
    float changed = rack->macro[0];
    key.type = SDL_KEYDOWN;
    key.key.keysym.sym = SDLK_z;
    key.key.keysym.mod = KMOD_CTRL;
    assert(meta_fx_event(&key, 0, &audio, &ui, &sister, &pages, &bank));
    assert(rack->macro[0] == before);
    key.key.keysym.sym = SDLK_y;
    assert(meta_fx_event(&key, 0, &audio, &ui, &sister, &pages, &bank));
    assert(rack->macro[0] == changed);
    click(32, 280, SDL_BUTTON_LEFT);
    click(100, 280, SDL_BUTTON_LEFT);
    assert(rack->captured == 3);
    drag(180, 280, 40);
    assert(rack->scene_mode);
    click(510, 16, SDL_BUTTON_LEFT);
    assert(rack->preset == 1 && !strcmp(rack->name, "PALE CLOUD"));
    click(510, 16, SDL_BUTTON_LEFT);
    assert(rack->preset == 2);
    if (argc > 1)
        shot(argv[1]);
    click(120, 312, SDL_BUTTON_LEFT);
    assert(meta_fx_window.maps);
    int count = rack->map_count;
    click(200, 312, SDL_BUTTON_LEFT);
    assert(rack->map_count == count + 1);
    meta_fx_window.page = 0;
    float low = rack->map[0].low;
    drag(430, 90, 30);
    assert(rack->map[0].low > low);
    /* Cycling skips unused C on two-parameter pedals and targets already
       owned by another macro, so Mix and Output remain reachable. */
    for (int i = 0; i < rack->map_count; ++i)
        if (rack->stage[rack->map[i].stage].type == TS_SISTER_FX_DELAY && rack->map[i].field == 1) {
            int f = meta_fx_next_target(rack, i, rack->map[i].stage, 1, 1);
            assert(f == 4);
            break;
        }
    if (argc > 2)
        shot(argv[2]);
    snprintf(meta_fx_window.path, sizeof(meta_fx_window.path), "native-meta-test.tsmfx");
    click(600, 343, SDL_BUTTON_LEFT);
    TsMetaControls saved = *rack;
    ts_meta_preset(rack, 8);
    click(530, 343, SDL_BUTTON_LEFT);
    assert(!memcmp(rack, &saved, sizeof(saved)));
    remove(meta_fx_window.path);
    click(40, 312, SDL_BUTTON_LEFT);
    click(78, 90, SDL_BUTTON_LEFT);
    assert(rack->stage[0].type != saved.stage[0].type);
    const int sizes[][2] = {{960, 600}, {1280, 720}, {800, 900}};
    for (int i = 0; i < 3; ++i) {
        SDL_SetWindowSize(meta_fx_window.window, sizes[i][0], sizes[i][1]);
        drag(540, 280, -10);
        click(590, 16, SDL_BUTTON_LEFT);
        assert(!meta_fx_window.visible);
        meta_fx_open(c.effect[slot].id);
    }
    uint32_t id = matrix_window.controls.effect[slot].id;
    meta_fx_hide();
    ts_matrix_effect_remove(&matrix_window.controls, slot);
    meta_fx_open(id);
    assert(!meta_fx_resolve(&ui) && !meta_fx_window.visible);
    meta_fx_close();
    matrix_history_clear();
    ts_sister_runtime_disable(&audio.sister);
    ts_sample_pages_free(&pages);
    ts_instrument_free(&bank);
    SDL_Quit();
    puts("Meta FX native macros, states, presets, stage edits, mappings, files, resizing and stale IDs "
         "passed.");
    return 0;
}
