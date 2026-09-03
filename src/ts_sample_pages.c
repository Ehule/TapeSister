#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

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
#define TS_PAGES_RMDIR(path) _rmdir(path)
#else
#include <dirent.h>
#include <unistd.h>
#define TS_PAGES_MKDIR(path) mkdir(path, 0775)
#define TS_PAGES_RMDIR(path) rmdir(path)
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

int ts_sample_pages_remove_last_and_switch(TsSamplePages *pages,
                                           TsInstrument *active,
                                           size_t destination_page,
                                           char *error, size_t error_size)
{
    size_t last;
    TsInstrument *discard;
    if (pages == NULL || active == NULL || !pages->active_live ||
        pages->page_count < 2u || destination_page >= pages->page_count - 1u) {
        pages_error(error, error_size, "Invalid Sample page removal");
        return 0;
    }
    last = pages->page_count - 1u;
    if (pages->active_page != last) {
        pages_error(error, error_size, "Only the active last Sample page can be removed");
        return 0;
    }
    if (!ts_sample_pages_switch(pages, active, destination_page,
                                error, error_size)) return 0;
    discard = pages->pages[last];
    ts_instrument_free(discard);
    free(discard);
    pages->pages[last] = NULL;
    --pages->page_count;
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

static int regular_file_exists(const char *path)
{
    struct stat info;
    if (path == NULL || stat(path, &info) != 0) return 0;
#ifdef _WIN32
    return (info.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(info.st_mode);
#endif
}

static const char *path_name(const char *path)
{
    const char *slash = path != NULL ? strrchr(path, '/') : NULL;
    const char *backslash = path != NULL ? strrchr(path, '\\') : NULL;
    const char *separator = slash != NULL &&
                            (backslash == NULL || slash > backslash) ?
                            slash : backslash;
    return separator != NULL ? separator + 1 : path;
}

static int path_directory(const char *path, char *directory, size_t size)
{
    const char *name;
    size_t length;
    if (path == NULL || path[0] == '\0' || directory == NULL || size == 0u)
        return 0;
    name = path_name(path);
    length = name != NULL && name > path ? (size_t)(name - path - 1) : 0u;
    if (length == 0u) {
        if (size < 2u) return 0;
        directory[0] = '.';
        directory[1] = '\0';
        return 1;
    }
    if (length >= size) return 0;
    memcpy(directory, path, length);
    directory[length] = '\0';
    return 1;
}

static int project_stem(const char *path, char *stem, size_t size)
{
    const char *name = path_name(path);
    size_t length = name != NULL ? strlen(name) : 0u;
    if (length <= 4u || name[length - 4u] != '.' ||
        (name[length - 3u] != 't' && name[length - 3u] != 'T') ||
        (name[length - 2u] != 's' && name[length - 2u] != 'S') ||
        (name[length - 1u] != 'r' && name[length - 1u] != 'R') ||
        length - 4u >= size) return 0;
    memcpy(stem, name, length - 4u);
    stem[length - 4u] = '\0';
    return stem[0] != '\0';
}

static int path_component_equal(const char *first, const char *second)
{
#ifdef _WIN32
    if (first == NULL || second == NULL) return first == second;
    while (*first != '\0' && *second != '\0') {
        unsigned char left = (unsigned char)*first++;
        unsigned char right = (unsigned char)*second++;
        if (left >= 'A' && left <= 'Z') left = (unsigned char)(left + 'a' - 'A');
        if (right >= 'A' && right <= 'Z') right = (unsigned char)(right + 'a' - 'A');
        if (left != right) return 0;
    }
    return *first == *second;
#else
    return first != NULL && second != NULL && strcmp(first, second) == 0;
#endif
}

static int bundle_is_owned(const char *directory, const char *project_name);

int ts_sample_pages_bundle_project_path(const char *requested_path,
                                        char *project_path,
                                        size_t project_path_size,
                                        char *error, size_t error_size)
{
    char requested_copy[TS_PROJECT_PATH_MAX];
    char directory[TS_PROJECT_PATH_MAX];
    char stem[512];
    char name_copy[768];
    const char *name;
    const char *directory_name;
    int written;
    if (requested_path == NULL ||
        snprintf(requested_copy, sizeof(requested_copy), "%s", requested_path) < 0 ||
        strlen(requested_path) >= sizeof(requested_copy) ||
        project_path == NULL || project_path_size == 0u ||
        !path_directory(requested_copy, directory, sizeof(directory)) ||
        !project_stem(requested_copy, stem, sizeof(stem))) {
        pages_error(error, error_size, "Enter a valid TSR project name");
        return 0;
    }
    name = path_name(requested_copy);
    if (name == NULL || snprintf(name_copy, sizeof(name_copy), "%s", name) < 0 ||
        strlen(name) >= sizeof(name_copy)) {
        pages_error(error, error_size, "Project filename is too long");
        return 0;
    }
    name = name_copy;
    directory_name = path_name(directory);
    if (directory_name != NULL &&
        (path_component_equal(directory_name, stem) ||
         bundle_is_owned(directory, name))) {
        written = snprintf(project_path, project_path_size, "%s", requested_copy);
    } else if (strcmp(directory, ".") == 0) {
        written = snprintf(project_path, project_path_size, "%s/%s", stem, name);
    } else {
        written = snprintf(project_path, project_path_size, "%s/%s/%s",
                           directory, stem, name);
    }
    if (written < 0 || (size_t)written >= project_path_size) {
        pages_error(error, error_size, "Project bundle path is too long");
        return 0;
    }
    pages_error(error, error_size, "");
    return 1;
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

static int remove_tree(const char *path)
{
#ifdef _WIN32
    {
        DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND ||
                   GetLastError() == ERROR_PATH_NOT_FOUND;
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ?
                   RemoveDirectoryA(path) != 0 : DeleteFileA(path) != 0;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return DeleteFileA(path) != 0;
    }
    {
        WIN32_FIND_DATAA entry;
        HANDLE search;
        char pattern[TS_PROJECT_PATH_MAX];
        char child[TS_PROJECT_PATH_MAX];
        if (snprintf(pattern, sizeof(pattern), "%s/*", path) < 0 ||
            strlen(path) + 3u >= sizeof(pattern)) return 0;
        search = FindFirstFileA(pattern, &entry);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(entry.cFileName, ".") == 0 ||
                    strcmp(entry.cFileName, "..") == 0) continue;
                if (snprintf(child, sizeof(child), "%s/%s", path,
                             entry.cFileName) < 0 ||
                    strlen(path) + strlen(entry.cFileName) + 2u >= sizeof(child) ||
                    !remove_tree(child)) {
                    FindClose(search);
                    return 0;
                }
            } while (FindNextFileA(search, &entry));
            FindClose(search);
        }
    }
#else
    {
        struct stat info;
        if (lstat(path, &info) != 0) return errno == ENOENT;
        if (!S_ISDIR(info.st_mode)) return unlink(path) == 0;
    }
    {
        DIR *directory = opendir(path);
        struct dirent *entry;
        char child[TS_PROJECT_PATH_MAX];
        if (directory == NULL) return 0;
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) continue;
            if (snprintf(child, sizeof(child), "%s/%s", path,
                         entry->d_name) < 0 ||
                strlen(path) + strlen(entry->d_name) + 2u >= sizeof(child) ||
                !remove_tree(child)) {
                closedir(directory);
                return 0;
            }
        }
        closedir(directory);
    }
#endif
    return TS_PAGES_RMDIR(path) == 0;
}

static int rename_directory(const char *source, const char *destination)
{
#ifdef _WIN32
    return MoveFileExA(source, destination, MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(source, destination) == 0;
#endif
}

static void sample_safe_name(const char *source, char *safe, size_t size)
{
    size_t used = 0u;
    int separator = 0;
    if (safe == NULL || size == 0u) return;
    if (source == NULL) source = "sample";
    while (*source != '\0' && used + 1u < size) {
        unsigned char ch = (unsigned char)*source++;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            safe[used++] = (char)ch;
            separator = 0;
        } else if (!separator && used > 0u) {
            safe[used++] = '-';
            separator = 1;
        }
    }
    while (used > 0u && safe[used - 1u] == '-') --used;
    if (used == 0u) {
        snprintf(safe, size, "sample");
        return;
    }
    safe[used] = '\0';
}

static int make_directory(const char *path, const char *description,
                          char *error, size_t error_size)
{
    if (TS_PAGES_MKDIR(path) == 0) return 1;
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "Could not create %s: %s",
                 description, strerror(errno));
    return 0;
}

static int write_partial_marker(const char *directory,
                                char *error, size_t error_size)
{
    char path[TS_PROJECT_PATH_MAX];
    FILE *file;
    if (snprintf(path, sizeof(path), "%s/.tapesister-partial", directory) < 0 ||
        strlen(directory) + 21u >= sizeof(path)) {
        pages_error(error, error_size, "Project staging path is too long");
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        pages_error(error, error_size, "Could not mark project staging folder");
        return 0;
    }
    {
        int failed = fputs("TapeSister partial project\n", file) == EOF;
        if (fclose(file) != 0) failed = 1;
        if (!failed) return 1;
        remove(path);
        pages_error(error, error_size, "Could not mark project staging folder");
        return 0;
    }
}

static int stage_is_owned(const char *directory)
{
    char marker[TS_PROJECT_PATH_MAX];
    return snprintf(marker, sizeof(marker), "%s/.tapesister-partial", directory) >= 0 &&
           strlen(directory) + 21u < sizeof(marker) && regular_file_exists(marker);
}

static int bundle_is_owned(const char *directory, const char *project_name)
{
    char manifest[TS_PROJECT_PATH_MAX];
    char line[1024];
    FILE *file;
    int matched = 0;
    if (snprintf(manifest, sizeof(manifest), "%s/manifest.txt", directory) < 0 ||
        strlen(directory) + 14u >= sizeof(manifest)) return 0;
    file = fopen(manifest, "rb");
    if (file == NULL) return 0;
    if (fgets(line, sizeof(line), file) != NULL &&
        strcmp(line, "TAPESISTER_PROJECT 2\n") == 0 &&
        fgets(line, sizeof(line), file) != NULL) {
        char expected[768];
        snprintf(expected, sizeof(expected), "project=%s\n", project_name);
        matched = strcmp(line, expected) == 0;
    }
    fclose(file);
    return matched;
}

static int write_instrument_wavs(const TsInstrument *instrument,
                                 const char *directory,
                                 char *error, size_t error_size)
{
    char path[TS_PROJECT_PATH_MAX];
    char safe[96];
    if (!make_directory(directory, "project sample folder", error, error_size))
        return 0;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        const TsBankSlot *member = &instrument->bank[slot];
        if (!member->occupied) continue;
        sample_safe_name(member->sample.name, safe, sizeof(safe));
        if (snprintf(path, sizeof(path), "%s/%02d_%s.wav", directory,
                     slot + 1, safe) < 0 ||
            strlen(directory) + strlen(safe) + 9u >= sizeof(path)) {
            pages_error(error, error_size, "Project WAV path is too long");
            return 0;
        }
        if (!ts_sample_save_wav16_tuned_looped(
                &member->sample, &member->audible_tuning,
                member->has_loop, member->loop_first, member->loop_last,
                member->loop_mode, path, error, error_size)) return 0;
    }
    return 1;
}

static int write_manifest_members(FILE *file, const TsInstrument *instrument,
                                  const char *group, const char *directory)
{
    char safe[96];
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        const TsBankSlot *member = &instrument->bank[slot];
        if (!member->occupied) continue;
        sample_safe_name(member->sample.name, safe, sizeof(safe));
        if (fprintf(file,
                    "sample=%s,%d,samples/%s/%02d_%s.wav,%u,%u,%zu,%d,%.9g,%d,%.9g,%d,%zu,%zu,%d,%.9g\n",
                    group, slot + 1, directory, slot + 1, safe,
                    (unsigned)member->sample.channels,
                    (unsigned)member->sample.sample_rate,
                    member->sample.frames,
                    member->tuning.root_note, member->tuning.fine_tune_cents,
                    member->audible_tuning.root_note,
                    member->audible_tuning.fine_tune_cents,
                    member->has_loop, member->loop_first, member->loop_last,
                    member->loop_mode, member->loop_crossfade_ms) < 0)
            return 0;
    }
    return 1;
}

static int validate_saved_instrument(const char *path,
                                     char *error, size_t error_size)
{
    TsInstrument loaded;
    int ok;
    ts_instrument_init(&loaded);
    ok = ts_instrument_load_recipe(&loaded, path, error, error_size);
    ts_instrument_free(&loaded);
    return ok;
}

int ts_sample_pages_save_project(const TsSamplePages *pages,
                                 const TsInstrument *active_sample,
                                 const TsInstrument *record_bank,
                                 const TsSisterProjectState *sister_state,
                                 const char *path,
                                 char *error, size_t error_size)
{
    char directory[TS_PROJECT_PATH_MAX];
    char stem[512];
    char staging[TS_PROJECT_PATH_MAX];
    char backup[TS_PROJECT_PATH_MAX];
    char project_data[TS_PROJECT_PATH_MAX];
    char samples[TS_PROJECT_PATH_MAX];
    char destination[TS_PROJECT_PATH_MAX];
    char manifest[TS_PROJECT_PATH_MAX];
    char state_path[TS_PROJECT_PATH_MAX];
    char sample_directory[TS_PROJECT_PATH_MAX];
    char group[64];
    FILE *file;
    int record_present;
    int had_existing;
    size_t sample_count = 0u;
    const char *project_name = path_name(path);
    if (pages == NULL || path == NULL || path[0] == '\0' ||
        pages->page_count == 0u ||
        (pages->active_live && active_sample == NULL)) {
        pages_error(error, error_size, "No paged project to save");
        return 0;
    }
    if (!path_directory(path, directory, sizeof(directory)) ||
        !project_stem(path, stem, sizeof(stem)) ||
        (!path_component_equal(path_name(directory), stem) &&
         !bundle_is_owned(directory, project_name))) {
        pages_error(error, error_size,
                    "TSR project must be saved inside its named project folder");
        return 0;
    }
    if (snprintf(staging, sizeof(staging), "%s.tapesister-partial", directory) < 0 ||
        snprintf(backup, sizeof(backup), "%s.tapesister-backup", directory) < 0 ||
        strlen(directory) + 21u >= sizeof(staging) ||
        strlen(directory) + 20u >= sizeof(backup)) {
        pages_error(error, error_size, "Project transaction path is too long");
        return 0;
    }
    had_existing = directory_exists(directory);
    if (had_existing && !bundle_is_owned(directory, project_name)) {
        pages_error(error, error_size,
                    "A folder with that name exists but is not this TapeSister project");
        return 0;
    }
    if (directory_exists(backup)) {
        pages_error(error, error_size,
                    "A previous project backup needs attention before saving");
        return 0;
    }
    if (directory_exists(staging)) {
        if (!stage_is_owned(staging) || !remove_tree(staging)) {
            pages_error(error, error_size,
                        "A project staging folder already exists and could not be recovered");
            return 0;
        }
    }
    if (!make_directory(staging, "project staging folder", error, error_size) ||
        !write_partial_marker(staging, error, error_size)) goto failed;
    if (snprintf(project_data, sizeof(project_data), "%s/project-data", staging) < 0 ||
        snprintf(samples, sizeof(samples), "%s/samples", staging) < 0 ||
        strlen(staging) + 14u >= sizeof(project_data) ||
        strlen(staging) + 9u >= sizeof(samples) ||
        !make_directory(project_data, "project data folder", error, error_size) ||
        !make_directory(samples, "project WAV folder", error, error_size)) goto failed;
    if (snprintf(destination, sizeof(destination), "%s/%s", staging,
                 project_name) < 0 ||
        strlen(staging) + strlen(project_name) + 2u >= sizeof(destination)) {
        pages_error(error, error_size, "Project file path is too long");
        goto failed;
    }
    if (!save_instrument_atomic(ts_sample_pages_page(pages, active_sample, 0u),
                                destination, error, error_size) ||
        !validate_saved_instrument(destination, error, error_size)) goto failed;
    for (size_t page = 1u; page < pages->page_count; ++page) {
        if (snprintf(destination, sizeof(destination), "%s/page-%03zu.tsr",
                     project_data, page + 1u) < 0 ||
            strlen(project_data) + 24u >= sizeof(destination)) {
            pages_error(error, error_size, "Project page path is too long");
            goto failed;
        }
        if (!save_instrument_atomic(ts_sample_pages_page(pages, active_sample, page),
                                    destination, error, error_size) ||
            !validate_saved_instrument(destination, error, error_size)) goto failed;
    }
    record_present = record_bank != NULL && ts_instrument_bank_count(record_bank) > 0;
    if (record_present) {
        if (snprintf(destination, sizeof(destination), "%s/record-bank.tsr",
                     project_data) < 0 ||
            strlen(project_data) + 18u >= sizeof(destination)) {
            pages_error(error, error_size, "Record Bank path is too long");
            goto failed;
        }
        if (!save_instrument_atomic(record_bank, destination, error, error_size) ||
            !validate_saved_instrument(destination, error, error_size)) goto failed;
    }
    for (size_t page = 0u; page < pages->page_count; ++page) {
        const TsInstrument *instrument = ts_sample_pages_page(
            pages, active_sample, page);
        snprintf(group, sizeof(group), "page-%03zu", page + 1u);
        if (snprintf(sample_directory, sizeof(sample_directory), "%s/%s",
                     samples, group) < 0 ||
            strlen(samples) + strlen(group) + 2u >= sizeof(sample_directory) ||
            !write_instrument_wavs(instrument, sample_directory,
                                   error, error_size)) goto failed;
        sample_count += (size_t)ts_instrument_bank_count(instrument);
    }
    if (record_present) {
        if (snprintf(sample_directory, sizeof(sample_directory),
                     "%s/record-bank", samples) < 0 ||
            strlen(samples) + 13u >= sizeof(sample_directory) ||
            !write_instrument_wavs(record_bank, sample_directory,
                                   error, error_size)) goto failed;
        sample_count += (size_t)ts_instrument_bank_count(record_bank);
    }
    if (sister_state != NULL) {
        TsSisterProjectState validated_state;
        int state_present = 0;
        if (sister_state->page_count != pages->page_count ||
            sister_state->active_page != pages->active_page) {
            pages_error(error, error_size,
                        "Sister state does not match the project pages");
            goto failed;
        }
        if (snprintf(state_path, sizeof(state_path), "%s/sister-state.ini",
                     staging) < 0 || strlen(staging) + 19u >= sizeof(state_path)) {
            pages_error(error, error_size, "Sister project state path is too long");
            goto failed;
        }
        if (!ts_sister_project_state_save_file(
                sister_state, state_path, error, error_size) ||
            !ts_sister_project_state_load_file(
                &validated_state, state_path, 48000u,
                &state_present, error, error_size) ||
            !state_present || validated_state.page_count != pages->page_count ||
            validated_state.active_page != pages->active_page) {
            if (error != NULL && error_size > 0u && error[0] == '\0')
                pages_error(error, error_size,
                            "Could not validate Sister project state");
            goto failed;
        }
    }
    if (snprintf(manifest, sizeof(manifest), "%s/manifest.txt", staging) < 0 ||
        strlen(staging) + 14u >= sizeof(manifest)) {
        pages_error(error, error_size, "Project manifest path is too long");
        goto failed;
    }
    file = fopen(manifest, "wb");
    if (file == NULL) {
        pages_error(error, error_size, "Could not create project page manifest");
        goto failed;
    }
    if (fprintf(file,
                "TAPESISTER_PROJECT 2\nproject=%s\npage_count=%zu\n"
                "active_page=%zu\nrecord_bank=%d\n"
                "sister_state=%d\nsample_format=WAV_PCM16\nsample_count=%zu\n",
                project_name, pages->page_count, pages->active_page,
                record_present, sister_state != NULL, sample_count) < 0)
        goto manifest_failed;
    for (size_t page = 0u; page < pages->page_count; ++page) {
        snprintf(group, sizeof(group), "page-%03zu", page + 1u);
        if (!write_manifest_members(file,
                ts_sample_pages_page(pages, active_sample, page),
                group, group)) goto manifest_failed;
    }
    if (record_present &&
        !write_manifest_members(file, record_bank,
                                "record-bank", "record-bank"))
        goto manifest_failed;
    {
        int write_failed = ferror(file);
        int close_failed = fclose(file) != 0;
        if (write_failed || close_failed) {
            pages_error(error, error_size, "Could not finish project page manifest");
            goto failed;
        }
    }
    if (had_existing && !rename_directory(directory, backup)) {
        pages_error(error, error_size, "Could not prepare the existing project for replacement");
        goto failed;
    }
    if (!rename_directory(staging, directory)) {
        if (had_existing) (void)rename_directory(backup, directory);
        pages_error(error, error_size, "Could not publish the complete project folder");
        goto failed;
    }
    if (snprintf(destination, sizeof(destination), "%s/.tapesister-partial",
                 directory) >= 0 && strlen(directory) + 21u < sizeof(destination))
        (void)remove(destination);
    if (had_existing) (void)remove_tree(backup);
    pages_error(error, error_size, "");
    return 1;

manifest_failed:
    fclose(file);
    pages_error(error, error_size, "Could not write project page manifest");
failed:
    if (stage_is_owned(staging)) (void)remove_tree(staging);
    return 0;
}

static int read_manifest(const char *project, char *directory,
                         size_t directory_size, int *layout,
                         size_t *page_count, size_t *active_page,
                         int *record_present, int *found,
                         char *error, size_t error_size)
{
    char path[TS_PROJECT_PATH_MAX];
    char expected[768];
    char line[1024];
    FILE *file;
    int version;
    if (!path_directory(project, directory, directory_size) ||
        snprintf(path, sizeof(path), "%s/manifest.txt", directory) < 0 ||
        strlen(directory) + 14u >= sizeof(path)) {
        pages_error(error, error_size, "Project manifest path is too long");
        return 0;
    }
    file = fopen(path, "rb");
    if (file != NULL) {
        if (fgets(line, sizeof(line), file) != NULL &&
            strncmp(line, "TAPESISTER_PROJECT ", 19u) == 0) {
            if (sscanf(line, "TAPESISTER_PROJECT %d", &version) != 1 ||
                version != 2) {
                fclose(file);
                pages_error(error, error_size,
                            "Unsupported TapeSister project manifest version");
                return 0;
            }
            if (fgets(line, sizeof(line), file) == NULL) {
                fclose(file);
                pages_error(error, error_size, "Malformed TapeSister project manifest");
                return 0;
            }
            snprintf(expected, sizeof(expected), "project=%s", path_name(project));
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, expected) != 0) {
                fclose(file);
                pages_error(error, error_size,
                            "Project manifest names a different TSR file");
                return 0;
            }
            if (fgets(line, sizeof(line), file) == NULL ||
                sscanf(line, "page_count=%zu", page_count) != 1 ||
                fgets(line, sizeof(line), file) == NULL ||
                sscanf(line, "active_page=%zu", active_page) != 1 ||
                fgets(line, sizeof(line), file) == NULL ||
                sscanf(line, "record_bank=%d", record_present) != 1 ||
                *page_count == 0u || *page_count > TS_PROJECT_PAGE_LIMIT ||
                *active_page >= *page_count ||
                (*record_present != 0 && *record_present != 1)) {
                fclose(file);
                pages_error(error, error_size, "Malformed TapeSister project manifest");
                return 0;
            }
            fclose(file);
            *layout = 2;
            *found = 1;
            return 1;
        }
        fclose(file);
    } else if (errno != ENOENT) {
        pages_error(error, error_size, "Could not open project page manifest");
        return 0;
    }
    if (!companion_path(project, directory, directory_size,
                        error, error_size) ||
        snprintf(path, sizeof(path), "%s/manifest.txt", directory) < 0 ||
        strlen(directory) + 14u >= sizeof(path)) return 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            *found = 0;
            *layout = 0;
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
    *layout = 1;
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
    int layout = 0;
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
    if (!read_manifest(path, directory, sizeof(directory), &layout,
                       &page_count, &active_page, &record_present,
                       &manifest_found, error, error_size)) goto failed;
    if (manifest_found) {
        while (loaded.page_count < page_count)
            if (!append_page(&loaded, error, error_size)) goto failed;
        for (size_t page = 1u; page < page_count; ++page) {
            if (snprintf(page_path, sizeof(page_path),
                         layout == 2 ? "%s/project-data/page-%03zu.tsr" :
                                       "%s/page-%03zu.tsr",
                         directory, page + 1u) < 0 ||
                strlen(directory) + (layout == 2 ? 37u : 24u) >=
                    sizeof(page_path)) {
                pages_error(error, error_size, "Project page path is too long");
                goto failed;
            }
            if (!ts_instrument_load_recipe(loaded.pages[page], page_path,
                                           error, error_size)) goto failed;
        }
        if (record_present) {
            if (snprintf(page_path, sizeof(page_path),
                         layout == 2 ? "%s/project-data/record-bank.tsr" :
                                       "%s/record-bank.tsr",
                         directory) < 0 ||
                strlen(directory) + (layout == 2 ? 31u : 18u) >=
                    sizeof(page_path)) {
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
