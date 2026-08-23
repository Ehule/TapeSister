#include "tapesister/palette.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    static const char path[] = "test-sister-palette.pal";
    static const char legacy[] = "test-sister-palette-legacy.pal";
    TsPalette saved;
    TsPalette loaded;
    char error[160];
    FILE *file;
    ts_palette_default(&saved);
    saved.colors[TS_PALETTE_STEREO_WAVE_LEFT] = 0xff123456u;
    saved.colors[TS_PALETTE_STEREO_WAVE_RIGHT] = 0xffabcdefu;
    saved.colors[TS_PALETTE_STEREO_WAVE_SUM] = 0xff102030u;
    CHECK(ts_palette_save(&saved, path, error, sizeof(error)));
    CHECK(ts_palette_load(&loaded, path, error, sizeof(error)));
    CHECK(loaded.colors[TS_PALETTE_STEREO_WAVE_LEFT] == 0xff123456u);
    CHECK(loaded.colors[TS_PALETTE_STEREO_WAVE_RIGHT] == 0xffabcdefu);
    CHECK(loaded.colors[TS_PALETTE_STEREO_WAVE_SUM] == 0xff102030u);
    file = fopen(legacy, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("[Palette]\nPatternText=#FF0000\nBlockMark=#010203\n"
              "TextOnBlock=#040506\nMouse=#070809\nDesktop=#101112\n"
              "Buttons=#131415\nPatternNote=#202122\n"
              "PatternInstrument=#303132\nPatternEffect=#404142\n", file);
        fclose(file);
        CHECK(ts_palette_load(&loaded, legacy, error, sizeof(error)));
        CHECK(loaded.colors[TS_PALETTE_STEREO_WAVE_LEFT] == 0xff202122u);
        CHECK(loaded.colors[TS_PALETTE_STEREO_WAVE_RIGHT] == 0xff404142u);
        CHECK(loaded.colors[TS_PALETTE_STEREO_WAVE_SUM] == 0xff303132u);
    }
    remove(path);
    remove(legacy);
    puts("Sister palette tests passed");
    return failures != 0;
}
