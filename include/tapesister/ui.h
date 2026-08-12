#ifndef TAPESISTER_UI_H
#define TAPESISTER_UI_H

#include <stdint.h>
#include "tapesister/sample.h"

enum { TS_UI_WIDTH = 640, TS_UI_HEIGHT = 400 };

typedef struct {
    uint32_t pixels[TS_UI_WIDTH * TS_UI_HEIGHT];
} TsFramebuffer;

typedef struct {
    TsRecipe recipe;
    int active_key;
    int path_entry;
    char path[256];
    char status[160];
} TsUiState;

void ts_ui_init(TsUiState *ui);
void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsSample *sample);
int ts_ui_write_ppm(const TsFramebuffer *fb, const char *path);
int ts_ui_key_from_point(int x, int y);

#endif
