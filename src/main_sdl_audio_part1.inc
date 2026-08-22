/*
 * TapeSister SDL audio-device Config integration.
 *
 * This translation unit wraps main_sdl.c with the Config-driven SDL device
 * selection layer. Runtime recording behavior remains in main_sdl.c.
 */
#include "tapesister/audio_config.h"
#include "tapesister/ui.h"
#include "tapesister/capture.h"
#include "tapesister/performance.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_AudioDeviceID ts_audio_open_device(const char *device, int iscapture,
                                               const SDL_AudioSpec *desired,
                                               SDL_AudioSpec *obtained,
                                               int allowed_changes);
static void ts_audio_close_device(SDL_AudioDeviceID device);
static void ts_audio_lock_device(SDL_AudioDeviceID device);
static void ts_audio_unlock_device(SDL_AudioDeviceID device);
static void ts_audio_pause_device(SDL_AudioDeviceID device, int pause_on);
static void ts_audio_quit(void);
static int ts_audio_main_config_load(TsConfig *config, const char *path,
                                     char *error, size_t error_size);
static int ts_audio_main_config_save(const TsConfig *config, const char *path,
                                     char *error, size_t error_size);
static void ts_audio_ui_render(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument);
static int ts_audio_config_field_from_point(int x, int y);
static size_t ts_audio_config_cursor_from_point(const TsUiState *ui,
                                                TsConfigField field, int x);
static int ts_audio_external_recorder_arm(TsExternalRecorder *recorder,
                                          int destination_slot,
                                          uint32_t sample_rate,
                                          int threshold_db,
                                          int pre_roll_ms,
                                          int silence_ms,
                                          int tail_ms,
                                          int max_seconds,
                                          char *error, size_t error_size);

/* Multi-source Capture is intentionally injected at the existing note-event
 * boundary. QWERTY still produces the same note events; a later MIDI PR can
 * feed this path without learning anything about keyboard scan codes. */
static TsUiBankAction ts_multi_bank_action(int right_button, unsigned modifiers);
static int ts_multi_capture_arm(TsCaptureRecorder *recorder, int destination_slot,
                                size_t capacity_frames, uint32_t sample_rate,
                                char *error, size_t error_size);
static int ts_multi_capture_set_source(TsCaptureRecorder *recorder, int source_slot,
                                       char *error, size_t error_size);
static int ts_multi_capture_trigger(TsCaptureRecorder *recorder,
                                    char *error, size_t error_size);
static int ts_multi_capture_cancel(TsCaptureRecorder *recorder);
static int ts_multi_capture_stop(TsCaptureRecorder *recorder,
                                 char *error, size_t error_size);
static int ts_multi_capture_write_sample(TsCaptureRecorder *recorder, float sample);
static int ts_multi_capture_source_matches(const TsCaptureRecorder *recorder,
                                           int selected_slot);
static TsNoteStartResult ts_multi_note_start(TsNoteBank *bank,
                                             const TsInstrument *instrument,
                                             const TsTuning *tuning,
                                             TsAuditionSource source, int note,
                                             int keyboard_base_note,
                                             int latched, int output_rate);
static int ts_multi_staged_start(TsNoteBank *bank,
                                 const TsInstrument *instrument,
                                 const TsTuning *tuning,
                                 TsAuditionSource source,
                                 uint32_t staged_notes,
                                 int keyboard_base_note,
                                 int output_rate);
static void ts_multi_note_release(TsNoteBank *bank, int note);
static void ts_multi_note_clear(TsNoteBank *bank);
static float ts_multi_note_read_split(TsNoteBank *bank, float *synth_output);
static void ts_multi_note_sync(TsNoteBank *bank, const TsInstrument *instrument,
                               int output_rate);

#define SDL_OpenAudioDevice ts_audio_open_device
#define SDL_CloseAudioDevice ts_audio_close_device
#define SDL_LockAudioDevice ts_audio_lock_device
#define SDL_UnlockAudioDevice ts_audio_unlock_device
#define SDL_PauseAudioDevice ts_audio_pause_device
#define SDL_Quit ts_audio_quit
#define ts_config_load ts_audio_main_config_load
#define ts_config_save ts_audio_main_config_save
#define ts_ui_render ts_audio_ui_render
#define ts_ui_config_field_from_point ts_audio_config_field_from_point
#define ts_ui_config_cursor_from_point ts_audio_config_cursor_from_point
#define ts_external_recorder_arm ts_audio_external_recorder_arm
#define ts_ui_bank_action ts_multi_bank_action
#define ts_capture_arm ts_multi_capture_arm
#define ts_capture_set_source ts_multi_capture_set_source
#define ts_capture_trigger ts_multi_capture_trigger
#define ts_capture_cancel ts_multi_capture_cancel
#define ts_capture_stop ts_multi_capture_stop
#define ts_capture_write_sample ts_multi_capture_write_sample
#define TS_CAPTURE_SOURCE_MATCHES(recorder, selected_slot) \
    ts_multi_capture_source_matches((recorder), (selected_slot))
#define ts_note_bank_start_tuned_at ts_multi_note_start
#define ts_note_bank_start_staged_chord ts_multi_staged_start
#define ts_note_bank_release ts_multi_note_release
#define ts_note_bank_clear ts_multi_note_clear
#define ts_note_bank_read_split ts_multi_note_read_split
#define ts_note_bank_sync ts_multi_note_sync

#include "main_sdl.c"

#undef SDL_OpenAudioDevice
#undef SDL_CloseAudioDevice
#undef SDL_LockAudioDevice
#undef SDL_UnlockAudioDevice
#undef SDL_PauseAudioDevice
#undef SDL_Quit
#undef ts_config_load
#undef ts_config_save
#undef ts_ui_render
#undef ts_ui_config_field_from_point
#undef ts_ui_config_cursor_from_point
#undef ts_external_recorder_arm
#undef ts_ui_bank_action
#undef ts_capture_arm
#undef ts_capture_set_source
#undef ts_capture_trigger
#undef ts_capture_cancel
#undef ts_capture_stop
#undef ts_capture_write_sample
#undef TS_CAPTURE_SOURCE_MATCHES
#undef ts_note_bank_start_tuned_at
#undef ts_note_bank_start_staged_chord
#undef ts_note_bank_release
#undef ts_note_bank_clear
#undef ts_note_bank_read_split
#undef ts_note_bank_sync

static TsCaptureRecorder *ts_multi_capture_recorder;
static TsPerformanceBank ts_multi_performance;
static uint16_t ts_multi_source_mask;
static float ts_multi_last_raw_mix;
static int ts_multi_shift_pending;
static int ts_multi_group_latched;
static int ts_multi_capture_scaled;
static int ts_multi_capture_requires_scaling;
static int ts_multi_capture_trigger_ready;
static int ts_multi_preserve_release_tails;
static int ts_locked_tile_dim_percent = 38;
static int ts_last_sample_page = -1;
static TsUiState *ts_live_ui;

typedef struct {
    int x0, y0, x1, y1;
    int valid;
} TsTileBounds;
static TsTileBounds ts_tile_bounds[TS_BANK_SLOT_COUNT];
static int ts_tile_bounds_ready;

static int ts_first_source(uint16_t mask)
{
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        if ((mask & (uint16_t)(1u << slot)) != 0u) return slot;
    return -1;
}

static void ts_multi_clear_group(void)
{
    ts_multi_source_mask = 0u;
    ts_multi_shift_pending = 0;
    ts_multi_group_latched = 0;
    ts_multi_last_raw_mix = 0.0f;
    ts_multi_capture_trigger_ready = 0;
    ts_multi_preserve_release_tails = 0;
    ts_performance_clear(&ts_multi_performance);
}

static int ts_multi_capture_source_matches(const TsCaptureRecorder *recorder,
                                           int selected_slot)
{
    if (recorder == NULL) return 0;
    if (ts_multi_group_latched && ts_multi_source_mask != 0u)
        return 1;
    return recorder->source_slot == selected_slot;
}

static TsUiBankAction ts_multi_bank_action(int right_button, unsigned modifiers)
{
    if (!right_button && ts_multi_capture_recorder != NULL &&
        (ts_multi_capture_recorder->state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
         ts_multi_capture_recorder->state == TS_CAPTURE_RECORDING) &&
        modifiers == TS_UI_BANK_MOD_SHIFT) {
        ts_multi_shift_pending = 1;
        return TS_UI_BANK_ACTION_AUDITION;
    }
    ts_multi_shift_pending = 0;
    return ts_ui_bank_action(right_button, modifiers);
}

static int ts_multi_capture_arm(TsCaptureRecorder *recorder, int destination_slot,
                                size_t capacity_frames, uint32_t sample_rate,
                                char *error, size_t error_size)
{
    int ok;
    ts_multi_capture_recorder = recorder;
    ts_multi_clear_group();
    ts_multi_capture_scaled = 0;
    ts_multi_capture_requires_scaling = 0;
    ok = ts_capture_arm(recorder, destination_slot, capacity_frames, sample_rate,
                        error, error_size);
    if (!ok) ts_multi_capture_recorder = NULL;
    return ok;
}

static int ts_multi_capture_set_source(TsCaptureRecorder *recorder, int source_slot,
                                       char *error, size_t error_size)
{
    int had_group;
    if (recorder == NULL || source_slot == TS_CAPTURE_SOURCE_SYNTH) {
        ts_multi_clear_group();
        return ts_capture_set_source(recorder, source_slot, error, error_size);
    }
    ts_multi_capture_recorder = recorder;
    ts_multi_capture_trigger_ready = 0;
    had_group = ts_multi_group_latched && ts_multi_source_mask != 0u;
    if (ts_multi_shift_pending) {
        uint16_t bit;
        int removing;
        ts_multi_shift_pending = 0;
        if (recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
            recorder->state != TS_CAPTURE_RECORDING) {
            snprintf(error, error_size, "Capture is not accepting source tiles");
            return 0;
        }
        if (source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT) {
            snprintf(error, error_size, "Invalid Capture source tile");
            return 0;
        }
        if (source_slot == recorder->destination_slot) {
            snprintf(error, error_size, "Capture target cannot join its source group");
            return 0;
        }
        bit = (uint16_t)(1u << source_slot);
        removing = (ts_multi_source_mask & bit) != 0u;
        if (removing)
            ts_multi_source_mask &= (uint16_t)~bit;
        else
            ts_multi_source_mask |= bit;
        ts_multi_group_latched = ts_multi_source_mask != 0u;
        /* The mask is authoritative for note fan-out. source_slot mirrors its
           first member for existing UI state; provenance survives an empty
           live group so the completed take still has a valid parent tile. */
        recorder->source_slot = ts_first_source(ts_multi_source_mask);
        if (recorder->source_slot >= 0)
            recorder->provenance_slot = recorder->source_slot;
        if (removing)
            ts_performance_release_sources_after_pass(&ts_multi_performance, bit);
        if (error != NULL && error_size > 0u) error[0] = '\0';
        return 1;
    }

    ts_multi_source_mask = (source_slot >= 0 && source_slot < TS_BANK_SLOT_COUNT) ?
                           (uint16_t)(1u << source_slot) : 0u;
    ts_multi_group_latched = 0;
    if (had_group) {
        ts_performance_release_after_pass(&ts_multi_performance);
        /* Normal audition clears the shared note bank before restarting the
           clicked tile; a nonpersistent workbench loop clears once more while
           standing down. Preserve the released tails through those transition
           clears. Persistent Loop Lock refreshes in place and clears neither. */
        if (source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT)
            ts_multi_preserve_release_tails = 1;
        else if (ts_live_ui != NULL && ts_live_ui->workbench_loop_persistent)
            ts_multi_preserve_release_tails = 0;
        else if (ts_live_ui != NULL && ts_live_ui->workbench_loop_active)
            ts_multi_preserve_release_tails = 2;
        else
            ts_multi_preserve_release_tails = 1;
    } else {
        ts_performance_clear(&ts_multi_performance);
        ts_multi_preserve_release_tails = 0;
    }
    if (!ts_capture_set_source(recorder, source_slot, error, error_size)) return 0;
    /* A plain click after a latched multi-source group intentionally falls
       through to main_sdl.c's established audition branch. That sounds the
       clicked tile at unity and, if Capture is armed, starts recording. */
    return had_group || recorder->state == TS_CAPTURE_RECORDING ? 0 : 1;
}

static int ts_multi_capture_trigger(TsCaptureRecorder *recorder,
                                    char *error, size_t error_size)
{
    int ready = ts_multi_capture_trigger_ready;
    int result;
    ts_multi_capture_trigger_ready = 0;
    if (ts_multi_group_latched && ts_multi_source_mask != 0u && !ready) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size,
                     "Play a QWERTY note or staged chord to start the source group");
        return 0;
    }
    result = ts_capture_trigger(recorder, error, error_size);
    return result;
}

static void ts_multi_scale_capture_if_needed(TsCaptureRecorder *recorder)
{
    if (recorder == NULL || ts_multi_capture_scaled ||
        !ts_multi_capture_requires_scaling || recorder->recorded_frames == 0u) return;
    (void)ts_performance_peak_scale(recorder->buffer,
                                    recorder->recorded_frames, 0.98f);
    ts_multi_capture_scaled = 1;
}

static int ts_multi_capture_cancel(TsCaptureRecorder *recorder)
{
    int result = ts_capture_cancel(recorder);
    if (result) ts_multi_clear_group();
    return result;
}

static int ts_multi_capture_stop(TsCaptureRecorder *recorder,
                                 char *error, size_t error_size)
{
    int result = ts_capture_stop(recorder, error, error_size);
    if (result) ts_multi_scale_capture_if_needed(recorder);
    return result;
}

static int ts_multi_capture_write_sample(TsCaptureRecorder *recorder, float sample)
{
    int completed;
    if (ts_multi_group_latched && recorder == ts_multi_capture_recorder &&
        recorder != NULL && recorder->state == TS_CAPTURE_RECORDING) {
        sample = ts_multi_last_raw_mix;
        ts_multi_capture_requires_scaling = 1;
    }
    completed = ts_capture_write_sample(recorder, sample);
    if (completed) ts_multi_scale_capture_if_needed(recorder);
    return completed;
}

static TsNoteStartResult ts_multi_note_start(TsNoteBank *bank,
                                             const TsInstrument *instrument,
                                             const TsTuning *tuning,
                                             TsAuditionSource source, int note,
                                             int keyboard_base_note,
                                             int latched, int output_rate)
{
    int started;
    if (!ts_multi_group_latched || ts_multi_source_mask == 0u)
        return ts_note_bank_start_tuned_at(bank, instrument, tuning, source, note,
                                           keyboard_base_note, latched, output_rate);
    started = ts_performance_trigger_group(&ts_multi_performance, instrument,
                                           ts_multi_source_mask, note,
                                           keyboard_base_note, latched,
                                           output_rate);
    ts_multi_capture_trigger_ready =
        started > 0 && ts_performance_count(&ts_multi_performance) > 0;
    return started > 0 ? TS_NOTE_STARTED : TS_NOTE_START_FAILED;
}

static int ts_multi_staged_start(TsNoteBank *bank,
                                 const TsInstrument *instrument,
                                 const TsTuning *tuning,
                                 TsAuditionSource source,
                                 uint32_t staged_notes,
                                 int keyboard_base_note,
                                 int output_rate)
{
    if (!ts_multi_group_latched || ts_multi_source_mask == 0u)
        return ts_note_bank_start_staged_chord(bank, instrument, tuning, source,
                                               staged_notes, keyboard_base_note,
                                               output_rate);
    ts_note_bank_clear(bank);
    {
        int started = ts_performance_trigger_staged(
            &ts_multi_performance, instrument, ts_multi_source_mask,
            staged_notes, keyboard_base_note, output_rate);
        ts_multi_capture_trigger_ready = started > 0;
        return started;
    }
}

static void ts_multi_note_release(TsNoteBank *bank, int note)
{
    ts_note_bank_release(bank, note);
    ts_performance_release(&ts_multi_performance, note);
}

static void ts_multi_note_clear(TsNoteBank *bank)
{
    ts_note_bank_clear(bank);
    ts_multi_capture_trigger_ready = 0;
    if (ts_multi_preserve_release_tails > 0)
        --ts_multi_preserve_release_tails;
    else {
        ts_performance_clear(&ts_multi_performance);
        ts_multi_last_raw_mix = 0.0f;
    }
}

static float ts_multi_note_read_split(TsNoteBank *bank, float *synth_output)
{
    float legacy_synth = 0.0f;
    float legacy = ts_note_bank_read_split(bank, &legacy_synth);
    float performance = ts_performance_read(&ts_multi_performance,
                                            &ts_multi_last_raw_mix);
    if (synth_output != NULL) *synth_output = legacy_synth;
    return legacy + performance;
}

static void ts_multi_note_sync(TsNoteBank *bank, const TsInstrument *instrument,
                               int output_rate)
{
    ts_note_bank_sync(bank, instrument, output_rate);
    if (ts_performance_count(&ts_multi_performance) > 0)
        ts_performance_sync(&ts_multi_performance, instrument, output_rate);
}

enum {
    TS_AUDIO_CONFIG_PATH_Y = 58,
    TS_AUDIO_CONFIG_PATH_STEP = 19,
    TS_AUDIO_CONFIG_ROW_H = 16,
    TS_AUDIO_CONFIG_VALUE_X = 188,
    TS_AUDIO_CONFIG_INPUT_Y = 136,
    TS_AUDIO_CONFIG_OUTPUT_Y = 155,
    TS_AUDIO_CONFIG_INPUT_W = 430,
    TS_AUDIO_CONFIG_CHANNEL_X = 455,
    TS_AUDIO_CONFIG_CHANNEL_W = 165
};

#define TS_AUDIO_LOGICAL_OUTPUT ((SDL_AudioDeviceID)0x7fff0001u)
#define TS_AUDIO_LOGICAL_CAPTURE ((SDL_AudioDeviceID)0x7fff0002u)

typedef struct {
    char **input_names;
    int input_count;
    char **output_names;
    int output_count;
} TsAudioDeviceCatalog;

static SDL_AudioDeviceID ts_real_output;
static SDL_AudioDeviceID ts_real_capture;
static SDL_AudioSpec ts_output_desired;
static SDL_AudioSpec ts_capture_desired;
static SDL_AudioSpec *ts_output_obtained_target;
static int ts_output_allowed_changes;
static int ts_capture_allowed_changes;
static void *ts_output_userdata;
static void *ts_capture_userdata;
static TsConfig *ts_live_config;
static TsAudioDeviceCatalog ts_device_catalog;
static int ts_config_was_open;
static int ts_capture_reconfigure_pending;
static char ts_active_output[TS_CONFIG_PATH_MAX];
static char ts_active_capture[TS_CONFIG_PATH_MAX];
static char ts_pending_notice[160];
static char ts_capture_reconfigure_error[160];

static void ts_copy_name(char *destination, size_t size, const char *source)
{
    size_t length;
    if (destination == NULL || size == 0) return;
    if (source == NULL) source = "";
    length = strlen(source);
    if (length >= size) length = size - 1u;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static const char *ts_configured_output_name(void)
{
    return ts_live_config != NULL && ts_live_config->audio_output_device[0] != '\0' ?
           ts_live_config->audio_output_device : NULL;
}

static const char *ts_configured_input_name(void)
{
    return ts_live_config != NULL && ts_live_config->record_input_device[0] != '\0' ?
           ts_live_config->record_input_device : NULL;
}

static SDL_AudioDeviceID ts_audio_real_id(SDL_AudioDeviceID logical)
{
    if (logical == TS_AUDIO_LOGICAL_OUTPUT) return ts_real_output;
    if (logical == TS_AUDIO_LOGICAL_CAPTURE) return ts_real_capture;
    return logical;
}

static char *ts_audio_name_copy(const char *name)
{
    size_t length;
    char *copy;
    if (name == NULL) return NULL;
    length = strlen(name);
    copy = (char *)malloc(length + 1u);
    if (copy != NULL) memcpy(copy, name, length + 1u);
    return copy;
}

static void ts_audio_name_list_free(char ***names, int *count)
{
    if (names != NULL && *names != NULL) {
        for (int i = 0; i < *count; ++i) free((*names)[i]);
        free(*names);
        *names = NULL;
    }
    if (count != NULL) *count = 0;
}

static int ts_audio_name_list_refresh(int capture, char ***names, int *count)
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
            made[kept] = ts_audio_name_copy(name);
            if (made[kept] == NULL) {
                for (int j = 0; j < kept; ++j) free(made[j]);
                free(made);
                return 0;
            }
            ++kept;
        }
    }
    ts_audio_name_list_free(names, count);
    *names = made;
    *count = kept;
    return 1;
}

static int ts_parse_locked_tile_dim(const char *path)
{
    FILE *file;
    char line[256];
    int value = 38;
    if (path == NULL || path[0] == '\0') return value;
    file = fopen(path, "rb");
    if (file == NULL) return value;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = line;
        char *equals;
        while (*text != '\0' && isspace((unsigned char)*text)) ++text;
        if (strncmp(text, "locked_tile_dim_percent", 23u) != 0) continue;
        equals = strchr(text, '=');
        if (equals == NULL) continue;
        value = atoi(equals + 1);
        if (value < 0) value = 0;
        if (value > 90) value = 90;
        break;
    }
    fclose(file);
    return value;
}
