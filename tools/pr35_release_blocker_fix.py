from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text, old, new, label):
    if old not in text:
        raise SystemExit(f"missing patch anchor: {label}")
    if text.count(old) != 1:
        raise SystemExit(f"non-unique patch anchor: {label} ({text.count(old)})")
    return text.replace(old, new, 1)


# 1. Give the large TsInstrument object a bounded-stack storage swap primitive.
path = "include/tapesister/sample.h"
text = read(path)
text = replace_once(
    text,
    "void ts_instrument_init(TsInstrument *instrument);\nvoid ts_instrument_free(TsInstrument *instrument);\n",
    "void ts_instrument_init(TsInstrument *instrument);\nvoid ts_instrument_free(TsInstrument *instrument);\nvoid ts_instrument_swap(TsInstrument *left, TsInstrument *right);\n",
    "sample.h swap prototype",
)
write(path, text)

path = "src/ts_sample.c"
text = read(path)
old = """void ts_instrument_free(TsInstrument *instrument)\n{\n    ts_sample_free(&instrument->parent);\n    ts_sample_free(&instrument->current);\n    bank_free(instrument);\n    memset(instrument, 0, sizeof(*instrument));\n}\n"""
new = old + """\nvoid ts_instrument_swap(TsInstrument *left, TsInstrument *right)\n{\n    /* TsInstrument is intentionally large. Never create a full temporary copy on\n       the call stack just to swap collections: Windows' default thread stack is\n       small enough for that to become a release-blocking stack overflow. */\n    unsigned char scratch[1024];\n    unsigned char *a = (unsigned char *)left;\n    unsigned char *b = (unsigned char *)right;\n    size_t offset = 0u;\n    if (left == NULL || right == NULL || left == right) return;\n    while (offset < sizeof(*left)) {\n        size_t amount = sizeof(scratch);\n        if (amount > sizeof(*left) - offset) amount = sizeof(*left) - offset;\n        memcpy(scratch, a + offset, amount);\n        memcpy(a + offset, b + offset, amount);\n        memcpy(b + offset, scratch, amount);\n        offset += amount;\n    }\n}\n"""
text = replace_once(text, old, new, "ts_sample.c bounded instrument swap")
write(path, text)

# 2. Add a regression test that repeatedly swaps two full collections without a
#    full-size stack temporary, and print the relevant storage sizes for Windows CI.
path = "tests/test_external_record.c"
text = read(path)
text = replace_once(
    text,
    '#include "tapesister/config.h"\n',
    '#include "tapesister/config.h"\n#include "tapesister/sample.h"\n#include "tapesister/ui.h"\n',
    "external test includes",
)
insert = r'''
static void test_large_instrument_swap_is_bounded_stack(void)
{
    TsInstrument left;
    TsInstrument right;
    char error[160];
    ts_instrument_init(&left);
    ts_instrument_init(&right);
    CHECK(ts_instrument_activate_silence(&left, 64u, 48000u,
                                         error, sizeof(error)));
    CHECK(ts_instrument_activate_silence(&right, 96u, 44100u,
                                         error, sizeof(error)));
    if (left.current.data != NULL) left.current.data[0] = 0.25f;
    if (right.current.data != NULL) right.current.data[0] = -0.5f;
    for (int iteration = 0; iteration < 2000; ++iteration)
        ts_instrument_swap(&left, &right);
    CHECK(left.current.frames == 64u);
    CHECK(right.current.frames == 96u);
    CHECK(left.current.data != NULL && fabsf(left.current.data[0] - 0.25f) < 0.0001f);
    CHECK(right.current.data != NULL && fabsf(right.current.data[0] + 0.5f) < 0.0001f);
    printf("storage sizes: TsInstrument=%zu TsFramebuffer=%zu TsUiState=%zu\n",
           sizeof(TsInstrument), sizeof(TsFramebuffer), sizeof(TsUiState));
    ts_instrument_free(&right);
    ts_instrument_free(&left);
}

'''
text = replace_once(text, "int main(void)\n{\n", insert + "int main(void)\n{\n", "external swap test function")
text = replace_once(
    text,
    "    test_config_defaults();\n",
    "    test_config_defaults();\n    test_large_instrument_swap_is_bounded_stack();\n",
    "external swap test invocation",
)
write(path, text)

# 3. Repair main_sdl.c: diagnostics, heap-parked REC collection, no full
#    TsInstrument stack temporary, stale transform cancellation, and a quieter
#    external-input callback lifecycle.
path = "src/main_sdl.c"
text = read(path)
text = replace_once(text, "#include <stdlib.h>\n", "#include <stdlib.h>\n#include <stdarg.h>\n", "stdarg include")

old = "enum { TS_SPLASH_MILLISECONDS = 5000 };\n\n"
new = r'''enum { TS_SPLASH_MILLISECONDS = 5000 };

static FILE *diagnostic_log_file;
static unsigned long diagnostic_sequence;

static void diagnostic_log_open(void)
{
    const char *path = getenv("TAPESISTER_DIAGNOSTIC_LOG");
#ifdef _WIN32
    if (path == NULL || path[0] == '\0') path = "tapesister-diagnostic.log";
#else
    if (path == NULL || path[0] == '\0') return;
#endif
    diagnostic_log_file = fopen(path, "wb");
    if (diagnostic_log_file == NULL) return;
    setvbuf(diagnostic_log_file, NULL, _IOLBF, 0);
}

static void diagnostic_log(const char *format, ...)
{
    va_list args;
    if (diagnostic_log_file == NULL || format == NULL) return;
    fprintf(diagnostic_log_file, "%06lu ", ++diagnostic_sequence);
    va_start(args, format);
    vfprintf(diagnostic_log_file, format, args);
    va_end(args);
    fputc('\n', diagnostic_log_file);
    fflush(diagnostic_log_file);
}

static void diagnostic_log_close(void)
{
    if (diagnostic_log_file != NULL) fclose(diagnostic_log_file);
    diagnostic_log_file = NULL;
}

'''
text = replace_once(text, old, new, "diagnostic logger")

# Input open: keep the SDL capture callback paused until a recorder is armed.
text = replace_once(
    text,
    "    device_name = config->record_input_device[0] != '\\0' ?\n                  config->record_input_device : NULL;\n    *input_device = SDL_OpenAudioDevice(\n",
    "    device_name = config->record_input_device[0] != '\\0' ?\n                  config->record_input_device : NULL;\n    diagnostic_log(\"REC input open begin device=%s\",\n                   device_name != NULL ? device_name : \"SYSTEM DEFAULT\");\n    *input_device = SDL_OpenAudioDevice(\n",
    "input open begin logging",
)
text = replace_once(
    text,
    "    if (*input_device == 0) {\n        snprintf(error, error_size, \"Could not open recording input: %.110s\", SDL_GetError());\n        return 0;\n    }\n",
    "    if (*input_device == 0) {\n        diagnostic_log(\"REC input open failed: %s\", SDL_GetError());\n        snprintf(error, error_size, \"Could not open recording input: %.110s\", SDL_GetError());\n        return 0;\n    }\n",
    "input open failure logging",
)
text = replace_once(
    text,
    "    snprintf(input->device_label, sizeof(input->device_label), \"%s\",\n             device_name != NULL ? device_name : \"SYSTEM DEFAULT\");\n    SDL_PauseAudioDevice(*input_device, 0);\n    error[0] = '\\0';\n",
    "    snprintf(input->device_label, sizeof(input->device_label), \"%.127s\",\n             device_name != NULL ? device_name : \"SYSTEM DEFAULT\");\n    diagnostic_log(\"REC input open ok id=%u rate=%u channels=%d (left paused)\",\n                   (unsigned)*input_device, input->sample_rate, input->channels);\n    error[0] = '\\0';\n",
    "input opened paused",
)

# Arm while paused, then enable the callback only after the recorder is ready.
old = r'''    stop_all_force(output_device, audio, ui);
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
'''
new = r'''    stop_all_force(output_device, audio, ui);
    /* Keep the callback stopped while recorder buffers are allocated/reset. */
    SDL_PauseAudioDevice(*input_device, 1);
    diagnostic_log("REC arm begin slot=%d", slot + 1);
    ok = ts_external_recorder_arm(
        &input->recorder, slot, input->sample_rate,
        ui->config.record_threshold_db,
        ui->config.record_preroll_ms,
        ui->config.record_silence_ms,
        ui->config.record_tail_ms,
        ui->config.record_max_seconds,
        error, sizeof(error));
    if (!ok) {
        diagnostic_log("REC arm failed: %s", error);
        snprintf(ui->status, sizeof(ui->status), "REC ARM FAILED: %.142s", error);
        return 0;
    }
    SDL_PauseAudioDevice(*input_device, 0);
    diagnostic_log("REC arm ok slot=%d callback running", slot + 1);
'''
text = replace_once(text, old, new, "arm input lifecycle")

old = r'''    if (input_device) SDL_LockAudioDevice(input_device);
    (void)ts_external_recorder_cancel(&input->recorder);
    ts_external_recorder_free(&input->recorder);
    if (input_device) SDL_UnlockAudioDevice(input_device);
'''
new = r'''    if (input_device) SDL_PauseAudioDevice(input_device, 1);
    diagnostic_log("REC cancel state=%s", ts_external_capture_state_name(input->recorder.state));
    (void)ts_external_recorder_cancel(&input->recorder);
    ts_external_recorder_free(&input->recorder);
'''
text = replace_once(text, old, new, "cancel input lifecycle")

# Once capture completes, stop the callback before copying/freeing recorder buffers.
text = replace_once(
    text,
    "    if (input->recorder.state != TS_EXTERNAL_CAPTURE_COMPLETED) return;\n    if (*input_device) SDL_LockAudioDevice(*input_device);\n",
    "    if (input->recorder.state != TS_EXTERNAL_CAPTURE_COMPLETED) return;\n    if (*input_device) SDL_PauseAudioDevice(*input_device, 1);\n    diagnostic_log(\"REC finalize begin slot=%d frames=%zu\",\n                   input->recorder.destination_slot + 1, input->recorder.recorded_frames);\n",
    "finalize pause callback",
)
text = replace_once(text, "    if (*input_device) SDL_UnlockAudioDevice(*input_device);\n    if (captured == NULL) {\n", "    if (captured == NULL) {\n", "finalize first unlock removal")
text = replace_once(
    text,
    "        if (*input_device) SDL_LockAudioDevice(*input_device);\n        ts_external_recorder_free(&input->recorder);\n        if (*input_device) SDL_UnlockAudioDevice(*input_device);\n",
    "        ts_external_recorder_free(&input->recorder);\n",
    "finalize oom lock removal",
)
text = replace_once(
    text,
    "    if (*input_device) SDL_LockAudioDevice(*input_device);\n    ts_external_recorder_free(&input->recorder);\n    if (*input_device) SDL_UnlockAudioDevice(*input_device);\n",
    "    ts_external_recorder_free(&input->recorder);\n    diagnostic_log(\"REC finalize commit %s slot=%d\", ok ? \"ok\" : \"failed\", slot + 1);\n",
    "finalize free lock removal",
)

# Replace the bank toggle function wholesale: lazy heap parking, bounded swap,
# transform invalidation, and navigation diagnostics.
pattern = re.compile(r"static int toggle_record_bank\(.*?\n}\n\nint main\(int argc, char \*\*argv\)", re.S)
match = pattern.search(text)
if match is None:
    raise SystemExit("missing toggle_record_bank block")
replacement = r'''static int toggle_record_bank(SDL_Window *window,
                              SDL_AudioDeviceID output_device,
                              ExternalInputState *input,
                              AudioState *audio, TsUiState *ui,
                              TransformController *controller,
                              TsInstrument *instrument,
                              TsInstrument **parked,
                              uint64_t *parked_saved_hash,
                              int *record_bank_active)
{
    uint64_t saved;
    diagnostic_log("BANK toggle begin active=%d parked=%p worker=%p output=%u input=%u",
                   *record_bank_active, (void *)*parked,
                   controller != NULL ? (void *)controller->worker : NULL,
                   (unsigned)output_device, 0u);
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
        audio->capture.state == TS_CAPTURE_RECORDING || external_capture_busy(input)) {
        diagnostic_log("BANK toggle refused: recording busy");
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL RECORDING BEFORE SWITCHING BANKS");
        return 0;
    }
    if (*parked == NULL) {
        *parked = (TsInstrument *)malloc(sizeof(**parked));
        if (*parked == NULL) {
            diagnostic_log("BANK toggle failed: parked allocation %zu bytes", sizeof(**parked));
            snprintf(ui->status, sizeof(ui->status),
                     "REC BANK UNAVAILABLE - OUT OF MEMORY");
            return 0;
        }
        ts_instrument_init(*parked);
        *parked_saved_hash = instrument_state_hash(*parked);
        diagnostic_log("BANK parked collection allocated on heap bytes=%zu ptr=%p",
                       sizeof(**parked), (void *)*parked);
    }
    /* A quick transform worker owns a cloned input, but its eventual result belongs
       to the old collection. Cancel/invalidate it before changing collection identity. */
    if (controller != NULL)
        mark_transform_stale(output_device, audio, ui, controller,
                             "BANK COLLECTION CHANGED - RENDER AGAIN");
    stop_all_force(output_device, audio, ui);
    diagnostic_log("BANK swap begin instrument=%p parked=%p", (void *)instrument, (void *)*parked);
    ts_instrument_swap(instrument, *parked);
    diagnostic_log("BANK swap complete");
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
    diagnostic_log("BANK toggle complete active=%d selected=%d", *record_bank_active,
                   instrument->selected_slot);
    return 1;
}

int main(int argc, char **argv)'''
text = text[:match.start()] + replacement + text[match.end():]

# Main stack: the second giant instrument becomes a lazy heap pointer.
text = replace_once(text, "    TsInstrument instrument;\n    TsInstrument parked_instrument;\n", "    TsInstrument instrument;\n    TsInstrument *parked_instrument = NULL;\n", "main parked pointer")
text = replace_once(text, "    ts_instrument_init(&instrument);\n    ts_instrument_init(&parked_instrument);\n", "    diagnostic_log_open();\n    diagnostic_log(\"START argc=%d sizeof(TsInstrument)=%zu sizeof(TsFramebuffer)=%zu sizeof(TsUiState)=%zu\",\n                   argc, sizeof(TsInstrument), sizeof(TsFramebuffer), sizeof(TsUiState));\n    ts_instrument_init(&instrument);\n", "main diagnostics and parked init removal")
text = replace_once(text, "    parked_saved_hash = instrument_state_hash(&parked_instrument);\n", "", "parked startup hash removal")

# Startup phase diagnostics and nonfatal output-device status.
text = replace_once(
    text,
    "    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {\n",
    "    diagnostic_log(\"SDL init begin\");\n    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {\n        diagnostic_log(\"SDL init failed: %s\", SDL_GetError());\n",
    "SDL init logging",
)
text = replace_once(
    text,
    "    {\n        char palette_error[160];\n",
    "    diagnostic_log(\"SDL init ok\");\n    {\n        char palette_error[160];\n",
    "SDL init ok logging",
)
text = replace_once(
    text,
    "    window = SDL_CreateWindow(\"TapeSister\", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,\n",
    "    diagnostic_log(\"video create begin\");\n    window = SDL_CreateWindow(\"TapeSister\", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,\n",
    "video begin logging",
)
text = replace_once(
    text,
    "    if (!window || !renderer || !texture) {\n        fprintf(stderr, \"Video setup failed: %s\\n\", SDL_GetError());\n",
    "    if (!window || !renderer || !texture) {\n        diagnostic_log(\"video setup failed: %s\", SDL_GetError());\n        fprintf(stderr, \"Video setup failed: %s\\n\", SDL_GetError());\n",
    "video failure logging",
)
text = replace_once(
    text,
    "    if (running && !show_splash(renderer)) {\n",
    "    if (running) diagnostic_log(\"video setup ok - splash begin\");\n    if (running && !show_splash(renderer)) {\n",
    "splash logging",
)
text = replace_once(
    text,
    "    SDL_zero(desired);\n",
    "    diagnostic_log(\"splash complete - output audio open begin\");\n    SDL_zero(desired);\n",
    "output audio logging begin",
)
text = replace_once(
    text,
    "    if (!device) snprintf(ui.status, sizeof(ui.status), \"AUDIO UNAVAILABLE: %.130s\", SDL_GetError());\n    else SDL_PauseAudioDevice(device, 0);\n",
    "    if (!device) {\n        diagnostic_log(\"output audio unavailable (nonfatal): %s\", SDL_GetError());\n        snprintf(ui.status, sizeof(ui.status), \"AUDIO UNAVAILABLE: %.130s\", SDL_GetError());\n    } else {\n        diagnostic_log(\"output audio open ok id=%u rate=%d channels=%d\",\n                       (unsigned)device, obtained.freq, obtained.channels);\n        SDL_PauseAudioDevice(device, 0);\n    }\n",
    "output audio result logging",
)

# Log every unmodified 1-key navigation and call the repaired toggle.
text = replace_once(
    text,
    "                    TsUiPanel before = ts_ui_panel(&ui);\n                    static const char *const names[] = {\n",
    "                    TsUiPanel before = ts_ui_panel(&ui);\n                    if (key == SDLK_1)\n                        diagnostic_log(\"KEY1 panel before=%d rec=%d worker=%p\",\n                                       (int)before, record_bank_active,\n                                       (void *)transform.worker);\n                    static const char *const names[] = {\n",
    "KEY1 diagnostic",
)
text = replace_once(
    text,
    "                        (void)toggle_record_bank(window, device, &external_input,\n                                                 &audio, &ui, &instrument,\n                                                 &parked_instrument,\n                                                 &parked_saved_hash,\n                                                 &record_bank_active);\n",
    "                        (void)toggle_record_bank(window, device, &external_input,\n                                                 &audio, &ui, &transform, &instrument,\n                                                 &parked_instrument,\n                                                 &parked_saved_hash,\n                                                 &record_bank_active);\n",
    "toggle call update",
)

# Shutdown: pause capture callback before close, free lazy parked collection, log end.
text = replace_once(
    text,
    "    discard_transform_preview(device, &audio, &ui, &transform);\n    if (input_device) SDL_CloseAudioDevice(input_device);\n",
    "    diagnostic_log(\"shutdown begin rec_active=%d input=%u output=%u\",\n                   record_bank_active, (unsigned)input_device, (unsigned)device);\n    discard_transform_preview(device, &audio, &ui, &transform);\n    if (input_device) {\n        SDL_PauseAudioDevice(input_device, 1);\n        SDL_CloseAudioDevice(input_device);\n    }\n",
    "shutdown input pause",
)
text = replace_once(
    text,
    "    ts_instrument_free(&parked_instrument);\n    ts_instrument_free(&instrument);\n",
    "    if (parked_instrument != NULL) {\n        ts_instrument_free(parked_instrument);\n        free(parked_instrument);\n        parked_instrument = NULL;\n    }\n    ts_instrument_free(&instrument);\n",
    "parked cleanup",
)
text = replace_once(
    text,
    "    SDL_Quit();\n    return 0;\n}\n",
    "    SDL_Quit();\n    diagnostic_log(\"shutdown complete\");\n    diagnostic_log_close();\n    return 0;\n}\n",
    "shutdown diagnostic close",
)

write(path, text)

print("PR35 Windows release-blocker patch applied")
