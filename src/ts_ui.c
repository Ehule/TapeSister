#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) (0xff000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* Temporary visual authority: tapehead.pal supplied by the user. */
#define PAL_TEXT RGB(255, 28, 0)
#define PAL_BLOCK RGB(45, 0, 57)
#define PAL_BLOCK_TEXT RGB(0, 158, 227)
#define PAL_MOUSE RGB(255, 210, 101)
#define PAL_DESKTOP RGB(28, 28, 28)
#define PAL_BUTTON RGB(93, 85, 93)
#define PAL_NOTE RGB(255, 231, 0)
#define PAL_INSTRUMENT RGB(24, 255, 0)
#define PAL_VOLUME RGB(255, 28, 231)
#define PAL_TUNING RGB(20, 125, 255)
#define PAL_EFFECT RGB(53, 255, 255)

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

static void line(TsFramebuffer *fb, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < TS_UI_WIDTH && y0 >= 0 && y0 < TS_UI_HEIGHT)
            fb->pixels[y0 * TS_UI_WIDTH + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
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
    case '/': return "00001000100001000100010001000010000";
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

static void frame(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t fill, uint32_t light)
{
    rect(fb, x, y, w, h, fill);
    rect(fb, x, y, w, 2, light);
    rect(fb, x, y, 2, h, light);
    rect(fb, x, y + h - 2, w, 2, RGB(14, 14, 14));
    rect(fb, x + w - 2, y, 2, h, RGB(14, 14, 14));
}

static void button(TsFramebuffer *fb, int x, int y, int w, const char *label, int active)
{
    uint32_t fill = active ? PAL_BLOCK : PAL_BUTTON;
    frame(fb, x, y, w, 23, fill, active ? PAL_MOUSE : RGB(140, 133, 140));
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
        text(fb, 58, 282,
             browser->mode == TS_BROWSER_EXPORT_BANK ? "FAMILY FOLDER" : "FILENAME",
             PAL_EFFECT, 1);
        rect(fb, 58, 294, 518, 24, RGB(8, 8, 8));
        if (length > 78) filename += length - 78;
        text(fb, 64, 303, filename, browser->filename_focus ? PAL_MOUSE : PAL_TEXT, 1);
        if (browser->filename_focus && cursor_visible) {
            int cursor_x = 64 + (int)strlen(filename) * 6;
            if (cursor_x > 572) cursor_x = 572;
            rect(fb, cursor_x, 301, 2, 11, PAL_MOUSE);
        }
    } else {
        text(fb, 58, 300, "SELECT AN EXISTING WAV OR TSR", PAL_EFFECT, 1);
    }

    button(fb, 58, 326, 72, "UP DIR", 0);
    button(fb, 135, 326, 120, browser->mode == TS_BROWSER_LOAD_WAV ? "OPEN" :
           browser->mode == TS_BROWSER_SAVE_RECIPE ? "SAVE" : "EXPORT",
           browser->overwrite_armed);
    button(fb, 260, 326, 84, "CANCEL", 0);
    snprintf(footer, sizeof(footer), "%.38s", browser->overwrite_armed ?
             "PRESS AGAIN TO OVERWRITE" : browser->message);
    text(fb, 354, 334, footer,
         browser->overwrite_armed ? PAL_VOLUME : RGB(190, 185, 190), 1);
}

void ts_ui_init(TsUiState *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->mouse_note = -1;
    ui->bank_view_slot = -1;
    ui->playhead_bank_slot = -1;
    ui->renaming_bank_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->show_keyboard = 1;
    ts_browser_init(&ui->browser);
    snprintf(ui->status, sizeof(ui->status), "READY - DROP A WAV OR GENERATE A PARENT");
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
    if (x >= width) x = width - 1;
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

void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument)
{
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
    button(fb, 447, 4, 82, "SAVE", 0);
    button(fb, 535, 4, 95, "EXPORT", 0);

    frame(fb, 10, 40, 620, 164, RGB(42, 39, 42), RGB(105, 98, 105));
    if (instrument->parent.frames) {
        char parent[96], info[112];
        snprintf(parent, sizeof(parent), "PARENT G%u %.28s",
                 instrument->generation, instrument->parent.name);
        if (showing_bank && shown_slot->occupied)
            snprintf(info, sizeof(info), "BANK %02d %.28s %u HZ %.2F SEC",
                     ui->bank_view_slot + 1, sample->name, sample->sample_rate,
                     (double)sample->frames / sample->sample_rate);
        else if (showing_bank)
            snprintf(info, sizeof(info), "BANK %02d EMPTY - SILENCE",
                     ui->bank_view_slot + 1);
        else
            snprintf(info, sizeof(info), "AUDITION %s %u HZ %.2F SEC",
                     showing_parent ? "PARENT" : "CURRENT", sample->sample_rate,
                     (double)sample->frames / sample->sample_rate);
        text(fb, 20, 49, parent, PAL_INSTRUMENT, 1);
        text(fb, 390, 49, info, PAL_EFFECT, 1);
    } else {
        text(fb, 20, 49, "NO PARENT", PAL_INSTRUMENT, 1);
    }

    rect(fb, TS_WAVE_X, TS_WAVE_Y, TS_WAVE_W, TS_WAVE_H, RGB(8, 8, 8));
    for (int x = TS_WAVE_X; x < TS_WAVE_X + TS_WAVE_W; x += 30)
        rect(fb, x, TS_WAVE_Y, 1, TS_WAVE_H, RGB(26, 24, 27));
    for (int y = TS_WAVE_Y + 20; y < TS_WAVE_Y + TS_WAVE_H; y += 20)
        rect(fb, TS_WAVE_X, y, TS_WAVE_W, 1, RGB(26, 24, 27));
    rect(fb, TS_WAVE_X, TS_WAVE_Y + TS_WAVE_H / 2, TS_WAVE_W, 1, RGB(74, 67, 75));

    if (has_loop && loop_last > view_first && loop_first < view_last) {
        int lx0 = frame_x(loop_first, view_first, view_last);
        int lx1 = frame_x(loop_last, view_first, view_last);
        rect(fb, lx0, TS_WAVE_Y, lx1 - lx0, TS_WAVE_H, RGB(5, 24, 48));
    }

    if (has_selection && selection_last > view_first &&
        selection_first < view_last) {
        int sx0 = frame_x(selection_first, view_first, view_last);
        int sx1 = frame_x(selection_last, view_first, view_last);
        rect(fb, sx0, TS_WAVE_Y, sx1 - sx0, TS_WAVE_H, PAL_BLOCK);
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
            line(fb, TS_WAVE_X + x, y0, TS_WAVE_X + x, y1, color);
            for (size_t i = begin; i < end && i < sample->frames; ++i) {
                if (sample->data[i] == 0.0f ||
                    (i > 0 && ((sample->data[i - 1u] < 0.0f && sample->data[i] > 0.0f) ||
                               (sample->data[i - 1u] > 0.0f && sample->data[i] < 0.0f)))) {
                    rect(fb, TS_WAVE_X + x, middle - 1, 1, 3, PAL_VOLUME);
                    break;
                }
            }
        }
    } else {
        text(fb, showing_bank ? 199 : 211, 135,
             showing_bank ? "EMPTY BANK SLOT" : "DROP WAV HERE",
             RGB(120, 113, 121), 2);
    }

    if (has_loop && loop_last > view_first && loop_first < view_last) {
        int lx0 = frame_x(loop_first, view_first, view_last);
        int lx1 = frame_x(loop_last, view_first, view_last);
        rect(fb, lx0, TS_WAVE_Y, 2, TS_WAVE_H, PAL_TUNING);
        rect(fb, lx1 - 2, TS_WAVE_Y, 2, TS_WAVE_H, PAL_TUNING);
        rect(fb, lx0, TS_WAVE_Y, 7, 4, PAL_TUNING);
        rect(fb, lx1 - 7, TS_WAVE_Y + TS_WAVE_H - 4, 7, 4, PAL_TUNING);
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
            rect(fb, playhead_x, TS_WAVE_Y, 2, TS_WAVE_H, playhead_color);
            rect(fb, playhead_x - 2, TS_WAVE_Y, 6, 3, playhead_color);
        }
    }

    button(fb, 10, 205, 70, "LOAD", ui->browser.mode == TS_BROWSER_LOAD_WAV);
    button(fb, 85, 205, 82, "GENERATE", 0);
    button(fb, 172, 205, 70, "RESEED", 0);
    button(fb, 247, 205, 78, "COMMIT", ui->commit_armed);
    button(fb, 330, 205, 72, "RESET", 0);
    button(fb, 407, 205, 61, "PARENT", !showing_bank && ui->audition_source == TS_AUDITION_PARENT);
    button(fb, 472, 205, 63, "CURRENT", !showing_bank && ui->audition_source == TS_AUDITION_CURRENT);
    button(fb, 540, 205, 90, "SET CURRENT",
           showing_bank && shown_slot->occupied);

    slider(fb, 10, 233, 100, "BODY", instrument->process.body, PAL_INSTRUMENT);
    slider(fb, 120, 233, 100, "EDGE", instrument->process.edge, PAL_VOLUME);
    slider(fb, 230, 233, 100, "DRIFT", instrument->process.drift, PAL_TUNING);
    button(fb, 345, 233, 53, "EDIT", ui->fx_page == TS_FX_EDIT);
    button(fb, 402, 233, 53, "NOISE", ui->fx_page == TS_FX_NOISE);
    button(fb, 459, 233, 53, "DELAY", ui->fx_page == TS_FX_DELAY);
    button(fb, 516, 233, 53, "SPACE", ui->fx_page == TS_FX_SPACE);
    button(fb, 573, 233, 57, "LOOP", ui->fx_page == TS_FX_LOOP);

    if (ui->fx_page == TS_FX_EDIT) {
        button(fb, 10, 261, 94, "REVERSE", 0);
        button(fb, 109, 261, 94, "NORMALIZE", 0);
        button(fb, 208, 261, 84, "AMP DOWN", 0);
        button(fb, 297, 261, 84, "AMP UP", 0);
        button(fb, 386, 261, 110, "FADE IN", 0);
        button(fb, 501, 261, 110, "FADE OUT", 0);
    } else if (ui->fx_page == TS_FX_NOISE) {
        char color[32];
        button(fb, 10, 261, 94, instrument->process.noise_enabled ? "NOISE ON" : "NOISE OFF",
               instrument->process.noise_enabled);
        slider(fb, 118, 261, 180, "AMOUNT", instrument->process.noise_amount, PAL_NOTE);
        snprintf(color, sizeof(color), "COLOR %s", ts_noise_color_name(instrument->process.noise_color));
        button(fb, 312, 261, 150, color, 0);
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
        button(fb, 10, 261, 84, "SET LOOP", instrument->has_loop);
        button(fb, 99, 261, 84, "CLEAR", 0);
        button(fb, 188, 261, 94, "PLAY LOOP", ui->playback_active);
        snprintf(crossfade, sizeof(crossfade), "XFADE %.1F MS", instrument->loop_crossfade_ms);
        slider(fb, 297, 261, 280, crossfade, instrument->loop_crossfade_ms / 50.0f,
               PAL_TUNING);
    }

    button(fb, 10, 289, 70, "PLAY ALL", 0);
    button(fb, 85, 289, 72, "PLAY SEL", 0);
    button(fb, 162, 289, 78, "PLAY VIEW", 0);
    button(fb, 245, 289, 52, "CROP", 0);
    button(fb, 302, 289, 74, "ZOOM SEL", 0);
    button(fb, 381, 289, 74, "SHOW ALL", 0);
    button(fb, 460, 289, 56, "UNDO", instrument->undo_count > 0);
    button(fb, 521, 289, 62, "REDO", instrument->redo_count > 0);
    button(fb, 588, 289, 42, ui->show_keyboard ? "BANK" : "KEYS", !ui->show_keyboard);

    if (ui->show_keyboard) {
        text(fb, 11, 318, "WHEEL ZOOM  SHIFT+WHEEL PAN  =/- ZOOM  ARROWS PAN  SHIFT+CLICK CHORD", RGB(184, 180, 184), 1);
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
    } else {
        text(fb, 11, 318, "CLICK PLAY  SHIFT FULL  ALT LOOP  CTRL SEL  RMB RENAME  SHIFT+RMB CLEAR", RGB(184, 180, 184), 1);
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
            if (i == ui->bank_view_slot) rect(fb, x + 2, y + 2, 68, 2, PAL_MOUSE);
        }
    }
    rect(fb, 0, 386, TS_UI_WIDTH, 14, RGB(10, 10, 10));
    text(fb, 8, 389, ui->status, PAL_MOUSE, 1);

    if (ui->browser.mode != TS_BROWSER_CLOSED)
        browser_render(fb, &ui->browser, ui->text_cursor_visible);
    else if (ui->renaming_bank_slot >= 0) {
        const char *shown = ui->bank_rename;
        size_t length = strlen(shown);
        char title[40];
        if (length > 62) shown += length - 62;
        frame(fb, 104, 306, 432, 76, RGB(36, 33, 37), PAL_MOUSE);
        snprintf(title, sizeof(title), "RENAME BANK %02d", ui->renaming_bank_slot + 1);
        text(fb, 116, 316, title, PAL_NOTE, 1);
        rect(fb, 116, 332, 408, 23, RGB(8, 8, 8));
        text(fb, 122, 340, shown, PAL_MOUSE, 1);
        if (ui->text_cursor_visible)
            rect(fb, 122 + (int)strlen(shown) * 6, 338, 2, 11, PAL_MOUSE);
        text(fb, 116, 365, "ENTER ACCEPTS   ESC CANCELS", RGB(190, 185, 190), 1);
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
    return TS_UI_BANK_ACTION_INVALID;
}
