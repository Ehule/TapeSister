#include "tapesister/sister_machine.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double sister_rates[TS_SISTER_RATE_COUNT] = {
    -2.0, -4.0 / 3.0, -1.0, -2.0 / 3.0, -0.5,
     0.5,  2.0 / 3.0,  1.0,  4.0 / 3.0,  2.0
};

static void publish_snapshot(TsSisterMachine *machine);

static float clampf(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static double clampd(double value, double minimum, double maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float frame_peak(TsStereoFrame frame)
{
    float left = fabsf(frame.l);
    float right = fabsf(frame.r);
    return left > right ? left : right;
}

static TsStereoFrame frame_scale(TsStereoFrame frame, float gain)
{
    TsStereoFrame result = {frame.l * gain, frame.r * gain};
    return result;
}

static TsStereoFrame frame_add(TsStereoFrame a, TsStereoFrame b)
{
    TsStereoFrame result = {a.l + b.l, a.r + b.r};
    return result;
}

static TsStereoFrame frame_lerp(TsStereoFrame a, TsStereoFrame b, float amount)
{
    TsStereoFrame result = {
        a.l + (b.l - a.l) * amount,
        a.r + (b.r - a.r) * amount
    };
    return result;
}

static uint32_t milliseconds_frames(uint32_t sample_rate, float milliseconds)
{
    double frames = (double)sample_rate * (double)milliseconds / 1000.0;
    if (frames < 1.0) return 1u;
    if (frames > (double)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)ceil(frames);
}

static void ramp_reset(TsSisterRamp *ramp, float value)
{
    if (ramp == NULL) return;
    ramp->current = value;
    ramp->target = value;
    ramp->step = 0.0f;
    ramp->remaining = 0u;
}

static void ramp_set(TsSisterRamp *ramp, float target, uint32_t frames)
{
    if (ramp == NULL) return;
    if (!isfinite(target)) target = 0.0f;
    ramp->target = target;
    if (frames == 0u || fabsf(target - ramp->current) <= FLT_EPSILON) {
        ramp->current = target;
        ramp->step = 0.0f;
        ramp->remaining = 0u;
        return;
    }
    ramp->step = (target - ramp->current) / (float)frames;
    ramp->remaining = frames;
}

static float ramp_advance(TsSisterRamp *ramp)
{
    if (ramp == NULL) return 0.0f;
    if (ramp->remaining > 0u) {
        ramp->current += ramp->step;
        --ramp->remaining;
        if (ramp->remaining == 0u) ramp->current = ramp->target;
    }
    return ramp->current;
}

static uint32_t prng_next(uint32_t *state)
{
    uint32_t value;
    if (state == NULL) return 0u;
    value = *state;
    if (value == 0u) value = UINT32_C(0x6d2b79f5);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static float prng_unit_open(uint32_t *state)
{
    return ((float)(prng_next(state) >> 8) + 0.5f) / 16777216.0f;
}

static float prng_signed(uint32_t *state)
{
    return prng_unit_open(state) * 2.0f - 1.0f;
}

static float prng_gaussian(uint32_t *state)
{
    float u1 = prng_unit_open(state);
    float u2 = prng_unit_open(state);
    return sqrtf(-2.0f * logf(u1)) * cosf((float)(2.0 * M_PI) * u2);
}

static uint32_t float_bits(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static float bits_float(uint32_t value)
{
    union { float f; uint32_t u; } bits;
    bits.u = value;
    return bits.f;
}

static uint64_t double_bits(double value)
{
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

static double bits_double(uint64_t value)
{
    union { double d; uint64_t u; } bits;
    bits.u = value;
    return bits.d;
}

double ts_sister_rate_value(int index)
{
    if (index < 0) index = 0;
    if (index >= TS_SISTER_RATE_COUNT) index = TS_SISTER_RATE_COUNT - 1;
    return sister_rates[index];
}

double ts_sister_positive_modulo(double value, size_t modulus)
{
    double result;
    if (modulus == 0u || !isfinite(value)) return 0.0;
    result = fmod(value, (double)modulus);
    if (result < 0.0) result += (double)modulus;
    if (result >= (double)modulus) result = 0.0;
    return result;
}

void ts_sister_parameters_default(TsSisterParameters *parameters,
                                  uint32_t sample_rate)
{
    if (parameters == NULL) return;
    memset(parameters, 0, sizeof(*parameters));
    parameters->head1_level = 0.45f;
    parameters->head1_time_ms = 500.0f;
    parameters->head1_feedback = 0.25f;
    parameters->head2_scrub = 0.5f;
    parameters->head2_rate_index = 7;
    parameters->head3_span = 0.5f;
    parameters->head3_rate_index = 7;
    parameters->width = 1.0f;
    parameters->filter_type = TS_SISTER_FILTER_BYPASS;
    parameters->filter_cutoff_hz = sample_rate > 0u ? (float)sample_rate * 0.45f : 20000.0f;
    parameters->filter_q = 0.70710678f;
    parameters->headroom = 0.5f;
    parameters->write_erase = 1.0f;
    parameters->monitor_dry = 1.0f;
    parameters->monitor_wet = 1.0f;
    parameters->mix_output_gain = 1.0f;
    parameters->clear_ms = 20.0f;
}

const char *ts_sister_filter_type_name(TsSisterFilterType type)
{
    switch (type) {
    case TS_SISTER_FILTER_LOWPASS: return "LP";
    case TS_SISTER_FILTER_HIGHPASS: return "HP";
    case TS_SISTER_FILTER_BANDPASS: return "BP";
    case TS_SISTER_FILTER_NOTCH: return "NOTCH";
    case TS_SISTER_FILTER_PEAK: return "PEAK";
    case TS_SISTER_FILTER_LOWSHELF: return "LOW SH";
    case TS_SISTER_FILTER_HIGHSHELF: return "HIGH SH";
    default: return "OFF";
    }
}

void ts_sister_parameters_kafka_start(TsSisterParameters *parameters,
                                      uint32_t sample_rate)
{
    ts_sister_parameters_default(parameters, sample_rate);
    if (parameters == NULL) return;
    parameters->filter_type = TS_SISTER_FILTER_LOWPASS;
    parameters->filter_cutoff_hz = 9479.2939453125f;
    parameters->filter_gain_db = 2.0f;
    parameters->filter_q = 0.90030003f;
}

int ts_sister_buffer_init(TsSisterBuffer *buffer, uint32_t sample_rate,
                          uint8_t channels, double duration_seconds)
{
    double frame_count;
    size_t capacity;
    size_t scalar_count;
    if (buffer == NULL || sample_rate == 0u ||
        !ts_sample_valid_channels(channels) ||
        !isfinite(duration_seconds) || duration_seconds <= 0.0 ||
        duration_seconds > TS_SISTER_MAX_SECONDS) return 0;
    frame_count = ceil((double)sample_rate * duration_seconds);
    if (frame_count < 2.0 || frame_count > (double)SIZE_MAX) return 0;
    capacity = (size_t)frame_count;
    if (!ts_sample_dimensions(capacity, channels, &scalar_count, NULL)) return 0;
    memset(buffer, 0, sizeof(*buffer));
    buffer->data = calloc(scalar_count, sizeof(*buffer->data));
    if (buffer->data == NULL) return 0;
    buffer->capacity_frames = capacity;
    buffer->sample_rate = sample_rate;
    buffer->channels = channels;
    return 1;
}

void ts_sister_buffer_free(TsSisterBuffer *buffer)
{
    if (buffer == NULL) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

void ts_sister_buffer_clear(TsSisterBuffer *buffer)
{
    size_t scalar_count;
    if (buffer == NULL || buffer->data == NULL ||
        !ts_sample_dimensions(buffer->capacity_frames, buffer->channels,
                              &scalar_count, NULL)) return;
    memset(buffer->data, 0, scalar_count * sizeof(*buffer->data));
}

TsStereoFrame ts_sister_buffer_read(const TsSisterBuffer *buffer,
                                    double frame_position)
{
    TsStereoFrame silence = {0.0f, 0.0f};
    TsStereoFrame first;
    TsStereoFrame second;
    size_t at;
    size_t next;
    float fraction;
    double wrapped;
    if (buffer == NULL || buffer->data == NULL ||
        buffer->capacity_frames == 0u ||
        !ts_sample_valid_channels(buffer->channels)) return silence;
    wrapped = ts_sister_positive_modulo(frame_position, buffer->capacity_frames);
    at = (size_t)floor(wrapped);
    next = at + 1u < buffer->capacity_frames ? at + 1u : 0u;
    fraction = (float)(wrapped - (double)at);
    if (buffer->channels == 1u) {
        first = ts_stereo_frame_from_mono(buffer->data[at]);
        second = ts_stereo_frame_from_mono(buffer->data[next]);
    } else {
        first.l = buffer->data[at * 2u];
        first.r = buffer->data[at * 2u + 1u];
        second.l = buffer->data[next * 2u];
        second.r = buffer->data[next * 2u + 1u];
    }
    return ts_stereo_frame_sanitize(frame_lerp(first, second, fraction));
}

int ts_sister_buffer_write(TsSisterBuffer *buffer, size_t frame,
                           TsStereoFrame value)
{
    if (buffer == NULL || buffer->data == NULL ||
        frame >= buffer->capacity_frames ||
        !ts_sample_valid_channels(buffer->channels)) return 0;
    value = ts_stereo_frame_sanitize(value);
    if (buffer->channels == 1u) {
        buffer->data[frame] = ts_stereo_frame_fold_mono(value);
    } else {
        buffer->data[frame * 2u] = value.l;
        buffer->data[frame * 2u + 1u] = value.r;
    }
    return 1;
}

static void snapshot_atomic_init(TsSisterSnapshotAtomic *snapshot)
{
    size_t i;
    atomic_init(&snapshot->revision, 0u);
    atomic_init(&snapshot->master_clock, 0u);
    atomic_init(&snapshot->write_position, 0u);
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        atomic_init(&snapshot->head_position_bits[i], double_bits(0.0));
        atomic_init(&snapshot->head_normalized_bits[i], float_bits(0.0f));
        atomic_init(&snapshot->head_peak_bits[i], float_bits(0.0f));
    }
    atomic_init(&snapshot->write_normalized_bits, float_bits(0.0f));
    atomic_init(&snapshot->rolling, 1);
    atomic_init(&snapshot->held, 0);
    atomic_init(&snapshot->clear_state, TS_SISTER_CLEAR_IDLE);
    atomic_init(&snapshot->mix_peak_bits, float_bits(0.0f));
    atomic_init(&snapshot->duck_gain_bits, float_bits(1.0f));
    atomic_init(&snapshot->wow_seconds_bits, float_bits(0.0f));
    atomic_init(&snapshot->drop_gain_bits[0], float_bits(1.0f));
    atomic_init(&snapshot->drop_gain_bits[1], float_bits(1.0f));
    atomic_init(&snapshot->overload_count, 0u);
    atomic_init(&snapshot->buffer_channels, 0u);
    atomic_init(&snapshot->sample_rate, 0u);
    atomic_init(&snapshot->duration_bits, double_bits(0.0));
}

static int allocate_decorrelators(TsSisterDecorrelator decorrelator[TS_SISTER_HEAD_COUNT],
                                  uint32_t sample_rate)
{
    size_t delay_frames = (size_t)ceil((double)sample_rate * 0.020);
    size_t i;
    if (delay_frames == 0u) delay_frames = 1u;
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        memset(&decorrelator[i], 0, sizeof(decorrelator[i]));
        decorrelator[i].delay = calloc(delay_frames, sizeof(float));
        if (decorrelator[i].delay == NULL) {
            while (i > 0u) free(decorrelator[--i].delay);
            return 0;
        }
        decorrelator[i].delay_frames = delay_frames;
    }
    return 1;
}

static void free_decorrelators(TsSisterDecorrelator decorrelator[TS_SISTER_HEAD_COUNT])
{
    size_t i;
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        free(decorrelator[i].delay);
        memset(&decorrelator[i], 0, sizeof(decorrelator[i]));
    }
}

static TsSisterBiquadCoefficients biquad_identity(void)
{
    TsSisterBiquadCoefficients coefficients = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    return coefficients;
}

static int coefficients_finite(TsSisterBiquadCoefficients c)
{
    return isfinite(c.b0) && isfinite(c.b1) && isfinite(c.b2) &&
           isfinite(c.a1) && isfinite(c.a2);
}

static TsSisterBiquadCoefficients calculate_biquad(const TsSisterParameters *parameters,
                                                   uint32_t sample_rate)
{
    TsSisterBiquadCoefficients result = biquad_identity();
    double cutoff;
    double q;
    double gain;
    double a;
    double omega;
    double cosine;
    double sine;
    double alpha;
    double beta;
    double b0, b1, b2, a0, a1, a2;
    if (parameters == NULL || sample_rate == 0u ||
        parameters->filter_type == TS_SISTER_FILTER_BYPASS) return result;
    cutoff = clampd(parameters->filter_cutoff_hz, 10.0,
                    (double)sample_rate * 0.45);
    q = clampd(parameters->filter_q, 0.1, 20.0);
    gain = clampd(parameters->filter_gain_db, -24.0, 24.0);
    a = pow(10.0, gain / 40.0);
    omega = 2.0 * M_PI * cutoff / (double)sample_rate;
    cosine = cos(omega);
    sine = sin(omega);
    alpha = sine / (2.0 * q);
    beta = 2.0 * sqrt(a) * alpha;
    b0 = 1.0; b1 = 0.0; b2 = 0.0; a0 = 1.0; a1 = 0.0; a2 = 0.0;
    switch (parameters->filter_type) {
        case TS_SISTER_FILTER_LOWPASS:
            b0 = (1.0 - cosine) * 0.5;
            b1 = 1.0 - cosine;
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case TS_SISTER_FILTER_HIGHPASS:
            b0 = (1.0 + cosine) * 0.5;
            b1 = -(1.0 + cosine);
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case TS_SISTER_FILTER_BANDPASS:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case TS_SISTER_FILTER_NOTCH:
            b0 = 1.0;
            b1 = -2.0 * cosine;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case TS_SISTER_FILTER_PEAK:
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * cosine;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha / a;
            break;
        case TS_SISTER_FILTER_LOWSHELF:
            b0 = a * ((a + 1.0) - (a - 1.0) * cosine + beta);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosine);
            b2 = a * ((a + 1.0) - (a - 1.0) * cosine - beta);
            a0 = (a + 1.0) + (a - 1.0) * cosine + beta;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosine);
            a2 = (a + 1.0) + (a - 1.0) * cosine - beta;
            break;
        case TS_SISTER_FILTER_HIGHSHELF:
            b0 = a * ((a + 1.0) + (a - 1.0) * cosine + beta);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine);
            b2 = a * ((a + 1.0) + (a - 1.0) * cosine - beta);
            a0 = (a + 1.0) - (a - 1.0) * cosine + beta;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosine);
            a2 = (a + 1.0) - (a - 1.0) * cosine - beta;
            break;
        default:
            return result;
    }
    if (!isfinite(a0) || fabs(a0) < DBL_EPSILON) return result;
    result.b0 = (float)(b0 / a0);
    result.b1 = (float)(b1 / a0);
    result.b2 = (float)(b2 / a0);
    result.a1 = (float)(a1 / a0);
    result.a2 = (float)(a2 / a0);
    return coefficients_finite(result) ? result : biquad_identity();
}

static TsSisterParameters sanitize_parameters(const TsSisterMachine *machine,
                                              const TsSisterParameters *input)
{
    TsSisterParameters result;
    uint32_t sample_rate = machine != NULL ? machine->buffer.sample_rate : 48000u;
    if (input == NULL) {
        ts_sister_parameters_default(&result, sample_rate);
        return result;
    }
    result = *input;
    result.head1_level = clampf(result.head1_level, 0.0f, 1.0f);
    result.head1_time_ms = clampf(result.head1_time_ms, 0.0f, 4000.0f);
    result.head1_feedback = clampf(result.head1_feedback, 0.0f, 1.0f);
    result.head2_level = clampf(result.head2_level, 0.0f, 1.0f);
    result.head2_scrub = clampf(result.head2_scrub, 0.0f, 1.0f);
    if (result.head2_rate_index < 0) result.head2_rate_index = 0;
    if (result.head2_rate_index >= TS_SISTER_RATE_COUNT)
        result.head2_rate_index = TS_SISTER_RATE_COUNT - 1;
    result.head2_feedback = clampf(result.head2_feedback, 0.0f, 1.0f);
    result.head3_level = clampf(result.head3_level, 0.0f, 1.0f);
    result.head3_span = clampf(result.head3_span, 0.0f, 1.0f);
    if (result.head3_rate_index < 0) result.head3_rate_index = 0;
    if (result.head3_rate_index >= TS_SISTER_RATE_COUNT)
        result.head3_rate_index = TS_SISTER_RATE_COUNT - 1;
    result.wow = clampf(result.wow, 0.0f, 10.0f);
    result.drop = clampf(result.drop, 0.0f, 100.0f);
    result.duck_enabled = result.duck_enabled != 0;
    if (result.duck_mode != TS_SISTER_DUCK_KAFKA_BIAS)
        result.duck_mode = TS_SISTER_DUCK_SAFE;
    result.duck_sensitivity = clampf(result.duck_sensitivity, 0.0f, 1.0f);
    result.decorrelation_enabled = result.decorrelation_enabled != 0;
    result.width = clampf(result.width, 0.0f, 1.0f);
    if (result.filter_type < TS_SISTER_FILTER_BYPASS ||
        result.filter_type >= TS_SISTER_FILTER_TYPE_COUNT)
        result.filter_type = TS_SISTER_FILTER_BYPASS;
    result.filter_cutoff_hz = clampf(result.filter_cutoff_hz, 10.0f,
                                      (float)sample_rate * 0.45f);
    result.filter_q = clampf(result.filter_q, 0.1f, 20.0f);
    result.filter_gain_db = clampf(result.filter_gain_db, -24.0f, 24.0f);
    result.headroom = clampf(result.headroom, 0.05f, 1.0f);
    result.write_erase = clampf(result.write_erase, 0.0f, 1.0f);
    result.monitor_dry = clampf(result.monitor_dry, 0.0f, 1.0f);
    result.monitor_wet = clampf(result.monitor_wet, 0.0f, 1.0f);
    result.mix_output_gain = clampf(result.mix_output_gain, 0.0f, 4.0f);
    result.clear_ms = clampf(result.clear_ms, 1.0f, 200.0f);
    return result;
}

static double head3_max_frames(const TsSisterMachine *machine)
{
    double maximum = (double)machine->buffer.sample_rate * TS_SISTER_HEAD3_MAX_SECONDS;
    if (maximum > (double)machine->buffer.capacity_frames - 1.0)
        maximum = (double)machine->buffer.capacity_frames - 1.0;
    if (maximum < 1.0) maximum = 1.0;
    return maximum;
}

static void reset_runtime_state(TsSisterMachine *machine, int clear_buffer,
                                int reset_clock)
{
    size_t i;
    double h1_delay;
    double h2_offset;
    double h3_offset;
    if (machine == NULL || machine->buffer.data == NULL) return;
    if (clear_buffer) ts_sister_buffer_clear(&machine->buffer);
    if (reset_clock) machine->master_clock = 0u;
    h1_delay = clampd((double)machine->parameters.head1_time_ms *
                      (double)machine->buffer.sample_rate / 1000.0,
                      1.0, (double)machine->buffer.capacity_frames - 1.0);
    h2_offset = (double)machine->parameters.head2_scrub *
                (double)(machine->buffer.capacity_frames - 1u);
    h3_offset = (double)machine->parameters.head3_span * head3_max_frames(machine);
    memset(machine->head, 0, sizeof(machine->head));
    machine->head[0].current_delay_frames = h1_delay;
    machine->head[0].target_delay_frames = h1_delay;
    machine->head[0].old_delay_frames = h1_delay;
    ramp_reset(&machine->head[0].level, machine->parameters.head1_level);
    ramp_reset(&machine->head[0].offset, (float)h1_delay);
    machine->head[0].phase = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.capacity_frames) - h1_delay,
        machine->buffer.capacity_frames);
    ramp_reset(&machine->head[1].level, machine->parameters.head2_level);
    ramp_reset(&machine->head[1].offset, (float)h2_offset);
    machine->head[1].previous_offset = h2_offset;
    machine->head[1].phase = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.capacity_frames) - h2_offset,
        machine->buffer.capacity_frames);
    ramp_reset(&machine->head[2].level, machine->parameters.head3_level);
    ramp_reset(&machine->head[2].offset, (float)h3_offset);
    machine->head[2].previous_offset = h3_offset;
    machine->head[2].phase = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.capacity_frames) - h3_offset,
        machine->buffer.capacity_frames);
    memset(machine->drop, 0, sizeof(machine->drop));
    for (i = 0u; i < 2u; ++i) {
        machine->drop[i].prng = UINT32_C(0xa341316c) ^ (uint32_t)(i * UINT32_C(0x9e3779b9));
        machine->drop[i].current = 1.0f;
        ramp_reset(&machine->drop[i].gain, 1.0f);
    }
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        TsSisterDecorrelator *decor = &machine->decorrelator[i];
        if (decor->delay != NULL)
            memset(decor->delay, 0, decor->delay_frames * sizeof(*decor->delay));
        decor->write_index = 0u;
        decor->lowpass_state = 0.0f;
        ramp_reset(&decor->enabled_mix,
                   machine->parameters.decorrelation_enabled ? 1.0f : 0.0f);
        machine->last_head_position[i] = machine->head[i].phase;
    }
    machine->wow_prng = UINT32_C(0x243f6a88);
    machine->wow_next_event_clock = 0u;
    machine->wow_target = 0.0f;
    machine->wow_state = 0.0f;
    machine->duck_energy = 0.0f;
    machine->duck_gain = 1.0f;
    memset(machine->dc_input_x1, 0, sizeof(machine->dc_input_x1));
    memset(machine->dc_input_y1, 0, sizeof(machine->dc_input_y1));
    memset(machine->filter_state, 0, sizeof(machine->filter_state));
    machine->filter_current = calculate_biquad(&machine->parameters,
                                                machine->buffer.sample_rate);
    machine->filter_target = machine->filter_current;
    memset(&machine->filter_step, 0, sizeof(machine->filter_step));
    machine->filter_ramp_remaining = 0u;
    machine->clear_state = TS_SISTER_CLEAR_IDLE;
    ramp_reset(&machine->clear_gain, 1.0f);
    ramp_reset(&machine->write_erase, machine->parameters.write_erase);
    memset(&machine->last_output, 0, sizeof(machine->last_output));
    machine->overload_count = 0u;
    machine->applied_parameters = machine->parameters;
}

int ts_sister_machine_init(TsSisterMachine *machine, uint32_t sample_rate,
                           uint8_t channels, double duration_seconds)
{
    if (machine == NULL) return 0;
    memset(machine, 0, sizeof(*machine));
    snapshot_atomic_init(&machine->snapshot);
    if (!ts_sister_buffer_init(&machine->buffer, sample_rate, channels,
                               duration_seconds)) return 0;
    if (!allocate_decorrelators(machine->decorrelator, sample_rate)) {
        ts_sister_buffer_free(&machine->buffer);
        return 0;
    }
    ts_sister_parameters_default(&machine->parameters, sample_rate);
    machine->rolling = 1;
    machine->held = 0;
    reset_runtime_state(machine, 1, 1);
    publish_snapshot(machine);
    return 1;
}

void ts_sister_machine_free(TsSisterMachine *machine)
{
    if (machine == NULL) return;
    ts_sister_buffer_free(&machine->buffer);
    free_decorrelators(machine->decorrelator);
    memset(machine, 0, sizeof(*machine));
}

int ts_sister_machine_reconfigure(TsSisterMachine *machine,
                                  uint32_t sample_rate, uint8_t channels,
                                  double duration_seconds)
{
    TsSisterBuffer replacement;
    TsSisterDecorrelator replacement_decor[TS_SISTER_HEAD_COUNT];
    TsSisterParameters parameters;
    int rolling;
    int held;
    if (machine == NULL) return 0;
    memset(&replacement, 0, sizeof(replacement));
    memset(replacement_decor, 0, sizeof(replacement_decor));
    if (!ts_sister_buffer_init(&replacement, sample_rate, channels,
                               duration_seconds)) return 0;
    if (!allocate_decorrelators(replacement_decor, sample_rate)) {
        ts_sister_buffer_free(&replacement);
        return 0;
    }
    parameters = machine->parameters;
    rolling = machine->rolling;
    held = machine->held;
    ts_sister_buffer_free(&machine->buffer);
    free_decorrelators(machine->decorrelator);
    machine->buffer = replacement;
    memcpy(machine->decorrelator, replacement_decor, sizeof(replacement_decor));
    machine->parameters = sanitize_parameters(machine, &parameters);
    machine->rolling = rolling;
    machine->held = held;
    reset_runtime_state(machine, 1, 1);
    machine->rolling = rolling;
    machine->held = held;
    publish_snapshot(machine);
    return 1;
}

void ts_sister_machine_reset(TsSisterMachine *machine)
{
    if (machine == NULL) return;
    machine->rolling = 1;
    machine->held = 0;
    reset_runtime_state(machine, 1, 1);
    publish_snapshot(machine);
}

void ts_sister_machine_set_parameters(TsSisterMachine *machine,
                                      const TsSisterParameters *parameters)
{
    TsSisterParameters next;
    TsSisterBiquadCoefficients target;
    uint32_t level_frames;
    uint32_t offset_frames;
    uint32_t filter_frames;
    double target_delay;
    double jump_threshold;
    size_t i;
    if (machine == NULL || machine->buffer.data == NULL || parameters == NULL) return;
    next = sanitize_parameters(machine, parameters);
    level_frames = milliseconds_frames(machine->buffer.sample_rate, 50.0f);
    offset_frames = milliseconds_frames(machine->buffer.sample_rate, 50.0f);
    filter_frames = milliseconds_frames(machine->buffer.sample_rate, 20.0f);
    ramp_set(&machine->head[0].level, next.head1_level, level_frames);
    ramp_set(&machine->head[1].level, next.head2_level, level_frames);
    ramp_set(&machine->head[2].level, next.head3_level, level_frames);
    ramp_set(&machine->write_erase, next.write_erase, level_frames);
    ramp_set(&machine->head[1].offset,
             next.head2_scrub * (float)(machine->buffer.capacity_frames - 1u),
             offset_frames);
    ramp_set(&machine->head[2].offset,
             next.head3_span * (float)head3_max_frames(machine), offset_frames);
    target_delay = clampd((double)next.head1_time_ms *
                          (double)machine->buffer.sample_rate / 1000.0,
                          1.0, (double)machine->buffer.capacity_frames - 1.0);
    jump_threshold = (double)machine->buffer.sample_rate * 0.005;
    if (fabs(target_delay - machine->head[0].target_delay_frames) > jump_threshold) {
        machine->head[0].old_delay_frames = machine->head[0].current_delay_frames;
        machine->head[0].current_delay_frames = target_delay;
        machine->head[0].jump_total = milliseconds_frames(machine->buffer.sample_rate, 15.0f);
        machine->head[0].jump_remaining = machine->head[0].jump_total;
    }
    machine->head[0].target_delay_frames = target_delay;
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i)
        ramp_set(&machine->decorrelator[i].enabled_mix,
                 next.decorrelation_enabled ? 1.0f : 0.0f,
                 milliseconds_frames(machine->buffer.sample_rate, 20.0f));
    target = calculate_biquad(&next, machine->buffer.sample_rate);
    machine->filter_target = target;
#define SET_FILTER_STEP(field) \
    machine->filter_step.field = (target.field - machine->filter_current.field) / \
                                 (float)filter_frames
    SET_FILTER_STEP(b0);
    SET_FILTER_STEP(b1);
    SET_FILTER_STEP(b2);
    SET_FILTER_STEP(a1);
    SET_FILTER_STEP(a2);
#undef SET_FILTER_STEP
    machine->filter_ramp_remaining = filter_frames;
    machine->parameters = next;
    machine->applied_parameters = next;
}

void ts_sister_machine_seed(TsSisterMachine *machine, uint32_t seed)
{
    if (machine == NULL) return;
    if (seed == 0u) seed = UINT32_C(0x6d2b79f5);
    machine->wow_prng = seed ^ UINT32_C(0x243f6a88);
    machine->drop[0].prng = seed ^ UINT32_C(0xa341316c);
    machine->drop[1].prng = seed ^ UINT32_C(0xc8013ea4);
    machine->wow_next_event_clock = machine->master_clock;
    machine->drop[0].next_event_clock = machine->master_clock;
    machine->drop[1].next_event_clock = machine->master_clock;
}

void ts_sister_machine_set_rolling(TsSisterMachine *machine, int rolling)
{
    if (machine != NULL) machine->rolling = rolling != 0;
}

void ts_sister_machine_set_hold(TsSisterMachine *machine, int held)
{
    if (machine != NULL) machine->held = held != 0;
}

int ts_sister_machine_request_clear(TsSisterMachine *machine)
{
    if (machine == NULL || machine->buffer.data == NULL ||
        machine->clear_state != TS_SISTER_CLEAR_IDLE) return 0;
    machine->clear_state = TS_SISTER_CLEAR_FADE_OUT;
    ramp_set(&machine->clear_gain, 0.0f,
             milliseconds_frames(machine->buffer.sample_rate,
                                 machine->parameters.clear_ms));
    return 1;
}

int ts_sister_machine_can_clear(const TsSisterMachine *machine)
{
    return machine != NULL && machine->clear_state == TS_SISTER_CLEAR_WAITING;
}

int ts_sister_machine_perform_clear(TsSisterMachine *machine)
{
    size_t i;
    if (!ts_sister_machine_can_clear(machine)) return 0;
    ts_sister_buffer_clear(&machine->buffer);
    memset(machine->dc_input_x1, 0, sizeof(machine->dc_input_x1));
    memset(machine->dc_input_y1, 0, sizeof(machine->dc_input_y1));
    memset(machine->filter_state, 0, sizeof(machine->filter_state));
    machine->filter_current = machine->filter_target;
    machine->filter_ramp_remaining = 0u;
    machine->duck_energy = 0.0f;
    machine->duck_gain = 1.0f;
    machine->wow_target = 0.0f;
    machine->wow_state = 0.0f;
    machine->wow_next_event_clock = machine->master_clock;
    for (i = 0u; i < 2u; ++i) {
        machine->drop[i].current = 1.0f;
        machine->drop[i].next_event_clock = machine->master_clock;
        ramp_reset(&machine->drop[i].gain, 1.0f);
    }
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        TsSisterDecorrelator *decor = &machine->decorrelator[i];
        if (decor->delay != NULL)
            memset(decor->delay, 0, decor->delay_frames * sizeof(*decor->delay));
        decor->write_index = 0u;
        decor->lowpass_state = 0.0f;
        machine->head[i].guard_initialized = 0;
        machine->head[i].guard_remaining = 0u;
    }
    machine->head[0].jump_remaining = 0u;
    machine->head[0].current_delay_frames =
        machine->head[0].target_delay_frames;
    machine->head[1].phase = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.capacity_frames) -
        machine->head[1].offset.current, machine->buffer.capacity_frames);
    machine->head[1].previous_offset = machine->head[1].offset.current;
    machine->head[2].phase = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.capacity_frames) -
        machine->head[2].offset.current, machine->buffer.capacity_frames);
    machine->head[2].previous_offset = machine->head[2].offset.current;
    machine->clear_state = TS_SISTER_CLEAR_FADE_IN;
    ramp_reset(&machine->clear_gain, 0.0f);
    ramp_set(&machine->clear_gain, 1.0f,
             milliseconds_frames(machine->buffer.sample_rate,
                                 machine->parameters.clear_ms));
    publish_snapshot(machine);
    return 1;
}

static TsStereoFrame process_internal(TsSisterMachine *machine,
                                      TsStereoFrame input,
                                      TsStereoFrame duck_sidechain,
                                      TsSisterOutput *result);

int ts_sister_machine_clear_offline(TsSisterMachine *machine)
{
    TsStereoFrame silence = {0.0f, 0.0f};
    TsSisterOutput ignored;
    size_t guard = 0u;
    size_t maximum;
    if (!ts_sister_machine_request_clear(machine)) return 0;
    maximum = (size_t)machine->buffer.sample_rate + 16u;
    while (!ts_sister_machine_can_clear(machine) && guard++ < maximum)
        process_internal(machine, silence, silence, &ignored);
    if (!ts_sister_machine_perform_clear(machine)) return 0;
    guard = 0u;
    while (machine->clear_state != TS_SISTER_CLEAR_IDLE && guard++ < maximum)
        process_internal(machine, silence, silence, &ignored);
    publish_snapshot(machine);
    return machine->clear_state == TS_SISTER_CLEAR_IDLE;
}

static double guard_read_position(double position, size_t write_position,
                                  size_t capacity)
{
    double wrapped = ts_sister_positive_modulo(position, capacity);
    double difference = wrapped - (double)write_position;
    if (difference > (double)capacity * 0.5) difference -= (double)capacity;
    if (difference < -(double)capacity * 0.5) difference += (double)capacity;
    if (fabs(difference) < 1.0)
        wrapped = ts_sister_positive_modulo((double)write_position - 1.0, capacity);
    return wrapped;
}

static double signed_write_distance(double position, size_t write_position,
                                    size_t capacity)
{
    double wrapped = ts_sister_positive_modulo(position, capacity);
    double difference = wrapped - (double)write_position;
    if (difference > (double)capacity * 0.5) difference -= (double)capacity;
    if (difference < -(double)capacity * 0.5) difference += (double)capacity;
    return difference;
}

static TsStereoFrame read_with_write_guard(TsSisterMachine *machine,
                                           TsSisterHeadState *head,
                                           double position,
                                           size_t write_position)
{
    double difference = signed_write_distance(position, write_position,
                                               machine->buffer.capacity_frames);
    double guarded = guard_read_position(position, write_position,
                                         machine->buffer.capacity_frames);
    TsStereoFrame current = ts_sister_buffer_read(&machine->buffer, guarded);
    TsStereoFrame result = current;
    if (head->guard_initialized &&
        difference * head->previous_guard_difference < 0.0 &&
        fabs(difference) < 4.0 && fabs(head->previous_guard_difference) < 4.0) {
        head->guard_from = head->previous_read;
        head->guard_total = milliseconds_frames(machine->buffer.sample_rate, 10.0f);
        head->guard_remaining = head->guard_total;
    }
    if (head->guard_remaining > 0u) {
        float amount = 1.0f - (float)head->guard_remaining /
                                  (float)head->guard_total;
        result = frame_lerp(head->guard_from, current, amount);
        --head->guard_remaining;
    }
    head->previous_read = result;
    head->previous_guard_difference = difference;
    head->guard_initialized = 1;
    return result;
}

static float update_wow(TsSisterMachine *machine)
{
    uint64_t interval;
    float coefficient;
    float amount;
    if (machine->parameters.wow <= 0.0f) {
        machine->wow_state = 0.0f;
        machine->wow_target = 0.0f;
        machine->wow_next_event_clock = machine->master_clock;
        return 0.0f;
    }
    interval = machine->buffer.sample_rate / 10u;
    if (interval == 0u) interval = 1u;
    if (machine->master_clock >= machine->wow_next_event_clock) {
        machine->wow_target = prng_signed(&machine->wow_prng);
        machine->wow_next_event_clock = machine->master_clock + interval;
    }
    coefficient = 1.0f - expf((float)(-2.0 * M_PI * 2.0) /
                              (float)machine->buffer.sample_rate);
    machine->wow_state += (machine->wow_target - machine->wow_state) * coefficient;
    amount = machine->parameters.wow / 10.0f;
    return machine->wow_state * amount * 0.004f;
}

static float update_drop(TsSisterMachine *machine, TsSisterDropState *drop)
{
    uint64_t interval;
    float amount = machine->parameters.drop / 100.0f;
    float target;
    if (amount <= 0.0f) {
        drop->current = 1.0f;
        ramp_reset(&drop->gain, 1.0f);
        drop->next_event_clock = machine->master_clock;
        return 1.0f;
    }
    interval = machine->buffer.sample_rate / 10u;
    if (interval == 0u) interval = 1u;
    if (machine->master_clock >= drop->next_event_clock) {
        target = clampf(prng_gaussian(&drop->prng) * 0.25f + 0.7f, 0.0f, 1.5f);
        target = 1.0f + (target - 1.0f) * amount;
        ramp_set(&drop->gain, target,
                 milliseconds_frames(machine->buffer.sample_rate, 5.0f));
        drop->next_event_clock = machine->master_clock + interval;
    }
    drop->current = ramp_advance(&drop->gain);
    if (!isfinite(drop->current)) {
        drop->current = 1.0f;
        ramp_reset(&drop->gain, 1.0f);
    }
    return drop->current;
}

static TsStereoFrame apply_decorrelation(TsSisterMachine *machine, size_t head,
                                         TsStereoFrame input)
{
    TsSisterDecorrelator *decor = &machine->decorrelator[head];
    float coefficient;
    float delayed;
    float mix;
    TsStereoFrame decorated;
    float mid;
    input = ts_stereo_frame_sanitize(input);
    coefficient = 1.0f - expf((float)(-2.0 * M_PI * 2000.0) /
                              (float)machine->buffer.sample_rate);
    decor->lowpass_state += (input.r - decor->lowpass_state) * coefficient;
    delayed = decor->delay[decor->write_index];
    decor->delay[decor->write_index] = decor->lowpass_state;
    decor->write_index = (decor->write_index + 1u) % decor->delay_frames;
    decorated.l = input.l;
    decorated.r = delayed;
    mix = ramp_advance(&decor->enabled_mix);
    decorated = frame_lerp(input, decorated, clampf(mix, 0.0f, 1.0f));
    mid = 0.5f * (decorated.l + decorated.r);
    decorated.l = mid + (decorated.l - mid) * machine->parameters.width;
    decorated.r = mid + (decorated.r - mid) * machine->parameters.width;
    return ts_stereo_frame_sanitize(decorated);
}

static float hard_feedback(float value, float gain, uint64_t *overload_count)
{
    float result;
    if (!isfinite(value) || !isfinite(gain)) {
        if (overload_count != NULL) ++*overload_count;
        return 0.0f;
    }
    result = value * gain;
    if (result > 1.0f) {
        if (overload_count != NULL) ++*overload_count;
        return 1.0f;
    }
    if (result < -1.0f) {
        if (overload_count != NULL) ++*overload_count;
        return -1.0f;
    }
    return result;
}

static float soft_saturate(float value)
{
    if (!isfinite(value)) return 0.0f;
    /* Unit slope around zero, with gradual Kafka-like write compression and
       a finite bound when the two clipped feedback branches meet input. */
    return clampf(2.0f * tanhf(value * 0.5f), -1.5f, 1.5f);
}

static float dc_block(TsSisterMachine *machine, size_t channel, float input)
{
    float radius = expf((float)(-2.0 * M_PI * 5.0) /
                        (float)machine->buffer.sample_rate);
    float output;
    if (!isfinite(input)) input = 0.0f;
    output = input - machine->dc_input_x1[channel] +
             radius * machine->dc_input_y1[channel];
    machine->dc_input_x1[channel] = input;
    machine->dc_input_y1[channel] = isfinite(output) ? output : 0.0f;
    return machine->dc_input_y1[channel];
}

static float update_duck(TsSisterMachine *machine, TsStereoFrame sidechain)
{
    float instant;
    float energy_coefficient;
    float envelope;
    float desired;
    float time_seconds;
    float coefficient;
    sidechain = ts_stereo_frame_sanitize(sidechain);
    if (!machine->parameters.duck_enabled) {
        machine->duck_gain = 1.0f;
        return 1.0f;
    }
    if (machine->parameters.duck_mode == TS_SISTER_DUCK_KAFKA_BIAS) {
        float bias = machine->parameters.duck_sensitivity * 75.0f;
        sidechain.l += bias;
        sidechain.r += bias;
    }
    instant = 0.5f * (sidechain.l * sidechain.l + sidechain.r * sidechain.r);
    if (!isfinite(instant)) instant = 0.0f;
    energy_coefficient = 1.0f - expf(-1.0f /
        (0.45f * (float)machine->buffer.sample_rate));
    machine->duck_energy += (instant - machine->duck_energy) * energy_coefficient;
    if (!isfinite(machine->duck_energy) || machine->duck_energy < 0.0f)
        machine->duck_energy = 0.0f;
    envelope = sqrtf(machine->duck_energy);
    if (machine->parameters.duck_mode == TS_SISTER_DUCK_KAFKA_BIAS) {
        desired = clampf(1.0f - envelope, 0.0f, 1.0f);
    } else {
        float threshold = powf(10.0f,
            (-6.0f - 54.0f * machine->parameters.duck_sensitivity) / 20.0f);
        desired = envelope <= threshold || envelope <= FLT_EPSILON
                    ? 1.0f : clampf(threshold / envelope, 0.0f, 1.0f);
    }
    time_seconds = desired < machine->duck_gain ? 0.010f : 0.050f;
    coefficient = 1.0f - expf(-1.0f /
        (time_seconds * (float)machine->buffer.sample_rate));
    machine->duck_gain += (desired - machine->duck_gain) * coefficient;
    if (!isfinite(machine->duck_gain)) machine->duck_gain = 1.0f;
    return clampf(machine->duck_gain, 0.0f, 1.0f);
}

static void advance_filter_coefficients(TsSisterMachine *machine)
{
    if (machine->filter_ramp_remaining == 0u) return;
#define ADVANCE_FILTER(field) machine->filter_current.field += machine->filter_step.field
    ADVANCE_FILTER(b0);
    ADVANCE_FILTER(b1);
    ADVANCE_FILTER(b2);
    ADVANCE_FILTER(a1);
    ADVANCE_FILTER(a2);
#undef ADVANCE_FILTER
    --machine->filter_ramp_remaining;
    if (machine->filter_ramp_remaining == 0u)
        machine->filter_current = machine->filter_target;
    if (!coefficients_finite(machine->filter_current)) {
        machine->filter_current = biquad_identity();
        machine->filter_target = machine->filter_current;
        machine->filter_ramp_remaining = 0u;
        memset(machine->filter_state, 0, sizeof(machine->filter_state));
        ++machine->overload_count;
    }
}

static float filter_channel(TsSisterMachine *machine, size_t channel, float input)
{
    TsSisterBiquadState *state = &machine->filter_state[channel];
    TsSisterBiquadCoefficients c = machine->filter_current;
    float output;
    if (!isfinite(input)) input = 0.0f;
    output = c.b0 * input + c.b1 * state->x1 + c.b2 * state->x2 -
             c.a1 * state->y1 - c.a2 * state->y2;
    if (!isfinite(output)) {
        memset(state, 0, sizeof(*state));
        ++machine->overload_count;
        return 0.0f;
    }
    state->x2 = state->x1;
    state->x1 = input;
    state->y2 = state->y1;
    state->y1 = output;
    return output;
}

static TsStereoFrame final_safety(TsSisterMachine *machine, TsStereoFrame input)
{
    float peak;
    input = ts_stereo_frame_sanitize(input);
    peak = frame_peak(input);
    if (peak > 1.0f) {
        input = frame_scale(input, 1.0f / peak);
        ++machine->overload_count;
    }
    return input;
}

static void publish_snapshot(TsSisterMachine *machine)
{
    TsSisterSnapshotAtomic *snapshot = &machine->snapshot;
    uint64_t sequence;
    size_t i;
    size_t capacity = machine->buffer.capacity_frames;
    sequence = atomic_fetch_add_explicit(&snapshot->revision, 1u,
                                          memory_order_acq_rel) + 1u;
    atomic_store_explicit(&snapshot->master_clock, machine->master_clock,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->write_position,
                          capacity == 0u ? 0u : machine->master_clock % capacity,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->write_normalized_bits,
                          float_bits(capacity == 0u ? 0.0f :
                          (float)(machine->master_clock % capacity) / (float)capacity),
                          memory_order_relaxed);
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        double position = ts_sister_positive_modulo(machine->last_head_position[i], capacity);
        atomic_store_explicit(&snapshot->head_position_bits[i], double_bits(position),
                              memory_order_relaxed);
        atomic_store_explicit(&snapshot->head_normalized_bits[i],
                              float_bits(capacity == 0u ? 0.0f :
                                         (float)(position / (double)capacity)),
                              memory_order_relaxed);
        atomic_store_explicit(&snapshot->head_peak_bits[i],
                              float_bits(frame_peak(machine->last_output.head[i])),
                              memory_order_relaxed);
    }
    atomic_store_explicit(&snapshot->rolling, machine->rolling, memory_order_relaxed);
    atomic_store_explicit(&snapshot->held, machine->held, memory_order_relaxed);
    atomic_store_explicit(&snapshot->clear_state, machine->clear_state,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->mix_peak_bits,
                          float_bits(frame_peak(machine->last_output.mix)),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->duck_gain_bits, float_bits(machine->duck_gain),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->wow_seconds_bits,
                          float_bits(machine->wow_state *
                                     (machine->parameters.wow / 10.0f) * 0.004f),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->drop_gain_bits[0],
                          float_bits(machine->drop[0].current), memory_order_relaxed);
    atomic_store_explicit(&snapshot->drop_gain_bits[1],
                          float_bits(machine->drop[1].current), memory_order_relaxed);
    atomic_store_explicit(&snapshot->overload_count, machine->overload_count,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->buffer_channels, machine->buffer.channels,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->sample_rate, machine->buffer.sample_rate,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->duration_bits,
                          double_bits(capacity == 0u ? 0.0 :
                                      (double)capacity / machine->buffer.sample_rate),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->revision, sequence + 1u, memory_order_release);
}

int ts_sister_machine_get_snapshot(const TsSisterMachine *machine,
                                   TsSisterSnapshot *result)
{
    const TsSisterSnapshotAtomic *snapshot;
    uint64_t before;
    uint64_t after;
    size_t i;
    int attempts;
    if (machine == NULL || result == NULL) return 0;
    snapshot = &machine->snapshot;
    for (attempts = 0; attempts < 16; ++attempts) {
        before = atomic_load_explicit(&snapshot->revision, memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        result->master_clock = atomic_load_explicit(&snapshot->master_clock,
                                                     memory_order_relaxed);
        result->write_position = (size_t)atomic_load_explicit(&snapshot->write_position,
                                                              memory_order_relaxed);
        result->write_normalized = bits_float((uint32_t)atomic_load_explicit(
            &snapshot->write_normalized_bits, memory_order_relaxed));
        for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
            result->head_position[i] = bits_double(atomic_load_explicit(
                &snapshot->head_position_bits[i], memory_order_relaxed));
            result->head_normalized[i] = bits_float((uint32_t)atomic_load_explicit(
                &snapshot->head_normalized_bits[i], memory_order_relaxed));
            result->head_peak[i] = bits_float((uint32_t)atomic_load_explicit(
                &snapshot->head_peak_bits[i], memory_order_relaxed));
        }
        result->rolling = atomic_load_explicit(&snapshot->rolling, memory_order_relaxed);
        result->held = atomic_load_explicit(&snapshot->held, memory_order_relaxed);
        result->clear_state = (TsSisterClearState)atomic_load_explicit(
            &snapshot->clear_state, memory_order_relaxed);
        result->mix_peak = bits_float((uint32_t)atomic_load_explicit(
            &snapshot->mix_peak_bits, memory_order_relaxed));
        result->duck_gain = bits_float((uint32_t)atomic_load_explicit(
            &snapshot->duck_gain_bits, memory_order_relaxed));
        result->wow_seconds = bits_float((uint32_t)atomic_load_explicit(
            &snapshot->wow_seconds_bits, memory_order_relaxed));
        result->drop_gain[0] = bits_float((uint32_t)atomic_load_explicit(
            &snapshot->drop_gain_bits[0], memory_order_relaxed));
        result->drop_gain[1] = bits_float((uint32_t)atomic_load_explicit(
            &snapshot->drop_gain_bits[1], memory_order_relaxed));
        result->overload_count = atomic_load_explicit(&snapshot->overload_count,
                                                       memory_order_relaxed);
        result->buffer_channels = (uint8_t)atomic_load_explicit(
            &snapshot->buffer_channels, memory_order_relaxed);
        result->sample_rate = (uint32_t)atomic_load_explicit(
            &snapshot->sample_rate, memory_order_relaxed);
        result->duration_seconds = bits_double(atomic_load_explicit(
            &snapshot->duration_bits, memory_order_relaxed));
        after = atomic_load_explicit(&snapshot->revision, memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            result->revision = after;
            return 1;
        }
    }
    return 0;
}

static TsStereoFrame process_internal(TsSisterMachine *machine,
                                      TsStereoFrame input,
                                      TsStereoFrame duck_sidechain,
                                      TsSisterOutput *result)
{
    TsSisterOutput output;
    TsStereoFrame raw[TS_SISTER_HEAD_COUNT];
    TsStereoFrame feedback1;
    TsStereoFrame feedback2;
    TsStereoFrame previous;
    TsStereoFrame retained;
    TsStereoFrame write;
    TsStereoFrame sum;
    size_t write_position;
    double delay_position;
    double head2_position;
    double head3_position;
    double current_offset;
    double offset_delta;
    float wow_seconds;
    float wow_frames;
    float drop2;
    float drop3;
    float clear_gain;
    float erase;
    float duck_gain;
    float level;
    float peak;
    memset(&output, 0, sizeof(output));
    if (machine == NULL || machine->buffer.data == NULL ||
        machine->buffer.capacity_frames < 2u) {
        if (result != NULL) *result = output;
        return output.mix;
    }
    if (!isfinite(input.l) || !isfinite(input.r) ||
        !isfinite(duck_sidechain.l) || !isfinite(duck_sidechain.r))
        ++machine->overload_count;
    input = ts_stereo_frame_sanitize(input);
    duck_sidechain = ts_stereo_frame_sanitize(duck_sidechain);
    write_position = (size_t)(machine->master_clock % machine->buffer.capacity_frames);

    if (machine->clear_state == TS_SISTER_CLEAR_WAITING) {
        clear_gain = 0.0f;
    } else {
        clear_gain = ramp_advance(&machine->clear_gain);
        if (machine->clear_state == TS_SISTER_CLEAR_FADE_OUT &&
            machine->clear_gain.remaining == 0u) {
            machine->clear_state = TS_SISTER_CLEAR_WAITING;
            clear_gain = 0.0f;
        } else if (machine->clear_state == TS_SISTER_CLEAR_FADE_IN &&
                   machine->clear_gain.remaining == 0u) {
            machine->clear_state = TS_SISTER_CLEAR_IDLE;
            clear_gain = 1.0f;
        }
    }

    if (machine->head[0].jump_remaining > 0u) {
        float amount = 1.0f - (float)machine->head[0].jump_remaining /
                                  (float)machine->head[0].jump_total;
        TsStereoFrame old_read = ts_sister_buffer_read(
            &machine->buffer,
            guard_read_position((double)write_position -
                                machine->head[0].old_delay_frames,
                                write_position, machine->buffer.capacity_frames));
        TsStereoFrame new_read = ts_sister_buffer_read(
            &machine->buffer,
            guard_read_position((double)write_position -
                                machine->head[0].current_delay_frames,
                                write_position, machine->buffer.capacity_frames));
        raw[0] = frame_lerp(old_read, new_read, amount);
        --machine->head[0].jump_remaining;
    } else {
        float coefficient = 1.0f - expf(-1.0f /
            (0.020f * (float)machine->buffer.sample_rate));
        machine->head[0].current_delay_frames +=
            (machine->head[0].target_delay_frames -
             machine->head[0].current_delay_frames) * coefficient;
        delay_position = guard_read_position((double)write_position -
                         machine->head[0].current_delay_frames,
                         write_position, machine->buffer.capacity_frames);
        raw[0] = ts_sister_buffer_read(&machine->buffer, delay_position);
    }
    delay_position = guard_read_position((double)write_position -
                     machine->head[0].current_delay_frames,
                     write_position, machine->buffer.capacity_frames);
    machine->last_head_position[0] = delay_position;

    current_offset = ramp_advance(&machine->head[1].offset);
    offset_delta = current_offset - machine->head[1].previous_offset;
    machine->head[1].phase = ts_sister_positive_modulo(
        machine->head[1].phase - offset_delta, machine->buffer.capacity_frames);
    machine->head[1].previous_offset = current_offset;
    current_offset = ramp_advance(&machine->head[2].offset);
    offset_delta = current_offset - machine->head[2].previous_offset;
    machine->head[2].phase = ts_sister_positive_modulo(
        machine->head[2].phase - offset_delta, machine->buffer.capacity_frames);
    machine->head[2].previous_offset = current_offset;

    wow_seconds = update_wow(machine);
    wow_frames = wow_seconds * (float)machine->buffer.sample_rate;
    head2_position = ts_sister_positive_modulo(machine->head[1].phase + wow_frames,
                                               machine->buffer.capacity_frames);
    head3_position = ts_sister_positive_modulo(machine->head[2].phase + wow_frames,
                                               machine->buffer.capacity_frames);
    raw[1] = read_with_write_guard(machine, &machine->head[1],
                                   head2_position, write_position);
    raw[2] = read_with_write_guard(machine, &machine->head[2],
                                   head3_position, write_position);
    machine->last_head_position[1] = guard_read_position(
        head2_position, write_position, machine->buffer.capacity_frames);
    machine->last_head_position[2] = guard_read_position(
        head3_position, write_position, machine->buffer.capacity_frames);

    drop2 = update_drop(machine, &machine->drop[0]);
    drop3 = update_drop(machine, &machine->drop[1]);
    level = ramp_advance(&machine->head[0].level);
    output.head[0] = frame_scale(apply_decorrelation(machine, 0u, raw[0]), level);
    level = ramp_advance(&machine->head[1].level);
    output.head[1] = frame_scale(apply_decorrelation(
        machine, 1u, frame_scale(raw[1], drop2)), level);
    level = ramp_advance(&machine->head[2].level);
    output.head[2] = frame_scale(apply_decorrelation(
        machine, 2u, frame_scale(raw[2], drop3)), level);

    feedback1.l = hard_feedback(raw[0].l, machine->parameters.head1_feedback,
                                &machine->overload_count) * clear_gain;
    feedback1.r = hard_feedback(raw[0].r, machine->parameters.head1_feedback,
                                &machine->overload_count) * clear_gain;
    feedback2.l = hard_feedback(raw[1].l, machine->parameters.head2_feedback,
                                &machine->overload_count) * clear_gain;
    feedback2.r = hard_feedback(raw[1].r, machine->parameters.head2_feedback,
                                &machine->overload_count) * clear_gain;
    erase = ramp_advance(&machine->write_erase);
    previous = ts_sister_buffer_read(&machine->buffer, (double)write_position);
    retained = frame_scale(previous, 1.0f - erase);
    write = frame_add(retained, frame_add(input, frame_add(feedback1, feedback2)));
    peak = frame_peak(write);
    if (!isfinite(write.l) || !isfinite(write.r) || peak > 1.0f)
        ++machine->overload_count;
    write.l = soft_saturate(dc_block(machine, 0u, write.l));
    write.r = soft_saturate(dc_block(machine, 1u, write.r));
    output.write = write;
    output.write_position = write_position;
    if (machine->rolling && !machine->held &&
        machine->clear_state != TS_SISTER_CLEAR_WAITING) {
        output.wrote = ts_sister_buffer_write(&machine->buffer,
                                               write_position, write);
    }

    sum = frame_add(output.head[0], frame_add(output.head[1], output.head[2]));
    sum = frame_scale(sum, machine->parameters.headroom);
    duck_gain = update_duck(machine, duck_sidechain);
    sum = frame_scale(sum, duck_gain);
    advance_filter_coefficients(machine);
    sum.l = filter_channel(machine, 0u, sum.l);
    sum.r = filter_channel(machine, 1u, sum.r);
    sum = frame_scale(sum, machine->parameters.mix_output_gain);
    output.head[0] = frame_scale(output.head[0], clear_gain);
    output.head[1] = frame_scale(output.head[1], clear_gain);
    output.head[2] = frame_scale(output.head[2], clear_gain);
    output.mix = final_safety(machine, frame_scale(sum, clear_gain));

    machine->head[1].phase = ts_sister_positive_modulo(
        machine->head[1].phase +
        ts_sister_rate_value(machine->parameters.head2_rate_index),
        machine->buffer.capacity_frames);
    machine->head[2].phase = ts_sister_positive_modulo(
        machine->head[2].phase +
        ts_sister_rate_value(machine->parameters.head3_rate_index),
        machine->buffer.capacity_frames);
    ++machine->master_clock;
    machine->last_output = output;
    if (result != NULL) *result = output;
    return output.mix;
}

TsSisterOutput ts_sister_machine_process_frame(TsSisterMachine *machine,
                                               TsStereoFrame input,
                                               TsStereoFrame duck_sidechain)
{
    TsSisterOutput result;
    process_internal(machine, input, duck_sidechain, &result);
    if (machine != NULL && machine->buffer.data != NULL) publish_snapshot(machine);
    return result;
}

void ts_sister_machine_process_block(TsSisterMachine *machine,
                                     const TsStereoFrame *input,
                                     const TsStereoFrame *duck_sidechain,
                                     TsSisterOutput *output,
                                     size_t frames)
{
    TsStereoFrame silence = {0.0f, 0.0f};
    size_t i;
    if (machine == NULL) return;
    for (i = 0u; i < frames; ++i) {
        TsStereoFrame source = input != NULL ? input[i] : silence;
        TsStereoFrame side = duck_sidechain != NULL ? duck_sidechain[i] : silence;
        TsSisterOutput frame_output;
        process_internal(machine, source, side, &frame_output);
        if (output != NULL) output[i] = frame_output;
    }
    if (machine->buffer.data != NULL) publish_snapshot(machine);
}
