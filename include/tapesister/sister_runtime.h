#ifndef TAPESISTER_SISTER_RUNTIME_H
#define TAPESISTER_SISTER_RUNTIME_H

#include "tapesister/capture.h"
#include "tapesister/performance.h"
#include "tapesister/sister_machine.h"
#include "tapesister/sister_wave_snapshot.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TS_SISTER_RUNTIME_PAGE_LIMIT = 1024,
    TS_SISTER_SOURCE_TILES = 1u << 0,
    TS_SISTER_SOURCE_FM = 1u << 1,
    TS_SISTER_SOURCE_EXT = 1u << 2,
    TS_SISTER_SOURCE_PREVIEW = 1u << 3,
    TS_SISTER_SOURCE_COUNT = 4,
    TS_SISTER_SOURCE_ALL = TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM |
                           TS_SISTER_SOURCE_EXT | TS_SISTER_SOURCE_PREVIEW
};

typedef enum {
    TS_SISTER_TAP_MIX = 0,
    TS_SISTER_TAP_H1,
    TS_SISTER_TAP_H2,
    TS_SISTER_TAP_H3,
    TS_SISTER_TAP_COUNT
} TsSisterTap;

typedef enum {
    TS_SISTER_DESTINATION_NONE = 0,
    TS_SISTER_DESTINATION_READY,
    TS_SISTER_DESTINATION_OCCUPIED,
    TS_SISTER_DESTINATION_LOCKED,
    TS_SISTER_DESTINATION_SOURCE_CONFLICT,
    TS_SISTER_DESTINATION_NO_EMPTY,
    TS_SISTER_DESTINATION_STALE
} TsSisterDestinationStatus;

typedef enum {
    TS_SISTER_WARNING_NONE = 0,
    TS_SISTER_WARNING_INPUT_UNAVAILABLE = 1u << 0,
    TS_SISTER_WARNING_DEVICE_CONTRACT = 1u << 1,
    TS_SISTER_WARNING_CALLBACK = 1u << 2,
    TS_SISTER_WARNING_ALLOCATION = 1u << 3
} TsSisterWarning;

typedef enum {
    TS_SISTER_TILE_SHIFT_FAILED = 0,
    TS_SISTER_TILE_SHIFT_COPIED,
    TS_SISTER_TILE_SHIFT_SOURCE_ADDED,
    TS_SISTER_TILE_SHIFT_SOURCE_REMOVED
} TsSisterTileShiftResult;

typedef struct {
    TsStereoFrame fm;
    TsStereoFrame external;
    TsStereoFrame preview;
} TsSisterSourceFrames;

typedef struct {
    TsStereoFrame input;
    TsStereoFrame duck_sidechain;
    TsStereoFrame tap[TS_SISTER_TAP_COUNT];
    /* Complete Sister output: monitored dry input plus processed head MIX. */
    TsStereoFrame monitor_return;
    /* Published for UI/backward compatibility; direct source buses are muted. */
    float dry_monitor_gain;
} TsSisterRuntimeFrame;

typedef struct {
    int enabled;
    int rolling;
    int held;
    int monitor_enabled;
    uint8_t source_switches;
    uint16_t source_mask;
    size_t active_page;
    int active_source_voices;
    TsSisterTap selected_tap;
    TsCaptureState capture_state;
    int capture_destination;
    TsSisterDestinationStatus destination_status;
    float source_input_peak;
    float tap_peak[TS_SISTER_TAP_COUNT];
    uint64_t overload_count;
    uint32_t warnings;
    int source_target_conflict;
    uint64_t processed_frames;
    uint64_t revision;
} TsSisterRoutingSnapshot;

typedef struct {
    atomic_uint_least64_t revision;
    atomic_int enabled;
    atomic_int rolling;
    atomic_int held;
    atomic_int monitor_enabled;
    atomic_uint_least32_t source_switches;
    atomic_uint_least32_t source_mask;
    atomic_uint_least64_t active_page;
    atomic_int active_source_voices;
    atomic_int selected_tap;
    atomic_int capture_state;
    atomic_int capture_destination;
    atomic_int destination_status;
    atomic_uint_least32_t source_input_peak_bits;
    atomic_uint_least32_t tap_peak_bits[TS_SISTER_TAP_COUNT];
    atomic_uint_least64_t overload_count;
    atomic_uint_least32_t warnings;
    atomic_int source_target_conflict;
    atomic_uint_least64_t processed_frames;
} TsSisterRoutingSnapshotAtomic;

typedef struct {
    TsSisterMachine machine;
    TsSisterPostFxEngine post_fx;
    TsSisterParameters parameters;
    TsPerformanceBank performance;
    TsCaptureRecorder capture;
    TsSisterRuntimeFrame last_frame;
    uint16_t page_source_masks[TS_SISTER_RUNTIME_PAGE_LIMIT];
    size_t active_page;
    uint64_t processed_frames;
    uint32_t warnings;
    uint8_t source_switches;
    uint8_t output_channels;
    TsSisterTap selected_tap;
    TsSisterDestinationStatus destination_status;
    uint16_t capture_transient_source_mask;
    int enabled;
    int rolling;
    int held;
    int monitor_enabled;
    int input_available;
    int source_target_conflict;
    int callback_failed;
    int parameters_published;
    float monitor_dry_current;
    float monitor_wet_current;
    TsSisterRamp source_gain[TS_SISTER_SOURCE_COUNT];
    TsSisterRamp ordinary_fx_return_gain;
    float master_feedback_current;
    TsStereoFrame master_feedback_previous;
    char selected_preset[48];
    TsSisterRoutingSnapshotAtomic snapshot;
    TsSisterWavePublisher waveform;
    size_t waveform_capacity_frames;
} TsSisterRuntime;

void ts_sister_runtime_init(TsSisterRuntime *runtime);
void ts_sister_runtime_free(TsSisterRuntime *runtime);
int ts_sister_runtime_enable(TsSisterRuntime *runtime, uint32_t sample_rate,
                             uint8_t output_channels,
                             uint8_t buffer_channels,
                             double duration_seconds,
                             char *error, size_t error_size);
void ts_sister_runtime_disable(TsSisterRuntime *runtime);
int ts_sister_runtime_reconfigure(TsSisterRuntime *runtime,
                                  uint32_t sample_rate,
                                  uint8_t output_channels,
                                  char *error, size_t error_size);
TsStereoFrame ts_sister_runtime_process_ordinary_post_fx(
    TsSisterRuntime *runtime, TsStereoFrame input);
void ts_sister_runtime_set_parameters(TsSisterRuntime *runtime,
                                      const TsSisterParameters *parameters);
void ts_sister_runtime_set_selected_preset(TsSisterRuntime *runtime,
                                           const char *name);
void ts_sister_runtime_set_rolling(TsSisterRuntime *runtime, int rolling);
void ts_sister_runtime_set_hold(TsSisterRuntime *runtime, int held);
void ts_sister_runtime_set_monitor(TsSisterRuntime *runtime, int enabled);
int ts_sister_runtime_request_clear(TsSisterRuntime *runtime);
int ts_sister_runtime_can_clear(const TsSisterRuntime *runtime);
int ts_sister_runtime_perform_clear(TsSisterRuntime *runtime);
void ts_sister_runtime_set_sources(TsSisterRuntime *runtime,
                                   uint8_t source_switches);
uint8_t ts_sister_runtime_sources(const TsSisterRuntime *runtime);
int ts_sister_runtime_owns_direct_tile_bus(const TsSisterRuntime *runtime);

int ts_sister_runtime_set_page(TsSisterRuntime *runtime, size_t page,
                               const TsInstrument *instrument);
uint16_t ts_sister_runtime_source_mask(const TsSisterRuntime *runtime);
TsSisterTileShiftResult ts_sister_runtime_shift_sample_tile(
    TsSisterRuntime *runtime, TsInstrument *instrument, int slot,
    char *status, size_t status_size);
int ts_sister_runtime_tiles_insert_active(const TsSisterRuntime *runtime);
int ts_sister_runtime_set_source_slot(TsSisterRuntime *runtime,
                                      const TsInstrument *instrument,
                                      int slot, int selected);
int ts_sister_runtime_toggle_source_slot(TsSisterRuntime *runtime,
                                         const TsInstrument *instrument,
                                         int slot);
int ts_sister_runtime_replace_source_slot(TsSisterRuntime *runtime,
                                          const TsInstrument *instrument,
                                          int slot);
void ts_sister_runtime_clear_source_mask(TsSisterRuntime *runtime);
uint16_t ts_sister_runtime_validate_source_mask(
    TsSisterRuntime *runtime, const TsInstrument *instrument);
void ts_sister_runtime_prepare_slot_replacement(TsSisterRuntime *runtime,
                                                int slot);
void ts_sister_runtime_sync_sources(TsSisterRuntime *runtime,
                                    const TsInstrument *instrument,
                                    int output_rate);

int ts_sister_runtime_note_on(TsSisterRuntime *runtime,
                              const TsInstrument *instrument,
                              const TsNoteEvent *event, int latched,
                              int output_rate);
void ts_sister_runtime_note_off(TsSisterRuntime *runtime,
                                const TsNoteEvent *event);
void ts_sister_runtime_release_midi_channel(TsSisterRuntime *runtime,
                                            int channel);
void ts_sister_runtime_panic(TsSisterRuntime *runtime);

TsSisterRuntimeFrame ts_sister_runtime_process_frame(
    TsSisterRuntime *runtime, const TsSisterSourceFrames *sources);
void ts_sister_runtime_process_block(TsSisterRuntime *runtime,
                                     const TsSisterSourceFrames *sources,
                                     TsSisterRuntimeFrame *output,
                                     size_t frames);

int ts_sister_runtime_find_destination(const TsSisterRuntime *runtime,
                                       const TsInstrument *instrument,
                                       int preferred_slot);
int ts_sister_runtime_arm_capture(TsSisterRuntime *runtime,
                                  const TsInstrument *instrument,
                                  int destination_slot,
                                  size_t capacity_frames,
                                  uint32_t sample_rate, uint8_t channels,
                                  TsSisterTap tap,
                                  uint16_t transient_capture_sources,
                                  char *error, size_t error_size);
int ts_sister_runtime_arm_overdub(TsSisterRuntime *runtime,
                                  const TsInstrument *instrument,
                                  int destination_slot,
                                  size_t capacity_frames,
                                  uint32_t sample_rate, TsSisterTap tap,
                                  uint16_t transient_capture_sources,
                                  char *error, size_t error_size);
int ts_sister_runtime_trigger_capture(TsSisterRuntime *runtime,
                                      char *error, size_t error_size);
int ts_sister_runtime_stop_capture(TsSisterRuntime *runtime,
                                   char *error, size_t error_size);
int ts_sister_runtime_cancel_capture(TsSisterRuntime *runtime);
int ts_sister_runtime_commit_capture(TsSisterRuntime *runtime,
                                     TsInstrument *instrument,
                                     int auto_resize,
                                     char *error, size_t error_size);

void ts_sister_runtime_input_available(TsSisterRuntime *runtime,
                                       int available);
void ts_sister_runtime_project_close(TsSisterRuntime *runtime);
void ts_sister_runtime_fail_silent(TsSisterRuntime *runtime,
                                   uint32_t warning);
int ts_sister_runtime_get_snapshot(const TsSisterRuntime *runtime,
                                   TsSisterRoutingSnapshot *snapshot);
int ts_sister_runtime_get_wave_snapshot(const TsSisterRuntime *runtime,
                                        TsSisterWaveSnapshot *snapshot);
const char *ts_sister_tap_name(TsSisterTap tap);

#endif
