#include "tapesister/browser.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#else
#include <dirent.h>
#include <unistd.h>
#endif

static int case_compare(const char *a, const char *b)
{
    for (;;) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb || ca == 0 || cb == 0) return ca - cb;
    }
}

static int entry_compare(const void *left, const void *right)
{
    const TsBrowserEntry *a = (const TsBrowserEntry *)left;
    const TsBrowserEntry *b = (const TsBrowserEntry *)right;
    if (a->is_directory != b->is_directory) return b->is_directory - a->is_directory;
    return case_compare(a->name, b->name);
}

static int ends_with_case(const char *value, const char *suffix)
{
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           case_compare(value + value_length - suffix_length, suffix) == 0;
}

const char *ts_browser_mode_extension(TsBrowserMode mode)
{
    if (mode == TS_BROWSER_SAVE_RECIPE) return ".tsr";
    if (mode == TS_BROWSER_SAVE_PRESET) return ".tsp";
    if (mode == TS_BROWSER_EXPORT_BANK) return "";
    if (ts_browser_mode_selects_config(mode)) return "";
    return ".wav";
}

const char *ts_browser_mode_title(TsBrowserMode mode)
{
    if (mode == TS_BROWSER_LOAD_WAV) return "LOAD WAV, TSR, OR TSP";
    if (mode == TS_BROWSER_SAVE_RECIPE) return "SAVE TSR PROJECT";
    if (mode == TS_BROWSER_SAVE_PRESET) return "SAVE PROCESS RECIPE";
    if (mode == TS_BROWSER_EXPORT_WAV) return "EXPORT CURRENT WAV";
    if (mode == TS_BROWSER_EXPORT_BANK) return "EXPORT SOUND COLLECTION";
    if (mode == TS_BROWSER_SELECT_SAMPLE_DIRECTORY) return "SELECT SAMPLE FOLDER";
    if (mode == TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE)
        return "SELECT FASTTRACKER EXECUTABLE";
    if (mode == TS_BROWSER_SELECT_EXCHANGE_DIRECTORY) return "SELECT FT2 EXCHANGE FOLDER";
    return "FILE BROWSER";
}

int ts_browser_mode_edits_filename(TsBrowserMode mode)
{
    return mode == TS_BROWSER_SAVE_RECIPE || mode == TS_BROWSER_SAVE_PRESET ||
           mode == TS_BROWSER_EXPORT_WAV || mode == TS_BROWSER_EXPORT_BANK;
}

int ts_browser_mode_selects_config(TsBrowserMode mode)
{
    return mode == TS_BROWSER_SELECT_SAMPLE_DIRECTORY ||
           mode == TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE ||
           mode == TS_BROWSER_SELECT_EXCHANGE_DIRECTORY;
}

int ts_browser_mode_selects_directory(TsBrowserMode mode)
{
    return mode == TS_BROWSER_SELECT_SAMPLE_DIRECTORY ||
           mode == TS_BROWSER_SELECT_EXCHANGE_DIRECTORY;
}

static int join_path(const char *directory, const char *name, char *path, size_t path_size)
{
    int written;
    if (directory == NULL || name == NULL || path == NULL || path_size == 0) return 0;
    written = snprintf(path, path_size, strcmp(directory, "/") == 0 ? "/%s" : "%s/%s",
                       directory, name);
    return written >= 0 && (size_t)written < path_size;
}

static int path_is_directory(const char *path)
{
#ifdef _WIN32
    struct _stat info;
    return _stat(path, &info) == 0 && (info.st_mode & _S_IFDIR) != 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

int ts_browser_path_exists(const char *path)
{
#ifdef _WIN32
    struct _stat info;
    return path != NULL && _stat(path, &info) == 0;
#else
    struct stat info;
    return path != NULL && stat(path, &info) == 0;
#endif
}

static int mode_accepts(const TsBrowser *browser, const char *name)
{
    if (browser->mode == TS_BROWSER_LOAD_WAV)
        return ends_with_case(name, ".wav") || ends_with_case(name, ".tsr") ||
               ends_with_case(name, ".tsp");
    if (browser->mode == TS_BROWSER_EXPORT_BANK) return 0;
    if (ts_browser_mode_selects_directory(browser->mode)) return 0;
    if (browser->mode == TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE) return 1;
    return ends_with_case(name, ts_browser_mode_extension(browser->mode));
}

static int maximum_scroll(const TsBrowser *browser)
{
    int maximum = browser->entry_count - TS_BROWSER_VISIBLE_ROWS;
    return maximum > 0 ? maximum : 0;
}

void ts_browser_set_scroll(TsBrowser *browser, int first_row)
{
    int maximum = maximum_scroll(browser);
    if (first_row < 0) first_row = 0;
    if (first_row > maximum) first_row = maximum;
    browser->scroll = first_row;
}

static void reveal_selection(TsBrowser *browser)
{
    if (browser->selected < browser->scroll) browser->scroll = browser->selected;
    if (browser->selected >= browser->scroll + TS_BROWSER_VISIBLE_ROWS)
        browser->scroll = browser->selected - TS_BROWSER_VISIBLE_ROWS + 1;
    ts_browser_set_scroll(browser, browser->scroll);
}

void ts_browser_init(TsBrowser *browser)
{
    memset(browser, 0, sizeof(*browser));
    browser->selected = -1;
    if (getcwd(browser->directory, sizeof(browser->directory)) == NULL)
        snprintf(browser->directory, sizeof(browser->directory), ".");
}

int ts_browser_refresh(TsBrowser *browser)
{
    char previous[TS_BROWSER_NAME_MAX + 1] = "";
    if (browser->selected >= 0 && browser->selected < browser->entry_count)
        snprintf(previous, sizeof(previous), "%s", browser->entries[browser->selected].name);
#ifdef _WIN32
    {
        WIN32_FIND_DATAA item;
        HANDLE search;
        char pattern[TS_BROWSER_PATH_MAX + 4];
        int written = snprintf(pattern, sizeof(pattern), "%s/*", browser->directory);
        if (written < 0 || (size_t)written >= sizeof(pattern)) {
            snprintf(browser->message, sizeof(browser->message), "DIRECTORY PATH IS TOO LONG");
            return 0;
        }
        search = FindFirstFileA(pattern, &item);
        if (search == INVALID_HANDLE_VALUE) {
            snprintf(browser->message, sizeof(browser->message), "COULD NOT OPEN DIRECTORY");
            return 0;
        }
        browser->entry_count = 0;
        do {
            TsBrowserEntry *entry;
            int is_directory;
            if (item.cFileName[0] == '.' || (item.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
                continue;
            is_directory = (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!is_directory && !mode_accepts(browser, item.cFileName)) continue;
            entry = &browser->entries[browser->entry_count++];
            snprintf(entry->name, sizeof(entry->name), "%s", item.cFileName);
            entry->is_directory = is_directory;
        } while (browser->entry_count < TS_BROWSER_MAX_ENTRIES && FindNextFileA(search, &item));
        FindClose(search);
    }
#else
    {
        DIR *directory = opendir(browser->directory);
        struct dirent *item;
        if (directory == NULL) {
            snprintf(browser->message, sizeof(browser->message), "COULD NOT OPEN DIRECTORY: %.118s",
                     strerror(errno));
            return 0;
        }
        browser->entry_count = 0;
        while ((item = readdir(directory)) != NULL &&
               browser->entry_count < TS_BROWSER_MAX_ENTRIES) {
            TsBrowserEntry *entry;
            char path[TS_BROWSER_PATH_MAX];
            if (item->d_name[0] == '.') continue;
            if (!join_path(browser->directory, item->d_name, path, sizeof(path))) continue;
            if (!path_is_directory(path) && !mode_accepts(browser, item->d_name)) continue;
            entry = &browser->entries[browser->entry_count++];
            snprintf(entry->name, sizeof(entry->name), "%s", item->d_name);
            entry->is_directory = path_is_directory(path);
        }
        closedir(directory);
    }
#endif
    qsort(browser->entries, (size_t)browser->entry_count, sizeof(browser->entries[0]),
          entry_compare);
    browser->selected = browser->entry_count > 0 ? 0 : -1;
    for (int i = 0; i < browser->entry_count; ++i)
        if (strcmp(browser->entries[i].name, previous) == 0) browser->selected = i;
    browser->scroll = 0;
    reveal_selection(browser);
    browser->overwrite_armed = 0;
    snprintf(browser->message, sizeof(browser->message), "%d ITEMS", browser->entry_count);
    return 1;
}

int ts_browser_open(TsBrowser *browser, TsBrowserMode mode, const char *default_filename)
{
    browser->mode = mode;
    browser->filename_focus = ts_browser_mode_edits_filename(mode);
    browser->dragging_scrollbar = 0;
    browser->overwrite_armed = 0;
    snprintf(browser->filename, sizeof(browser->filename), "%s",
             default_filename != NULL ? default_filename : "");
    browser->filename_cursor = strlen(browser->filename);
    return ts_browser_refresh(browser);
}

void ts_browser_close(TsBrowser *browser)
{
    browser->mode = TS_BROWSER_CLOSED;
    browser->dragging_scrollbar = 0;
    browser->overwrite_armed = 0;
}

void ts_browser_move_selection(TsBrowser *browser, int amount)
{
    if (browser->entry_count <= 0) return;
    browser->selected += amount;
    if (browser->selected < 0) browser->selected = 0;
    if (browser->selected >= browser->entry_count) browser->selected = browser->entry_count - 1;
    browser->overwrite_armed = 0;
    reveal_selection(browser);
}

void ts_browser_select(TsBrowser *browser, int index)
{
    if (index < 0 || index >= browser->entry_count) return;
    browser->selected = index;
    browser->overwrite_armed = 0;
    reveal_selection(browser);
    if (!browser->entries[index].is_directory)
        snprintf(browser->filename, sizeof(browser->filename), "%s",
                 browser->entries[index].name);
    browser->filename_cursor = strlen(browser->filename);
}

void ts_browser_scroll(TsBrowser *browser, int rows)
{
    ts_browser_set_scroll(browser, browser->scroll + rows);
}

int ts_browser_selected_path(const TsBrowser *browser, char *path, size_t path_size)
{
    if (browser->selected < 0 || browser->selected >= browser->entry_count) return 0;
    return join_path(browser->directory, browser->entries[browser->selected].name,
                     path, path_size);
}

int ts_browser_enter_selected_directory(TsBrowser *browser)
{
    char path[TS_BROWSER_PATH_MAX];
    if (browser->selected < 0 || browser->selected >= browser->entry_count ||
        !browser->entries[browser->selected].is_directory ||
        !ts_browser_selected_path(browser, path, sizeof(path))) return 0;
    snprintf(browser->directory, sizeof(browser->directory), "%s", path);
    return ts_browser_refresh(browser);
}

int ts_browser_parent(TsBrowser *browser)
{
    char parent[TS_BROWSER_PATH_MAX];
    char *slash;
    char *backslash;
    snprintf(parent, sizeof(parent), "%s", browser->directory);
    if (strcmp(parent, "/") == 0) return 0;
    if (strlen(parent) == 3 && parent[1] == ':' &&
        (parent[2] == '/' || parent[2] == '\\')) return 0;
    slash = strrchr(parent, '/');
    backslash = strrchr(parent, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash == NULL) snprintf(parent, sizeof(parent), ".");
    else if (slash == parent) slash[1] = '\0';
    else if (slash == parent + 2 && parent[1] == ':') slash[1] = '\0';
    else *slash = '\0';
    if (!path_is_directory(parent)) return 0;
    snprintf(browser->directory, sizeof(browser->directory), "%s", parent);
    return ts_browser_refresh(browser);
}

void ts_browser_set_filename(TsBrowser *browser, const char *filename)
{
    snprintf(browser->filename, sizeof(browser->filename), "%s",
             filename != NULL ? filename : "");
    browser->filename_cursor = strlen(browser->filename);
    browser->overwrite_armed = 0;
}

void ts_browser_append_filename(TsBrowser *browser, const char *text)
{
    size_t used = strlen(browser->filename);
    size_t cursor = browser->filename_cursor > used ? used : browser->filename_cursor;
    while (text != NULL && *text != '\0' && used < TS_BROWSER_NAME_MAX) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32u && c <= 126u && c != '/' && c != '\\') {
            memmove(browser->filename + cursor + 1u, browser->filename + cursor,
                    used - cursor + 1u);
            browser->filename[cursor++] = (char)c;
            ++used;
        }
    }
    browser->filename_cursor = cursor;
    browser->overwrite_armed = 0;
}

void ts_browser_backspace_filename(TsBrowser *browser)
{
    size_t length = strlen(browser->filename);
    size_t cursor = browser->filename_cursor > length ? length : browser->filename_cursor;
    if (cursor > 0) {
        memmove(browser->filename + cursor - 1u, browser->filename + cursor,
                length - cursor + 1u);
        browser->filename_cursor = cursor - 1u;
    }
    browser->overwrite_armed = 0;
}

void ts_browser_delete_filename(TsBrowser *browser)
{
    size_t length = strlen(browser->filename);
    size_t cursor = browser->filename_cursor > length ? length : browser->filename_cursor;
    if (cursor < length)
        memmove(browser->filename + cursor, browser->filename + cursor + 1u,
                length - cursor);
    browser->filename_cursor = cursor;
    browser->overwrite_armed = 0;
}

void ts_browser_move_filename_cursor(TsBrowser *browser, int amount)
{
    size_t length = strlen(browser->filename);
    ptrdiff_t position = (ptrdiff_t)(browser->filename_cursor > length ?
                         length : browser->filename_cursor) + amount;
    if (position < 0) position = 0;
    if ((size_t)position > length) position = (ptrdiff_t)length;
    browser->filename_cursor = (size_t)position;
}

void ts_browser_set_filename_cursor(TsBrowser *browser, size_t position)
{
    size_t length = strlen(browser->filename);
    browser->filename_cursor = position > length ? length : position;
}

int ts_browser_destination_path(const TsBrowser *browser, char *path, size_t path_size)
{
    char name[TS_BROWSER_NAME_MAX + 1];
    const char *extension;
    int written;
    if (browser->mode != TS_BROWSER_SAVE_RECIPE &&
        browser->mode != TS_BROWSER_SAVE_PRESET &&
        browser->mode != TS_BROWSER_EXPORT_WAV &&
        browser->mode != TS_BROWSER_EXPORT_BANK)
        return 0;
    if (browser->filename[0] == '\0') return 0;
    extension = ts_browser_mode_extension(browser->mode);
    if (extension[0] == '\0')
        snprintf(name, sizeof(name), "%s", browser->filename);
    else if (ends_with_case(browser->filename, extension))
        snprintf(name, sizeof(name), "%s", browser->filename);
    else {
        written = snprintf(name, sizeof(name), "%s%s", browser->filename, extension);
        if (written < 0 || (size_t)written >= sizeof(name)) return 0;
    }
    return join_path(browser->directory, name, path, path_size);
}
