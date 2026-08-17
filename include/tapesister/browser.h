#ifndef TAPESISTER_BROWSER_H
#define TAPESISTER_BROWSER_H

#include <stddef.h>

enum {
    TS_BROWSER_MAX_ENTRIES = 1024,
    TS_BROWSER_NAME_MAX = 255,
    TS_BROWSER_PATH_MAX = 1024,
    TS_BROWSER_VISIBLE_ROWS = 11
};

typedef enum {
    TS_BROWSER_CLOSED = 0,
    TS_BROWSER_LOAD_WAV,
    TS_BROWSER_SAVE_RECIPE,
    TS_BROWSER_SAVE_PRESET,
    TS_BROWSER_EXPORT_WAV,
    TS_BROWSER_EXPORT_BANK,
    TS_BROWSER_SELECT_SAMPLE_DIRECTORY,
    TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE,
    TS_BROWSER_SELECT_EXCHANGE_DIRECTORY,
    TS_BROWSER_SELECT_CDP_BIN_DIRECTORY
} TsBrowserMode;

typedef struct {
    char name[TS_BROWSER_NAME_MAX + 1];
    int is_directory;
} TsBrowserEntry;

typedef struct {
    TsBrowserMode mode;
    char directory[TS_BROWSER_PATH_MAX];
    char filename[TS_BROWSER_NAME_MAX + 1];
    TsBrowserEntry entries[TS_BROWSER_MAX_ENTRIES];
    int entry_count;
    int selected;
    int scroll;
    int filename_focus;
    size_t filename_cursor;
    int overwrite_armed;
    int dragging_scrollbar;
    int scrollbar_drag_offset;
    char message[160];
} TsBrowser;

void ts_browser_init(TsBrowser *browser);
int ts_browser_open(TsBrowser *browser, TsBrowserMode mode, const char *default_filename);
void ts_browser_close(TsBrowser *browser);
int ts_browser_refresh(TsBrowser *browser);
int ts_browser_parent(TsBrowser *browser);
int ts_browser_enter_selected_directory(TsBrowser *browser);
void ts_browser_move_selection(TsBrowser *browser, int amount);
void ts_browser_select(TsBrowser *browser, int index);
void ts_browser_scroll(TsBrowser *browser, int rows);
void ts_browser_set_scroll(TsBrowser *browser, int first_row);
void ts_browser_set_filename(TsBrowser *browser, const char *filename);
void ts_browser_append_filename(TsBrowser *browser, const char *text);
void ts_browser_backspace_filename(TsBrowser *browser);
void ts_browser_delete_filename(TsBrowser *browser);
void ts_browser_move_filename_cursor(TsBrowser *browser, int amount);
void ts_browser_set_filename_cursor(TsBrowser *browser, size_t position);
int ts_browser_selected_path(const TsBrowser *browser, char *path, size_t path_size);
int ts_browser_destination_path(const TsBrowser *browser, char *path, size_t path_size);
int ts_browser_path_exists(const char *path);
const char *ts_browser_mode_title(TsBrowserMode mode);
const char *ts_browser_mode_extension(TsBrowserMode mode);
int ts_browser_mode_edits_filename(TsBrowserMode mode);
int ts_browser_mode_selects_config(TsBrowserMode mode);
int ts_browser_mode_selects_directory(TsBrowserMode mode);

#endif
