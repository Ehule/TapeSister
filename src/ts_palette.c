#include "tapesister/palette.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGB(r,g,b) (0xff000000u | ((uint32_t)(r) << 16) | \
                    ((uint32_t)(g) << 8) | (uint32_t)(b))

static const char *const color_keys[TS_PALETTE_COLOR_COUNT] = {
    "PatternText", "BlockMark", "TextOnBlock", "Mouse", "Desktop", "Buttons",
    "PatternNote", "PatternInstrument", "PatternVolume", "PatternTuning",
    "PatternEffect", "PatternEmpty", "WaveSelection"
};

static const char *const color_names[TS_PALETTE_COLOR_COUNT] = {
    "TEXT", "SELECTION", "SELECTED TEXT", "MOUSE", "DESKTOP", "BUTTONS",
    "WAVE", "INSTRUMENT", "ZERO CROSS", "LOOP", "EFFECT", "EMPTY",
    "WAVE SELECTION"
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int equal_icase(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

static int parse_color(const char *text, uint32_t *color)
{
    char *end;
    unsigned long value;
    if (text[0] == '#') ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (strlen(text) != 6u) return 0;
    for (int i = 0; i < 6; ++i)
        if (!isxdigit((unsigned char)text[i])) return 0;
    errno = 0;
    value = strtoul(text, &end, 16);
    if (errno != 0 || *end != '\0' || value > 0xfffffful) return 0;
    *color = 0xff000000u | (uint32_t)value;
    return 1;
}

static int parse_contrast(const char *text, int *contrast)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    while (*end != '\0' && isspace((unsigned char)*end)) ++end;
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 100)
        return 0;
    *contrast = (int)value;
    return 1;
}

void ts_palette_default(TsPalette *palette)
{
    static const uint32_t defaults[TS_PALETTE_COLOR_COUNT] = {
        RGB(255, 28, 0), RGB(45, 0, 57), RGB(0, 158, 227), RGB(255, 210, 101),
        RGB(28, 28, 28), RGB(93, 85, 93), RGB(255, 231, 0), RGB(24, 255, 0),
        RGB(255, 28, 231), RGB(20, 125, 255), RGB(53, 255, 255), RGB(89, 0, 255),
        RGB(45, 0, 57)
    };
    if (palette == NULL) return;
    memcpy(palette->colors, defaults, sizeof(defaults));
    palette->desktop_contrast = 52;
    palette->buttons_contrast = 57;
}

const char *ts_palette_color_key(TsPaletteColor color)
{
    return color >= 0 && color < TS_PALETTE_COLOR_COUNT ? color_keys[color] : "Color";
}

const char *ts_palette_color_name(TsPaletteColor color)
{
    return color >= 0 && color < TS_PALETTE_COLOR_COUNT ? color_names[color] : "COLOR";
}

int ts_palette_load(TsPalette *palette, const char *path,
                    char *error, size_t error_size)
{
    FILE *file;
    TsPalette loaded;
    int found[TS_PALETTE_COLOR_COUNT] = {0};
    int in_palette = 1;
    int line_number = 0;
    char line[256];
    if (palette == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "Invalid palette destination");
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        if (error != NULL && error_size > 0)
            snprintf(error, error_size, "Could not open palette: %s", path);
        return 0;
    }
    ts_palette_default(&loaded);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = trim(line);
        char *equals;
        char *key;
        char *value;
        int recognized = 0;
        ++line_number;
        if (*text == '\0' || *text == ';' || *text == '#') continue;
        if (*text == '[') {
            char *close = strchr(text, ']');
            if (close == NULL) goto malformed;
            *close = '\0';
            in_palette = equal_icase(text + 1, "TapeheadPalette") ||
                         equal_icase(text + 1, "Palette");
            continue;
        }
        if (!in_palette) continue;
        equals = strchr(text, '=');
        if (equals == NULL) goto malformed;
        *equals = '\0';
        key = trim(text);
        value = trim(equals + 1);
        for (int color = 0; color < TS_PALETTE_COLOR_COUNT; ++color) {
            if (equal_icase(key, color_keys[color])) {
                recognized = 1;
                if (!parse_color(value, &loaded.colors[color])) goto malformed;
                found[color] = 1;
                break;
            }
        }
        if (!recognized && equal_icase(key, "DesktopContrast")) {
            recognized = 1;
            if (!parse_contrast(value, &loaded.desktop_contrast)) goto malformed;
        } else if (!recognized && equal_icase(key, "ButtonsContrast")) {
            recognized = 1;
            if (!parse_contrast(value, &loaded.buttons_contrast)) goto malformed;
        }
    }
    if (ferror(file)) {
        fclose(file);
        set_error(error, error_size, "Could not finish reading palette");
        return 0;
    }
    fclose(file);
    for (int color = 0; color < 6; ++color) {
        if (!found[color]) {
            if (error != NULL && error_size > 0)
                snprintf(error, error_size, "Palette missing %s", color_keys[color]);
            return 0;
        }
    }
    for (int color = 6; color < TS_PALETTE_WAVE_SELECTION; ++color)
        if (!found[color]) loaded.colors[color] = loaded.colors[TS_PALETTE_PATTERN_TEXT];
    if (!found[TS_PALETTE_WAVE_SELECTION])
        loaded.colors[TS_PALETTE_WAVE_SELECTION] = loaded.colors[TS_PALETTE_BLOCK_MARK];
    *palette = loaded;
    set_error(error, error_size, "");
    return 1;

malformed:
    fclose(file);
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "Malformed palette line %d", line_number);
    return 0;
}

static int save_palette(const TsPalette *palette, const char *path,
                        int include_wave_selection,
                        char *error, size_t error_size)
{
    FILE *file;
    int failed = 0;
    if (palette == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "Invalid palette source");
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        if (error != NULL && error_size > 0)
            snprintf(error, error_size, "Could not create palette: %s", path);
        return 0;
    }
    failed |= fprintf(file, include_wave_selection ?
                      "; TapeSister palette; Tapehead-compatible entries are unchanged.\n"
                      "; WaveSelection is TapeSister-only and is omitted by Export TH.\n\n"
                      "[TapeheadPalette]\n" :
                      "; Tapehead Edition compatible palette\n"
                      "; Exported by TapeSister without TapeSister-only entries.\n\n"
                      "[TapeheadPalette]\n") < 0;
    for (int color = 0; color < (include_wave_selection ?
                                TS_PALETTE_COLOR_COUNT : TS_PALETTE_WAVE_SELECTION);
         ++color) {
        uint32_t value = palette->colors[color];
        failed |= fprintf(file, "%s=#%02X%02X%02X\n", color_keys[color],
                          (unsigned)((value >> 16) & 0xffu),
                          (unsigned)((value >> 8) & 0xffu),
                          (unsigned)(value & 0xffu)) < 0;
    }
    failed |= fprintf(file, "DesktopContrast=%d\nButtonsContrast=%d\n",
                      palette->desktop_contrast, palette->buttons_contrast) < 0;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        set_error(error, error_size, "Could not finish writing palette");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_palette_save(const TsPalette *palette, const char *path,
                    char *error, size_t error_size)
{
    return save_palette(palette, path, 1, error, error_size);
}

int ts_palette_save_tapehead(const TsPalette *palette, const char *path,
                             char *error, size_t error_size)
{
    return save_palette(palette, path, 0, error, error_size);
}

uint8_t ts_palette_component(const TsPalette *palette, TsPaletteColor color,
                             int component)
{
    uint32_t value;
    if (palette == NULL || color < 0 || color >= TS_PALETTE_COLOR_COUNT ||
        component < 0 || component > 2) return 0;
    value = palette->colors[color];
    return (uint8_t)(value >> (16 - component * 8));
}

void ts_palette_set_component(TsPalette *palette, TsPaletteColor color,
                              int component, uint8_t value)
{
    unsigned shift;
    uint32_t mask;
    if (palette == NULL || color < 0 || color >= TS_PALETTE_COLOR_COUNT ||
        component < 0 || component > 2) return;
    shift = (unsigned)(16 - component * 8);
    mask = 0xffu << shift;
    palette->colors[color] = (palette->colors[color] & ~mask) |
                             ((uint32_t)value << shift) | 0xff000000u;
}
