#ifndef TAPESISTER_UI_H
#define TAPESISTER_UI_H

#include <stdint.h>
#include "tapesister/sample.h"

enum { TS_UI_WIDTH = 640, TS_UI_HEIGHT = 400 };
enum { TS_WAVE_X = 20, TS_WAVE_Y = 64, TS_WAVE_W = 600, TS_WAVE_H = 134 };

typedef enum {
    TS_FX_NOISE = 0,
    TS_FX_DELAY,
    TS_FX_SPACE
} TsFxPage;

typedef struct {
    uint32_t pixels[TS_UI_WIDTH * TS_UI_HEIGHT];
} TsFramebuffer;

typedef struct {
    int active_key;
    int path_entry;
    int selecting;
    int commit_armed;
    TsFxPage fx_page;
    size_t selection_anchor;
    char path[256];
    char status[160];
} TsUiState;

void ts_ui_init(TsUiState *ui);
void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument);
int ts_ui_write_ppm(const TsFramebuffer *fb, const char *path);
int ts_ui_key_from_point(int x, int y);

#endif
