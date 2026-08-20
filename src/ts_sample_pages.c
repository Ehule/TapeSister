#include "tapesister/sample_pages.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define TS_PAGES_MKDIR(path) _mkdir(path)
#else
#define TS_PAGES_MKDIR(path) mkdir(path, 0775)
#endif

enum { TS_PROJECT_PATH_MAX = 2048, TS_PROJECT_PAGE_LIMIT = 1024 };

static void pages_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static void swap_instruments(TsInstrument *first, TsInstrument *second)
{
    unsigned char scratch[4096];
    unsigned char *left = (unsigned char *)first;
    unsigned char *right = (unsigned char *)second;
    size_t remaining = sizeof(*first);
    while (remaining > 0u) {
        size_t amount = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        memcpy(scratch, left, amount);
        memcpy(left, right, amount);
        memcpy(right, scratch, amount);
        left += amount;
        right += amount;
        remaining -= amount;
    }
}

static TsInstrument *new_page(char *error, size_t error_size)
{
    TsInstrument *page = (TsInstrument *)malloc(sizeof(*page));
    if (page == NULL) {
        pages_error(error, error_size, "Out of memory creating Sample page");
        return NULL;
    }
    ts_instrument_init(page);
    return page;
}

int ts_sample_pages_init(TsSamplePages *pages,
                         char *error, size_t error_size)
{
    if (pages == NULL) {
        pages_error(error, error_size, "Sample page storage is unavailable");
        return 0;
    }
    memset(pages, 0, sizeof(*pages));
    pages->pages = (TsInstrument **)calloc(1u, sizeof(*pages->pages));
    if (pages->pages == NULL) {
        pages_error(error, error_size, "Out of memory creating Sample pages");
        return 0;
    }
    pages->pages[0] = new_page(error, error_size);
    if (pages->pages[0] == NULL) {
        free(pages->pages);
        memset(pages, 0, sizeof(*pages));
        return 0;
    }
    pages->page_count = 1u;
    pages->page_capacity = 1u;
    pages->active_page = 0u;
    pages->active_live = 1;
    pages_error(error, error_size, "");
    return 1;
}

void ts_sample_pages_free(TsSamplePages *pages)
{
    if (pages == NULL) return;
    if (pages->pages != NULL) {
        for (size_t page = 0; page < pages->page_count; ++page) {
            if (pages->pages[page] == NULL) continue;
            ts_instrument_free(pages->pages[page]);
            free(pages->pages[page]);
        }
        free(pages->pages);
    }
    memset(pages, 0, sizeof(*pages));
}

size_t ts_sample_pages_count(const TsSamplePages *pages)
{
    return pages != NULL ? pages->page_count : 0u;
}

size_t ts_sample_pages_active(const TsSamplePages *pages)
{
    return pages != NULL ? pages->active_page : 0u;
}

const TsInstrument *ts_sample_pages_page(const TsSamplePages *pages,
                                         const TsInstrument *active,
                                         size_t page)
{
    if (pages == NULL || page >= pages->page_count) return NULL;
    if (pages->active_live && page == pages->active_page) return active;
    return pages->pages[page];
}

TsInstrument *ts_sample_pages_page_mut(TsSamplePages *pages,
                                       TsInstrument *active, size_t page)
{
    return (TsInstrument *)ts_sample_pages_page(pages, active, page);
}

static int append_page(TsSamplePages *pages, char *error, size_t error_size)
{
    TsInstrument **grown;
    TsInstrument *page;
    size_t capacity;
    if (pages->page_count >= TS_PROJECT_PAGE_LIMIT) {
        pages_error(error, error_size, "Sample page limit reached");
        return 0;
    }
    page = new_page(error, error_size);
    if (page == NULL) return 0;
    if (pages->page_count == pages->page_capacity) {
        capacity = pages->page_capacity < 4u ? 4u : pages->page_capacity * 2u;
        grown = (TsInstrument **)realloc(pages->pages,
                                         capacity * sizeof(*grown));
        if (grown == NULL) {
            ts_instrument_free(page);
            free(page);
            pages_error(error, error_size, "Out of memory growing Sample pages");
            return 0;
        }
        pages->pages = grown;
        pages->page_capacity = capacity;
    }
    pages->pages[pages->page_count++] = page;
    return 1;
}

int ts_sample_pages_switch(TsSamplePages *pages, TsInstrument *active,
                           size_t page, char *error, size_t error_size)
{
    if (pages == NULL || active == NULL || !pages->active_live ||
        page >= pages->page_count) {
        pages_error(error, error_size, "Invalid Sample page switch");
        return 0;
    }
    if (page == pages->active_page) {
        pages_error(error, error_size, "");
        return 1;
    }
    swap_instruments(active, pages->pages[pages->active_page]);
    swap_instruments(active, pages->pages[page]);
    pages->active_page = page;
    pages_error(error, error_size, "");
    return 1;
}

int ts_sample_pages_append_and_switch(TsSamplePages *pages,
                                      TsInstrument *active,
                                      size_t *new_page,
                                      char *error, size_t error_size)
{
    size_t page;
    if (new_page != NULL) *new_page = 0u;
    if (pages == NULL || active == NULL || !pages->active_live) {
        pages_error(error, error_size, "Sample page storage is unavailable");
        return 0;
    }
    page = pages->page_count;
    if (!append_page(pages, error, error_size)) return 0;
    if (!ts_sample_pages_switch(pages, active, page, error, error_size)) {
        TsInstrument *discard = pages->pages[page];
        ts_instrument_free(discard);
        free(discard);
        pages->pages[page] = NULL;
        --pages->page_count;
        return 0;
    }
    if (new_page != NULL) *new_page = page;
    pages_error(error, error_size, "");
    return 1;
}

int ts_sample_pages_park(TsSamplePages *pages, TsInstrument *active,
                         char *error, size_t error_size)
{
    if (pages == NULL || active == NULL || !pages->active_live ||
        pages->active_page >= pages->page_count) {
        pages_error(error, error_size, "Sample page is already parked");
        return 0;
    }
    swap_instruments(active, pages->pages[pages->active_page]);
    pages->active_live = 0;
    pages_error(error, error_size, "");
    return 1;
}

int ts_sample_pages_unpark(TsSamplePages *pages, TsInstrument *active,
                           char *error, size_t error_size)
{
    if (pages == NULL || active == NULL || pages->active_live ||
        pages->active_page >= pages->page_count) {
        pages_error(error, error_size, "Sample page is already active");
        return 0;
    }
    swap_instruments(active, pages->pages[pages->active_page]);
    pages->active_live = 1;
    pages_error(error, error_size, "");
    return 1;
}

typedef struct {
    size_t page;
    int slot;
} KeepDestination;

static int find_empty_destination(TsSamplePages *pages,
                                  TsInstrument *active_sample,
                                  size_t *page_out, int *slot_out,
                                  char *error, size_t error_size)
{
    for (;;) {
        for (size_t page = 0; page < pages->page_count; ++page) {
            TsInstrument *instrument = ts_sample_pages_page_mut(
                pages, active_sample, page);
            for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
                if (!instrument->bank[slot].occupied) {
                    *page_out = page;
                    *slot_out = slot;
                    return 1;
                }
            }
        }
        if (!append_page(pages, error, error_size)) return 0;
    }
}

int ts_sample_pages_keep_record_bank(TsSamplePages *pages,
                                     TsInstrument *active_sample,
                                     TsInstrument *record_bank,
                                     size_t *copied,
                                     size_t *first_page,
                                     size_t *last_page,
                                     char *error, size_t error_size)
{
    KeepDestination destinations[TS_BANK_SLOT_COUNT];
    int sources[TS_BANK_SLOT_COUNT];
    size_t source_count = 0u;
    size_t old_page_count;
    size_t copied_count = 0u;
    if (copied != NULL) *copied = 0u;
    if (first_page != NULL) *first_page = 0u;
    if (last_page != NULL) *last_page = 0u;
    if (pages == NULL || record_bank == NULL ||
        (pages->active_live && active_sample == NULL)) {
        pages_error(error, error_size, "Sample page storage is unavailable");
        return 0;
    }
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        if (record_bank->bank[slot].occupied) sources[source_count++] = slot;
    if (source_count == 0u) {
        pages_error(error, error_size, "");
        return 1;
    }
    old_page_count = pages->page_count;
    for (size_t index = 0; index < source_count; ++index) {
        TsInstrument *destination;
        if (!find_empty_destination(pages, active_sample,
                                    &destinations[index].page,
                                    &destinations[index].slot,
                                    error, error_size)) goto rollback;
        destination = ts_sample_pages_page_mut(
            pages, active_sample, destinations[index].page);
        if (!ts_instrument_copy_bank_slot_from(
                destination, destinations[index].slot,
                record_bank, sources[index], error, error_size)) goto rollback;
        ++copied_count;
    }
    if (!ts_instrument_bank_clear_all(record_bank, error, error_size))
        return 0;
    if (copied != NULL) *copied = copied_count;
    if (first_page != NULL) *first_page = destinations[0].page + 1u;
    if (last_page != NULL) *last_page = destinations[copied_count - 1u].page + 1u;
    pages_error(error, error_size, "");
    return 1;

rollback:
    for (size_t index = 0; index < copied_count; ++index) {
        TsInstrument *destination = ts_sample_pages_page_mut(
            pages, active_sample, destinations[index].page);
        char ignored[80];
        (void)ts_instrument_bank_clear(destination,
                                       destinations[index].slot,
                                       ignored, sizeof(ignored));
    }
    while (pages->page_count > old_page_count) {
        size_t last = pages->page_count - 1u;
        ts_instrument_free(pages->pages[last]);
        free(pages->pages[last]);
        pages->pages[last] = NULL;
        --pages->page_count;
    }
    return 0;
}

static int directory_exists(const char *path)
{
    struct stat info;
    if (path == NULL || stat(path, &info) != 0) return 0;
#ifdef _WIN32
    return (info.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(info.st_mode);
#endif
}

static int ensure_companion_directory(const char *path,
                                      char *error, size_t error_size)
{
    if (directory_exists(path)) return 1;
    if (TS_PAGES_MKDIR(path) == 0 || errno == EEXIST) return 1;
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "Could not create project companion folder: %s",
                 strerror(errno));
    return 0;
}

static int companion_path(const char *project, char *directory, size_t size,
                          char *error, size_t error_size)
{
    int written = snprintf(directory, size, "%s.samples", project);
    if (written < 0 || (size_t)written >= size) {
        pages_error(error, error_size, "Project path is too long");
        return 0;
    }
    return 1;
}

static int finish_atomic(const char *temporary, const char *destination,
                         char *error, size_t error_size)
{
#ifdef _WIN32
    if (MoveFileExA(temporary, destination,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
#else
    if (rename(temporary, destination) == 0) return 1;
#endif
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "Could not replace project file: %s",
                 strerror(errno));
    remove(temporary);
    return 0;
}

static int save_instrument_atomic(const TsInstrument *instrument,
                                  const char *destination,
                                  char *error, size_t error_size)
{
    char temporary[TS_PROJECT_PATH_MAX + 32];
    int written = snprintf(temporary, sizeof(temporary),
                           "%s.tapesister-tmp", destination);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        pages_error(error, error_size, "Project path is too long");
        return 0;
    }
    if (!ts_instrument_save_recipe(instrument, temporary, error, error_size)) {
        remove(temporary);
        return 0;
    }
    return finish_atomic(temporary, destination, error, error_size);
}

int ts_sample_pages_save_project(const TsSamplePages *pages,
                                 const TsInstrument *active_sample,
                                 const TsInstrument *record_bank,
                                 const char *path,
                                 char *error, size_t error_size)
{
    char directory[TS_PROJECT_PATH_MAX];
    char destination[TS_PROJECT_PATH_MAX];
    char manifest[TS_PROJECT_PATH_MAX];
    char temporary[TS_PROJECT_PATH_MAX + 32];
    FILE *file;
    int record_present;
    if (pages == NULL || path == NULL || path[0] == '\0' ||
        pages->page_count == 0u ||
        (pages->active_live && active_sample == NULL)) {
        pages_error(error, error_size, "No paged project to save");
        return 0;
    }
    if (!companion_path(path, directory, sizeof(directory), error, error_size) ||
        !ensure_companion_directory(directory, error, error_size)) return 0;
    if (!save_instrument_atomic(ts_sample_pages_page(pages, active_sample, 0u),
                                path, error, error_size)) return 0;
    for (size_t page = 1u; page < pages->page_count; ++page) {
        if (snprintf(destination, sizeof(destination), "%s/page-%03zu.tsr",
                     directory, page + 1u) < 0 || strlen(directory) + 24u >= sizeof(destination)) {
            pages_error(error, error_size, "Project page path is too long");
            return 0;
        }
        if (!save_instrument_atomic(ts_sample_pages_page(pages, active_sample, page),
                                    destination, error, error_size)) return 0;
    }
    record_present = record_bank != NULL && ts_instrument_bank_count(record_bank) > 0;
    if (record_present) {
        if (snprintf(destination, sizeof(destination), "%s/record-bank.tsr",
                     directory) < 0 || strlen(directory) + 18u >= sizeof(destination)) {
            pages_error(error, error_size, "Record Bank path is too long");
            return 0;
        }
        if (!save_instrument_atomic(record_bank, destination, error, error_size)) return 0;
    }
    if (snprintf(manifest, sizeof(manifest), "%s/manifest.txt", directory) < 0 ||
        strlen(directory) + 14u >= sizeof(manifest) ||
        snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", manifest) < 0 ||
        strlen(manifest) + 16u >= sizeof(temporary)) {
        pages_error(error, error_size, "Project manifest path is too long");
        return 0;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        pages_error(error, error_size, "Could not create project page manifest");
        return 0;
    }
    fprintf(file, "TAPESISTER_PAGES 1\npage_count=%zu\nactive_page=%zu\nrecord_bank=%d\n",
            pages->page_count, pages->active_page, record_present);
    {
        int write_failed = ferror(file);
        int close_failed = fclose(file) != 0;
        if (write_failed || close_failed) {
            remove(temporary);
            pages_error(error, error_size, "Could not finish project page manifest");
            return 0;
        }
    }
    if (!finish_atomic(temporary, manifest, error, error_size)) return 0;
    pages_error(error, error_size, "");
    return 1;
}

static int read_manifest(const char *directory, size_t *page_count,
                         size_t *active_page, int *record_present,
                         int *found, char *error, size_t error_size)
{
    char path[TS_PROJECT_PATH_MAX];
    char line[128];
    FILE *file;
    int version;
    if (snprintf(path, sizeof(path), "%s/manifest.txt", directory) < 0 ||
        strlen(directory) + 14u >= sizeof(path)) {
        pages_error(error, error_size, "Project manifest path is too long");
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            *found = 0;
            return 1;
        }
        pages_error(error, error_size, "Could not open project page manifest");
        return 0;
    }
    *found = 1;
    if (fgets(line, sizeof(line), file) == NULL ||
        sscanf(line, "TAPESISTER_PAGES %d", &version) != 1 || version != 1 ||
        fgets(line, sizeof(line), file) == NULL ||
        sscanf(line, "page_count=%zu", page_count) != 1 ||
        fgets(line, sizeof(line), file) == NULL ||
        sscanf(line, "active_page=%zu", active_page) != 1 ||
        fgets(line, sizeof(line), file) == NULL ||
        sscanf(line, "record_bank=%d", record_present) != 1 ||
        *page_count == 0u || *page_count > TS_PROJECT_PAGE_LIMIT ||
        *active_page >= *page_count ||
        (*record_present != 0 && *record_present != 1)) {
        fclose(file);
        pages_error(error, error_size, "Malformed project page manifest");
        return 0;
    }
    fclose(file);
    return 1;
}

int ts_sample_pages_load_project(TsSamplePages *pages,
                                 TsInstrument *active_sample,
                                 TsInstrument *record_bank,
                                 const char *path,
                                 char *error, size_t error_size)
{
    TsSamplePages loaded;
    TsInstrument loaded_record;
    char directory[TS_PROJECT_PATH_MAX];
    char page_path[TS_PROJECT_PATH_MAX];
    size_t page_count = 1u;
    size_t active_page = 0u;
    int record_present = 0;
    int manifest_found = 0;
    int loaded_ready = 0;
    if (pages == NULL || active_sample == NULL || record_bank == NULL ||
        !pages->active_live || path == NULL || path[0] == '\0') {
        pages_error(error, error_size,
                    "Return to a Sample page before opening a project");
        return 0;
    }
    memset(&loaded, 0, sizeof(loaded));
    ts_instrument_init(&loaded_record);
    if (!ts_sample_pages_init(&loaded, error, error_size)) goto failed;
    loaded_ready = 1;
    loaded.active_live = 0;
    if (!ts_instrument_load_recipe(loaded.pages[0], path, error, error_size))
        goto failed;
    if (!companion_path(path, directory, sizeof(directory), error, error_size) ||
        !read_manifest(directory, &page_count, &active_page, &record_present,
                       &manifest_found, error, error_size)) goto failed;
    if (manifest_found) {
        while (loaded.page_count < page_count)
            if (!append_page(&loaded, error, error_size)) goto failed;
        for (size_t page = 1u; page < page_count; ++page) {
            if (snprintf(page_path, sizeof(page_path), "%s/page-%03zu.tsr",
                         directory, page + 1u) < 0 ||
                strlen(directory) + 24u >= sizeof(page_path)) {
                pages_error(error, error_size, "Project page path is too long");
                goto failed;
            }
            if (!ts_instrument_load_recipe(loaded.pages[page], page_path,
                                           error, error_size)) goto failed;
        }
        if (record_present) {
            if (snprintf(page_path, sizeof(page_path), "%s/record-bank.tsr",
                         directory) < 0 || strlen(directory) + 18u >= sizeof(page_path)) {
                pages_error(error, error_size, "Record Bank path is too long");
                goto failed;
            }
            if (!ts_instrument_load_recipe(&loaded_record, page_path,
                                           error, error_size)) goto failed;
        }
    }
    loaded.active_page = active_page;
    ts_instrument_free(active_sample);
    ts_instrument_init(active_sample);
    ts_sample_pages_free(pages);
    ts_instrument_free(record_bank);
    *record_bank = loaded_record;
    ts_instrument_init(&loaded_record);
    *pages = loaded;
    memset(&loaded, 0, sizeof(loaded));
    if (!ts_sample_pages_unpark(pages, active_sample, error, error_size))
        return 0;
    pages_error(error, error_size, "");
    return 1;

failed:
    if (loaded_ready) ts_sample_pages_free(&loaded);
    ts_instrument_free(&loaded_record);
    return 0;
}
