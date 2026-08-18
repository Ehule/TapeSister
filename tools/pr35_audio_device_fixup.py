from pathlib import Path
import subprocess


def replace_function(text, signature, replacement):
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"function signature not found: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"opening brace not found: {signature}")
    depth = 0
    in_string = False
    in_char = False
    escaped = False
    i = brace
    while i < len(text):
        ch = text[i]
        if escaped:
            escaped = False
        elif ch == "\\" and (in_string or in_char):
            escaped = True
        elif ch == '"' and not in_char:
            in_string = not in_string
        elif ch == "'" and not in_string:
            in_char = not in_char
        elif not in_string and not in_char:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[:start] + replacement + text[i + 1:]
        i += 1
    raise SystemExit(f"closing brace not found: {signature}")


baseline = subprocess.check_output(
    ["git", "show", "HEAD:src/ts_ui.c"], text=True
)

# Preserve every existing Drone/Transform helper by starting from the untouched
# branch version, then make only the four UI edits this feature actually needs.
ui = baseline
ui = ui.replace(
    "    ui->capture_state = TS_CAPTURE_IDLE;\n    ui->external_record_bank = 0;",
    "    ui->capture_state = TS_CAPTURE_IDLE;\n    ui->external_record_bank = 0;\n"
    "    ui->config_audio_control = TS_UI_CONFIG_AUDIO_NONE;",
    1,
)

config_render = r'''static void config_render(TsFramebuffer *fb, const TsUiState *ui)
{
    frame(fb, TS_MODAL_PANEL_X, TS_MODAL_PANEL_Y,
          TS_MODAL_PANEL_W, TS_MODAL_PANEL_H, RGB(36, 33, 37), PAL_MOUSE);
    text(fb, 20, 45, "CONFIGURATION", PAL_NOTE, 1);
    text(fb, 330, 45, "PATH TYPE/DBL CLICK   AUDIO < > / WHEEL", PAL_EFFECT, 1);
    for (int i = 0; i < TS_CONFIG_FIELD_COUNT; ++i) {
        const char *value = ts_config_field_const(&ui->config, (TsConfigField)i);
        size_t length = strlen(value);
        size_t cursor = i == (int)ui->config_field ? ui->config_cursor : length;
        size_t visible = (size_t)((TS_CONFIG_VALUE_W - 8) / 6);
        size_t first = 0u;
        char shown[96];
        int y = TS_CONFIG_FIELD_Y + i * TS_CONFIG_FIELD_STEP_Y;
        int active = ui->config_audio_control == TS_UI_CONFIG_AUDIO_NONE &&
                     i == (int)ui->config_field;
        if (cursor > length) cursor = length;
        if (length > visible) {
            first = length - visible;
            if (cursor < first) first = cursor;
            if (cursor > first + visible) first = cursor - visible;
        }
        snprintf(shown, sizeof(shown), "%.*s", (int)visible, value + first);
        frame(fb, TS_CONFIG_FIELD_X, y, TS_CONFIG_FIELD_W, TS_CONFIG_FIELD_H,
              RGB(27, 25, 28), active ? PAL_MOUSE : RGB(72, 67, 74));
        text(fb, 24, y + 5, ts_config_field_name((TsConfigField)i),
             active ? PAL_MOUSE : PAL_TEXT, 1);
        text(fb, TS_CONFIG_VALUE_X, y + 5, shown, PAL_EFFECT, 1);
        if (active && ui->text_cursor_visible && cursor >= first &&
            cursor - first <= visible) {
            int cx = TS_CONFIG_VALUE_X + (int)(cursor - first) * 6;
            rect(fb, cx, y + 3, 1, 11, PAL_MOUSE);
        }
    }
    {
        char input[84];
        char output[100];
        char channel[20];
        const char *input_name = ui->config.record_input_device[0] != '\0' ?
                                 ui->config.record_input_device : "SYSTEM DEFAULT";
        const char *output_name = ui->config.audio_output_device[0] != '\0' ?
                                  ui->config.audio_output_device : "SYSTEM DEFAULT";
        snprintf(input, sizeof(input), "INPUT  < %.58s >", input_name);
        snprintf(output, sizeof(output), "OUTPUT < %.78s >", output_name);
        if (ui->config.record_input_channel == 0)
            snprintf(channel, sizeof(channel), "CH < MIX >");
        else
            snprintf(channel, sizeof(channel), "CH < %d >",
                     ui->config.record_input_channel);
        frame(fb, 20, TS_CONFIG_AUDIO_INPUT_Y, TS_CONFIG_AUDIO_INPUT_W,
              TS_CONFIG_AUDIO_ROW_H, RGB(27, 25, 28),
              ui->config_audio_control == TS_UI_CONFIG_AUDIO_INPUT_DEVICE ?
              PAL_MOUSE : RGB(72, 67, 74));
        text(fb, 24, TS_CONFIG_AUDIO_INPUT_Y + 5, input,
             ui->config_audio_control == TS_UI_CONFIG_AUDIO_INPUT_DEVICE ?
             PAL_MOUSE : PAL_TEXT, 1);
        frame(fb, TS_CONFIG_AUDIO_CHANNEL_X, TS_CONFIG_AUDIO_INPUT_Y,
              TS_CONFIG_AUDIO_CHANNEL_W, TS_CONFIG_AUDIO_ROW_H,
              RGB(27, 25, 28),
              ui->config_audio_control == TS_UI_CONFIG_AUDIO_INPUT_CHANNEL ?
              PAL_MOUSE : RGB(72, 67, 74));
        text(fb, TS_CONFIG_AUDIO_CHANNEL_X + 5, TS_CONFIG_AUDIO_INPUT_Y + 5,
             channel, ui->config_audio_control == TS_UI_CONFIG_AUDIO_INPUT_CHANNEL ?
             PAL_MOUSE : PAL_TEXT, 1);
        frame(fb, 20, TS_CONFIG_AUDIO_OUTPUT_Y, 600, TS_CONFIG_AUDIO_ROW_H,
              RGB(27, 25, 28),
              ui->config_audio_control == TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE ?
              PAL_MOUSE : RGB(72, 67, 74));
        text(fb, 24, TS_CONFIG_AUDIO_OUTPUT_Y + 5, output,
             ui->config_audio_control == TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE ?
             PAL_MOUSE : PAL_TEXT, 1);
    }
    for (size_t i = 0; i < sizeof(config_buttons) / sizeof(config_buttons[0]); ++i)
        button(fb, config_buttons[i].x, TS_PALETTE_ACTION_Y,
               config_buttons[i].width, config_buttons[i].label, 0);
}'''
ui = replace_function(
    ui,
    "static void config_render(TsFramebuffer *fb, const TsUiState *ui)",
    config_render,
)

hit = r'''TsUiConfigAudioControl ts_ui_config_audio_control_from_point(int x, int y)
{
    if (y >= TS_CONFIG_AUDIO_INPUT_Y &&
        y < TS_CONFIG_AUDIO_INPUT_Y + TS_CONFIG_AUDIO_ROW_H) {
        if (x >= 20 && x < 20 + TS_CONFIG_AUDIO_INPUT_W)
            return TS_UI_CONFIG_AUDIO_INPUT_DEVICE;
        if (x >= TS_CONFIG_AUDIO_CHANNEL_X &&
            x < TS_CONFIG_AUDIO_CHANNEL_X + TS_CONFIG_AUDIO_CHANNEL_W)
            return TS_UI_CONFIG_AUDIO_INPUT_CHANNEL;
    }
    if (y >= TS_CONFIG_AUDIO_OUTPUT_Y &&
        y < TS_CONFIG_AUDIO_OUTPUT_Y + TS_CONFIG_AUDIO_ROW_H &&
        x >= 20 && x < 620)
        return TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE;
    return TS_UI_CONFIG_AUDIO_NONE;
}

'''
needle = "size_t ts_ui_config_cursor_from_point(const TsUiState *ui,\n                                      TsConfigField field, int x)"
if needle not in ui:
    raise SystemExit("config cursor function anchor missing")
ui = ui.replace(needle, hit + needle, 1)

cursor = r'''size_t ts_ui_config_cursor_from_point(const TsUiState *ui,
                                      TsConfigField field, int x)
{
    const char *value = ts_config_field_const(&ui->config, field);
    size_t length = value != NULL ? strlen(value) : 0u;
    size_t cursor = field == ui->config_field ? ui->config_cursor : length;
    size_t visible = (size_t)((TS_CONFIG_VALUE_W - 8) / 6);
    size_t first = 0u;
    size_t clicked;
    if (cursor > length) cursor = length;
    if (length > visible) {
        first = length - visible;
        if (cursor < first) first = cursor;
        if (cursor > first + visible) first = cursor - visible;
    }
    if (x <= TS_CONFIG_VALUE_X) return first;
    clicked = first + (size_t)((x - TS_CONFIG_VALUE_X + 3) / 6);
    return clicked > length ? length : clicked;
}'''
ui = replace_function(
    ui,
    "size_t ts_ui_config_cursor_from_point(const TsUiState *ui,\n                                      TsConfigField field, int x)",
    cursor,
)
Path("src/ts_ui.c").write_text(ui)

# Python regex replacement strings in the first-stage patcher turned textual \\0
# into literal NUL bytes in a few C character literals. Repair them explicitly.
main_path = Path("src/main_sdl.c")
main = main_path.read_text()
main = main.replace("'\x00'", "'\\0'")
main = main.replace(
    '"CONFIG SAVED - OUTPUT %.70s UNAVAILABLE; KEPT PREVIOUS: %.50s",',
    '"OUTPUT SWITCH FAILED - KEPT PREVIOUS: %.105s",',
)
# The shortened failure status only needs SDL's error text as its format argument.
main = main.replace(
    '                 configured != NULL && configured[0] != \'\\0\' ? configured : "SYSTEM DEFAULT",\n'
    '                 error);\n        diagnostic_log("runtime output switch failed nonfatally: %s", error);',
    '                 error);\n        diagnostic_log("runtime output switch failed nonfatally: %s", error);',
    1,
)
main_path.write_text(main)

print("PR35 audio Config UI fixup preserved Drone/Transform helpers and C literals")
