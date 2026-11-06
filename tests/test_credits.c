#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void config_roundtrip(void)
{
    const char *path = "test-credits-config.ini";
    TsConfig saved, loaded;
    char error[256];
    ts_config_init(&saved);
    assert(saved.exit_credits == 1);
    saved.exit_credits = 0;
    assert(ts_config_save(&saved, path, error, sizeof(error)));
    assert(ts_config_load(&loaded, path, error, sizeof(error)));
    assert(loaded.exit_credits == 0);
    FILE *file = fopen(path, "wb"); assert(file);
    fputs("[Startup]\nstartup_welcome_autoplay=0\n", file); fclose(file);
    assert(ts_config_load(&loaded, path, error, sizeof(error)));
    assert(loaded.exit_credits == 1 && !loaded.startup_welcome_autoplay);
    file = fopen(path, "wb"); assert(file);
    fputs("exit_credits=maybe\n", file); fclose(file);
    assert(!ts_config_load(&loaded, path, error, sizeof(error)));
    assert(loaded.exit_credits == 1);
    remove(path);
}

static SDL_Event key(Uint32 id, SDL_Keycode sym, int mod)
{
    SDL_Event event; SDL_zero(event);
    event.type = SDL_KEYDOWN;
    event.key.windowID = id;
    event.key.keysym.sym = sym;
    event.key.keysym.mod = (Uint16)mod;
    return event;
}

int main(int argc, char **argv)
{
    config_roundtrip();
    SDL_SetMainReady();
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER));
    SDL_Window *host = SDL_CreateWindow("Credits host", 0, 0, 1280, 1024, 0);
    assert(host);
    SDL_Renderer *renderer = SDL_CreateRenderer(host, -1, SDL_RENDERER_SOFTWARE);
    assert(renderer);
    Uint32 host_id = SDL_GetWindowID(host);
    SDL_Event event = key(host_id, SDLK_F12, KMOD_SHIFT);
    assert(credits_event(&event, host));
    assert(credits_window.id && credits_window.artwork);
    Uint32 credits_id = credits_window.id;
    event.key.repeat = 1;
    assert(credits_event(&event, host) && credits_window.id == credits_id);
    /* Existing F12 and Ctrl+Shift+F12 retain Audio Health/session-marker ownership. */
    event = key(host_id, SDLK_F12, 0); assert(!credits_event(&event, host));
    event.key.keysym.mod = KMOD_CTRL | KMOD_SHIFT; assert(!credits_event(&event, host));
    event = key(host_id, SDLK_SPACE, 0); assert(!credits_event(&event, host));
    /* Resizing repaints the same art; no texture reload or session action. */
    SDL_SetWindowSize(credits_window.window, 1000, 600);
    SDL_zero(event); event.type = SDL_WINDOWEVENT;
    event.window.windowID = credits_id; event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    SDL_Texture *art = credits_window.artwork;
    assert(credits_event(&event, host) && credits_window.artwork == art);
    SDL_SetWindowSize(credits_window.window, 1280, 1024);
    credits_paint(credits_window.renderer, credits_window.artwork);
    if (argc > 1) {
        SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, 1280, 1024, 32, SDL_PIXELFORMAT_ARGB8888);
        assert(image && !SDL_RenderReadPixels(credits_window.renderer, NULL,
            image->format->format, image->pixels, image->pitch));
        assert(!SDL_SaveBMP(image, argv[1])); SDL_FreeSurface(image);
    }
    event = key(credits_id, SDLK_SPACE, 0);
    assert(credits_event(&event, host) && !credits_window.id);
    assert(credits_show(host));
    SDL_zero(event); event.type = SDL_MOUSEBUTTONDOWN; event.button.windowID = credits_window.id;
    assert(credits_event(&event, host) && !credits_window.id);
    assert(credits_show(host));
    SDL_zero(event); event.type = SDL_WINDOWEVENT; event.window.windowID = credits_window.id;
    event.window.event = SDL_WINDOWEVENT_CLOSE;
    assert(credits_event(&event, host) && !credits_window.id);
    /* A readable native fallback survives missing artwork. */
    credits_paint(renderer, NULL);
    Uint32 started = SDL_GetTicks();
    credits_farewell(host, renderer, 25);
    assert(SDL_GetTicks() - started < 2000);
    event = key(host_id, SDLK_RETURN, 0); assert(SDL_PushEvent(&event) == 1);
    started = SDL_GetTicks(); credits_farewell(host, renderer, TS_CREDITS_MS);
    assert(SDL_GetTicks() - started < 1500);
    SDL_zero(event); event.type = SDL_QUIT; assert(credits_dismisses(&event));
    event = key(host_id, SDLK_ESCAPE, 0); event.key.repeat = 1;
    assert(!credits_dismisses(&event));
    /* Cancelling Exit and confirming New never open a farewell. */
    static TsUiState ui; ts_ui_init(&ui);
    ui.exit_confirm_open = 1; ui.exit_choice = 1;
    assert(!exit_confirmation_activate(&ui) && !credits_window.id);
    ui.exit_confirm_open = 1; ui.exit_for_new = 1; ui.exit_choice = 1;
    assert(!exit_confirmation_activate(&ui) && ui.new_project_pending && !credits_window.id);
    ui.exit_for_new = 0; ui.exit_confirm_open = 1; ui.exit_choice = 0;
    assert(exit_confirmation_activate(&ui));
    /* Exercise the real Control Center hit target, not a second opening path. */
    static AudioState audio;
    static TsSamplePages pages;
    static TsInstrument bank;
    ts_sister_runtime_init(&audio.sister);
    audio.output_rate = 48000;
    ui.exit_confirm_open = ui.new_project_pending = 0;
    performance_show(&ui, &audio);
    assert(performance_transport.window);
    SDL_zero(event); event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = performance_transport.id;
    event.button.button = SDL_BUTTON_LEFT; event.button.x = 600; event.button.y = 618;
    assert(performance_event(&event, host, 0, &audio, &ui, &pages, &bank, NULL, 48000));
    assert(credits_window.id);
    credits_close();
    if (argc > 2) {
        performance_update(0, &audio);
        SDL_Surface *image = SDL_CreateRGBSurfaceFrom(performance_transport.pixels,
            PERF_W, PERF_H, 32, PERF_W * 4, 0xff0000, 0xff00, 0xff, 0);
        assert(image && !SDL_SaveBMP(image, argv[2])); SDL_FreeSurface(image);
    }
    performance_close(); ts_sister_runtime_free(&audio.sister);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(host); SDL_Quit();
    puts("Credits artwork, controller, fallback, farewell and config checks passed.");
    return 0;
}
