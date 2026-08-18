from pathlib import Path
import re


def load(path):
    return Path(path).read_text()


def save(path, text):
    Path(path).write_text(text)


def must_replace(text, old, new, label):
    if old not in text:
        raise SystemExit(f"missing replacement anchor: {label}")
    return text.replace(old, new, 1)


def must_regex(text, pattern, repl, label, flags=0):
    new, count = re.subn(pattern, repl, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"regex replacement failed ({count}): {label}")
    return new

# ---------------------------------------------------------------------------
# Config contract
# ---------------------------------------------------------------------------
p = "include/tapesister/config.h"
s = load(p)
s = must_replace(
    s,
    "    char cdp_bin_path[TS_CONFIG_PATH_MAX];\n    char record_input_device[TS_CONFIG_PATH_MAX];",
    "    char cdp_bin_path[TS_CONFIG_PATH_MAX];\n    char record_input_device[TS_CONFIG_PATH_MAX];\n    char audio_output_device[TS_CONFIG_PATH_MAX];",
    "config output field")
save(p, s)

p = "src/ts_config.c"
s = load(p)
s = must_replace(
    s,
    "        } else if (strcmp(key, \"record_input_device\") == 0) {\n            if (!copy_value(loaded.record_input_device, value, error, error_size)) { fclose(file); return 0; }",
    "        } else if (strcmp(key, \"record_input_device\") == 0) {\n            if (!copy_value(loaded.record_input_device, value, error, error_size)) { fclose(file); return 0; }\n        } else if (strcmp(key, \"audio_output_device\") == 0) {\n            if (!copy_value(loaded.audio_output_device, value, error, error_size)) { fclose(file); return 0; }",
    "parse output device")
s = must_replace(
    s,
    "                \"chain_stamp_crossfade_ms=%d\\n\"\n                \"\\n[External Recording]\\n\"",
    "                \"chain_stamp_crossfade_ms=%d\\n\"\n                \"\\n[Audio]\\n\"\n                \"; Blank uses the operating system default playback device. Otherwise use the exact SDL device name.\\n\"\n                \"audio_output_device=%s\\n\"\n                \"\\n[External Recording]\\n\"",
    "save audio section")
s = must_replace(
    s,
    "                config->drone_crossfade_ms,\n                config->chain_stamp_crossfade_ms,\n                config->record_input_device,",
    "                config->drone_crossfade_ms,\n                config->chain_stamp_crossfade_ms,\n                config->audio_output_device,\n                config->record_input_device,",
    "save output argument")
save(p, s)

p = "tapesister.ini.example"
s = load(p)
anchor = "\n[External Recording]\n"
if "[Audio]" not in s:
    s = must_replace(
        s, anchor,
        "\n[Audio]\n; Blank uses the system default stereo playback device.\naudio_output_device=\n" + anchor,
        "ini audio section")
save(p, s)

# ---------------------------------------------------------------------------
# UI contract and compact Config layout
# ---------------------------------------------------------------------------
p = "include/tapesister/ui.h"
s = load(p)
s = must_replace(
    s,
    "enum { TS_CONFIG_FIELD_X = 20, TS_CONFIG_FIELD_Y = 63,\n       TS_CONFIG_FIELD_W = 600, TS_CONFIG_FIELD_H = 19,\n       TS_CONFIG_FIELD_STEP_Y = 28 };",
    "enum { TS_CONFIG_FIELD_X = 20, TS_CONFIG_FIELD_Y = 58,\n       TS_CONFIG_FIELD_W = 600, TS_CONFIG_FIELD_H = 16,\n       TS_CONFIG_FIELD_STEP_Y = 19,\n       TS_CONFIG_VALUE_X = 188, TS_CONFIG_VALUE_W = 428,\n       TS_CONFIG_AUDIO_INPUT_Y = 136, TS_CONFIG_AUDIO_OUTPUT_Y = 155,\n       TS_CONFIG_AUDIO_ROW_H = 16, TS_CONFIG_AUDIO_INPUT_W = 430,\n       TS_CONFIG_AUDIO_CHANNEL_X = 455, TS_CONFIG_AUDIO_CHANNEL_W = 165 };",
    "compact config geometry")
s = must_replace(
    s,
    "typedef enum {\n    TS_UI_CONFIG_ACTION_NONE = 0,\n    TS_UI_CONFIG_ACTION_SAVE,\n    TS_UI_CONFIG_ACTION_USE_CWD,\n    TS_UI_CONFIG_ACTION_PALETTE,\n    TS_UI_CONFIG_ACTION_CANCEL\n} TsUiConfigAction;",
    "typedef enum {\n    TS_UI_CONFIG_ACTION_NONE = 0,\n    TS_UI_CONFIG_ACTION_SAVE,\n    TS_UI_CONFIG_ACTION_USE_CWD,\n    TS_UI_CONFIG_ACTION_PALETTE,\n    TS_UI_CONFIG_ACTION_CANCEL\n} TsUiConfigAction;\n\ntypedef enum {\n    TS_UI_CONFIG_AUDIO_NONE = 0,\n    TS_UI_CONFIG_AUDIO_INPUT_DEVICE,\n    TS_UI_CONFIG_AUDIO_INPUT_CHANNEL,\n    TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE\n} TsUiConfigAudioControl;",
    "config audio enum")
s = must_replace(
    s,
    "    TsConfigField config_field;\n    size_t config_cursor;",
    "    TsConfigField config_field;\n    TsUiConfigAudioControl config_audio_control;\n    size_t config_cursor;",
    "config audio focus state")
s = must_replace(
    s,
    "int ts_ui_config_field_from_point(int x, int y);\nsize_t ts_ui_config_cursor_from_point",
    "int ts_ui_config_field_from_point(int x, int y);\nTsUiConfigAudioControl ts_ui_config_audio_control_from_point(int x, int y);\nsize_t ts_ui_config_cursor_from_point",
    "config audio hit prototype")
save(p, s)

p = "src/ts_ui.c"
s = load(p)
# Initialize audio selector focus.
s = must_replace(
    s,
    "    ui->capture_state = TS_CAPTURE_IDLE;\n    ui->external_record_bank = 0;",
    "    ui->capture_state = TS_CAPTURE_IDLE;\n    ui->external_record_bank = 0;\n    ui->config_audio_control = TS_UI_CONFIG_AUDIO_NONE;",
    "ui config audio init")

# Replace Config renderer as one compact modal that keeps y>=205 untouched.
pattern = r"static void config_render\(TsFramebuffer \*fb, const TsUiState \*ui\)\n\{.*?\n\}\n\nstatic void palette_render"
repl = r'''static void config_render(TsFramebuffer *fb, const TsUiState *ui)
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
        if (length > visible) {
            first = cursor > visible ? cursor - visible : length - visible;
            if (cursor < first) first = cursor;
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
        snprintf(channel, sizeof(channel), ui->config.record_input_channel == 0 ?
                 "CH < MIX >" : "CH < %d >", ui->config.record_input_channel);
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
}

static void palette_render'''
s = must_regex(s, pattern, repl, "config renderer", flags=re.S)

# Add audio row hit testing directly after path hit testing.
marker = "size_t ts_ui_config_cursor_from_point(const TsUiState *ui,\n                                      TsConfigField field, int x)"
audio_hit = '''TsUiConfigAudioControl ts_ui_config_audio_control_from_point(int x, int y)
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
s = must_replace(s, marker, audio_hit + marker, "audio hit test")

# Cursor maps only inside the compact value half of a path row.
pattern = r"size_t ts_ui_config_cursor_from_point\(const TsUiState \*ui,\n                                      TsConfigField field, int x\)\n\{.*?\n\}"
repl = r'''size_t ts_ui_config_cursor_from_point(const TsUiState *ui,
                                      TsConfigField field, int x)
{
    const char *value = ts_config_field_const(&ui->config, field);
    size_t length = value != NULL ? strlen(value) : 0u;
    size_t cursor = field == ui->config_field ? ui->config_cursor : length;
    size_t visible = (size_t)((TS_CONFIG_VALUE_W - 8) / 6);
    size_t first = 0u;
    size_t clicked;
    if (length > visible) {
        first = cursor > visible ? cursor - visible : length - visible;
        if (cursor < first) first = cursor;
    }
    if (x <= TS_CONFIG_VALUE_X) return first;
    clicked = first + (size_t)((x - TS_CONFIG_VALUE_X + 3) / 6);
    return clicked > length ? length : clicked;
}'''
s = must_regex(s, pattern, repl, "config cursor mapping", flags=re.S)
save(p, s)

# ---------------------------------------------------------------------------
# Main SDL: heap-backed device catalog, output switching, config interactions
# ---------------------------------------------------------------------------
p = "src/main_sdl.c"
s = load(p)

# Insert device catalog after AudioState. Forward-declare playback callback so the
# output open helper can stay next to its state type.
anchor = "} AudioState;\n\ntypedef struct {\n    SDL_Thread *thread;"
catalog_code = r'''} AudioState;

static void audio_callback(void *userdata, Uint8 *stream, int bytes);

typedef struct {
    char **input_names;
    int input_count;
    char **output_names;
    int output_count;
} AudioDeviceCatalog;

static char *audio_device_name_copy(const char *name)
{
    size_t length;
    char *copy;
    if (name == NULL) return NULL;
    length = strlen(name);
    copy = (char *)malloc(length + 1u);
    if (copy != NULL) memcpy(copy, name, length + 1u);
    return copy;
}

static void audio_device_names_free(char ***names, int *count)
{
    if (names != NULL && *names != NULL) {
        for (int i = 0; i < *count; ++i) free((*names)[i]);
        free(*names);
        *names = NULL;
    }
    if (count != NULL) *count = 0;
}

static void audio_device_catalog_free(AudioDeviceCatalog *catalog)
{
    if (catalog == NULL) return;
    audio_device_names_free(&catalog->input_names, &catalog->input_count);
    audio_device_names_free(&catalog->output_names, &catalog->output_count);
}

static int audio_device_names_refresh(int capture, char ***names, int *count)
{
    int available = SDL_GetNumAudioDevices(capture);
    char **made = NULL;
    int kept = 0;
    if (available < 0) available = 0;
    if (available > 0) {
        made = (char **)calloc((size_t)available, sizeof(*made));
        if (made == NULL) return 0;
        for (int i = 0; i < available; ++i) {
            const char *name = SDL_GetAudioDeviceName(i, capture);
            if (name == NULL || name[0] == '\0') continue;
            made[kept] = audio_device_name_copy(name);
            if (made[kept] == NULL) {
                for (int j = 0; j < kept; ++j) free(made[j]);
                free(made);
                return 0;
            }
            ++kept;
        }
    }
    audio_device_names_free(names, count);
    *names = made;
    *count = kept;
    return 1;
}

static void refresh_audio_device_catalog(AudioDeviceCatalog *catalog)
{
    if (catalog == NULL) return;
    if (!audio_device_names_refresh(1, &catalog->input_names, &catalog->input_count))
        diagnostic_log("could not refresh SDL capture-device catalog");
    if (!audio_device_names_refresh(0, &catalog->output_names, &catalog->output_count))
        diagnostic_log("could not refresh SDL playback-device catalog");
    diagnostic_log("audio catalog refreshed: inputs=%d outputs=%d",
                   catalog->input_count, catalog->output_count);
}

static int audio_device_config_index(char **names, int count, const char *configured)
{
    if (configured == NULL || configured[0] == '\0') return 0;
    for (int i = 0; i < count; ++i)
        if (strcmp(names[i], configured) == 0) return i + 1;
    return -1;
}

static void cycle_config_device(TsUiState *ui, AudioDeviceCatalog *catalog,
                                int capture, int direction)
{
    char **names = capture ? catalog->input_names : catalog->output_names;
    int count = capture ? catalog->input_count : catalog->output_count;
    char *configured = capture ? ui->config.record_input_device :
                                 ui->config.audio_output_device;
    int index = audio_device_config_index(names, count, configured);
    int positions = count + 1;
    if (positions <= 1) index = 0;
    else {
        if (index < 0) index = 0;
        index = (index + (direction >= 0 ? 1 : positions - 1)) % positions;
    }
    if (index == 0) configured[0] = '\0';
    else snprintf(configured, TS_CONFIG_PATH_MAX, "%s", names[index - 1]);
    snprintf(ui->status, sizeof(ui->status), "%s DEVICE: %.118s",
             capture ? "INPUT" : "OUTPUT",
             configured[0] != '\0' ? configured : "SYSTEM DEFAULT");
}

static void cycle_config_input_channel(TsUiState *ui, int direction)
{
    int value = ui->config.record_input_channel;
    if (value < 0 || value > 2) value = 0;
    value = (value + (direction >= 0 ? 1 : 2)) % 3;
    ui->config.record_input_channel = value;
    snprintf(ui->status, sizeof(ui->status), value == 0 ?
             "INPUT CHANNEL: MIX TO MONO" : "INPUT CHANNEL: %d", value);
}

static void select_config_audio_control(TsUiState *ui,
                                        TsUiConfigAudioControl control)
{
    ui->config_audio_control = control;
    SDL_StopTextInput();
}

static SDL_AudioDeviceID open_playback_device(const char *configured,
                                              AudioState *audio,
                                              SDL_AudioSpec *obtained,
                                              char *error, size_t error_size)
{
    SDL_AudioSpec desired;
    SDL_AudioDeviceID opened;
    const char *name = configured != NULL && configured[0] != '\0' ? configured : NULL;
    SDL_zero(desired);
    SDL_zero(*obtained);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = audio_callback;
    desired.userdata = audio;
    opened = SDL_OpenAudioDevice(name, 0, &desired, obtained, 0);
    if (opened == 0) {
        snprintf(error, error_size, "%.140s", SDL_GetError());
        return 0;
    }
    if (obtained->format != AUDIO_F32SYS || obtained->channels != 2 || obtained->freq <= 0) {
        snprintf(error, error_size, "Playback device returned unsupported format");
        SDL_CloseAudioDevice(opened);
        return 0;
    }
    error[0] = '\0';
    return opened;
}

typedef struct {
    SDL_Thread *thread;'''
s = must_replace(s, anchor, catalog_code, "audio catalog insertion")

# Path selection and Config opening now manage selector focus and refresh the SDL catalog.
s = must_replace(
    s,
    "    ui->config_field = field;\n    value = ts_config_field_const(&ui->config, field);",
    "    ui->config_field = field;\n    ui->config_audio_control = TS_UI_CONFIG_AUDIO_NONE;\n    SDL_StartTextInput();\n    value = ts_config_field_const(&ui->config, field);",
    "path selection clears audio focus")
s = must_replace(
    s,
    "static void begin_config(TsUiState *ui)\n{\n    ui->config_before_edit = ui->config;\n    ui->config_open = 1;\n    select_config_field(ui, TS_CONFIG_SAMPLE_PATH);\n    SDL_StartTextInput();\n    snprintf(ui->status, sizeof(ui->status), \"EDITING TAPESISTER PATHS\");\n}",
    "static void begin_config(TsUiState *ui, AudioDeviceCatalog *catalog)\n{\n    ui->config_before_edit = ui->config;\n    ui->config_open = 1;\n    ui->config_audio_control = TS_UI_CONFIG_AUDIO_NONE;\n    refresh_audio_device_catalog(catalog);\n    select_config_field(ui, TS_CONFIG_SAMPLE_PATH);\n    snprintf(ui->status, sizeof(ui->status), \"CONFIG: PATHS + AUDIO DEVICES REFRESHED\");\n}",
    "begin config refresh")

# Turn the old save helper into a file/UI commit only; runtime audio changes are
# applied by a wrapper after ExternalInputState is defined.
s = must_replace(s, "static void save_config(TsUiState *ui)", "static int save_config_file(TsUiState *ui)", "save helper return")
s = must_replace(
    s,
    "        snprintf(ui->status, sizeof(ui->status), \"CONFIG SAVE FAILED: %.130s\", error);\n        return;\n    }\n    ui->config_before_edit = ui->config;",
    "        snprintf(ui->status, sizeof(ui->status), \"CONFIG SAVE FAILED: %.130s\", error);\n        return 0;\n    }",
    "save helper failure and old snapshot")
s = must_replace(
    s,
    "    snprintf(ui->status, sizeof(ui->status), \"SAVED CONFIG %.112s\", config_file_path());\n}\n\nstatic void config_use_cwd",
    "    snprintf(ui->status, sizeof(ui->status), \"SAVED CONFIG %.112s\", config_file_path());\n    return 1;\n}\n\nstatic void config_use_cwd",
    "save helper success")

# Put runtime save/apply wrapper immediately after ExternalInputState declaration.
external_anchor = "} ExternalInputState;\n\nstatic int external_capture_busy"
wrapper = r'''} ExternalInputState;

static int switch_playback_device(SDL_AudioDeviceID *device, AudioState *audio,
                                  SDL_AudioSpec *obtained,
                                  const char *configured,
                                  TsUiState *ui)
{
    SDL_AudioSpec candidate_spec;
    SDL_AudioDeviceID candidate;
    char error[160];
    candidate = open_playback_device(configured, audio, &candidate_spec,
                                     error, sizeof(error));
    if (candidate == 0) {
        snprintf(ui->status, sizeof(ui->status),
                 "CONFIG SAVED - OUTPUT %.70s UNAVAILABLE; KEPT PREVIOUS: %.50s",
                 configured != NULL && configured[0] != '\0' ? configured : "SYSTEM DEFAULT",
                 error);
        diagnostic_log("runtime output switch failed nonfatally: %s", error);
        return 0;
    }
    SDL_PauseAudioDevice(candidate, 1);
    stop_all_force(*device, audio, ui);
    if (*device) {
        SDL_PauseAudioDevice(*device, 1);
        SDL_CloseAudioDevice(*device);
    }
    *device = candidate;
    *obtained = candidate_spec;
    audio->output_rate = candidate_spec.freq;
    SDL_PauseAudioDevice(candidate, 0);
    snprintf(ui->status, sizeof(ui->status), "SAVED CONFIG - OUTPUT: %.110s",
             configured != NULL && configured[0] != '\0' ? configured : "SYSTEM DEFAULT");
    diagnostic_log("runtime output switched: %s rate=%d",
                   configured != NULL && configured[0] != '\0' ? configured : "SYSTEM DEFAULT",
                   candidate_spec.freq);
    return 1;
}

static void save_config_and_apply_audio(TsUiState *ui,
                                        SDL_AudioDeviceID *device,
                                        SDL_AudioSpec *obtained,
                                        AudioState *audio,
                                        SDL_AudioDeviceID *input_device,
                                        ExternalInputState *external_input)
{
    int output_changed = strcmp(ui->config.audio_output_device,
                                ui->config_before_edit.audio_output_device) != 0;
    int input_changed = strcmp(ui->config.record_input_device,
                               ui->config_before_edit.record_input_device) != 0 ||
                        ui->config.record_input_channel !=
                        ui->config_before_edit.record_input_channel;
    if (!save_config_file(ui)) return;
    if (input_changed && input_device != NULL && *input_device != 0) {
        SDL_PauseAudioDevice(*input_device, 1);
        SDL_CloseAudioDevice(*input_device);
        *input_device = 0;
        external_input->channels = 0;
        external_input->input_channel = 0;
        external_input->sample_rate = 0u;
        external_input->device_label[0] = '\0';
        diagnostic_log("closed cached capture device after Config input change");
    }
    if (output_changed || *device == 0)
        (void)switch_playback_device(device, audio, obtained,
                                     ui->config.audio_output_device, ui);
    else if (input_changed)
        snprintf(ui->status, sizeof(ui->status),
                 "SAVED CONFIG - INPUT: %.108s  CH %d",
                 ui->config.record_input_device[0] != '\0' ?
                 ui->config.record_input_device : "SYSTEM DEFAULT",
                 ui->config.record_input_channel);
    ui->config_before_edit = ui->config;
}

static int external_capture_busy'''
s = must_replace(s, external_anchor, wrapper, "save/apply audio wrapper")

# Main locals: keep the catalog tiny (four machine words) and its names heap-backed.
s = must_replace(
    s,
    "    AudioState audio = {0};\n    ExternalInputState external_input = {0};",
    "    AudioState audio = {0};\n    AudioDeviceCatalog audio_devices = {0};\n    ExternalInputState external_input = {0};",
    "main audio catalog local")

# Startup output selection with exact configured device, safe fallback to default.
pattern = r'''    SDL_zero\(desired\);\n    SDL_zero\(obtained\);\n    desired\.freq = 48000;\n    desired\.format = AUDIO_F32SYS;\n    desired\.channels = 2;\n    desired\.samples = 512;\n    desired\.callback = audio_callback;\n    desired\.userdata = &audio;\n    device = SDL_OpenAudioDevice\(NULL, 0, &desired, &obtained, 0\);\n    audio\.output_rate = obtained\.freq;\n    if \(!device\) snprintf\(ui\.status, sizeof\(ui\.status\), "AUDIO UNAVAILABLE: %.130s", SDL_GetError\(\)\);\n    else SDL_PauseAudioDevice\(device, 0\);'''
repl = r'''    {
        char output_error[160];
        const char *requested = ui.config.audio_output_device;
        device = open_playback_device(requested, &audio, &obtained,
                                      output_error, sizeof(output_error));
        if (device == 0 && requested[0] != '\0') {
            char requested_error[160];
            snprintf(requested_error, sizeof(requested_error), "%.159s", output_error);
            device = open_playback_device("", &audio, &obtained,
                                          output_error, sizeof(output_error));
            if (device != 0) {
                snprintf(ui.status, sizeof(ui.status),
                         "OUTPUT %.70s UNAVAILABLE - USING SYSTEM DEFAULT",
                         requested);
                diagnostic_log("configured output unavailable (%s); default opened",
                               requested_error);
            }
        }
        audio.output_rate = device != 0 ? obtained.freq : 0;
        if (!device)
            snprintf(ui.status, sizeof(ui.status), "AUDIO UNAVAILABLE: %.130s", output_error);
        else SDL_PauseAudioDevice(device, 0);
    }'''
s = must_regex(s, pattern, repl, "startup output selection")
# desired no longer needed in main declaration.
s = must_replace(s, "    SDL_AudioSpec desired, obtained;", "    SDL_AudioSpec obtained;", "remove main desired spec")

# Config text typing is disabled while an audio selector owns focus.
s = must_replace(
    s,
    "            } else if (event.type == SDL_TEXTINPUT && ui.config_open &&\n                       !ui.exit_confirm_open) {",
    "            } else if (event.type == SDL_TEXTINPUT && ui.config_open &&\n                       ui.config_audio_control == TS_UI_CONFIG_AUDIO_NONE &&\n                       !ui.exit_confirm_open) {",
    "config text focus guard")

# Keyboard Config behavior: left/right cycles selected audio control; Enter saves/apply.
s = must_replace(
    s,
    "                    if (key == SDLK_ESCAPE) cancel_config(&ui);\n                    else if ((mod & KMOD_CTRL) && key == SDLK_BACKSPACE && field != NULL) {",
    "                    if (key == SDLK_ESCAPE) cancel_config(&ui);\n                    else if (ui.config_audio_control != TS_UI_CONFIG_AUDIO_NONE &&\n                             (key == SDLK_LEFT || key == SDLK_RIGHT)) {\n                        int direction = key == SDLK_RIGHT ? 1 : -1;\n                        if (ui.config_audio_control == TS_UI_CONFIG_AUDIO_INPUT_DEVICE)\n                            cycle_config_device(&ui, &audio_devices, 1, direction);\n                        else if (ui.config_audio_control == TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE)\n                            cycle_config_device(&ui, &audio_devices, 0, direction);\n                        else cycle_config_input_channel(&ui, direction);\n                    }\n                    else if ((mod & KMOD_CTRL) && key == SDLK_BACKSPACE && field != NULL) {",
    "config keyboard audio cycling")
s = must_replace(
    s,
    "                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)\n                        save_config(&ui);",
    "                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)\n                        save_config_and_apply_audio(&ui, &device, &obtained, &audio,\n                                                    &input_device, &external_input);",
    "config keyboard save apply")

# Mouse wheel over audio rows cycles devices/channel; elsewhere Config remains modal.
wheel_anchor = "            } else if (event.type == SDL_MOUSEWHEEL &&\n                       (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||\n                        ui.config_open || ui.export_choice_open ||"
wheel_insert = '''            } else if (event.type == SDL_MOUSEWHEEL && ui.config_open) {
                int raw_x, raw_y, x, y;
                TsUiConfigAudioControl control;
                SDL_GetMouseState(&raw_x, &raw_y);
                logical_mouse(window, raw_x, raw_y, &x, &y);
                control = ts_ui_config_audio_control_from_point(x, y);
                if (control != TS_UI_CONFIG_AUDIO_NONE) {
                    int direction = event.wheel.y >= 0 ? 1 : -1;
                    select_config_audio_control(&ui, control);
                    if (control == TS_UI_CONFIG_AUDIO_INPUT_DEVICE)
                        cycle_config_device(&ui, &audio_devices, 1, direction);
                    else if (control == TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE)
                        cycle_config_device(&ui, &audio_devices, 0, direction);
                    else cycle_config_input_channel(&ui, direction);
                } else snprintf(ui.status, sizeof(ui.status),
                                "HOVER INPUT / CH / OUTPUT TO CYCLE AUDIO DEVICE");
''' + wheel_anchor
s = must_replace(s, wheel_anchor, wheel_insert, "config audio wheel")

# Mouse Config branch: path fields still edit/browse; audio rows cycle by click side.
old = '''                } else if (ui.config_open) {
                    int selected = ts_ui_config_field_from_point(x, y);
                    TsUiConfigAction action = ts_ui_config_action_from_point(x, y);
                    if (selected >= 0) {
                        if (selected != (int)ui.config_field)
                            select_config_field(&ui, (TsConfigField)selected);
                        ui.config_cursor = ts_ui_config_cursor_from_point(
                                               &ui, ui.config_field, x);
                        if (event.button.clicks >= 2)
                            browser_open_config_path(&ui, ui.config_field);
                    } else if (action == TS_UI_CONFIG_ACTION_SAVE)
                        save_config(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_USE_CWD)
                        config_use_cwd(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_PALETTE)
                        begin_palette(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_CANCEL)
                        cancel_config(&ui);'''
new = '''                } else if (ui.config_open) {
                    int selected = ts_ui_config_field_from_point(x, y);
                    TsUiConfigAudioControl audio_control =
                        ts_ui_config_audio_control_from_point(x, y);
                    TsUiConfigAction action = ts_ui_config_action_from_point(x, y);
                    if (selected >= 0) {
                        if (selected != (int)ui.config_field ||
                            ui.config_audio_control != TS_UI_CONFIG_AUDIO_NONE)
                            select_config_field(&ui, (TsConfigField)selected);
                        ui.config_cursor = ts_ui_config_cursor_from_point(
                                               &ui, ui.config_field, x);
                        if (event.button.clicks >= 2)
                            browser_open_config_path(&ui, ui.config_field);
                    } else if (audio_control != TS_UI_CONFIG_AUDIO_NONE) {
                        int split = audio_control == TS_UI_CONFIG_AUDIO_INPUT_DEVICE ?
                                    20 + TS_CONFIG_AUDIO_INPUT_W / 2 :
                                    audio_control == TS_UI_CONFIG_AUDIO_INPUT_CHANNEL ?
                                    TS_CONFIG_AUDIO_CHANNEL_X + TS_CONFIG_AUDIO_CHANNEL_W / 2 :
                                    320;
                        int direction = x >= split ? 1 : -1;
                        select_config_audio_control(&ui, audio_control);
                        if (audio_control == TS_UI_CONFIG_AUDIO_INPUT_DEVICE)
                            cycle_config_device(&ui, &audio_devices, 1, direction);
                        else if (audio_control == TS_UI_CONFIG_AUDIO_OUTPUT_DEVICE)
                            cycle_config_device(&ui, &audio_devices, 0, direction);
                        else cycle_config_input_channel(&ui, direction);
                    } else if (action == TS_UI_CONFIG_ACTION_SAVE)
                        save_config_and_apply_audio(&ui, &device, &obtained, &audio,
                                                    &input_device, &external_input);
                    else if (action == TS_UI_CONFIG_ACTION_USE_CWD)
                        config_use_cwd(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_PALETTE)
                        begin_palette(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_CANCEL)
                        cancel_config(&ui);'''
s = must_replace(s, old, new, "config mouse audio selectors")

# Config opening refreshes lists and is refused while either recorder is active.
s = must_replace(
    s,
    "                } else if (y >= 4 && y < 28 && x >= 350 && x < 426) {\n                    begin_config(&ui);",
    "                } else if (y >= 4 && y < 28 && x >= 350 && x < 426) {\n                    if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||\n                        audio.capture.state == TS_CAPTURE_RECORDING ||\n                        external_capture_busy(&external_input))\n                        snprintf(ui.status, sizeof(ui.status),\n                                 \"STOP OR CANCEL RECORDING BEFORE CONFIG\");\n                    else begin_config(&ui, &audio_devices);",
    "config open recorder guard")

# Shutdown catalog after devices close.
s = must_replace(
    s,
    "    ts_external_recorder_free(&external_input.recorder);\n    ts_capture_free(&audio.capture);",
    "    ts_external_recorder_free(&external_input.recorder);\n    audio_device_catalog_free(&audio_devices);\n    ts_capture_free(&audio.capture);",
    "catalog shutdown")
save(p, s)

# ---------------------------------------------------------------------------
# Tests and docs
# ---------------------------------------------------------------------------
p = "tests/test_external_record.c"
s = load(p)
insert = r'''
static void test_audio_device_config_roundtrip(void)
{
    static const char path[] = "test-external-audio-config.ini";
    static const char old_path[] = "test-external-audio-old.ini";
    TsConfig saved;
    TsConfig loaded;
    char error[160];
    FILE *file;
    ts_config_init(&saved);
    CHECK(saved.record_input_device[0] == '\0');
    CHECK(saved.audio_output_device[0] == '\0');
    CHECK(saved.record_input_channel == TS_RECORD_INPUT_CHANNEL_DEFAULT);
    snprintf(saved.record_input_device, sizeof(saved.record_input_device),
             "Test Capture Device");
    snprintf(saved.audio_output_device, sizeof(saved.audio_output_device),
             "Test Playback Device");
    saved.record_input_channel = 2;
    CHECK(ts_config_save(&saved, path, error, sizeof(error)));
    ts_config_init(&loaded);
    CHECK(ts_config_load(&loaded, path, error, sizeof(error)));
    CHECK(strcmp(loaded.record_input_device, "Test Capture Device") == 0);
    CHECK(strcmp(loaded.audio_output_device, "Test Playback Device") == 0);
    CHECK(loaded.record_input_channel == 2);
    remove(path);

    file = fopen(old_path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("[External Recording]\nrecord_input_device=\nrecord_input_channel=1\n", file);
        fclose(file);
        ts_config_init(&loaded);
        CHECK(ts_config_load(&loaded, old_path, error, sizeof(error)));
        CHECK(loaded.record_input_device[0] == '\0');
        CHECK(loaded.audio_output_device[0] == '\0');
        CHECK(loaded.record_input_channel == 1);
        remove(old_path);
    }
}
'''
s = must_replace(s, "\nint main(void)\n{", insert + "\nint main(void)\n{", "audio config test function")
s = must_replace(s, "    test_config_defaults();\n", "    test_config_defaults();\n    test_audio_device_config_roundtrip();\n", "run audio config test")
save(p, s)

p = "Makefile"
s = load(p)
s = must_replace(
    s,
    "tapesister_canvas_tests tapesister_capture_tests tapesister_transform_tests",
    "tapesister_canvas_tests tapesister_capture_tests tapesister_external_record_tests tapesister_transform_tests",
    "make test external target")
s = must_replace(
    s,
    "\t./tapesister_capture_tests\n\t./tapesister_transform_tests",
    "\t./tapesister_capture_tests\n\t./tapesister_external_record_tests\n\t./tapesister_transform_tests",
    "make test external run")
if "tapesister_external_record_tests:" not in s:
    s = must_replace(
        s,
        "tapesister_capture_tests: $(CORE) tests/test_capture.c\n\t$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm\n\n",
        "tapesister_capture_tests: $(CORE) tests/test_capture.c\n\t$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm\n\ntapesister_external_record_tests: $(CORE) tests/test_external_record.c\n\t$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm\n\n",
        "make external target definition")
s = s.replace("tapesister_capture_tests tapesister_transform_tests tapesister_chain_stamp_tests", "tapesister_capture_tests tapesister_external_record_tests tapesister_transform_tests tapesister_chain_stamp_tests")
save(p, s)

p = "README.md"
s = load(p)
old = "**Config** replaces only the framed waveform panel with compact blank-safe editable paths for the sample root, FastTracker executable, and FT2 exchange folder, leaving the toolbar and every control below `y=205` visible."
new = "**Config** replaces only the framed waveform panel with compact blank-safe editable paths plus SDL-enumerated **INPUT**, **INPUT CH**, and **OUTPUT** selectors, leaving the toolbar and every control below `y=205` visible. Device selectors always include **SYSTEM DEFAULT**; click their left/right halves, use Left/Right while selected, or hover and use the wheel to cycle currently detected devices. Input choice persists as `record_input_device`, channel as `record_input_channel`, and playback choice as `audio_output_device` in `tapesister.ini`. Saving an output change opens the new device before releasing the old working device, so a failed switch is nonfatal and keeps playback alive. A missing named output at startup falls back to system default while retaining the saved name; a missing explicitly named recording input never silently substitutes another source."
if old in s:
    s = s.replace(old, new, 1)
else:
    raise SystemExit("README Config anchor missing")
save(p, s)

print("PR35 audio input/output device Config patch applied")
