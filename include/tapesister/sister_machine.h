#ifndef TAPESISTER_SISTER_MACHINE_H
#define TAPESISTER_SISTER_MACHINE_H

#include "tapesister/sample.h"
#include "tapesister/sister_effects.h"
#include "tapesister/sister_post_fx.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TS_SISTER_HEAD_COUNT = 3,
    TS_SISTER_RATE_COUNT = 10
};

#define TS_SISTER_DEFAULT_SECONDS 40.0
#define TS_SISTER_MIN_SECONDS 5.0
#define TS_SISTER_MAX_SECONDS 60.0
#define TS_SISTER_HEAD3_MAX_SECONDS 8.0

typedef enum {
    TS_SISTER_FILTER_BYPASS = 0,
    TS_SISTER_FILTER_LOWPASS,
    TS_SISTER_FILTER_HIGHPASS,
    TS_SISTER_FILTER_BANDPASS,
    TS_SISTER_FILTER_NOTCH,
    TS_SISTER_FILTER_PEAK,
    TS_SISTER_FILTER_LOWSHELF,
    TS_SISTER_FILTER_HIGHSHELF,
    TS_SISTER_FILTER_TYPE_COUNT
} TsSisterFilterType;

typedef enum {
    TS_SISTER_DUCK_SAFE = 0,
    TS_SISTER_DUCK_KAFKA_BIAS
} TsSisterDuckMode;

typedef enum {
    TS_SISTER_CLEAR_IDLE = 0,
    TS_SISTER_CLEAR_FADE_OUT,
    TS_SISTER_CLEAR_WAITING,
    TS_SISTER_CLEAR_FADE_IN
} TsSisterClearState;

typedef struct {
    float *data;              /* Interleaved M or L,R. */
    /* Logical musical canvas followed by the fixed physical allocation. */
    size_t capacity_frames;
    size_t storage_frames;
    size_t valid_history_frames;
    uint32_t sample_rate;
    uint8_t channels;
} TsSisterBuffer;

typedef struct {
    float current;
    float target;
    float step;
    uint32_t remaining;
} TsSisterRamp;

typedef struct {
    double phase;
    double previous_offset;
    double old_delay_frames;
    double current_delay_frames;
    double target_delay_frames;
    uint32_t jump_remaining;
    uint32_t jump_total;
    TsStereoFrame previous_read;
    TsStereoFrame guard_from;
    double previous_guard_difference;
    uint32_t guard_remaining;
    uint32_t guard_total;
    int guard_initialized;
    TsSisterRamp level;
    TsSisterRamp offset;
} TsSisterHeadState;

typedef struct {
    uint32_t prng;
    uint64_t next_event_clock;
    float current;
    TsSisterRamp gain;
} TsSisterDropState;

typedef struct {
    float *delay;
    size_t delay_frames;
    size_t write_index;
    float lowpass_state;
    TsSisterRamp enabled_mix;
} TsSisterDecorrelator;

typedef struct {
    float b0, b1, b2, a1, a2;
} TsSisterBiquadCoefficients;

typedef struct {
    float x1, x2, y1, y2;
} TsSisterBiquadState;

typedef struct {
    float head1_level;
    float head1_time_ms;
    float head1_feedback;
    float head2_level;
    float head2_scrub;
    int head2_rate_index;
    float head2_feedback;
    float head3_level;
    float head3_span;
    int head3_rate_index;
    float wow;
    float drop;
    int duck_enabled;
    TsSisterDuckMode duck_mode;
    float duck_sensitivity;
    int decorrelation_enabled;
    float width;
    TsSisterFilterType filter_type;
    float filter_cutoff_hz;
    float filter_q;
    float filter_gain_db;
    /* Global stereo-weave controls. Target bits are mutually exclusive
       between the head group and MIX; multiple head bits are valid. */
    float soak;
    float bleed;
    uint8_t soak_targets;
    /* PR9 shares one visible control set across independent target histories. */
    TsSisterFxControls fx;
    /* Requested logical rolling canvas. The fixed store is always 60 s. */
    float buffer_seconds;
    float headroom;
    /* Fraction of the previous rolling-memory cell erased on each write pass.
       1.0 is full replacement; 0.0 retains the complete previous cell. */
    float write_erase;
    /* Spectral aging applied only to retained old memory. Zero is exact bypass. */
    float ghost_tone;
    /* Pre-tape input trim. Applied before rolling-memory write and Duck. */
    float input_gain;
    /* Per-source trims are applied by the runtime before its established
       multi-source headroom normalization and the master INPUT trim. */
    float tiles_gain;
    float fm_gain;
    float external_gain;
    float preview_gain;
    float monitor_dry;
    float monitor_wet;
    /* Post-filter MIX gain. Final linked safety remains authoritative. */
    float mix_output_gain;
    /* Completed post-effects return trim. Applied before linked safety and
       therefore also owns the explicit Master FX Feedback send level. */
    float fx_return_gain;
    float clear_ms;
} TsSisterParameters;

const char *ts_sister_filter_type_name(TsSisterFilterType type);

typedef struct {
    /* Exact sanitized, input-trimmed frame presented to the tape engine. */
    TsStereoFrame input;
    TsStereoFrame head[TS_SISTER_HEAD_COUNT];
    TsStereoFrame mix;
    /* Completed DISTORTION -> DELAY -> REVERB result before linked hardware
       safety. This is the owned Master FX Feedback tap. */
    TsStereoFrame post_fx;
    /* Exact bounded frame offered to rolling memory this callback. */
    TsStereoFrame write;
    size_t write_position;
    int wrote;
} TsSisterOutput;

typedef struct {
    uint64_t master_clock;
    size_t write_position;
    double head_position[TS_SISTER_HEAD_COUNT];
    float write_normalized;
    float head_normalized[TS_SISTER_HEAD_COUNT];
    int rolling;
    int held;
    TsSisterClearState clear_state;
    float head_peak[TS_SISTER_HEAD_COUNT];
    float mix_peak;
    float duck_gain;
    float wow_seconds;
    float drop_gain[2];
    uint64_t overload_count;
    uint8_t buffer_channels;
    uint32_t sample_rate;
    double duration_seconds;
    double target_duration_seconds;
    int resize_pending;
    uint64_t revision;
} TsSisterSnapshot;

typedef struct {
    atomic_uint_least64_t revision;
    atomic_uint_least64_t master_clock;
    atomic_uint_least64_t write_position;
    atomic_uint_least64_t head_position_bits[TS_SISTER_HEAD_COUNT];
    atomic_uint_least32_t write_normalized_bits;
    atomic_uint_least32_t head_normalized_bits[TS_SISTER_HEAD_COUNT];
    atomic_int rolling;
    atomic_int held;
    atomic_int clear_state;
    atomic_uint_least32_t head_peak_bits[TS_SISTER_HEAD_COUNT];
    atomic_uint_least32_t mix_peak_bits;
    atomic_uint_least32_t duck_gain_bits;
    atomic_uint_least32_t wow_seconds_bits;
    atomic_uint_least32_t drop_gain_bits[2];
    atomic_uint_least64_t overload_count;
    atomic_uint_least32_t buffer_channels;
    atomic_uint_least32_t sample_rate;
    atomic_uint_least64_t duration_bits;
    atomic_uint_least64_t target_duration_bits;
    atomic_int resize_pending;
} TsSisterSnapshotAtomic;

typedef struct {
    TsSisterBuffer buffer;
    TsSisterParameters parameters;
    TsSisterParameters applied_parameters;
    TsSisterHeadState head[TS_SISTER_HEAD_COUNT];
    TsSisterDropState drop[2];
    TsSisterDecorrelator decorrelator[TS_SISTER_HEAD_COUNT];
    TsSisterWeaveState soak_weave[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterBiquadCoefficients filter_current;
    TsSisterBiquadCoefficients filter_target;
    TsSisterBiquadCoefficients filter_step;
    TsSisterBiquadState filter_state[2];
    uint32_t filter_ramp_remaining;
    uint64_t master_clock;
    size_t pending_capacity_frames;
    uint32_t resize_debounce_remaining;
    size_t retained_old_capacity_frames;
    uint32_t retained_resize_remaining;
    uint32_t retained_resize_total;
    int rolling;
    int held;
    TsSisterClearState clear_state;
    TsSisterRamp clear_gain;
    TsSisterRamp write_erase;
    TsSisterRamp ghost_tone;
    TsSisterRamp input_gain;
    TsSisterRamp mix_output_gain;
    TsSisterRamp fx_return_gain;
    TsSisterRamp feedback[2];
    TsSisterRamp wow_amount;
    TsSisterRamp drop_amount;
    TsSisterRamp duck_sensitivity;
    TsSisterRamp width;
    TsSisterRamp headroom;
    float ghost_lowpass_state[2];
    uint32_t wow_prng;
    uint64_t wow_next_event_clock;
    float wow_target;
    float wow_state;
    float duck_energy;
    float duck_gain;
    float dc_input_x1[2];
    float dc_input_y1[2];
    uint64_t overload_count;
    TsSisterOutput last_output;
    double last_head_position[TS_SISTER_HEAD_COUNT];
    TsSisterSnapshotAtomic snapshot;
} TsSisterMachine;

void ts_sister_parameters_default(TsSisterParameters *parameters,
                                  uint32_t sample_rate);
void ts_sister_parameters_kafka_start(TsSisterParameters *parameters,
                                      uint32_t sample_rate);
void ts_sister_parameters_sanitize(TsSisterParameters *parameters,
                                   uint32_t sample_rate);
float ts_sister_ghost_cutoff_hz(float amount, uint32_t sample_rate);
double ts_sister_rate_value(int index);
double ts_sister_positive_modulo(double value, size_t modulus);

int ts_sister_buffer_init(TsSisterBuffer *buffer, uint32_t sample_rate,
                          uint8_t channels, double duration_seconds);
void ts_sister_buffer_free(TsSisterBuffer *buffer);
void ts_sister_buffer_clear(TsSisterBuffer *buffer);
TsStereoFrame ts_sister_buffer_read(const TsSisterBuffer *buffer,
                                    double frame_position);
int ts_sister_buffer_write(TsSisterBuffer *buffer, size_t frame,
                           TsStereoFrame value);

int ts_sister_machine_init(TsSisterMachine *machine, uint32_t sample_rate,
                           uint8_t channels, double duration_seconds);
void ts_sister_machine_free(TsSisterMachine *machine);
int ts_sister_machine_reconfigure(TsSisterMachine *machine,
                                  uint32_t sample_rate, uint8_t channels,
                                  double duration_seconds);
int ts_sister_machine_request_duration(TsSisterMachine *machine,
                                       double duration_seconds);
void ts_sister_machine_reset(TsSisterMachine *machine);
void ts_sister_machine_set_parameters(TsSisterMachine *machine,
                                      const TsSisterParameters *parameters);
void ts_sister_machine_seed(TsSisterMachine *machine, uint32_t seed);
void ts_sister_machine_set_rolling(TsSisterMachine *machine, int rolling);
void ts_sister_machine_set_hold(TsSisterMachine *machine, int held);

int ts_sister_machine_request_clear(TsSisterMachine *machine);
int ts_sister_machine_can_clear(const TsSisterMachine *machine);
int ts_sister_machine_perform_clear(TsSisterMachine *machine);
int ts_sister_machine_clear_offline(TsSisterMachine *machine);

TsSisterOutput ts_sister_machine_process_frame(TsSisterMachine *machine,
                                               TsStereoFrame input,
                                               TsStereoFrame duck_sidechain);
TsSisterOutput ts_sister_machine_process_frame_with_fx(
    TsSisterMachine *machine, TsSisterPostFxEngine *post_fx,
    TsStereoFrame input, TsStereoFrame duck_sidechain,
    TsStereoFrame causal_fx_return);
void ts_sister_machine_process_block(TsSisterMachine *machine,
                                     const TsStereoFrame *input,
                                     const TsStereoFrame *duck_sidechain,
                                     TsSisterOutput *output,
                                     size_t frames);
int ts_sister_machine_get_snapshot(const TsSisterMachine *machine,
                                   TsSisterSnapshot *snapshot);

#endif
