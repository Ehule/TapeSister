/* Included by ts_sample_pages.c; shares page allocation and ownership helpers. */
struct TsSampleFolderImport {
    TsInstrument *staged[TS_PROJECT_PAGE_LIMIT];
    const TsInstrument *original[TS_PROJECT_PAGE_LIMIT];
    unsigned empty[TS_PROJECT_PAGE_LIMIT];
    int selected[TS_PROJECT_PAGE_LIMIT];
    TsInstrument **registry;
    size_t original_count, count, active_page;
    int active_live, started, ready, published;
    TsSampleFolderSummary summary;
};

TsSampleFolderSummary ts_sample_folder_summary(const TsSampleFolderImport *load)
{
    TsSampleFolderSummary empty = {0};
    return load ? load->summary : empty;
}

void ts_sample_folder_dispose(TsSampleFolderImport *load)
{
    if (!load) return;
    for (size_t p = 0; p < load->count; ++p) if (load->staged[p]) {
        ts_instrument_free(load->staged[p]);
        free(load->staged[p]);
    }
    free(load->registry);
    free(load);
}

TsSampleFolderImport *ts_sample_folder_prepare(const TsSamplePages *pages,
    const TsInstrument *active, char *error, size_t size)
{
    if (!pages || !pages->pages || !pages->page_count ||
        pages->page_count > TS_PROJECT_PAGE_LIMIT ||
        pages->active_page >= pages->page_count || pages->mosaic_bank ||
        (pages->active_live && !active)) {
        pages_error(error, size, "Return to the Sample bank before importing a folder");
        return NULL;
    }
    TsSampleFolderImport *load = calloc(1, sizeof(*load));
    if (!load) goto memory;
    load->registry = calloc(TS_PROJECT_PAGE_LIMIT, sizeof(*load->registry));
    if (!load->registry) { free(load); goto memory; }
    load->count = load->original_count = pages->page_count;
    load->active_page = pages->active_page;
    load->active_live = pages->active_live;
    memcpy(load->registry, pages->pages, pages->page_count * sizeof(*pages->pages));
    for (size_t p = 0; p < pages->page_count; ++p) {
        const TsInstrument *bank = ts_sample_pages_page(pages, active, p);
        if (!bank || bank->selected_slot < 0 || bank->selected_slot >= TS_BANK_SLOT_COUNT) {
            ts_sample_folder_dispose(load);
            pages_error(error, size, "Invalid Sample page selection");
            return NULL;
        }
        load->original[p] = bank;
        load->selected[p] = bank->selected_slot;
        for (int s = 0; s < TS_BANK_SLOT_COUNT; ++s)
            if (!bank->bank[s].occupied && !bank->bank[s].locked)
                load->empty[p] |= 1u << s;
    }
    pages_error(error, size, "");
    return load;
memory:
    pages_error(error, size, "Out of memory preparing folder import");
    return NULL;
}

static int folder_audio_name(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    char lower[8];
    size_t n = strlen(ext);
    if (n >= sizeof(lower)) return 0;
    for (size_t i = 0; i <= n; ++i) lower[i] = (char)tolower((unsigned char)ext[i]);
    return !strcmp(lower, ".wav") || !strcmp(lower, ".flac") ||
           !strcmp(lower, ".mp3") || !strcmp(lower, ".ogg");
}

static int folder_compare_names(const void *a, const void *b)
{
    const unsigned char *left = *(const unsigned char *const *)a;
    const unsigned char *right = *(const unsigned char *const *)b;
    while (*left && *right && tolower(*left) == tolower(*right)) { ++left; ++right; }
    int difference = tolower(*left) - tolower(*right);
    return difference ? difference : strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int folder_join(char *out, size_t n, const char *directory, const char *name)
{
    size_t length = strlen(directory);
    int separator = length && directory[length-1] == '/';
#ifdef _WIN32
    separator = separator || (length && directory[length-1] == '\\');
#endif
    int written = snprintf(out, n, "%s%s%s", directory,
        separator ? "" : "/", name);
#ifdef _WIN32
    /* Match the native browser's drive-root and UNC search paths. */
    for (char *at = out; *at; ++at) if (*at == '/') *at = '\\';
#endif
    return written >= 0 && (size_t)written < n;
}

static int folder_add_name(char ***names, size_t *count, size_t *capacity, const char *name)
{
    if (*count == *capacity) {
        size_t grown = *capacity ? *capacity * 2 : 32;
        if (grown > SIZE_MAX / sizeof(**names)) return 0;
        char **next = realloc(*names, grown * sizeof(**names));
        if (!next) return 0;
        *names = next; *capacity = grown;
    }
    char *copy = malloc(strlen(name) + 1);
    if (!copy) return 0;
    strcpy(copy, name);
    (*names)[(*count)++] = copy;
    return 1;
}

int ts_sample_folder_decode(TsSampleFolderImport *load, const char *directory,
    TsAudioImportCancelCheck cancel, void *userdata, char *error, size_t size)
{
    char **names = NULL, path[TS_PROJECT_PATH_MAX];
    size_t count = 0, capacity = 0, page = 0;
    int ok = 0;
    if (!load || load->started || !directory || !directory[0]) {
        pages_error(error, size, "Invalid folder import"); return 0;
    }
    load->started = 1;
#ifdef _WIN32
    char pattern[TS_PROJECT_PATH_MAX];
    if (!folder_join(pattern, sizeof(pattern), directory, "*")) goto path_error;
    WIN32_FIND_DATAA item;
    HANDLE search = FindFirstFileA(pattern, &item);
    if (search == INVALID_HANDLE_VALUE) {
        DWORD failure = GetLastError();
        if (failure != ERROR_FILE_NOT_FOUND) goto open_error;
    } else {
        int scan_ok = 1;
        do {
            if (cancel && cancel(userdata)) { scan_ok = 0; break; }
            if (!strcmp(item.cFileName, ".") || !strcmp(item.cFileName, "..")) continue;
            if ((item.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                !folder_audio_name(item.cFileName)) { ++load->summary.skipped; continue; }
            if (!folder_add_name(&names, &count, &capacity, item.cFileName)) { scan_ok = -1; break; }
        } while (FindNextFileA(search, &item));
        DWORD failure = GetLastError();
        FindClose(search);
        if (!scan_ok) goto cancelled;
        if (scan_ok < 0) goto memory_error;
        if (failure != ERROR_NO_MORE_FILES) goto open_error;
    }
#else
    DIR *dir = opendir(directory);
    if (!dir) goto open_error;
    struct dirent *item;
    int scan_ok = 1;
    errno = 0;
    while ((item = readdir(dir)) != NULL) {
        if (cancel && cancel(userdata)) { scan_ok = 0; break; }
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        struct stat info;
        if (!folder_join(path, sizeof(path), directory, item->d_name)) { scan_ok = -2; break; }
        if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode) || !folder_audio_name(item->d_name))
            ++load->summary.skipped;
        else if (!folder_add_name(&names, &count, &capacity, item->d_name)) { scan_ok = -1; break; }
        errno = 0;
    }
    int read_error = errno;
    closedir(dir);
    if (!scan_ok) goto cancelled;
    if (scan_ok == -1) goto memory_error;
    if (scan_ok == -2) goto path_error;
    if (read_error) goto open_error;
#endif
    if (count > 1) qsort(names, count, sizeof(*names), folder_compare_names);
    for (size_t i = 0; i < count; ++i) {
        if (cancel && cancel(userdata)) goto cancelled;
        if (!folder_join(path, sizeof(path), directory, names[i])) goto path_error;
        TsAudioImport decoded;
        ts_audio_import_init(&decoded);
        int decoded_ok = ts_audio_import_decode_cancelable(&decoded, path, cancel, userdata, error, size);
        if (cancel && cancel(userdata)) { ts_audio_import_free(&decoded); goto cancelled; }
        if (!decoded_ok) { ++load->summary.failed; ts_audio_import_free(&decoded); continue; }
        while (page < load->count && !load->empty[page]) ++page;
        if (page == load->count) {
            if (page == TS_PROJECT_PAGE_LIMIT) {
                ts_audio_import_free(&decoded);
                pages_error(error, size, "Folder exceeds the Sample page limit; nothing imported"); goto done;
            }
            ++load->count;
            load->empty[page] = (1u << TS_BANK_SLOT_COUNT) - 1u;
            load->selected[page] = 0;
        }
        int slot = 0;
        while (!(load->empty[page] & (1u << slot))) ++slot;
        if (!load->staged[page]) load->staged[page] = new_page(error, size);
        TsInstrument *bank = load->staged[page];
        int installed = bank && ts_instrument_select_bank(bank, slot, error, size) &&
            ts_instrument_import_sample(bank, &decoded.sample, decoded.has_loop,
                decoded.loop_first, decoded.loop_last, decoded.loop_mode, error, size);
        ts_audio_import_free(&decoded);
        if (!installed) goto done;
        load->empty[page] &= ~(1u << slot);
        if (!load->summary.imported) load->summary.first_page = page + 1;
        load->summary.last_page = page + 1;
        ++load->summary.imported;
    }
    for (size_t p = 0; p < load->count; ++p) {
        if (cancel && cancel(userdata)) goto cancelled;
        if (load->staged[p] && !ts_instrument_select_bank(load->staged[p], load->selected[p], error, size)) goto done;
        if (p >= load->original_count) load->registry[p] = load->staged[p];
    }
    if (!load->summary.imported) {
        pages_error(error, size, count ? "No readable audio in this folder" : "No WAV, FLAC, MP3 or OGG files in this folder");
        goto done;
    }
    load->ready = ok = 1;
    pages_error(error, size, "");
    goto done;
open_error: pages_error(error, size, "Could not read the selected folder"); goto done;
path_error: pages_error(error, size, "Folder filename/path is too long; nothing imported"); goto done;
memory_error: pages_error(error, size, "Out of memory reading folder; nothing imported"); goto done;
cancelled: pages_error(error, size, "Folder import cancelled; nothing imported");
done:
    for (size_t i = 0; i < count; ++i) free(names[i]);
    free(names);
    return ok;
}

int ts_sample_folder_publish(TsSampleFolderImport *load, TsSamplePages *pages,
    TsInstrument *active, char *error, size_t size)
{
    if (!load || !load->ready || load->published || !pages || pages->mosaic_bank ||
        pages->page_count != load->original_count || pages->active_page != load->active_page ||
        pages->active_live != load->active_live) goto changed;
    for (size_t p = 0; p < load->original_count; ++p) {
        TsInstrument *bank = ts_sample_pages_page_mut(pages, active, p), *staged = load->staged[p];
        if (pages->pages[p] != load->registry[p] || bank != load->original[p] ||
            !bank || bank->selected_slot != load->selected[p]) goto changed;
        if (staged) for (int s = 0; s < TS_BANK_SLOT_COUNT; ++s)
            if (staged->bank[s].occupied && (bank->bank[s].occupied || bank->bank[s].locked)) goto changed;
    }
    /* Ownership only: preserve every occupied buffer, tile ID and private history. */
    for (size_t p = 0; p < load->original_count; ++p) {
        TsInstrument *bank = ts_sample_pages_page_mut(pages, active, p), *staged = load->staged[p];
        if (!staged) continue;
        int editor_changes = staged->bank[bank->selected_slot].occupied;
        for (int s = 0; s < TS_BANK_SLOT_COUNT; ++s) if (staged->bank[s].occupied) {
            TsBankSlot old = bank->bank[s];
            staged->bank[s].output_route = old.output_route;
            bank->bank[s] = staged->bank[s];
            staged->bank[s] = old;
        }
        if (editor_changes) {
            /* The selected destination was empty. Its fully prepared editor
               replaces the old empty editor without touching the bank array. */
            unsigned char scratch[4096];
            size_t remaining = offsetof(TsInstrument, bank), offset = 0;
            while (remaining) {
                size_t n = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
                memcpy(scratch, (unsigned char *)bank + offset, n);
                memcpy((unsigned char *)bank + offset, (unsigned char *)staged + offset, n);
                memcpy((unsigned char *)staged + offset, scratch, n);
                remaining -= n; offset += n;
            }
        }
    }
    TsInstrument **old = pages->pages;
    pages->pages = load->registry;
    pages->page_count = load->count;
    pages->page_capacity = TS_PROJECT_PAGE_LIMIT;
    load->registry = old;
    for (size_t p = load->original_count; p < load->count; ++p) load->staged[p] = NULL;
    load->published = 1;
    pages_error(error, size, "");
    return 1;
changed:
    pages_error(error, size, "Sample destinations changed; nothing imported. Try again");
    return 0;
}
