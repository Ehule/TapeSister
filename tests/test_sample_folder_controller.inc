static void test_folder_browser(SDL_Window *window, SDL_AudioDeviceID device,
    AudioState *audio, const TsSample *source)
{
    static TsInstrument instrument;
    static TsUiState ui;
    TsSamplePages pages;
    ImportController import;
    BrowserAudition audition;
    char error[160], directory[TS_BROWSER_PATH_MAX], path[1200];
    ts_ui_init(&ui); ts_instrument_init(&instrument);
    import_controller_init(&import); memset(&audition, 0, sizeof(audition));
    import_controller_init(&audition.loader);
    assert(ts_sample_pages_init(&pages, error, sizeof(error)));
    assert(ts_instrument_import_sample(&instrument, source, 0, 0, 0,
        TS_LOOP_FORWARD, error, sizeof(error)));
    const float *original = instrument.current.data;
    uint64_t hash = ts_sample_hash(&instrument.current);
    size_t base_length = strlen(ui.browser.directory);
    assert(base_length + sizeof("/test-folder-controller") <= sizeof(directory));
    memcpy(directory, ui.browser.directory, base_length);
    memcpy(directory + base_length, "/test-folder-controller", sizeof("/test-folder-controller"));
#ifdef _WIN32
    assert(!_mkdir(directory));
#else
    assert(!mkdir(directory, 0775));
#endif
    for (int i = 0; i < 18; ++i) {
        snprintf(path, sizeof(path), "%s/%02d.wav", directory, i);
        assert(ts_sample_save_wav32f(source, path, error, sizeof(error)));
    }
    snprintf(ui.browser.directory, sizeof(ui.browser.directory), "%s", directory);
    browser_open(&ui, TS_BROWSER_LOAD_WAV);
    SDL_Event event = {0};
    event.type = SDL_MOUSEBUTTONDOWN; event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT; event.button.x = 280; event.button.y = 48;
#define FOLDER_EVENT() folder_import_event(&event,window,device,audio,&ui,&instrument,&pages,&import,&audition)
    assert(FOLDER_EVENT());
    assert(ui.browser.mode == TS_BROWSER_IMPORT_AUDIO_FOLDER && !ui.file_busy);
    /* Directory mode filters files but preserves navigation and the selected folder. */
    assert(!strcmp(ui.browser.directory, directory) && ui.browser.entry_count == 0);
    assert(!ts_browser_mode_selects_config(ui.browser.mode));
    static TsFramebuffer fb;
    ts_ui_render(&fb, &ui, &instrument);
    const char *shots = getenv("TS_TEST_FOLDER_SCREENSHOTS");
    if (shots) {
        snprintf(path, sizeof(path), "%s/folder-picker.ppm", shots);
        assert(ts_ui_write_ppm(&fb, path));
    }
    event.button.x = 380; event.button.y = 335;
    assert(FOLDER_EVENT()); assert(ui.browser.mode == TS_BROWSER_LOAD_WAV);
    browser_cycle_focus(&ui.browser, 1);
    browser_cycle_focus(&ui.browser, 1);
    assert(ui.browser.action_focus == 1); /* Folder button is keyboard accessible. */
    event.type = SDL_KEYDOWN; event.key.windowID = SDL_GetWindowID(window);
    event.key.keysym.sym = SDLK_RETURN;
    assert(FOLDER_EVENT()); assert(ui.browser.mode == TS_BROWSER_IMPORT_AUDIO_FOLDER);
    begin_audition(device, audio, &ui, &instrument, TS_AUDITION_WORKBENCH_LOOP, 1, 44100);
    assert(audio->playing && audio->sample == &instrument.current);
    event.type = SDL_MOUSEBUTTONDOWN; event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT; event.button.x = 270; event.button.y = 335;
    assert(FOLDER_EVENT()); assert(folder_import_worker && ui.file_busy);
    Uint32 until = SDL_GetTicks() + 10000;
    do {
        float block[512];
        double before = audio->position;
        audio_callback(audio, (Uint8 *)block, sizeof(block));
        assert(audio->playing && audio->position != before && instrument.current.data == original);
        poll_folder_import(device, audio, &ui, &instrument, &pages);
        SDL_Delay(1);
    } while (folder_import_worker && SDL_GetTicks() < until);
    assert(!folder_import_worker && !ui.file_busy && ui.browser.mode == TS_BROWSER_CLOSED);
    assert(pages.page_count == 2 && ui.sample_page_count == 2);
    assert(audio->playing && audio->sample == &instrument.current && instrument.current.data == original);
    assert(ts_sample_hash(&instrument.current) == hash && strstr(ui.status, "18 SOUNDS"));
    stop_all(device, audio, &ui);

    /* The shortcut, pending-cancel and post-cancel browser all use real handlers. */
    event.type = SDL_KEYDOWN; event.key.windowID = SDL_GetWindowID(window);
    event.key.keysym.sym = SDLK_o; event.key.keysym.mod = KMOD_CTRL | KMOD_SHIFT;
    assert(FOLDER_EVENT()); assert(ui.browser.mode == TS_BROWSER_IMPORT_AUDIO_FOLDER);
    event.type = SDL_MOUSEBUTTONDOWN; event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT; event.button.x = 270; event.button.y = 335;
    assert(FOLDER_EVENT()); assert(folder_import_worker);
    event.type = SDL_KEYDOWN; event.key.windowID = SDL_GetWindowID(window);
    event.key.keysym.sym = SDLK_ESCAPE; event.key.keysym.mod = KMOD_NONE;
    assert(FOLDER_EVENT());
    until = SDL_GetTicks() + 10000;
    do {
        poll_folder_import(device, audio, &ui, &instrument, &pages); SDL_Delay(1);
    } while (folder_import_worker && SDL_GetTicks() < until);
    assert(!folder_import_worker && !ui.file_busy && pages.page_count == 2);
    assert(!pages.pages[1]->bank[3].occupied);
    assert(strstr(ui.status, "CANCELLED"));
    assert(FOLDER_EVENT()); assert(ui.browser.mode == TS_BROWSER_LOAD_WAV);
    if (shots) {
        ts_ui_render(&fb, &ui, &instrument);
        snprintf(path, sizeof(path), "%s/folder-load-button.ppm", shots);
        assert(ts_ui_write_ppm(&fb, path));
    }
    close_import_preview(device, audio, &ui, &import);
    ts_audio_import_free(&import.decoded);
    ts_sample_pages_free(&pages); ts_instrument_free(&instrument);
    for (int i = 0; i < 18; ++i) {
        snprintf(path, sizeof(path), "%s/%02d.wav", directory, i); assert(!remove(path));
    }
#ifdef _WIN32
    assert(!_rmdir(directory));
#else
    assert(!rmdir(directory));
#endif
#undef FOLDER_EVENT
}
