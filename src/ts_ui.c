#include "tapesister/ui.h"

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

void ts_ui_init(TsUiState *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->active_key = -1;
    snprintf(ui->status, sizeof(ui->status), "READY - DROP A WAV OR GENERATE A PARENT");
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
    const TsSample *sample = &instrument->current;
    size_t view_first = instrument->view_first;
    size_t view_last = instrument->view_last;
    clear(fb, PAL_DESKTOP);

    rect(fb, 0, 0, TS_UI_WIDTH, 32, RGB(12, 12, 12));
    text(fb, 14, 9, "TAPESISTER", PAL_TEXT, 2);
    button(fb, 447, 4, 82, "SAVE", 0);
    button(fb, 535, 4, 95, "EXPORT", 0);

    frame(fb, 10, 40, 620, 188, RGB(42, 39, 42), RGB(105, 98, 105));
    if (instrument->parent.frames) {
        char parent[96], info[112];
        snprintf(parent, sizeof(parent), "PARENT %.34s", instrument->parent.name);
        snprintf(info, sizeof(info), "CURRENT %u HZ %.2F SEC",
                 sample->sample_rate, (double)sample->frames / sample->sample_rate);
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

    if (instrument->has_selection && instrument->selection_last > view_first &&
        instrument->selection_first < view_last) {
        int sx0 = frame_x(instrument->selection_first, view_first, view_last);
        int sx1 = frame_x(instrument->selection_last, view_first, view_last);
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
            uint32_t color = instrument->has_selection && at >= instrument->selection_first &&
                             at < instrument->selection_last ? PAL_BLOCK_TEXT : PAL_NOTE;
            line(fb, TS_WAVE_X + x, y0, TS_WAVE_X + x, y1, color);
        }
    } else {
        text(fb, 211, 135, "DROP WAV HERE", RGB(120, 113, 121), 2);
    }

    button(fb, 10, 234, 74, "LOAD", ui->path_entry);
    button(fb, 89, 234, 86, "GENERATE", 0);
    button(fb, 180, 234, 72, "RESEED", 0);
    slider(fb, 267, 234, 70, "BODY", instrument->process.body, PAL_INSTRUMENT);
    slider(fb, 354, 234, 70, "EDGE", instrument->process.edge, PAL_VOLUME);
    slider(fb, 441, 234, 70, "DRIFT", instrument->process.drift, PAL_TUNING);
    button(fb, 540, 234, 90, "STOP ALL", 0);

    button(fb, 10, 262, 74, "PLAY ALL", 0);
    button(fb, 89, 262, 76, "PLAY SEL", 0);
    button(fb, 170, 262, 82, "PLAY VIEW", 0);
    button(fb, 257, 262, 58, "CROP", 0);
    button(fb, 320, 262, 82, "ZOOM SEL", 0);
    button(fb, 407, 262, 82, "SHOW ALL", 0);
    button(fb, 494, 262, 62, "UNDO", instrument->undo_count > 0);
    button(fb, 561, 262, 69, "REDO", instrument->redo_count > 0);

    text(fb, 11, 291, "SELECT WAVEFORM WITH MOUSE   PLAY KEYS Z-M AND Q-U", RGB(184, 180, 184), 1);
    const int white_x = 10, white_y = 306, white_w = 43, white_h = 73;
    const char *labels[14] = {"C","D","E","F","G","A","B","C","D","E","F","G","A","B"};
    for (int i = 0; i < 14; ++i) {
        int active = ui->active_key == i;
        rect(fb, white_x + i * white_w, white_y, white_w - 1, white_h,
             active ? PAL_MOUSE : RGB(220, 216, 207));
        text(fb, white_x + i * white_w + 23, white_y + 58, labels[i], RGB(24, 24, 24), 1);
    }
    const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    for (int i = 0; i < 10; ++i) {
        int key = semitones[i];
        int active = ui->active_key == key;
        rect(fb, white_x + (black_after[i] + 1) * white_w - 16, white_y, 31, 44,
             active ? PAL_VOLUME : RGB(18, 18, 18));
    }
    rect(fb, 0, 386, TS_UI_WIDTH, 14, RGB(10, 10, 10));
    text(fb, 8, 389, ui->status, PAL_MOUSE, 1);

    if (ui->path_entry) {
        frame(fb, 58, 126, 524, 88, PAL_BUTTON, PAL_MOUSE);
        text(fb, 72, 140, "LOAD WAV PATH", PAL_NOTE, 1);
        rect(fb, 72, 158, 496, 25, RGB(8, 8, 8));
        const char *shown = ui->path[0] ? ui->path : "TYPE PATH OR DROP FILE";
        size_t length = strlen(shown);
        if (length > 80) shown += length - 80;
        text(fb, 78, 167, shown, PAL_EFFECT, 1);
        text(fb, 72, 192, "ENTER LOADS   ESC CANCELS", RGB(210, 205, 210), 1);
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
    if (x < 10 || x >= 622 || y < 306 || y >= 379) return -1;
    const int white_w = 43;
    const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    if (y < 350) {
        for (int i = 0; i < 10; ++i) {
            int left = 10 + (black_after[i] + 1) * white_w - 16;
            if (x >= left && x < left + 31) return semitones[i];
        }
    }
    static const int white_semitones[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
    int index = (x - 10) / white_w;
    return index >= 0 && index < 14 ? white_semitones[index] : -1;
}
