#ifndef TAPESISTER_PALETTE_H
#define TAPESISTER_PALETTE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TS_PALETTE_PATTERN_TEXT = 0,
    TS_PALETTE_BLOCK_MARK,
    TS_PALETTE_TEXT_ON_BLOCK,
    TS_PALETTE_MOUSE,
    TS_PALETTE_DESKTOP,
    TS_PALETTE_BUTTONS,
    TS_PALETTE_PATTERN_NOTE,
    TS_PALETTE_PATTERN_INSTRUMENT,
    TS_PALETTE_PATTERN_VOLUME,
    TS_PALETTE_PATTERN_TUNING,
    TS_PALETTE_PATTERN_EFFECT,
    TS_PALETTE_PATTERN_EMPTY,
    TS_PALETTE_COLOR_COUNT
} TsPaletteColor;

typedef struct {
    uint32_t colors[TS_PALETTE_COLOR_COUNT];
    int desktop_contrast;
    int buttons_contrast;
} TsPalette;

void ts_palette_default(TsPalette *palette);
const char *ts_palette_color_key(TsPaletteColor color);
const char *ts_palette_color_name(TsPaletteColor color);
int ts_palette_load(TsPalette *palette, const char *path,
                    char *error, size_t error_size);
int ts_palette_save(const TsPalette *palette, const char *path,
                    char *error, size_t error_size);
uint8_t ts_palette_component(const TsPalette *palette, TsPaletteColor color,
                             int component);
void ts_palette_set_component(TsPalette *palette, TsPaletteColor color,
                              int component, uint8_t value);

#endif
