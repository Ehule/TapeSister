from pathlib import Path

path = Path('src/main_sdl.c')
s = path.read_text(encoding='utf-8')

def replace_once(old, new, label):
    global s
    count = s.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one match, found {count}')
    s = s.replace(old, new, 1)

# Runtime diagnostics are intentionally tiny and append-only so a Windows failure
# leaves useful breadcrumbs without requiring a debugger.
replace_once('#include <stdio.h>\n#include <stdlib.h>', '#include <stdio.h>\n#include <stdarg.h>\n#include <stdlib.h>', 'stdarg include')

replace_once('static const char *config_file_path(void)\n{\n', '''static void diagnostic_log(const char *format, ...)\n{\n    FILE *file = fopen("tapesister-diagnostic.log", "ab");\n    va_list arguments;\n    if (file == NULL) return;\n    fprintf(file, "[runtime] ");\n    va_start(arguments, format);\n    vfprintf(file, format, arguments);\n    va_end(arguments);\n    fputc('\\n', file);\n    fflush(file);\n    fclose(file);\n}\n\nstatic const char *config_file_path(void)\n{\n''', 'diagnostic logger')

# Input devices must open paused. The callback only runs while an actual recorder is armed.
replace_once('''    snprintf(input->device_label, sizeof(input->device_label), "%s",\n             device_name != NULL ? device_name : "SYSTEM DEFAULT");\n    SDL_PauseAudioDevice(*input_device, 0);\n    error[0] = '\\0';\n    return 1;\n''', '''    snprintf(input->device_label, sizeof(input->device_label), "%.127s",\n             device_name != NULL ? device_name : "SYSTEM DEFAULT");\n    diagnostic_log("capture device opened paused: %s rate=%u channels=%d",\n                   input->device_label, input->sample_rate, input->channels);\n    error[0] = '\\0';\n    return 1;\n''', 'leave input paused')

replace_once('''    if (*input_device == 0) {\n        snprintf(error, error_size, "Could not open recording input: %.110s", SDL_GetError());\n        return 0;\n    }\n''', '''    if (*input_device == 0) {\n        snprintf(error, error_size, "Could not open recording input: %.110s", SDL_GetError());\n        diagnostic_log("capture device open failed (nonfatal): %s", error);\n        return 0;\n    }\n''', 'nonfatal input log')

replace_once('''    stop_all_force(output_device, audio, ui);\n    SDL_LockAudioDevice(*input_device);\n    ok = ts_external_recorder_arm(\n''', '''    stop_all_force(output_device, audio, ui);\n    /* SDL starts capture devices paused. Re-pause explicitly before replacing\n       recorder buffers so no callback can observe freed or half-initialized tape. */\n    SDL_PauseAudioDevice(*input_device, 1);\n    SDL_LockAudioDevice(*input_device);\n    ok = ts_external_recorder_arm(\n''', 'arm pause')

replace_once('''    SDL_UnlockAudioDevice(*input_device);\n    if (!ok) {\n        snprintf(ui->status, sizeof(ui->status), "REC ARM FAILED: %.142s", error);\n        return 0;\n    }\n    sync_external_capture_ui(*input_device, input, ui);\n''', '''    SDL_UnlockAudioDevice(*input_device);\n    if (!ok) {\n        snprintf(ui->status, sizeof(ui->status), "REC ARM FAILED: %.142s", error);\n        diagnostic_log("REC arm failed (nonfatal): %s", error);\n        return 0;\n    }\n    SDL_PauseAudioDevice(*input_device, 0);\n    diagnostic_log("REC armed: slot=%d threshold=%d dB", slot + 1,\n                   ui->config.record_threshold_db);\n    sync_external_capture_ui(*input_device, input, ui);\n''', 'arm unpause')

replace_once('''    snprintf(ui->status, sizeof(ui->status),\n             "REC %02d ARMED  %s  CH %d  THRESH %d DB",\n             slot + 1, input->device_label, ui->config.record_input_channel,\n             ui->config.record_threshold_db);\n''', '''    snprintf(ui->status, sizeof(ui->status),\n             "REC %02d ARMED  %.96s  CH %d  THRESH %d DB",\n             slot + 1, input->device_label, ui->config.record_input_channel,\n             ui->config.record_threshold_db);\n''', 'armed status truncation')

replace_once('''static void cancel_external_capture(SDL_AudioDeviceID input_device,\n                                    ExternalInputState *input, TsUiState *ui)\n{\n    if (input_device) SDL_LockAudioDevice(input_device);\n''', '''static void cancel_external_capture(SDL_AudioDeviceID input_device,\n                                    ExternalInputState *input, TsUiState *ui)\n{\n    if (input_device) SDL_PauseAudioDevice(input_device, 1);\n    if (input_device) SDL_LockAudioDevice(input_device);\n''', 'cancel pause')

replace_once('''static void stop_external_capture_early(SDL_AudioDeviceID input_device,\n                                        ExternalInputState *input,\n                                        TsUiState *ui)\n{\n    char error[160];\n    int ok;\n    if (input_device) SDL_LockAudioDevice(input_device);\n''', '''static void stop_external_capture_early(SDL_AudioDeviceID input_device,\n                                        ExternalInputState *input,\n                                        TsUiState *ui)\n{\n    char error[160];\n    int ok;\n    if (input_device) SDL_PauseAudioDevice(input_device, 1);\n    if (input_device) SDL_LockAudioDevice(input_device);\n''', 'manual stop pause')

replace_once('''    if (input->recorder.state != TS_EXTERNAL_CAPTURE_COMPLETED) return;\n    if (*input_device) SDL_LockAudioDevice(*input_device);\n''', '''    if (input->recorder.state != TS_EXTERNAL_CAPTURE_COMPLETED) return;\n    if (*input_device) SDL_PauseAudioDevice(*input_device, 1);\n    if (*input_device) SDL_LockAudioDevice(*input_device);\n''', 'finalize pause')

# Replace the dangerous full-size stack swap with lazy heap parking and a fixed 4 KiB
# byte-swap buffer. The active instrument remains where stable main kept it.
start = s.index('static int toggle_record_bank(')
end = s.index('\nint main(int argc, char **argv)', start)
old_toggle = s[start:end]
new_toggle = r'''static void swap_instrument_storage(TsInstrument *first, TsInstrument *second)
{
    unsigned char scratch[4096];
    unsigned char *left = (unsigned char *)first;
    unsigned char *right = (unsigned char *)second;
    size_t remaining = sizeof(*first);
    while (remaining > 0u) {
        size_t amount = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        memcpy(scratch, left, amount);
        memcpy(left, right, amount);
        memcpy(right, scratch, amount);
        left += amount;
        right += amount;
        remaining -= amount;
    }
}

static int toggle_record_bank(SDL_Window *window,
                              SDL_AudioDeviceID output_device,
                              ExternalInputState *input,
                              AudioState *audio, TsUiState *ui,
                              TsInstrument *instrument,
                              TsInstrument **parked,
                              uint64_t *parked_saved_hash,
                              int *record_bank_active,
                              TransformController *controller)
{
    uint64_t saved;
    diagnostic_log("bank toggle requested active=%d parked=%p", *record_bank_active,
                   (void *)(parked != NULL ? *parked : NULL));
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
        audio->capture.state == TS_CAPTURE_RECORDING || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL RECORDING BEFORE SWITCHING BANKS");
        diagnostic_log("bank toggle deferred: recorder busy");
        return 0;
    }
    if (controller != NULL && controller->worker != NULL) {
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->status, sizeof(ui->status),
                 "CANCELING TRANSFORM - PRESS 1 AGAIN WHEN IT STOPS");
        diagnostic_log("bank toggle deferred: canceling transform worker");
        return 0;
    }
    if (parked == NULL || parked_saved_hash == NULL) {
        snprintf(ui->status, sizeof(ui->status), "REC BANK STORAGE ERROR");
        return 0;
    }
    if (*parked == NULL) {
        *parked = (TsInstrument *)malloc(sizeof(**parked));
        if (*parked == NULL) {
            snprintf(ui->status, sizeof(ui->status),
                     "REC BANK UNAVAILABLE - OUT OF MEMORY");
            diagnostic_log("bank toggle failed: heap allocation of %zu bytes", sizeof(**parked));
            return 0;
        }
        ts_instrument_init(*parked);
        *parked_saved_hash = instrument_state_hash(*parked);
        diagnostic_log("allocated parked REC collection on heap: %zu bytes", sizeof(**parked));
    }

    /* A transform preview may retain pointers into the active collection. Clear all
       preview/controller identity before the collection storage changes. */
    if (controller != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 0;
        controller->quick_apply = 0;
        discard_transform_preview(output_device, audio, ui, controller);
        ui->transform_open = 0;
        ui->transform_rendering = 0;
    }
    stop_all_force(output_device, audio, ui);
    diagnostic_log("bank swap begin");
    swap_instrument_storage(instrument, *parked);
    diagnostic_log("bank swap complete");
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
    diagnostic_log("bank toggle complete active=%d", *record_bank_active);
    return 1;
}
'''
s = s[:start] + new_toggle + s[end:]

replace_once('''    TsInstrument instrument;\n    TsInstrument parked_instrument;\n''', '''    TsInstrument instrument;\n    TsInstrument *parked_instrument = NULL;\n''', 'heap parked declaration')
replace_once('''    int record_bank_active = 0;\n    int running = 1;\n\n    ts_instrument_init(&instrument);\n    ts_instrument_init(&parked_instrument);\n''', '''    int record_bank_active = 0;\n    int diagnostic_bank_stress = argc > 1 &&\n                                 strcmp(argv[1], "--diagnostic-bank-toggle-stress") == 0;\n    int diagnostic_failed = 0;\n    int running = 1;\n\n    diagnostic_log("entered main: TsInstrument=%zu framebuffer=%zu UI=%zu stress=%d",\n                   sizeof(TsInstrument), sizeof(TsFramebuffer), sizeof(TsUiState),\n                   diagnostic_bank_stress);\n    ts_instrument_init(&instrument);\n''', 'main heap init')
replace_once('''    parked_saved_hash = instrument_state_hash(&parked_instrument);\n''', '''    parked_saved_hash = 0u;\n''', 'parked hash lazy')

# Skip the splash in automated stress mode and avoid interpreting the diagnostic flag as a file.
replace_once('''    if (running && !show_splash(renderer)) {\n''', '''    if (running && !diagnostic_bank_stress && !show_splash(renderer)) {\n''', 'stress skip splash')
replace_once('''    if (argc > 1) {\n        load_instrument(device, &audio, &ui, &instrument, argv[1]);\n    } else if (ui.config.startup_welcome_sample) {\n''', '''    if (argc > 1 && !diagnostic_bank_stress) {\n        load_instrument(device, &audio, &ui, &instrument, argv[1]);\n    } else if (!diagnostic_bank_stress && ui.config.startup_welcome_sample) {\n''', 'stress skip file load')

# Updated toggle call includes heap pointer and transform controller.
replace_once('''                        (void)toggle_record_bank(window, device, &external_input,\n                                                 &audio, &ui, &instrument,\n                                                 &parked_instrument,\n                                                 &parked_saved_hash,\n                                                 &record_bank_active);\n''', '''                        (void)toggle_record_bank(window, device, &external_input,\n                                                 &audio, &ui, &instrument,\n                                                 &parked_instrument,\n                                                 &parked_saved_hash,\n                                                 &record_bank_active, &transform);\n''', 'toggle call')

# Run a real storage/navigation stress path in CI with an explicitly missing capture
# device. This exercises the exact toggle function without destructive manual testing.
needle = '''    (void)stage_incoming_exchange(&ui, &exchange_offer, ignored_exchange, 0);\n\n    while (running) {\n'''
insert = '''    (void)stage_incoming_exchange(&ui, &exchange_offer, ignored_exchange, 0);\n\n    if (diagnostic_bank_stress && running) {\n        char original_device[TS_CONFIG_PATH_MAX];\n        snprintf(original_device, sizeof(original_device), "%s",\n                 ui.config.record_input_device);\n        diagnostic_log("starting 2000 bank-toggle stress passes");\n        for (int pass = 0; pass < 2000; ++pass) {\n            if (!toggle_record_bank(window, device, &external_input, &audio, &ui,\n                                    &instrument, &parked_instrument,\n                                    &parked_saved_hash, &record_bank_active,\n                                    &transform)) {\n                diagnostic_log("stress toggle failed at pass %d: %s", pass, ui.status);\n                diagnostic_failed = 1;\n                break;\n            }\n        }\n        if (!diagnostic_failed && !record_bank_active &&\n            !toggle_record_bank(window, device, &external_input, &audio, &ui,\n                                &instrument, &parked_instrument,\n                                &parked_saved_hash, &record_bank_active, &transform))\n            diagnostic_failed = 1;\n        if (!diagnostic_failed) {\n            snprintf(ui.config.record_input_device, sizeof(ui.config.record_input_device),\n                     "__TAPESISTER_INTENTIONALLY_MISSING_CAPTURE_DEVICE__");\n            if (arm_external_capture(device, &input_device, &audio, &external_input,\n                                     &ui, &instrument)) {\n                diagnostic_log("ERROR: intentionally missing capture device opened unexpectedly");\n                cancel_external_capture(input_device, &external_input, &ui);\n                diagnostic_failed = 1;\n            } else {\n                diagnostic_log("missing capture device remained nonfatal: %s", ui.status);\n            }\n            snprintf(ui.config.record_input_device, sizeof(ui.config.record_input_device),\n                     "%s", original_device);\n        }\n        if (!diagnostic_failed && record_bank_active &&\n            !toggle_record_bank(window, device, &external_input, &audio, &ui,\n                                &instrument, &parked_instrument,\n                                &parked_saved_hash, &record_bank_active, &transform))\n            diagnostic_failed = 1;\n        diagnostic_log("bank-toggle stress complete result=%s",\n                       diagnostic_failed ? "FAIL" : "PASS");\n        running = 0;\n    }\n\n    while (running) {\n'''
if s.count(needle) != 1:
    raise SystemExit(f'stress insertion: expected one match, found {s.count(needle)}')
s = s.replace(needle, insert, 1)

# Cleanup heap storage and pause input before close.
replace_once('''    discard_transform_preview(device, &audio, &ui, &transform);\n    if (input_device) SDL_CloseAudioDevice(input_device);\n''', '''    discard_transform_preview(device, &audio, &ui, &transform);\n    if (input_device) SDL_PauseAudioDevice(input_device, 1);\n    if (input_device) SDL_CloseAudioDevice(input_device);\n''', 'shutdown input pause')
replace_once('''    ts_sample_free(&clipboard);\n    ts_instrument_free(&parked_instrument);\n    ts_instrument_free(&instrument);\n''', '''    ts_sample_free(&clipboard);\n    if (parked_instrument != NULL) {\n        ts_instrument_free(parked_instrument);\n        free(parked_instrument);\n        parked_instrument = NULL;\n    }\n    ts_instrument_free(&instrument);\n''', 'heap parked cleanup')
replace_once('''    SDL_Quit();\n    return 0;\n}\n''', '''    SDL_Quit();\n    diagnostic_log("shutdown complete diagnostic_failed=%d", diagnostic_failed);\n    return diagnostic_failed ? 2 : 0;\n}\n''', 'diagnostic exit code')

path.write_text(s, encoding='utf-8')
print('PR35 architectural fix applied')
