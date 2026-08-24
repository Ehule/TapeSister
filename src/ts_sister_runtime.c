#include "tapesister/sister_runtime.h"

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
    atomic_init(&snapshot->destination_status, TS_SISTER_DESTINATION_NONE);
    atomic_init(&snapshot->source_input_peak_bits, float_bits(0.0f));
    for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap)
        atomic_init(&snapshot->tap_peak_bits[tap], float_bits(0.0f));
    atomic_init(&snapshot->overload_count, 0u);
    atomic_init(&snapshot->warnings, 0u);
    atomic_init(&snapshot->source_target_conflict, 0);
    atomic_init(&snapshot->processed_frames, 0u);
}

static void publish_snapshot(TsSisterRuntime *runtime)
{
    TsSisterRoutingSnapshotAtomic *snapshot;
    uint64_t revision;
    uint16_t mask = 0u;
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
    atomic_store_explicit(&snapshot->revision, revision + 2u,
                          memory_order_release);
}

static int source_count(uint8_t switches)
{
    int count = 0;
    switches &= TS_SISTER_SOURCE_ALL;
    while (switches != 0u) {
        count += switches & 1u;
        switches >>= 1u;
    }
    return count;
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
    runtime->rolling = 1;
    runtime->input_available = 1;
    runtime->monitor_dry_current = 1.0f;
    runtime->monitor_wet_current = 1.0f;
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
    ts_performance_clear(&runtime->performance);
    publish_snapshot(runtime);
}

int ts_sister_runtime_enable(TsSisterRuntime *runtime, uint32_t sample_rate,
                             uint8_t output_channels,
                             uint8_t buffer_channels,
                             double duration_seconds,
                             char *error, size_t error_size)
{
    TsSisterMachine machine;
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
    if (!ts_sister_machine_init(&machine, sample_rate, buffer_channels,
                                duration_seconds)) {
        runtime->warnings |= TS_SISTER_WARNING_ALLOCATION;
        publish_snapshot(runtime);
        runtime_error(error, error_size,
                      "Could not allocate Sister rolling storage");
        return 0;
    }
    ts_sister_machine_set_parameters(&machine, &runtime->parameters);
    ts_sister_machine_set_rolling(&machine, runtime->rolling);
    ts_sister_machine_set_hold(&machine, runtime->held);
    ts_sister_machine_free(&runtime->machine);
    runtime->machine = machine;
    runtime->parameters = machine.parameters;
    runtime->enabled = 1;
    runtime->output_channels = output_channels;
    runtime->callback_failed = 0;
    runtime->warnings &= ~(uint32_t)(TS_SISTER_WARNING_DEVICE_CONTRACT |
                                     TS_SISTER_WARNING_ALLOCATION |
                                     TS_SISTER_WARNING_CALLBACK);
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    runtime->processed_frames = 0u;
    runtime->source_target_conflict = 0;
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    ts_capture_free(&runtime->capture);
    ts_performance_clear(&runtime->performance);
    ts_sister_wave_publisher_clear(&runtime->waveform, buffer_channels);
    publish_snapshot(runtime);
    runtime_error(error, error_size, "");
    return 1;
}

void ts_sister_runtime_disable(TsSisterRuntime *runtime)
{
    if (runtime == NULL) return;
    runtime->enabled = 0;
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    ts_performance_clear(&runtime->performance);
    ts_capture_free(&runtime->capture);
    ts_sister_machine_free(&runtime->machine);
    ts_sister_wave_publisher_clear(&runtime->waveform, 2u);
    runtime->output_channels = 0u;
    runtime->destination_status = TS_SISTER_DESTINATION_NONE;
    runtime->source_target_conflict = 0;
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
        runtime->output_channels = output_channels;
        runtime->warnings &= ~(uint32_t)TS_SISTER_WARNING_DEVICE_CONTRACT;
        publish_snapshot(runtime);
        runtime_error(error, error_size, "");
        return 1;
    }
    if (sample_rate == 0u || output_channels != 2u ||
        !ts_sister_machine_reconfigure(
            &runtime->machine, sample_rate, runtime->machine.buffer.channels,
            (double)runtime->machine.buffer.capacity_frames /
                (double)runtime->machine.buffer.sample_rate)) {
        runtime->warnings |= sample_rate == 0u || output_channels != 2u ?
            TS_SISTER_WARNING_DEVICE_CONTRACT : TS_SISTER_WARNING_ALLOCATION;
        ts_sister_runtime_disable(runtime);
        runtime_error(error, error_size,
                      "Sister restart failed; ordinary audio remains available");
        return 0;
    }
    runtime->output_channels = output_channels;
    runtime->parameters = runtime->machine.parameters;
    runtime->last_frame = (TsSisterRuntimeFrame){0};
    ts_capture_free(&runtime->capture);
    ts_performance_clear(&runtime->performance);
    ts_sister_wave_publisher_clear(&runtime->waveform,
                                   runtime->machine.buffer.channels);
    runtime->warnings &= ~(uint32_t)(TS_SISTER_WARNING_DEVICE_CONTRACT |
                                     TS_SISTER_WARNING_ALLOCATION);
    publish_snapshot(runtime);
    runtime_error(error, error_size, "");
    return 1;
}

void ts_sister_runtime_set_parameters(TsSisterRuntime *runtime,
                                      const TsSisterParameters *parameters)
{
    if (runtime == NULL || parameters == NULL) return;
    runtime->parameters = *parameters;
    if (runtime->enabled) {
        ts_sister_machine_set_parameters(&runtime->machine, parameters);
        runtime->parameters = runtime->machine.parameters;
    }
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_selected_preset(TsSisterRuntime *runtime,
                                           const char *name)
{
    if (runtime == NULL) return;
    snprintf(runtime->selected_preset, sizeof(runtime->selected_preset),
             "%.47s", name != NULL ? name : "");
    publish_snapshot(runtime);
}

void ts_sister_runtime_set_rolling(TsSisterRuntime *runtime, int rolling)
{
    if (runtime == NULL) return;
    runtime->rolling = rolling != 0;
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
    if (runtime == NULL) return;
    runtime->monitor_enabled = enabled != 0;
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
    if (runtime == NULL) return;
    runtime->source_switches = source_switches & TS_SISTER_SOURCE_ALL;
    publish_snapshot(runtime);
}

uint8_t ts_sister_runtime_sources(const TsSisterRuntime *runtime)
{
    return runtime != NULL ? runtime->source_switches : 0u;
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
    int armed;
    frame.dry_monitor_gain = 1.0f;
    if (runtime == NULL) return frame;
    if (sources != NULL) source = *sources;
    source.fm = ts_stereo_frame_sanitize(source.fm);
    source.external = ts_stereo_frame_sanitize(source.external);
    source.preview = ts_stereo_frame_sanitize(source.preview);
    tile_bus = ts_performance_read_stereo(&runtime->performance, &tile_raw);
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
    if ((runtime->source_switches & TS_SISTER_SOURCE_TILES) != 0u)
        input = frame_add(input, tile_bus);
    if ((runtime->source_switches & TS_SISTER_SOURCE_FM) != 0u)
        input = frame_add(input, source.fm);
    if ((runtime->source_switches & TS_SISTER_SOURCE_EXT) != 0u &&
        runtime->input_available)
        input = frame_add(input, source.external);
    if ((runtime->source_switches & TS_SISTER_SOURCE_PREVIEW) != 0u)
        input = frame_add(input, source.preview);
    armed = source_count(runtime->source_switches);
    if (armed > 1) input = frame_scale(input, 1.0f / sqrtf((float)armed));
    output = ts_sister_machine_process_frame(&runtime->machine, input, input);
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
    frame.monitor_return = runtime->monitor_enabled ? frame_add(
        frame_scale(frame.input, runtime->monitor_dry_current),
        frame_scale(frame.tap[TS_SISTER_TAP_MIX],
                    runtime->monitor_wet_current)) :
        (TsStereoFrame){0.0f, 0.0f};
    runtime->last_frame = frame;
    ++runtime->processed_frames;
    publish_snapshot(runtime);
    return frame;
}

void ts_sister_runtime_process_block(TsSisterRuntime *runtime,
                                     const TsSisterSourceFrames *sources,
                                     TsSisterRuntimeFrame *output,
                                     size_t frames)
{
    if (runtime == NULL || output == NULL) return;
    for (size_t frame = 0u; frame < frames; ++frame)
        output[frame] = ts_sister_runtime_process_frame(
            runtime, sources != NULL ? &sources[frame] : NULL);
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
    if (runtime == NULL) return;
    runtime->input_available = available != 0;
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
        snapshot->destination_status =
            (TsSisterDestinationStatus)atomic_load_explicit(
                &source->destination_status, memory_order_relaxed);
        snapshot->source_input_peak = bits_float(atomic_load_explicit(
            &source->source_input_peak_bits, memory_order_relaxed));
        for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap)
            snapshot->tap_peak[tap] = bits_float(atomic_load_explicit(
                &source->tap_peak_bits[tap], memory_order_relaxed));
        snapshot->overload_count = atomic_load_explicit(
            &source->overload_count, memory_order_relaxed);
        snapshot->warnings = atomic_load_explicit(
            &source->warnings, memory_order_relaxed);
        snapshot->source_target_conflict = atomic_load_explicit(
            &source->source_target_conflict, memory_order_relaxed);
        snapshot->processed_frames = atomic_load_explicit(
            &source->processed_frames, memory_order_relaxed);
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
