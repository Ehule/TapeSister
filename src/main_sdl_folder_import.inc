typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t cancel, done;
    TsSampleFolderImport *load;
    char directory[TS_BROWSER_PATH_MAX], error[160];
    int ok;
} FolderImportWorker;

static FolderImportWorker *folder_import_worker;

static int folder_import_cancelled(void *userdata)
{
    return SDL_AtomicGet(&((FolderImportWorker *)userdata)->cancel) != 0;
}

static int folder_import_main(void *userdata)
{
    FolderImportWorker *worker = userdata;
    worker->ok = ts_sample_folder_decode(worker->load, worker->directory,
        folder_import_cancelled, worker, worker->error, sizeof(worker->error));
    SDL_AtomicSet(&worker->done, 1);
    return 0;
}

static void discard_folder_import(void)
{
    FolderImportWorker *worker = folder_import_worker;
    if (!worker) return;
    SDL_AtomicSet(&worker->cancel, 1);
    SDL_WaitThread(worker->thread, NULL);
    ts_sample_folder_dispose(worker->load);
    free(worker);
    folder_import_worker = NULL;
}

static int begin_folder_import(SDL_AudioDeviceID device, AudioState *audio,
    TsUiState *ui, TsInstrument *instrument, TsSamplePages *pages,
    ImportController *import, BrowserAudition *audition, const char *directory)
{
    if (folder_import_worker || ui->file_busy) return 0;
    if (!directory || strlen(directory) >= TS_BROWSER_PATH_MAX) {
        snprintf(ui->status, sizeof(ui->status), "FOLDER PATH IS TOO LONG - NO TILES ADDED");
        return 0;
    }
    if (audio->capture.state == TS_CAPTURE_COMPLETED)
        finalize_capture(device, audio, ui, instrument);
    if (audio->capture.state != TS_CAPTURE_IDLE || pages->mosaic_bank) {
        snprintf(ui->status, sizeof(ui->status),
            "FINISH CAPTURE / MOSAIC EDIT BEFORE IMPORTING A FOLDER");
        return 0;
    }
    FolderImportWorker *worker = calloc(1, sizeof(*worker));
    if (!worker) {
        snprintf(ui->status, sizeof(ui->status), "FOLDER IMPORT: OUT OF MEMORY");
        return 0;
    }
    snprintf(worker->directory, sizeof(worker->directory), "%s", directory);
    worker->load = ts_sample_folder_prepare(pages, instrument, worker->error, sizeof(worker->error));
    if (!worker->load) {
        snprintf(ui->status, sizeof(ui->status), "FOLDER IMPORT: %.140s", worker->error);
        free(worker); return 0;
    }
    browser_audition_stop(device, audio, ui, audition, 1);
    close_import_preview(device, audio, ui, import);
    worker->thread = SDL_CreateThread(folder_import_main, "TapeSister Folder", worker);
    if (!worker->thread) {
        ts_sample_folder_dispose(worker->load); free(worker);
        snprintf(ui->status, sizeof(ui->status), "COULD NOT START FOLDER DECODER");
        return 0;
    }
    folder_import_worker = worker;
    ui->file_busy = 1;
    snprintf(ui->file_busy_label, sizeof(ui->file_busy_label), "IMPORTING");
    snprintf(ui->status, sizeof(ui->status), "IMPORTING AUDIO FOLDER - ESC / CANCEL STOPS; PLAYBACK CONTINUES");
    return 1;
}

static void poll_folder_import(SDL_AudioDeviceID device, AudioState *audio,
    TsUiState *ui, TsInstrument *instrument, TsSamplePages *pages)
{
    FolderImportWorker *worker = folder_import_worker;
    if (!worker || !SDL_AtomicGet(&worker->done)) return;
    SDL_WaitThread(worker->thread, NULL);
    int ok = worker->ok && !SDL_AtomicGet(&worker->cancel);
    if (ok) {
        lock_edit(device, audio);
        ok = ts_sample_folder_publish(worker->load, pages, instrument,
            worker->error, sizeof(worker->error));
        if (device) SDL_UnlockAudioDevice(device);
    }
    ui->file_busy = 0;
    ui->file_busy_label[0] = '\0';
    if (ok) {
        TsSampleFolderSummary result = ts_sample_folder_summary(worker->load);
        ui->sample_page_count = (int)pages->page_count;
        ui->load_bank_slot = -1;
        ts_ui_reset_parent_view(ui, instrument->parent.frames);
        ts_browser_close(&ui->browser);
        SDL_StopTextInput();
        snprintf(ui->status, sizeof(ui->status),
            "IMPORTED %zu SOUNDS / SAMPLE PAGES %zu-%zu; %zu OTHER ENTRIES, %zu UNREADABLE SKIPPED",
            result.imported, result.first_page, result.last_page, result.skipped, result.failed);
    } else {
        snprintf(ui->status, sizeof(ui->status), "%s",
            SDL_AtomicGet(&worker->cancel) ? "FOLDER IMPORT CANCELLED - NO TILES ADDED" : worker->error);
        snprintf(ui->browser.message, sizeof(ui->browser.message), "NO TILES ADDED - SEE STATUS");
    }
    ts_sample_folder_dispose(worker->load);
    free(worker);
    folder_import_worker = NULL;
}

/* Route these controls before the ordinary browser. While decoding, leave
   window/quit and note-release events available; project mutations wait. */
static int folder_import_event(const SDL_Event *event, SDL_Window *window,
    SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
    TsInstrument *instrument, TsSamplePages *pages, ImportController *import,
    BrowserAudition *audition)
{
    Uint32 id = SDL_GetWindowID(window);
    int key = event->type == SDL_KEYDOWN && event->key.windowID == id;
    int click = event->type == SDL_MOUSEBUTTONDOWN && event->button.windowID == id;
    int x = 0, y = 0;
    if (click) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        if (w > 0 && h > 0) { x = event->button.x * TS_UI_WIDTH / w; y = event->button.y * TS_UI_HEIGHT / h; }
    }
    int primary = click && event->button.button == SDL_BUTTON_LEFT &&
        x >= 224 && x < 344 && y >= 326 && y < 349;
    int cancel_click = click && event->button.button == SDL_BUTTON_LEFT &&
        x >= 349 && x < 433 && y >= 326 && y < 349;
    int enter = key && !event->key.repeat &&
        (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER);
    int escape = key && event->key.keysym.sym == SDLK_ESCAPE;
    if (folder_import_worker) {
        if (escape || cancel_click || event->type == SDL_QUIT ||
            (event->type == SDL_WINDOWEVENT && event->window.windowID == id &&
             event->window.event == SDL_WINDOWEVENT_CLOSE)) {
            SDL_AtomicSet(&folder_import_worker->cancel, 1);
            snprintf(ui->status, sizeof(ui->status), "CANCELLING FOLDER IMPORT - NO TILES WILL BE ADDED");
        }
        if (event->type == SDL_DROPFILE) { SDL_free(event->drop.file); return 1; }
        return key || click ||
            (event->type == SDL_MOUSEWHEEL && event->wheel.windowID == id) ||
            (event->type == SDL_TEXTINPUT && event->text.windowID == id);
    }
    if (ui->file_busy || ui->exit_confirm_open || ui->project_overwrite_confirm_open) return 0;
    if (ui->midi_learn_active) return 0;
    if (ui->browser.mode == TS_BROWSER_IMPORT_AUDIO_FOLDER) {
        if (escape || cancel_click || (enter && ui->browser.action_focus == 3)) {
            browser_open(ui, TS_BROWSER_LOAD_WAV); return 1;
        }
        if (primary || (enter && (ui->browser.action_focus == 2 ||
            (ui->browser.action_focus == -1 && ui->browser.selected < 0)))) {
            (void)begin_folder_import(device, audio, ui, instrument, pages, import, audition, ui->browser.directory);
            return 1;
        }
    }
    int folder_button = click && event->button.button == SDL_BUTTON_LEFT &&
        ui->browser.mode == TS_BROWSER_LOAD_WAV && x >= 262 && x < 388 && y >= 39 && y < 62;
    int shortcut = key && !event->key.repeat && event->key.keysym.sym == SDLK_o &&
        (event->key.keysym.mod & KMOD_CTRL) && (event->key.keysym.mod & KMOD_SHIFT) &&
        !(event->key.keysym.mod & (KMOD_ALT | KMOD_GUI)) &&
        (ui->browser.mode == TS_BROWSER_LOAD_WAV || !ui_dialog_open(ui));
    if (folder_button || shortcut ||
        (enter && ui->browser.mode == TS_BROWSER_LOAD_WAV && ui->browser.action_focus == 1)) {
        browser_audition_stop(device, audio, ui, audition, 1);
        close_import_preview(device, audio, ui, import);
        browser_open(ui, TS_BROWSER_IMPORT_AUDIO_FOLDER);
        return 1;
    }
    if (event->type == SDL_DROPFILE && event->drop.windowID == id &&
        !ui->mosaic_open && !ui_dialog_open(ui)) {
        struct stat info;
        if (!stat(event->drop.file, &info) && S_ISDIR(info.st_mode)) {
            (void)begin_folder_import(device, audio, ui, instrument, pages, import, audition, event->drop.file);
            SDL_free(event->drop.file); return 1;
        }
    }
    return 0;
}
