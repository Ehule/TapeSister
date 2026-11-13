/* Presentation only: no transport, project, device or inactivity ownership. */
enum { TS_CREDITS_W = 1280, TS_CREDITS_H = 1024, TS_CREDITS_MS = 2000 };
static uint32_t event_window_id(const SDL_Event *event);
static const char *const credits_rows[][2] = {
    {"Ehule/Sasna Kra", "Concept & design"},
    {"FastTracker II", "Tracker"},
    {"8bitbubsy", "FT2 clone / Tapehead"},
    {"SunVox", "Modules & MetaModules"},
    {"CDP", "Composers Desktop Project"},
    {"ppooll", "Project & contributors"},
    {"Bahiamansa", "Kafka / failure_v2"},
    {"SOMA Laboratory", "Cosmos / ROAT / SimplexFM"},
    {"Namkhai Norbu", "Prism"},
    {"ChatGPT / OpenAI", "Development assistance"}
};
static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *artwork;
    Uint32 id;
} credits_window;

static void credits_text(SDL_Renderer *renderer, int x, int y,
    const char *value, Uint8 r, Uint8 g, Uint8 b, int scale)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    for (; *value; ++value, x += 6 * scale) {
        const char *glyph = ts_ui_glyph((char)toupper((unsigned char)*value));
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyph[row * 5 + col] == '1') {
                    SDL_Rect pixel = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
    }
}

static void credits_paint(SDL_Renderer *renderer, SDL_Texture *artwork)
{
    SDL_RenderSetLogicalSize(renderer, TS_CREDITS_W, TS_CREDITS_H);
    SDL_SetRenderDrawColor(renderer, 7, 4, 12, 255);
    SDL_RenderClear(renderer);
    if (artwork) {
        SDL_Rect destination = {0, 0, TS_CREDITS_W, TS_CREDITS_H};
        SDL_RenderCopy(renderer, artwork, NULL, &destination);
    } else {
        /* Missing/corrupt artwork must still leave readable acknowledgments. */
        SDL_Rect border = {20, 20, TS_CREDITS_W - 40, TS_CREDITS_H - 40};
        SDL_SetRenderDrawColor(renderer, 230, 151, 42, 255);
        SDL_RenderDrawRect(renderer, &border);
        credits_text(renderer, 478, 110, "TAPESISTER", 230, 151, 42, 5);
        credits_text(renderer, 512, 225, "CREDITS", 230, 151, 42, 5);
        for (size_t row = 0; row < sizeof(credits_rows) / sizeof(credits_rows[0]); ++row) {
            int y = 330 + (int)row * 44;
            credits_text(renderer, 95, y, credits_rows[row][0], 247, 237, 200, 3);
            credits_text(renderer, 600, y, credits_rows[row][1], 196, 137, 182, 3);
        }
        credits_text(renderer, 382, 905, "PRESS ANY KEY TO CLOSE", 230, 151, 42, 4);
    }
    SDL_RenderPresent(renderer);
}

static void credits_close(void)
{
    if (credits_window.artwork) SDL_DestroyTexture(credits_window.artwork);
    if (credits_window.renderer) SDL_DestroyRenderer(credits_window.renderer);
    if (credits_window.window) SDL_DestroyWindow(credits_window.window);
    memset(&credits_window, 0, sizeof(credits_window));
}

static int credits_show(SDL_Window *host)
{
    if (credits_window.window) {
        SDL_RaiseWindow(credits_window.window);
        return 1;
    }
    int display = host ? SDL_GetWindowDisplayIndex(host) : 0;
    if (display < 0) display = 0;
    credits_window.window = SDL_CreateWindow("TapeSister Credits - Shift+F12",
        SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        900, 720, TS_WORKSPACE_WINDOW_FLAGS);
    if (credits_window.window)
        credits_window.renderer = SDL_CreateRenderer(credits_window.window, -1, SDL_RENDERER_SOFTWARE);
    if (!credits_window.renderer) { credits_close(); return 0; }
    SDL_SetWindowMinimumSize(credits_window.window, 560, 448);
    SDL_SetWindowAlwaysOnTop(credits_window.window, SDL_TRUE);
    credits_window.id = SDL_GetWindowID(credits_window.window);
    int width = 0, height = 0;
    credits_window.artwork = load_presentation_texture(credits_window.renderer,
        "assets/tapesister_credits.png", &width, &height);
    if (credits_window.artwork) SDL_SetTextureScaleMode(credits_window.artwork, SDL_ScaleModeLinear);
    credits_paint(credits_window.renderer, credits_window.artwork);
    SDL_RaiseWindow(credits_window.window);
    return 1;
}

static int credits_event(const SDL_Event *event, SDL_Window *host)
{
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12 &&
        (event->key.keysym.mod & KMOD_SHIFT) &&
        !(event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
        if (!event->key.repeat) {
            if (credits_window.id && event->key.windowID == credits_window.id) credits_close();
            else (void)credits_show(host);
        }
        return 1;
    }
    if (!credits_window.id || event_window_id(event) != credits_window.id) return 0;
    if ((event->type == SDL_KEYDOWN && !event->key.repeat) ||
        event->type == SDL_MOUSEBUTTONDOWN ||
        (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE)) {
        credits_close();
    } else if (event->type == SDL_WINDOWEVENT &&
        (event->window.event == SDL_WINDOWEVENT_EXPOSED ||
         event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        credits_paint(credits_window.renderer, credits_window.artwork);
    } else if (event->type == SDL_DROPFILE || event->type == SDL_DROPTEXT) {
        SDL_free(event->drop.file);
    }
    return 1;
}

static int credits_dismisses(const SDL_Event *event)
{
    return event->type == SDL_QUIT ||
        (event->type == SDL_KEYDOWN && !event->key.repeat) ||
        event->type == SDL_MOUSEBUTTONDOWN ||
        (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE);
}

/* Called only after a confirmed exit has completed saves and stopped devices.
   The ordinary application loop owns the on-demand screen and keeps running. */
static void credits_farewell(SDL_Window *window, SDL_Renderer *renderer, Uint32 duration)
{
    if (!window || !renderer || !duration) return;
    SDL_RestoreWindow(window);
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    int width = 0, height = 0;
    SDL_Texture *artwork = load_presentation_texture(renderer,
        "assets/tapesister_credits.png", &width, &height);
    if (artwork) SDL_SetTextureScaleMode(artwork, SDL_ScaleModeLinear);
    Uint32 started = SDL_GetTicks();
    int showing = 1;
    credits_paint(renderer, artwork);
    while (showing && SDL_GetTicks() - started < duration) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 16)) {
            if (credits_dismisses(&event)) showing = 0;
            else if (event.type == SDL_WINDOWEVENT)
                credits_paint(renderer, artwork);
            else if (event.type == SDL_DROPFILE || event.type == SDL_DROPTEXT)
                SDL_free(event.drop.file);
        }
    }
    if (artwork) SDL_DestroyTexture(artwork);
}
