#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) (0xff000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static const TsPalette *render_palette;

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

static void wave_rect(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t color)
{
    if (x < TS_WAVE_X) { w -= TS_WAVE_X - x; x = TS_WAVE_X; }
    if (y < TS_WAVE_Y) { h -= TS_WAVE_Y - y; y = TS_WAVE_Y; }
    if (x + w > TS_WAVE_X + TS_WAVE_W) w = TS_WAVE_X + TS_WAVE_W - x;
    if (y + h > TS_WAVE_Y + TS_WAVE_H) h = TS_WAVE_Y + TS_WAVE_H - y;
    if (w > 0 && h > 0) rect(fb, x, y, w, h, color);
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

static void slider(TsFramebuffer *fb, int x, int y, int w, const char *label, float value,
                   uint32_t color)
{
    text(fb, x, y, label, RGB(222, 218, 214), 1);
    rect(fb, x, y + 13, w, 6, RGB(12, 12, 12));
    rect(fb, x + 1, y + 14, (int)((w - 2) * value), 4, color);
    int knob = x + (int)((w - 6) * value);
    rect(fb, knob, y + 10, 6, 12, PAL_MOUSE);
}

static uint32_t family_relation_color(TsFamilyRelation relation)
{
    if (relation == TS_FAMILY_CHILD) return PAL_INSTRUMENT;
    if (relation == TS_FAMILY_COUSIN) return PAL_TUNING;
    if (relation == TS_FAMILY_STRANGER) return PAL_VOLUME;
    if (relation == TS_FAMILY_CAPTURED) return PAL_EFFECT;
    return PAL_NOTE;
}

static void browser_render(TsFramebuffer *fb, const TsBrowser *browser, int cursor_visible)
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

    if (browser->mode != TS_BROWSER_LOAD_WAV) {
        const char *filename = browser->filename;
        size_t length = strlen(filename);
        size_t cursor = browser->filename_cursor > length ? length :
                        browser->filename_cursor;
        size_t first = length > 78 ? length - 78 : 0;
        text(fb, 58, 282,
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
    } else {
        text(fb, 58, 300, "SELECT AN EXISTING WAV, TSR, OR TSP", PAL_EFFECT, 1);
    }

    button(fb, 58, 326, 72, "UP DIR", 0);
    button(fb, 135, 326, 120, browser->mode == TS_BROWSER_LOAD_WAV ? "OPEN" :
           (browser->mode == TS_BROWSER_SAVE_RECIPE ||
            browser->mode == TS_BROWSER_SAVE_PRESET) ? "SAVE" : "EXPORT",
           browser->overwrite_armed);
    button(fb, 260, 326, 84, "CANCEL", 0);
    snprintf(footer, sizeof(footer), "%.38s", browser->overwrite_armed ?
             "PRESS AGAIN TO OVERWRITE" : browser->message);
    text(fb, 354, 334, footer,
         browser->overwrite_armed ? PAL_VOLUME : RGB(190, 185, 190), 1);
}

static void config_render(TsFramebuffer *fb, const TsUiState *ui)
{
    static const int field_y[TS_CONFIG_FIELD_COUNT] = {103, 158, 213};
    frame(fb, 34, 46, 572, 306, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 50, 60, "CONFIGURATION", PAL_NOTE, 1);
    text(fb, 50, 75,
         "PATHS MAY BE BLANK  TAB CHANGES FIELD  CTRL BACKSPACE CLEARS",
         RGB(190, 185, 190), 1);
    for (int i = 0; i < TS_CONFIG_FIELD_COUNT; ++i) {
        const char *value = ts_config_field_const(&ui->config, (TsConfigField)i);
        size_t length = strlen(value);
        size_t cursor = i == (int)ui->config_field ? ui->config_cursor : length;
        size_t first = length > 82u ? length - 82u : 0u;
        char shown[83];
        if (cursor > length) cursor = length;
        if (cursor < first) first = cursor;
        if (cursor > first + 82u) first = cursor - 82u;
        snprintf(shown, sizeof(shown), "%.82s", value + first);
        text(fb, 50, field_y[i] - 13,
             ts_config_field_name((TsConfigField)i),
             i == (int)ui->config_field ? PAL_EFFECT : RGB(190, 185, 190), 1);
        rect(fb, 50, field_y[i], 540, 24, RGB(8, 8, 8));
        text(fb, 56, field_y[i] + 8, shown,
             i == (int)ui->config_field ? PAL_MOUSE : PAL_INSTRUMENT, 1);
        if (i == (int)ui->config_field && ui->text_cursor_visible)
            rect(fb, 56 + (int)(cursor - first) * 6,
                 field_y[i] + 5, 2, 14, PAL_MOUSE);
    }
    button(fb, 50, 274, 110, "SAVE CONFIG", 0);
    button(fb, 170, 274, 100, "USE CWD", 0);
    button(fb, 280, 274, 88, "PALETTE", 0);
    button(fb, 378, 274, 82, "CANCEL", 0);
    text(fb, 50, 312,
         "SEND FT2 EXPORTS THE LOOP AWARE COLLECTION THEN LAUNCHES FT2",
         PAL_TUNING, 1);
    text(fb, 50, 327,
         "PALETTE EDITOR READS AND WRITES TAPEHEAD COMPATIBLE PAL FILES",
         PAL_VOLUME, 1);
}

static void palette_render(TsFramebuffer *fb, const TsUiState *ui)
{
    static const char *const channel_names[3] = {"RED", "GREEN", "BLUE"};
    char value[48];
    uint32_t selected = ui->palette.colors[ui->palette_entry];
    frame(fb, 28, 34, 584, 344, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 44, 47, "PALETTE EDITOR", PAL_NOTE, 1);
    text(fb, 350, 47, "TAPEHEAD PAL COMPATIBLE", PAL_EFFECT, 1);
    for (int color = 0; color < TS_PALETTE_COLOR_COUNT; ++color) {
        int column = color / 6;
        int row = color % 6;
        int x = 44 + column * 282;
        int y = 66 + row * 25;
        uint32_t entry = ui->palette.colors[color];
        rect(fb, x, y, 268, 21, color == ui->palette_entry ? PAL_BLOCK : RGB(18, 18, 18));
        if (color == ui->palette_entry) {
            rect(fb, x, y, 268, 2, PAL_MOUSE);
            rect(fb, x, y + 19, 268, 2, PAL_MOUSE);
            rect(fb, x, y, 2, 21, PAL_MOUSE);
            rect(fb, x + 266, y, 2, 21, PAL_MOUSE);
        }
        rect(fb, x + 5, y + 4, 24, 13, entry);
        text(fb, x + 36, y + 7, ts_palette_color_name((TsPaletteColor)color),
             color == ui->palette_entry ? PAL_BLOCK_TEXT : RGB(222, 218, 214), 1);
        snprintf(value, sizeof(value), "#%06X", (unsigned)(entry & 0xffffffu));
        text(fb, x + 200, y + 7, value, color == ui->palette_entry ? PAL_MOUSE : PAL_TEXT, 1);
    }
    snprintf(value, sizeof(value), "%s  #%06X",
             ts_palette_color_name((TsPaletteColor)ui->palette_entry),
             (unsigned)(selected & 0xffffffu));
    text(fb, 44, 220, value, PAL_INSTRUMENT, 1);
    for (int component = 0; component < 3; ++component) {
        float amount = ts_palette_component(&ui->palette,
                       (TsPaletteColor)ui->palette_entry, component) / 255.0f;
        int y = 237 + component * 27;
        slider(fb, 44, y, 322, channel_names[component], amount,
               component == 0 ? PAL_TEXT : component == 1 ? PAL_INSTRUMENT : PAL_TUNING);
        if (component == ui->palette_channel)
            rect(fb, 40, y + 9, 3, 14, PAL_MOUSE);
        snprintf(value, sizeof(value), "%3u",
                 (unsigned)ts_palette_component(&ui->palette,
                 (TsPaletteColor)ui->palette_entry, component));
        text(fb, 378, y + 13, value, component == ui->palette_channel ? PAL_MOUSE : PAL_EFFECT, 1);
    }
    frame(fb, 456, 226, 132, 50, selected, PAL_MOUSE);
    snprintf(value, sizeof(value), "DESKTOP %d", ui->palette.desktop_contrast);
    slider(fb, 456, 282, 132, value,
           (float)(ui->palette.desktop_contrast - 1) / 99.0f, PAL_DESKTOP);
    if (ui->palette_channel == 3)
        rect(fb, 452, 291, 3, 14, PAL_MOUSE);
    snprintf(value, sizeof(value), "BUTTON %d", ui->palette.buttons_contrast);
    slider(fb, 456, 309, 132, value,
           (float)(ui->palette.buttons_contrast - 1) / 99.0f, PAL_BUTTON);
    if (ui->palette_channel == 4)
        rect(fb, 452, 318, 3, 14, PAL_MOUSE);
    button(fb, 44, 339, 82, "IMPORT TH", 0);
    button(fb, 132, 339, 76, "SAVE TS", 0);
    button(fb, 214, 339, 88, "EXPORT TH", 0);
    button(fb, 308, 339, 68, "RESET", 0);
    button(fb, 382, 339, 62, "DONE", 1);
    button(fb, 450, 339, 76, "CANCEL", 0);
    text(fb, 532, 347, "ARROWS", PAL_TUNING, 1);
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
    ts_warp_gesture_init(&ui->warp_gesture);
    ts_smear_gesture_init(&ui->smear_gesture);
    ts_tear_gesture_init(&ui->tear_gesture);
    ui->mouse_note = -1;
    ui->bank_view_slot = -1;
    ui->load_bank_slot = -1;
    ui->playhead_bank_slot = -1;
    ui->renaming_bank_slot = -1;
    ui->renaming_recipe_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->show_keyboard = 1;
    ui->show_recipes = 0;
    ts_browser_init(&ui->browser);
    ts_config_init(&ui->config);
    ts_palette_default(&ui->palette);
    ui->palette_entry = TS_PALETTE_PATTERN_TEXT;
    ui->palette_channel = 0;
    ts_recipe_bank_init(&ui->recipes);
    snprintf(ui->status, sizeof(ui->status), "READY - SELECT A TILE, LOAD, OR CREATE");
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

int ts_ui_transform_auto_audition_allowed(const TsUiState *ui)
{
    return ui == NULL || !ui->workbench_loop_active;
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
    if (ui != NULL && ui->has_pitch_suggestion) return &ui->pitch_suggestion;
    return instrument != NULL ? &instrument->tuning : NULL;
}

const TsTuning *ts_ui_display_tuning(const TsUiState *ui,
                                     const TsInstrument *instrument)
{
    if (ui != NULL && ui->has_pitch_suggestion) return &ui->pitch_suggestion;
    return instrument != NULL ? &instrument->audible_tuning : NULL;
}

void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument)
{
    render_palette = &ui->palette;
    const TsTuning *display_tuning = ts_ui_display_tuning(ui, instrument);
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
    } else if (showing_parent) {
        valid_parent_view(ui, instrument->parent.frames, &view_first, &view_last);
        selection_first += instrument->crop_first;
        selection_last += instrument->crop_first;
        loop_first += instrument->crop_first;
        loop_last += instrument->crop_first;
    }
    clear(fb, PAL_DESKTOP);

    rect(fb, 0, 0, TS_UI_WIDTH, 32, RGB(12, 12, 12));
    text(fb, 14, 9, "TAPESISTER", PAL_TEXT, 2);
    button(fb, 350, 4, 76, "CONFIG", ui->config_open);
    button(fb, 431, 4, 80, "SEND FT2", 0);
    button(fb, 516, 4, 52, "SAVE", 0);
    button(fb, 573, 4, 57, "EXPORT", 0);

    frame(fb, 10, 40, 620, 164, RGB(42, 39, 42), RGB(105, 98, 105));
    if (sample->frames) {
        char tile[96], info[112];
        int tile_number = showing_bank ? ui->bank_view_slot + 1 :
                          instrument->selected_slot + 1;
        snprintf(tile, sizeof(tile), "TILE %02d %.36s", tile_number, sample->name);
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
        text(fb, 390, 49, info, PAL_EFFECT, 1);
    } else {
        char empty[40];
        snprintf(empty, sizeof(empty), "TILE %02d EMPTY", instrument->selected_slot + 1);
        text(fb, 20, 49, empty, PAL_INSTRUMENT, 1);
    }

    wave_rect(fb, TS_WAVE_X, TS_WAVE_Y, TS_WAVE_W, TS_WAVE_H, RGB(8, 8, 8));
    for (int x = TS_WAVE_X; x < TS_WAVE_X + TS_WAVE_W; x += 30)
        wave_rect(fb, x, TS_WAVE_Y, 1, TS_WAVE_H, RGB(26, 24, 27));
    for (int y = TS_WAVE_Y + 20; y < TS_WAVE_Y + TS_WAVE_H; y += 20)
        wave_rect(fb, TS_WAVE_X, y, TS_WAVE_W, 1, RGB(26, 24, 27));
    wave_rect(fb, TS_WAVE_X, TS_WAVE_Y + TS_WAVE_H / 2, TS_WAVE_W, 1, RGB(74, 67, 75));

    if (has_loop && loop_last > view_first && loop_first < view_last) {
        int lx0 = frame_x(loop_first, view_first, view_last);
        int lx1 = frame_x(loop_last, view_first, view_last);
        wave_rect(fb, lx0, TS_WAVE_Y, lx1 - lx0, TS_WAVE_H, RGB(5, 24, 48));
    }

    if (has_selection && selection_last > view_first &&
        selection_first < view_last) {
        int sx0 = frame_x(selection_first, view_first, view_last);
        int sx1 = frame_x(selection_last, view_first, view_last);
        wave_rect(fb, sx0, TS_WAVE_Y, sx1 - sx0, TS_WAVE_H, PAL_BLOCK);
    }
    if (sample->frames && view_last > view_first) {
        if (view_last > sample->frames) view_last = sample->frames;
        for (int x = 0; x < TS_WAVE_W; ++x) {
            size_t begin = view_first + (size_t)x * (view_last - view_first) / TS_WAVE_W;
            size_t end = view_first + (size_t)(x + 1) * (view_last - view_first) / TS_WAVE_W;
            if (end <= begin) end = begin + 1;
            float low = 1.0f, high = -1.0f;
            for (size_t i = begin; i < end && i < sample->frames; ++i) {
                if (sample->data[i] < low) low = sample->data[i];
                if (sample->data[i] > high) high = sample->data[i];
            }
            int middle = TS_WAVE_Y + TS_WAVE_H / 2;
            int y0 = middle - (int)(high * (TS_WAVE_H / 2 - 6));
            int y1 = middle - (int)(low * (TS_WAVE_H / 2 - 6));
            size_t at = begin;
            uint32_t color = has_selection && at >= selection_first &&
                             at < selection_last ? PAL_BLOCK_TEXT : PAL_NOTE;
            wave_line(fb, TS_WAVE_X + x, y0, TS_WAVE_X + x, y1, color);
            for (size_t i = begin; i < end && i < sample->frames; ++i) {
                if (sample->data[i] == 0.0f ||
                    (i > 0 && ((sample->data[i - 1u] < 0.0f && sample->data[i] > 0.0f) ||
                               (sample->data[i - 1u] > 0.0f && sample->data[i] < 0.0f)))) {
                    wave_rect(fb, TS_WAVE_X + x, middle - 1, 1, 3, PAL_VOLUME);
                    break;
                }
            }
        }
    } else {
        text(fb, showing_bank ? 199 : 211, 135,
             showing_bank ? "EMPTY BANK SLOT" : "DROP WAV HERE",
             RGB(120, 113, 121), 2);
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

    if (ui->playback_active && ui->playhead_frames > 0) {
        int playhead_x = -1;
        uint32_t playhead_color = ui->playhead_source == TS_AUDITION_PARENT ?
                                  PAL_INSTRUMENT : PAL_MOUSE;
        if (((showing_bank && ui->playhead_bank_slot == ui->bank_view_slot) ||
             (!showing_bank && ui->playhead_bank_slot < 0 &&
              ui->playhead_source == ui->audition_source)) &&
            ui->playhead_frame >= view_first && ui->playhead_frame <= view_last) {
            playhead_x = frame_x(ui->playhead_frame, view_first, view_last);
        }
        if (playhead_x >= TS_WAVE_X && playhead_x <= TS_WAVE_X + TS_WAVE_W) {
            if (playhead_x == TS_WAVE_X + TS_WAVE_W) --playhead_x;
            wave_rect(fb, playhead_x, TS_WAVE_Y, 2, TS_WAVE_H, playhead_color);
            wave_rect(fb, playhead_x - 2, TS_WAVE_Y, 6, 3, playhead_color);
        }
    }

    button(fb, 10, 205, 70, "LOAD", ui->browser.mode == TS_BROWSER_LOAD_WAV);
    button(fb, 85, 205, 82, "CREATE", 0);
    button(fb, 172, 205, 70, "VARY", 0);
    button(fb, 247, 205, 78, "LOOP", ui->workbench_loop_active);

    slider(fb, 10, 233, 72, "BODY", instrument->process.body, PAL_INSTRUMENT);
    slider(fb, 88, 233, 72, "EDGE", instrument->process.edge, PAL_VOLUME);
    slider(fb, 166, 233, 72, "DRIFT", instrument->process.drift, PAL_TUNING);
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
        snprintf(root, sizeof(root), "ROOT %s",
                 ts_midi_note_name(display_tuning->root_note, note, sizeof(note)));
        snprintf(fine, sizeof(fine), "TRIM %+.1F C",
                 display_tuning->fine_tune_cents);
        snprintf(frequency, sizeof(frequency), "%.2F HZ",
                 ts_tuning_frequency(display_tuning));
        button(fb, 10, 261, 48, "DOWN", 0);
        button(fb, 62, 261, 90, root, 1);
        button(fb, 156, 261, 48, "UP", 0);
        slider(fb, 214, 261, 146, fine,
               (display_tuning->fine_tune_cents + 100.0f) / 200.0f, PAL_TUNING);
        button(fb, 370, 261, 90, frequency, 0);
        button(fb, 470, 261, 160,
               ui->has_pitch_suggestion ? "ACCEPT SUGGESTION" : "SUGGEST PITCH",
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
        slider(fb, 10, 261, 520, mutation, instrument->family_mutation, PAL_VOLUME);
        button(fb, 538, 261, 92,
               instrument->family_trajectory ? "CHAIN ON" : "CHAIN OFF",
               instrument->family_trajectory);
    } else if (ui->fx_page == TS_FX_DELAY) {
        button(fb, 10, 261, 94, instrument->process.delay_enabled ? "DELAY ON" : "DELAY OFF",
               instrument->process.delay_enabled);
        slider(fb, 118, 261, 92, "TIME", instrument->process.delay_seconds, PAL_NOTE);
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

    button(fb, 10, 289, 70, "PLAY ALL", 0);
    button(fb, 85, 289, 72, "PLAY SEL", 0);
    button(fb, 162, 289, 78, "PLAY VIEW", 0);
    if (!ui->show_keyboard && !ui->show_recipes && !ui->show_ingredients)
        button(fb, 245, 289, 131,
               ui->bank_clear_armed ? "CONFIRM CLEAR" : "CLEAR ALL",
               ui->bank_clear_armed);
    else {
        button(fb, 245, 289, 52, "CROP", 0);
        button(fb, 302, 289, 74, "ZOOM SEL", 0);
    }
    button(fb, 381, 289, 74, "SHOW ALL", 0);
    button(fb, 588, 289, 42, ui->show_keyboard ? "BANK" :
           ui->show_recipes ? "INGR" : ui->show_ingredients ? "KEYS" : "RCPE",
           !ui->show_keyboard);

    if (ui->show_keyboard) {
        text(fb, 11, 318, "SHIFT+CLICK CHORD  SHIFT+RIGHT CLICK SETS ROOT NOTE", RGB(184, 180, 184), 1);
        const int white_x = 10, white_y = 330, white_w = 43, white_h = 49;
        const char *labels[14] = {"C","D","E","F","G","A","B","C","D","E","F","G","A","B"};
        const int white_semitones[14] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
        for (int i = 0; i < 14; ++i) {
            int active = (ui->active_notes & (1u << white_semitones[i])) != 0;
            rect(fb, white_x + i * white_w, white_y, white_w - 1, white_h,
                 active ? PAL_MOUSE : RGB(220, 216, 207));
            text(fb, white_x + i * white_w + 23, white_y + 36, labels[i], RGB(24, 24, 24), 1);
        }
        {
            const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
            const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
            for (int i = 0; i < 10; ++i) {
                int key = semitones[i];
                int active = (ui->active_notes & (1u << key)) != 0;
                rect(fb, white_x + (black_after[i] + 1) * white_w - 16, white_y, 31, 31,
                     active ? PAL_VOLUME : RGB(18, 18, 18));
            }
        }
    } else if (ui->show_recipes) {
        text(fb, 11, 318,
             "CLICK APPLY  SHIFT CLICK CAPTURE  RMB RENAME  SHIFT+RMB CLEAR  SAVE TSP",
             RGB(184, 180, 184), 1);
        for (int i = 0; i < TS_RECIPE_SLOT_COUNT; ++i) {
            const TsPortableRecipe *slot = &ui->recipes.slots[i];
            char label[24];
            int x = 10 + (i % 8) * 77;
            int y = 330 + (i / 8) * 25;
            if (slot->occupied)
                snprintf(label, sizeof(label), "%02d %.7s", i + 1, slot->name);
            else
                snprintf(label, sizeof(label), "%02d USER", i + 1);
            button(fb, x, y, 72, label, i == ui->recipes.active_slot);
            if (slot->factory) rect(fb, x + 2, y + 2, 3, 19, PAL_INSTRUMENT);
        }
    } else if (ui->show_ingredients) {
        text(fb, 11, 318, "INGR  INGREDIENT SHELVES COMING SOON",
             RGB(184, 180, 184), 1);
        text(fb, 11, 348, "SELECTED TILE REMAINS ON THE WORKBENCH",
             PAL_INSTRUMENT, 1);
    } else {
        text(fb, 11, 318,
             ui->fx_page == TS_FX_EDIT ?
             "PASTE REPLACES TARGET  NO RANGE PASTES IN PLACE  FIT STRETCHES" :
             ui->fx_page == TS_FX_FAMILY && instrument->has_selection ?
             "CREATE/VARY STAMP FM INSIDE SELECTION  OUTSIDE STAYS UNCHANGED" :
             ui->fx_page == TS_FX_FAMILY ?
             "CREATE FILLS ACTIVE TILE  VARY REPLACES OR CHAINS TO NEXT EMPTY" :
             "CLICK PLAY  DOUBLE EMPTY SILENCE  SHIFT FULL  ALT LOOP  CTRL SEL  CTRL+SHIFT CLONE",
             RGB(184, 180, 184), 1);
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
            if (slot->occupied)
                rect(fb, x + 2, y + 19, 61, 2,
                     family_relation_color(slot->relation));
            if (i == ui->bank_view_slot) rect(fb, x + 4, y + 4, 64, 2, PAL_EFFECT);
            if (i == instrument->selected_slot) {
                rect(fb, x - 2, y - 2, 76, 3, PAL_MOUSE);
                rect(fb, x - 2, y + 22, 76, 3, PAL_MOUSE);
                rect(fb, x - 2, y - 2, 3, 27, PAL_MOUSE);
                rect(fb, x + 71, y - 2, 3, 27, PAL_MOUSE);
            }
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
    } else if (ui->palette_open)
        palette_render(fb, ui);
    else if (ui->config_open)
        config_render(fb, ui);
    else if (ui->browser.mode != TS_BROWSER_CLOSED)
        browser_render(fb, &ui->browser, ui->text_cursor_visible);
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
