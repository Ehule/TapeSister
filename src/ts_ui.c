#include "tapesister/ui.h"
#include "tapesister/sister_ui.h"
#include "tapesister/waveform_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGB(r,g,b) (0xff000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static const TsPalette *render_palette;
static TsWaveformCache waveform_caches[TS_UI_WAVEFORM_COUNT];
static int waveform_caches_initialized;

static TsWaveformCache *waveform_cache(TsUiWaveformKind kind)
{
    if (!waveform_caches_initialized) {
        for (int i = 0; i < TS_UI_WAVEFORM_COUNT; ++i)
            ts_waveform_cache_init(&waveform_caches[i]);
        waveform_caches_initialized = 1;
    }
    return kind >= 0 && kind < TS_UI_WAVEFORM_COUNT ?
           &waveform_caches[kind] : NULL;
}

void ts_ui_waveform_cache_invalidate(TsUiState *ui, TsUiWaveformKind kind)
{
    if (ui == NULL || kind < 0 || kind >= TS_UI_WAVEFORM_COUNT) return;
    ++ui->waveform_revisions[kind];
    if (ui->waveform_revisions[kind] == 0u)
        ui->waveform_revisions[kind] = 1u;
}

void ts_ui_waveform_cache_reset_counters(void)
{
    for (int i = 0; i < TS_UI_WAVEFORM_COUNT; ++i) {
        TsWaveformCache *cache = waveform_cache((TsUiWaveformKind)i);
        ts_waveform_cache_init(cache);
    }
}

uint64_t ts_ui_waveform_cache_rebuild_count(TsUiWaveformKind kind)
{
    TsWaveformCache *cache = waveform_cache(kind);
    return cache != NULL ? cache->rebuild_count : 0u;
}

static const TsPalette *active_palette(void)
{
    static TsPalette fallback;
    static int initialized;
    if (!initialized) {
        ts_palette_default(&fallback);
        initialized = 1;
    }
    return render_palette != NULL ? render_palette : &fallback;
}

#define PAL_TEXT (active_palette()->colors[TS_PALETTE_PATTERN_TEXT])
#define PAL_BLOCK (active_palette()->colors[TS_PALETTE_BLOCK_MARK])
#define PAL_BLOCK_TEXT (active_palette()->colors[TS_PALETTE_TEXT_ON_BLOCK])
#define PAL_MOUSE (active_palette()->colors[TS_PALETTE_MOUSE])
#define PAL_DESKTOP (active_palette()->colors[TS_PALETTE_DESKTOP])
#define PAL_BUTTON (active_palette()->colors[TS_PALETTE_BUTTONS])
#define PAL_NOTE (active_palette()->colors[TS_PALETTE_PATTERN_NOTE])
#define PAL_INSTRUMENT (active_palette()->colors[TS_PALETTE_PATTERN_INSTRUMENT])
#define PAL_VOLUME (active_palette()->colors[TS_PALETTE_PATTERN_VOLUME])
#define PAL_TUNING (active_palette()->colors[TS_PALETTE_PATTERN_TUNING])
#define PAL_EFFECT (active_palette()->colors[TS_PALETTE_PATTERN_EFFECT])
#define PAL_WAVE_SELECTION (active_palette()->colors[TS_PALETTE_WAVE_SELECTION])
#define PAL_ACTIVE_TILE (active_palette()->colors[TS_PALETTE_ACTIVE_TILE])
#define PAL_WAVE_LEFT (active_palette()->colors[TS_PALETTE_STEREO_WAVE_LEFT])
#define PAL_WAVE_RIGHT (active_palette()->colors[TS_PALETTE_STEREO_WAVE_RIGHT])
#define PAL_WAVE_SUM (active_palette()->colors[TS_PALETTE_STEREO_WAVE_SUM])

int ts_ui_logo_contains(int x, int y)
{
    return x >= 0 && x < 160 && y >= 0 && y < 32;
}

int ts_ui_fm_background_click_allowed(const TsUiState *ui, int x, int y)
{
    if (ui == NULL || !ui->fm_open || ui->fm_bank_choice_open ||
        ui->fm_full_choice_open)
        return 0;
    return ts_ui_logo_contains(x, y) ||
           (!ui->show_keyboard && !ui->show_recipes &&
            !ui->show_ingredients && ts_ui_bank_slot_from_point(x, y) >= 0);
}

int ts_ui_wheel_guard_accept(TsUiWheelGuard *guard, int target,
                             uint32_t now_ms)
{
    uint32_t elapsed;
    if (guard == NULL || target < 0) return 0;
    if (!guard->active) {
        guard->target = target;
        guard->last_event_ms = now_ms;
        guard->active = 1;
        return 1;
    }
    elapsed = now_ms - guard->last_event_ms;
    if (target != guard->target && elapsed < TS_UI_WHEEL_HANDOFF_QUIET_MS) {
        /* Ongoing inertial events keep extending the required quiet period. */
        guard->last_event_ms = now_ms;
        return 0;
    }
    guard->target = target;
    guard->last_event_ms = now_ms;
    return 1;
}

int ts_ui_waveform_mode_contains(int x, int y)
{
    return x >= 526 && x < 620 && y >= 43 && y < 60;
}

typedef struct {
    int x;
    int width;
    const char *label;
} TsPanelButton;

static const TsPanelButton config_buttons[] = {
    {20, 96, "SAVE CONFIG"}, {121, 76, "USE CWD"},
    {202, 76, "PALETTE"}, {283, 68, "CANCEL"}
};

static const TsPanelButton palette_buttons[] = {
    {20, 96, "LOAD SHARED"}, {121, 96, "SAVE SHARED"},
    {222, 61, "RESET"}, {288, 54, "DONE"}, {347, 66, "CANCEL"}
};

typedef struct {
    TsUiWaveAction action;
    int x;
    int width;
    const char *label;
} TsWaveButton;

static const TsWaveButton wave_buttons[] = {
    {TS_UI_WAVE_ACTION_PLAY_ALL, 10, 58, "PLAY ALL"},
    {TS_UI_WAVE_ACTION_PLAY_SELECTION, 72, 58, "PLAY SEL"},
    {TS_UI_WAVE_ACTION_PLAY_VIEW, 134, 64, "PLAY VIEW"},
    {TS_UI_WAVE_ACTION_CROP, 202, 38, "CROP"},
    {TS_UI_WAVE_ACTION_ZOOM_SELECTION, 244, 58, "ZOOM SEL"},
    {TS_UI_WAVE_ACTION_SELECT_ALL, 306, 52, "SEL ALL"},
    {TS_UI_WAVE_ACTION_SELECT_WAVE, 362, 58, "SEL WAVE"},
    {TS_UI_WAVE_ACTION_SHOW_ALL, 424, 58, "SHOW ALL"},
    {TS_UI_WAVE_ACTION_CLEAR_ALL, 486, 97, "CLEAR ALL"},
    {TS_UI_WAVE_ACTION_CYCLE_PANEL, 588, 42, "BANK"}
};

enum {
    TS_CANVAS_CONTROLS_Y = TS_WAVE_Y + TS_WAVE_H - 19,
    TS_CANVAS_CONTROLS_H = 17,
    TS_CANVAS_LEFT_HANDLE_X = TS_WAVE_X + 3,
    TS_CANVAS_RIGHT_HANDLE_X = TS_WAVE_X + TS_WAVE_W - 11,
    TS_CANVAS_HANDLE_Y = TS_WAVE_Y + 48,
    TS_CANVAS_HANDLE_W = 8,
    TS_CANVAS_HANDLE_H = 36
};

enum {
    TS_TUNE_MATERIAL_SEMI_DOWN_X = 10,
    TS_TUNE_MATERIAL_SEMI_UP_X = 66,
    TS_TUNE_MATERIAL_CENT_DOWN_X = 122,
    TS_TUNE_MATERIAL_CENT_UP_X = 168,
    TS_TUNE_REFERENCE_DOWN_X = 214,
    TS_TUNE_REFERENCE_NOTE_X = 252,
    TS_TUNE_REFERENCE_UP_X = 320,
    TS_TUNE_REFERENCE_FINE_X = 358,
    TS_TUNE_REFERENCE_TONE_X = 438,
    TS_TUNE_REFERENCE_VOLUME_X = 514,
    TS_TUNE_DETECT_X = 572
};

static void clear(TsFramebuffer *fb, uint32_t color)
{
    for (int i = 0; i < TS_UI_WIDTH * TS_UI_HEIGHT; ++i) fb->pixels[i] = color;
}

static void rect(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TS_UI_WIDTH) w = TS_UI_WIDTH - x;
    if (y + h > TS_UI_HEIGHT) h = TS_UI_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px) fb->pixels[py * TS_UI_WIDTH + px] = color;
}

void ts_ui_draw_tile_state_borders(TsFramebuffer *fb, int slot,
                                   int active, int sister_source,
                                   const TsPalette *palette)
{
    int x;
    int y;
    uint32_t active_color;
    if (fb == NULL || palette == NULL || slot < 0 ||
        slot >= TS_BANK_SLOT_COUNT || (!active && !sister_source)) return;
    x = 10 + (slot % 8) * 77;
    y = 330 + (slot / 8) * 25;
    active_color = palette->colors[TS_PALETTE_ACTIVE_TILE];
    if (sister_source) {
        uint32_t horizontal =
            palette->colors[TS_PALETTE_SISTER_SOURCE_HORIZONTAL];
        uint32_t vertical =
            palette->colors[TS_PALETTE_SISTER_SOURCE_VERTICAL];
        rect(fb, x - 2, y - 2, 76, 2, horizontal);
        rect(fb, x - 2, y + 23, 76, 2, horizontal);
        rect(fb, x - 2, y, 2, 23, vertical);
        rect(fb, x + 72, y, 2, 23, vertical);
    }
    if (active && sister_source) {
        rect(fb, x + 1, y + 1, 70, 1, active_color);
        rect(fb, x + 1, y + 21, 70, 1, active_color);
        rect(fb, x + 1, y + 1, 1, 21, active_color);
        rect(fb, x + 70, y + 1, 1, 21, active_color);
    } else if (active) {
        rect(fb, x - 2, y - 2, 76, 3, active_color);
        rect(fb, x - 2, y + 22, 76, 3, active_color);
        rect(fb, x - 2, y - 2, 3, 27, active_color);
        rect(fb, x + 71, y - 2, 3, 27, active_color);
    }
}

static void wave_rect(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t color)
{
    if (x < TS_WAVE_X) { w -= TS_WAVE_X - x; x = TS_WAVE_X; }
    if (y < TS_WAVE_Y) { h -= TS_WAVE_Y - y; y = TS_WAVE_Y; }
    if (x + w > TS_WAVE_X + TS_WAVE_W) w = TS_WAVE_X + TS_WAVE_W - x;
    if (y + h > TS_WAVE_Y + TS_WAVE_H) h = TS_WAVE_Y + TS_WAVE_H - y;
    if (w > 0 && h > 0) rect(fb, x, y, w, h, color);
}

static uint32_t blend_color(uint32_t background, uint32_t foreground,
                            unsigned foreground_percent)
{
    unsigned background_percent;
    unsigned red;
    unsigned green;
    unsigned blue;
    if (foreground_percent > 100u) foreground_percent = 100u;
    background_percent = 100u - foreground_percent;
    red = (((background >> 16) & 0xffu) * background_percent +
           ((foreground >> 16) & 0xffu) * foreground_percent) / 100u;
    green = (((background >> 8) & 0xffu) * background_percent +
             ((foreground >> 8) & 0xffu) * foreground_percent) / 100u;
    blue = ((background & 0xffu) * background_percent +
            (foreground & 0xffu) * foreground_percent) / 100u;
    return RGB(red, green, blue);
}

static void wave_blend_rect(TsFramebuffer *fb, int x, int y, int w, int h,
                            uint32_t color, unsigned opacity_percent)
{
    if (x < TS_WAVE_X) { w -= TS_WAVE_X - x; x = TS_WAVE_X; }
    if (y < TS_WAVE_Y) { h -= TS_WAVE_Y - y; y = TS_WAVE_Y; }
    if (x + w > TS_WAVE_X + TS_WAVE_W) w = TS_WAVE_X + TS_WAVE_W - x;
    if (y + h > TS_WAVE_Y + TS_WAVE_H) h = TS_WAVE_Y + TS_WAVE_H - y;
    if (w <= 0 || h <= 0) return;
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px) {
            size_t at = (size_t)py * TS_UI_WIDTH + (size_t)px;
            fb->pixels[at] = blend_color(fb->pixels[at], color, opacity_percent);
        }
}

static void wave_line(TsFramebuffer *fb, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (x0 >= TS_WAVE_X && x0 < TS_WAVE_X + TS_WAVE_W &&
            y0 >= TS_WAVE_Y && y0 < TS_WAVE_Y + TS_WAVE_H)
            fb->pixels[y0 * TS_UI_WIDTH + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        {
            int twice = error * 2;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    }
}

static const char *glyph(char c)
{
    switch (c) {
    case 'A': return "01110100011000111111100011000110001";
    case 'B': return "11110100011000111110100011000111110";
    case 'C': return "01111100001000010000100001000001111";
    case 'D': return "11110100011000110001100011000111110";
    case 'E': return "11111100001000011110100001000011111";
    case 'F': return "11111100001000011110100001000010000";
    case 'G': return "01111100001000010111100011000101111";
    case 'H': return "10001100011000111111100011000110001";
    case 'I': return "11111001000010000100001000010011111";
    case 'J': return "00111000100001000010000101001001100";
    case 'K': return "10001100101010011000101001001010001";
    case 'L': return "10000100001000010000100001000011111";
    case 'M': return "10001110111010110101100011000110001";
    case 'N': return "10001110011010110011100011000110001";
    case 'O': return "01110100011000110001100011000101110";
    case 'P': return "11110100011000111110100001000010000";
    case 'Q': return "01110100011000110001101011001001101";
    case 'R': return "11110100011000111110101001001010001";
    case 'S': return "01111100001000001110000010000111110";
    case 'T': return "11111001000010000100001000010000100";
    case 'U': return "10001100011000110001100011000101110";
    case 'V': return "10001100011000110001100010101000100";
    case 'W': return "10001100011000110101101011010101010";
    case 'X': return "10001100010101000100010101000110001";
    case 'Y': return "10001100010101000100001000010000100";
    case 'Z': return "11111000010001000100010001000011111";
    case '0': return "01110100011001110101110011000101110";
    case '1': return "00100011000010000100001000010001110";
    case '2': return "01110100010000100010001000100011111";
    case '3': return "11110000010000101110000010000111110";
    case '4': return "00010001100101010010111110001000010";
    case '5': return "11111100001000011110000010000111110";
    case '6': return "01110100001000011110100011000101110";
    case '7': return "11111000010001000100010000100001000";
    case '8': return "01110100011000101110100011000101110";
    case '9': return "01110100011000101111000010000101110";
    case '.': return "00000000000000000000000000010000100";
    case ':': return "00000001000010000000001000010000000";
    case '-': return "00000000000000011111000000000000000";
    case '+': return "00000001000010011111001000010000000";
    case '<': return "00001000100100010000010000001000001";
    case '>': return "10000010000001000001000100100010000";
    case '#': return "01010010101111101010111110101000000";
    case '/': return "00001000100001000100010001000010000";
    case '\\': return "10000010000010000100000100000100001";
    case '~': return "00000000000100110110000000000000000";
    case '_': return "00000000000000000000000000000011111";
    default:  return "00000000000000000000000000000000000";
    }
}

static void text(TsFramebuffer *fb, int x, int y, const char *value, uint32_t color, int scale)
{
    for (; *value; ++value, x += 6 * scale) {
        char c = *value >= 'a' && *value <= 'z' ? (char)(*value - 32) : *value;
        const char *bits = glyph(c);
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx)
                if (bits[gy * 5 + gx] == '1')
                    rect(fb, x + gx * scale, y + gy * scale, scale, scale, color);
    }
}

static void wave_text(TsFramebuffer *fb, int x, int y, const char *value,
                      uint32_t color, int scale)
{
    for (; *value; ++value, x += 6 * scale) {
        char c = *value >= 'a' && *value <= 'z' ? (char)(*value - 32) : *value;
        const char *bits = glyph(c);
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx)
                if (bits[gy * 5 + gx] == '1')
                    wave_rect(fb, x + gx * scale, y + gy * scale,
                              scale, scale, color);
    }
}

static uint32_t contrast_color(uint32_t base, int contrast, float scale)
{
    float exponent;
    float multiplier;
    int red;
    int green;
    int blue;
    if (contrast < 1) contrast = 1;
    if (contrast > 100) contrast = 100;
    exponent = (float)contrast / 40.0f;
    multiplier = powf(scale, exponent);
    red = (int)((float)((base >> 16) & 0xffu) * multiplier + 0.5f);
    green = (int)((float)((base >> 8) & 0xffu) * multiplier + 0.5f);
    blue = (int)((float)(base & 0xffu) * multiplier + 0.5f);
    if (red > 255) red = 255;
    if (green > 255) green = 255;
    if (blue > 255) blue = 255;
    return RGB(red, green, blue);
}

static void bevel_frame(TsFramebuffer *fb, int x, int y, int w, int h,
                        uint32_t fill, uint32_t light, uint32_t dark)
{
    rect(fb, x, y, w, h, fill);
    rect(fb, x, y, w, 2, light);
    rect(fb, x, y, 2, h, light);
    rect(fb, x, y + h - 2, w, 2, dark);
    rect(fb, x + w - 2, y, 2, h, dark);
}

static void frame(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t fill, uint32_t light)
{
    uint32_t desktop_light = contrast_color(
        PAL_DESKTOP, active_palette()->desktop_contrast, 1.5f);
    uint32_t desktop_dark = contrast_color(
        PAL_DESKTOP, active_palette()->desktop_contrast, 0.5f);
    bevel_frame(fb, x, y, w, h, fill, light != 0 ? light : desktop_light,
                desktop_dark);
}

static void button(TsFramebuffer *fb, int x, int y, int w, const char *label, int active)
{
    uint32_t fill = active ? PAL_BLOCK : PAL_BUTTON;
    uint32_t light = contrast_color(
        PAL_BUTTON, active_palette()->buttons_contrast, 1.5f);
    uint32_t dark = contrast_color(
        PAL_BUTTON, active_palette()->buttons_contrast, 0.5f);
    bevel_frame(fb, x, y, w, 23, fill, light, dark);
    text(fb, x + 6, y + 8, label, active ? PAL_BLOCK_TEXT : RGB(245, 242, 235), 1);
}

static void mini_button(TsFramebuffer *fb, int x, int y, int w,
                        const char *label, int active)
{
    uint32_t fill = active ? PAL_BLOCK : PAL_BUTTON;
    uint32_t light = contrast_color(
        PAL_BUTTON, active_palette()->buttons_contrast, 1.5f);
    uint32_t dark = contrast_color(
        PAL_BUTTON, active_palette()->buttons_contrast, 0.5f);
    bevel_frame(fb, x, y, w, TS_CANVAS_CONTROLS_H, fill, light, dark);
    text(fb, x + 4, y + 5, label, active ? PAL_BLOCK_TEXT : PAL_TEXT, 1);
}

static void slider(TsFramebuffer *fb, int x, int y, int w, const char *label, float value,
                   uint32_t color)
{
    text(fb, x, y, label, RGB(222, 218, 214), 1);
    rect(fb, x, y + 13, w, 6, RGB(12, 12, 12));
    rect(fb, x + 1, y + 14, (int)((w - 2) * value), 4, color);
    int knob = x + (int)((w - 6) * value);
    rect(fb, knob, y + 10, 6, 12, PAL_MOUSE);
}

static void bipolar_slider(TsFramebuffer *fb, int x, int y, int w,
                           const char *label, float value, uint32_t color)
{
    int center;
    int knob;
    int fill_first;
    int fill_last;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    text(fb, x, y, label, RGB(222, 218, 214), 1);
    rect(fb, x, y + 13, w, 6, RGB(12, 12, 12));
    center = x + (w - 6) / 2;
    knob = x + (int)lrintf((float)(w - 6) * (value + 1.0f) * 0.5f);
    fill_first = knob < center ? knob + 3 : center + 3;
    fill_last = knob < center ? center + 3 : knob + 3;
    if (fill_last > fill_first)
        rect(fb, fill_first, y + 14, fill_last - fill_first, 4, color);
    rect(fb, center + 2, y + 12, 2, 8, color);
    rect(fb, knob, y + 10, 6, 12, PAL_MOUSE);
}

static void compact_slider(TsFramebuffer *fb, int x, int y, int w,
                           const char *label, float value, int numeric,
                           uint32_t color, int active)
{
    char shown[8];
    int track_x = x + 54;
    int track_w = w - 78;
    int knob;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (active) rect(fb, x - 4, y + 1, 3, 11, PAL_MOUSE);
    text(fb, x, y + 3, label, active ? PAL_MOUSE : RGB(222, 218, 214), 1);
    rect(fb, track_x, y + 5, track_w, 4, RGB(8, 8, 8));
    rect(fb, track_x + 1, y + 6, (int)((track_w - 2) * value), 2, color);
    knob = track_x + (int)((track_w - 4) * value);
    rect(fb, knob, y + 2, 4, 10, PAL_MOUSE);
    snprintf(shown, sizeof(shown), "%3d", numeric);
    text(fb, x + w - 20, y + 3, shown, active ? PAL_MOUSE : PAL_EFFECT, 1);
}

static void browser_render(TsFramebuffer *fb, const TsBrowser *browser,
                           int cursor_visible, int file_busy)
{
    char shown[96];
    char footer[40];
    const char *directory = browser->directory;
    size_t directory_length = strlen(directory);
    frame(fb, 42, 34, 556, 342, RGB(36, 33, 37), PAL_MOUSE);
    rect(fb, 44, 36, 552, 28, RGB(12, 12, 12));
    text(fb, 56, 45, ts_browser_mode_title(browser->mode), PAL_NOTE, 1);
    if (directory_length > 73) directory += directory_length - 73;
    text(fb, 56, 70, directory, PAL_INSTRUMENT, 1);

    rect(fb, TS_BROWSER_LIST_X, TS_BROWSER_LIST_Y, TS_BROWSER_LIST_W,
         TS_BROWSER_SCROLL_H, RGB(8, 8, 8));
    for (int row = 0; row < TS_BROWSER_VISIBLE_ROWS; ++row) {
        int index = browser->scroll + row;
        int y = TS_BROWSER_LIST_Y + row * TS_BROWSER_ROW_H;
        if (index >= browser->entry_count) break;
        if (index == browser->selected)
            rect(fb, TS_BROWSER_LIST_X + 1, y + 1, TS_BROWSER_LIST_W - 2,
                 TS_BROWSER_ROW_H - 1, PAL_BLOCK);
        snprintf(shown, sizeof(shown), browser->entries[index].is_directory ?
                 "[DIR] %.72s" : "      %.72s", browser->entries[index].name);
        text(fb, TS_BROWSER_LIST_X + 6, y + 6, shown,
             index == browser->selected ? PAL_BLOCK_TEXT :
             browser->entries[index].is_directory ? PAL_INSTRUMENT : PAL_TEXT, 1);
    }

    rect(fb, TS_BROWSER_SCROLL_X, TS_BROWSER_LIST_Y, TS_BROWSER_SCROLL_W,
         TS_BROWSER_SCROLL_H, RGB(16, 16, 16));
    {
        int thumb_h = browser->entry_count <= TS_BROWSER_VISIBLE_ROWS ? TS_BROWSER_SCROLL_H :
                      TS_BROWSER_SCROLL_H * TS_BROWSER_VISIBLE_ROWS / browser->entry_count;
        int maximum_scroll = browser->entry_count - TS_BROWSER_VISIBLE_ROWS;
        int travel;
        int thumb_y;
        if (thumb_h < 18) thumb_h = 18;
        travel = TS_BROWSER_SCROLL_H - thumb_h;
        thumb_y = TS_BROWSER_LIST_Y + (maximum_scroll > 0 ?
                  browser->scroll * travel / maximum_scroll : 0);
        frame(fb, TS_BROWSER_SCROLL_X + 2, thumb_y, TS_BROWSER_SCROLL_W - 4, thumb_h,
              PAL_BUTTON, PAL_MOUSE);
    }

    if (ts_browser_mode_edits_filename(browser->mode)) {
        const char *filename = browser->filename;
        size_t length = strlen(filename);
        size_t cursor = browser->filename_cursor > length ? length :
                        browser->filename_cursor;
        size_t first = length > 78 ? length - 78 : 0;
        text(fb, 58, 282,
             browser->creating_directory ? "NEW FOLDER NAME" :
             browser->mode == TS_BROWSER_EXPORT_BANK ? "COLLECTION FOLDER" : "FILENAME",
             PAL_EFFECT, 1);
        rect(fb, 58, 294, 518, 24, RGB(8, 8, 8));
        if (cursor < first) first = cursor;
        if (cursor > first + 78) first = cursor - 78;
        filename += first;
        snprintf(shown, sizeof(shown), "%.78s", filename);
        text(fb, 64, 303, shown, browser->filename_focus ? PAL_MOUSE : PAL_TEXT, 1);
        if (browser->filename_focus && cursor_visible) {
            int cursor_x = 64 + (int)(cursor - first) * 6;
            if (cursor_x > 572) cursor_x = 572;
            rect(fb, cursor_x, 301, 2, 11, PAL_MOUSE);
        }
    } else if (browser->mode == TS_BROWSER_LOAD_WAV) {
        text(fb, 58, 300, "SELECT AN EXISTING WAV, TSR, OR TSP", PAL_EFFECT, 1);
    } else if (ts_browser_mode_selects_directory(browser->mode)) {
        text(fb, 58, 300, "NAVIGATE, THEN USE THIS FOLDER", PAL_EFFECT, 1);
    } else {
        text(fb, 58, 300, "SELECT AN EXECUTABLE FILE", PAL_EFFECT, 1);
    }

    button(fb, 58, 326, 72, "UP DIR", 0);
    if (ts_browser_mode_allows_create_directory(browser->mode))
        button(fb, 135, 326, 84,
               browser->creating_directory ? "BACK" : "NEW DIR",
               browser->creating_directory);
    button(fb, 224, 326, 120,
           file_busy ? "PLEASE WAIT" :
           browser->overwrite_armed ? "OVERWRITE?" :
           browser->creating_directory ? "CREATE" :
           ts_browser_mode_selects_directory(browser->mode) ? "USE FOLDER" :
           browser->mode == TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE ? "USE FILE" :
           browser->mode == TS_BROWSER_LOAD_WAV ? "OPEN" :
           (browser->mode == TS_BROWSER_SAVE_RECIPE ||
            browser->mode == TS_BROWSER_SAVE_PRESET) ? "SAVE" : "EXPORT",
           file_busy || browser->overwrite_armed || browser->creating_directory);
    button(fb, 349, 326, 84, "CANCEL", 0);
    snprintf(footer, sizeof(footer), "%.24s", browser->overwrite_armed ?
             "CONFIRM FILE OVERWRITE" : browser->message);
    text(fb, 441, 334, footer,
         browser->overwrite_armed ? PAL_VOLUME : RGB(190, 185, 190), 1);
}

static void config_render(TsFramebuffer *fb, const TsUiState *ui)
{
    frame(fb, TS_MODAL_PANEL_X, TS_MODAL_PANEL_Y,
          TS_MODAL_PANEL_W, TS_MODAL_PANEL_H + 24, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 20, 45, "CONFIGURATION", PAL_NOTE, 1);
    text(fb, 442, 45, "BLANK OK / DBL-CLICK BROWSE", PAL_EFFECT, 1);
    for (int i = 0; i < TS_CONFIG_FIELD_COUNT; ++i) {
        const char *value = ts_config_field_const(&ui->config, (TsConfigField)i);
        size_t length = strlen(value);
        size_t cursor = i == (int)ui->config_field ? ui->config_cursor : length;
        size_t first = length > 96u ? length - 96u : 0u;
        int y = TS_CONFIG_FIELD_Y + i * TS_CONFIG_FIELD_STEP_Y;
        char shown[97];
        if (cursor > length) cursor = length;
        if (cursor < first) first = cursor;
        if (cursor > first + 96u) first = cursor - 96u;
        snprintf(shown, sizeof(shown), "%.96s", value + first);
        text(fb, TS_CONFIG_FIELD_X, y - 9,
             ts_config_field_name((TsConfigField)i),
             i == (int)ui->config_field ? PAL_EFFECT : RGB(190, 185, 190), 1);
        rect(fb, TS_CONFIG_FIELD_X, y,
             TS_CONFIG_FIELD_W, TS_CONFIG_FIELD_H, RGB(8, 8, 8));
        text(fb, TS_CONFIG_FIELD_X + 6, y + 6, shown,
             i == (int)ui->config_field ? PAL_MOUSE : PAL_INSTRUMENT, 1);
        if (i == (int)ui->config_field && ui->text_cursor_visible)
            rect(fb, TS_CONFIG_FIELD_X + 6 + (int)(cursor - first) * 6,
                 y + 3, 2, 13, PAL_MOUSE);
    }
    for (size_t i = 0; i < sizeof(config_buttons) / sizeof(config_buttons[0]); ++i)
        button(fb, config_buttons[i].x, TS_CONFIG_ACTION_Y,
               config_buttons[i].width, config_buttons[i].label, 0);
    text(fb, 364, 220, "TAB FIELD  CTRL BACKSPACE CLEAR", PAL_TUNING, 1);
}

static int drone_crossfade_range(const TsUiState *ui, size_t *first, size_t *last)
{
    size_t right_frames;
    if (first != NULL) *first = 0;
    if (last != NULL) *last = 0;
    if (ui == NULL || ui->drone_output_frames == 0 ||
        ui->drone_source_last <= ui->drone_split_frame ||
        ui->drone_overlap_frames == 0)
        return 0;
    right_frames = ui->drone_source_last - ui->drone_split_frame;
    if (right_frames > ui->drone_output_frames ||
        ui->drone_overlap_frames > right_frames)
        return 0;
    if (first != NULL) *first = right_frames - ui->drone_overlap_frames;
    if (last != NULL) *last = right_frames;
    return 1;
}

int ts_ui_drone_waveform_contains(int x, int y)
{
    return x >= TS_DRONE_WAVE_X && x < TS_DRONE_WAVE_X + TS_DRONE_WAVE_W &&
           y >= TS_DRONE_WAVE_Y && y < TS_DRONE_WAVE_Y + TS_DRONE_WAVE_H;
}

int ts_ui_drone_crossfade_handle_from_point(const TsUiState *ui, int x, int y)
{
    size_t first;
    size_t last;
    int first_x;
    int last_x;
    if (!ts_ui_drone_waveform_contains(x, y) ||
        !drone_crossfade_range(ui, &first, &last))
        return 0;
    first_x = TS_DRONE_WAVE_X +
              (int)(first * TS_DRONE_WAVE_W / ui->drone_output_frames);
    last_x = TS_DRONE_WAVE_X +
             (int)(last * TS_DRONE_WAVE_W / ui->drone_output_frames);
    if (abs(x - first_x) <= 5 && abs(x - last_x) <= 5)
        return x <= first_x + (last_x - first_x) / 2 ? 1 : 2;
    if (abs(x - first_x) <= 5) return 1;
    if (abs(x - last_x) <= 5) return 2;
    return 0;
}

static void mini_playhead(TsFramebuffer *fb, const TsUiState *ui,
                          const TsSample *sample, int x, int y,
                          int width, int height, size_t first, size_t last);

static void drone_waveform(TsFramebuffer *fb, const TsUiState *ui)
{
    const TsSample *sample = ui->drone_preview_sample;
    size_t crossfade_first = 0;
    size_t crossfade_last = 0;
    int has_crossfade = drone_crossfade_range(
        ui, &crossfade_first, &crossfade_last);
    int center = TS_DRONE_WAVE_Y + TS_DRONE_WAVE_H / 2;
    rect(fb, TS_DRONE_WAVE_X, TS_DRONE_WAVE_Y,
         TS_DRONE_WAVE_W, TS_DRONE_WAVE_H, RGB(8, 8, 8));
    for (int x = TS_DRONE_WAVE_X; x < TS_DRONE_WAVE_X + TS_DRONE_WAVE_W;
         x += 50)
        rect(fb, x, TS_DRONE_WAVE_Y, 1, TS_DRONE_WAVE_H, RGB(35, 32, 36));
    if (has_crossfade) {
        int first_x = TS_DRONE_WAVE_X +
                      (int)(crossfade_first * TS_DRONE_WAVE_W /
                            ui->drone_output_frames);
        int last_x = TS_DRONE_WAVE_X +
                     (int)(crossfade_last * TS_DRONE_WAVE_W /
                           ui->drone_output_frames);
        if (last_x <= first_x) last_x = first_x + 1;
        rect(fb, first_x, TS_DRONE_WAVE_Y, last_x - first_x,
             TS_DRONE_WAVE_H, PAL_WAVE_SELECTION);
    }
    rect(fb, TS_DRONE_WAVE_X, center, TS_DRONE_WAVE_W, 1, RGB(80, 73, 81));
    if (sample != NULL && sample->data != NULL && sample->frames > 0) {
        TsWaveformRequest request;
        TsWaveformCache *cache = waveform_cache(TS_UI_WAVEFORM_DRONE);
        memset(&request, 0, sizeof(request));
        request.sample = sample;
        request.last = sample->frames;
        request.width = TS_DRONE_WAVE_W;
        request.revision = ui->waveform_revisions[TS_UI_WAVEFORM_DRONE];
        (void)ts_waveform_cache_prepare(cache, &request);
        for (int column = 0; column < TS_DRONE_WAVE_W; ++column) {
            const TsWaveformColumn *analysis = &cache->columns[column];
            size_t first = analysis->first;
            size_t last = analysis->last;
            float minimum = analysis->minimum;
            float maximum = analysis->maximum;
            int y0;
            int y1;
            uint32_t color;
            if (minimum < -1.0f) minimum = -1.0f;
            if (minimum > 1.0f) minimum = 1.0f;
            if (maximum < -1.0f) maximum = -1.0f;
            if (maximum > 1.0f) maximum = 1.0f;
            y0 = center - (int)lrintf(maximum * (TS_DRONE_WAVE_H / 2 - 3));
            y1 = center - (int)lrintf(minimum * (TS_DRONE_WAVE_H / 2 - 3));
            color = has_crossfade && first < crossfade_last && last > crossfade_first ?
                    PAL_BLOCK_TEXT : PAL_NOTE;
            wave_line(fb, TS_DRONE_WAVE_X + column, y0,
                      TS_DRONE_WAVE_X + column, y1, color);
        }
    } else {
        text(fb, 224, 108, "PREVIEW WAVEFORM UNAVAILABLE", PAL_EFFECT, 1);
    }
    rect(fb, TS_DRONE_WAVE_X, TS_DRONE_WAVE_Y, 3, TS_DRONE_WAVE_H, PAL_TUNING);
    rect(fb, TS_DRONE_WAVE_X + TS_DRONE_WAVE_W - 3, TS_DRONE_WAVE_Y,
         3, TS_DRONE_WAVE_H, PAL_TUNING);
    if (has_crossfade) {
        int first_x = TS_DRONE_WAVE_X +
                      (int)(crossfade_first * TS_DRONE_WAVE_W /
                            ui->drone_output_frames);
        int last_x = TS_DRONE_WAVE_X +
                     (int)(crossfade_last * TS_DRONE_WAVE_W /
                           ui->drone_output_frames);
        rect(fb, first_x - 1, TS_DRONE_WAVE_Y, 3, TS_DRONE_WAVE_H, PAL_EFFECT);
        rect(fb, last_x - 1, TS_DRONE_WAVE_Y, 3, TS_DRONE_WAVE_H, PAL_EFFECT);
    }
    mini_playhead(fb, ui, sample,
                  TS_DRONE_WAVE_X, TS_DRONE_WAVE_Y,
                  TS_DRONE_WAVE_W, TS_DRONE_WAVE_H,
                  0u, sample != NULL ? sample->frames : 0u);
}

static void drone_render(TsFramebuffer *fb, const TsUiState *ui)
{
    char crossfade[64];
    char output[64];
    frame(fb, TS_MODAL_PANEL_X, TS_MODAL_PANEL_Y,
          TS_MODAL_PANEL_W, TS_MODAL_PANEL_H, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 20, 47, "DRONE MAKER", PAL_NOTE, 1);
    snprintf(crossfade, sizeof(crossfade), "CROSSFADE: %.2F MS",
             ui->drone_effective_crossfade_ms);
    text(fb, 414, 47, crossfade, PAL_EFFECT, 1);
    text(fb, 20, 63, "WHEEL COARSE  SHIFT+WHEEL FINE  DRAG EDGES - ZERO SNAP",
         RGB(190, 185, 190), 1);
    snprintf(output, sizeof(output), "OUT %zu  SPLIT %zu",
             ui->drone_output_frames, ui->drone_split_frame);
    text(fb, 476, 63, output, PAL_TUNING, 1);
    drone_waveform(fb, ui);
    button(fb, 20, 161, 110, "PREVIEW LOOP", ui->drone_preview_active);
    button(fb, 136, 161, 60, "STOP", 0);
    button(fb, 202, 161, 120, "COPY NEW TILE", 0);
    button(fb, 328, 161, 132, "REPLACE SELECTION", 0);
    button(fb, 466, 161, 80, "CANCEL", 0);
    text(fb, 20, 190, "P PREVIEW  SPACE STOP  C COPY  R REPLACE  ESC CANCEL",
         RGB(190, 185, 190), 1);
}

static int transform_frame_x(size_t frame, size_t first, size_t last)
{
    if (last <= first || frame <= first) return TS_TRANSFORM_WAVE_X;
    if (frame >= last) return TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W;
    return TS_TRANSFORM_WAVE_X +
           (int)((frame - first) * TS_TRANSFORM_WAVE_W / (last - first));
}

static void mini_playhead(TsFramebuffer *fb, const TsUiState *ui,
                          const TsSample *sample, int x, int y,
                          int width, int height, size_t first, size_t last)
{
    size_t frame_at;
    int marker_x;
    if (fb == NULL || ui == NULL || sample == NULL ||
        !ui->playback_active || ui->playhead_sample != sample ||
        ui->playhead_frames == 0u || last <= first || width <= 0) return;
    frame_at = ui->playhead_frame;
    if (frame_at < first || frame_at >= last) return;
    marker_x = x + (int)((frame_at - first) * (size_t)width / (last - first));
    if (marker_x >= x + width) marker_x = x + width - 1;
    rect(fb, marker_x, y, 1, height, PAL_VOLUME);
    rect(fb, marker_x > x ? marker_x - 1 : marker_x, y, 3, 3, PAL_VOLUME);
}

static void transform_waveform(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument)
{
    const TsSample *sample = &instrument->current;
    size_t first = instrument->view_first;
    size_t last = instrument->view_last;
    int middle = TS_TRANSFORM_WAVE_Y + TS_TRANSFORM_WAVE_H / 2;
    rect(fb, TS_TRANSFORM_WAVE_X, TS_TRANSFORM_WAVE_Y,
         TS_TRANSFORM_WAVE_W, TS_TRANSFORM_WAVE_H, RGB(8, 8, 8));
    if (sample->data == NULL || sample->frames == 0u) return;
    if (last <= first || last > sample->frames) { first = 0u; last = sample->frames; }
    if (instrument->has_selection && instrument->selection_last > first &&
        instrument->selection_first < last) {
        int x0 = transform_frame_x(instrument->selection_first, first, last);
        int x1 = transform_frame_x(instrument->selection_last, first, last);
        if (x0 < TS_TRANSFORM_WAVE_X) x0 = TS_TRANSFORM_WAVE_X;
        if (x1 > TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W)
            x1 = TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W;
        rect(fb, x0, TS_TRANSFORM_WAVE_Y, x1 - x0,
             TS_TRANSFORM_WAVE_H, PAL_WAVE_SELECTION);
    }
    rect(fb, TS_TRANSFORM_WAVE_X, middle, TS_TRANSFORM_WAVE_W, 1, PAL_BUTTON);
    TsWaveformRequest request;
    TsWaveformCache *cache = waveform_cache(TS_UI_WAVEFORM_TRANSFORM);
    memset(&request, 0, sizeof(request));
    request.sample = sample;
    request.first = first;
    request.last = last;
    request.width = TS_TRANSFORM_WAVE_W;
    request.replacement = ui->transform_preview_sample;
    request.replacement_first = ui->transform_preview_first;
    request.replacement_last = ui->transform_preview_last;
    request.revision = ui->waveform_revisions[TS_UI_WAVEFORM_TRANSFORM];
    (void)ts_waveform_cache_prepare(cache, &request);
    for (int column = 0; column < TS_TRANSFORM_WAVE_W; ++column) {
        const TsWaveformColumn *analysis = &cache->columns[column];
        size_t begin = analysis->first;
        size_t end = analysis->last;
        float low = analysis->minimum;
        float high = analysis->maximum;
        int y0;
        int y1;
        y0 = middle - (int)lrintf(high * (TS_TRANSFORM_WAVE_H / 2 - 4));
        y1 = middle - (int)lrintf(low * (TS_TRANSFORM_WAVE_H / 2 - 4));
        if (y0 > y1) { int swap = y0; y0 = y1; y1 = swap; }
        if (y0 < TS_TRANSFORM_WAVE_Y) y0 = TS_TRANSFORM_WAVE_Y;
        if (y1 >= TS_TRANSFORM_WAVE_Y + TS_TRANSFORM_WAVE_H)
            y1 = TS_TRANSFORM_WAVE_Y + TS_TRANSFORM_WAVE_H - 1;
        rect(fb, TS_TRANSFORM_WAVE_X + column, y0, 1,
             y1 > y0 ? y1 - y0 + 1 : 1,
             ui->transform_preview_available &&
             end > ui->transform_preview_first &&
             begin < ui->transform_preview_last ? PAL_EFFECT :
             instrument->has_selection && end > instrument->selection_first &&
             begin < instrument->selection_last ? PAL_BLOCK_TEXT : PAL_NOTE);
    }
    if (instrument->has_selection) {
        int x0 = transform_frame_x(instrument->selection_first, first, last);
        int x1 = transform_frame_x(instrument->selection_last, first, last);
        if (x0 >= TS_TRANSFORM_WAVE_X &&
            x0 < TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W)
            rect(fb, x0, TS_TRANSFORM_WAVE_Y, 2, TS_TRANSFORM_WAVE_H, PAL_EFFECT);
        if (x1 > TS_TRANSFORM_WAVE_X &&
            x1 <= TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W)
            rect(fb, x1 - 2, TS_TRANSFORM_WAVE_Y, 2, TS_TRANSFORM_WAVE_H, PAL_EFFECT);
    }
    if (ui->playhead_sample == ui->transform_preview_sample &&
        ui->transform_preview_sample != NULL && ui->playhead_frames > 0u) {
        size_t span = ui->transform_preview_last > ui->transform_preview_first ?
                      ui->transform_preview_last - ui->transform_preview_first : 0u;
        size_t mapped = ui->transform_preview_first +
            (size_t)((double)ui->playhead_frame * (double)span /
                     (double)ui->playhead_frames);
        TsUiState mapped_ui = *ui;
        mapped_ui.playhead_sample = sample;
        mapped_ui.playhead_frame = mapped;
        mini_playhead(fb, &mapped_ui, sample,
                      TS_TRANSFORM_WAVE_X, TS_TRANSFORM_WAVE_Y,
                      TS_TRANSFORM_WAVE_W, TS_TRANSFORM_WAVE_H, first, last);
    } else {
        mini_playhead(fb, ui, sample,
                      TS_TRANSFORM_WAVE_X, TS_TRANSFORM_WAVE_Y,
                      TS_TRANSFORM_WAVE_W, TS_TRANSFORM_WAVE_H, first, last);
    }
}

static void transform_control(TsFramebuffer *fb, const TsCdpRecipe *recipe,
                              const TsCdpRecipeValues *values, size_t index,
                              uint32_t sample_rate)
{
    const TsCdpControlSpec *control = &recipe->controls[index];
    int x = 20 + (int)index * 150;
    float value = ts_cdp_control_quantize(control, values->controls[index]);
    float amount = control->maximum > control->minimum ?
                   (value - control->minimum) /
                   (control->maximum - control->minimum) : 0.0f;
    char shown[48];
    ts_cdp_control_format(control, value, sample_rate, recipe->analysis_points,
                          recipe->analysis_overlap, shown, sizeof(shown));
    text(fb, x, 155, control->label, PAL_TUNING, 1);
    text(fb, x + 62, 155, shown, PAL_EFFECT, 1);
    frame(fb, x, 166, 140, 18, RGB(12, 12, 12), PAL_BUTTON);
    rect(fb, x + 3, 169, (int)lrintf(amount * 134.0f), 12, PAL_BLOCK);
}

static void dsp_transform_control(TsFramebuffer *fb,
                                  const TsDspRecipe *recipe,
                                  const TsDspRecipeValues *values,
                                  size_t index)
{
    const TsDspRecipeControl *control;
    float amount;
    int x;
    char shown[48];
    if (recipe == NULL || values == NULL || index >= recipe->control_count) return;
    control = &recipe->controls[index];
    amount = values->controls[index];
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    x = 20 + (int)index * 150;
    ts_dsp_recipe_control_format(control, amount, shown, sizeof(shown));
    text(fb, x, 155, control->label, PAL_TUNING, 1);
    text(fb, x + 62, 155, shown, PAL_EFFECT, 1);
    frame(fb, x, 166, 140, 18, RGB(12, 12, 12), PAL_BUTTON);
    rect(fb, x + 3, 169, (int)lrintf(amount * 134.0f), 12, PAL_BLOCK);
}

static void transform_render(TsFramebuffer *fb, const TsUiState *ui,
                             const TsInstrument *instrument)
{
    const int dsp = ui->transform_backend == TS_TRANSFORM_BACKEND_DSP;
    const TsCdpRecipe *recipe = dsp ? NULL :
        ts_cdp_factory_recipe_at((size_t)ui->transform_recipe_index);
    const TsDspRecipe *dsp_recipe = dsp && ui->transform_dsp_slot >= 0 ?
        ts_dsp_factory_recipe_at((size_t)ui->transform_dsp_slot) : NULL;
    const char *name;
    const char *description;
    char mix[32];
    char selection[48];
    if (!dsp && recipe == NULL) recipe = ts_cdp_factory_recipe_at(0u);
    if ((dsp && dsp_recipe == NULL) || (!dsp && recipe == NULL)) return;
    name = dsp ? dsp_recipe->display_name : recipe->display_name;
    description = dsp ? dsp_recipe->description : recipe->description;
    frame(fb, 10, 40, 620, 264, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 20, 46, "TRANSFORM", PAL_NOTE, 1);
    text(fb, dsp ? 476 : 452, 46, dsp ? "NATIVE DSP" : "OFFLINE CDP8",
         PAL_EFFECT, 1);
    transform_waveform(fb, ui, instrument);
    button(fb, 20, 126, 94, name, 1);
    text(fb, 124, 134, description, PAL_INSTRUMENT, 1);
    if (dsp) {
        for (size_t i = 0; i < dsp_recipe->control_count; ++i)
            dsp_transform_control(fb, dsp_recipe, &ui->transform_dsp_values, i);
        button(fb, 20, 190, 112, "AUTO PREVIEW", ui->transform_rendering);
    } else {
        for (size_t i = 0; i < recipe->control_count; ++i)
            transform_control(fb, recipe, &ui->transform_values, i,
                              instrument->current.sample_rate);
        if (recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED)
            snprintf(mix, sizeof(mix), "MIX N/A");
        else
            snprintf(mix, sizeof(mix), "MIX %d%%",
                     (int)lrintf(ui->transform_values.mix * 100.0f));
        button(fb, 20, 190, 112, mix, 0);
    }
    button(fb, 142, 190, 110, "SELECTION",
           ui->transform_scope == TS_TRANSFORM_SELECTION &&
           instrument->has_selection);
    button(fb, 258, 190, 78, "WHOLE",
           ui->transform_scope == TS_TRANSFORM_WHOLE);
    snprintf(selection, sizeof(selection), instrument->has_selection ?
             "SEL %zu:%zu" : "NO SELECTION",
             instrument->selection_first, instrument->selection_last);
    text(fb, 350, 198, selection,
         instrument->has_selection ? PAL_TUNING : RGB(150, 145, 150), 1);
    button(fb, 20, 220, 80, ui->transform_rendering ? "WORKING" : "RENDER",
           ui->transform_rendering);
    button(fb, 106, 220, 74, "APPLY", ui->transform_preview_available);
    button(fb, 186, 220, 92,
           ui->transform_preview_active ? "STOP" : "AUDITION",
           ui->transform_preview_active);
    button(fb, 284, 220, 110, "SAVE/UPDATE", 0);
    button(fb, 400, 220, 70, ui->transform_rendering ? "CANCEL" : "BACK", 0);
    text(fb, 480, 228,
         ui->transform_preview_available ? ts_cdp_safety_name(ui->transform_safety) :
         dsp || ui->transform_runtime_available ? "READY" : "NO RUNTIME",
         ui->transform_safety == TS_CDP_SAFETY_HOT ? PAL_VOLUME : PAL_EFFECT, 1);
    text(fb, 20, 254, ui->transform_message, PAL_MOUSE, 1);
    text(fb, 20, 273,
         dsp ? "SPACE AUDITIONS  U SAVES PRESET  ESC CANCELS OR RETURNS" :
         ui->transform_runtime_available ?
         "CLICK NAME NEW TAKE  ENTER RENDERS  U SAVES  ESC RETURNS" :
         "SET CDP BIN PATH IN CONFIG - REQUIRED PROCESS WILL BE NAMED",
         dsp || ui->transform_runtime_available ? PAL_TUNING : PAL_VOLUME, 1);
}

static void fm_render(TsFramebuffer *fb, const TsUiState *ui,
                      const TsInstrument *instrument)
{
    static const char *lock_names[5] = {"PITCH", "WAVE", "LFO", "FILTER", "STRUCT"};
    static const uint32_t lock_bits[5] = {
        TS_FM_MUTATE_PITCH, TS_FM_MUTATE_WAVE, TS_FM_MUTATE_LFO,
        TS_FM_MUTATE_FILTER, TS_FM_MUTATE_STRUCTURE
    };
    const TsSample *preview = ui->fm_preview_sample;
    frame(fb, 10, 40, 620, 274, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 20, 46, "FM SOUND LOGIC", PAL_NOTE, 1);
    text(fb, 474, 46, "SIX-VOICE SOURCE", PAL_EFFECT, 1);
    frame(fb, 20, 62, 600, 48, RGB(8, 8, 8), PAL_BUTTON);
    rect(fb, 22, 85, 596, 1, PAL_BUTTON);
    if (preview != NULL && preview->data != NULL && preview->frames > 1u) {
        for (int column = 0; column < 596; ++column) {
            size_t first = (size_t)column * preview->frames / 596u;
            size_t last = (size_t)(column + 1) * preview->frames / 596u;
            float low = 1.0f, high = -1.0f;
            if (last <= first) last = first + 1u;
            if (last > preview->frames) last = preview->frames;
            for (size_t frame_at = first; frame_at < last; ++frame_at) {
                float sample_value = preview->data[frame_at];
                if (sample_value < low) low = sample_value;
                if (sample_value > high) high = sample_value;
            }
            {
                int y0 = 85 - (int)lrintf(high * 20.0f);
                int y1 = 85 - (int)lrintf(low * 20.0f);
                if (y0 < 64) y0 = 64;
                if (y1 > 108) y1 = 108;
                rect(fb, 22 + column, y0, 1, y1 - y0 + 1, PAL_INSTRUMENT);
            }
        }
    }
    mini_playhead(fb, ui, preview, 22, 63, 596, 46,
                  0u, preview != NULL ? preview->frames : 0u);
    for (int page = 0; page < TS_FM_PAGE_COUNT; ++page) {
        int x = 20 + page * 86;
        button(fb, x, 116, 82, ts_fm_page_name((TsFmPage)page),
               ui->fm_page == (TsFmPage)page);
    }
    for (int control = 0; control < TS_FM_OPERATOR_COUNT; ++control) {
        int x = 20 + control * 100;
        char label[24];
        char value[32];
        float amount = ts_fm_control_normalized(&ui->fm_patch, ui->fm_page, control);
        int disabled = ui->fm_patch.drone_mode &&
            ((ui->fm_page == TS_FM_PAGE_FILTER &&
              (control == 2 || control == 3)) ||
             (ui->fm_page == TS_FM_PAGE_STRUCTURE && control == 5));
        ts_fm_control_format(&ui->fm_patch, ui->fm_page, control,
                             label, sizeof(label), value, sizeof(value));
        text(fb, x, 146, label, disabled ? RGB(112, 108, 114) : PAL_TUNING, 1);
        text(fb, x, 158, value, disabled ? RGB(112, 108, 114) : PAL_EFFECT, 1);
        frame(fb, x, 171, 94, 17, RGB(12, 12, 12), PAL_BUTTON);
        rect(fb, x + 3, 174, (int)lrintf(amount * 88.0f), 11,
             disabled ? RGB(70, 66, 72) : PAL_BLOCK);
        button(fb, x, 193, 94,
               (ui->fm_patch.active_mask & (1u << control)) != 0u ?
               "VOICE ON" : "VOICE OFF",
               (ui->fm_patch.active_mask & (1u << control)) != 0u);
    }
    if (ui->fm_page == TS_FM_PAGE_PITCH) {
        static const char *pitch_classes[12] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        char root[24];
        char scale[32];
        text(fb, 20, 225, "RANDOM PITCH", PAL_NOTE, 1);
        button(fb, 96, 218, 112,
               ui->fm_patch.pitch_lock ? "PITCH LOCK" : "PITCH OPEN",
               ui->fm_patch.pitch_lock);
        snprintf(root, sizeof(root), "ROOT %s",
                 pitch_classes[ui->fm_patch.pitch_root]);
        button(fb, 214, 218, 96, root, 0);
        snprintf(scale, sizeof(scale), "SCALE %s",
                 ts_fm_pitch_scale_name(ui->fm_patch.pitch_scale));
        button(fb, 316, 218, 132, scale, 0);
        button(fb, 454, 218, 166, "APPLY PITCHES", 0);
    } else {
        text(fb, 20, 225, "MUTATE", PAL_NOTE, 1);
        for (int lock = 0; lock < 5; ++lock)
            button(fb, 76 + lock * 108, 218, 102, lock_names[lock],
                   (ui->fm_patch.mutation_mask & lock_bits[lock]) != 0u);
    }
    button(fb, 20, 252, 88, "RANDOMIZE", 0);
    button(fb, 114, 252, 90, "MAKE BANK", 0);
    button(fb, 210, 252, 66, "APPLY", 0);
    button(fb, 282, 252, 84, "AUDITION", 0);
    button(fb, 372, 252, 70, ui->fm_held_notes > 0 ? "HELD" : "HOLD",
           ui->fm_held_notes > 0);
    button(fb, 448, 252, 64, "BACK", 0);
    button(fb, 20, 278, 86, "DRONE", ui->fm_patch.drone_mode);
    button(fb, 112, 278, 100, "EXTREME", ui->fm_patch.extreme_mode);
    button(fb, 218, 278, 86,
           instrument != NULL && instrument->family_trajectory ?
           "CHAIN ON" : "CHAIN OFF",
           instrument != NULL && instrument->family_trajectory);
    {
        char range[32];
        float amount = instrument != NULL ? instrument->family_mutation : 0.0f;
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        snprintf(range, sizeof(range), "RANGE %d", (int)lrintf(amount * 100.0f));
        text(fb, 314, 286, range, PAL_TUNING, 1);
        frame(fb, 386, 279, 234, 20, RGB(12, 12, 12), PAL_BUTTON);
        rect(fb, 389, 282, (int)lrintf(amount * 228.0f), 14, PAL_BLOCK);
    }
    text(fb, 20, 306, ui->fm_message, PAL_MOUSE, 1);
    if (ui->fm_bank_choice_open) {
        frame(fb, 62, 218, 516, 92, RGB(28, 25, 30), PAL_VOLUME);
        text(fb, 82, 226, "MAKE A 16-SOUND FM BANK?", PAL_NOTE, 1);
        text(fb, 82, 242,
             "CURRENT PATCH IS TILE 01; RANGE + CHAIN BUILD 15 VARIATIONS",
             PAL_MOUSE, 1);
        text(fb, 82, 257,
             "REPLACE NEEDS EVERY CURRENT TILE UNLOCKED",
             PAL_TUNING, 1);
        button(fb, 82, 278, 132, "REPLACE PAGE", 0);
        button(fb, 220, 278, 150, "NEW SAMPLE PAGE", 1);
        button(fb, 376, 278, 94, "CANCEL", 0);
    } else if (ui->fm_full_choice_open) {
        frame(fb, 72, 226, 496, 80, RGB(28, 25, 30), PAL_VOLUME);
        text(fb, 92, 234, "THIS SAMPLE PAGE IS FULL", PAL_NOTE, 1);
        text(fb, 92, 250,
             "OVERWRITE THIS TILE OR CONTINUE THE CHAIN ON A NEW PAGE?",
             PAL_MOUSE, 1);
        button(fb, 92, 274, 126, "OVERWRITE", 0);
        button(fb, 224, 274, 150, "NEW SAMPLE PAGE", 1);
        button(fb, 380, 274, 94, "CANCEL", 0);
    }
}

static void palette_render(TsFramebuffer *fb, const TsUiState *ui)
{
    static const char *const short_names[TS_PALETTE_TAPESISTER_COLOR_COUNT] = {
        "TITLE", "ACTIVE", "ACT TEXT", "POINTER", "DESKTOP", "CONTROLS", "WAVE",
        "PRIMARY", "EDGE/ZERO", "LOOP", "EFFECT", "SPARE", "WAVE SEL", "ACT TILE",
        "WAVE LEFT", "WAVE RIGHT", "WAVE SUM", "SISTER H", "SISTER V"
    };
    static const char *const channel_names[3] = {"RED", "GREEN", "BLUE"};
    char value[48];
    uint32_t selected = ui->palette.colors[ui->palette_entry];
    frame(fb, TS_MODAL_PANEL_X, TS_MODAL_PANEL_Y,
          TS_MODAL_PANEL_W, TS_MODAL_PANEL_H, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 20, 45, "PALETTE EDITOR", PAL_NOTE, 1);
    text(fb, 438, 45, "LIVE TAPESISTER COLORS", PAL_EFFECT, 1);
    for (int color = 0; color < TS_PALETTE_TAPESISTER_COLOR_COUNT; ++color) {
        int column = color % TS_PALETTE_SWATCH_COLUMNS;
        int row = color / TS_PALETTE_SWATCH_COLUMNS;
        int x = TS_PALETTE_SWATCH_X + column * TS_PALETTE_SWATCH_STEP_X;
        int y = TS_PALETTE_SWATCH_Y + row * TS_PALETTE_SWATCH_STEP_Y;
        uint32_t entry = ui->palette.colors[color];
        rect(fb, x, y, TS_PALETTE_SWATCH_W, TS_PALETTE_SWATCH_H,
             color == ui->palette_entry ? PAL_BLOCK : RGB(18, 18, 18));
        if (color == ui->palette_entry) {
            rect(fb, x, y, TS_PALETTE_SWATCH_W, 1, PAL_MOUSE);
            rect(fb, x, y + TS_PALETTE_SWATCH_H - 1, TS_PALETTE_SWATCH_W, 1, PAL_MOUSE);
            rect(fb, x, y, 1, TS_PALETTE_SWATCH_H, PAL_MOUSE);
            rect(fb, x + TS_PALETTE_SWATCH_W - 1, y, 1, TS_PALETTE_SWATCH_H, PAL_MOUSE);
        }
        rect(fb, x + 3, y + 3, 11, 7, entry);
        text(fb, x + 18, y + 3, short_names[color],
             color == ui->palette_entry ? PAL_BLOCK_TEXT : RGB(222, 218, 214), 1);
    }
    snprintf(value, sizeof(value), "%s  #%06X",
             short_names[ui->palette_entry],
             (unsigned)(selected & 0xffffffu));
    /* The palette now has three swatch rows. Keep the selected-color readout
       in the free lower-middle lane rather than painting across row three. */
    text(fb, 250, 139, value, PAL_INSTRUMENT, 1);
    for (int component = 0; component < 3; ++component) {
        float amount = ts_palette_component(&ui->palette,
                       (TsPaletteColor)ui->palette_entry, component) / 255.0f;
        int y = TS_PALETTE_SLIDER_Y + component * TS_PALETTE_SLIDER_STEP_Y;
        compact_slider(fb, TS_PALETTE_SLIDER_X, y, TS_PALETTE_SLIDER_W,
                       channel_names[component], amount,
                       ts_palette_component(&ui->palette,
                           (TsPaletteColor)ui->palette_entry, component),
                       component == 0 ? PAL_TEXT : component == 1 ?
                       PAL_INSTRUMENT : PAL_TUNING,
                       component == ui->palette_channel);
    }
    compact_slider(fb, TS_PALETTE_CONTRAST_X, TS_PALETTE_SLIDER_Y,
                   TS_PALETTE_CONTRAST_W, "DESKTOP",
                   (float)(ui->palette.desktop_contrast - 1) / 99.0f,
                   ui->palette.desktop_contrast, PAL_DESKTOP,
                   ui->palette_channel == 3);
    compact_slider(fb, TS_PALETTE_CONTRAST_X,
                   TS_PALETTE_SLIDER_Y + TS_PALETTE_SLIDER_STEP_Y,
                   TS_PALETTE_CONTRAST_W, "BUTTONS",
                   (float)(ui->palette.buttons_contrast - 1) / 99.0f,
                   ui->palette.buttons_contrast, PAL_BUTTON,
                   ui->palette_channel == 4);
    text(fb, 432, 89, "COLOR", PAL_TUNING, 1);
    frame(fb, 432, 99, 70, 47, selected, PAL_MOUSE);
    text(fb, 510, 89, "WAVE SEL", PAL_TUNING, 1);
    frame(fb, 510, 99, 108, 47, RGB(8, 8, 8), PAL_MOUSE);
    rect(fb, 538, 102, 40, 41, PAL_WAVE_SELECTION);
    rect(fb, 513, 121, 102, 1, RGB(74, 67, 75));
    for (int x = 516; x < 612; x += 8) {
        int height = 8 + ((x / 8) % 4) * 4;
        rect(fb, x, 122 - height / 2, 1, height,
             x >= 538 && x < 578 ? PAL_BLOCK_TEXT : PAL_NOTE);
    }
    text(fb, 541, 104, "0.25S", PAL_EFFECT, 1);
    text(fb, TS_PALETTE_TAPEHEAD_X, 149, "TAPEHEAD EYEDROPPER", PAL_TUNING, 1);
    for (int swatch = 0; swatch < ts_palette_tapehead_swatch_count(); ++swatch) {
        TsPaletteColor source = ts_palette_tapehead_swatch_color(swatch);
        int x = TS_PALETTE_TAPEHEAD_X + swatch * TS_PALETTE_TAPEHEAD_STEP_X;
        int defined = ts_palette_color_is_defined(&ui->palette_suggestions, source);
        uint32_t color = defined ? ui->palette_suggestions.colors[source] :
                                   RGB(92, 88, 92);
        rect(fb, x, TS_PALETTE_TAPEHEAD_Y, TS_PALETTE_TAPEHEAD_W,
             TS_PALETTE_TAPEHEAD_H, color);
        if (!defined) {
            rect(fb, x + 1, TS_PALETTE_TAPEHEAD_Y + 1, 1,
                 TS_PALETTE_TAPEHEAD_H - 2, RGB(42, 40, 42));
            rect(fb, x + TS_PALETTE_TAPEHEAD_W - 2,
                 TS_PALETTE_TAPEHEAD_Y + 1, 1,
                 TS_PALETTE_TAPEHEAD_H - 2, RGB(42, 40, 42));
        }
    }
    for (size_t i = 0; i < sizeof(palette_buttons) / sizeof(palette_buttons[0]); ++i)
        button(fb, palette_buttons[i].x, TS_PALETTE_ACTION_Y,
               palette_buttons[i].width, palette_buttons[i].label,
               i + 1u == TS_UI_PALETTE_ACTION_DONE);
    text(fb, 462, 182, "PGUP/DN COLOR  ARROWS", PAL_TUNING, 1);
}

int ts_ui_request_startup_welcome(TsUiState *ui, int splash_complete,
                                  int audio_ready)
{
    if (ui == NULL || !splash_complete || ui->startup_welcome_playback_requested)
        return 0;
    ui->startup_welcome_playback_requested = 1;
    return ui->startup_welcome_installed && ui->startup_welcome_autoplay && audio_ready;
}

void ts_ui_init(TsUiState *ui)
{
    memset(ui, 0, sizeof(*ui));
    for (int i = 0; i < TS_UI_WAVEFORM_COUNT; ++i)
        ui->waveform_revisions[i] = 1u;
    ts_warp_gesture_init(&ui->warp_gesture);
    ts_smear_gesture_init(&ui->smear_gesture);
    ts_tear_gesture_init(&ui->tear_gesture);
    ts_stretch_gesture_init(&ui->stretch_gesture);
    ts_canvas_gesture_init(&ui->canvas_gesture);
    ts_amplitude_gesture_init(&ui->amplitude_gesture);
    ts_material_macro_gesture_init(&ui->material_macro_gesture);
    ui->amplitude_profile_first_x = TS_WAVE_W;
    ui->amplitude_profile_last_x = -1;
    ui->mouse_note = -1;
    ui->bank_view_slot = -1;
    ui->load_bank_slot = -1;
    ui->playhead_bank_slot = -1;
    ui->drone_source_slot = -1;
    ui->capture_destination_slot = -1;
    ui->capture_source_slot = -1;
    ui->overdub_confirm_slot = -1;
    ui->capture_state = TS_CAPTURE_IDLE;
    ui->external_record_bank = 0;
    ui->record_source = TS_RECORD_SOURCE_EXT;
    ui->renaming_bank_slot = -1;
    ui->renaming_recipe_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->show_keyboard = 1;
    ui->keyboard_octave = 4;
    ui->keyboard_base_note = TS_KEYBOARD_BASE_NOTE;
    ui->tune_reference.root_note = TS_KEYBOARD_BASE_NOTE;
    ui->tune_reference.fine_tune_cents = 0.0f;
    ui->show_recipes = 0;
    ui->cdp_page = 0;
    ui->dsp_page = 0;
    ts_browser_init(&ui->browser);
    ts_config_init(&ui->config);
    ts_palette_default(&ui->palette);
    ui->palette_entry = TS_PALETTE_PATTERN_TEXT;
    ui->palette_channel = 0;
    ts_recipe_bank_init(&ui->recipes);
    ui->transform_scope = TS_TRANSFORM_WHOLE;
    ui->transform_backend = TS_TRANSFORM_BACKEND_CDP;
    ui->transform_recipe_index = -1;
    ui->transform_dsp_slot = -1;
    ui->transform_safety = TS_CDP_SAFETY_INVALID;
    ui->fm_page = TS_FM_PAGE_PITCH;
    snprintf(ui->fm_message, sizeof(ui->fm_message),
             "ADJUST A PAGE OR PLAY THE SYNTH");
    for (size_t recipe = 0; recipe < ts_cdp_factory_recipe_count(); ++recipe)
        ts_cdp_recipe_values_default(ts_cdp_factory_recipe_at(recipe),
                                     &ui->cdp_presets[recipe]);
    for (size_t recipe = 0; recipe < ts_dsp_factory_recipe_count(); ++recipe)
        ts_dsp_recipe_values_default(ts_dsp_factory_recipe_at(recipe),
                                     &ui->dsp_presets[recipe]);
    ui->transform_values = ui->cdp_presets[0];
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "SELECT A SCOPE AND RENDER");
    snprintf(ui->status, sizeof(ui->status), "READY - SELECT A TILE, LOAD, OR CREATE");
}

int ts_ui_config_field_from_point(int x, int y)
{
    if (x < TS_CONFIG_FIELD_X || x >= TS_CONFIG_FIELD_X + TS_CONFIG_FIELD_W)
        return -1;
    for (int field = 0; field < TS_CONFIG_FIELD_COUNT; ++field) {
        int top = TS_CONFIG_FIELD_Y + field * TS_CONFIG_FIELD_STEP_Y;
        if (y >= top && y < top + TS_CONFIG_FIELD_H) return field;
    }
    return -1;
}

size_t ts_ui_config_cursor_from_point(const TsUiState *ui,
                                      TsConfigField field, int x)
{
    const char *value;
    size_t length;
    size_t cursor;
    size_t first;
    size_t clicked;
    if (ui == NULL || (int)field < 0 || (int)field >= TS_CONFIG_FIELD_COUNT) return 0;
    value = ts_config_field_const(&ui->config, field);
    if (value == NULL) return 0;
    length = strlen(value);
    cursor = field == ui->config_field ? ui->config_cursor : length;
    if (cursor > length) cursor = length;
    first = length > 96u ? length - 96u : 0u;
    if (cursor < first) first = cursor;
    if (cursor > first + 96u) first = cursor - 96u;
    if (x <= TS_CONFIG_FIELD_X + 6) return first;
    clicked = first + (size_t)((x - (TS_CONFIG_FIELD_X + 6) + 3) / 6);
    return clicked > length ? length : clicked;
}

TsUiConfigAction ts_ui_config_action_from_point(int x, int y)
{
    if (y < TS_CONFIG_ACTION_Y || y >= TS_CONFIG_ACTION_Y + 23)
        return TS_UI_CONFIG_ACTION_NONE;
    for (size_t i = 0; i < sizeof(config_buttons) / sizeof(config_buttons[0]); ++i)
        if (x >= config_buttons[i].x &&
            x < config_buttons[i].x + config_buttons[i].width)
            return (TsUiConfigAction)(i + 1u);
    return TS_UI_CONFIG_ACTION_NONE;
}

int ts_ui_palette_entry_from_point(int x, int y)
{
    for (int color = 0; color < TS_PALETTE_TAPESISTER_COLOR_COUNT; ++color) {
        int column = color % TS_PALETTE_SWATCH_COLUMNS;
        int row = color / TS_PALETTE_SWATCH_COLUMNS;
        int left = TS_PALETTE_SWATCH_X + column * TS_PALETTE_SWATCH_STEP_X;
        int top = TS_PALETTE_SWATCH_Y + row * TS_PALETTE_SWATCH_STEP_Y;
        if (x >= left && x < left + TS_PALETTE_SWATCH_W &&
            y >= top && y < top + TS_PALETTE_SWATCH_H) return color;
    }
    return -1;
}

int ts_ui_palette_tapehead_swatch_from_point(int x, int y)
{
    if (y < TS_PALETTE_TAPEHEAD_Y ||
        y >= TS_PALETTE_TAPEHEAD_Y + TS_PALETTE_TAPEHEAD_H ||
        x < TS_PALETTE_TAPEHEAD_X) return -1;
    for (int swatch = 0; swatch < ts_palette_tapehead_swatch_count(); ++swatch) {
        int left = TS_PALETTE_TAPEHEAD_X + swatch * TS_PALETTE_TAPEHEAD_STEP_X;
        if (x >= left && x < left + TS_PALETTE_TAPEHEAD_W) return swatch;
    }
    return -1;
}

static int slider_value_from_point(int x, int left, int width, int maximum)
{
    int track_left = left + 54;
    int track_width = width - 78;
    if (x < track_left || x >= track_left + track_width) return -1;
    return (x - track_left) * maximum / (track_width - 1);
}

int ts_ui_palette_channel_from_point(int x, int y, int *value)
{
    if (value != NULL) *value = -1;
    for (int channel = 0; channel < 3; ++channel) {
        int top = TS_PALETTE_SLIDER_Y + channel * TS_PALETTE_SLIDER_STEP_Y;
        if (x >= TS_PALETTE_SLIDER_X &&
            x < TS_PALETTE_SLIDER_X + TS_PALETTE_SLIDER_W &&
            y >= top && y < top + TS_PALETTE_SLIDER_H) {
            if (value != NULL)
                *value = slider_value_from_point(x, TS_PALETTE_SLIDER_X,
                                                  TS_PALETTE_SLIDER_W, 255);
            return channel;
        }
    }
    for (int contrast = 0; contrast < 2; ++contrast) {
        int top = TS_PALETTE_SLIDER_Y + contrast * TS_PALETTE_SLIDER_STEP_Y;
        if (x >= TS_PALETTE_CONTRAST_X &&
            x < TS_PALETTE_CONTRAST_X + TS_PALETTE_CONTRAST_W &&
            y >= top && y < top + TS_PALETTE_SLIDER_H) {
            int adjusted = slider_value_from_point(x, TS_PALETTE_CONTRAST_X,
                                                    TS_PALETTE_CONTRAST_W, 99);
            if (value != NULL && adjusted >= 0) *value = adjusted + 1;
            return contrast + 3;
        }
    }
    return -1;
}

TsUiPaletteAction ts_ui_palette_action_from_point(int x, int y)
{
    if (y < TS_PALETTE_ACTION_Y || y >= TS_PALETTE_ACTION_Y + 23)
        return TS_UI_PALETTE_ACTION_NONE;
    for (size_t i = 0; i < sizeof(palette_buttons) / sizeof(palette_buttons[0]); ++i)
        if (x >= palette_buttons[i].x &&
            x < palette_buttons[i].x + palette_buttons[i].width)
            return (TsUiPaletteAction)(i + 1u);
    return TS_UI_PALETTE_ACTION_NONE;
}

TsUiLoadSelectionAction ts_ui_load_selection_action_from_point(int x, int y)
{
    if (y < 132 || y >= 155) return TS_UI_LOAD_SELECTION_NONE;
    if (x >= 146 && x < 242) return TS_UI_LOAD_SELECTION_PASTE;
    if (x >= 272 && x < 368) return TS_UI_LOAD_SELECTION_FIT;
    if (x >= 398 && x < 494) return TS_UI_LOAD_SELECTION_CANCEL;
    return TS_UI_LOAD_SELECTION_NONE;
}

TsUiExchangeAction ts_ui_exchange_action_from_point(TsUiExchangeDialog dialog,
                                                     int x, int y)
{
    if (dialog == TS_UI_EXCHANGE_SEND) {
        if (y >= 112 && y < 135) {
            if (x >= 126 && x < 246)
                return TS_UI_EXCHANGE_ACTION_SEND_ONE_INSTRUMENT;
            if (x >= 254 && x < 374)
                return TS_UI_EXCHANGE_ACTION_SEND_SEPARATE_INSTRUMENTS;
            if (x >= 382 && x < 514)
                return TS_UI_EXCHANGE_ACTION_SEND_ALL_PAGES;
        }
        if (y >= 158 && y < 181) {
            if (x >= 128 && x < 240) return TS_UI_EXCHANGE_ACTION_CHECK_INBOX;
            if (x >= 250 && x < 390)
                return TS_UI_EXCHANGE_ACTION_TOGGLE_NEW_INSTANCE;
            if (x >= 400 && x < 512) return TS_UI_EXCHANGE_ACTION_LATER;
        }
    } else if (dialog == TS_UI_EXCHANGE_RECEIVE && y >= 158 && y < 181) {
        if (x >= 184 && x < 316) return TS_UI_EXCHANGE_ACTION_IMPORT;
        if (x >= 330 && x < 456) return TS_UI_EXCHANGE_ACTION_LATER;
    }
    return TS_UI_EXCHANGE_ACTION_NONE;
}

TsUiDroneAction ts_ui_drone_action_from_point(int x, int y)
{
    if (y < 161 || y >= 184) return TS_UI_DRONE_ACTION_NONE;
    if (x >= 20 && x < 130) return TS_UI_DRONE_ACTION_PREVIEW;
    if (x >= 136 && x < 196) return TS_UI_DRONE_ACTION_STOP;
    if (x >= 202 && x < 322) return TS_UI_DRONE_ACTION_COPY;
    if (x >= 328 && x < 460) return TS_UI_DRONE_ACTION_REPLACE;
    if (x >= 466 && x < 546) return TS_UI_DRONE_ACTION_CANCEL;
    return TS_UI_DRONE_ACTION_NONE;
}

TsUiTransformAction ts_ui_transform_action_from_point(int x, int y)
{
    if (x >= 20 && x < 114 && y >= 126 && y < 149)
        return TS_UI_TRANSFORM_ACTION_RECIPE;
    if (x >= 142 && x < 252 && y >= 190 && y < 213)
        return TS_UI_TRANSFORM_ACTION_SELECTION;
    if (x >= 258 && x < 336 && y >= 190 && y < 213)
        return TS_UI_TRANSFORM_ACTION_WHOLE;
    if (y >= 220 && y < 243) {
        if (x >= 20 && x < 100) return TS_UI_TRANSFORM_ACTION_RENDER;
        if (x >= 106 && x < 180) return TS_UI_TRANSFORM_ACTION_APPLY;
        if (x >= 186 && x < 278) return TS_UI_TRANSFORM_ACTION_AUDITION;
        if (x >= 284 && x < 394) return TS_UI_TRANSFORM_ACTION_SAVE;
        if (x >= 400 && x < 470) return TS_UI_TRANSFORM_ACTION_BACK;
    }
    return TS_UI_TRANSFORM_ACTION_NONE;
}

int ts_ui_transform_control_from_point(int x, int y)
{
    if (y < 155 || y >= 184) return -1;
    for (int index = 0; index < TS_CDP_CONTROL_COUNT; ++index) {
        int left = 20 + index * 150;
        if (x >= left && x < left + 140) return index;
    }
    return -1;
}

int ts_ui_transform_mix_contains(int x, int y)
{
    return x >= 20 && x < 132 && y >= 190 && y < 213;
}

int ts_ui_transform_waveform_contains(int x, int y)
{
    return x >= TS_TRANSFORM_WAVE_X &&
           x < TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W &&
           y >= TS_TRANSFORM_WAVE_Y &&
           y < TS_TRANSFORM_WAVE_Y + TS_TRANSFORM_WAVE_H;
}

int ts_ui_amplitude_draw_toggle_contains(int x, int y)
{
    return x >= TS_WAVE_X + TS_WAVE_W - 54 &&
           x < TS_WAVE_X + TS_WAVE_W - 4 &&
           y >= TS_WAVE_Y + 4 && y < TS_WAVE_Y + 21;
}

int ts_ui_amplitude_draw_start_contains(int x, int y)
{
    return x >= TS_WAVE_X - 6 && x < TS_WAVE_X + TS_WAVE_W + 6 &&
           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H;
}

int ts_ui_amplitude_draw_local_x(int x)
{
    if (x <= TS_WAVE_X + 3) return 0;
    if (x >= TS_WAVE_X + TS_WAVE_W - 4) return TS_WAVE_W - 1;
    x -= TS_WAVE_X;
    if (x < 0) return 0;
    if (x >= TS_WAVE_W) return TS_WAVE_W - 1;
    return x;
}

TsUiTuneAction ts_ui_tune_action_from_point(int x, int y)
{
    if (y < 261 || y >= 285) return TS_UI_TUNE_ACTION_NONE;
    if (x >= TS_TUNE_MATERIAL_SEMI_DOWN_X && x < 62)
        return TS_UI_TUNE_ACTION_MATERIAL_SEMITONE_DOWN;
    if (x >= TS_TUNE_MATERIAL_SEMI_UP_X && x < 118)
        return TS_UI_TUNE_ACTION_MATERIAL_SEMITONE_UP;
    if (x >= TS_TUNE_MATERIAL_CENT_DOWN_X && x < 164)
        return TS_UI_TUNE_ACTION_MATERIAL_CENT_DOWN;
    if (x >= TS_TUNE_MATERIAL_CENT_UP_X && x < 210)
        return TS_UI_TUNE_ACTION_MATERIAL_CENT_UP;
    if (x >= TS_TUNE_REFERENCE_DOWN_X && x < 248)
        return TS_UI_TUNE_ACTION_REFERENCE_DOWN;
    if (x >= TS_TUNE_REFERENCE_UP_X && x < 354)
        return TS_UI_TUNE_ACTION_REFERENCE_UP;
    if (x >= TS_TUNE_REFERENCE_TONE_X && x < 510)
        return TS_UI_TUNE_ACTION_REFERENCE_TONE;
    if (x >= TS_TUNE_DETECT_X && x < 630)
        return TS_UI_TUNE_ACTION_DETECT_OR_MATCH;
    return TS_UI_TUNE_ACTION_NONE;
}

int ts_ui_fm_button_from_point(int x, int y)
{
    return x >= 506 && x < 630 && y >= 261 && y < 285;
}

TsFmPage ts_ui_fm_page_from_point(int x, int y)
{
    if (y < 116 || y >= 140) return (TsFmPage)-1;
    for (int page = 0; page < TS_FM_PAGE_COUNT; ++page)
        if (x >= 20 + page * 86 && x < 102 + page * 86) return (TsFmPage)page;
    return (TsFmPage)-1;
}

int ts_ui_fm_control_from_point(int x, int y)
{
    if (y < 146 || y >= 188) return -1;
    for (int control = 0; control < TS_FM_OPERATOR_COUNT; ++control)
        if (x >= 20 + control * 100 && x < 114 + control * 100) return control;
    return -1;
}

int ts_ui_fm_voice_from_point(int x, int y)
{
    if (y < 193 || y >= 217) return -1;
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
        if (x >= 20 + voice * 100 && x < 114 + voice * 100) return voice;
    return -1;
}

uint32_t ts_ui_fm_mutation_from_point(int x, int y)
{
    static const uint32_t bits[5] = {
        TS_FM_MUTATE_PITCH, TS_FM_MUTATE_WAVE, TS_FM_MUTATE_LFO,
        TS_FM_MUTATE_FILTER, TS_FM_MUTATE_STRUCTURE
    };
    if (y < 218 || y >= 242) return 0u;
    for (int lock = 0; lock < 5; ++lock)
        if (x >= 76 + lock * 108 && x < 178 + lock * 108) return bits[lock];
    return 0u;
}

TsUiFmAction ts_ui_fm_action_from_point(int x, int y)
{
    if (y >= 218 && y < 242) {
        if (x >= 96 && x < 208) return TS_UI_FM_ACTION_PITCH_LOCK;
        if (x >= 214 && x < 310) return TS_UI_FM_ACTION_PITCH_ROOT;
        if (x >= 316 && x < 448) return TS_UI_FM_ACTION_PITCH_SCALE;
        if (x >= 454 && x < 620) return TS_UI_FM_ACTION_APPLY_PITCHES;
    }
    if (y >= 252 && y < 276) {
        if (x >= 20 && x < 108) return TS_UI_FM_ACTION_RANDOMIZE;
        if (x >= 114 && x < 204) return TS_UI_FM_ACTION_BANK_MAKER;
        if (x >= 210 && x < 276) return TS_UI_FM_ACTION_APPLY;
        if (x >= 282 && x < 366) return TS_UI_FM_ACTION_AUDITION;
        if (x >= 372 && x < 442) return TS_UI_FM_ACTION_HOLD;
        if (x >= 448 && x < 512) return TS_UI_FM_ACTION_BACK;
    }
    if (y >= 278 && y < 302) {
        if (x >= 20 && x < 106) return TS_UI_FM_ACTION_DRONE;
        if (x >= 112 && x < 212) return TS_UI_FM_ACTION_EXTREME;
        if (x >= 218 && x < 304) return TS_UI_FM_ACTION_CHAIN;
    }
    return TS_UI_FM_ACTION_NONE;
}

TsUiFmAction ts_ui_fm_bank_action_from_point(int x, int y)
{
    if (y < 278 || y >= 302) return TS_UI_FM_ACTION_NONE;
    if (x >= 82 && x < 214) return TS_UI_FM_ACTION_BANK_REPLACE;
    if (x >= 220 && x < 370) return TS_UI_FM_ACTION_BANK_NEW_PAGE;
    if (x >= 376 && x < 470) return TS_UI_FM_ACTION_BANK_CANCEL;
    return TS_UI_FM_ACTION_NONE;
}

TsUiFmAction ts_ui_fm_full_action_from_point(int x, int y)
{
    if (y < 274 || y >= 298) return TS_UI_FM_ACTION_NONE;
    if (x >= 92 && x < 218) return TS_UI_FM_ACTION_OVERWRITE;
    if (x >= 224 && x < 374) return TS_UI_FM_ACTION_NEW_PAGE;
    if (x >= 380 && x < 474) return TS_UI_FM_ACTION_CANCEL_FULL;
    return TS_UI_FM_ACTION_NONE;
}

int ts_ui_fm_range_contains(int x, int y)
{
    return x >= 386 && x < 620 && y >= 278 && y < 302;
}

int ts_ui_fm_pitch_root_contains(int x, int y)
{
    return x >= 214 && x < 310 && y >= 218 && y < 242;
}

int ts_ui_fm_pitch_scale_contains(int x, int y)
{
    return x >= 316 && x < 448 && y >= 218 && y < 242;
}

TsUiWaveAction ts_ui_wave_action_from_point(int x, int y)
{
    if (y < 289 || y >= 312) return TS_UI_WAVE_ACTION_NONE;
    for (size_t i = 0; i < sizeof(wave_buttons) / sizeof(wave_buttons[0]); ++i)
        if (x >= wave_buttons[i].x &&
            x < wave_buttons[i].x + wave_buttons[i].width)
            return wave_buttons[i].action;
    return TS_UI_WAVE_ACTION_NONE;
}

TsUiCanvasAction ts_ui_canvas_action_from_point(int x, int y)
{
    if (y < TS_CANVAS_CONTROLS_Y ||
        y >= TS_CANVAS_CONTROLS_Y + TS_CANVAS_CONTROLS_H)
        return TS_UI_CANVAS_ACTION_NONE;
    if (x >= 24 && x < 52) return TS_UI_CANVAS_ACTION_HALF;
    if (x >= 56 && x < 84) return TS_UI_CANVAS_ACTION_DOUBLE;
    if (x >= 456 && x < 476) return TS_UI_CANVAS_ACTION_GRID_COARSER;
    if (x >= 538 && x < 558) return TS_UI_CANVAS_ACTION_GRID_FINER;
    if (x >= 562 && x < 615) return TS_UI_CANVAS_ACTION_GRID_SNAP;
    return TS_UI_CANVAS_ACTION_NONE;
}

int ts_ui_capture_button_from_point(int x, int y)
{
    return x >= 536 && x < 630 && y >= 313 && y < 330;
}

int ts_ui_capture_channels_button_from_point(int x, int y)
{
    return x >= 350 && x < 378 && y >= 313 && y < 330;
}

int ts_ui_overdub_button_from_point(int x, int y)
{
    return x >= 382 && x < 456 && y >= 313 && y < 330;
}

int ts_ui_new_page_button_from_point(int x, int y)
{
    return x >= 461 && x < 531 && y >= 313 && y < 330;
}

int ts_ui_record_keep_button_from_point(int x, int y)
{
    return x >= 400 && x < 456 && y >= 313 && y < 330;
}

int ts_ui_monitor_button_from_point(int x, int y)
{
    return x >= 461 && x < 531 && y >= 313 && y < 330;
}

int ts_ui_record_source_button_from_point(int x, int y)
{
    return x >= 320 && x < 395 && y >= 313 && y < 330;
}

int ts_ui_canvas_edge_from_point(int x, int y)
{
    if (y < TS_CANVAS_HANDLE_Y || y >= TS_CANVAS_HANDLE_Y + TS_CANVAS_HANDLE_H)
        return 0;
    if (x >= TS_CANVAS_LEFT_HANDLE_X &&
        x < TS_CANVAS_LEFT_HANDLE_X + TS_CANVAS_HANDLE_W) return 1;
    if (x >= TS_CANVAS_RIGHT_HANDLE_X &&
        x < TS_CANVAS_RIGHT_HANDLE_X + TS_CANVAS_HANDLE_W) return 2;
    return 0;
}

static int point_in_slider(int x, int y, int left, int top, int width)
{
    return x >= left && x < left + width && y >= top && y < top + 24;
}

TsUiSlider ts_ui_slider_from_point(const TsUiState *ui, int x, int y)
{
    if (point_in_slider(x, y, 10, 233, 72)) return TS_UI_SLIDER_BODY;
    if (point_in_slider(x, y, 88, 233, 72)) return TS_UI_SLIDER_EDGE;
    if (point_in_slider(x, y, 166, 233, 72)) return TS_UI_SLIDER_DRIFT;
    if (ui == NULL) return TS_UI_SLIDER_NONE;
    switch (ui->fx_page) {
    case TS_FX_TUNE:
        if (point_in_slider(x, y, TS_TUNE_REFERENCE_FINE_X, 261, 76))
            return TS_UI_SLIDER_TUNE_FINE;
        if (point_in_slider(x, y, TS_TUNE_REFERENCE_VOLUME_X, 261, 54))
            return TS_UI_SLIDER_TUNE_REFERENCE_VOLUME;
        break;
    case TS_FX_NOISE:
        if (point_in_slider(x, y, 118, 261, 180)) return TS_UI_SLIDER_NOISE_AMOUNT;
        break;
    case TS_FX_SHAPE:
        if (point_in_slider(x, y, 104, 261, 94)) return TS_UI_SLIDER_FILTER_CUTOFF;
        if (point_in_slider(x, y, 202, 261, 80)) return TS_UI_SLIDER_FILTER_RESONANCE;
        if (point_in_slider(x, y, 384, 261, 92)) return TS_UI_SLIDER_SHAPER_DRIVE;
        if (point_in_slider(x, y, 480, 261, 92)) return TS_UI_SLIDER_SHAPER_MIX;
        break;
    case TS_FX_FAMILY:
        if (point_in_slider(x, y, 10, 261, 380)) return TS_UI_SLIDER_VARIATION_RANGE;
        break;
    case TS_FX_DELAY:
        if (point_in_slider(x, y, 118, 261, 92)) return TS_UI_SLIDER_DELAY_TIME;
        if (point_in_slider(x, y, 220, 261, 92)) return TS_UI_SLIDER_DELAY_FEEDBACK;
        if (point_in_slider(x, y, 322, 261, 92)) return TS_UI_SLIDER_DELAY_DAMPING;
        if (point_in_slider(x, y, 424, 261, 92)) return TS_UI_SLIDER_DELAY_MIX;
        break;
    case TS_FX_SPACE:
        if (point_in_slider(x, y, 118, 261, 120)) return TS_UI_SLIDER_REVERB_DECAY;
        if (point_in_slider(x, y, 250, 261, 120)) return TS_UI_SLIDER_REVERB_DAMPING;
        if (point_in_slider(x, y, 382, 261, 120)) return TS_UI_SLIDER_REVERB_MIX;
        break;
    case TS_FX_LOOP:
        if (point_in_slider(x, y, 365, 261, 212)) return TS_UI_SLIDER_LOOP_CROSSFADE;
        break;
    default:
        break;
    }
    return TS_UI_SLIDER_NONE;
}

static int cycle_index(int value, int amount, int count)
{
    int result;
    if (count <= 0) return 0;
    result = (value + amount) % count;
    return result < 0 ? result + count : result;
}

int ts_ui_keyboard_base_note(const TsUiState *ui)
{
    int note = ui != NULL ? ui->keyboard_base_note : TS_KEYBOARD_BASE_NOTE;
    if (note < 0) note = 0;
    if (note > 104) note = 104;
    return note;
}

int ts_ui_keyboard_set_octave(TsUiState *ui, int octave)
{
    if (ui == NULL) return 4;
    if (octave < 0) octave = 0;
    if (octave > 7) octave = 7;
    ui->keyboard_octave = octave;
    ui->keyboard_base_note = (octave + 1) * 12;
    return octave;
}

int ts_ui_keyboard_cycle_octave(TsUiState *ui, int amount)
{
    if (ui == NULL) return 4;
    ui->keyboard_octave = cycle_index(ui->keyboard_octave, amount, 8);
    ui->keyboard_base_note = (ui->keyboard_octave + 1) * 12;
    return ui->keyboard_octave;
}

int ts_ui_keyboard_shift_semitone(TsUiState *ui, int amount)
{
    int note;
    if (ui == NULL) return TS_KEYBOARD_BASE_NOTE;
    note = ui->keyboard_base_note + amount;
    if (note < 0) note = 0;
    if (note > 104) note = 104;
    ui->keyboard_base_note = note;
    ui->keyboard_octave = note / 12 - 1;
    if (ui->keyboard_octave < 0) ui->keyboard_octave = 0;
    if (ui->keyboard_octave > 7) ui->keyboard_octave = 7;
    return note;
}

int ts_ui_palette_cycle_entry(int entry, int amount)
{
    return cycle_index(entry, amount, TS_PALETTE_TAPESISTER_COLOR_COUNT);
}

int ts_ui_palette_cycle_channel(int channel, int amount)
{
    return cycle_index(channel, amount, 5);
}

TsConfigField ts_ui_config_cycle_field(TsConfigField field, int amount)
{
    return (TsConfigField)cycle_index((int)field, amount, TS_CONFIG_FIELD_COUNT);
}

void ts_ui_begin_palette_edit(TsUiState *ui)
{
    if (ui == NULL) return;
    ui->palette_before_edit = ui->palette;
    ui->palette_suggestions = ui->palette;
    ui->palette_open = 1;
    ui->config_open = 0;
}

void ts_ui_finish_palette_edit(TsUiState *ui, int cancel)
{
    if (ui == NULL) return;
    if (cancel) ui->palette = ui->palette_before_edit;
    ui->palette_open = 0;
    ui->config_open = 1;
}

void ts_ui_cycle_panel(TsUiState *ui)
{
    if (ui == NULL) return;
    if (ui->show_keyboard) {
        ui->show_keyboard = 0;
    } else if (!ui->show_recipes && !ui->show_ingredients) {
        ui->show_recipes = 1;
    } else if (ui->show_recipes) {
        ui->show_recipes = 0;
        ui->show_ingredients = 1;
    } else {
        ui->show_ingredients = 0;
        ui->show_keyboard = 1;
    }
}

TsUiPanel ts_ui_panel(const TsUiState *ui)
{
    if (ui == NULL) return TS_UI_PANEL_SAMPLE_TILES;
    if (ui->show_keyboard) return TS_UI_PANEL_KEYBOARD;
    if (ui->show_recipes) return TS_UI_PANEL_CDP;
    if (ui->show_ingredients) return TS_UI_PANEL_DSP;
    return TS_UI_PANEL_SAMPLE_TILES;
}

void ts_ui_select_panel(TsUiState *ui, TsUiPanel panel)
{
    if (ui == NULL || panel < TS_UI_PANEL_SAMPLE_TILES || panel > TS_UI_PANEL_DSP)
        return;
    if (panel == TS_UI_PANEL_CDP && ui->show_recipes) {
        ui->cdp_page = (ui->cdp_page + 1) % TS_CDP_BANK_COUNT;
        return;
    }
    if (panel == TS_UI_PANEL_DSP && ui->show_ingredients) {
        ui->dsp_page = (ui->dsp_page + 1) % TS_DSP_BANK_COUNT;
        return;
    }
    ui->show_keyboard = panel == TS_UI_PANEL_KEYBOARD;
    ui->show_recipes = panel == TS_UI_PANEL_CDP;
    ui->show_ingredients = panel == TS_UI_PANEL_DSP;
}

int ts_ui_transform_auto_audition_allowed(const TsUiState *ui)
{
    return ui == NULL || !ui->workbench_loop_active;
}

TsUiLoopCommand ts_ui_loop_command(const TsUiState *ui, int shift_pressed)
{
    if (ui != NULL && ui->workbench_loop_persistent)
        return shift_pressed ? TS_UI_LOOP_LOCK_RELEASE : TS_UI_LOOP_LOCKED;
    return shift_pressed ? TS_UI_LOOP_LOCK_START : TS_UI_LOOP_START;
}

int ts_ui_loop_transport_can_stop(const TsUiState *ui, int force)
{
    return force || ui == NULL || !ui->workbench_loop_persistent;
}

int ts_ui_space_plays_selection(const TsInstrument *instrument)
{
    return instrument != NULL && instrument->has_selection &&
           instrument->selection_last > instrument->selection_first &&
           instrument->selection_last <= instrument->current.frames;
}

void ts_ui_reset_parent_view(TsUiState *ui, size_t frames)
{
    if (ui == NULL) return;
    ui->parent_view_first = 0;
    ui->parent_view_last = frames;
}

static void valid_parent_view(const TsUiState *ui, size_t frames,
                              size_t *first, size_t *last)
{
    *first = ui != NULL ? ui->parent_view_first : 0;
    *last = ui != NULL ? ui->parent_view_last : frames;
    if (*last <= *first || *last > frames) {
        *first = 0;
        *last = frames;
    }
}

int ts_ui_zoom_parent_view(TsUiState *ui, size_t frames, size_t anchor,
                           float anchor_ratio, float scale)
{
    size_t first, last, span, new_span, new_first;
    if (ui == NULL || frames < 2 || scale <= 0.0f) return 0;
    valid_parent_view(ui, frames, &first, &last);
    span = last - first;
    new_span = (size_t)lrintf((float)span * scale);
    if (new_span < 16u) new_span = frames < 16u ? frames : 16u;
    if (new_span > frames) new_span = frames;
    if (new_span == span) return 0;
    if (anchor > frames) anchor = frames;
    if (anchor_ratio < 0.0f) anchor_ratio = 0.0f;
    if (anchor_ratio > 1.0f) anchor_ratio = 1.0f;
    {
        size_t before = (size_t)lrintf((float)new_span * anchor_ratio);
        new_first = anchor > before ? anchor - before : 0;
    }
    if (new_first + new_span > frames) new_first = frames - new_span;
    ui->parent_view_first = new_first;
    ui->parent_view_last = new_first + new_span;
    return 1;
}

int ts_ui_pan_parent_view(TsUiState *ui, size_t frames, ptrdiff_t amount)
{
    size_t first, last, span, new_first;
    if (ui == NULL || frames < 2 || amount == 0) return 0;
    valid_parent_view(ui, frames, &first, &last);
    span = last - first;
    if (span >= frames) return 0;
    if (amount < 0) {
        size_t magnitude = (size_t)(-amount);
        new_first = magnitude > first ? 0 : first - magnitude;
    } else {
        new_first = first + (size_t)amount;
        if (new_first + span > frames) new_first = frames - span;
    }
    if (new_first == first) return 0;
    ui->parent_view_first = new_first;
    ui->parent_view_last = new_first + span;
    return 1;
}

size_t ts_ui_parent_frame_from_x(const TsUiState *ui, size_t frames, int x, int width)
{
    size_t first, last;
    if (width <= 0 || frames == 0) return 0;
    valid_parent_view(ui, frames, &first, &last);
    if (x < 0) x = 0;
    if (x >= width - 1) return last;
    return first + (size_t)x * (last - first) / (size_t)width;
}

static int frame_x(size_t frame_index, size_t view_first, size_t view_last)
{
    if (view_last <= view_first) return TS_WAVE_X;
    if (frame_index <= view_first) return TS_WAVE_X;
    if (frame_index >= view_last) return TS_WAVE_X + TS_WAVE_W;
    return TS_WAVE_X + (int)((frame_index - view_first) * TS_WAVE_W /
                             (view_last - view_first));
}

const TsTuning *ts_ui_audition_tuning(const TsUiState *ui,
                                      const TsInstrument *instrument)
{
    (void)ui;
    return instrument != NULL ? &instrument->tuning : NULL;
}

const TsTuning *ts_ui_display_tuning(const TsUiState *ui,
                                     const TsInstrument *instrument)
{
    (void)ui;
    return instrument != NULL ? &instrument->audible_tuning : NULL;
}

static float input_dbfs(float level)
{
    if (!isfinite(level) || level <= 0.000001f) return -120.0f;
    return 20.0f * log10f(level);
}

static int input_meter_y(float level)
{
    float db = input_dbfs(level);
    /* The recorder accepts thresholds down to -90 dBFS.  Use the same
       complete range so the threshold line remains spatially truthful. */
    if (db < -90.0f) db = -90.0f;
    if (db > 0.0f) db = 0.0f;
    return TS_WAVE_Y + TS_WAVE_H - 4 -
           (int)lrintf((db + 90.0f) / 90.0f * (TS_WAVE_H - 8));
}

static void live_input_render(TsFramebuffer *fb, const TsUiState *ui)
{
    const int waveform_width = TS_WAVE_W - 28;
    const int meter_x = TS_WAVE_X + TS_WAVE_W - 18;
    const int meter_y = TS_WAVE_Y + 4;
    const int meter_h = TS_WAVE_H - 8;
    char title[112];
    char threshold[24];
    float level_db = input_dbfs(ui->input_level);
    float peak_db = input_dbfs(ui->input_peak);
    int level_y = input_meter_y(ui->input_level);
    int peak_y = input_meter_y(ui->input_peak);
    int threshold_y = input_meter_y(ui->input_threshold);
    uint32_t meter_color = ui->input_clipping ? RGB(255, 74, 58) : PAL_VOLUME;
    frame(fb, 10, 40, 620, 164, RGB(42, 39, 42), RGB(105, 98, 105));
    if (ui->capture_state == TS_CAPTURE_RECORDING && ui->input_sample_rate > 0u)
        snprintf(title, sizeof(title),
                 "INPUT  %+.1F DBFS   PEAK %+.1F DBFS   REC %.2F S",
                 level_db, peak_db,
                 (double)ui->capture_recorded_frames / ui->input_sample_rate);
    else
        snprintf(title, sizeof(title),
                 "INPUT  %+.1F DBFS   PEAK %+.1F DBFS   ARMED",
                 level_db, peak_db);
    text(fb, 20, 49, title, ui->input_clipping ? RGB(255, 96, 72) : PAL_INSTRUMENT, 1);
    wave_rect(fb, TS_WAVE_X, TS_WAVE_Y, TS_WAVE_W, TS_WAVE_H, RGB(8, 8, 8));
    for (int y = TS_WAVE_Y + 20; y < TS_WAVE_Y + TS_WAVE_H; y += 20)
        wave_rect(fb, TS_WAVE_X, y, waveform_width, 1,
                  contrast_color(PAL_DESKTOP,
                                 active_palette()->desktop_contrast, 0.72f));
    wave_rect(fb, TS_WAVE_X, TS_WAVE_Y + TS_WAVE_H / 2,
              waveform_width, 1, PAL_BUTTON);
    if (ui->capture_state == TS_CAPTURE_RECORDING && ui->input_wave_columns > 0u) {
        size_t visible = ui->input_wave_columns;
        if (visible > (size_t)waveform_width) visible = (size_t)waveform_width;
        for (size_t column = 0; column < visible; ++column) {
            float low = ui->input_wave_minimum[column];
            float high = ui->input_wave_maximum[column];
            int middle = TS_WAVE_Y + TS_WAVE_H / 2;
            int y0 = middle - (int)lrintf(high * (TS_WAVE_H / 2 - 6));
            int y1 = middle - (int)lrintf(low * (TS_WAVE_H / 2 - 6));
            if (y0 < TS_WAVE_Y + 2) y0 = TS_WAVE_Y + 2;
            if (y1 > TS_WAVE_Y + TS_WAVE_H - 2) y1 = TS_WAVE_Y + TS_WAVE_H - 2;
            wave_line(fb, TS_WAVE_X + (int)column, y0,
                      TS_WAVE_X + (int)column, y1, PAL_NOTE);
        }
    } else {
        text(fb, 202, 126, "WAITING FOR THRESHOLD", RGB(120, 113, 121), 2);
    }
    wave_rect(fb, meter_x, meter_y, 12, meter_h, RGB(24, 24, 24));
    if (level_y < meter_y) level_y = meter_y;
    if (level_y > meter_y + meter_h) level_y = meter_y + meter_h;
    wave_rect(fb, meter_x + 2, level_y, 8,
              meter_y + meter_h - level_y, meter_color);
    wave_rect(fb, meter_x - 2, peak_y, 16, 2,
              ui->input_clipping ? RGB(255, 74, 58) : PAL_EFFECT);
    wave_rect(fb, meter_x - 4, threshold_y, 20, 2, PAL_TUNING);
    snprintf(threshold, sizeof(threshold), "TH %.0F", input_dbfs(ui->input_threshold));
    wave_text(fb, meter_x - 48,
              threshold_y > TS_WAVE_Y + 10 ? threshold_y - 9 : threshold_y + 3,
              threshold, PAL_TUNING, 1);
    if (ui->input_clipping)
        wave_text(fb, meter_x - 34, TS_WAVE_Y + 5, "CLIP", RGB(255, 74, 58), 1);
}

void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument)
{
    render_palette = &ui->palette;
    const TsTuning *display_tuning = &ui->tune_reference;
    int showing_bank = ui->bank_view_slot >= 0 && ui->bank_view_slot < TS_BANK_SLOT_COUNT;
    int showing_parent = !showing_bank && ui->audition_source == TS_AUDITION_PARENT;
    const TsBankSlot *shown_slot = showing_bank ? &instrument->bank[ui->bank_view_slot] : NULL;
    const TsSample *sample = showing_bank ? &shown_slot->sample :
                             showing_parent ? &instrument->parent : &instrument->current;
    size_t view_first = instrument->view_first;
    size_t view_last = instrument->view_last;
    size_t selection_first = instrument->selection_first;
    size_t selection_last = instrument->selection_last;
    size_t loop_first = instrument->loop_first;
    size_t loop_last = instrument->loop_last;
    uint32_t grid_divisions = instrument->grid_divisions;
    int grid_snap = instrument->grid_snap;
    int has_selection = !showing_bank && instrument->has_selection;
    int has_loop = showing_bank ? shown_slot->has_loop : instrument->has_loop;
    TsLoopMode display_loop_mode = showing_bank ? shown_slot->loop_mode :
                                                  instrument->loop_mode;
    if (showing_bank) {
        view_first = 0;
        view_last = sample->frames;
        selection_first = selection_last = 0;
        loop_first = shown_slot->loop_first;
        loop_last = shown_slot->loop_last;
        grid_divisions = shown_slot->edit.grid_divisions;
        grid_snap = shown_slot->edit.grid_snap;
    } else if (showing_parent) {
        valid_parent_view(ui, instrument->parent.frames, &view_first, &view_last);
        selection_first += instrument->crop_first;
        selection_last += instrument->crop_first;
        loop_first += instrument->crop_first;
        loop_last += instrument->crop_first;
    }
    clear(fb, PAL_DESKTOP);

    rect(fb, 0, 0, TS_UI_WIDTH, 32, RGB(12, 12, 12));
    text(fb, 14, 9, "TAPESISTER",
         ui->sister_warning ? PAL_VOLUME :
         ui->sister_enabled ? PAL_WAVE_RIGHT : PAL_TEXT, 2);
    if (ui->sister_enabled) {
        rect(fb, 13, 27, 112, 2,
             ui->sister_rolling && !ui->sister_held ? PAL_WAVE_LEFT : PAL_BUTTON);
        /* Shape as well as color communicates the live/held state. */
        if (ui->sister_held) {
            rect(fb, 130, 10, 3, 12, PAL_TUNING);
            rect(fb, 136, 10, 3, 12, PAL_TUNING);
        } else if (ui->sister_rolling) {
            for (int row = 0; row < 6; ++row)
                rect(fb, 130 + row, 12 + row, 1, 12 - row * 2, PAL_WAVE_LEFT);
        }
        if (ui->sister_monitor_enabled)
            text(fb, 145, 9, "M", PAL_WAVE_SUM, 1);
    }
    if (ui->sister_capture_active) {
        rect(fb, 4, 3, 150, 2, PAL_VOLUME);
        rect(fb, 4, 28, 150, 2, PAL_VOLUME);
        rect(fb, 4, 3, 2, 27, PAL_VOLUME);
        rect(fb, 152, 3, 2, 27, PAL_VOLUME);
        text(fb, 160, 13, "REC", PAL_VOLUME, 1);
    }
    {
        char history[24];
        snprintf(history, sizeof(history), "UNDO %02d/%02d",
                 instrument->undo_count, TS_HISTORY_DEPTH);
        text(fb, 280, 13, history, PAL_EFFECT, 1);
    }
    button(fb, 350, 4, 76, "CONFIG", ui->config_open);
    button(fb, 431, 4, 80, "FT2 LINK", ui->exchange_dialog != TS_UI_EXCHANGE_NONE);
    button(fb, 516, 4, 52, "SAVE", 0);
    button(fb, 573, 4, 57, "EXPORT", 0);

    frame(fb, 10, 40, 620, 164, RGB(42, 39, 42), RGB(105, 98, 105));
    if (sample->frames) {
        char tile[96], info[112];
        int tile_number = showing_bank ? ui->bank_view_slot + 1 :
                          instrument->selected_slot + 1;
        snprintf(tile, sizeof(tile), "TILE %02d %c %.34s", tile_number,
                 sample->channels == 2u ? 'S' : 'M', sample->name);
        if (showing_bank && shown_slot->occupied) {
            snprintf(info, sizeof(info), "BANK %02d %s  %.2F SEC",
                     ui->bank_view_slot + 1,
                     ts_bank_capture_name(shown_slot->capture_kind),
                     (double)sample->frames / sample->sample_rate);
        }
        else if (showing_bank)
            snprintf(info, sizeof(info), "BANK %02d EMPTY - SILENCE",
                     ui->bank_view_slot + 1);
        else
            snprintf(info, sizeof(info), "EDITING TILE %02d  %u HZ %.2F SEC",
                     instrument->selected_slot + 1, sample->sample_rate,
                     (double)sample->frames / sample->sample_rate);
        text(fb, 20, 49, tile, PAL_INSTRUMENT, 1);
        text(fb, 310, 49, info, PAL_EFFECT, 1);
    } else {
        char empty[40];
        snprintf(empty, sizeof(empty), "TILE %02d EMPTY", instrument->selected_slot + 1);
        text(fb, 20, 49, empty, PAL_INSTRUMENT, 1);
    }
    mini_button(fb, 526, 43, 94,
                ts_waveform_display_name((TsWaveformDisplayMode)
                                         ui->config.waveform_display_mode),
                ui->config.waveform_display_mode != TS_WAVEFORM_DISPLAY_STEREO);
    wave_rect(fb, TS_WAVE_X, TS_WAVE_Y, TS_WAVE_W, TS_WAVE_H, RGB(8, 8, 8));
    if (grid_divisions < TS_GRID_DIVISION_MIN ||
        grid_divisions > TS_GRID_DIVISION_MAX ||
        (grid_divisions & (grid_divisions - 1u)) != 0u)
        grid_divisions = TS_GRID_DIVISION_DEFAULT;
    if (!showing_parent && sample->frames > 0 && view_last > view_first) {
        uint32_t grid_color = contrast_color(
            PAL_DESKTOP, active_palette()->desktop_contrast,
            grid_snap ? 1.35f : 0.72f);
        size_t quotient = sample->frames / grid_divisions;
        size_t remainder = sample->frames % grid_divisions;
        for (uint32_t division = 0; division <= grid_divisions; ++division) {
            size_t frame = quotient * division + remainder * division / grid_divisions;
            if (frame >= view_first && frame <= view_last) {
                int x = frame_x(frame, view_first, view_last);
                if (x == TS_WAVE_X + TS_WAVE_W) --x;
                wave_rect(fb, x, TS_WAVE_Y, 1, TS_WAVE_H, grid_color);
            }
        }
    }
    for (int y = TS_WAVE_Y + 20; y < TS_WAVE_Y + TS_WAVE_H; y += 20)
        wave_rect(fb, TS_WAVE_X, y, TS_WAVE_W, 1,
                  contrast_color(PAL_DESKTOP,
                                 active_palette()->desktop_contrast, 0.72f));
    wave_rect(fb, TS_WAVE_X, TS_WAVE_Y + TS_WAVE_H / 2,
              TS_WAVE_W, 1, PAL_BUTTON);

    if (has_loop && loop_last > view_first && loop_first < view_last) {
        int lx0 = frame_x(loop_first, view_first, view_last);
        int lx1 = frame_x(loop_last, view_first, view_last);
        wave_rect(fb, lx0, TS_WAVE_Y, lx1 - lx0, TS_WAVE_H, RGB(5, 24, 48));
    }

    if (has_selection && selection_last > view_first &&
        selection_first < view_last) {
        int sx0 = frame_x(selection_first, view_first, view_last);
        int sx1 = frame_x(selection_last, view_first, view_last);
        /* Blend over the already-rendered grid instead of painting an opaque
           block. This preserves both vertical snap divisions and horizontal
           amplitude guides through the selection. */
        wave_blend_rect(fb, sx0, TS_WAVE_Y, sx1 - sx0, TS_WAVE_H,
                        PAL_WAVE_SELECTION, 45u);
    }
    if (sample->frames && view_last > view_first) {
        TsWaveformRequest request;
        TsWaveformCache *cache = waveform_cache(TS_UI_WAVEFORM_MAIN);
        if (view_last > sample->frames) view_last = sample->frames;
        memset(&request, 0, sizeof(request));
        request.sample = sample;
        request.first = view_first;
        request.last = view_last;
        request.width = TS_WAVE_W;
        request.detect_zero_crossings = 1;
        request.revision = ui->waveform_revisions[TS_UI_WAVEFORM_MAIN];
        (void)ts_waveform_cache_prepare(cache, &request);
        for (int x = 0; x < TS_WAVE_W; ++x) {
            const TsWaveformColumn *analysis = &cache->columns[x];
            size_t begin = analysis->first;
            size_t end = analysis->last;
            int middle = TS_WAVE_Y + TS_WAVE_H / 2;
            int selected = has_selection && end > selection_first &&
                           begin < selection_last;
            TsWaveformDisplayColumn display = ts_waveform_display_column(
                analysis, sample->channels,
                (TsWaveformDisplayMode)ui->config.waveform_display_mode);
            uint32_t color = selected ? PAL_BLOCK_TEXT :
                             ui->config.waveform_display_mode ==
                                 TS_WAVEFORM_DISPLAY_MONO_SUM ? PAL_WAVE_SUM :
                             PAL_WAVE_LEFT;
            if (display.stereo) {
                int left_y0 = middle - (int)(display.left_maximum *
                                              (TS_WAVE_H / 2 - 6));
                int left_y1 = middle - (int)(display.left_minimum *
                                              (TS_WAVE_H / 2 - 6));
                int right_y0 = middle - (int)(display.right_maximum *
                                               (TS_WAVE_H / 2 - 6));
                int right_y1 = middle - (int)(display.right_minimum *
                                               (TS_WAVE_H / 2 - 6));
                uint32_t right_color = selected ? PAL_TUNING : PAL_WAVE_RIGHT;
                wave_line(fb, TS_WAVE_X + x, left_y0,
                          TS_WAVE_X + x, left_y1, color);
                wave_line(fb, TS_WAVE_X + x, right_y0,
                          TS_WAVE_X + x, right_y1, right_color);
            } else {
                int y0 = middle - (int)(display.left_maximum * (TS_WAVE_H / 2 - 6));
                int y1 = middle - (int)(display.left_minimum * (TS_WAVE_H / 2 - 6));
                wave_line(fb, TS_WAVE_X + x, y0,
                          TS_WAVE_X + x, y1, color);
            }
            if (analysis->has_zero_crossing)
                wave_rect(fb, TS_WAVE_X + x, middle - 1, 1, 3, PAL_VOLUME);
        }
    } else {
        text(fb, showing_bank ? 199 : 211, 135,
             showing_bank ? "EMPTY BANK SLOT" : "DROP WAV HERE",
             RGB(120, 113, 121), 2);
    }

    if (!showing_bank && !showing_parent && ui->amplitude_draw_dragging &&
        ui->amplitude_profile_last_x >= ui->amplitude_profile_first_x) {
        int middle = TS_WAVE_Y + TS_WAVE_H / 2;
        int previous_x = -1;
        int previous_top = middle;
        int previous_bottom = middle;
        for (int x = ui->amplitude_profile_first_x;
             x <= ui->amplitude_profile_last_x; ++x) {
            int top;
            int bottom;
            int amount;
            if (x < 0 || x >= TS_WAVE_W || !ui->amplitude_profile_set[x])
                continue;
            amount = (int)lrintf(ui->amplitude_profile[x] * 0.5f *
                                 (float)(TS_WAVE_H / 2 - 8));
            top = middle - amount;
            bottom = middle + amount;
            if (previous_x >= 0) {
                wave_line(fb, TS_WAVE_X + previous_x, previous_top,
                          TS_WAVE_X + x, top, PAL_EFFECT);
                wave_line(fb, TS_WAVE_X + previous_x, previous_bottom,
                          TS_WAVE_X + x, bottom, PAL_EFFECT);
            }
            previous_x = x;
            previous_top = top;
            previous_bottom = bottom;
        }
        if (ui->amplitude_profile_first_x >= 0 &&
            ui->amplitude_profile_first_x < TS_WAVE_W &&
            ui->amplitude_profile_set[ui->amplitude_profile_first_x]) {
            int amount = (int)lrintf(
                ui->amplitude_profile[ui->amplitude_profile_first_x] * 0.5f *
                (float)(TS_WAVE_H / 2 - 8));
            wave_line(fb, TS_WAVE_X + ui->amplitude_profile_first_x,
                      middle - amount,
                      TS_WAVE_X + ui->amplitude_profile_first_x,
                      middle + amount, PAL_EFFECT);
        }
        if (ui->amplitude_profile_last_x >= 0 &&
            ui->amplitude_profile_last_x < TS_WAVE_W &&
            ui->amplitude_profile_set[ui->amplitude_profile_last_x]) {
            int amount = (int)lrintf(
                ui->amplitude_profile[ui->amplitude_profile_last_x] * 0.5f *
                (float)(TS_WAVE_H / 2 - 8));
            wave_line(fb, TS_WAVE_X + ui->amplitude_profile_last_x,
                      middle - amount,
                      TS_WAVE_X + ui->amplitude_profile_last_x,
                      middle + amount, PAL_EFFECT);
        }
    }

    if (ui->tape_dragging && !showing_bank && !showing_parent &&
        ui->tape_source_last > ui->tape_source_first &&
        ui->tape_source_last <= instrument->current.frames) {
        size_t source_length = ui->tape_source_last - ui->tape_source_first;
        int64_t destination = ui->tape_destination;
        int first_visible = TS_WAVE_X + (int)((destination - (int64_t)view_first) *
                            TS_WAVE_W / (int64_t)(view_last - view_first));
        int last_visible = TS_WAVE_X + (int)(((destination + (int64_t)source_length) -
                           (int64_t)view_first) * TS_WAVE_W /
                           (int64_t)(view_last - view_first));
        int clipped_first = first_visible < TS_WAVE_X ? TS_WAVE_X : first_visible;
        int clipped_last = last_visible > TS_WAVE_X + TS_WAVE_W ?
                           TS_WAVE_X + TS_WAVE_W : last_visible;
        for (int x = clipped_first; x < clipped_last; ++x) {
            int64_t frame_begin = (int64_t)view_first +
                                  (int64_t)(x - TS_WAVE_X) *
                                  (int64_t)(view_last - view_first) / TS_WAVE_W;
            int64_t frame_end = (int64_t)view_first +
                                (int64_t)(x - TS_WAVE_X + 1) *
                                (int64_t)(view_last - view_first) / TS_WAVE_W;
            int64_t source_begin = frame_begin - destination +
                                   (int64_t)ui->tape_source_first;
            int64_t source_end = frame_end - destination +
                                 (int64_t)ui->tape_source_first;
            if (source_begin < (int64_t)ui->tape_source_first)
                source_begin = (int64_t)ui->tape_source_first;
            if (source_end > (int64_t)ui->tape_source_last)
                source_end = (int64_t)ui->tape_source_last;
            if (source_end <= source_begin &&
                source_begin >= (int64_t)ui->tape_source_first &&
                source_begin < (int64_t)ui->tape_source_last)
                source_end = source_begin + 1;
            if (source_begin < source_end) {
                float low = 1.0f;
                float high = -1.0f;
                int middle = TS_WAVE_Y + TS_WAVE_H / 2;
                int y0;
                int y1;
                for (int64_t source_at = source_begin; source_at < source_end; ++source_at) {
                    float value = instrument->current.data[source_at];
                    if (value < low) low = value;
                    if (value > high) high = value;
                }
                y0 = middle - (int)(high * (TS_WAVE_H / 2 - 6));
                y1 = middle - (int)(low * (TS_WAVE_H / 2 - 6));
                if (y0 == y1) wave_rect(fb, x, y0 - 1, 1, 3, PAL_EFFECT);
                else wave_line(fb, x, y0, x, y1, PAL_EFFECT);
            }
        }
        if (clipped_last > clipped_first) {
            wave_rect(fb, clipped_first, TS_WAVE_Y + 2, clipped_last - clipped_first, 2,
                 PAL_EFFECT);
            wave_rect(fb, clipped_first, TS_WAVE_Y + TS_WAVE_H - 4,
                 clipped_last - clipped_first, 2, PAL_EFFECT);
            wave_rect(fb, clipped_first, TS_WAVE_Y + 2, 2, TS_WAVE_H - 4, PAL_EFFECT);
            wave_rect(fb, clipped_last - 2, TS_WAVE_Y + 2, 2, TS_WAVE_H - 4, PAL_EFFECT);
        }
    }

    if (has_loop && loop_last > view_first && loop_first < view_last) {
        int lx0 = frame_x(loop_first, view_first, view_last);
        int lx1 = frame_x(loop_last, view_first, view_last);
        wave_rect(fb, lx0, TS_WAVE_Y, 2, TS_WAVE_H, PAL_TUNING);
        wave_rect(fb, lx1 - 2, TS_WAVE_Y, 2, TS_WAVE_H, PAL_TUNING);
        wave_rect(fb, lx0, TS_WAVE_Y, 7, 4, PAL_TUNING);
        wave_rect(fb, lx1 - 7, TS_WAVE_Y + TS_WAVE_H - 4, 7, 4, PAL_TUNING);
        {
            int cy = TS_WAVE_Y + 10;
            int center = (lx0 + lx1) / 2;
            if (display_loop_mode != TS_LOOP_REVERSE) {
                wave_line(fb, center - 8, cy, center + 8, cy, PAL_TUNING);
                wave_line(fb, center + 8, cy, center + 3, cy - 4, PAL_TUNING);
                wave_line(fb, center + 8, cy, center + 3, cy + 4, PAL_TUNING);
            }
            if (display_loop_mode != TS_LOOP_FORWARD) {
                int offset = display_loop_mode == TS_LOOP_PING_PONG ? 18 : 0;
                wave_line(fb, center - 8, cy + offset, center + 8, cy + offset, PAL_TUNING);
                wave_line(fb, center - 8, cy + offset, center - 3, cy + offset - 4, PAL_TUNING);
                wave_line(fb, center - 8, cy + offset, center - 3, cy + offset + 4, PAL_TUNING);
            }
        }
    }

    if (has_selection && selection_last > selection_first &&
        selection_last > view_first && selection_first < view_last &&
        sample->sample_rate > 0) {
        char duration[40];
        size_t visible_first = selection_first > view_first ?
                               selection_first : view_first;
        int selection_x = frame_x(visible_first, view_first, view_last);
        double seconds = (double)(selection_last - selection_first) /
                         (double)sample->sample_rate;
        snprintf(duration, sizeof(duration), "SEL %.3F SEC", seconds);
        wave_text(fb, selection_x + 4, TS_WAVE_Y + 5, duration, PAL_EFFECT, 1);
    }

    if (ui->has_stretch_readout) {
        char pitch[64];
        snprintf(pitch, sizeof(pitch), "PITCH %+.2F ST  TIME X%.3F",
                 ui->stretch_pitch_semitones,
                 ui->stretch_duration_ratio);
        wave_text(fb, TS_WAVE_X + TS_WAVE_W - 200, TS_WAVE_Y + 5,
                  pitch, PAL_EFFECT, 1);
    }

    {
        int playhead_x = -1;
        uint32_t playhead_color = PAL_MOUSE;
        int playback_matches = ui->playback_active && ui->playhead_frames > 0 &&
            ((showing_bank && ui->playhead_bank_slot == ui->bank_view_slot) ||
             (!showing_bank && ui->playhead_bank_slot < 0 &&
              ui->playhead_source == ui->audition_source));
        if (playback_matches && ui->playhead_frame >= view_first &&
            ui->playhead_frame <= view_last) {
            playhead_color = ui->playhead_source == TS_AUDITION_PARENT ?
                             PAL_INSTRUMENT : PAL_MOUSE;
            playhead_x = frame_x(ui->playhead_frame, view_first, view_last);
        } else if (!showing_bank && instrument->has_playhead) {
            size_t editor_playhead = ui->audition_source == TS_AUDITION_PARENT ?
                instrument->crop_first + instrument->playhead_frame :
                instrument->playhead_frame;
            if (editor_playhead >= view_first && editor_playhead <= view_last)
                playhead_x = frame_x(editor_playhead, view_first, view_last);
        }
        if (playhead_x >= TS_WAVE_X && playhead_x <= TS_WAVE_X + TS_WAVE_W) {
            if (playhead_x == TS_WAVE_X + TS_WAVE_W) --playhead_x;
            wave_rect(fb, playhead_x, TS_WAVE_Y, 2, TS_WAVE_H, playhead_color);
            wave_rect(fb, playhead_x - 2, TS_WAVE_Y, 6, 3, playhead_color);
        }
    }

    if (!showing_bank && !showing_parent && sample->frames >= TS_CANVAS_MIN_FRAMES) {
        char divisions[16];
        uint32_t handle_color = ui->canvas_gesture.active ? PAL_MOUSE : PAL_EFFECT;
        if (ui->canvas_gesture.active && sample->sample_rate > 0 &&
            ui->canvas_drag_start_frames > 0) {
            char canvas[64];
            double seconds = (double)sample->frames / sample->sample_rate;
            double change = ((double)sample->frames -
                             (double)ui->canvas_drag_start_frames) /
                            sample->sample_rate;
            snprintf(canvas, sizeof(canvas), "CANVAS %.3F S (%+.3F S)",
                     seconds, change);
            wave_text(fb, TS_WAVE_X + TS_WAVE_W - 194, TS_WAVE_Y + 5,
                      canvas, PAL_EFFECT, 1);
        }
        wave_rect(fb, TS_CANVAS_LEFT_HANDLE_X, TS_CANVAS_HANDLE_Y,
                  TS_CANVAS_HANDLE_W, 2, handle_color);
        wave_rect(fb, TS_CANVAS_LEFT_HANDLE_X,
                  TS_CANVAS_HANDLE_Y + TS_CANVAS_HANDLE_H - 2,
                  TS_CANVAS_HANDLE_W, 2, handle_color);
        wave_rect(fb, TS_CANVAS_LEFT_HANDLE_X, TS_CANVAS_HANDLE_Y,
                  2, TS_CANVAS_HANDLE_H, handle_color);
        wave_rect(fb, TS_CANVAS_LEFT_HANDLE_X + TS_CANVAS_HANDLE_W - 2,
                  TS_CANVAS_HANDLE_Y, 2, TS_CANVAS_HANDLE_H, handle_color);
        wave_rect(fb, TS_CANVAS_RIGHT_HANDLE_X, TS_CANVAS_HANDLE_Y,
                  TS_CANVAS_HANDLE_W, 2, handle_color);
        wave_rect(fb, TS_CANVAS_RIGHT_HANDLE_X,
                  TS_CANVAS_HANDLE_Y + TS_CANVAS_HANDLE_H - 2,
                  TS_CANVAS_HANDLE_W, 2, handle_color);
        wave_rect(fb, TS_CANVAS_RIGHT_HANDLE_X, TS_CANVAS_HANDLE_Y,
                  2, TS_CANVAS_HANDLE_H, handle_color);
        wave_rect(fb, TS_CANVAS_RIGHT_HANDLE_X + TS_CANVAS_HANDLE_W - 2,
                  TS_CANVAS_HANDLE_Y, 2, TS_CANVAS_HANDLE_H, handle_color);
        mini_button(fb, 24, TS_CANVAS_CONTROLS_Y, 28, "/2", 0);
        mini_button(fb, 56, TS_CANVAS_CONTROLS_Y, 28, "X2", 0);
        mini_button(fb, 456, TS_CANVAS_CONTROLS_Y, 20, "<", 0);
        snprintf(divisions, sizeof(divisions), "DIV %u", grid_divisions);
        mini_button(fb, 480, TS_CANVAS_CONTROLS_Y, 54, divisions, 0);
        mini_button(fb, 538, TS_CANVAS_CONTROLS_Y, 20, ">", 0);
        mini_button(fb, 562, TS_CANVAS_CONTROLS_Y, 53,
                    grid_snap == TS_GRID_SNAP_ALL ? "SNAP" :
                    grid_snap == TS_GRID_SNAP_MOVE_ONLY ? "MOVE" : "OFF",
                    grid_snap != TS_GRID_SNAP_OFF);
        mini_button(fb, TS_WAVE_X + TS_WAVE_W - 54, TS_WAVE_Y + 4,
                    50, "DRAW", ui->amplitude_draw_mode);
    }

    if (ui->input_meter_active) live_input_render(fb, ui);

    button(fb, 10, 205, 70, "LOAD", ui->browser.mode == TS_BROWSER_LOAD_WAV);
    button(fb, 85, 205, 82, "CREATE", 0);
    button(fb, 172, 205, 70, "VARY", 0);
    button(fb, 247, 205, 78,
           ui->workbench_loop_persistent ? "LOOP LOCK" : "LOOP",
           ui->workbench_loop_active);
    button(fb, 330, 205, 72, "DRONE", ui->drone_open);

    bipolar_slider(fb, 10, 233, 72, "BODY",
                   ui->material_macro_gesture.active &&
                   ui->material_macro_gesture.macro ==
                       TS_MATERIAL_MACRO_BODY ?
                   ui->material_macro_amount : 0.0f, PAL_INSTRUMENT);
    bipolar_slider(fb, 88, 233, 72, "EDGE",
                   ui->material_macro_gesture.active &&
                   ui->material_macro_gesture.macro ==
                       TS_MATERIAL_MACRO_EDGE ?
                   ui->material_macro_amount : 0.0f, PAL_VOLUME);
    bipolar_slider(fb, 166, 233, 72, "DRIFT",
                   ui->material_macro_gesture.active &&
                   ui->material_macro_gesture.macro ==
                       TS_MATERIAL_MACRO_DRIFT ?
                   ui->material_macro_amount : 0.0f, PAL_TUNING);
    slider(fb, 244, 233, 86, "WARP", ui->warp_amount, PAL_MOUSE);
    slider(fb, 505, 205, 125, "SMEAR", ui->smear_amount, PAL_MOUSE);
    slider(fb, 407, 205, 93, "TEAR", ui->tear_amount, PAL_EFFECT);
    button(fb, 335, 233, 34, "EDIT", ui->fx_page == TS_FX_EDIT);
    button(fb, 372, 233, 34, "TUNE", ui->fx_page == TS_FX_TUNE);
    button(fb, 409, 233, 36, "NOIS", ui->fx_page == TS_FX_NOISE);
    button(fb, 448, 233, 38, "SHAP", ui->fx_page == TS_FX_SHAPE);
    button(fb, 489, 233, 34, "VAR", ui->fx_page == TS_FX_FAMILY);
    button(fb, 526, 233, 36, "DELY", ui->fx_page == TS_FX_DELAY);
    button(fb, 565, 233, 29, "SPC", ui->fx_page == TS_FX_SPACE);
    button(fb, 597, 233, 33, "LOOP", ui->fx_page == TS_FX_LOOP);

    if (ui->fx_page == TS_FX_EDIT) {
        button(fb, 10, 261, 55, "COPY", 0);
        button(fb, 69, 261, 47, "CUT", 0);
        button(fb, 120, 261, 61, "PASTE", 0);
        button(fb, 185, 261, 47, "FIT", 0);
        button(fb, 236, 261, 55, "REV", 0);
        button(fb, 295, 261, 65, "NORM", 0);
        button(fb, 364, 261, 55, "-3 DB", 0);
        button(fb, 423, 261, 55, "+3 DB", 0);
        button(fb, 482, 261, 69, "FADE IN", 0);
        button(fb, 555, 261, 75, "FADE OUT", 0);
    } else if (ui->fx_page == TS_FX_TUNE) {
        char root[32];
        char note[12];
        char frequency[32];
        char fine[32];
        snprintf(root, sizeof(root), "REF %s",
                 ts_midi_note_name(display_tuning->root_note, note, sizeof(note)));
        snprintf(fine, sizeof(fine), "R %+.0F C",
                 display_tuning->fine_tune_cents);
        snprintf(frequency, sizeof(frequency), "%.2F HZ",
                 ts_tuning_frequency(display_tuning));
        char reference_volume[16];
        snprintf(reference_volume, sizeof(reference_volume), "V%d",
                 ui->config.reference_tone_volume);
        button(fb, TS_TUNE_MATERIAL_SEMI_DOWN_X, 261, 52, "MAT -ST", 0);
        button(fb, TS_TUNE_MATERIAL_SEMI_UP_X, 261, 52, "MAT +ST", 0);
        button(fb, TS_TUNE_MATERIAL_CENT_DOWN_X, 261, 42, "-1 C", 0);
        button(fb, TS_TUNE_MATERIAL_CENT_UP_X, 261, 42, "+1 C", 0);
        button(fb, TS_TUNE_REFERENCE_DOWN_X, 261, 34, "R-", 0);
        button(fb, TS_TUNE_REFERENCE_NOTE_X, 261, 64, root, 1);
        button(fb, TS_TUNE_REFERENCE_UP_X, 261, 34, "R+", 0);
        slider(fb, TS_TUNE_REFERENCE_FINE_X, 261, 76, fine,
               (display_tuning->fine_tune_cents + 100.0f) / 200.0f, PAL_TUNING);
        button(fb, TS_TUNE_REFERENCE_TONE_X, 261, 72, frequency,
               ui->tune_reference_active);
        slider(fb, TS_TUNE_REFERENCE_VOLUME_X, 261, 54, reference_volume,
               (float)ui->config.reference_tone_volume / 100.0f, PAL_VOLUME);
        button(fb, TS_TUNE_DETECT_X, 261, 58,
               ui->has_pitch_suggestion ? "MATCH" : "DETECT",
               ui->has_pitch_suggestion);
    } else if (ui->fx_page == TS_FX_NOISE) {
        char color[32];
        button(fb, 10, 261, 94, instrument->process.noise_enabled ? "NOISE ON" : "NOISE OFF",
               instrument->process.noise_enabled);
        slider(fb, 118, 261, 180, "AMOUNT", instrument->process.noise_amount, PAL_NOTE);
        snprintf(color, sizeof(color), "COLOR %s", ts_noise_color_name(instrument->process.noise_color));
        button(fb, 312, 261, 150, color, 0);
    } else if (ui->fx_page == TS_FX_SHAPE) {
        char filter[28];
        char shaper[28];
        float cutoff = logf(instrument->process.filter_cutoff_hz / 20.0f) /
                       logf(1000.0f);
        float drive = (instrument->process.shaper_drive - 1.0f) / 15.0f;
        snprintf(filter, sizeof(filter), "FILTER %s",
                 instrument->process.filter_enabled ?
                 ts_filter_mode_name(instrument->process.filter_mode) : "OFF");
        snprintf(shaper, sizeof(shaper), "SHAPER %s",
                 instrument->process.shaper_enabled ?
                 ts_shaper_mode_name(instrument->process.shaper_mode) : "OFF");
        button(fb, 10, 261, 90, filter, instrument->process.filter_enabled);
        slider(fb, 104, 261, 94, "CUTOFF", cutoff, PAL_INSTRUMENT);
        slider(fb, 202, 261, 80, "RES", instrument->process.filter_resonance, PAL_TUNING);
        button(fb, 286, 261, 94, shaper, instrument->process.shaper_enabled);
        slider(fb, 384, 261, 92, "DRIVE", drive, PAL_VOLUME);
        slider(fb, 480, 261, 92, "MIX", instrument->process.shaper_mix, PAL_EFFECT);
    } else if (ui->fx_page == TS_FX_FAMILY) {
        char mutation[32];
        snprintf(mutation, sizeof(mutation), "RANGE %d",
                 (int)lrintf(instrument->family_mutation * 100.0f));
        slider(fb, 10, 261, 380, mutation, instrument->family_mutation, PAL_VOLUME);
        button(fb, 398, 261, 100,
               instrument->family_trajectory ? "CHAIN ON" : "CHAIN OFF",
               instrument->family_trajectory);
        button(fb, 506, 261, 124, "FM LOGIC", ui->fm_open);
    } else if (ui->fx_page == TS_FX_DELAY) {
        button(fb, 10, 261, 94, instrument->process.delay_enabled ? "DELAY ON" : "DELAY OFF",
               instrument->process.delay_enabled);
        slider(fb, 118, 261, 92, "TIME",
               (instrument->process.delay_seconds - 0.005f) / 0.995f, PAL_NOTE);
        slider(fb, 220, 261, 92, "FEEDBACK", instrument->process.delay_feedback / 0.85f, PAL_VOLUME);
        slider(fb, 322, 261, 92, "DAMP", instrument->process.delay_damping, PAL_TUNING);
        slider(fb, 424, 261, 92, "MIX", instrument->process.delay_mix, PAL_EFFECT);
    } else if (ui->fx_page == TS_FX_SPACE) {
        button(fb, 10, 261, 94, instrument->process.reverb_enabled ? "SPACE ON" : "SPACE OFF",
               instrument->process.reverb_enabled);
        slider(fb, 118, 261, 120, "DECAY", instrument->process.reverb_decay / 0.9f, PAL_NOTE);
        slider(fb, 250, 261, 120, "DAMP", instrument->process.reverb_damping, PAL_TUNING);
        slider(fb, 382, 261, 120, "MIX", instrument->process.reverb_mix, PAL_EFFECT);
    } else {
        char crossfade[32];
        char mode[32];
        float display_crossfade = showing_bank ? shown_slot->loop_crossfade_ms :
                                                instrument->loop_crossfade_ms;
        button(fb, 10, 261, 74, "SET LOOP", has_loop);
        button(fb, 89, 261, 64, "CLEAR", 0);
        button(fb, 158, 261, 84, "PLAY LOOP", ui->playback_active);
        snprintf(mode, sizeof(mode), "MODE %.9s", ts_loop_mode_name(display_loop_mode));
        button(fb, 247, 261, 108, mode, display_loop_mode != TS_LOOP_FORWARD);
        snprintf(crossfade, sizeof(crossfade), "XFADE %.1F MS", display_crossfade);
        slider(fb, 365, 261, 212, crossfade, display_crossfade / 50.0f,
               PAL_TUNING);
    }

    for (size_t i = 0; i < 8u; ++i)
        button(fb, wave_buttons[i].x, 289, wave_buttons[i].width,
               wave_buttons[i].label, 0);
    if (!ui->show_keyboard && !ui->show_recipes && !ui->show_ingredients)
        button(fb, wave_buttons[8].x, 289, wave_buttons[8].width,
               ui->bank_clear_armed ? "CONFIRM CLEAR" : "CLEAR ALL",
               ui->bank_clear_armed);
    button(fb, wave_buttons[9].x, 289, wave_buttons[9].width,
           ui->show_keyboard ? "BANK" : ui->show_recipes ? "DSP" :
           ui->show_ingredients ? "KEYS" : "CDP", !ui->show_keyboard);

    if (ui->show_keyboard) {
        char keyboard_hint[96];
        char base_note[8];
        if (ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER)
            snprintf(keyboard_hint, sizeof(keyboard_hint),
                     "KEY %s  SHIFT+CLICK STAGES  CLICK STAGED KEY LAUNCHES",
                     ts_midi_note_name(ts_ui_keyboard_base_note(ui),
                                       base_note, sizeof(base_note)));
        else
            snprintf(keyboard_hint, sizeof(keyboard_hint),
                     "KEY %s  SHIFT+WHEEL SEMITONE / F1-F8  SHIFT+CLICK CHORD",
                     ts_midi_note_name(ts_ui_keyboard_base_note(ui),
                                       base_note, sizeof(base_note)));
        text(fb, 11, 318, keyboard_hint, RGB(184, 180, 184), 1);
        const int white_x = 10, white_y = 330, white_w = 43, white_h = 49;
        const int white_semitones[14] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
        for (int i = 0; i < 14; ++i) {
            char label[8];
            int label_x;
            int active = (ui->active_notes & (1u << white_semitones[i])) != 0;
            int staged = (ui->staged_notes & (1u << white_semitones[i])) != 0;
            rect(fb, white_x + i * white_w, white_y, white_w - 1, white_h,
                 staged ? PAL_TUNING : active ? PAL_MOUSE : RGB(220, 216, 207));
            ts_midi_note_name(ts_ui_keyboard_base_note(ui) + white_semitones[i],
                              label, sizeof(label));
            label_x = white_x + i * white_w +
                      (white_w - 1 - (int)strlen(label) * 6) / 2;
            text(fb, label_x, white_y + 36, label, RGB(24, 24, 24), 1);
        }
        {
            const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
            const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
            for (int i = 0; i < 10; ++i) {
                char label[8];
                int left = white_x + (black_after[i] + 1) * white_w - 16;
                int label_x;
                int key = semitones[i];
                int active = (ui->active_notes & (1u << key)) != 0;
                int staged = (ui->staged_notes & (1u << key)) != 0;
                rect(fb, left, white_y, 31, 31,
                     staged ? PAL_TUNING : active ? PAL_VOLUME : RGB(18, 18, 18));
                ts_midi_note_name(ts_ui_keyboard_base_note(ui) + key,
                                  label, sizeof(label));
                label_x = left + (31 - (int)strlen(label) * 6) / 2;
                text(fb, label_x, white_y + 19, label,
                     active || staged ? PAL_BLOCK_TEXT : RGB(220, 216, 207), 1);
            }
        }
    } else if (ui->show_recipes) {
        mini_button(fb, 10, 312, 48, "CDP 1", ui->cdp_page == 0);
        mini_button(fb, 62, 312, 48, "CDP 2", ui->cdp_page == 1);
        text(fb, 120, 318,
             "LEFT APPLY  MIDDLE EDIT     1 TILES  2 KEYS  3 PAGE  4 DSP",
             RGB(184, 180, 184), 1);
        for (int i = 0; i < TS_RECIPE_SLOT_COUNT; ++i) {
            const TsCdpRecipe *recipe =
                ts_cdp_factory_recipe_for_slot((size_t)ui->cdp_page, (size_t)i);
            char label[24];
            int x = 10 + (i % 8) * 77;
            int y = 330 + (i / 8) * 25;
            if (recipe != NULL)
                snprintf(label, sizeof(label), "%02d %.5s", i + 1,
                         recipe->display_name);
            else
                snprintf(label, sizeof(label), "%02d EMPTY", i + 1);
            button(fb, x, y, 72, label,
                   ui->transform_recipe_index ==
                   ui->cdp_page * TS_CDP_BANK_SLOT_COUNT + i);
            if (recipe != NULL) rect(fb, x + 2, y + 2, 3, 19, PAL_EFFECT);
        }
    } else if (ui->show_ingredients) {
        mini_button(fb, 10, 312, 48, "DSP 1", ui->dsp_page == 0);
        mini_button(fb, 62, 312, 48, "DSP 2", ui->dsp_page == 1);
        text(fb, 120, 318,
             ui->dsp_page == 0 ?
             "PROCESS  LEFT APPLY  MIDDLE EDIT     4 TOGGLES PAGE" :
             "PRIMITIVES  LEFT APPLY  MIDDLE EDIT  4 TOGGLES PAGE",
             RGB(184, 180, 184), 1);
        for (int i = 0; i < TS_RECIPE_SLOT_COUNT; ++i) {
            const TsDspRecipe *slot =
                ts_dsp_factory_recipe_for_slot((size_t)ui->dsp_page, (size_t)i);
            char label[24];
            int x = 10 + (i % 8) * 77;
            int y = 330 + (i / 8) * 25;
            snprintf(label, sizeof(label), "%02d %.7s", i + 1,
                     slot != NULL ? slot->display_name : "EMPTY");
            button(fb, x, y, 72, label,
                   ui->transform_dsp_slot ==
                   ui->dsp_page * TS_DSP_BANK_SLOT_COUNT + i);
            if (slot != NULL)
                rect(fb, x + 2, y + 2, 3, 19,
                     ui->dsp_page == 0 ? PAL_INSTRUMENT : PAL_EFFECT);
        }
    } else {
        const char *capture_label = ui->external_record_bank ?
                                    (ui->capture_state == TS_CAPTURE_RECORDING ? "STOP REC" :
                                     ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?
                                     "REC ARMED" : "REC ARM") :
                                    (ui->capture_state == TS_CAPTURE_RECORDING ? "STOP" :
                                     ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?
                                     "ARMED" : "CAPTURE");
        char page_hint[112];
        const char *bank_hint =
            ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?
            "CLICK OCCUPIED SOURCE  TARGET STAYS ARMED" :
            ui->capture_state == TS_CAPTURE_RECORDING ?
            "PERFORMING  CLICK STOP TO KEEP EARLY" :
            ui->fx_page == TS_FX_EDIT ?
            "PASTE REPLACES RANGE  NO RANGE PASTES IN PLACE  FIT STRETCHES" :
            ui->fx_page == TS_FX_FAMILY && instrument->has_selection ?
            "CREATE FRESH STAMP  VARY ANSWERS SCULPTED SELECTION" :
            ui->fx_page == TS_FX_FAMILY ?
            "CREATE FRESH SOURCE  VARY ANSWERS CURRENT MATERIAL" :
            "CLICK PLAY  DOUBLE EMPTY  RESIZE";
        if (ui->external_record_bank)
            bank_hint = ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?
                        "REC BANK ARMED  MAKE SOUND  THRESHOLD STARTS TAPE" :
                        ui->capture_state == TS_CAPTURE_RECORDING ?
                        "REC BANK RECORDING INPUT  STOP KEEPS TAKE" :
                        instrument->family_trajectory ?
                        "REC BANK  CHAIN ON  TAKES ADVANCE AND REARM" :
                        "REC BANK  SELECT EMPTY TILE  REC ARM  SHIFT+1";
        else {
            snprintf(page_hint, sizeof(page_hint), "SAMPLE %d/%d  %.22s",
                     ui->sample_page + 1,
                     ui->sample_page_count > 0 ? ui->sample_page_count : 1,
                     bank_hint);
            bank_hint = page_hint;
        }
        text(fb, 11, 318, bank_hint,
             ui->external_record_bank ? PAL_VOLUME : RGB(184, 180, 184), 1);
        if (ui->external_record_bank) {
            mini_button(fb, 320, 313, 75,
                        ui->record_source == TS_RECORD_SOURCE_SYNTH ?
                        "SRC SYNTH" : "SRC EXT",
                        ui->record_source == TS_RECORD_SOURCE_SYNTH);
            mini_button(fb, 400, 313, 56, "KEEP", 0);
            if (ui->record_source == TS_RECORD_SOURCE_EXT)
                mini_button(fb, 461, 313, 70,
                            ui->monitor_enabled ? "MON ON" : "MONITOR",
                            ui->monitor_enabled);
        } else {
            int capture_channels = ui->capture_state != TS_CAPTURE_IDLE ?
                                   ui->capture_channels :
                                   ui->config.capture_channels;
            char source_count[24];
            snprintf(source_count, sizeof(source_count), "SRC %d  SHIFT+T",
                     ts_performance_source_count(ui->sister_source_mask));
            text(fb, 250, 318, source_count, PAL_NOTE, 1);
            mini_button(fb, 350, 313, 28,
                        capture_channels == 2 ? "S" : "M",
                        capture_channels == 2);
            mini_button(fb, 382, 313, 74, "OVERDUB", ui->capture_overdub);
            mini_button(fb, 461, 313, 70, "+ PAGE", 0);
        }
        mini_button(fb, 536, 313, 94, capture_label,
                    ui->capture_state != TS_CAPTURE_IDLE);
        for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
            const TsBankSlot *slot = &instrument->bank[i];
            char label[24];
            int x = 10 + (i % 8) * 77;
            int y = 330 + (i / 8) * 25;
            if (slot->occupied)
                snprintf(label, sizeof(label), "%02d %.7s", i + 1, slot->sample.name);
            else
                snprintf(label, sizeof(label), "%02d ---", i + 1);
            button(fb, x, y, 72, label, slot->occupied);
            if (slot->occupied && slot->has_loop)
                rect(fb, x + 66, y + 4, 3, 15, PAL_TUNING);
            if (i == ui->bank_view_slot) rect(fb, x + 4, y + 4, 64, 2, PAL_EFFECT);
            if (i == ui->capture_source_slot) {
                rect(fb, x + 4, y + 4, 3, 15, PAL_NOTE);
                text(fb, x + 10, y + 8, "SRC", PAL_NOTE, 1);
            }
            if (i == ui->capture_destination_slot) {
                uint32_t record_color = ui->text_cursor_visible ? PAL_VOLUME : PAL_EFFECT;
                rect(fb, x - 2, y - 2, 76, 3, record_color);
                rect(fb, x - 2, y + 22, 76, 3, record_color);
                rect(fb, x - 2, y - 2, 3, 27, record_color);
                rect(fb, x + 71, y - 2, 3, 27, record_color);
            }
            ts_ui_draw_tile_state_borders(
                fb, i, i == instrument->selected_slot,
                (ui->sister_source_mask & (uint16_t)(1u << i)) != 0u,
                active_palette());
        }
    }
    rect(fb, 0, 386, TS_UI_WIDTH, 14, RGB(10, 10, 10));
    text(fb, 8, 389, ui->status, PAL_MOUSE, 1);

    if (ui->exit_confirm_open) {
        frame(fb, 154, 128, 332, 130, RGB(36, 33, 37), PAL_MOUSE);
        text(fb, 172, 143, "EXIT TAPESISTER?", PAL_NOTE, 1);
        text(fb, 172, 164,
             ui->exit_has_unsaved ? "UNSAVED CHANGES WILL BE LOST" : "CLOSE TAPESISTER",
             ui->exit_has_unsaved ? PAL_VOLUME : RGB(190, 185, 190), 1);
        button(fb, 172, 188, 136, "EXIT", 0);
        button(fb, 324, 188, 144, "CANCEL", 1);
        text(fb, 172, 230, "ENTER/Y EXIT   ESC/N CANCEL", RGB(190, 185, 190), 1);
    } else if (ui->overdub_confirm_open) {
        char target[80];
        frame(fb, 146, 122, 348, 132, RGB(36, 33, 37), PAL_MOUSE);
        text(fb, 166, 137, "OVERDUB TARGET?", PAL_NOTE, 1);
        snprintf(target, sizeof(target),
                 "MIX A NEW PERFORMANCE INTO TILE %02d",
                 ui->overdub_confirm_slot + 1);
        text(fb, 166, 158, target, PAL_EFFECT, 1);
        text(fb, 166, 176, "THIS IS ONE UNDO/REDO OPERATION", PAL_VOLUME, 1);
        button(fb, 166, 200, 136, "OVERDUB", 1);
        button(fb, 318, 200, 156, "CANCEL", 0);
        text(fb, 166, 238, "ENTER/Y CONFIRM   ESC/N CANCEL", RGB(190, 185, 190), 1);
    } else if (ui->fm_open)
        fm_render(fb, ui, instrument);
    else if (ui->transform_open)
        transform_render(fb, ui, instrument);
    else if (ui->drone_open)
        drone_render(fb, ui);
    else if (ui->exchange_dialog != TS_UI_EXCHANGE_NONE) {
        char count[80];
        frame(fb, 110, 54, 420, 140, RGB(36, 33, 37), PAL_MOUSE);
        if (ui->exchange_dialog == TS_UI_EXCHANGE_SEND) {
            text(fb, 128, 68, "SEND TAPESISTER BANK TO TAPEHEAD", PAL_NOTE, 1);
            snprintf(count, sizeof(count), "%d TILES  %d SAMPLE PAGE%s",
                     ui->exchange_item_count,
                     ui->sample_page_count > 0 ? ui->sample_page_count : 1,
                     ui->sample_page_count == 1 ? "" : "S");
            text(fb, 128, 88, count, PAL_EFFECT, 1);
            button(fb, 126, 112, 120, "PAGE -> ONE", 0);
            button(fb, 254, 112, 120, "PAGE -> SPLIT", 0);
            button(fb, 382, 112, 132, "ALL PAGES",
                   ui->sample_page_count > 1 && !ui->external_record_bank);
            text(fb, 128, 140,
                 "ALL PAGES MAPS EACH PAGE TO ONE TAPEHEAD INSTRUMENT",
                 PAL_INSTRUMENT, 1);
            button(fb, 128, 158, 112, "CHECK INBOX", 0);
            button(fb, 250, 158, 140, "NEW INSTANCE",
                   ui->exchange_force_new_instance);
            button(fb, 400, 158, 112, "CANCEL", 0);
        } else {
            text(fb, 128, 68, "TAPEHEAD TRANSFER FOUND", PAL_NOTE, 1);
            snprintf(count, sizeof(count), "%d SAMPLES -> TILES 01-16",
                     ui->exchange_item_count);
            text(fb, 128, 88, count, PAL_EFFECT, 1);
            text(fb, 128, 106,
                 ui->exchange_layout == TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS ?
                 "SOURCE: SEPARATE FT2 INSTRUMENTS" :
                 "SOURCE: SAMPLES FROM ONE FT2 INSTRUMENT",
                 PAL_INSTRUMENT, 1);
            text(fb, 128, 124,
                 "IMPORT REPLACES THE CURRENT TAPESISTER BANK", PAL_VOLUME, 1);
            text(fb, 128, 140, ui->exchange_name, PAL_EFFECT, 1);
            button(fb, 184, 158, 132, "IMPORT", 0);
            button(fb, 330, 158, 126, "LATER", 0);
        }
    }
    else if (ui->load_selection_choice_open) {
        char source[58];
        snprintf(source, sizeof(source), "%.52s", ui->load_selection_name);
        frame(fb, 126, 78, 388, 108, RGB(36, 33, 37), PAL_MOUSE);
        text(fb, 146, 91, "LOAD WAV INTO SELECTION?", PAL_NOTE, 1);
        text(fb, 146, 108, source, PAL_EFFECT, 1);
        button(fb, 146, 132, 96, "PASTE", 0);
        button(fb, 272, 132, 96, "FIT", 1);
        button(fb, 398, 132, 96, "CANCEL", 0);
        text(fb, 146, 164, "P/ENTER EXACT   F FITS RANGE   ESC CANCEL", RGB(190, 185, 190), 1);
    } else if (ui->palette_open)
        palette_render(fb, ui);
    else if (ui->config_open)
        config_render(fb, ui);
    else if (ui->browser.mode != TS_BROWSER_CLOSED)
        browser_render(fb, &ui->browser, ui->text_cursor_visible, ui->file_busy);
    else if (ui->renaming_bank_slot >= 0) {
        size_t length = strlen(ui->bank_rename);
        size_t cursor = ui->bank_rename_cursor > length ? length : ui->bank_rename_cursor;
        size_t first = length > 62 ? length - 62 : 0;
        char shown[63];
        char title[40];
        if (cursor < first) first = cursor;
        if (cursor > first + 62) first = cursor - 62;
        snprintf(shown, sizeof(shown), "%.62s", ui->bank_rename + first);
        frame(fb, 104, 306, 432, 76, RGB(36, 33, 37), PAL_MOUSE);
        snprintf(title, sizeof(title), "RENAME BANK %02d", ui->renaming_bank_slot + 1);
        text(fb, 116, 316, title, PAL_NOTE, 1);
        rect(fb, 116, 332, 408, 23, RGB(8, 8, 8));
        text(fb, 122, 340, shown, PAL_MOUSE, 1);
        if (ui->text_cursor_visible)
            rect(fb, 122 + (int)(cursor - first) * 6, 338, 2, 11, PAL_MOUSE);
        text(fb, 116, 365, "ENTER ACCEPTS   ESC CANCELS", RGB(190, 185, 190), 1);
    } else if (ui->renaming_recipe_slot >= 0) {
        size_t length = strlen(ui->recipe_rename);
        size_t cursor = ui->recipe_rename_cursor > length ? length :
                        ui->recipe_rename_cursor;
        char title[40];
        frame(fb, 160, 306, 320, 76, RGB(36, 33, 37), PAL_MOUSE);
        snprintf(title, sizeof(title), "RENAME RECIPE %02d", ui->renaming_recipe_slot + 1);
        text(fb, 172, 316, title, PAL_NOTE, 1);
        rect(fb, 172, 332, 296, 23, RGB(8, 8, 8));
        text(fb, 178, 340, ui->recipe_rename, PAL_MOUSE, 1);
        if (ui->text_cursor_visible)
            rect(fb, 178 + (int)cursor * 6, 338, 2, 11, PAL_MOUSE);
        text(fb, 172, 365, "ENTER ACCEPTS   ESC CANCELS", RGB(190, 185, 190), 1);
    } else if (ui->export_choice_open) {
        frame(fb, 154, 135, 332, 112, RGB(36, 33, 37), PAL_MOUSE);
        text(fb, 172, 150, "EXPORT WHAT?", PAL_NOTE, 1);
        button(fb, 172, 176, 136, "SELECTED WAV", 0);
        button(fb, 324, 176, 144, "COLLECTION", 0);
        text(fb, 172, 218, "C SELECTED  F FULL COLLECTION   ESC CANCEL", RGB(190, 185, 190), 1);
    }

    if (ui->capture_state == TS_CAPTURE_RECORDING) {
        size_t capacity = ui->capture_capacity_frames;
        size_t recorded = ui->capture_recorded_frames;
        int progress = capacity > 0u ?
                       (int)((double)recorded * TS_UI_WIDTH / (double)capacity) : 0;
        if (progress < 0) progress = 0;
        if (progress > TS_UI_WIDTH) progress = TS_UI_WIDTH;
        rect(fb, 0, 0, TS_UI_WIDTH, 3, PAL_VOLUME);
        rect(fb, 0, TS_UI_HEIGHT - 3, TS_UI_WIDTH, 3, PAL_VOLUME);
        rect(fb, 0, 0, 3, TS_UI_HEIGHT, PAL_VOLUME);
        rect(fb, TS_UI_WIDTH - 3, 0, 3, TS_UI_HEIGHT, PAL_VOLUME);
        rect(fb, 0, 32, TS_UI_WIDTH, 4, RGB(30, 8, 8));
        rect(fb, 0, 32, progress, 4, PAL_VOLUME);
    }
    if (ui->overlay[0] != '\0') {
        int scale = strlen(ui->overlay) <= 46u ? 2 : 1;
        int width = (int)strlen(ui->overlay) * 6 * scale;
        int x = (TS_UI_WIDTH - width) / 2;
        frame(fb, 24, 126, 592, 76, RGB(12, 12, 12), PAL_VOLUME);
        if (x < 34) x = 34;
        text(fb, x, 153, ui->overlay, PAL_VOLUME, scale);
    }
    if (ui->file_busy) {
        char busy[64];
        int dots = ui->file_busy_phase % 4 + 1;
        snprintf(busy, sizeof(busy), "%s%.*s",
                 ui->file_busy_label[0] != '\0' ? ui->file_busy_label : "WORKING",
                 dots, "....");
        frame(fb, 154, 142, 332, 94, RGB(12, 12, 12), PAL_VOLUME);
        text(fb, 174, 158, busy, PAL_VOLUME, 2);
        text(fb, 174, 207, "PLEASE WAIT - FILE OPERATION IN PROGRESS",
             RGB(190, 185, 190), 1);
    }
}

int ts_ui_write_ppm(const TsFramebuffer *fb, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", TS_UI_WIDTH, TS_UI_HEIGHT);
    for (int i = 0; i < TS_UI_WIDTH * TS_UI_HEIGHT; ++i) {
        unsigned char rgb[3] = {
            (unsigned char)(fb->pixels[i] >> 16),
            (unsigned char)(fb->pixels[i] >> 8),
            (unsigned char)fb->pixels[i]
        };
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

int ts_ui_key_from_point(int x, int y)
{
    if (x < 10 || x >= 622 || y < 330 || y >= 379) return -1;
    const int white_w = 43;
    const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    if (y < 361) {
        for (int i = 0; i < 10; ++i) {
            int left = 10 + (black_after[i] + 1) * white_w - 16;
            if (x >= left && x < left + 31) return semitones[i];
        }
    }
    static const int white_semitones[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
    int index = (x - 10) / white_w;
    return index >= 0 && index < 14 ? white_semitones[index] : -1;
}

size_t ts_ui_right_drag_playhead_frame(size_t anchor, size_t pointer,
                                       size_t selection_first,
                                       size_t selection_last,
                                       size_t sample_frames)
{
    size_t frame = pointer >= anchor ? selection_first : selection_last;
    if (sample_frames == 0) return 0;
    if (frame >= sample_frames) frame = sample_frames - 1u;
    return frame;
}

int ts_ui_bank_slot_from_point(int x, int y)
{
    int column;
    int row;
    if (x < 10 || x >= 626 || y < 330 || y >= 379) return -1;
    column = (x - 10) / 77;
    row = (y - 330) / 25;
    if (column < 0 || column >= 8 || row < 0 || row >= 2) return -1;
    if ((x - 10) % 77 >= 72 || (y - 330) % 25 >= 24) return -1;
    return row * 8 + column;
}

int ts_ui_recipe_slot_from_point(int x, int y)
{
    return ts_ui_bank_slot_from_point(x, y);
}

int ts_ui_cdp_slot_from_point(int x, int y)
{
    return ts_ui_bank_slot_from_point(x, y);
}

int ts_ui_cdp_page_from_point(int x, int y)
{
    if (y < 312 || y >= 326) return -1;
    if (x >= 10 && x < 58) return 0;
    if (x >= 62 && x < 110) return 1;
    return -1;
}

int ts_ui_dsp_page_from_point(int x, int y)
{
    return ts_ui_cdp_page_from_point(x, y);
}

TsUiBankAction ts_ui_bank_action(int right_button, unsigned modifiers)
{
    unsigned relevant = modifiers & (TS_UI_BANK_MOD_SHIFT |
                                     TS_UI_BANK_MOD_CTRL |
                                     TS_UI_BANK_MOD_ALT);
    if (right_button)
        return relevant == TS_UI_BANK_MOD_SHIFT ? TS_UI_BANK_ACTION_CLEAR :
               relevant == 0 ? TS_UI_BANK_ACTION_RENAME : TS_UI_BANK_ACTION_INVALID;
    if (relevant == 0) return TS_UI_BANK_ACTION_AUDITION;
    if (relevant == TS_UI_BANK_MOD_SHIFT) return TS_UI_BANK_ACTION_CAPTURE_CURRENT;
    if (relevant == TS_UI_BANK_MOD_ALT) return TS_UI_BANK_ACTION_CAPTURE_LOOP;
    if (relevant == TS_UI_BANK_MOD_CTRL) return TS_UI_BANK_ACTION_CAPTURE_SELECTION;
    if (relevant == (TS_UI_BANK_MOD_CTRL | TS_UI_BANK_MOD_SHIFT))
        return TS_UI_BANK_ACTION_CLONE;
    if (relevant == (TS_UI_BANK_MOD_CTRL | TS_UI_BANK_MOD_ALT))
        return TS_UI_BANK_ACTION_TOGGLE_LOCK;
    return TS_UI_BANK_ACTION_INVALID;
}

int ts_ui_execute_bank_action(TsInstrument *instrument, int slot,
                              TsUiBankAction action,
                              char *error, size_t error_size)
{
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        if (error != NULL && error_size > 0)
            snprintf(error, error_size, "Invalid bank slot");
        return 0;
    }
    if (action == TS_UI_BANK_ACTION_CAPTURE_CURRENT)
        return ts_instrument_bank_capture(instrument, slot,
                                          TS_BANK_CAPTURE_CURRENT,
                                          error, error_size);
    if (action == TS_UI_BANK_ACTION_CAPTURE_LOOP)
        return ts_instrument_bank_capture(instrument, slot,
                                          TS_BANK_CAPTURE_LOOP,
                                          error, error_size);
    if (action == TS_UI_BANK_ACTION_CAPTURE_SELECTION)
        return ts_instrument_bank_capture(instrument, slot,
                                          TS_BANK_CAPTURE_SELECTION,
                                          error, error_size);
    if (action == TS_UI_BANK_ACTION_CLONE)
        return ts_instrument_copy_selected(instrument, slot, error, error_size);
    if (action == TS_UI_BANK_ACTION_CLEAR)
        return ts_instrument_bank_clear(instrument, slot, error, error_size);
    if (action == TS_UI_BANK_ACTION_TOGGLE_LOCK)
        return ts_instrument_bank_toggle_locked(instrument, slot, error, error_size);
    if (action == TS_UI_BANK_ACTION_AUDITION)
        return ts_instrument_select_bank(instrument, slot, error, error_size);
    if (action == TS_UI_BANK_ACTION_RENAME) {
        if (!instrument->bank[slot].occupied) {
            if (error != NULL && error_size > 0)
                snprintf(error, error_size, "Bank slot is empty");
            return 0;
        }
        if (error != NULL && error_size > 0) error[0] = '\0';
        return 1;
    }
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "Unsupported bank command");
    return 0;
}

int ts_ui_tape_action(int right_button, unsigned modifiers, TsPostEditKind *kind)
{
    unsigned relevant = modifiers & (TS_UI_BANK_MOD_SHIFT |
                                     TS_UI_BANK_MOD_CTRL |
                                     TS_UI_BANK_MOD_ALT);
    if (kind == NULL ||
        (relevant != TS_UI_BANK_MOD_SHIFT && relevant != TS_UI_BANK_MOD_CTRL))
        return 0;
    if (relevant == TS_UI_BANK_MOD_SHIFT)
        *kind = right_button ? TS_POST_COPY_OVERWRITE : TS_POST_COPY_MIX;
    else
        *kind = right_button ? TS_POST_MOVE_OVERWRITE : TS_POST_MOVE_MIX;
    return 1;
}

static float sister_clamp(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (value < -1.0f) return -1.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static void sister_wave_lane(TsFramebuffer *fb,
                             const TsSisterWaveSnapshot *wave,
                             int x, int y, int width, int height,
                             int channel, uint32_t color)
{
    int middle = y + height / 2;
    rect(fb, x, middle, width, 1, RGB(55, 52, 57));
    for (int px = 0; px < width; ++px) {
        size_t bin = (size_t)px * TS_SISTER_WAVE_BIN_COUNT / (size_t)width;
        const TsSisterWaveBin *source = &wave->bins[bin];
        float low = channel == 0 ? source->left_minimum :
                    channel == 1 ? source->right_minimum :
                    0.5f * (source->left_minimum + source->right_minimum);
        float high = channel == 0 ? source->left_maximum :
                     channel == 1 ? source->right_maximum :
                     0.5f * (source->left_maximum + source->right_maximum);
        int y0 = middle - (int)lrintf(sister_clamp(high) * (height / 2 - 3));
        int y1 = middle - (int)lrintf(sister_clamp(low) * (height / 2 - 3));
        if (y0 > y1) { int swap = y0; y0 = y1; y1 = swap; }
        /* Sister has its own waveform viewport.  wave_line() deliberately
           clips to the ordinary TapeSister canvas, whose top edge is y=64
           and right edge is x=620; using it here discarded the upper part
           and final columns of this 640x400 window. */
        rect(fb, x + px, y0, 1, y1 - y0 + 1, color);
    }
}

static void sister_marker(TsFramebuffer *fb, float normalized,
                          int x, int width, int y, int height,
                          uint32_t color, int shape)
{
    int marker_x;
    if (!isfinite(normalized)) return;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    marker_x = x + (int)lrintf(normalized * (float)(width - 1));
    rect(fb, marker_x, y, 2, height, color);
    if (shape == 0) rect(fb, marker_x - 2, y, 6, 4, color);
    else if (shape == 1) rect(fb, marker_x - 3, y + height - 4, 8, 4, color);
    else {
        rect(fb, marker_x - 3, y, 8, 2, color);
        rect(fb, marker_x - 1, y + 2, 4, 2, color);
    }
}

static void sister_parameter(TsFramebuffer *fb, int x, int y, int width,
                             const char *label, float amount, uint32_t color)
{
    char value[32];
    if (!isfinite(amount)) amount = 0.0f;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    rect(fb, x, y, width, 18, RGB(24, 23, 25));
    rect(fb, x + 1, y + 14, (int)lrintf((width - 2) * amount), 3, color);
    snprintf(value, sizeof(value), "%s %02d", label, (int)lrintf(amount * 99.0f));
    text(fb, x + 3, y + 3, value, color, 1);
}

static void sister_percent_parameter(TsFramebuffer *fb, int x, int y,
                                     int width, const char *label,
                                     float amount, int maximum_percent,
                                     uint32_t color)
{
    char value[32];
    if (!isfinite(amount)) amount = 0.0f;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    rect(fb, x, y, width, 18, RGB(24, 23, 25));
    rect(fb, x + 1, y + 14, (int)lrintf((width - 2) * amount), 3, color);
    snprintf(value, sizeof(value), "%s %03d", label,
             (int)lrintf(amount * (float)maximum_percent));
    text(fb, x + 3, y + 3, value, color, 1);
}

static void sister_vertical_mixer(TsFramebuffer *fb, int x, int y,
                                  const TsSisterUiModel *model)
{
    static const char *const labels[5] = {"T", "F", "E", "A", "X"};
    const float amounts[5] = {
        model->parameters.tiles_gain / 4.0f,
        model->parameters.fm_gain / 4.0f,
        model->parameters.external_gain / 4.0f,
        model->parameters.preview_gain / 4.0f,
        model->parameters.fx_return_gain / 2.0f
    };
    const uint32_t colors[5] = {
        PAL_NOTE, PAL_EFFECT, PAL_WAVE_RIGHT, PAL_MOUSE, PAL_INSTRUMENT
    };
    const int track_y = y + 22;
    const int track_height = 84;
    rect(fb, x, y, 80, 110, RGB(24, 23, 25));
    text(fb, x + 25, y + 3, "MIXER", PAL_MOUSE, 1);
    for (int control = 0; control < 5; ++control) {
        float amount = amounts[control];
        int lane_x = x + 2 + control * 15;
        int handle_y;
        int unity_y;
        if (!isfinite(amount)) amount = 0.0f;
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        text(fb, lane_x + 4, y + 12, labels[control], colors[control], 1);
        rect(fb, lane_x + 5, track_y, 4, track_height, RGB(7, 7, 8));
        unity_y = track_y + (int)lrintf(
            (track_height - 1) * (control == 4 ? 0.5f : 0.75f));
        rect(fb, lane_x + 2, unity_y, 10, 1, PAL_BUTTON);
        handle_y = track_y + (int)lrintf(
            (track_height - 1) * (1.0f - amount));
        rect(fb, lane_x + 1, handle_y - 1, 12, 3, colors[control]);
    }
}

static void sister_choice_parameter(TsFramebuffer *fb, int x, int y,
                                    int width, const char *label,
                                    const char *choice, int active,
                                    uint32_t color)
{
    char value[32];
    rect(fb, x, y, width, 18, RGB(24, 23, 25));
    if (active) rect(fb, x + 1, y + 14, width - 2, 3, color);
    snprintf(value, sizeof(value), "%s %s", label, choice);
    text(fb, x + 3, y + 3, value, color, 1);
}

static void sister_target_toggle(TsFramebuffer *fb, int x, int y, int width,
                                 const char *label, int active,
                                 uint32_t color)
{
    rect(fb, x, y, width, 18, RGB(24, 23, 25));
    if (active) {
        rect(fb, x + 1, y + 1, width - 2, 2, color);
        rect(fb, x + 1, y + 14, width - 2, 3, color);
    }
    text(fb, x + 8, y + 5, label, active ? color : PAL_MOUSE, 1);
}

void ts_sister_ui_render(TsFramebuffer *fb, const TsSisterUiModel *model,
                         const TsPalette *palette)
{
    static const char *const tap_names[TS_SISTER_TAP_COUNT] = {
        "MIX", "H1", "H2", "H3"
    };
    TsWaveformDisplayMode mode;
    char line[160];
    char preset_label[20];
    if (fb == NULL || model == NULL || palette == NULL) return;
    render_palette = palette;
    clear(fb, PAL_DESKTOP);
    rect(fb, 0, 0, TS_SISTER_UI_WIDTH, 36, RGB(12, 12, 12));
    button(fb, 10, 8, 62, model->routing.enabled ? "POWER ON" : "POWER", model->routing.enabled);
    button(fb, 78, 8, 62, "ROLL", model->routing.rolling);
    button(fb, 146, 8, 62, "HOLD", model->routing.held);
    button(fb, 214, 8, 62, "CLEAR", model->engine.clear_state != TS_SISTER_CLEAR_IDLE);
    button(fb, 282, 8, 62, "MONITOR", model->routing.monitor_enabled);
    {
        float target = model->parameters.buffer_seconds;
        float current = (float)model->engine.duration_seconds;
        float amount = (target - (float)TS_SISTER_MIN_SECONDS) /
            (float)(TS_SISTER_MAX_SECONDS - TS_SISTER_MIN_SECONDS);
        char canvas[32];
        if (model->engine.resize_pending)
            snprintf(canvas, sizeof(canvas), "BUF %.0F>%.0F", current, target);
        else
            snprintf(canvas, sizeof(canvas), "BUFFER %.0FS", target);
        sister_choice_parameter(fb, 350, 8, 94, "", canvas,
                                model->engine.resize_pending, PAL_TUNING);
        rect(fb, 351, 25, (int)lrintf(92.0f * sister_clamp(amount)), 3,
             PAL_TUNING);
    }
    button(fb, 450, 8, 76, model->fx_page ? "TAPE" : "FX PAGE",
           model->fx_page);
    button(fb, 532, 8, 98, ts_waveform_display_name(model->waveform_mode),
           model->waveform_mode != TS_WAVEFORM_DISPLAY_STEREO);

    if (model->fx_page) {
        static const char *const names[3] = {"REVERB", "DELAY", "DISTORTION"};
        const uint32_t colors[3] = {PAL_WAVE_RIGHT, PAL_EFFECT, PAL_VOLUME};
        const uint8_t masks[3] = {
            model->parameters.fx.reverb_targets,
            model->parameters.fx.delay_targets,
            model->parameters.fx.distortion_targets
        };
        for (int row = 0; row < 3; ++row) {
            int top = 48 + row * 78;
            rect(fb, 10, top, 620, 68, RGB(10, 10, 11));
            text(fb, 18, top + 28, names[row], colors[row], 1);
        }
        sister_choice_parameter(fb, 110, 72, 130, "TYPE",
            ts_sister_reverb_type_name(model->parameters.fx.reverb_type),
            model->parameters.fx.reverb_mix > 0.0f, PAL_WAVE_RIGHT);
        sister_percent_parameter(fb, 250, 72, 130, "MIX",
            model->parameters.fx.reverb_mix, 100, PAL_WAVE_RIGHT);
        sister_percent_parameter(fb, 390, 72, 130, "DECAY",
            model->parameters.fx.reverb_decay, 100, PAL_WAVE_RIGHT);
        sister_percent_parameter(fb, 110, 150, 130, "TIME",
            model->parameters.fx.delay_time, 100, PAL_EFFECT);
        sister_percent_parameter(fb, 250, 150, 130, "FEEDBACK",
            model->parameters.fx.delay_feedback, 100, PAL_EFFECT);
        sister_percent_parameter(fb, 390, 150, 130, "MIX",
            model->parameters.fx.delay_mix, 100, PAL_EFFECT);
        sister_percent_parameter(fb, 110, 228, 130, "DRIVE",
            model->parameters.fx.distortion_drive, 100, PAL_VOLUME);
        sister_percent_parameter(fb, 250, 228, 130, "TONE",
            model->parameters.fx.distortion_tone, 100, PAL_VOLUME);
        sister_percent_parameter(fb, 390, 228, 130, "MIX",
            model->parameters.fx.distortion_mix, 100, PAL_VOLUME);
        for (int row = 0; row < 3; ++row) {
            int y = 97 + row * 78;
            sister_target_toggle(fb, 110, y, 56, "H1",
                masks[row] & TS_SISTER_EFFECT_TARGET_H1, PAL_NOTE);
            sister_target_toggle(fb, 172, y, 56, "H2",
                masks[row] & TS_SISTER_EFFECT_TARGET_H2, PAL_EFFECT);
            sister_target_toggle(fb, 234, y, 56, "H3",
                masks[row] & TS_SISTER_EFFECT_TARGET_H3, PAL_TUNING);
            sister_target_toggle(fb, 296, y, 56, "MIX",
                masks[row] & TS_SISTER_EFFECT_TARGET_MIX, colors[row]);
            text(fb, 366, y + 5,
                row == 0 ? "TAIL-SAFE TYPE MORPH" :
                row == 1 ? "8-2000 MS / STEREO" :
                           "RAT-INSPIRED / 2X", PAL_MOUSE, 1);
        }
        text(fb, 10, 311, "MASTER", PAL_TUNING, 1);
        sister_percent_parameter(fb, 110, 306, 410, "FX FEEDBACK",
            model->parameters.fx.master_feedback, 100, PAL_TUNING);
        text(fb, 530, 311, "0-135%", PAL_MOUSE, 1);
        text(fb, 10, 340,
             "FIXED ORDER  DISTORTION > DELAY > REVERB   MIX FX WORK WITH POWER OFF",
             PAL_MOUSE, 1);
        goto sister_footer;
    }

    rect(fb, 10, 40, 620, 126, RGB(7, 7, 8));
    mode = ts_waveform_display_sanitize(model->waveform_mode);
    if (mode == TS_WAVEFORM_DISPLAY_STEREO && model->waveform.channels == 2u) {
        text(fb, 14, 45, "L", PAL_WAVE_LEFT, 1);
        text(fb, 14, 108, "R", PAL_WAVE_RIGHT, 1);
        sister_wave_lane(fb, &model->waveform, 26, 40, 600, 61, 0, PAL_WAVE_LEFT);
        sister_wave_lane(fb, &model->waveform, 26, 103, 600, 63, 1, PAL_WAVE_RIGHT);
    } else {
        int channel = mode == TS_WAVEFORM_DISPLAY_RIGHT ? 1 :
                      mode == TS_WAVEFORM_DISPLAY_MONO_SUM ? 2 : 0;
        uint32_t color = mode == TS_WAVEFORM_DISPLAY_RIGHT ? PAL_WAVE_RIGHT :
                         mode == TS_WAVEFORM_DISPLAY_MONO_SUM ? PAL_WAVE_SUM :
                         PAL_WAVE_LEFT;
        sister_wave_lane(fb, &model->waveform, 14, 40, 612, 126, channel, color);
    }
    sister_marker(fb, model->engine.write_normalized, 26, 600, 40, 126, PAL_VOLUME, 0);
    sister_marker(fb, model->engine.head_normalized[0], 26, 600, 40, 126, PAL_NOTE, 1);
    sister_marker(fb, model->engine.head_normalized[1], 26, 600, 40, 126, PAL_EFFECT, 2);
    sister_marker(fb, model->engine.head_normalized[2], 26, 600, 40, 126, PAL_TUNING, 0);

    button(fb, 10, 172, 70, "TILES", model->routing.source_switches & TS_SISTER_SOURCE_TILES);
    button(fb, 86, 172, 70, "FM", model->routing.source_switches & TS_SISTER_SOURCE_FM);
    button(fb, 162, 172, 70, "EXT", model->routing.source_switches & TS_SISTER_SOURCE_EXT);
    button(fb, 238, 172, 70, "AUDITION", model->routing.source_switches & TS_SISTER_SOURCE_PREVIEW);
    snprintf(line, sizeof(line), "MASK %04X  V %02d  IN %.2F  MIX %.2F",
             model->routing.source_mask, model->routing.active_source_voices,
             model->routing.source_input_peak,
             model->routing.tap_peak[TS_SISTER_TAP_MIX]);
    text(fb, 322, 179, line,
         model->routing.warnings ? PAL_VOLUME : PAL_MOUSE, 1);
    snprintf(line, sizeof(line), "H1 %.2F  H2 %.2F  H3 %.2F  OV %llu",
             model->routing.tap_peak[TS_SISTER_TAP_H1],
             model->routing.tap_peak[TS_SISTER_TAP_H2],
             model->routing.tap_peak[TS_SISTER_TAP_H3],
             (unsigned long long)model->routing.overload_count);
    text(fb, 322, 190, line,
         model->routing.overload_count != 0u ? PAL_VOLUME : PAL_TUNING, 1);
    sister_vertical_mixer(fb, 548, 172, model);

    text(fb, 10, 207, "H1", PAL_NOTE, 1);
    sister_parameter(fb, 72, 202, 110, "LEVEL", model->parameters.head1_level, PAL_NOTE);
    sister_parameter(fb, 192, 202, 110, "TIME", model->parameters.head1_time_ms / 4000.0f, PAL_NOTE);
    sister_parameter(fb, 312, 202, 110, "FEED", model->parameters.head1_feedback, PAL_NOTE);
    sister_parameter(fb, 432, 202, 110, "CUTOFF",
                     log10f(model->parameters.filter_cutoff_hz / 20.0f) / 3.0f,
                     PAL_INSTRUMENT);
    text(fb, 10, 235, "H2", PAL_EFFECT, 1);
    sister_parameter(fb, 72, 230, 110, "LEVEL", model->parameters.head2_level, PAL_EFFECT);
    sister_parameter(fb, 192, 230, 110, "SCRUB", model->parameters.head2_scrub, PAL_EFFECT);
    sister_parameter(fb, 312, 230, 110, "RATE", model->parameters.head2_rate_index / 9.0f, PAL_EFFECT);
    sister_parameter(fb, 432, 230, 110, "FEED", model->parameters.head2_feedback, PAL_EFFECT);
    text(fb, 10, 263, "H3", PAL_TUNING, 1);
    sister_parameter(fb, 72, 258, 110, "LEVEL", model->parameters.head3_level, PAL_TUNING);
    sister_parameter(fb, 192, 258, 110, "SPAN", model->parameters.head3_span, PAL_TUNING);
    sister_parameter(fb, 312, 258, 110, "RATE", model->parameters.head3_rate_index / 9.0f, PAL_TUNING);
    sister_parameter(fb, 432, 258, 110, "FILTER Q",
                     (model->parameters.filter_q - 0.1f) / 19.9f,
                     PAL_INSTRUMENT);

    sister_parameter(fb, 10, 286, 82, "WOW", model->parameters.wow / 10.0f, PAL_TUNING);
    sister_parameter(fb, 98, 286, 82, "DROP", model->parameters.drop / 100.0f, PAL_EFFECT);
    sister_parameter(fb, 186, 286, 82, "DUCK", model->parameters.duck_enabled ? model->parameters.duck_sensitivity : 0.0f, PAL_VOLUME);
    sister_choice_parameter(fb, 274, 286, 82, "DECOR",
                            model->parameters.decorrelation_enabled ? "ON" : "OFF",
                            model->parameters.decorrelation_enabled,
                            PAL_WAVE_RIGHT);
    sister_parameter(fb, 362, 286, 82, "WIDTH", model->parameters.width, PAL_WAVE_LEFT);
    sister_choice_parameter(fb, 450, 286, 82, "FILTER",
                            ts_sister_filter_type_name(model->parameters.filter_type),
                            model->parameters.filter_type != TS_SISTER_FILTER_BYPASS,
                            PAL_INSTRUMENT);
    sister_parameter(fb, 538, 286, 82, "GAIN",
                     (model->parameters.filter_gain_db + 24.0f) / 48.0f,
                     PAL_INSTRUMENT);

    sister_percent_parameter(fb, 10, 308, 98, "INPUT",
                             model->parameters.input_gain / 2.0f,
                             200, PAL_VOLUME);
    sister_percent_parameter(fb, 113, 308, 98, "DRY",
                             model->parameters.monitor_dry, 100, PAL_WAVE_LEFT);
    sister_percent_parameter(fb, 216, 308, 98, "WET",
                             model->parameters.monitor_wet, 100, PAL_WAVE_RIGHT);
    sister_percent_parameter(fb, 319, 308, 98, "OUT",
                             model->parameters.mix_output_gain / 4.0f,
                             400, PAL_INSTRUMENT);
    sister_percent_parameter(fb, 422, 308, 98, "ERASE",
                             model->parameters.write_erase, 100, PAL_VOLUME);
    sister_percent_parameter(fb, 525, 308, 99, "GHOST",
                             model->parameters.ghost_tone, 100, PAL_EFFECT);

    sister_percent_parameter(fb, 10, 330, 124, "SOAK",
                             model->parameters.soak, 100, PAL_WAVE_RIGHT);
    sister_percent_parameter(fb, 140, 330, 124, "BLEED",
                             model->parameters.bleed, 100, PAL_EFFECT);
    sister_target_toggle(fb, 276, 330, 52, "H1",
        model->parameters.soak_targets & TS_SISTER_EFFECT_TARGET_H1, PAL_NOTE);
    sister_target_toggle(fb, 334, 330, 52, "H2",
        model->parameters.soak_targets & TS_SISTER_EFFECT_TARGET_H2, PAL_EFFECT);
    sister_target_toggle(fb, 392, 330, 52, "H3",
        model->parameters.soak_targets & TS_SISTER_EFFECT_TARGET_H3, PAL_TUNING);
    sister_target_toggle(fb, 450, 330, 52, "MIX",
        model->parameters.soak_targets & TS_SISTER_EFFECT_TARGET_MIX,
        PAL_WAVE_RIGHT);
    text(fb, 510, 336, "STEREO WEAVE", PAL_MOUSE, 1);

sister_footer:
    snprintf(line, sizeof(line), "TARGET %s  %.88s",
             model->destination_slot >= 0 ? "READY" : "--",
             model->routing.capture_state == TS_CAPTURE_RECORDING ?
             "RECORDING - MONITOR LEVELS DO NOT CHANGE CAPTURE" : model->status);
    text(fb, 10, 355, line,
         model->routing.source_target_conflict ? PAL_VOLUME : PAL_MOUSE, 1);

    button(fb, 10, 370, 58, tap_names[model->selected_tap], 1);
    button(fb, 74, 370, 44, model->capture_channels == 2 ? "S" : "M", model->capture_channels == 2);
    button(fb, 124, 370, 100,
           model->destination_mode == TS_SISTER_UI_DEST_NEXT_EMPTY ? "NEXT EMPTY" : "CURRENT", 0);
    button(fb, 230, 370, 28, "<", 0);
    snprintf(preset_label, sizeof(preset_label), "%.18s",
             model->preset_name[0] != '\0' ? model->preset_name : "PRESET");
    button(fb, 264, 370, 130, preset_label, 0);
    button(fb, 400, 370, 28, ">", 0);
    button(fb, 450, 370, 82, "CAPTURE", model->routing.capture_state != TS_CAPTURE_IDLE && !model->capture_overdub);
    button(fb, 538, 370, 92, "OVERDUB", model->routing.capture_state != TS_CAPTURE_IDLE && model->capture_overdub);

    if (model->preset_manage_open) {
        rect(fb, 160, 130, 320, 170, RGB(8, 8, 9));
        rect(fb, 160, 130, 320, 1, PAL_MOUSE);
        rect(fb, 160, 299, 320, 1, PAL_MOUSE);
        rect(fb, 160, 130, 1, 170, PAL_MOUSE);
        rect(fb, 479, 130, 1, 170, PAL_MOUSE);
        text(fb, 180, 145, "SISTER PRESET MANAGER", PAL_TEXT, 1);
        text(fb, 180, 165,
             model->preset_editing ? model->preset_edit_name : model->preset_name,
             model->preset_editing ? PAL_NOTE : PAL_MOUSE, 1);
        if (model->preset_factory)
            text(fb, 180, 182, "FACTORY - RECALL ONLY", PAL_TUNING, 1);
        button(fb, 180, 200, 128, "SAVE AS", model->preset_editing == 1);
        button(fb, 332, 200, 128, "OVERWRITE", model->preset_confirmation == 1);
        button(fb, 180, 230, 128, "RENAME", model->preset_editing == 2);
        button(fb, 332, 230, 128, "DELETE", model->preset_confirmation == 2);
        button(fb, 180, 260, 128, "CONFIRM", model->preset_editing || model->preset_confirmation);
        button(fb, 332, 260, 128, "CANCEL", 0);
    }
}
