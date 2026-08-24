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
    "PatternEffect", "PatternEmpty", "WaveSelection", "ActiveTile",
    "StereoWaveLeft", "StereoWaveRight", "StereoWaveSum",
    "TrackLengthPlayhead", "FastTracksPlayhead", "ControlPlayhead",
    "FastTracksSync", "FastTracksPhase", "FastTracksSong",
    "FastTracksLengthPlayhead"
};

static const char *const color_names[TS_PALETTE_COLOR_COUNT] = {
    "TITLE / TEXT", "ACTIVE CONTROL", "ACTIVE TEXT", "POINTER", "DESKTOP",
    "CONTROLS", "WAVEFORM", "PRIMARY", "EDGE / ZERO", "LOOP / DRIFT",
    "EFFECT", "SPARE", "WAVE SELECTION", "ACTIVE TILE", "STEREO WAVE LEFT",
    "STEREO WAVE RIGHT", "STEREO WAVE SUM", "LEN HEAD",
    "FASTTRACKS HEAD", "CONTROL HEAD", "FASTTRACKS SYNC",
    "FASTTRACKS PHASE", "FASTTRACKS SONG", "FASTTRACKS + LEN HEAD"
};

static const TsPaletteColor tapehead_swatch_colors[TS_PALETTE_TAPEHEAD_COLOR_COUNT] = {
    TS_PALETTE_PATTERN_TEXT, TS_PALETTE_BLOCK_MARK, TS_PALETTE_TEXT_ON_BLOCK,
    TS_PALETTE_MOUSE, TS_PALETTE_DESKTOP, TS_PALETTE_BUTTONS,
    TS_PALETTE_PATTERN_NOTE, TS_PALETTE_PATTERN_INSTRUMENT,
    TS_PALETTE_PATTERN_VOLUME, TS_PALETTE_PATTERN_TUNING,
    TS_PALETTE_PATTERN_EFFECT, TS_PALETTE_PATTERN_EMPTY,
    TS_PALETTE_TRACK_LENGTH_PLAYHEAD, TS_PALETTE_FASTTRACKS_PLAYHEAD,
    TS_PALETTE_CONTROL_PLAYHEAD, TS_PALETTE_FASTTRACKS_SYNC,
    TS_PALETTE_FASTTRACKS_PHASE, TS_PALETTE_FASTTRACKS_SONG,
    TS_PALETTE_FASTTRACKS_LENGTH_PLAYHEAD
};

static const char *const tapehead_swatch_names[TS_PALETTE_TAPEHEAD_COLOR_COUNT] = {
    "PAT TEXT", "BLOCK MARK", "BLOCK TEXT", "MOUSE", "DESKTOP", "BUTTONS",
    "PAT NOTE", "PAT INST", "PAT VOLUME", "PAT TUNING", "PAT EFFECT",
    "PAT EMPTY", "LEN HEAD", "FT HEAD", "CONTROL HEAD", "FT SYNC",
    "FT PHASE", "FT SONG", "FT + LEN"
};

static const TsPaletteColor universal_save_order[TS_PALETTE_COLOR_COUNT] = {
    TS_PALETTE_PATTERN_TEXT, TS_PALETTE_BLOCK_MARK, TS_PALETTE_TEXT_ON_BLOCK,
    TS_PALETTE_MOUSE, TS_PALETTE_DESKTOP, TS_PALETTE_BUTTONS,
    TS_PALETTE_PATTERN_NOTE, TS_PALETTE_PATTERN_INSTRUMENT,
    TS_PALETTE_PATTERN_VOLUME, TS_PALETTE_PATTERN_TUNING,
    TS_PALETTE_PATTERN_EFFECT, TS_PALETTE_PATTERN_EMPTY,
    TS_PALETTE_TRACK_LENGTH_PLAYHEAD, TS_PALETTE_FASTTRACKS_PLAYHEAD,
    TS_PALETTE_CONTROL_PLAYHEAD, TS_PALETTE_FASTTRACKS_SYNC,
    TS_PALETTE_FASTTRACKS_PHASE, TS_PALETTE_FASTTRACKS_SONG,
    TS_PALETTE_FASTTRACKS_LENGTH_PLAYHEAD, TS_PALETTE_WAVE_SELECTION,
    TS_PALETTE_ACTIVE_TILE, TS_PALETTE_STEREO_WAVE_LEFT,
    TS_PALETTE_STEREO_WAVE_RIGHT, TS_PALETTE_STEREO_WAVE_SUM
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
        RGB(45, 0, 57), RGB(255, 210, 101), RGB(255, 231, 0),
        RGB(53, 255, 255), RGB(24, 255, 0), RGB(65, 215, 255),
        RGB(255, 174, 32), RGB(255, 49, 49), RGB(0, 206, 65),
        RGB(255, 49, 49), RGB(255, 174, 32), RGB(208, 97, 255)
    };
    if (palette == NULL) return;
    memcpy(palette->colors, defaults, sizeof(defaults));
    palette->defined_colors = (1u << TS_PALETTE_COLOR_COUNT) - 1u;
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
    loaded.defined_colors = 0u;
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
                loaded.defined_colors |= 1u << color;
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
    if (!found[TS_PALETTE_ACTIVE_TILE])
        loaded.colors[TS_PALETTE_ACTIVE_TILE] = loaded.colors[TS_PALETTE_MOUSE];
    /* PR5 keys are optional so every existing palette remains valid. */
    if (!found[TS_PALETTE_STEREO_WAVE_LEFT])
        loaded.colors[TS_PALETTE_STEREO_WAVE_LEFT] =
            loaded.colors[TS_PALETTE_PATTERN_NOTE];
    if (!found[TS_PALETTE_STEREO_WAVE_RIGHT])
        loaded.colors[TS_PALETTE_STEREO_WAVE_RIGHT] =
            loaded.colors[TS_PALETTE_PATTERN_EFFECT];
    if (!found[TS_PALETTE_STEREO_WAVE_SUM])
        loaded.colors[TS_PALETTE_STEREO_WAVE_SUM] =
            loaded.colors[TS_PALETTE_PATTERN_INSTRUMENT];
    *palette = loaded;
    set_error(error, error_size, "");
    return 1;

malformed:
    fclose(file);
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "Malformed palette line %d", line_number);
    return 0;
}

int ts_palette_load_first(TsPalette *palette,
                          const char *const *paths, size_t path_count,
                          char *loaded_path, size_t loaded_path_size,
                          char *error, size_t error_size)
{
    char attempt_error[256];
    char first_error[256] = "No palette paths were available";
    int attempted = 0;
    if (loaded_path != NULL && loaded_path_size > 0u) loaded_path[0] = '\0';
    if (palette == NULL || paths == NULL || path_count == 0u) {
        set_error(error, error_size, "Invalid palette path list");
        return 0;
    }
    for (size_t candidate = 0; candidate < path_count; ++candidate) {
        int duplicate = 0;
        if (paths[candidate] == NULL || paths[candidate][0] == '\0') continue;
        for (size_t earlier = 0; earlier < candidate && !duplicate; ++earlier)
            duplicate = paths[earlier] != NULL &&
                        strcmp(paths[earlier], paths[candidate]) == 0;
        if (duplicate) continue;
        if (ts_palette_load(palette, paths[candidate], attempt_error,
                            sizeof(attempt_error))) {
            if (loaded_path != NULL && loaded_path_size > 0u)
                snprintf(loaded_path, loaded_path_size, "%s", paths[candidate]);
            set_error(error, error_size, "");
            return 1;
        }
        if (!attempted)
            snprintf(first_error, sizeof(first_error), "%s", attempt_error);
        attempted = 1;
    }
    set_error(error, error_size, first_error);
    return 0;
}

static int save_palette(const TsPalette *palette, const char *path,
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
    failed |= fprintf(file,
                      "; Shared TapeSister / Tapehead palette\n"
                      "; Both applications preserve every key in this file.\n\n"
                      "[Palette]\n") < 0;
    for (int index = 0; index < TS_PALETTE_COLOR_COUNT; ++index) {
        TsPaletteColor color = universal_save_order[index];
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
    return save_palette(palette, path, error, error_size);
}

int ts_palette_save_tapehead(const TsPalette *palette, const char *path,
                             char *error, size_t error_size)
{
    return save_palette(palette, path, error, error_size);
}

int ts_palette_color_is_defined(const TsPalette *palette, TsPaletteColor color)
{
    return palette != NULL && color >= 0 && color < TS_PALETTE_COLOR_COUNT &&
           (palette->defined_colors & (1u << color)) != 0u;
}

int ts_palette_tapehead_swatch_count(void)
{
    return TS_PALETTE_TAPEHEAD_COLOR_COUNT;
}

TsPaletteColor ts_palette_tapehead_swatch_color(int swatch)
{
    return swatch >= 0 && swatch < TS_PALETTE_TAPEHEAD_COLOR_COUNT ?
           tapehead_swatch_colors[swatch] : TS_PALETTE_COLOR_COUNT;
}

const char *ts_palette_tapehead_swatch_name(int swatch)
{
    return swatch >= 0 && swatch < TS_PALETTE_TAPEHEAD_COLOR_COUNT ?
           tapehead_swatch_names[swatch] : "TAPEHEAD COLOR";
}

int ts_palette_sample_tapehead_from(TsPalette *destination_palette,
                                    const TsPalette *source_palette,
                                    TsPaletteColor destination, int swatch)
{
    TsPaletteColor source = ts_palette_tapehead_swatch_color(swatch);
    if (destination_palette == NULL || source_palette == NULL ||
        destination < 0 ||
        (int)destination >= TS_PALETTE_TAPESISTER_COLOR_COUNT ||
        source < 0 || source >= TS_PALETTE_COLOR_COUNT ||
        !ts_palette_color_is_defined(source_palette, source)) return 0;
    destination_palette->colors[destination] = source_palette->colors[source];
    destination_palette->defined_colors |= 1u << destination;
    return 1;
}

int ts_palette_sample_tapehead(TsPalette *palette, TsPaletteColor destination,
                               int swatch)
{
    return ts_palette_sample_tapehead_from(palette, palette, destination, swatch);
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
    palette->defined_colors |= 1u << color;
}
