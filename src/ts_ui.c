#include "tapesister/ui.h"

#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) (0xff000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

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
                if (bits[gy * 5 + gx] == '1') rect(fb, x + gx * scale, y + gy * scale, scale, scale, color);
    }
}

static void frame(TsFramebuffer *fb, int x, int y, int w, int h, uint32_t dark, uint32_t light)
{
    rect(fb, x, y, w, h, dark);
    rect(fb, x, y, w, 2, light);
    rect(fb, x, y, 2, h, light);
    rect(fb, x, y + h - 2, w, 2, RGB(21, 22, 29));
    rect(fb, x + w - 2, y, 2, h, RGB(21, 22, 29));
}

static void button(TsFramebuffer *fb, int x, int y, int w, const char *label, int active)
{
    uint32_t fill = active ? RGB(184, 108, 78) : RGB(64, 67, 79);
    frame(fb, x, y, w, 24, fill, active ? RGB(242, 165, 108) : RGB(126, 131, 145));
    text(fb, x + 8, y + 8, label, RGB(238, 232, 215), 1);
}

static void slider(TsFramebuffer *fb, int x, int y, int w, const char *label, float value)
{
    text(fb, x, y, label, RGB(187, 189, 195), 1);
    rect(fb, x, y + 14, w, 6, RGB(28, 29, 36));
    rect(fb, x + 1, y + 15, (int)((w - 2) * value), 4, RGB(144, 95, 164));
    int knob = x + (int)((w - 6) * value);
    rect(fb, knob, y + 11, 6, 12, RGB(223, 174, 109));
}

void ts_ui_init(TsUiState *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->recipe.seed = 0x54415045u;
    ui->recipe.body = 0.62f;
    ui->recipe.edge = 0.34f;
    ui->recipe.drift = 0.23f;
    ui->recipe.seconds = 2.0f;
    ui->recipe.frequency = 130.8128f;
    ui->active_key = -1;
    snprintf(ui->status, sizeof(ui->status), "READY - DROP A WAV OR GENERATE");
}

void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsSample *sample)
{
    const uint32_t bg = RGB(38, 39, 48);
    const uint32_t panel = RGB(52, 54, 65);
    const uint32_t ink = RGB(232, 225, 210);
    const uint32_t purple = RGB(162, 102, 181);
    clear(fb, bg);

    rect(fb, 0, 0, TS_UI_WIDTH, 32, RGB(26, 27, 34));
    text(fb, 14, 9, "TAPESISTER", RGB(234, 165, 101), 2);
    button(fb, 447, 4, 82, "SAVE", 0);
    button(fb, 535, 4, 95, "EXPORT", 0);

    frame(fb, 10, 42, 620, 202, panel, RGB(91, 94, 107));
    text(fb, 20, 52, sample && sample->frames ? sample->name : "NO SAMPLE", ink, 1);
    if (sample && sample->frames) {
        char info[96];
        snprintf(info, sizeof(info), "%u HZ  %.2F SEC  PEAK %.2F", sample->sample_rate,
                 (double)sample->frames / sample->sample_rate, ts_sample_peak(sample));
        text(fb, 360, 52, info, RGB(153, 156, 166), 1);
    }

    rect(fb, 20, 70, 600, 154, RGB(17, 18, 23));
    for (int x = 20; x < 620; x += 30) rect(fb, x, 70, 1, 154, RGB(31, 32, 40));
    for (int y = 90; y < 224; y += 20) rect(fb, 20, y, 600, 1, RGB(31, 32, 40));
    rect(fb, 20, 146, 600, 1, RGB(71, 63, 77));
    if (sample && sample->frames) {
        for (int x = 0; x < 600; ++x) {
            size_t begin = (size_t)x * sample->frames / 600u;
            size_t end = (size_t)(x + 1) * sample->frames / 600u;
            if (end <= begin) end = begin + 1;
            float low = 1.0f, high = -1.0f;
            for (size_t i = begin; i < end && i < sample->frames; ++i) {
                if (sample->data[i] < low) low = sample->data[i];
                if (sample->data[i] > high) high = sample->data[i];
            }
            int y0 = 146 - (int)(high * 68.0f);
            int y1 = 146 - (int)(low * 68.0f);
            line(fb, 20 + x, y0, 20 + x, y1, purple);
        }
    } else {
        text(fb, 211, 139, "DROP WAV HERE", RGB(101, 103, 114), 2);
    }

    button(fb, 10, 254, 86, "LOAD WAV", ui->path_entry);
    button(fb, 102, 254, 96, "GENERATE", 0);
    button(fb, 204, 254, 82, "RESEED", 0);
    button(fb, 544, 254, 86, "STOP ALL", 0);
    slider(fb, 304, 254, 62, "BODY", ui->recipe.body);
    slider(fb, 383, 254, 62, "EDGE", ui->recipe.edge);
    slider(fb, 462, 254, 62, "DRIFT", ui->recipe.drift);

    text(fb, 11, 290, "PLAY: Z S X D C V G B H N J M   UPPER OCTAVE: Q 2 W 3 E R 5 T 6 Y 7 U", RGB(165, 166, 175), 1);
    const int white_x = 10, white_y = 310, white_w = 43, white_h = 69;
    const char *labels[14] = {"C","D","E","F","G","A","B","C","D","E","F","G","A","B"};
    for (int i = 0; i < 14; ++i) {
        int active = ui->active_key == i;
        rect(fb, white_x + i * white_w, white_y, white_w - 1, white_h,
             active ? RGB(226, 164, 104) : RGB(218, 212, 195));
        text(fb, white_x + i * white_w + 23, white_y + 54, labels[i], RGB(35, 36, 43), 1);
    }
    const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    for (int i = 0; i < 10; ++i) {
        int key = semitones[i];
        int active = ui->active_key == key;
        rect(fb, white_x + (black_after[i] + 1) * white_w - 16, white_y, 31, 42,
             active ? RGB(172, 92, 119) : RGB(31, 32, 39));
    }
    rect(fb, 0, 386, TS_UI_WIDTH, 14, RGB(25, 26, 32));
    text(fb, 8, 389, ui->status, RGB(194, 188, 174), 1);

    if (ui->path_entry) {
        frame(fb, 58, 126, 524, 88, RGB(55, 57, 69), RGB(219, 161, 98));
        text(fb, 72, 140, "LOAD WAV PATH", RGB(238, 225, 204), 1);
        rect(fb, 72, 158, 496, 25, RGB(19, 20, 25));
        const char *shown = ui->path[0] ? ui->path : "TYPE PATH OR DROP FILE";
        size_t length = strlen(shown);
        if (length > 80) shown += length - 80;
        text(fb, 78, 167, shown, RGB(202, 198, 188), 1);
        text(fb, 72, 192, "ENTER LOADS   ESC CANCELS", RGB(151, 153, 164), 1);
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
    if (x < 10 || x >= 622 || y < 310 || y >= 379) return -1;
    const int white_w = 43;
    const int black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
    const int semitones[] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    if (y < 352) {
        for (int i = 0; i < 10; ++i) {
            int left = 10 + (black_after[i] + 1) * white_w - 16;
            if (x >= left && x < left + 31) return semitones[i];
        }
    }
    static const int white_semitones[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
    int index = (x - 10) / white_w;
    return index >= 0 && index < 14 ? white_semitones[index] : -1;
}
