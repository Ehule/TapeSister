from pathlib import Path
import re


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def sub_once(text, pattern, replacement, label, flags=re.S):
    updated, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex match, found {count}")
    return updated


# ---------------------------------------------------------------------------
# Config: input device/channel plus the existing threshold feel settings.
# ---------------------------------------------------------------------------
path = "include/tapesister/config.h"
s = read(path)
s = replace_once(
    s,
    "    TS_RECORD_MAX_SECONDS_MIN = 1,\n    TS_RECORD_MAX_SECONDS_MAX = 600,\n    TS_RECORD_MAX_SECONDS_DEFAULT = 20\n};",
    "    TS_RECORD_MAX_SECONDS_MIN = 1,\n    TS_RECORD_MAX_SECONDS_MAX = 600,\n    TS_RECORD_MAX_SECONDS_DEFAULT = 20,\n    TS_RECORD_INPUT_CHANNEL_MIN = 0,\n    TS_RECORD_INPUT_CHANNEL_MAX = 2,\n    TS_RECORD_INPUT_CHANNEL_DEFAULT = 1\n};",
    "config channel constants")
s = replace_once(
    s,
    "    char exchange_path[TS_CONFIG_PATH_MAX];\n    char cdp_bin_path[TS_CONFIG_PATH_MAX];",
    "    char exchange_path[TS_CONFIG_PATH_MAX];\n    char cdp_bin_path[TS_CONFIG_PATH_MAX];\n    char record_input_device[TS_CONFIG_PATH_MAX];",
    "config device field")
s = replace_once(
    s,
    "    int chain_stamp_crossfade_ms;\n    int record_threshold_db;",
    "    int chain_stamp_crossfade_ms;\n    int record_input_channel;\n    int record_threshold_db;",
    "config channel field")
write(path, s)

path = "src/ts_config.c"
s = read(path)
s = replace_once(
    s,
    "        config->chain_stamp_crossfade_ms = TS_CHAIN_STAMP_CROSSFADE_MS_DEFAULT;\n        config->record_threshold_db = TS_RECORD_THRESHOLD_DB_DEFAULT;",
    "        config->chain_stamp_crossfade_ms = TS_CHAIN_STAMP_CROSSFADE_MS_DEFAULT;\n        config->record_input_channel = TS_RECORD_INPUT_CHANNEL_DEFAULT;\n        config->record_threshold_db = TS_RECORD_THRESHOLD_DB_DEFAULT;",
    "config channel init")
s = replace_once(
    s,
    "        } else if (strcmp(key, \"CdpBinPath\") == 0) {\n            if (!copy_value(loaded.cdp_bin_path, value, error, error_size)) { fclose(file); return 0; }\n        } else if (strcmp(key, \"startup_welcome_sample\") == 0) {",
    "        } else if (strcmp(key, \"CdpBinPath\") == 0) {\n            if (!copy_value(loaded.cdp_bin_path, value, error, error_size)) { fclose(file); return 0; }\n        } else if (strcmp(key, \"record_input_device\") == 0) {\n            if (!copy_value(loaded.record_input_device, value, error, error_size)) { fclose(file); return 0; }\n        } else if (strcmp(key, \"startup_welcome_sample\") == 0) {",
    "config device parse")
s = replace_once(
    s,
    "        } else if (strcmp(key, \"record_threshold_db\") == 0) {",
    "        } else if (strcmp(key, \"record_input_channel\") == 0) {\n            if (!parse_clamped_integer(value, TS_RECORD_INPUT_CHANNEL_MIN, TS_RECORD_INPUT_CHANNEL_MAX, &loaded.record_input_channel)) { snprintf(error, error_size, \"Invalid integer on config line %d\", line_number); fclose(file); return 0; }\n        } else if (strcmp(key, \"record_threshold_db\") == 0) {",
    "config channel parse")
s = replace_once(
    s,
    "                \"\\n[External Recording]\\n\"\n                \"; Threshold in dBFS; lower values trigger on quieter sounds.\\n\"\n                \"record_threshold_db=%d\\n\"",
    "                \"\\n[External Recording]\\n\"\n                \"; Blank uses the operating system default capture device. Otherwise use the exact SDL device name.\\n\"\n                \"record_input_device=%s\\n\"\n                \"; 0 mixes all input channels to mono, 1 records the first/left channel, 2 the second/right.\\n\"\n                \"record_input_channel=%d\\n\"\n                \"; Threshold in dBFS; lower values trigger on quieter sounds.\\n\"\n                \"record_threshold_db=%d\\n\"",
    "config save format")
s = replace_once(
    s,
    "                config->drone_crossfade_ms,\n                config->chain_stamp_crossfade_ms,\n                config->record_threshold_db,",
    "                config->drone_crossfade_ms,\n                config->chain_stamp_crossfade_ms,\n                config->record_input_device,\n                config->record_input_channel,\n                config->record_threshold_db,",
    "config save args")
write(path, s)

path = "tapesister.ini.example"
s = read(path)
s = replace_once(
    s,
    "[External Recording]\n; Threshold in dBFS. Lower values trigger on quieter sounds (-90 to 0).",
    "[External Recording]\n; Blank uses the machine's default recording device. To force an interface, copy its exact SDL device name here.\nrecord_input_device=\n; 0 = mix all channels to mono, 1 = first/left input, 2 = second/right input.\nrecord_input_channel=1\n; Threshold in dBFS. Lower values trigger on quieter sounds (-90 to 0).",
    "ini input settings")
write(path, s)

path = "tests/test_external_record.c"
s = read(path)
s = replace_once(
    s,
    "    CHECK(config.record_threshold_db == TS_RECORD_THRESHOLD_DB_DEFAULT);",
    "    CHECK(config.record_input_device[0] == '\\0');\n    CHECK(config.record_input_channel == TS_RECORD_INPUT_CHANNEL_DEFAULT);\n    CHECK(config.record_threshold_db == TS_RECORD_THRESHOLD_DB_DEFAULT);",
    "config tests")
write(path, s)

# ---------------------------------------------------------------------------
# UI: mark an alternate collection as the external REC bank and reuse the
# existing Capture destination frame/progress visuals.
# ---------------------------------------------------------------------------
path = "include/tapesister/ui.h"
s = read(path)
s = replace_once(
    s,
    "    TsCaptureState capture_state;\n    int capture_destination_slot;",
    "    TsCaptureState capture_state;\n    int external_record_bank;\n    int capture_destination_slot;",
    "ui rec flag")
write(path, s)

path = "src/ts_ui.c"
s = read(path)
s = replace_once(
    s,
    "    ui->capture_state = TS_CAPTURE_IDLE;\n    ui->renaming_bank_slot = -1;",
    "    ui->capture_state = TS_CAPTURE_IDLE;\n    ui->external_record_bank = 0;\n    ui->renaming_bank_slot = -1;",
    "ui rec init")
s = replace_once(
    s,
    "        const char *capture_label = ui->capture_state == TS_CAPTURE_RECORDING ? \"STOP\" :\n                                    ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?\n                                    \"ARMED\" : \"CAPTURE\";",
    "        const char *capture_label = ui->external_record_bank ?\n                                    (ui->capture_state == TS_CAPTURE_RECORDING ? \"STOP REC\" :\n                                     ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?\n                                     \"REC ARMED\" : \"REC ARM\") :\n                                    (ui->capture_state == TS_CAPTURE_RECORDING ? \"STOP\" :\n                                     ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?\n                                     \"ARMED\" : \"CAPTURE\");",
    "ui rec button")
s = replace_once(
    s,
    "        text(fb, 11, 318, bank_hint, RGB(184, 180, 184), 1);",
    "        if (ui->external_record_bank)\n            bank_hint = ui->capture_state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ?\n                        \"REC BANK ARMED  MAKE SOUND  THRESHOLD STARTS TAPE\" :\n                        ui->capture_state == TS_CAPTURE_RECORDING ?\n                        \"REC BANK RECORDING INPUT  STOP KEEPS TAKE\" :\n                        instrument->family_trajectory ?\n                        \"REC BANK  CHAIN ON  TAKES ADVANCE AND REARM\" :\n                        \"REC BANK  SELECT EMPTY TILE  REC ARM  1 TOGGLE\";\n        text(fb, 11, 318, bank_hint,\n             ui->external_record_bank ? PAL_VOLUME : RGB(184, 180, 184), 1);",
    "ui rec hint")
write(path, s)

# ---------------------------------------------------------------------------
# SDL integration. A second TsInstrument is the REC collection. Swapping the
# whole structs is intentional: every tile/editor pointer stays owned exactly
# once and recorded material immediately inherits the normal TapeSister tools.
# ---------------------------------------------------------------------------
path = "src/main_sdl.c"
s = read(path)

helpers = r'''
typedef struct {
    TsExternalRecorder recorder;
    int channels;
    int input_channel;
    uint32_t sample_rate;
    char device_label[128];
} ExternalInputState;

static int external_capture_busy(const ExternalInputState *input)
{
    return input != NULL &&
           (input->recorder.state == TS_EXTERNAL_CAPTURE_ARMED ||
            input->recorder.state == TS_EXTERNAL_CAPTURE_RECORDING);
}

static void external_input_callback(void *userdata, Uint8 *stream, int bytes)
{
    ExternalInputState *input = (ExternalInputState *)userdata;
    const float *samples = (const float *)stream;
    int values = bytes / (int)sizeof(float);
    int channels = input != NULL && input->channels > 0 ? input->channels : 1;
    if (input == NULL) return;
    for (int frame = 0; frame + channels <= values; frame += channels) {
        float value = 0.0f;
        if (input->input_channel == 0) {
            for (int channel = 0; channel < channels; ++channel)
                value += samples[frame + channel];
            value /= (float)channels;
        } else {
            int channel = input->input_channel - 1;
            if (channel >= channels) channel = 0;
            value = samples[frame + channel];
        }
        (void)ts_external_recorder_write_sample(&input->recorder, value);
    }
}

static int ensure_external_input_open(SDL_AudioDeviceID *input_device,
                                      ExternalInputState *input,
                                      const TsConfig *config,
                                      char *error, size_t error_size)
{
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    const char *device_name;
    if (input_device == NULL || input == NULL || config == NULL) return 0;
    if (*input_device != 0) return 1;
    SDL_zero(desired);
    SDL_zero(obtained);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 256;
    desired.callback = external_input_callback;
    desired.userdata = input;
    device_name = config->record_input_device[0] != '\0' ?
                  config->record_input_device : NULL;
    *input_device = SDL_OpenAudioDevice(
        device_name, 1, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
        SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (*input_device == 0) {
        snprintf(error, error_size, "Could not open recording input: %.110s", SDL_GetError());
        return 0;
    }
    if (obtained.format != AUDIO_F32SYS || obtained.freq <= 0 || obtained.channels == 0) {
        snprintf(error, error_size, "Recording input returned an unsupported audio format");
        SDL_CloseAudioDevice(*input_device);
        *input_device = 0;
        return 0;
    }
    if (config->record_input_channel > (int)obtained.channels) {
        snprintf(error, error_size, "Input channel %d is unavailable; device has %d channel%s",
                 config->record_input_channel, (int)obtained.channels,
                 obtained.channels == 1 ? "" : "s");
        SDL_CloseAudioDevice(*input_device);
        *input_device = 0;
        return 0;
    }
    input->channels = obtained.channels;
    input->input_channel = config->record_input_channel;
    input->sample_rate = (uint32_t)obtained.freq;
    snprintf(input->device_label, sizeof(input->device_label), "%s",
             device_name != NULL ? device_name : "SYSTEM DEFAULT");
    SDL_PauseAudioDevice(*input_device, 0);
    error[0] = '\0';
    return 1;
}

static TsCaptureState external_ui_state(TsExternalCaptureState state)
{
    if (state == TS_EXTERNAL_CAPTURE_ARMED)
        return TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    if (state == TS_EXTERNAL_CAPTURE_RECORDING)
        return TS_CAPTURE_RECORDING;
    return TS_CAPTURE_IDLE;
}

static void sync_external_capture_ui(SDL_AudioDeviceID input_device,
                                     ExternalInputState *input,
                                     TsUiState *ui)
{
    if (input_device) SDL_LockAudioDevice(input_device);
    ui->capture_state = external_ui_state(input->recorder.state);
    ui->capture_destination_slot = input->recorder.destination_slot;
    ui->capture_source_slot = -1;
    ui->capture_recorded_frames = input->recorder.recorded_frames;
    ui->capture_capacity_frames = input->recorder.capacity_frames;
    ui->staged_notes = 0u;
    if (input_device) SDL_UnlockAudioDevice(input_device);
}

static int arm_external_capture(SDL_AudioDeviceID output_device,
                                SDL_AudioDeviceID *input_device,
                                AudioState *audio, ExternalInputState *input,
                                TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    int slot = instrument->selected_slot;
    int ok;
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        snprintf(ui->status, sizeof(ui->status), "SELECT AN EMPTY REC TILE FIRST");
        return 0;
    }
    if (instrument->bank[slot].occupied) {
        snprintf(ui->status, sizeof(ui->status),
                 "REC TILE %02d IS OCCUPIED - SELECT AN EMPTY TILE", slot + 1);
        return 0;
    }
    if (!ensure_external_input_open(input_device, input, &ui->config,
                                    error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "REC INPUT FAILED: %.140s", error);
        return 0;
    }
    stop_all_force(output_device, audio, ui);
    SDL_LockAudioDevice(*input_device);
    ok = ts_external_recorder_arm(
        &input->recorder, slot, input->sample_rate,
        ui->config.record_threshold_db,
        ui->config.record_preroll_ms,
        ui->config.record_silence_ms,
        ui->config.record_tail_ms,
        ui->config.record_max_seconds,
        error, sizeof(error));
    SDL_UnlockAudioDevice(*input_device);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "REC ARM FAILED: %.142s", error);
        return 0;
    }
    sync_external_capture_ui(*input_device, input, ui);
    show_overlay(ui, "REC ARMED", 850u);
    snprintf(ui->status, sizeof(ui->status),
             "REC %02d ARMED  %s  CH %d  THRESH %d DB",
             slot + 1, input->device_label, ui->config.record_input_channel,
             ui->config.record_threshold_db);
    return 1;
}

static void cancel_external_capture(SDL_AudioDeviceID input_device,
                                    ExternalInputState *input, TsUiState *ui)
{
    if (input_device) SDL_LockAudioDevice(input_device);
    (void)ts_external_recorder_cancel(&input->recorder);
    ts_external_recorder_free(&input->recorder);
    if (input_device) SDL_UnlockAudioDevice(input_device);
    sync_external_capture_ui(input_device, input, ui);
    show_overlay(ui, "REC CANCELLED", 700u);
    snprintf(ui->status, sizeof(ui->status), "EXTERNAL RECORDING CANCELLED - TILE UNCHANGED");
}

static void stop_external_capture_early(SDL_AudioDeviceID input_device,
                                        ExternalInputState *input,
                                        TsUiState *ui)
{
    char error[160];
    int ok;
    if (input_device) SDL_LockAudioDevice(input_device);
    ok = ts_external_recorder_stop(&input->recorder, error, sizeof(error));
    if (input_device) SDL_UnlockAudioDevice(input_device);
    if (!ok)
        snprintf(ui->status, sizeof(ui->status), "REC STOP FAILED: %.140s", error);
    else
        snprintf(ui->status, sizeof(ui->status), "STOPPING REC - KEEPING SHORT TAKE");
}

static void external_capture_button(SDL_AudioDeviceID output_device,
                                    SDL_AudioDeviceID *input_device,
                                    AudioState *audio, ExternalInputState *input,
                                    TsUiState *ui, TsInstrument *instrument)
{
    if (input->recorder.state == TS_EXTERNAL_CAPTURE_RECORDING) {
        stop_external_capture_early(*input_device, input, ui);
        return;
    }
    if (input->recorder.state == TS_EXTERNAL_CAPTURE_ARMED) {
        cancel_external_capture(*input_device, input, ui);
        return;
    }
    (void)arm_external_capture(output_device, input_device, audio, input, ui, instrument);
}

static int install_external_take(SDL_AudioDeviceID output_device,
                                 AudioState *audio, TsUiState *ui,
                                 TsInstrument *instrument, int slot,
                                 const float *captured, size_t frames,
                                 uint32_t sample_rate,
                                 char *error, size_t error_size)
{
    int ok;
    char name[32];
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT || captured == NULL ||
        frames == 0u || sample_rate == 0u) {
        snprintf(error, error_size, "Invalid external take");
        return 0;
    }
    lock_edit(output_device, audio);
    ok = ts_instrument_select_bank(instrument, slot, error, error_size);
    if (ok && instrument->bank[slot].occupied) {
        snprintf(error, error_size, "REC destination is no longer empty");
        ok = 0;
    }
    if (ok)
        ok = ts_instrument_activate_silence(instrument, frames, sample_rate,
                                            error, error_size);
    if (ok) {
        TsBankSlot *bank = &instrument->bank[slot];
        if (instrument->current.frames != frames || instrument->parent.frames != frames ||
            bank->sample.frames != frames || bank->edit_parent.frames != frames ||
            instrument->current.data == NULL || instrument->parent.data == NULL ||
            bank->sample.data == NULL || bank->edit_parent.data == NULL) {
            snprintf(error, error_size, "REC tile buffers did not match the captured take");
            ok = 0;
        } else {
            size_t bytes = frames * sizeof(*captured);
            memcpy(instrument->current.data, captured, bytes);
            memcpy(instrument->parent.data, captured, bytes);
            memcpy(bank->sample.data, captured, bytes);
            memcpy(bank->edit_parent.data, captured, bytes);
            snprintf(name, sizeof(name), "REC %02d", slot + 1);
            snprintf(instrument->current.name, sizeof(instrument->current.name), "%s", name);
            snprintf(instrument->parent.name, sizeof(instrument->parent.name), "%s", name);
            snprintf(bank->sample.name, sizeof(bank->sample.name), "%s", name);
            snprintf(bank->edit_parent.name, sizeof(bank->edit_parent.name), "%s", name);
            bank->capture_kind = TS_BANK_CAPTURE_CURRENT;
            bank->relation = TS_FAMILY_CAPTURED;
            bank->parent_slot = -1;
            instrument->source_kind = TS_SOURCE_IMPORTED;
            instrument->has_selection = 1;
            instrument->selection_first = 0u;
            instrument->selection_last = frames;
            instrument->has_playhead = 1;
            instrument->playhead_frame = 0u;
            ts_instrument_show_all(instrument);
        }
    }
    unlock_edit(output_device, audio, ui, instrument);
    return ok;
}

static void finalize_external_recording(SDL_AudioDeviceID output_device,
                                        SDL_AudioDeviceID *input_device,
                                        AudioState *audio,
                                        ExternalInputState *input,
                                        TsUiState *ui,
                                        TsInstrument *instrument)
{
    char error[160];
    float *captured = NULL;
    size_t frames;
    uint32_t sample_rate;
    int slot;
    int chain;
    int ok;
    if (input->recorder.state != TS_EXTERNAL_CAPTURE_COMPLETED) return;
    if (*input_device) SDL_LockAudioDevice(*input_device);
    frames = input->recorder.recorded_frames;
    sample_rate = input->recorder.sample_rate;
    slot = input->recorder.destination_slot;
    if (frames > 0u && frames <= SIZE_MAX / sizeof(*captured)) {
        captured = (float *)malloc(frames * sizeof(*captured));
        if (captured != NULL)
            memcpy(captured, input->recorder.buffer, frames * sizeof(*captured));
    }
    if (*input_device) SDL_UnlockAudioDevice(*input_device);
    if (captured == NULL) {
        if (*input_device) SDL_LockAudioDevice(*input_device);
        ts_external_recorder_free(&input->recorder);
        if (*input_device) SDL_UnlockAudioDevice(*input_device);
        sync_external_capture_ui(*input_device, input, ui);
        snprintf(ui->status, sizeof(ui->status), "REC TAKE FAILED - OUT OF MEMORY");
        return;
    }
    chain = instrument->family_trajectory;
    ok = install_external_take(output_device, audio, ui, instrument, slot,
                               captured, frames, sample_rate,
                               error, sizeof(error));
    free(captured);
    if (*input_device) SDL_LockAudioDevice(*input_device);
    ts_external_recorder_free(&input->recorder);
    if (*input_device) SDL_UnlockAudioDevice(*input_device);
    if (!ok) {
        sync_external_capture_ui(*input_device, input, ui);
        snprintf(ui->status, sizeof(ui->status), "REC COMMIT FAILED: %.138s", error);
        return;
    }
    show_overlay(ui, "REC TAKE KEPT", 850u);
    if (chain) {
        int next = ts_external_next_chain_slot(slot);
        if (next >= 0 && !instrument->bank[next].occupied) {
            lock_edit(output_device, audio);
            ok = ts_instrument_select_bank(instrument, next, error, sizeof(error));
            unlock_edit(output_device, audio, ui, instrument);
            if (ok && arm_external_capture(output_device, input_device, audio, input,
                                           ui, instrument)) {
                snprintf(ui->status, sizeof(ui->status),
                         "REC %02d KEPT - CHAIN ARMED %02d", slot + 1, next + 1);
                return;
            }
        }
        snprintf(ui->status, sizeof(ui->status),
                 "REC %02d KEPT - CHAIN STOPPED AT NEXT OCCUPIED/END TILE", slot + 1);
    } else {
        sync_external_capture_ui(*input_device, input, ui);
        snprintf(ui->status, sizeof(ui->status),
                 "REC %02d KEPT - %zu FRAMES AT %u HZ", slot + 1, frames, sample_rate);
    }
}

static int toggle_record_bank(SDL_Window *window,
                              SDL_AudioDeviceID output_device,
                              ExternalInputState *input,
                              AudioState *audio, TsUiState *ui,
                              TsInstrument *instrument,
                              TsInstrument *parked,
                              uint64_t *parked_saved_hash,
                              int *record_bank_active)
{
    TsInstrument temp;
    uint64_t saved;
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
        audio->capture.state == TS_CAPTURE_RECORDING || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL RECORDING BEFORE SWITCHING BANKS");
        return 0;
    }
    stop_all_force(output_device, audio, ui);
    temp = *instrument;
    *instrument = *parked;
    *parked = temp;
    saved = ui->saved_state_hash;
    ui->saved_state_hash = *parked_saved_hash;
    *parked_saved_hash = saved;
    *record_bank_active = !*record_bank_active;
    ui->external_record_bank = *record_bank_active;
    ui->capture_state = TS_CAPTURE_IDLE;
    ui->capture_destination_slot = -1;
    ui->capture_source_slot = -1;
    ui->capture_recorded_frames = 0u;
    ui->capture_capacity_frames = 0u;
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ts_ui_select_panel(ui, TS_UI_PANEL_SAMPLE_TILES);
    if (window != NULL)
        SDL_SetWindowTitle(window, *record_bank_active ?
                          "TapeSister - REC BANK" : "TapeSister");
    show_overlay(ui, *record_bank_active ? "REC BANK" : "SAMPLE BANK", 800u);
    snprintf(ui->status, sizeof(ui->status),
             *record_bank_active ?
             "REC BANK - SELECT EMPTY TILE AND REC ARM  PRESS 1 AGAIN FOR SAMPLE BANK" :
             "SAMPLE BANK - PRESS 1 AGAIN TO RETURN TO REC BANK");
    return 1;
}
'''

s = replace_once(s, "int main(int argc, char **argv)\n{", helpers + "\nint main(int argc, char **argv)\n{", "main helpers")

s = replace_once(
    s,
    "    SDL_AudioDeviceID device = 0;\n    SDL_AudioSpec desired, obtained;\n    TsInstrument instrument;",
    "    SDL_AudioDeviceID device = 0;\n    SDL_AudioDeviceID input_device = 0;\n    SDL_AudioSpec desired, obtained;\n    TsInstrument instrument;\n    TsInstrument parked_instrument;",
    "main device vars")
s = replace_once(
    s,
    "    AudioState audio = {0};\n    char ignored_exchange[TS_EXCHANGE_PATH_MAX] = {0};\n    uint32_t last_exchange_poll = 0;\n    int running = 1;",
    "    AudioState audio = {0};\n    ExternalInputState external_input = {0};\n    char ignored_exchange[TS_EXCHANGE_PATH_MAX] = {0};\n    uint32_t last_exchange_poll = 0;\n    uint64_t parked_saved_hash = 0;\n    int record_bank_active = 0;\n    int running = 1;",
    "main rec vars")
s = replace_once(
    s,
    "    ts_instrument_init(&instrument);\n    ts_sample_init(&clipboard);",
    "    ts_instrument_init(&instrument);\n    ts_instrument_init(&parked_instrument);\n    ts_external_recorder_init(&external_input.recorder);\n    ts_sample_init(&clipboard);",
    "main rec init")
s = replace_once(
    s,
    "    ui.saved_state_hash = instrument_state_hash(&instrument);\n    last_exchange_poll = SDL_GetTicks();",
    "    ui.saved_state_hash = instrument_state_hash(&instrument);\n    parked_saved_hash = instrument_state_hash(&parked_instrument);\n    last_exchange_poll = SDL_GetTicks();",
    "parked hash init")

# Capture button routes to external input only while the REC collection is active.
s = replace_once(
    s,
    "                    } else if (capture_control) {\n                        capture_button(device, &audio, &ui, &instrument, obtained.freq);",
    "                    } else if (capture_control) {\n                        if (record_bank_active)\n                            external_capture_button(device, &input_device, &audio,\n                                                    &external_input, &ui, &instrument);\n                        else\n                            capture_button(device, &audio, &ui, &instrument, obtained.freq);",
    "capture button routing")

# Clear-all cannot destroy an armed external destination.
s = replace_once(
    s,
    "                } else if (wave_action == TS_UI_WAVE_ACTION_CLEAR_ALL &&\n                           !ui.show_keyboard && !ui.show_recipes &&\n                           !ui.show_ingredients) {\n                    clear_all_bank_slots(device, &audio, &ui, &instrument);",
    "                } else if (wave_action == TS_UI_WAVE_ACTION_CLEAR_ALL &&\n                           !ui.show_keyboard && !ui.show_recipes &&\n                           !ui.show_ingredients) {\n                    if (record_bank_active && external_capture_busy(&external_input))\n                        snprintf(ui.status, sizeof(ui.status),\n                                 \"CANCEL REC BEFORE CLEAR ALL\");\n                    else\n                        clear_all_bank_slots(device, &audio, &ui, &instrument);",
    "clear all lock")

# Number-row panel selection: repeating 1 while already on Sample Tiles swaps the
# complete collection, analogous to repeating 3/4 for their internal pages.
number_pattern = r'''                \} else if \(\(mod & \(KMOD_SHIFT \| KMOD_CTRL \| KMOD_ALT\)\) == 0 &&\n                           key >= SDLK_1 && key <= SDLK_4\) \{\n                    TsUiPanel panel = \(TsUiPanel\)\(key - SDLK_1\);\n                    TsUiPanel before = ts_ui_panel\(&ui\);\n                    static const char \*const names\[\] = \{\n                        "SAMPLE TILES", "KEYBOARD", "CDP", "DSP"\n                    \};\n                    ts_ui_select_panel\(&ui, panel\);\n                    ui.bank_view_slot = -1;\n                    if \(panel == TS_UI_PANEL_CDP\)\n                        snprintf\(ui.status, sizeof\(ui.status\),\n                                 "CDP %d PAGE%s - KEYS 1 2 3 4",\n                                 ui.cdp_page \+ 1,\n                                 before == TS_UI_PANEL_CDP \? " TOGGLED" : " RESTORED"\);\n                    else if \(panel == TS_UI_PANEL_DSP\)\n                        snprintf\(ui.status, sizeof\(ui.status\),\n                                 "DSP %d %s%s - KEYS 1 2 3 4",\n                                 ui.dsp_page \+ 1,\n                                 ui.dsp_page == 0 \? "PROCESS" : "PRIMITIVES",\n                                 before == TS_UI_PANEL_DSP \? " TOGGLED" : " RESTORED"\);\n                    else\n                        snprintf\(ui.status, sizeof\(ui.status\),\n                                 "%s PANEL - KEYS 1 2 3 4", names\[panel\]\);'''
number_replacement = r'''                } else if ((mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) == 0 &&
                           key >= SDLK_1 && key <= SDLK_4) {
                    TsUiPanel panel = (TsUiPanel)(key - SDLK_1);
                    TsUiPanel before = ts_ui_panel(&ui);
                    static const char *const names[] = {
                        "SAMPLE TILES", "KEYBOARD", "CDP", "DSP"
                    };
                    if (panel == TS_UI_PANEL_SAMPLE_TILES &&
                        before == TS_UI_PANEL_SAMPLE_TILES) {
                        (void)toggle_record_bank(window, device, &external_input,
                                                 &audio, &ui, &instrument,
                                                 &parked_instrument,
                                                 &parked_saved_hash,
                                                 &record_bank_active);
                    } else {
                        ts_ui_select_panel(&ui, panel);
                        ui.bank_view_slot = -1;
                        if (panel == TS_UI_PANEL_CDP)
                            snprintf(ui.status, sizeof(ui.status),
                                     "CDP %d PAGE%s - KEYS 1 2 3 4",
                                     ui.cdp_page + 1,
                                     before == TS_UI_PANEL_CDP ? " TOGGLED" : " RESTORED");
                        else if (panel == TS_UI_PANEL_DSP)
                            snprintf(ui.status, sizeof(ui.status),
                                     "DSP %d %s%s - KEYS 1 2 3 4",
                                     ui.dsp_page + 1,
                                     ui.dsp_page == 0 ? "PROCESS" : "PRIMITIVES",
                                     before == TS_UI_PANEL_DSP ? " TOGGLED" : " RESTORED");
                        else
                            snprintf(ui.status, sizeof(ui.status),
                                     "%s PANEL - KEYS 1 2 3 4", names[panel]);
                    }'''
s = sub_once(s, number_pattern, number_replacement, "number 1 bank toggle")

# Escape and Space own external recording before ordinary editing/playback.
s = replace_once(
    s,
    "                } else if (key == SDLK_ESCAPE) {\n                    ui.bank_clear_armed = 0;\n                    if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||",
    "                } else if (key == SDLK_ESCAPE) {\n                    ui.bank_clear_armed = 0;\n                    if (record_bank_active && external_capture_busy(&external_input)) {\n                        cancel_external_capture(input_device, &external_input, &ui);\n                    } else if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||",
    "escape external cancel")
s = replace_once(
    s,
    "                } else if (key == SDLK_SPACE) {\n                    ui.bank_clear_armed = 0;\n                    if (audio.capture.state == TS_CAPTURE_RECORDING)",
    "                } else if (key == SDLK_SPACE) {\n                    ui.bank_clear_armed = 0;\n                    if (record_bank_active &&\n                        external_input.recorder.state == TS_EXTERNAL_CAPTURE_RECORDING)\n                        stop_external_capture_early(input_device, &external_input, &ui);\n                    else if (record_bank_active &&\n                             external_input.recorder.state == TS_EXTERNAL_CAPTURE_ARMED)\n                        snprintf(ui.status, sizeof(ui.status),\n                                 \"REC ARMED - MAKE SOUND OR ESC/CAPTURE TO CANCEL\");\n                    else if (audio.capture.state == TS_CAPTURE_RECORDING)",
    "space external stop")

# Don't let tile selection/clearing change the destination while a take is armed.
s = replace_once(
    s,
    "                    } else if (bank_slot >= 0) {\n                        TsUiBankAction action = ts_ui_bank_action(\n                            0, bank_modifiers(mod));",
    "                    } else if (bank_slot >= 0) {\n                        TsUiBankAction action = ts_ui_bank_action(\n                            0, bank_modifiers(mod));\n                        if (record_bank_active && external_capture_busy(&external_input)) {\n                            snprintf(ui.status, sizeof(ui.status),\n                                     \"REC TILE %02d LOCKED - STOP OR ESC FIRST\",\n                                     external_input.recorder.destination_slot + 1);\n                            continue;\n                        }",
    "left tile lock")
s = replace_once(
    s,
    "                action = ts_ui_bank_action(1, bank_modifiers(mod));\n                if (ui.exit_confirm_open) {",
    "                action = ts_ui_bank_action(1, bank_modifiers(mod));\n                if (record_bank_active && bank_slot >= 0 &&\n                    external_capture_busy(&external_input)) {\n                    snprintf(ui.status, sizeof(ui.status),\n                             \"REC TILE %02d LOCKED - STOP OR ESC FIRST\",\n                             external_input.recorder.destination_slot + 1);\n                } else if (ui.exit_confirm_open) {",
    "right tile lock")

# Poll the input callback's state and commit completed takes before rendering.
s = replace_once(
    s,
    "        if (audio.capture.state == TS_CAPTURE_COMPLETED)\n            finalize_capture(device, &audio, &ui, &instrument);\n        poll_transform_worker(device, &audio, &ui, &instrument, &transform);\n        sync_capture_ui(device, &audio, &ui);",
    "        if (record_bank_active &&\n            external_input.recorder.state == TS_EXTERNAL_CAPTURE_RECORDING &&\n            ui.capture_state != TS_CAPTURE_RECORDING) {\n            show_overlay(&ui, \"REC STARTED\", 650u);\n            snprintf(ui.status, sizeof(ui.status),\n                     \"REC %02d RECORDING - SILENCE WILL AUTO STOP\",\n                     external_input.recorder.destination_slot + 1);\n        }\n        if (record_bank_active &&\n            external_input.recorder.state == TS_EXTERNAL_CAPTURE_COMPLETED)\n            finalize_external_recording(device, &input_device, &audio,\n                                        &external_input, &ui, &instrument);\n        if (audio.capture.state == TS_CAPTURE_COMPLETED)\n            finalize_capture(device, &audio, &ui, &instrument);\n        poll_transform_worker(device, &audio, &ui, &instrument, &transform);\n        if (record_bank_active)\n            sync_external_capture_ui(input_device, &external_input, &ui);\n        else\n            sync_capture_ui(device, &audio, &ui);",
    "external poll/finalize")

# Cleanup both independent collections and the capture device.
s = replace_once(
    s,
    "    if (device) SDL_CloseAudioDevice(device);\n    ts_capture_free(&audio.capture);",
    "    if (input_device) SDL_CloseAudioDevice(input_device);\n    if (device) SDL_CloseAudioDevice(device);\n    ts_external_recorder_free(&external_input.recorder);\n    ts_capture_free(&audio.capture);",
    "input cleanup")
s = replace_once(
    s,
    "    ts_sample_free(&clipboard);\n    ts_instrument_free(&instrument);",
    "    ts_sample_free(&clipboard);\n    ts_instrument_free(&parked_instrument);\n    ts_instrument_free(&instrument);",
    "parked cleanup")
write(path, s)

# ---------------------------------------------------------------------------
# User-facing docs for the exact workflow being tested.
# ---------------------------------------------------------------------------
path = "README.md"
s = read(path)
insert = '''\n## External REC bank\n\nPress `1` to reach **Sample Tiles**, then press `1` again to toggle the complete\n16-tile **REC BANK**. The window title and bank hint identify the alternate collection.\nThe normal collection is parked intact while REC is active; pressing `1` again swaps it\nback. Recorded tiles are ordinary TapeSister tiles, so the same selection, loop, DSP,\nCDP, export, and FT2 Link tools work immediately after capture.\n\nSelect an empty REC tile and click **REC ARM**. TapeSister opens the configured capture\ndevice (or the machine default when `record_input_device` is blank), keeps a circular\npre-roll, and waits without writing a take until the signal crosses\n`record_threshold_db`. Silence plus the configured tail ends the take automatically;\n**STOP REC** or Space keeps a shorter take, while Escape or clicking the armed button\ncancels it without changing the tile.\n\nWhen **CHAIN** is on in the Family page, a completed take advances to the next sequential\nempty REC tile and immediately rearms. This makes it possible to arm once, walk to an\nexternal synth or microphone, make a sound, wait for silence, change the source, and\ncontinue until the bank is full. CHAIN stops rather than overwriting an occupied tile or\nwrapping past tile 16.\n\n`[External Recording]` in `tapesister.ini` controls the input device and channel plus\nthreshold, pre-roll, silence, tail, and maximum take length. `record_input_channel=0`\nmixes the device inputs to mono; `1` records the first/left channel and `2` the\nsecond/right channel. These settings are intentionally editable without recompiling.\n\n'''
s = replace_once(s, "## Capture a performance to a new tile\n", insert + "## Capture a performance to a new tile\n", "README rec section")
write(path, s)

print("PR35 external REC bank integration patches applied")
