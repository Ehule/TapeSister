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
    TS_PALETTE_WAVE_SELECTION,
    TS_PALETTE_ACTIVE_TILE,
    TS_PALETTE_STEREO_WAVE_LEFT,
    TS_PALETTE_STEREO_WAVE_RIGHT,
    TS_PALETTE_STEREO_WAVE_SUM,
    TS_PALETTE_SISTER_SOURCE_HORIZONTAL,
    TS_PALETTE_SISTER_SOURCE_VERTICAL,
    TS_PALETTE_TRACK_LENGTH_PLAYHEAD,
    TS_PALETTE_FASTTRACKS_PLAYHEAD,
    TS_PALETTE_CONTROL_PLAYHEAD,
    TS_PALETTE_FASTTRACKS_SYNC,
    TS_PALETTE_FASTTRACKS_PHASE,
    TS_PALETTE_FASTTRACKS_SONG,
    TS_PALETTE_FASTTRACKS_LENGTH_PLAYHEAD,
    TS_PALETTE_COLOR_COUNT
} TsPaletteColor;

enum {
    TS_PALETTE_TAPESISTER_COLOR_COUNT = TS_PALETTE_SISTER_SOURCE_VERTICAL + 1,
    TS_PALETTE_TAPEHEAD_COLOR_COUNT = 19
};

typedef struct {
    uint32_t colors[TS_PALETTE_COLOR_COUNT];
    uint32_t defined_colors;
    int desktop_contrast;
    int buttons_contrast;
} TsPalette;

void ts_palette_default(TsPalette *palette);
const char *ts_palette_color_key(TsPaletteColor color);
const char *ts_palette_color_name(TsPaletteColor color);
int ts_palette_load(TsPalette *palette, const char *path,
                    char *error, size_t error_size);
int ts_palette_load_first(TsPalette *palette,
                          const char *const *paths, size_t path_count,
                          char *loaded_path, size_t loaded_path_size,
                          char *error, size_t error_size);
int ts_palette_save(const TsPalette *palette, const char *path,
                    char *error, size_t error_size);
/* Kept for source compatibility. Universal saves now retain both apps' keys. */
int ts_palette_save_tapehead(const TsPalette *palette, const char *path,
                             char *error, size_t error_size);
int ts_palette_color_is_defined(const TsPalette *palette, TsPaletteColor color);
int ts_palette_tapehead_swatch_count(void);
TsPaletteColor ts_palette_tapehead_swatch_color(int swatch);
const char *ts_palette_tapehead_swatch_name(int swatch);
int ts_palette_sample_tapehead_from(TsPalette *destination_palette,
                                    const TsPalette *source_palette,
                                    TsPaletteColor destination, int swatch);
int ts_palette_sample_tapehead(TsPalette *palette, TsPaletteColor destination,
                               int swatch);
uint8_t ts_palette_component(const TsPalette *palette, TsPaletteColor color,
                             int component);
void ts_palette_set_component(TsPalette *palette, TsPaletteColor color,
                              int component, uint8_t value);

#endif
