#include "tapesister/sister_runtime.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void runtime_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static TsStereoFrame frame_add(TsStereoFrame a, TsStereoFrame b)
{
    return ts_stereo_frame_sanitize(
        (TsStereoFrame){a.l + b.l, a.r + b.r});
}

static TsStereoFrame frame_scale(TsStereoFrame value, float gain)
{
    if (!isfinite(gain)) gain = 0.0f;
    return ts_stereo_frame_sanitize(
        (TsStereoFrame){value.l * gain, value.r * gain});
}

static TsStereoFrame frame_effect_return(TsStereoFrame dry,
                                         TsStereoFrame processed,
                                         float gain)
{
    TsStereoFrame result = {
        dry.l + (processed.l - dry.l) * gain,
        dry.r + (processed.r - dry.r) * gain
    };
    return ts_stereo_frame_sanitize(result);
}

static float frame_peak(TsStereoFrame value)
{
    float left;
    float right;
    value = ts_stereo_frame_sanitize(value);
    left = fabsf(value.l);
    right = fabsf(value.r);
    return left > right ? left : right;
}

static float monitor_approach(float current, float target, uint32_t sample_rate)
{
    float coefficient;
    if (!isfinite(current)) current = target;
    if (!isfinite(target)) target = 0.0f;
    if (target < 0.0f) target = 0.0f;
    if (target > 1.0f) target = 1.0f;
    if (sample_rate == 0u) return target;
    coefficient = 1.0f - expf(-1.0f / (0.020f * (float)sample_rate));
    current += (target - current) * coefficient;
    return fabsf(target - current) < 0.000001f ? target : current;
}

static void output_meter_update(TsSisterRuntime *runtime,
                                TsStereoFrame output,
                                float pre_limiter_peak)
{
    float sample[2];
    float level_release;
    float peak_release;
    uint32_t sample_rate;
    if (runtime == NULL) return;
    sample_rate = runtime->limiter.sample_rate;
    if (sample_rate == 0u) sample_rate = runtime->machine.buffer.sample_rate;
    if (sample_rate == 0u) sample_rate = 48000u;
    sample[0] = fabsf(isfinite(output.l) ? output.l : 0.0f);
    sample[1] = fabsf(isfinite(output.r) ? output.r : 0.0f);
    level_release = 1.0f / (0.30f * (float)sample_rate);
    peak_release = 1.0f / (0.75f * (float)sample_rate);
    for (int channel = 0; channel < 2; ++channel) {
        if (sample[channel] >= runtime->output_level[channel])
            runtime->output_level[channel] = sample[channel];
        else {
            runtime->output_level[channel] -= level_release;
            if (runtime->output_level[channel] < sample[channel])
                runtime->output_level[channel] = sample[channel];
        }
        if (sample[channel] >= runtime->output_peak_hold[channel]) {
            runtime->output_peak_hold[channel] = sample[channel];
            runtime->output_peak_hold_frames[channel] =
                (uint32_t)(0.75f * (float)sample_rate);
        } else if (runtime->output_peak_hold_frames[channel] != 0u) {
            --runtime->output_peak_hold_frames[channel];
        } else {
            runtime->output_peak_hold[channel] -= peak_release;
            if (runtime->output_peak_hold[channel] <
                runtime->output_level[channel])
                runtime->output_peak_hold[channel] =
                    runtime->output_level[channel];
        }
        if (pre_limiter_peak >= 1.0f)
            runtime->output_clip_hold_frames[channel] = sample_rate * 2u;
        else if (runtime->output_clip_hold_frames[channel] != 0u)
            --runtime->output_clip_hold_frames[channel];
    }
}

static void runtime_ramp_reset(TsSisterRamp *ramp, float value)
{
    if (ramp == NULL) return;
    ramp->current = value;
    ramp->target = value;
    ramp->step = 0.0f;
    ramp->remaining = 0u;
}

static void runtime_ramp_set(TsSisterRamp *ramp, float target,
                             uint32_t sample_rate)
{
    uint32_t frames;
    if (ramp == NULL) return;
    if (!isfinite(target)) target = 0.0f;
    frames = sample_rate > 0u ? (uint32_t)ceilf(sample_rate * 0.020f) : 0u;
    ramp->target = target;
    if (frames == 0u || fabsf(target - ramp->current) <= FLT_EPSILON) {
        runtime_ramp_reset(ramp, target);
        return;
    }
    ramp->step = (target - ramp->current) / (float)frames;
    ramp->remaining = frames;
}

static float runtime_ramp_advance(TsSisterRamp *ramp)
{
    if (ramp == NULL) return 0.0f;
    if (ramp->remaining > 0u) {
        ramp->current += ramp->step;
        --ramp->remaining;
        if (ramp->remaining == 0u) ramp->current = ramp->target;
    }
    return ramp->current;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits_float(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void snapshot_atomic_init(TsSisterRoutingSnapshotAtomic *snapshot)
{
    if (snapshot == NULL) return;
    atomic_init(&snapshot->revision, 0u);
    atomic_init(&snapshot->enabled, 0);
    atomic_init(&snapshot->rolling, 1);
    atomic_init(&snapshot->held, 0);
    atomic_init(&snapshot->monitor_enabled, 0);
    atomic_init(&snapshot->source_switches, 0u);
    atomic_init(&snapshot->source_mask, 0u);
    atomic_init(&snapshot->active_page, 0u);
    atomic_init(&snapshot->active_source_voices, 0);
    atomic_init(&snapshot->selected_tap, TS_SISTER_TAP_MIX);
    atomic_init(&snapshot->capture_state, TS_CAPTURE_IDLE);
    atomic_init(&snapshot->capture_destination, -1);
    atomic_init(&snapshot->capture_recorded_frames, 0u);
    atomic_init(&snapshot->capture_capacity_frames, 0u);
    atomic_init(&snapshot->destination_status, TS_SISTER_DESTINATION_NONE);
    atomic_init(&snapshot->source_input_peak_bits, float_bits(0.0f));
    for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap)
        atomic_init(&snapshot->tap_peak_bits[tap], float_bits(0.0f));
    for (int channel = 0; channel < 2; ++channel) {
        atomic_init(&snapshot->output_level_bits[channel], float_bits(0.0f));
        atomic_init(&snapshot->output_peak_hold_bits[channel],
                    float_bits(0.0f));
        atomic_init(&snapshot->output_clip[channel], 0);
    }
    atomic_init(&snapshot->limiter_enabled,
                TS_SISTER_LIMITER_DEFAULT_ENABLED);
    atomic_init(&snapshot->limiter_ceiling_db_bits,
                float_bits(TS_SISTER_LIMITER_DEFAULT_CEILING_DB));
    atomic_init(&snapshot->limiter_gain_reduction_db_bits, float_bits(0.0f));
    atomic_init(&snapshot->limiter_input_peak_bits, float_bits(0.0f));
    atomic_init(&snapshot->overload_count, 0u);
    atomic_init(&snapshot->warnings, 0u);
    atomic_init(&snapshot->source_target_conflict, 0);
    atomic_init(&snapshot->processed_frames, 0u);
    atomic_init(&snapshot->fallout_lfo_phase_bits, float_bits(0.0f));
    atomic_init(&snapshot->fallout_rise_phase_bits, float_bits(0.0f));
    atomic_init(&snapshot->fallout_rise_complete, 0);
    atomic_init(&snapshot->fx_transition_progress_bits, float_bits(1.0f));
    atomic_init(&snapshot->fx_transition_active, 0);
    atomic_init(&snapshot->fx_transition_source,
                TS_SISTER_FX_TRANSITION_NONE);
    atomic_init(&snapshot->fx_transition_target_enabled, 0);
    atomic_init(&snapshot->fx_transition_topology, 0);
    atomic_init(&snapshot->fx_master_transition_progress_bits, float_bits(1.0f));
    atomic_init(&snapshot->fx_master_transition_active, 0);
    atomic_init(&snapshot->fx_master_transition_target_enabled, 0);
    atomic_init(&snapshot->fallout_component_transition_progress_bits,
                float_bits(1.0f));
    atomic_init(&snapshot->fallout_component_transition_active, 0);
    atomic_init(&snapshot->fallout_component_transition_source,
                TS_SISTER_FALLOUT_TRANSITION_NONE);
    atomic_init(&snapshot->fallout_component_transition_target_enabled, 0);
    atomic_init(&snapshot->fallout_master_transition_progress_bits,
                float_bits(1.0f));
    atomic_init(&snapshot->fallout_master_transition_active, 0);
    atomic_init(&snapshot->fallout_master_transition_target_enabled, 0);
    atomic_init(&snapshot->fallout_preset_transition_progress_bits,
                float_bits(1.0f));
    atomic_init(&snapshot->fallout_preset_transition_active, 0);
}

static void publish_snapshot(TsSisterRuntime *runtime)
{
    TsSisterRoutingSnapshotAtomic *snapshot;
    uint64_t revision;
    uint16_t mask = 0u;
    TsSisterFxTransitionStatus fx_transition;
    TsSisterFxTransitionStatus fx_master_transition;
    TsSisterFalloutTransitionStatus fallout_transition;
    TsSisterFalloutTransitionStatus fallout_master_transition;
    int transition_active = 0;
    float transition_progress;
    if (runtime == NULL) return;
    snapshot = &runtime->snapshot;
    revision = atomic_load_explicit(&snapshot->revision,
                                    memory_order_relaxed);
    if ((revision & 1u) != 0u) ++revision;
    atomic_store_explicit(&snapshot->revision, revision + 1u,
                          memory_order_release);
    if (runtime->active_page < TS_SISTER_RUNTIME_PAGE_LIMIT)
        mask = runtime->page_source_masks[runtime->active_page];
    atomic_store_explicit(&snapshot->enabled, runtime->enabled,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->rolling, runtime->rolling,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->held, runtime->held,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->monitor_enabled,
                          runtime->monitor_enabled, memory_order_relaxed);
    atomic_store_explicit(&snapshot->source_switches,
                          runtime->source_switches, memory_order_relaxed);
    atomic_store_explicit(&snapshot->source_mask, mask,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->active_page, runtime->active_page,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->active_source_voices,
                          ts_performance_count(&runtime->performance),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->selected_tap, runtime->selected_tap,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->capture_state,
                          atomic_load_explicit(&runtime->capture.state,
                                               memory_order_relaxed),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->capture_destination,
                          runtime->capture.destination_slot,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->capture_recorded_frames,
                          runtime->capture.recorded_frames,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->capture_capacity_frames,
                          runtime->capture.capacity_frames,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->destination_status,
                          runtime->destination_status,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->source_input_peak_bits,
                          float_bits(frame_peak(runtime->last_frame.input)),
                          memory_order_relaxed);
    for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap)
        atomic_store_explicit(&snapshot->tap_peak_bits[tap],
                              float_bits(frame_peak(runtime->last_frame.tap[tap])),
                              memory_order_relaxed);
    for (int channel = 0; channel < 2; ++channel) {
        atomic_store_explicit(&snapshot->output_level_bits[channel],
                              float_bits(runtime->output_level[channel]),
                              memory_order_relaxed);
        atomic_store_explicit(&snapshot->output_peak_hold_bits[channel],
                              float_bits(runtime->output_peak_hold[channel]),
                              memory_order_relaxed);
        atomic_store_explicit(&snapshot->output_clip[channel],
                              runtime->output_clip_hold_frames[channel] != 0u,
                              memory_order_relaxed);
    }
    atomic_store_explicit(&snapshot->limiter_enabled,
                          runtime->limiter.enabled, memory_order_relaxed);
    atomic_store_explicit(&snapshot->limiter_ceiling_db_bits,
                          float_bits(runtime->limiter.ceiling_db),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->limiter_gain_reduction_db_bits,
                          float_bits(runtime->limiter_gain_reduction_db),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->limiter_input_peak_bits,
                          float_bits(runtime->limiter_input_peak),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->overload_count,
                          runtime->enabled ? runtime->machine.overload_count : 0u,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->warnings, runtime->warnings,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->source_target_conflict,
                          runtime->source_target_conflict,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->processed_frames,
                          runtime->processed_frames, memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_lfo_phase_bits,
                          float_bits((float)runtime->fallout.lfo_phase),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_rise_phase_bits,
                          float_bits(runtime->fallout.rise_value),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_rise_complete,
                          runtime->fallout.rise_one_shot_complete,
                          memory_order_relaxed);
    fx_transition =
        ts_sister_post_fx_effect_transition_status(&runtime->post_fx);
    atomic_store_explicit(&snapshot->fx_transition_progress_bits,
                          float_bits(fx_transition.progress),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->fx_transition_active,
                          fx_transition.active,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->fx_transition_source,
                          fx_transition.source, memory_order_relaxed);
    atomic_store_explicit(&snapshot->fx_transition_target_enabled,
                          fx_transition.target_enabled, memory_order_relaxed);
    atomic_store_explicit(&snapshot->fx_transition_topology,
                          fx_transition.topology, memory_order_relaxed);
    fx_master_transition =
        ts_sister_post_fx_master_transition_status(&runtime->post_fx);
    atomic_store_explicit(&snapshot->fx_master_transition_progress_bits,
                          float_bits(fx_master_transition.progress),
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->fx_master_transition_active,
                          fx_master_transition.active, memory_order_relaxed);
    atomic_store_explicit(&snapshot->fx_master_transition_target_enabled,
                          fx_master_transition.target_enabled,
                          memory_order_relaxed);
    fallout_transition =
        ts_sister_fallout_component_transition_status(&runtime->fallout);
    atomic_store_explicit(
        &snapshot->fallout_component_transition_progress_bits,
        float_bits(fallout_transition.progress), memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_component_transition_active,
                          fallout_transition.active, memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_component_transition_source,
                          fallout_transition.source, memory_order_relaxed);
    atomic_store_explicit(
        &snapshot->fallout_component_transition_target_enabled,
        fallout_transition.target_enabled, memory_order_relaxed);
    fallout_master_transition =
        ts_sister_fallout_master_transition_status(&runtime->fallout);
    atomic_store_explicit(
        &snapshot->fallout_master_transition_progress_bits,
        float_bits(fallout_master_transition.progress), memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_master_transition_active,
                          fallout_master_transition.active,
                          memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_master_transition_target_enabled,
                          fallout_master_transition.target_enabled,
                          memory_order_relaxed);
    transition_progress = ts_sister_fallout_preset_transition_progress(
        &runtime->fallout, &transition_active);
    atomic_store_explicit(&snapshot->fallout_preset_transition_progress_bits,
                          float_bits(transition_progress), memory_order_relaxed);
    atomic_store_explicit(&snapshot->fallout_preset_transition_active,
                          transition_active, memory_order_relaxed);
    atomic_store_explicit(&snapshot->revision, revision + 2u,
                          memory_order_release);
}

static void publish_frame_snapshot(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    if (runtime->snapshot_batch_depth != 0u) {
        runtime->snapshot_pending = 1;
        return;
    }
    publish_snapshot(runtime);
}

void ts_sister_runtime_begin_audio_block(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    if (runtime->snapshot_batch_depth != UINT32_MAX)
        ++runtime->snapshot_batch_depth;
    ts_sister_machine_begin_audio_block(&runtime->machine);
}

void ts_sister_runtime_end_audio_block(TsSisterRuntime *runtime)
{
    if (runtime == NULL || runtime->snapshot_batch_depth == 0u) return;
    ts_sister_machine_end_audio_block(&runtime->machine);
    --runtime->snapshot_batch_depth;
    runtime->limiter_gain_reduction_db =
        ts_sister_limiter_gain_reduction_db(&runtime->limiter);
    if (runtime->snapshot_batch_depth == 0u && runtime->snapshot_pending) {
        runtime->snapshot_pending = 0;
        publish_snapshot(runtime);
    }
}

static uint8_t source_bit(int source_index)
{
    static const uint8_t bits[TS_SISTER_SOURCE_COUNT] = {
        TS_SISTER_SOURCE_TILES, TS_SISTER_SOURCE_FM,
        TS_SISTER_SOURCE_EXT, TS_SISTER_SOURCE_PREVIEW
    };
    return source_index >= 0 && source_index < TS_SISTER_SOURCE_COUNT ?
           bits[source_index] : 0u;
}

static float source_route_target(const TsSisterRuntime *runtime,
                                 int source_index)
{
    uint8_t bit;
    if (runtime == NULL) return 0.0f;
    bit = source_bit(source_index);
    if ((runtime->source_switches & bit) == 0u) return 0.0f;
    if (bit == TS_SISTER_SOURCE_EXT && !runtime->input_available) return 0.0f;
    return 1.0f;
}

static TsStereoFrame selected_tap(const TsSisterRuntimeFrame *frame,
                                  TsSisterTap tap)
{
    if (frame == NULL || tap < 0 || tap >= TS_SISTER_TAP_COUNT)
        return (TsStereoFrame){0.0f, 0.0f};
    return frame->tap[tap];
}

static TsBankCaptureKind capture_kind_for_tap(TsSisterTap tap)
{
    if (tap == TS_SISTER_TAP_H1) return TS_BANK_CAPTURE_SISTER_H1;
    if (tap == TS_SISTER_TAP_H2) return TS_BANK_CAPTURE_SISTER_H2;
    if (tap == TS_SISTER_TAP_H3) return TS_BANK_CAPTURE_SISTER_H3;
    return TS_BANK_CAPTURE_SISTER_MIX;
}

void ts_sister_runtime_init(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    memset(runtime, 0, sizeof(*runtime));
    ts_sister_parameters_default(&runtime->parameters, 48000u);
    ts_performance_init(&runtime->performance);
    ts_capture_init(&runtime->capture);
    ts_sister_limiter_init(&runtime->limiter);
    runtime->rolling = 1;
    runtime->input_available = 1;
    runtime->monitor_dry_current = 1.0f;
    runtime->monitor_wet_current = 1.0f;
    runtime_ramp_reset(&runtime->source_gain[0],
                       runtime->parameters.tiles_gain);
    runtime_ramp_reset(&runtime->source_gain[1],
                       runtime->parameters.fm_gain);
    runtime_ramp_reset(&runtime->source_gain[2],
                       runtime->parameters.external_gain);
    runtime_ramp_reset(&runtime->source_gain[3],
                       runtime->parameters.preview_gain);
    for (int source_index = 0; source_index < TS_SISTER_SOURCE_COUNT;
         ++source_index)
        runtime_ramp_reset(&runtime->source_route[source_index], 0.0f);
    runtime_ramp_reset(&runtime->monitor_route, 0.0f);
    runtime_ramp_reset(&runtime->direct_tile_route, 0.0f);
    runtime_ramp_reset(&runtime->ordinary_fx_return_gain,
                       runtime->parameters.fx_return_gain);
    runtime->selected_tap = TS_SISTER_TAP_MIX;
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    snapshot_atomic_init(&runtime->snapshot);
    ts_sister_wave_publisher_init(&runtime->waveform);
    publish_snapshot(runtime);
}

void ts_sister_runtime_free(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    ts_sister_machine_free(&runtime->machine);
    ts_sister_fallout_free(&runtime->fallout);
    ts_sister_post_fx_free(&runtime->post_fx);
    ts_sister_limiter_free(&runtime->limiter);
    ts_capture_free(&runtime->capture);
    memset(runtime->page_source_masks, 0,
           sizeof(runtime->page_source_masks));
    runtime->enabled = 0;
    runtime->output_channels = 0u;
    runtime->source_switches = 0u;
    runtime->monitor_enabled = 0;
    runtime->active_page = 0u;
    runtime->processed_frames = 0u;
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    ts_performance_free(&runtime->performance);
    publish_snapshot(runtime);
}

int ts_sister_runtime_enable(TsSisterRuntime *runtime, uint32_t sample_rate,
                             uint8_t output_channels,
                             uint8_t buffer_channels,
                             double duration_seconds,
                             char *error, size_t error_size)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    if (runtime == NULL || sample_rate == 0u || output_channels != 2u ||
        !ts_sample_valid_channels(buffer_channels) ||
        !isfinite(duration_seconds) || duration_seconds <= 0.0 ||
        duration_seconds > TS_SISTER_MAX_SECONDS) {
        if (runtime != NULL) {
            runtime->warnings |= TS_SISTER_WARNING_DEVICE_CONTRACT;
            publish_snapshot(runtime);
        }
        runtime_error(error, error_size,
                      "Sister requires a valid rate and stereo output contract");
        return 0;
    }
    memset(&machine, 0, sizeof(machine));
    if ((!runtime->fallout.ready || runtime->fallout.sample_rate != sample_rate) &&
        !ts_sister_fallout_reconfigure(&runtime->fallout, sample_rate)) {
        runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
        publish_snapshot(runtime);
        runtime_error(error, error_size, "Could not allocate Fallout storage");
        return 0;
    }
    if ((!runtime->post_fx.ready || runtime->post_fx.sample_rate != sample_rate) &&
        !ts_sister_post_fx_reconfigure(&runtime->post_fx, sample_rate)) {
        runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
        publish_snapshot(runtime);
        runtime_error(error, error_size, "Could not allocate Sister FX storage");
        return 0;
    }
    if ((!runtime->limiter.ready ||
         runtime->limiter.sample_rate != sample_rate) &&
        !ts_sister_limiter_reconfigure(&runtime->limiter, sample_rate)) {
        runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
        publish_snapshot(runtime);
        runtime_error(error, error_size,
                      "Could not allocate output limiter lookahead");
        return 0;
    }
    if (!ts_sister_machine_init(&machine, sample_rate, buffer_channels,
                                duration_seconds)) {
        runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
        publish_snapshot(runtime);
        runtime_error(error, error_size,
                      "Could not allocate Sister rolling storage");
        return 0;
    }
    parameters = runtime->parameters;
    if (!runtime->parameters_published)
        parameters.buffer_seconds = (float)duration_seconds;
    ts_sister_machine_set_parameters(&machine, &parameters);
    machine.fx_return_gain.current =
        runtime->ordinary_fx_return_gain.current;
    runtime_ramp_set(&machine.fx_return_gain, parameters.fx_return_gain,
                     sample_rate);
    ts_sister_machine_set_rolling(&machine, runtime->rolling);
    ts_sister_machine_set_hold(&machine, runtime->held);
    ts_sister_machine_free(&runtime->machine);
    runtime->machine = machine;
    runtime->parameters = machine.parameters;
    /* Audio is not running yet: restore the saved gate truth exactly. Starting
       every new engine at fully wet made an OFF preset audibly fade from ON. */
    ts_sister_post_fx_sync_controls(&runtime->post_fx, &runtime->parameters.fx);
    ts_sister_fallout_sync_controls(&runtime->fallout,
                                    &runtime->parameters.fx.fallout);
    runtime->enabled = 1;
    runtime->output_channels = output_channels;
    runtime->callback_failed = 0;
    runtime->warnings &= ~(uint32_t)(TS_SISTER_WARNING_DEVICE_CONTRACT |
                                     TS_SISTER_WARNING_ALLOCATION |
                                     TS_SISTER_WARNING_CALLBACK);
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    runtime->processed_frames = 0u;
    runtime->master_feedback_current = 0.0f;
    runtime->master_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    runtime->fallout_feedback_current = 0.0f;
    runtime->fallout_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    memset(runtime->output_level, 0, sizeof(runtime->output_level));
    memset(runtime->output_peak_hold, 0, sizeof(runtime->output_peak_hold));
    memset(runtime->output_peak_hold_frames, 0,
           sizeof(runtime->output_peak_hold_frames));
    memset(runtime->output_clip_hold_frames, 0,
           sizeof(runtime->output_clip_hold_frames));
    runtime->source_target_conflict = 0;
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    for (int source_index = 0; source_index < TS_SISTER_SOURCE_COUNT;
         ++source_index)
        runtime_ramp_reset(&runtime->source_route[source_index],
                           source_route_target(runtime, source_index));
    runtime_ramp_reset(&runtime->monitor_route,
                       runtime->monitor_enabled ? 1.0f : 0.0f);
    runtime_ramp_reset(&runtime->direct_tile_route,
                       runtime->rolling ? 1.0f : 0.0f);
    ts_capture_free(&runtime->capture);
    ts_performance_clear(&runtime->performance);
    ts_sister_wave_publisher_clear(&runtime->waveform, buffer_channels);
    runtime->waveform_capacity_frames = runtime->machine.buffer.capacity_frames;
    publish_snapshot(runtime);
    runtime_error(error, error_size, "");
    return 1;
}

void ts_sister_runtime_disable(TsSisterRuntime *runtime)
{
    uint32_t sample_rate;
    if (runtime == NULL) return;
    sample_rate = runtime->machine.buffer.sample_rate != 0u ?
        runtime->machine.buffer.sample_rate :
        runtime->post_fx.ready ? runtime->post_fx.sample_rate : 48000u;
    if (runtime->enabled) {
        runtime->ordinary_fx_return_gain.current =
            runtime->machine.fx_return_gain.current;
        runtime_ramp_set(&runtime->ordinary_fx_return_gain,
                         runtime->parameters.fx_return_gain, sample_rate);
    }
    runtime->enabled = 0;
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    ts_performance_clear(&runtime->performance);
    ts_capture_free(&runtime->capture);
    ts_sister_machine_free(&runtime->machine);
    ts_sister_wave_publisher_clear(&runtime->waveform, 2u);
    runtime->waveform_capacity_frames = 0u;
    runtime->output_channels = 0u;
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    runtime->source_target_conflict = 0;
    runtime->master_feedback_current = 0.0f;
    runtime->master_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    runtime->fallout_feedback_current = 0.0f;
    runtime->fallout_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    memset(runtime->output_level, 0, sizeof(runtime->output_level));
    memset(runtime->output_peak_hold, 0, sizeof(runtime->output_peak_hold));
    memset(runtime->output_peak_hold_frames, 0,
           sizeof(runtime->output_peak_hold_frames));
    memset(runtime->output_clip_hold_frames, 0,
           sizeof(runtime->output_clip_hold_frames));
    ts_sister_fallout_clear(&runtime->fallout);
    publish_snapshot(runtime);
}

int ts_sister_runtime_reconfigure(TsSisterRuntime *runtime,
                                  uint32_t sample_rate,
                                  uint8_t output_channels,
                                  char *error, size_t error_size)
{
    if (runtime == NULL) {
        runtime_error(error, error_size, "Sister runtime is unavailable");
        return 0;
    }
    if (!runtime->enabled) {
        if (sample_rate == 0u || output_channels != 2u) {
            runtime->warnings |= TS_SISTER_WARNING_DEVICE_CONTRACT;
            publish_snapshot(runtime);
            runtime_error(error, error_size,
                          "Sister output device contract is unavailable");
            return 0;
        }
        if (!runtime->fallout.ready || runtime->fallout.sample_rate != sample_rate) {
            if (!ts_sister_fallout_reconfigure(&runtime->fallout, sample_rate)) {
                runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
                publish_snapshot(runtime);
                runtime_error(error, error_size,
                              "Could not allocate Fallout storage");
                return 0;
            }
            ts_sister_fallout_sync_controls(&runtime->fallout,
                                             &runtime->parameters.fx.fallout);
        }
        if (!runtime->post_fx.ready || runtime->post_fx.sample_rate != sample_rate) {
            if (!ts_sister_post_fx_reconfigure(&runtime->post_fx, sample_rate)) {
                runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
                publish_snapshot(runtime);
                runtime_error(error, error_size,
                              "Could not allocate post-effects storage");
                return 0;
            }
            ts_sister_post_fx_sync_controls(&runtime->post_fx,
                                             &runtime->parameters.fx);
        }
        if (!runtime->limiter.ready ||
            runtime->limiter.sample_rate != sample_rate) {
            if (!ts_sister_limiter_reconfigure(&runtime->limiter,
                                                sample_rate)) {
                runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
                publish_snapshot(runtime);
                runtime_error(error, error_size,
                              "Could not allocate output limiter lookahead");
                return 0;
            }
        }
        runtime->output_channels = output_channels;
        runtime->warnings &= ~(uint32_t)TS_SISTER_WARNING_DEVICE_CONTRACT;
        publish_snapshot(runtime);
        runtime_error(error, error_size, "");
        return 1;
    }
    if (sample_rate == 0u || output_channels != 2u ||
        !ts_sister_fallout_reconfigure(&runtime->fallout, sample_rate) ||
        !ts_sister_post_fx_reconfigure(&runtime->post_fx, sample_rate) ||
        !ts_sister_limiter_reconfigure(&runtime->limiter, sample_rate) ||
        !ts_sister_machine_reconfigure(
            &runtime->machine, sample_rate, runtime->machine.buffer.channels,
            runtime->machine.parameters.buffer_seconds)) {
        runtime->warnings |= sample_rate == 0u || output_channels != 2u ?
            TS_SISTER_WARNING_DEVICE_CONTRACT : TS_SISTER_WARNING_ALLOCATION;
        ts_sister_runtime_disable(runtime);
        runtime_error(error, error_size,
                      "Sister restart failed; ordinary audio remains available");
        return 0;
    }
    runtime->output_channels = output_channels;
    runtime->parameters = runtime->machine.parameters;
    ts_sister_post_fx_sync_controls(&runtime->post_fx, &runtime->parameters.fx);
    ts_sister_fallout_sync_controls(&runtime->fallout,
                                    &runtime->parameters.fx.fallout);
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    runtime->master_feedback_current = 0.0f;
    runtime->master_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    runtime->fallout_feedback_current = 0.0f;
    runtime->fallout_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    ts_capture_free(&runtime->capture);
    ts_performance_clear(&runtime->performance);
    ts_sister_wave_publisher_clear(&runtime->waveform,
                                   runtime->machine.buffer.channels);
    runtime->waveform_capacity_frames = runtime->machine.buffer.capacity_frames;
    runtime->warnings &= ~(uint32_t)(TS_SISTER_WARNING_DEVICE_CONTRACT |
                                     TS_SISTER_WARNING_ALLOCATION);
    publish_snapshot(runtime);
    runtime_error(error, error_size, "");
    return 1;
}

void ts_sister_runtime_set_parameters(TsSisterRuntime *runtime,
                                      const TsSisterParameters *parameters)
{
    uint32_t sample_rate;
    if (runtime == NULL || parameters == NULL) return;
    runtime->parameters = *parameters;
    runtime->parameters_published = 1;
    ts_sister_parameters_sanitize(&runtime->parameters,
        runtime->post_fx.ready ? runtime->post_fx.sample_rate :
        runtime->enabled ? runtime->machine.buffer.sample_rate : 48000u);
    sample_rate = runtime->post_fx.ready ? runtime->post_fx.sample_rate :
                  runtime->enabled ? runtime->machine.buffer.sample_rate :
                  48000u;
    runtime_ramp_set(&runtime->source_gain[0],
                     runtime->parameters.tiles_gain, sample_rate);
    runtime_ramp_set(&runtime->source_gain[1],
                     runtime->parameters.fm_gain, sample_rate);
    runtime_ramp_set(&runtime->source_gain[2],
                     runtime->parameters.external_gain, sample_rate);
    runtime_ramp_set(&runtime->source_gain[3],
                     runtime->parameters.preview_gain, sample_rate);
    runtime_ramp_set(&runtime->ordinary_fx_return_gain,
                     runtime->parameters.fx_return_gain, sample_rate);
    if (runtime->enabled) {
        ts_sister_machine_set_parameters(&runtime->machine, &runtime->parameters);
        runtime->parameters = runtime->machine.parameters;
    }
    ts_sister_post_fx_set_controls(&runtime->post_fx, &runtime->parameters.fx);
    /* Publish sanitized, authoritative slot state back to the controller and
       persistence model after every edit. */
    runtime->parameters.fx = runtime->post_fx.controls;
    ts_sister_fallout_set_controls(&runtime->fallout,
                                   &runtime->parameters.fx.fallout);
    publish_snapshot(runtime);
}

void ts_sister_runtime_recall_fallout_preset(
    TsSisterRuntime *runtime, const TsSisterFalloutControls *controls)
{
    TsSisterFalloutControls target;
    uint32_t sample_rate;
    if (runtime == NULL || controls == NULL) return;
    target = *controls;
    sample_rate = runtime->post_fx.ready ? runtime->post_fx.sample_rate :
                  runtime->enabled ? runtime->machine.buffer.sample_rate :
                  48000u;
    target.enabled = runtime->parameters.fx.fallout.enabled;
    target.rise_retrigger = runtime->parameters.fx.fallout.rise_retrigger + 1u;
    ts_sister_fallout_controls_sanitize(&target);
    runtime->parameters.fx.fallout = target;
    runtime->parameters_published = 1;
    ts_sister_parameters_sanitize(&runtime->parameters, sample_rate);
    if (runtime->enabled)
        runtime->machine.parameters.fx.fallout = runtime->parameters.fx.fallout;
    ts_sister_fallout_recall_preset(
        &runtime->fallout, &runtime->parameters.fx.fallout);
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_selected_preset(TsSisterRuntime *runtime,
                                           const char *name)
{
    if (runtime == NULL) return;
    snprintf(runtime->selected_preset, sizeof(runtime->selected_preset),
             "%.47s", name != NULL ? name : "");
    runtime->selected_preset_modified = 0;
    publish_snapshot(runtime);
}

void ts_sister_runtime_mark_selected_preset_modified(TsSisterRuntime *runtime)
{
    if (runtime == NULL || runtime->selected_preset[0] == '\0') return;
    runtime->selected_preset_modified = 1;
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_rolling(TsSisterRuntime *runtime, int rolling)
{
    uint32_t sample_rate;
    if (runtime == NULL) return;
    runtime->rolling = rolling != 0;
    sample_rate = runtime->enabled ? runtime->machine.buffer.sample_rate :
                  48000u;
    if (runtime->enabled)
        runtime_ramp_set(&runtime->direct_tile_route,
                         runtime->rolling ? 1.0f : 0.0f, sample_rate);
    else
        runtime_ramp_reset(&runtime->direct_tile_route, 0.0f);
    if (runtime->enabled)
        ts_sister_machine_set_rolling(&runtime->machine, runtime->rolling);
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_hold(TsSisterRuntime *runtime, int held)
{
    if (runtime == NULL) return;
    runtime->held = held != 0;
    if (runtime->enabled)
        ts_sister_machine_set_hold(&runtime->machine, runtime->held);
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_monitor(TsSisterRuntime *runtime, int enabled)
{
    uint32_t sample_rate;
    if (runtime == NULL) return;
    runtime->monitor_enabled = enabled != 0;
    sample_rate = runtime->enabled ? runtime->machine.buffer.sample_rate :
                  runtime->post_fx.ready ? runtime->post_fx.sample_rate :
                  48000u;
    if (runtime->enabled)
        runtime_ramp_set(&runtime->monitor_route,
                         runtime->monitor_enabled ? 1.0f : 0.0f,
                         sample_rate);
    else
        runtime_ramp_reset(&runtime->monitor_route, 0.0f);
    publish_snapshot(runtime);
}

int ts_sister_runtime_request_clear(TsSisterRuntime *runtime)
{
    int result;
    if (runtime == NULL || !runtime->enabled) return 0;
    result = ts_sister_machine_request_clear(&runtime->machine);
    publish_snapshot(runtime);
    return result;
}

int ts_sister_runtime_can_clear(const TsSisterRuntime *runtime)
{
    return runtime != NULL && runtime->enabled &&
           ts_sister_machine_can_clear(&runtime->machine);
}

int ts_sister_runtime_perform_clear(TsSisterRuntime *runtime)
{
    int result;
    if (runtime == NULL || !runtime->enabled) return 0;
    result = ts_sister_machine_perform_clear(&runtime->machine);
    if (result) {
        runtime->last_frame = (TsSisterRuntimeFrame){0};
        ts_sister_wave_publisher_clear(&runtime->waveform,
                                       runtime->machine.buffer.channels);
    }
    publish_snapshot(runtime);
    return result;
}

void ts_sister_runtime_set_sources(TsSisterRuntime *runtime,
                                   uint8_t source_switches)
{
    uint32_t sample_rate;
    if (runtime == NULL) return;
    runtime->source_switches = source_switches & TS_SISTER_SOURCE_ALL;
    sample_rate = runtime->enabled ? runtime->machine.buffer.sample_rate :
                  runtime->post_fx.ready ? runtime->post_fx.sample_rate :
                  48000u;
    for (int source_index = 0; source_index < TS_SISTER_SOURCE_COUNT;
         ++source_index) {
        float target = source_route_target(runtime, source_index);
        if (runtime->enabled)
            runtime_ramp_set(&runtime->source_route[source_index], target,
                             sample_rate);
        else
            runtime_ramp_reset(&runtime->source_route[source_index], target);
    }
    publish_snapshot(runtime);
}

uint8_t ts_sister_runtime_sources(const TsSisterRuntime *runtime)
{
    return runtime != NULL ? runtime->source_switches : 0u;
}

float ts_sister_runtime_source_route(const TsSisterRuntime *runtime,
                                     int source_index)
{
    float route;
    if (runtime == NULL || source_index < 0 ||
        source_index >= TS_SISTER_SOURCE_COUNT) return 0.0f;
    route = runtime->source_route[source_index].current;
    if (!isfinite(route) || route < 0.0f) return 0.0f;
    return route > 1.0f ? 1.0f : route;
}

float ts_sister_runtime_direct_tile_route(const TsSisterRuntime *runtime)
{
    float route;
    if (runtime == NULL || !runtime->enabled || runtime->callback_failed)
        return 0.0f;
    route = runtime->direct_tile_route.current;
    if (!isfinite(route) || route < 0.0f) return 0.0f;
    return route > 1.0f ? 1.0f : route;
}

int ts_sister_runtime_owns_direct_tile_bus(const TsSisterRuntime *runtime)
{
    return runtime != NULL && runtime->enabled && !runtime->callback_failed &&
           runtime->machine.rolling;
}

int ts_sister_runtime_set_page(TsSisterRuntime *runtime, size_t page,
                               const TsInstrument *instrument)
{
    if (runtime == NULL || page >= TS_SISTER_RUNTIME_PAGE_LIMIT) return 0;
    ts_performance_clear(&runtime->performance);
    runtime->active_page = page;
    (void)ts_sister_runtime_validate_source_mask(runtime, instrument);
    publish_snapshot(runtime);
    return 1;
}

uint16_t ts_sister_runtime_source_mask(const TsSisterRuntime *runtime)
{
    if (runtime == NULL || runtime->active_page >= TS_SISTER_RUNTIME_PAGE_LIMIT)
        return 0u;
    return runtime->page_source_masks[runtime->active_page];
}

TsSisterTileShiftResult ts_sister_runtime_shift_sample_tile(
    TsSisterRuntime *runtime, TsInstrument *instrument, int slot,
    char *status, size_t status_size)
{
    uint16_t bit;
    int was_source;
    if (runtime == NULL || instrument == NULL || slot < 0 ||
        slot >= TS_BANK_SLOT_COUNT) {
        if (status != NULL && status_size > 0u)
            snprintf(status, status_size, "Invalid Sample Bank tile");
        return TS_SISTER_TILE_SHIFT_FAILED;
    }
    if (!instrument->bank[slot].occupied) {
        if (!ts_instrument_bank_capture(instrument, slot,
                                        TS_BANK_CAPTURE_CURRENT,
                                        status, status_size))
            return TS_SISTER_TILE_SHIFT_FAILED;
        /* Copy and Capture results are deliberately ordinary tiles. */
        (void)ts_sister_runtime_set_source_slot(runtime, instrument, slot, 0);
        if (status != NULL && status_size > 0u)
            snprintf(status, status_size,
                     "TILE %02d COPIED - SHIFT-CLICK AGAIN TO ADD SOURCE",
                     slot + 1);
        return TS_SISTER_TILE_SHIFT_COPIED;
    }
    bit = (uint16_t)(1u << slot);
    was_source = (ts_sister_runtime_source_mask(runtime) & bit) != 0u;
    if (!ts_sister_runtime_toggle_source_slot(runtime, instrument, slot)) {
        if (status != NULL && status_size > 0u)
            snprintf(status, status_size, "TILE %02d SOURCE UNCHANGED", slot + 1);
        return TS_SISTER_TILE_SHIFT_FAILED;
    }
    if (status != NULL && status_size > 0u)
        snprintf(status, status_size, "TILE %02d SISTER SOURCE %s",
                 slot + 1, was_source ? "REMOVED" : "ADDED");
    return was_source ? TS_SISTER_TILE_SHIFT_SOURCE_REMOVED :
                        TS_SISTER_TILE_SHIFT_SOURCE_ADDED;
}

int ts_sister_runtime_tiles_insert_active(const TsSisterRuntime *runtime)
{
    return runtime != NULL && runtime->enabled &&
           (runtime->source_switches & TS_SISTER_SOURCE_TILES) != 0u;
}

int ts_sister_runtime_set_source_slot(TsSisterRuntime *runtime,
                                      const TsInstrument *instrument,
                                      int slot, int selected)
{
    uint16_t bit;
    uint16_t *mask;
    if (runtime == NULL || instrument == NULL || slot < 0 ||
        slot >= TS_BANK_SLOT_COUNT ||
        runtime->active_page >= TS_SISTER_RUNTIME_PAGE_LIMIT)
        return 0;
    bit = (uint16_t)(1u << slot);
    mask = &runtime->page_source_masks[runtime->active_page];
    if (selected) {
        const TsBankSlot *source = &instrument->bank[slot];
        if (!source->occupied || source->sample.data == NULL ||
            source->sample.frames < 2u || source->sample.sample_rate == 0u ||
            ts_instrument_bank_is_blank_canvas(instrument, slot))
            return 0;
        *mask |= bit;
    } else {
        *mask &= (uint16_t)~bit;
        ts_performance_stop_sources(&runtime->performance, bit);
    }
    publish_snapshot(runtime);
    return 1;
}

int ts_sister_runtime_toggle_source_slot(TsSisterRuntime *runtime,
                                         const TsInstrument *instrument,
                                         int slot)
{
    uint16_t bit;
    if (runtime == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) return 0;
    bit = (uint16_t)(1u << slot);
    return ts_sister_runtime_set_source_slot(
        runtime, instrument, slot,
        (ts_sister_runtime_source_mask(runtime) & bit) == 0u);
}

int ts_sister_runtime_replace_source_slot(TsSisterRuntime *runtime,
                                          const TsInstrument *instrument,
                                          int slot)
{
    uint16_t previous;
    uint16_t bit;
    if (runtime == NULL || instrument == NULL ||
        slot < 0 || slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[slot].occupied ||
        instrument->bank[slot].sample.data == NULL ||
        instrument->bank[slot].sample.frames < 2u)
        return 0;
    previous = runtime->page_source_masks[runtime->active_page];
    bit = (uint16_t)(1u << slot);
    ts_performance_stop_sources(&runtime->performance,
                                (uint16_t)(previous & ~bit));
    runtime->page_source_masks[runtime->active_page] = bit;
    publish_snapshot(runtime);
    return 1;
}

void ts_sister_runtime_clear_source_mask(TsSisterRuntime *runtime)
{
    if (runtime == NULL || runtime->active_page >= TS_SISTER_RUNTIME_PAGE_LIMIT)
        return;
    runtime->page_source_masks[runtime->active_page] = 0u;
    ts_performance_clear(&runtime->performance);
    publish_snapshot(runtime);
}

uint16_t ts_sister_runtime_validate_source_mask(
    TsSisterRuntime *runtime, const TsInstrument *instrument)
{
    uint16_t mask;
    uint16_t valid = 0u;
    if (runtime == NULL || runtime->active_page >= TS_SISTER_RUNTIME_PAGE_LIMIT)
        return 0u;
    mask = runtime->page_source_masks[runtime->active_page];
    if (instrument != NULL) {
        for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
            const TsBankSlot *source = &instrument->bank[slot];
            uint16_t bit = (uint16_t)(1u << slot);
            if ((mask & bit) != 0u && source->occupied &&
                source->sample.data != NULL && source->sample.frames >= 2u &&
                source->sample.sample_rate != 0u &&
                !ts_instrument_bank_is_blank_canvas(instrument, slot))
                valid |= bit;
        }
    }
    runtime->page_source_masks[runtime->active_page] = valid;
    ts_performance_stop_sources(&runtime->performance,
                                (uint16_t)(mask & (uint16_t)~valid));
    return valid;
}

void ts_sister_runtime_prepare_slot_replacement(TsSisterRuntime *runtime,
                                                int slot)
{
    if (runtime == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) return;
    ts_performance_stop_sources(&runtime->performance,
                                (uint16_t)(1u << slot));
    publish_snapshot(runtime);
}

void ts_sister_runtime_sync_sources(TsSisterRuntime *runtime,
                                    const TsInstrument *instrument,
                                    int output_rate)
{
    if (runtime == NULL || instrument == NULL || output_rate <= 0) return;
    (void)ts_sister_runtime_validate_source_mask(runtime, instrument);
    if (ts_performance_count(&runtime->performance) > 0)
        ts_performance_sync(&runtime->performance, instrument, output_rate);
    publish_snapshot(runtime);
}

int ts_sister_runtime_note_on(TsSisterRuntime *runtime,
                              const TsInstrument *instrument,
                              const TsNoteEvent *event, int latched,
                              int output_rate)
{
    uint16_t mask;
    int started;
    if (runtime == NULL || !runtime->enabled || instrument == NULL ||
        event == NULL || output_rate <= 0 ||
        (runtime->source_switches & TS_SISTER_SOURCE_TILES) == 0u)
        return 0;
    mask = ts_sister_runtime_validate_source_mask(runtime, instrument);
    ts_performance_set_attack_ms(&runtime->performance,
                                 TS_AUDITION_ATTACK_MS_DEFAULT);
    started = ts_performance_trigger_group_event(
        &runtime->performance, instrument, mask, event, latched, output_rate);
    publish_snapshot(runtime);
    return started;
}

void ts_sister_runtime_note_off(TsSisterRuntime *runtime,
                                const TsNoteEvent *event)
{
    if (runtime == NULL) return;
    ts_performance_release_event(&runtime->performance, event);
    publish_snapshot(runtime);
}

void ts_sister_runtime_release_midi_channel(TsSisterRuntime *runtime,
                                            int channel)
{
    if (runtime == NULL) return;
    ts_performance_release_midi_channel(&runtime->performance, channel);
    publish_snapshot(runtime);
}

void ts_sister_runtime_panic(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    ts_performance_clear(&runtime->performance);
    publish_snapshot(runtime);
}

TsSisterRuntimeFrame ts_sister_runtime_process_frame(
    TsSisterRuntime *runtime, const TsSisterSourceFrames *sources)
{
    TsSisterRuntimeFrame frame = {0};
    TsSisterSourceFrames source = {0};
    TsStereoFrame tile_raw;
    TsStereoFrame tile_bus;
    TsStereoFrame input = {0.0f, 0.0f};
    TsSisterOutput output;
    TsStereoFrame causal_return;
    float source_gain[TS_SISTER_SOURCE_COUNT];
    float source_route[TS_SISTER_SOURCE_COUNT];
    float route_energy = 0.0f;
    float monitor_route;
    float master_fx_gate;
    float fallout_gate;
    frame.dry_monitor_gain = 1.0f;
    if (runtime == NULL) return frame;
    if (sources != NULL) source = *sources;
    source.tiles = ts_stereo_frame_sanitize(source.tiles);
    source.fm = ts_stereo_frame_sanitize(source.fm);
    source.external = ts_stereo_frame_sanitize(source.external);
    source.preview = ts_stereo_frame_sanitize(source.preview);
    tile_bus = ts_performance_read_stereo(&runtime->performance, &tile_raw);
    tile_bus = frame_add(tile_bus, source.tiles);
    (void)tile_raw;
    if (!runtime->enabled || runtime->callback_failed) {
        runtime->last_frame = frame;
        publish_snapshot(runtime);
        return frame;
    }
    runtime->monitor_dry_current = monitor_approach(
        runtime->monitor_dry_current, runtime->parameters.monitor_dry,
        runtime->machine.buffer.sample_rate);
    runtime->monitor_wet_current = monitor_approach(
        runtime->monitor_wet_current, runtime->parameters.monitor_wet,
        runtime->machine.buffer.sample_rate);
    frame.dry_monitor_gain = runtime->monitor_dry_current;
    for (int source_index = 0; source_index < TS_SISTER_SOURCE_COUNT;
         ++source_index) {
        source_gain[source_index] = runtime_ramp_advance(
            &runtime->source_gain[source_index]);
        source_route[source_index] = runtime_ramp_advance(
            &runtime->source_route[source_index]);
        route_energy += source_route[source_index] *
                        source_route[source_index];
    }
    input = frame_add(input, frame_scale(
        tile_bus, source_gain[0] * source_route[0]));
    input = frame_add(input, frame_scale(
        source.fm, source_gain[1] * source_route[1]));
    input = frame_add(input, frame_scale(
        source.external, source_gain[2] * source_route[2]));
    input = frame_add(input, frame_scale(
        source.preview, source_gain[3] * source_route[3]));
    if (route_energy > 1.0f)
        input = frame_scale(input, 1.0f / sqrtf(route_energy));
    /* PRE slots touch only newly arriving source material. They run before
       Sister's input trim, rolling write, Duck detector, and head feedback;
       material already resident in the tape buffer is never processed again. */
    input = ts_sister_post_fx_process_pre(&runtime->post_fx, input, 0);
    monitor_route = runtime_ramp_advance(&runtime->monitor_route);
    (void)runtime_ramp_advance(&runtime->direct_tile_route);
    master_fx_gate = ts_sister_post_fx_master_engage(&runtime->post_fx);
    fallout_gate = ts_sister_fallout_engage(&runtime->fallout);
    causal_return = frame_add(
        master_fx_gate > 0.0f ? runtime->master_feedback_previous :
                               (TsStereoFrame){0.0f, 0.0f},
        fallout_gate > 0.0f ? runtime->fallout_feedback_previous :
                              (TsStereoFrame){0.0f, 0.0f});
    {
        float peak = fmaxf(fabsf(causal_return.l), fabsf(causal_return.r));
        if (!isfinite(peak)) causal_return = (TsStereoFrame){0.0f, 0.0f};
        else if (peak > 1.5f)
            causal_return = frame_scale(causal_return, 1.5f / peak);
    }
    output = ts_sister_machine_process_frame_with_insert_fx(
        &runtime->machine, &runtime->fallout, &runtime->post_fx,
        input, input, causal_return);
    if (runtime->waveform_capacity_frames !=
        runtime->machine.buffer.capacity_frames) {
        ts_sister_wave_publisher_resize(
            &runtime->waveform, runtime->waveform_capacity_frames,
            runtime->machine.buffer.capacity_frames,
            runtime->machine.master_clock == 0u ? 0u :
            runtime->machine.master_clock - 1u);
        runtime->waveform_capacity_frames =
            runtime->machine.buffer.capacity_frames;
    }
    master_fx_gate = ts_sister_post_fx_master_engage(&runtime->post_fx);
    if (master_fx_gate <= 0.0f) {
        runtime->master_feedback_current = 0.0f;
        runtime->master_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    } else {
        runtime->master_feedback_current = monitor_approach(
            runtime->master_feedback_current,
            runtime->parameters.fx.master_feedback * 1.35f * master_fx_gate,
            runtime->machine.buffer.sample_rate);
        if (runtime->parameters.fx.master_feedback <= 0.0f &&
            runtime->master_feedback_current < 0.000001f)
            runtime->master_feedback_current = 0.0f;
        TsStereoFrame feedback = frame_scale(
            ts_stereo_frame_sanitize(output.post_fx),
            runtime->master_feedback_current);
        /* Linked, bounded conditioning retains intentional self-oscillation
           without allowing a broken sample to poison rolling memory. */
        float peak = fmaxf(fabsf(feedback.l), fabsf(feedback.r));
        if (!isfinite(peak)) feedback = (TsStereoFrame){0.0f, 0.0f};
        else if (peak > 1.5f) feedback = frame_scale(feedback, 1.5f / peak);
        feedback.l = tanhf(feedback.l);
        feedback.r = tanhf(feedback.r);
        runtime->master_feedback_previous = ts_stereo_frame_sanitize(feedback);
    }
    fallout_gate = ts_sister_fallout_engage(&runtime->fallout);
    if (fallout_gate <= 0.0f) {
        runtime->fallout_feedback_current = 0.0f;
        runtime->fallout_feedback_previous = (TsStereoFrame){0.0f, 0.0f};
    } else {
        runtime->fallout_feedback_current = monitor_approach(
            runtime->fallout_feedback_current,
            ts_sister_fallout_feedback_amount(&runtime->fallout) * 1.20f *
                fallout_gate,
            runtime->machine.buffer.sample_rate);
        if (!runtime->parameters.fx.fallout.enabled &&
            runtime->fallout_feedback_current < 0.000001f)
            runtime->fallout_feedback_current = 0.0f;
        TsStereoFrame feedback = frame_scale(
            ts_stereo_frame_sanitize(output.fallout_wet),
            runtime->fallout_feedback_current);
        float peak = fmaxf(fabsf(feedback.l), fabsf(feedback.r));
        if (!isfinite(peak)) feedback = (TsStereoFrame){0.0f, 0.0f};
        else if (peak > 1.5f) feedback = frame_scale(feedback, 1.5f / peak);
        feedback.l = tanhf(feedback.l);
        feedback.r = tanhf(feedback.r);
        runtime->fallout_feedback_previous =
            ts_stereo_frame_sanitize(feedback);
    }
    frame.input = ts_stereo_frame_sanitize(output.input);
    frame.duck_sidechain = frame.input;
    ts_sister_wave_publisher_push(&runtime->waveform, output.write,
                                  output.write_position,
                                  runtime->machine.buffer.capacity_frames,
                                  runtime->machine.buffer.channels,
                                  output.wrote);
    frame.tap[TS_SISTER_TAP_MIX] = ts_stereo_frame_sanitize(output.mix);
    frame.tap[TS_SISTER_TAP_H1] = ts_stereo_frame_sanitize(output.head[0]);
    frame.tap[TS_SISTER_TAP_H2] = ts_stereo_frame_sanitize(output.head[1]);
    frame.tap[TS_SISTER_TAP_H3] = ts_stereo_frame_sanitize(output.head[2]);
    if (atomic_load_explicit(&runtime->capture.state, memory_order_relaxed) ==
        TS_CAPTURE_RECORDING)
        (void)ts_capture_write_frame(
            &runtime->capture, selected_tap(&frame, runtime->selected_tap));
    frame.monitor_return = frame_scale(frame_add(
        frame_scale(frame.input, runtime->monitor_dry_current),
        frame_scale(frame.tap[TS_SISTER_TAP_MIX],
                    runtime->monitor_wet_current)), monitor_route);
    runtime->last_frame = frame;
    ++runtime->processed_frames;
    publish_frame_snapshot(runtime);
    return frame;
}

TsStereoFrame ts_sister_runtime_process_ordinary_post_fx(
    TsSisterRuntime *runtime, TsStereoFrame input)
{
    TsStereoFrame output;
    float return_gain;
    if (runtime == NULL || !runtime->post_fx.ready)
        return ts_stereo_frame_sanitize(input);
    input = ts_stereo_frame_sanitize(input);
    output = ts_sister_post_fx_process(&runtime->post_fx,
        TS_SISTER_HEAD_COUNT, input, 0);
    return_gain = runtime_ramp_advance(&runtime->ordinary_fx_return_gain);
    return frame_effect_return(input, output, return_gain);
}

TsStereoFrame ts_sister_runtime_process_output(TsSisterRuntime *runtime,
                                               TsStereoFrame input)
{
    TsStereoFrame output;
    float pre_peak = 0.0f;
    if (runtime == NULL) return ts_stereo_frame_sanitize(input);
    output = ts_sister_limiter_process(&runtime->limiter, input,
                                       NULL, &pre_peak);
    if (runtime->snapshot_batch_depth == 0u)
        runtime->limiter_gain_reduction_db =
            ts_sister_limiter_gain_reduction_db(&runtime->limiter);
    runtime->limiter_input_peak = pre_peak;
    output_meter_update(runtime, output, pre_peak);
    publish_frame_snapshot(runtime);
    return output;
}

void ts_sister_runtime_configure_limiter(TsSisterRuntime *runtime,
                                         int enabled, float ceiling_db,
                                         float lookahead_ms,
                                         float release_ms)
{
    if (runtime == NULL) return;
    ts_sister_limiter_set_controls(&runtime->limiter, enabled, ceiling_db,
                                   lookahead_ms, release_ms);
    runtime->limiter_gain_reduction_db = 0.0f;
    runtime->limiter_input_peak = 0.0f;
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_limiter_enabled(TsSisterRuntime *runtime,
                                           int enabled)
{
    if (runtime == NULL) return;
    ts_sister_limiter_set_enabled(&runtime->limiter, enabled);
    publish_snapshot(runtime);
}

void ts_sister_runtime_process_block(TsSisterRuntime *runtime,
                                     const TsSisterSourceFrames *sources,
                                     TsSisterRuntimeFrame *output,
                                     size_t frames)
{
    if (runtime == NULL || output == NULL) return;
    ts_sister_runtime_begin_audio_block(runtime);
    for (size_t frame = 0u; frame < frames; ++frame)
        output[frame] = ts_sister_runtime_process_frame(
            runtime, sources != NULL ? &sources[frame] : NULL);
    ts_sister_runtime_end_audio_block(runtime);
}

int ts_sister_runtime_get_wave_snapshot(const TsSisterRuntime *runtime,
                                        TsSisterWaveSnapshot *snapshot)
{
    return runtime != NULL &&
           ts_sister_wave_snapshot_get(&runtime->waveform, snapshot);
}

int ts_sister_runtime_find_destination(const TsSisterRuntime *runtime,
                                       const TsInstrument *instrument,
                                       int preferred_slot)
{
    uint16_t mask;
    if (runtime == NULL || instrument == NULL) return -1;
    mask = ts_sister_runtime_source_mask(runtime);
    if (preferred_slot >= 0 && preferred_slot < TS_BANK_SLOT_COUNT &&
        (mask & (uint16_t)(1u << preferred_slot)) == 0u &&
        !instrument->bank[preferred_slot].locked &&
        ts_instrument_bank_is_blank_canvas(instrument, preferred_slot))
        return preferred_slot;
    for (int distance = 1; distance <= TS_BANK_SLOT_COUNT; ++distance) {
        int origin = preferred_slot >= 0 && preferred_slot < TS_BANK_SLOT_COUNT ?
                     preferred_slot : -1;
        int slot = (origin + distance) % TS_BANK_SLOT_COUNT;
        if ((mask & (uint16_t)(1u << slot)) == 0u &&
            !instrument->bank[slot].locked &&
            ts_instrument_bank_is_blank_canvas(instrument, slot))
            return slot;
    }
    return -1;
}

static int validate_destination(TsSisterRuntime *runtime,
                                const TsInstrument *instrument,
                                int destination_slot,
                                uint16_t transient_capture_sources,
                                int overdub,
                                char *error, size_t error_size)
{
    uint16_t bit;
    if (runtime == NULL || instrument == NULL || destination_slot < 0 ||
        destination_slot >= TS_BANK_SLOT_COUNT) {
        runtime_error(error, error_size, "Invalid Sister destination");
        return 0;
    }
    bit = (uint16_t)(1u << destination_slot);
    runtime->source_target_conflict = 0;
    if ((ts_sister_runtime_source_mask(runtime) & bit) != 0u ||
        (transient_capture_sources & bit) != 0u) {
        runtime->destination_status = TS_SISTER_DESTINATION_SOURCE_CONFLICT;
        runtime->source_target_conflict = 1;
        publish_snapshot(runtime);
        runtime_error(error, error_size,
                      "Sister target cannot be an active source");
        return 0;
    }
    if (instrument->bank[destination_slot].locked) {
        runtime->destination_status = TS_SISTER_DESTINATION_LOCKED;
        publish_snapshot(runtime);
        runtime_error(error, error_size,
                      "Sister target is protected");
        return 0;
    }
    if (overdub) {
        if (!instrument->bank[destination_slot].occupied ||
            instrument->bank[destination_slot].sample.data == NULL) {
            runtime->destination_status = TS_SISTER_DESTINATION_STALE;
            publish_snapshot(runtime);
            runtime_error(error, error_size,
                          "Sister Overdub needs an occupied target");
            return 0;
        }
    } else if (!ts_instrument_bank_is_blank_canvas(instrument,
                                                    destination_slot)) {
        runtime->destination_status = TS_SISTER_DESTINATION_OCCUPIED;
        publish_snapshot(runtime);
        runtime_error(error, error_size,
                      "Sister Capture needs a blank silent target");
        return 0;
    }
    runtime->destination_status = TS_SISTER_DESTINATION_READY;
    publish_snapshot(runtime);
    return 1;
}

int ts_sister_runtime_validate_capture_target(
    TsSisterRuntime *runtime, const TsInstrument *instrument,
    int destination_slot, uint16_t transient_capture_sources, int overdub,
    char *error, size_t error_size)
{
    if (runtime == NULL || !runtime->enabled) {
        runtime_error(error, error_size, "Enable Sister before Capture");
        return 0;
    }
    if (atomic_load_explicit(&runtime->capture.state, memory_order_acquire) !=
        TS_CAPTURE_IDLE) {
        runtime_error(error, error_size, "Sister Capture is already active");
        return 0;
    }
    return validate_destination(runtime, instrument, destination_slot,
                                transient_capture_sources, overdub != 0,
                                error, error_size);
}

static void install_capture_recorder(TsCaptureRecorder *destination,
                                     TsCaptureRecorder *prepared)
{
    TsCaptureState state = atomic_load_explicit(&prepared->state,
                                                 memory_order_acquire);
    ts_capture_free(destination);
    destination->buffer = prepared->buffer;
    destination->overdub_base = prepared->overdub_base;
    destination->capacity_frames = prepared->capacity_frames;
    destination->recorded_frames = prepared->recorded_frames;
    destination->overdub_base_frames = prepared->overdub_base_frames;
    destination->sample_rate = prepared->sample_rate;
    destination->overdub_base_rate = prepared->overdub_base_rate;
    destination->channels = prepared->channels;
    destination->overdub_base_channels = prepared->overdub_base_channels;
    destination->staged_notes = prepared->staged_notes;
    destination->destination_slot = prepared->destination_slot;
    destination->source_slot = prepared->source_slot;
    destination->provenance_slot = prepared->provenance_slot;
    destination->stopped_early = prepared->stopped_early;
    destination->auto_resize = prepared->auto_resize;
    destination->overdub = prepared->overdub;
    atomic_store_explicit(&destination->state, state, memory_order_release);
    prepared->buffer = NULL;
    prepared->overdub_base = NULL;
    ts_capture_init(prepared);
}

int ts_sister_runtime_install_prepared_capture(
    TsSisterRuntime *runtime, const TsInstrument *instrument,
    TsCaptureRecorder *prepared, TsSisterTap tap,
    uint16_t transient_capture_sources, char *error, size_t error_size)
{
    TsCaptureState prepared_state;
    if (runtime == NULL || !runtime->enabled || prepared == NULL ||
        tap < 0 || tap >= TS_SISTER_TAP_COUNT) {
        runtime_error(error, error_size, "Enable Sister before Capture");
        return 0;
    }
    prepared_state = atomic_load_explicit(&prepared->state,
                                          memory_order_acquire);
    if (prepared_state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
        prepared->buffer == NULL ||
        prepared->source_slot != TS_CAPTURE_SOURCE_SISTER) {
        runtime_error(error, error_size,
                      "Sister Capture buffer is not prepared");
        return 0;
    }
    if (!ts_sister_runtime_validate_capture_target(
            runtime, instrument, prepared->destination_slot,
            transient_capture_sources, prepared->overdub,
            error, error_size))
        return 0;
    install_capture_recorder(&runtime->capture, prepared);
    runtime->selected_tap = tap;
    runtime->capture_transient_source_mask = transient_capture_sources;
    publish_snapshot(runtime);
    runtime_error(error, error_size, "");
    return 1;
}

int ts_sister_runtime_arm_capture(TsSisterRuntime *runtime,
                                  const TsInstrument *instrument,
                                  int destination_slot,
                                  size_t capacity_frames,
                                  uint32_t sample_rate, uint8_t channels,
                                  TsSisterTap tap,
                                  uint16_t transient_capture_sources,
                                  char *error, size_t error_size)
{
    if (runtime == NULL || !runtime->enabled || tap < 0 ||
        tap >= TS_SISTER_TAP_COUNT) {
        runtime_error(error, error_size, "Enable Sister before Capture");
        return 0;
    }
    if (!validate_destination(runtime, instrument, destination_slot,
                              transient_capture_sources, 0,
                              error, error_size)) return 0;
    if (!ts_capture_arm_channels(&runtime->capture, destination_slot,
                                 capacity_frames, sample_rate, channels,
                                 error, error_size)) return 0;
    if (!ts_capture_set_source(&runtime->capture, TS_CAPTURE_SOURCE_SISTER,
                               error, error_size)) {
        ts_capture_free(&runtime->capture);
        return 0;
    }
    runtime->selected_tap = tap;
    runtime->capture_transient_source_mask = transient_capture_sources;
    publish_snapshot(runtime);
    return 1;
}

int ts_sister_runtime_arm_overdub(TsSisterRuntime *runtime,
                                  const TsInstrument *instrument,
                                  int destination_slot,
                                  size_t capacity_frames,
                                  uint32_t sample_rate, TsSisterTap tap,
                                  uint16_t transient_capture_sources,
                                  char *error, size_t error_size)
{
    const TsSample *base;
    if (runtime == NULL || !runtime->enabled || instrument == NULL ||
        tap < 0 || tap >= TS_SISTER_TAP_COUNT) {
        runtime_error(error, error_size, "Enable Sister before Overdub");
        return 0;
    }
    if (!validate_destination(runtime, instrument, destination_slot,
                              transient_capture_sources, 1,
                              error, error_size)) return 0;
    base = &instrument->bank[destination_slot].sample;
    if (!ts_capture_arm_overdub_channels(
            &runtime->capture, destination_slot, capacity_frames, sample_rate,
            base->channels, base->data, base->frames, base->sample_rate,
            base->channels, error, error_size)) return 0;
    if (!ts_capture_set_source(&runtime->capture, TS_CAPTURE_SOURCE_SISTER,
                               error, error_size)) {
        ts_capture_free(&runtime->capture);
        return 0;
    }
    runtime->selected_tap = tap;
    runtime->capture_transient_source_mask = transient_capture_sources;
    publish_snapshot(runtime);
    return 1;
}

int ts_sister_runtime_trigger_capture(TsSisterRuntime *runtime,
                                      char *error, size_t error_size)
{
    int result = runtime != NULL && runtime->enabled ?
        ts_capture_trigger(&runtime->capture, error, error_size) : 0;
    if (!result && (runtime == NULL || !runtime->enabled))
        runtime_error(error, error_size, "Sister is disabled");
    if (runtime != NULL) publish_snapshot(runtime);
    return result;
}

int ts_sister_runtime_stop_capture(TsSisterRuntime *runtime,
                                   char *error, size_t error_size)
{
    int result = runtime != NULL ?
        ts_capture_stop(&runtime->capture, error, error_size) : 0;
    if (runtime != NULL) publish_snapshot(runtime);
    return result;
}

int ts_sister_runtime_cancel_capture(TsSisterRuntime *runtime)
{
    int result;
    if (runtime == NULL) return 0;
    result = ts_capture_cancel(&runtime->capture);
    if (result) {
        ts_capture_free(&runtime->capture);
        runtime->destination_status = TS_SISTER_DESTINATION_NONE;
        runtime->source_target_conflict = 0;
    }
    publish_snapshot(runtime);
    return result;
}

int ts_sister_runtime_commit_capture(TsSisterRuntime *runtime,
                                     TsInstrument *instrument,
                                     int auto_resize,
                                     char *error, size_t error_size)
{
    TsCaptureRecorder *capture;
    int destination;
    int ok;
    if (runtime == NULL || instrument == NULL) {
        runtime_error(error, error_size, "Sister Capture is unavailable");
        return 0;
    }
    capture = &runtime->capture;
    destination = capture->destination_slot;
    if (atomic_load_explicit(&capture->state, memory_order_acquire) !=
        TS_CAPTURE_COMPLETED ||
        !validate_destination(runtime, instrument, destination,
                              runtime->capture_transient_source_mask,
                              capture->overdub, error, error_size))
        return 0;
    if (!capture->overdub && capture->recorded_frames > 0u)
        (void)ts_performance_peak_scale_channels(
            capture->buffer, capture->recorded_frames, capture->channels, 0.98f);
    if (capture->overdub)
        ok = ts_instrument_commit_overdub_channels(
            instrument, destination, TS_CAPTURE_SOURCE_SISTER,
            capture->overdub_base, capture->overdub_base_frames,
            capture->overdub_base_rate, capture->overdub_base_channels,
            capture->buffer, capture->recorded_frames, capture->sample_rate,
            capture->channels, auto_resize, error, error_size);
    else
        ok = ts_instrument_commit_capture_channels(
            instrument, destination, TS_CAPTURE_SOURCE_SISTER,
            capture->buffer, capture->recorded_frames, capture->sample_rate,
            capture->channels, capture->stopped_early, auto_resize,
            error, error_size);
    if (!ok) {
        runtime->destination_status = TS_SISTER_DESTINATION_STALE;
        publish_snapshot(runtime);
        return 0;
    }
    instrument->bank[destination].capture_kind =
        capture_kind_for_tap(runtime->selected_tap);
    ts_capture_free(capture);
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    runtime->source_target_conflict = 0;
    runtime->capture_transient_source_mask = 0u;
    publish_snapshot(runtime);
    runtime_error(error, error_size, "");
    return 1;
}

void ts_sister_runtime_input_available(TsSisterRuntime *runtime,
                                       int available)
{
    uint32_t sample_rate;
    if (runtime == NULL) return;
    runtime->input_available = available != 0;
    sample_rate = runtime->enabled ? runtime->machine.buffer.sample_rate :
                  runtime->post_fx.ready ? runtime->post_fx.sample_rate :
                  48000u;
    if (runtime->enabled)
        runtime_ramp_set(&runtime->source_route[2],
                         source_route_target(runtime, 2), sample_rate);
    else
        runtime_ramp_reset(&runtime->source_route[2],
                           source_route_target(runtime, 2));
    if (runtime->input_available)
        runtime->warnings &= ~(uint32_t)TS_SISTER_WARNING_INPUT_UNAVAILABLE;
    else
        runtime->warnings |= TS_SISTER_WARNING_INPUT_UNAVAILABLE;
    publish_snapshot(runtime);
}

void ts_sister_runtime_project_close(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    memset(runtime->page_source_masks, 0,
           sizeof(runtime->page_source_masks));
    runtime->active_page = 0u;
    runtime->source_target_conflict = 0;
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    ts_performance_clear(&runtime->performance);
    ts_capture_free(&runtime->capture);
    if (runtime->enabled)
        (void)ts_sister_machine_clear_offline(&runtime->machine);
    ts_sister_wave_publisher_clear(&runtime->waveform,
                                   runtime->enabled ?
                                   runtime->machine.buffer.channels : 2u);
    publish_snapshot(runtime);
}

void ts_sister_runtime_fail_silent(TsSisterRuntime *runtime,
                                   uint32_t warning)
{
    if (runtime == NULL) return;
    runtime->callback_failed = 1;
    runtime->warnings |= warning | TS_SISTER_WARNING_CALLBACK;
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    if (runtime->capture.state == TS_CAPTURE_RECORDING)
        (void)ts_capture_cancel(&runtime->capture);
    ts_performance_clear(&runtime->performance);
    publish_snapshot(runtime);
}

int ts_sister_runtime_get_snapshot(const TsSisterRuntime *runtime,
                                   TsSisterRoutingSnapshot *snapshot)
{
    const TsSisterRoutingSnapshotAtomic *source;
    uint64_t before;
    uint64_t after;
    if (runtime == NULL || snapshot == NULL) return 0;
    source = &runtime->snapshot;
    for (int attempt = 0; attempt < 8; ++attempt) {
        before = atomic_load_explicit(&source->revision, memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        snapshot->enabled = atomic_load_explicit(&source->enabled,
                                                 memory_order_relaxed);
        snapshot->rolling = atomic_load_explicit(&source->rolling,
                                                 memory_order_relaxed);
        snapshot->held = atomic_load_explicit(&source->held,
                                              memory_order_relaxed);
        snapshot->monitor_enabled = atomic_load_explicit(
            &source->monitor_enabled, memory_order_relaxed);
        snapshot->source_switches = (uint8_t)atomic_load_explicit(
            &source->source_switches, memory_order_relaxed);
        snapshot->source_mask = (uint16_t)atomic_load_explicit(
            &source->source_mask, memory_order_relaxed);
        snapshot->active_page = (size_t)atomic_load_explicit(
            &source->active_page, memory_order_relaxed);
        snapshot->active_source_voices = atomic_load_explicit(
            &source->active_source_voices, memory_order_relaxed);
        snapshot->selected_tap = (TsSisterTap)atomic_load_explicit(
            &source->selected_tap, memory_order_relaxed);
        snapshot->capture_state = (TsCaptureState)atomic_load_explicit(
            &source->capture_state, memory_order_relaxed);
        snapshot->capture_destination = atomic_load_explicit(
            &source->capture_destination, memory_order_relaxed);
        snapshot->capture_recorded_frames = atomic_load_explicit(
            &source->capture_recorded_frames, memory_order_relaxed);
        snapshot->capture_capacity_frames = atomic_load_explicit(
            &source->capture_capacity_frames, memory_order_relaxed);
        snapshot->destination_status =
            (TsSisterDestinationStatus)atomic_load_explicit(
                &source->destination_status, memory_order_relaxed);
        snapshot->source_input_peak = bits_float(atomic_load_explicit(
            &source->source_input_peak_bits, memory_order_relaxed));
        for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap)
            snapshot->tap_peak[tap] = bits_float(atomic_load_explicit(
                &source->tap_peak_bits[tap], memory_order_relaxed));
        for (int channel = 0; channel < 2; ++channel) {
            snapshot->output_level[channel] = bits_float(
                atomic_load_explicit(&source->output_level_bits[channel],
                                     memory_order_relaxed));
            snapshot->output_peak_hold[channel] = bits_float(
                atomic_load_explicit(&source->output_peak_hold_bits[channel],
                                     memory_order_relaxed));
            snapshot->output_clip[channel] = atomic_load_explicit(
                &source->output_clip[channel], memory_order_relaxed);
        }
        snapshot->limiter_enabled = atomic_load_explicit(
            &source->limiter_enabled, memory_order_relaxed);
        snapshot->limiter_ceiling_db = bits_float(atomic_load_explicit(
            &source->limiter_ceiling_db_bits, memory_order_relaxed));
        snapshot->limiter_gain_reduction_db = bits_float(
            atomic_load_explicit(&source->limiter_gain_reduction_db_bits,
                                 memory_order_relaxed));
        snapshot->limiter_input_peak = bits_float(atomic_load_explicit(
            &source->limiter_input_peak_bits, memory_order_relaxed));
        snapshot->overload_count = atomic_load_explicit(
            &source->overload_count, memory_order_relaxed);
        snapshot->warnings = atomic_load_explicit(
            &source->warnings, memory_order_relaxed);
        snapshot->source_target_conflict = atomic_load_explicit(
            &source->source_target_conflict, memory_order_relaxed);
        snapshot->processed_frames = atomic_load_explicit(
            &source->processed_frames, memory_order_relaxed);
        snapshot->fallout_lfo_phase = bits_float(atomic_load_explicit(
            &source->fallout_lfo_phase_bits, memory_order_relaxed));
        snapshot->fallout_rise_phase = bits_float(atomic_load_explicit(
            &source->fallout_rise_phase_bits, memory_order_relaxed));
        snapshot->fallout_rise_complete = atomic_load_explicit(
            &source->fallout_rise_complete, memory_order_relaxed);
        snapshot->fx_transition_progress = bits_float(atomic_load_explicit(
            &source->fx_transition_progress_bits, memory_order_relaxed));
        snapshot->fx_transition_active = atomic_load_explicit(
            &source->fx_transition_active, memory_order_relaxed);
        snapshot->fx_transition_source =
            (TsSisterFxTransitionSource)atomic_load_explicit(
                &source->fx_transition_source, memory_order_relaxed);
        snapshot->fx_transition_target_enabled = atomic_load_explicit(
            &source->fx_transition_target_enabled, memory_order_relaxed);
        snapshot->fx_transition_topology = atomic_load_explicit(
            &source->fx_transition_topology, memory_order_relaxed);
        snapshot->fx_master_transition_progress = bits_float(
            atomic_load_explicit(&source->fx_master_transition_progress_bits,
                                 memory_order_relaxed));
        snapshot->fx_master_transition_active = atomic_load_explicit(
            &source->fx_master_transition_active, memory_order_relaxed);
        snapshot->fx_master_transition_target_enabled = atomic_load_explicit(
            &source->fx_master_transition_target_enabled,
            memory_order_relaxed);
        snapshot->fallout_component_transition_progress = bits_float(
            atomic_load_explicit(
                &source->fallout_component_transition_progress_bits,
                memory_order_relaxed));
        snapshot->fallout_component_transition_active = atomic_load_explicit(
            &source->fallout_component_transition_active,
            memory_order_relaxed);
        snapshot->fallout_component_transition_source =
            (TsSisterFalloutTransitionSource)atomic_load_explicit(
                &source->fallout_component_transition_source,
                memory_order_relaxed);
        snapshot->fallout_component_transition_target_enabled =
            atomic_load_explicit(
                &source->fallout_component_transition_target_enabled,
                memory_order_relaxed);
        snapshot->fallout_master_transition_progress = bits_float(
            atomic_load_explicit(
                &source->fallout_master_transition_progress_bits,
                memory_order_relaxed));
        snapshot->fallout_master_transition_active = atomic_load_explicit(
            &source->fallout_master_transition_active, memory_order_relaxed);
        snapshot->fallout_master_transition_target_enabled =
            atomic_load_explicit(
                &source->fallout_master_transition_target_enabled,
                memory_order_relaxed);
        snapshot->fallout_preset_transition_progress = bits_float(
            atomic_load_explicit(
                &source->fallout_preset_transition_progress_bits,
                memory_order_relaxed));
        snapshot->fallout_preset_transition_active = atomic_load_explicit(
            &source->fallout_preset_transition_active, memory_order_relaxed);
        after = atomic_load_explicit(&source->revision, memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            snapshot->revision = after;
            return 1;
        }
    }
    return 0;
}

const char *ts_sister_tap_name(TsSisterTap tap)
{
    if (tap == TS_SISTER_TAP_H1) return "H1";
    if (tap == TS_SISTER_TAP_H2) return "H2";
    if (tap == TS_SISTER_TAP_H3) return "H3";
    return "MIX";
}
