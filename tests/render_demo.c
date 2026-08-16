#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tapesister-independent-tiles.ppm";
    TsInstrument instrument;
    TsUiState ui;
    TsFramebuffer fb;
    char error[160];
    ts_instrument_init(&instrument);
    ts_ui_init(&ui);
    if (!ts_instrument_generate(&instrument, TS_GENERATOR_METALLIC, 0x54415045u,
                                error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    ts_instrument_set_selection(&instrument, instrument.current.frames / 5,
                                instrument.current.frames * 3 / 5);
    snprintf(ui.status, sizeof(ui.status), "PARENT PRESERVED - CURRENT READY TO SHAPE");
    if (argc > 2 && strcmp(argv[2], "palette") == 0) {
        ui.palette_open = 1;
        ui.palette_entry = TS_PALETTE_WAVE_SELECTION;
        ui.palette_channel = 1;
        snprintf(ui.status, sizeof(ui.status),
                 "LIVE PALETTE - WAVE SELECTION IS TAPESISTER ONLY");
    } else if (argc > 2 && strcmp(argv[2], "config") == 0) {
        ui.config_open = 1;
        ui.config_field = TS_CONFIG_EXCHANGE_PATH;
        snprintf(ui.config.sample_path, sizeof(ui.config.sample_path),
                 "/home/user/Samples");
        snprintf(ui.config.fasttracker_path, sizeof(ui.config.fasttracker_path),
                 "/home/user/FT2-Tapehead/ft2-clone");
        snprintf(ui.config.exchange_path, sizeof(ui.config.exchange_path),
                 "/home/user/Samples/TapeSister-Handoff");
        ui.config_cursor = strlen(ui.config.exchange_path);
        ui.text_cursor_visible = 1;
        snprintf(ui.status, sizeof(ui.status),
                 "CONFIG PATHS ARE SAVED IN TAPESISTER INI");
    } else if (argc > 2 && strcmp(argv[2], "browser") == 0) {
        ts_browser_open(&ui.browser, TS_BROWSER_EXPORT_WAV, "metallic-family-07.wav");
        ui.text_cursor_visible = 1;
        snprintf(ui.browser.directory, sizeof(ui.browser.directory),
                 "/home/user/Samples/TapeSister/Metallic Family");
        ui.browser.entry_count = 18;
        for (int i = 0; i < ui.browser.entry_count; ++i) {
            ui.browser.entries[i].is_directory = i < 3;
            if (i < 3)
                snprintf(ui.browser.entries[i].name, sizeof(ui.browser.entries[i].name),
                         "%s", i == 0 ? "Drones" : i == 1 ? "Percussion" : "Sources");
            else
                snprintf(ui.browser.entries[i].name, sizeof(ui.browser.entries[i].name),
                         "metallic-generation-%02d.wav", i - 2);
        }
        ui.browser.selected = 7;
        ui.browser.scroll = 0;
        snprintf(ui.browser.message, sizeof(ui.browser.message), "18 ITEMS");
    } else if (argc > 2 && strcmp(argv[2], "load-selection") == 0) {
        ui.load_selection_choice_open = 1;
        snprintf(ui.load_selection_name, sizeof(ui.load_selection_name),
                 "glass-harmonic-source.wav");
        snprintf(ui.status, sizeof(ui.status),
                 "CHOOSE PASTE, FIT, OR CANCEL FOR THE SELECTED RANGE");
    } else if (argc > 2 && strcmp(argv[2], "ab") == 0) {
        ui.audition_source = TS_AUDITION_PARENT;
        ui.playback_active = 1;
        ui.playhead_source = TS_AUDITION_PARENT;
        ui.playhead_frame = instrument.parent.frames * 7u / 16u;
        ui.playhead_frames = instrument.parent.frames;
        snprintf(ui.status, sizeof(ui.status), "AUDITIONING PARENT - PLAYING SELECTION");
    } else if (argc > 2 && strcmp(argv[2], "loop") == 0) {
        ts_instrument_set_selection_snapped(&instrument, instrument.current.frames / 5,
                                            instrument.current.frames * 3 / 5);
        if (!ts_instrument_set_loop_from_selection(&instrument, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        ui.fx_page = TS_FX_LOOP;
        ui.playback_active = 1;
        ui.playhead_source = TS_AUDITION_CURRENT;
        ui.playhead_frame = (instrument.loop_first + instrument.loop_last) / 2u;
        ui.playhead_frames = instrument.current.frames;
        ui.active_notes = (1u << 0) | (1u << 4) | (1u << 7);
        snprintf(ui.status, sizeof(ui.status), "LATCHED CHORD 3/5 - DRAG LOOP FLAGS LIVE");
    } else if (argc > 2 && strcmp(argv[2], "bank") == 0) {
        TsProcessRecipe process = instrument.process;
        ts_instrument_set_selection_snapped(&instrument, instrument.current.frames / 5,
                                            instrument.current.frames * 3 / 5);
        if (!ts_instrument_set_loop_from_selection(&instrument, error, sizeof(error)) ||
            !ts_instrument_bank_capture(&instrument, 1, TS_BANK_CAPTURE_CURRENT,
                                        error, sizeof(error)) ||
            !ts_instrument_bank_capture(&instrument, 2, TS_BANK_CAPTURE_SELECTION,
                                        error, sizeof(error)) ||
            !ts_instrument_bank_capture(&instrument, 3, TS_BANK_CAPTURE_LOOP,
                                        error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        process.edge = 0.64f;
        process.drift = 0.37f;
        if (!ts_instrument_set_process(&instrument, &process, error, sizeof(error)) ||
            !ts_instrument_bank_capture(&instrument, 4, TS_BANK_CAPTURE_CURRENT,
                                        error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        if (!ts_instrument_bank_rename(&instrument, 2, "TAIL LAYER",
                                       error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        ui.show_keyboard = 0;
        ui.bank_view_slot = 2;
        ui.playback_active = 1;
        ui.playhead_bank_slot = 2;
        ui.playhead_frame = instrument.bank[2].sample.frames / 2u;
        ui.playhead_frames = instrument.bank[2].sample.frames;
        snprintf(ui.status, sizeof(ui.status),
                 "BANK 03 AUDITION - WAVEFORM FOLLOWS SLOT  RMB RENAME  SHIFT+RMB CLEAR");
    } else if (argc > 2 && strcmp(argv[2], "family") == 0) {
        int child = -1;
        int cousin = -1;
        int path = -1;
        ts_instrument_set_selection(&instrument, instrument.current.frames / 5u,
                                    instrument.current.frames * 4u / 5u);
        if (!ts_instrument_set_loop_from_selection(&instrument, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        instrument.bank[0].has_loop = 1;
        instrument.bank[0].loop_first = instrument.loop_first;
        instrument.bank[0].loop_last = instrument.loop_last;
        instrument.bank[0].loop_mode = TS_LOOP_FORWARD;
        instrument.bank[0].loop_crossfade_ms = instrument.loop_crossfade_ms;
        instrument.family_relation = TS_FAMILY_CHILD;
        instrument.family_mutation = 0.28f;
        instrument.family_locks = TS_FAMILY_LOCK_LOOP |
                                  TS_FAMILY_LOCK_DURATION |
                                  TS_FAMILY_LOCK_PITCH;
        if (!ts_instrument_generate_family_candidate(&instrument, 0, 0, &child,
                                                       error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        instrument.family_relation = TS_FAMILY_COUSIN;
        instrument.family_mutation = 0.68f;
        instrument.family_locks = TS_FAMILY_LOCK_LOOP |
                                  TS_FAMILY_LOCK_PITCH |
                                  TS_FAMILY_LOCK_ENVELOPE;
        if (!ts_instrument_generate_family_candidate(&instrument, 0, 0, &cousin,
                                                       error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        instrument.family_trajectory = 1;
        instrument.family_relation = TS_FAMILY_CHILD;
        if (!ts_instrument_generate_family_candidate(&instrument, cousin, 0, &path,
                                                       error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        ui.fx_page = TS_FX_FAMILY;
        ui.show_keyboard = 0;
        ui.show_recipes = 0;
        ui.bank_view_slot = path;
        ui.playback_active = 1;
        ui.playhead_bank_slot = path;
        ui.playhead_frame = instrument.bank[path].sample.frames / 2u;
        ui.playhead_frames = instrument.bank[path].sample.frames;
        snprintf(ui.status, sizeof(ui.status),
                 "PATH ON  CHILD %02d OF COUSIN %02d  LOCK LOOP PITCH ENV",
                 path + 1, cousin + 1);
    } else if (argc > 2 && strcmp(argv[2], "tape") == 0) {
        size_t source_first = instrument.current.frames / 7u;
        size_t source_last = instrument.current.frames * 3u / 8u;
        size_t loop_first = instrument.current.frames / 2u;
        size_t loop_last = instrument.current.frames * 6u / 7u;
        ts_instrument_set_selection_snapped(&instrument, source_first, source_last);
        source_first = instrument.selection_first;
        source_last = instrument.selection_last;
        ts_instrument_set_selection_snapped(&instrument, loop_first, loop_last);
        if (!ts_instrument_set_loop_from_selection(&instrument, error, sizeof(error)) ||
            !ts_instrument_set_loop_mode(&instrument, TS_LOOP_PING_PONG,
                                         error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        ts_instrument_set_selection(&instrument, source_first, source_last);
        ui.fx_page = TS_FX_LOOP;
        ui.tape_dragging = 1;
        ui.tape_drag_button = 1;
        ui.tape_drag_kind = TS_POST_COPY_MIX;
        ui.tape_source_first = source_first;
        ui.tape_source_last = source_last;
        ui.tape_destination = (int64_t)(instrument.current.frames * 9u / 16u);
        snprintf(ui.status, sizeof(ui.status),
                 "COPY MIX GHOST - RELEASE AT ZERO CROSSING  LOOP PING-PONG");
    } else if (argc > 2 && strcmp(argv[2], "recipe") == 0) {
        TsProcessRecipe process = ui.recipes.slots[7].process;
        if (!ts_instrument_set_process(&instrument, &process, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        ui.fx_page = TS_FX_SHAPE;
        ui.show_keyboard = 0;
        ui.show_recipes = 1;
        ui.recipes.active_slot = 7;
        snprintf(ui.status, sizeof(ui.status),
                 "BROKEN FOLD APPLIED - PARENT PRESERVED  UNDO RESTORES");
    } else if (argc > 2 && strcmp(argv[2], "tuning") == 0) {
        if (!ts_instrument_set_tuning(&instrument, 57, -14.2f,
                                      error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            ts_instrument_free(&instrument);
            return 1;
        }
        ui.fx_page = TS_FX_TUNE;
        ui.has_pitch_suggestion = 1;
        ui.pitch_suggestion.root_note = 58;
        ui.pitch_suggestion.fine_tune_cents = 3.7f;
        ui.pitch_confidence = 0.91f;
        ui.active_notes = (1u << 0) | (1u << 7) | (1u << 12);
        snprintf(ui.status, sizeof(ui.status),
                 "PREVIEW A#3 +3.7 C  CONF 91%% - ACCEPT OR ESC CANCEL");
    }
    ts_ui_render(&fb, &ui, &instrument);
    if (!ts_ui_write_ppm(&fb, path)) {
        fprintf(stderr, "Could not write %s\n", path);
        ts_instrument_free(&instrument);
        return 1;
    }
    ts_instrument_free(&instrument);
    printf("Wrote %s\n", path);
    return 0;
}
