#include "sister_test_helpers.h"
#include "tapesister/input_monitor.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

enum { STRESS_RATE = 1000 };

typedef struct {
    uint64_t frames;
    uint64_t transitions;
    uint64_t finite_checks;
    uint64_t crossings;
    uint64_t resize_commits;
    double checksum;
} StressResult;

static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state != 0u ? *state : UINT32_C(0x6d2b79f5);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static float random_unit(uint32_t *state)
{
    return (float)(random_next(state) >> 8) / 16777215.0f;
}

static double head_age(const TsSisterMachine *machine, size_t index)
{
    double write = (double)(machine->master_clock %
                            machine->buffer.storage_frames);
    return ts_sister_positive_modulo(write - machine->head[index].phase,
                                     machine->buffer.storage_frames);
}

static void verify_runtime(const TsSisterRuntime *runtime,
                           const TsSisterRuntimeFrame *frame,
                           StressResult *result)
{
    const TsSisterMachine *machine = &runtime->machine;
    assert(machine->buffer.data != NULL);
    assert(machine->buffer.capacity_frames >= (size_t)STRESS_RATE * 5u);
    assert(machine->buffer.capacity_frames <= (size_t)STRESS_RATE * 60u);
    assert(machine->buffer.valid_history_frames <=
           machine->buffer.capacity_frames);
    assert(sister_frame_finite(frame->input));
    assert(sister_frame_finite(frame->monitor_return));
    assert(sister_frame_finite(runtime->master_feedback_previous));
    for (size_t tap = 0u; tap < TS_SISTER_TAP_COUNT; ++tap) {
        assert(sister_frame_finite(frame->tap[tap]));
    }
    assert(sister_peak(frame->tap[TS_SISTER_TAP_MIX]) <= 1.0001f);
    for (size_t head = 0u; head < TS_SISTER_HEAD_COUNT; ++head) {
        double age = head_age(machine, head);
        assert(isfinite(machine->head[head].phase));
        assert(isfinite(machine->last_head_position[head]));
        assert(isfinite(age));
        assert(age < (double)machine->buffer.storage_frames);
        assert(machine->head[head].guard_remaining <=
               machine->head[head].guard_total);
    }
    assert(machine->head[0].jump_remaining <= machine->head[0].jump_total);
    result->checksum += frame->tap[TS_SISTER_TAP_MIX].l * 0.61803398875 +
                        frame->tap[TS_SISTER_TAP_MIX].r * 0.38196601125;
    ++result->frames;
    ++result->finite_checks;
}

static void process_frames(TsSisterRuntime *runtime,
                           TsSisterSourceFrames *source,
                           uint32_t *random, size_t frames,
                           StressResult *result)
{
    for (size_t i = 0u; i < frames; ++i) {
        TsSisterRuntimeFrame frame;
        float hardware[TS_INPUT_DEVICE_CHANNEL_MAX];
        float noise = ((float)(int32_t)random_next(random) /
                       (float)INT32_MAX) * 0.08f;
        float phase = (float)(result->frames % 1000u) / 1000.0f;
        hardware[0] = 0.11f * sinf(phase * 6.28318530718f) + noise;
        hardware[1] = 0.07f * cosf(phase * 6.28318530718f) - noise;
        hardware[2] = hardware[0] * 0.73f;
        hardware[3] = hardware[1] * 0.61f;
        hardware[4] = -hardware[0] * 0.37f;
        hardware[5] = -hardware[1] * 0.29f;
        hardware[6] = noise * 0.5f;
        hardware[7] = -noise * 0.5f;
        source->external = ts_input_channel_select(
            hardware, TS_INPUT_DEVICE_CHANNEL_MAX, TS_INPUT_CHANNEL_STEREO);
        source->fm.l = 0.05f * sinf(phase * 12.56637061436f);
        source->fm.r = -source->fm.l * 0.73f;
        source->preview.l = (result->frames % 997u) == 0u ? 0.14f : 0.0f;
        source->preview.r = -source->preview.l * 0.5f;
        frame = ts_sister_runtime_process_frame(runtime, source);
        verify_runtime(runtime, &frame, result);
    }
}

static void head_identity_and_crossing(void)
{
    TsSisterRuntime runtime;
    TsSisterParameters p;
    TsSisterSourceFrames source = {0};
    StressResult result = {0};
    uint32_t random = UINT32_C(0x48324831);
    double h1_phase;
    double h1_current;
    double h1_target;
    double previous_difference;
    assert(sister_test_enable(&runtime, STRESS_RATE, 2u, 5.0));
    p = runtime.parameters;
    p.head1_time_ms = 2000.0f;
    p.head1_level = 0.5f;
    p.head2_scrub = 0.75f;
    p.head2_rate_index = 9; /* 2x: H2 advances through H1 once per frame. */
    p.head2_level = 0.5f;
    p.head3_span = 0.2f;
    p.head3_rate_index = 0;
    p.head3_level = 0.5f;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_EXT);
    process_frames(&runtime, &source, &random, 60u, &result);

    h1_phase = runtime.machine.head[0].phase;
    h1_current = runtime.machine.head[0].current_delay_frames;
    h1_target = runtime.machine.head[0].target_delay_frames;
    p = runtime.parameters;
    p.head2_scrub = 0.63f;
    ts_sister_runtime_set_parameters(&runtime, &p);
    assert(runtime.machine.head[0].phase == h1_phase);
    assert(runtime.machine.head[0].current_delay_frames == h1_current);
    assert(runtime.machine.head[0].target_delay_frames == h1_target);

    previous_difference = head_age(&runtime.machine, 1u) -
                          runtime.machine.head[0].current_delay_frames;
    for (size_t i = 0u; i < 4500u; ++i) {
        double difference;
        process_frames(&runtime, &source, &random, 1u, &result);
        difference = head_age(&runtime.machine, 1u) -
                     runtime.machine.head[0].current_delay_frames;
        if (difference * previous_difference < 0.0) ++result.crossings;
        previous_difference = difference;
        assert(runtime.machine.head[0].phase == h1_phase);
    }
    assert(result.crossings > 0u);
    assert(runtime.post_fx.delay[0].data != runtime.post_fx.delay[1].data);
    assert(runtime.post_fx.delay[1].data != runtime.post_fx.delay[2].data);
    assert(runtime.post_fx.delay[2].data != runtime.post_fx.delay[3].data);
    ts_sister_runtime_free(&runtime);
}

static void continuity_transitions(void)
{
    TsSisterRuntime runtime;
    TsSisterParameters p;
    TsSisterSourceFrames source = {0};
    TsStereoFrame previous = {0};
    float worst = 0.0f;
    uint32_t random = UINT32_C(0xc11c5afe);
    assert(sister_test_enable(&runtime, STRESS_RATE, 2u, 5.0));
    p = runtime.parameters;
    p.head1_level = p.head2_level = p.head3_level = 0.22f;
    p.head1_feedback = p.head2_feedback = 0.15f;
    p.fx.reverb_mix = p.fx.delay_mix = p.fx.distortion_mix = 0.0f;
    p.fx.master_feedback = 0.0f;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_EXT);
    for (size_t i = 0u; i < 18000u; ++i) {
        TsSisterRuntimeFrame frame;
        float phase = (float)i / 50.0f;
        source.external = (TsStereoFrame){0.12f * sinf(phase),
                                          0.09f * cosf(phase)};
        if (i == 1000u) ts_sister_runtime_set_hold(&runtime, 1);
        if (i == 1700u) ts_sister_runtime_set_hold(&runtime, 0);
        if (i == 2400u) ts_sister_runtime_set_rolling(&runtime, 0);
        if (i == 3100u) ts_sister_runtime_set_rolling(&runtime, 1);
        if (i == 3800u) {
            p.head2_scrub = 0.97f;
            p.head2_rate_index = 0;
            ts_sister_runtime_set_parameters(&runtime, &p);
        }
        if (i == 5400u) {
            p.buffer_seconds = 60.0f;
            ts_sister_runtime_set_parameters(&runtime, &p);
        }
        if (i == 8500u) {
            p.buffer_seconds = 5.0f;
            ts_sister_runtime_set_parameters(&runtime, &p);
        }
        if (i == 11600u) {
            p.soak = 0.8f;
            p.soak_targets = TS_SISTER_EFFECT_TARGET_H2;
            ts_sister_runtime_set_parameters(&runtime, &p);
        }
        if (i == 14000u) {
            p.soak_targets = TS_SISTER_EFFECT_TARGET_MIX;
            ts_sister_runtime_set_parameters(&runtime, &p);
        }
        frame = ts_sister_runtime_process_frame(&runtime, &source);
        if (i > 100u) {
            float jump_l = fabsf(frame.tap[TS_SISTER_TAP_MIX].l - previous.l);
            float jump_r = fabsf(frame.tap[TS_SISTER_TAP_MIX].r - previous.r);
            float jump = jump_l > jump_r ? jump_l : jump_r;
            if (jump > worst) worst = jump;
        }
        previous = frame.tap[TS_SISTER_TAP_MIX];
        assert(sister_frame_finite(previous));
        (void)random_next(&random);
    }
    /* The controlled source moves by <0.003/sample. This generous bound
       permits the machine's intentional tape gestures while catching a full-
       scale discontinuity or accidental head-state substitution. */
    assert(worst < 0.75f);
    ts_sister_runtime_free(&runtime);
}

static void apply_transition(TsSisterRuntime *runtime, TsInstrument *instrument,
                             TsSisterParameters *p, uint32_t *random,
                             uint64_t transition, StressResult *result)
{
    uint32_t value = random_next(random);
    switch (value % 16u) {
    case 0u:
        p->head1_time_ms = random_unit(random) * 4000.0f;
        p->head2_scrub = random_unit(random);
        p->head3_span = random_unit(random);
        break;
    case 1u:
        p->head1_level = random_unit(random);
        p->head2_level = random_unit(random);
        p->head3_level = random_unit(random);
        break;
    case 2u:
        p->head2_rate_index = (int)(random_next(random) % TS_SISTER_RATE_COUNT);
        p->head3_rate_index = (int)(random_next(random) % TS_SISTER_RATE_COUNT);
        break;
    case 3u:
        p->head1_feedback = random_unit(random);
        p->head2_feedback = random_unit(random);
        p->fx.master_feedback = random_unit(random);
        break;
    case 4u:
        p->buffer_seconds = 5.0f + (float)(random_next(random) % 56u);
        ++result->resize_commits;
        break;
    case 5u:
        p->soak = random_unit(random);
        p->bleed = random_unit(random);
        p->soak_targets = ts_sister_effect_targets_sanitize(
            (uint8_t)(random_next(random) & TS_SISTER_EFFECT_TARGET_ALL));
        break;
    case 6u:
        p->fx.reverb_type = (TsSisterReverbType)(
            random_next(random) % TS_SISTER_REVERB_TYPE_COUNT);
        p->fx.reverb_mix = random_unit(random);
        p->fx.reverb_decay = random_unit(random);
        p->fx.reverb_targets = ts_sister_effect_targets_sanitize(
            (uint8_t)(random_next(random) & TS_SISTER_EFFECT_TARGET_ALL));
        break;
    case 7u:
        p->fx.delay_time = random_unit(random);
        p->fx.delay_feedback = random_unit(random);
        p->fx.delay_mix = random_unit(random);
        p->fx.delay_targets = ts_sister_effect_targets_sanitize(
            (uint8_t)(random_next(random) & TS_SISTER_EFFECT_TARGET_ALL));
        break;
    case 8u:
        p->fx.distortion_drive = random_unit(random);
        p->fx.distortion_tone = random_unit(random);
        p->fx.distortion_mix = random_unit(random);
        p->fx.distortion_targets = ts_sister_effect_targets_sanitize(
            (uint8_t)(random_next(random) & TS_SISTER_EFFECT_TARGET_ALL));
        break;
    case 9u:
        p->tiles_gain = random_unit(random) * 4.0f;
        p->fm_gain = random_unit(random) * 4.0f;
        p->external_gain = random_unit(random) * 4.0f;
        p->preview_gain = random_unit(random) * 4.0f;
        p->fx_return_gain = random_unit(random) * 2.0f;
        break;
    case 10u:
        ts_sister_runtime_set_sources(runtime,
            (uint8_t)(random_next(random) & TS_SISTER_SOURCE_ALL));
        break;
    case 11u:
        ts_sister_runtime_set_hold(runtime, (int)(random_next(random) & 1u));
        ts_sister_runtime_set_rolling(runtime, (int)(random_next(random) & 1u));
        break;
    case 12u: {
        TsNoteEvent note;
        assert(ts_note_event_midi(&note, 36 + (int)(random_next(random) % 60u),
                                  40 + (int)(random_next(random) % 88u),
                                  1 + (int)(random_next(random) % 4u)));
        if ((transition & 1u) == 0u)
            (void)ts_sister_runtime_note_on(runtime, instrument, &note, 0,
                                            STRESS_RATE);
        else
            ts_sister_runtime_note_off(runtime, &note);
        break;
    }
    case 13u:
        runtime->input_available = (int)(random_next(random) & 1u);
        break;
    case 14u:
        p->wow = random_unit(random) * 10.0f;
        p->drop = random_unit(random) * 100.0f;
        p->width = random_unit(random);
        p->decorrelation_enabled = (int)(random_next(random) & 1u);
        break;
    default:
        p->filter_type = (TsSisterFilterType)(
            random_next(random) % TS_SISTER_FILTER_TYPE_COUNT);
        p->filter_cutoff_hz = 20.0f + random_unit(random) * 19000.0f;
        p->filter_q = 0.1f + random_unit(random) * 19.9f;
        break;
    }
    ts_sister_runtime_set_parameters(runtime, p);
    *p = runtime->parameters;
    ++result->transitions;
}

static StressResult run_stress(uint32_t seed, uint64_t transitions,
                               uint64_t soak_frames)
{
    TsSisterRuntime runtime;
    TsSisterParameters p;
    TsSisterSourceFrames source = {0};
    TsInstrument instrument;
    StressResult result = {0};
    uint32_t random = seed;
    float *rolling_store;
    float *delay_store[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    assert(sister_test_make_tiles(&instrument, 4, 1, STRESS_RATE, 400u));
    assert(sister_test_enable(&runtime, STRESS_RATE, 2u, 5.0));
    p = runtime.parameters;
    p.head1_level = p.head2_level = p.head3_level = 0.55f;
    p.head1_feedback = p.head2_feedback = 0.7f;
    p.soak = 0.5f;
    p.fx.delay_mix = p.fx.reverb_mix = p.fx.distortion_mix = 0.45f;
    p.fx.master_feedback = 0.65f;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_ALL);
    for (int slot = 0; slot < 4; ++slot)
        assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, slot, 1));
    rolling_store = runtime.machine.buffer.data;
    for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i)
        delay_store[i] = runtime.post_fx.delay[i].data;

    for (uint64_t transition = 0u; transition < transitions; ++transition) {
        apply_transition(&runtime, &instrument, &p, &random, transition,
                         &result);
        process_frames(&runtime, &source, &random, 4u, &result);
        assert(runtime.machine.buffer.data == rolling_store);
        for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i)
            assert(runtime.post_fx.delay[i].data == delay_store[i]);
    }

    /* Faster-than-realtime soak: at the 1 kHz deterministic test rate, the
       frame count maps directly to simulated audio seconds without weakening
       any state, feedback, interpolation, or effect work. */
    for (uint64_t frame = 0u; frame < soak_frames; ++frame) {
        if (frame % 1000u == 0u)
            apply_transition(&runtime, &instrument, &p, &random,
                             transitions + frame / 1000u, &result);
        process_frames(&runtime, &source, &random, 1u, &result);
    }
    ts_sister_runtime_panic(&runtime);
    assert(ts_performance_count(&runtime.performance) == 0);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    return result;
}

int main(int argc, char **argv)
{
    uint64_t transitions = 10000u;
    uint64_t soak_frames = 60000u;
    static const uint32_t seeds[] = {
        UINT32_C(0x48324831), UINT32_C(0xc11c5afe), UINT32_C(0x6d2b79f5)
    };
    StressResult total = {0};
    if (argc == 2 && strcmp(argv[1], "--certify") == 0) {
        transitions = 100000u / 3u;
        soak_frames = UINT64_C(7200000) / 3u;
    }
    head_identity_and_crossing();
    continuity_transitions();
    for (size_t i = 0u; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        StressResult result = run_stress(
            seeds[i], transitions + (i == 0u && argc == 2 ? 1u : 0u),
            soak_frames);
        total.frames += result.frames;
        total.transitions += result.transitions;
        total.finite_checks += result.finite_checks;
        total.resize_commits += result.resize_commits;
        total.checksum += result.checksum;
        printf("seed=%08" PRIx32 " transitions=%" PRIu64
               " frames=%" PRIu64 " checksum=%.9f\n",
               seeds[i], result.transitions, result.frames, result.checksum);
    }
    printf("pathological PASS transitions=%" PRIu64 " frames=%" PRIu64
           " simulated_seconds=%.3f finite_checks=%" PRIu64
           " resize_requests=%" PRIu64 " checksum=%.9f\n",
           total.transitions, total.frames,
           (double)total.frames / (double)STRESS_RATE,
           total.finite_checks, total.resize_commits, total.checksum);
    return 0;
}
