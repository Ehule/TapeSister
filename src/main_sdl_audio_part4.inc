
    if (ts_real_capture != 0 &&
        !ts_capture_name_matches_config()) {
        ts_capture_reconfigure_pending = 1;
        ts_capture_reconfigure_error[0] = '\0';
    }
    ts_mark_capture_config_after_save();
    return 1;
}

static void cfg_rect(TsFramebuffer *fb, int x, int y, int w, int h,
                     uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TS_UI_WIDTH) w = TS_UI_WIDTH - x;
    if (y + h > TS_UI_HEIGHT) h = TS_UI_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            fb->pixels[py * TS_UI_WIDTH + px] = color;
}

static const char *cfg_glyph(char c)
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
    case '_': return "00000000000000000000000000000011111";
    default: return "00000000000000000000000000000000000";
    }
}

static void cfg_text(TsFramebuffer *fb, int x, int y, const char *value,
                     uint32_t color)
{
    for (; *value != '\0'; ++value, x += 6) {
        char c = *value >= 'a' && *value <= 'z' ?
                 (char)(*value - ('a' - 'A')) : *value;
        const char *bits = cfg_glyph(c);
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx)
                if (bits[gy * 5 + gx] == '1')
                    cfg_rect(fb, x + gx, y + gy, 1, 1, color);
    }
}

static void cfg_frame(TsFramebuffer *fb, int x, int y, int w, int h,
                      uint32_t fill, uint32_t edge)
{
    cfg_rect(fb, x, y, w, h, fill);
    cfg_rect(fb, x, y, w, 1, edge);
    cfg_rect(fb, x, y, 1, h, edge);
    cfg_rect(fb, x, y + h - 1, w, 1, 0xff151316u);
    cfg_rect(fb, x + w - 1, y, 1, h, 0xff151316u);
}

static void ts_audio_render_config(TsFramebuffer *fb, const TsUiState *ui)
{
    uint32_t text_color = ui->palette.colors[TS_PALETTE_PATTERN_TEXT];
    uint32_t mouse_color = ui->palette.colors[TS_PALETTE_MOUSE];
    uint32_t effect_color = ui->palette.colors[TS_PALETTE_PATTERN_EFFECT];
    uint32_t instrument_color = ui->palette.colors[TS_PALETTE_PATTERN_INSTRUMENT];
    uint32_t panel = 0xff242125u;
    uint32_t row = 0xff1b191cu;
    uint32_t edge = 0xff48434au;

    cfg_frame(fb, 10, 40, 620, 153, panel, mouse_color);
    cfg_text(fb, 20, 45, "CONFIGURATION", instrument_color);
    cfg_text(fb, 320, 45, "AUDIO/MIDI < OR >   SAVE APPLIES", effect_color);

    for (int field = 0; field < TS_CONFIG_FIELD_COUNT; ++field) {
        const char *value = ts_config_field_const(&ui->config, (TsConfigField)field);
        size_t length = value != NULL ? strlen(value) : 0u;
        size_t cursor = field == (int)ui->config_field ?
                        ui->config_cursor : length;
        size_t visible = 69u;
        size_t first = 0u;
        char shown[72];
        int y = TS_AUDIO_CONFIG_PATH_Y + field * TS_AUDIO_CONFIG_PATH_STEP;
        if (cursor > length) cursor = length;
        if (length > visible) {
            first = length - visible;
            if (cursor < first) first = cursor;
            if (cursor > first + visible) first = cursor - visible;
        }
        snprintf(shown, sizeof(shown), "%.69s",
                 value != NULL ? value + first : "");
        cfg_frame(fb, 20, y, 600, TS_AUDIO_CONFIG_ROW_H, row,
