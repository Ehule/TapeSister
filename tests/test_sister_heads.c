#include "sister_test_helpers.h"

#include <stdio.h>

static void default_contract(void)
{
    TsSisterParameters parameters;
    ts_sister_parameters_default(&parameters, 48000u);
    assert(parameters.head1_level == 0.45f);
    assert(parameters.head1_time_ms == 500.0f);
    assert(parameters.head1_feedback == 0.25f);
    assert(parameters.head2_level == 0.0f && parameters.head2_scrub == 0.5f);
    assert(parameters.head2_rate_index == 7);
    assert(parameters.head3_level == 0.0f && parameters.head3_span == 0.5f);
    assert(parameters.head3_rate_index == 7);
    assert(parameters.wow == 0.0f && parameters.drop == 0.0f);
    assert(!parameters.duck_enabled && !parameters.decorrelation_enabled);
    assert(parameters.width == 1.0f);
    assert(parameters.filter_type == TS_SISTER_FILTER_BYPASS);
    assert(parameters.input_gain == 1.0f);
    assert(parameters.tiles_gain == 1.0f);
    assert(parameters.fm_gain == 1.0f);
    assert(parameters.external_gain == 1.0f);
    assert(parameters.preview_gain == 1.0f);
    assert(parameters.fx_return_gain == 1.0f);
    assert(parameters.mix_output_gain == 1.0f);
    assert(parameters.clear_ms == 20.0f);
    ts_sister_parameters_kafka_start(&parameters, 48000u);
    assert(parameters.filter_type == TS_SISTER_FILTER_LOWPASS);
    assert(sister_close(parameters.filter_cutoff_hz, 9479.2939453125f, 1e-4f));
    assert(sister_close(parameters.filter_gain_db, 2.0f, 1e-6f));
    assert(sister_close(parameters.filter_q, 0.90030003f, 1e-6f));
}

static void set_free_head_age(TsSisterMachine *machine, size_t head,
                              double age)
{
    machine->head[head].logical_age = age;
    machine->head[head].phase = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.storage_frames) - age,
        machine->buffer.storage_frames);
}

static void h1_delay_for_rate(uint32_t sample_rate)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    size_t write_position;
    double expected_position;
    float expected;
    assert(ts_sister_machine_init(&machine, sample_rate, 2u, 0.020));
    ts_sister_parameters_default(&parameters, sample_rate);
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 5.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 1.0f, 0.5f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = machine.buffer.capacity_frames / 2u;
    write_position = (size_t)(machine.master_clock % machine.buffer.capacity_frames);
    expected_position = (double)write_position - (double)sample_rate * 0.005;
    expected = (float)(expected_position / (double)machine.buffer.capacity_frames);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.head[0].l, expected, 2e-4f));
    assert(sister_close(output.head[0].r, expected * 0.5f, 2e-4f));
    ts_sister_machine_free(&machine);
}

static void minimum_delay_and_jump_crossfade(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput first;
    size_t write_position;
    float old_expected;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 0.0f;
    parameters.head2_level = parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    assert(machine.head[0].current_delay_frames == 1.0);
    sister_fill_buffer(&machine, 1.0f, 1.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = 50u;
    parameters.head1_time_ms = 20.0f;
    ts_sister_machine_set_parameters(&machine, &parameters);
    assert(machine.head[0].jump_remaining == 15u);
    write_position = 50u;
    old_expected = (float)(write_position - 1u) /
                   (float)machine.buffer.capacity_frames;
    first = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(first.head[0].l, old_expected, 2e-4f));
    while (machine.head[0].jump_remaining > 0u)
        first = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_frame_finite(first.head[0]));
    ts_sister_machine_free(&machine);
}

static void scrub_span_rates_and_taps(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    double h2_distance;
    double h3_distance;
    size_t i;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 10.0));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 1.0f;
    parameters.head2_rate_index = 7;
    parameters.head3_level = 1.0f;
    parameters.head3_span = 1.0f;
    parameters.head3_rate_index = 7;
    sister_configure_immediate(&machine, &parameters);
    assert(sister_close(machine.head[1].offset.current,
                        (float)(machine.buffer.capacity_frames - 1u), 1e-4f));
    assert(sister_close(machine.head[2].offset.current, 8000.0f, 1e-4f));
    for (i = 0u; i < TS_SISTER_RATE_COUNT; ++i) {
        static const double exact[] = {
            -2.0, -4.0 / 3.0, -1.0, -2.0 / 3.0, -0.5,
             0.5,  2.0 / 3.0,  1.0,  4.0 / 3.0,  2.0
        };
        assert(fabs(ts_sister_rate_value((int)i) - exact[i]) < 1e-12);
    }
    sister_fill_buffer(&machine, 0.8f, 0.3f);
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(output.head[0].l == 0.0f && output.head[0].r == 0.0f);
    assert(sister_frame_finite(output.head[1]) && sister_frame_finite(output.head[2]));
    h2_distance = machine.head[1].logical_age;
    h3_distance = machine.head[2].logical_age;
    for (i = 0u; i < 100u; ++i)
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(fabs(machine.head[1].logical_age - h2_distance) < 1e-6);
    assert(fabs(machine.head[2].logical_age - h3_distance) < 1e-6);

    parameters.head2_rate_index = 0;
    parameters.head3_rate_index = 9;
    sister_configure_immediate(&machine, &parameters);
    set_free_head_age(&machine, 1u, -0.5);
    set_free_head_age(&machine, 2u, 0.5);
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.head[1].logical_age < 3.0);
    assert(machine.head[2].logical_age >
           (double)machine.buffer.capacity_frames - 3.0);
    assert(sister_frame_finite(output.mix));
    ts_sister_machine_free(&machine);
}

static void shared_position_and_write_guard(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    size_t previous;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 0.0f;
    parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 0.5f, 1.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = 50u;
    set_free_head_age(&machine, 1u, 0.0);
    previous = 49u;
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.head[1].l,
                        machine.buffer.data[previous * 2u], 1e-5f));
    assert(sister_close(output.head[1].r,
                        machine.buffer.data[previous * 2u + 1u], 1e-5f));
    assert(sister_close(output.head[1].r, output.head[1].l * 2.0f, 1e-4f));
    machine.master_clock = 60u;
    set_free_head_age(&machine, 1u, -0.5);
    machine.head[1].guard_initialized = 1;
    machine.head[1].previous_guard_difference = -0.5;
    machine.head[1].previous_read = (TsStereoFrame){0.125f, 0.25f};
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.head[1].guard_total == 10u);
    assert(machine.head[1].guard_remaining == 9u);
    assert(sister_close(output.head[1].l, 0.125f, 1e-6f));
    assert(sister_close(output.head[1].r, 0.25f, 1e-6f));
    ts_sister_machine_free(&machine);
}

static double circular_position_delta(double current, double previous,
                                      size_t capacity)
{
    double delta = ts_sister_positive_modulo(
        current - previous + (double)capacity * 0.5, capacity) -
        (double)capacity * 0.5;
    return delta;
}

static void assert_snapshot_matches_audio_read(
    const TsSisterMachine *machine, const TsSisterSnapshot *snapshot,
    size_t head)
{
    double write = (double)(machine->master_clock %
                            machine->buffer.storage_frames);
    double age = ts_sister_positive_modulo(
        write - machine->last_head_position[head],
        machine->buffer.storage_frames);
    double expected = ts_sister_positive_modulo(
        (double)(machine->master_clock % machine->buffer.capacity_frames) - age,
        machine->buffer.capacity_frames);
    assert(fabs(snapshot->head_position[head] - expected) < 1e-6);
    assert(fabs((double)snapshot->head_normalized[head] -
                expected / (double)machine->buffer.capacity_frames) < 2e-7);
}

static void stationary_free_head_case(double seconds, uint8_t channels,
                                      int rate_index, float scrub, float span,
                                      float h2_level, float h3_level)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterSnapshot previous;
    TsSisterSnapshot current;
    double rate = ts_sister_rate_value(rate_index);
    size_t frames;

    assert(ts_sister_machine_init(&machine, 1000u, channels, seconds));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = (float)seconds;
    parameters.head1_level = 0.0f;
    parameters.head2_level = h2_level;
    parameters.head2_scrub = scrub;
    parameters.head2_rate_index = rate_index;
    parameters.head3_level = h3_level;
    parameters.head3_span = span;
    parameters.head3_rate_index = rate_index;
    parameters.wow = 0.0f;
    parameters.drop = 0.0f;
    parameters.decorrelation_enabled = channels == 2u;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 0.5f, 0.25f);
    ts_sister_machine_set_rolling(&machine, 0);
    (void)ts_sister_machine_process_frame(
        &machine, sister_silence(), sister_silence());
    assert(ts_sister_machine_get_snapshot(&machine, &previous));
    frames = (size_t)ceil(2.0 * (double)machine.buffer.capacity_frames /
                          fabs(rate)) + 4u;
    for (size_t frame = 0u; frame < frames; ++frame) {
        (void)ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        assert(ts_sister_machine_get_snapshot(&machine, &current));
        for (size_t head = 1u; head < TS_SISTER_HEAD_COUNT; ++head) {
            double raw_delta = current.head_position[head] -
                               previous.head_position[head];
            double delta = circular_position_delta(
                current.head_position[head], previous.head_position[head],
                machine.buffer.capacity_frames);
            if (fabs(delta - rate) >= 1e-6) {
                double capacity = (double)machine.buffer.capacity_frames;
                double previous_edge = fmin(previous.head_position[head],
                    capacity - previous.head_position[head]);
                double current_edge = fmin(current.head_position[head],
                    capacity - current.head_position[head]);
                double write_distance = fabs(circular_position_delta(
                    current.head_position[head],
                    (double)current.write_position,
                    machine.buffer.capacity_frames));
                /* The established one-frame write guard may briefly alter the
                   exact increment at the true circular edge or write cell,
                   but never by more than its bounded guard distance. */
                assert((previous_edge < 3.0 || current_edge < 3.0 ||
                        write_distance <= 8.0) &&
                       fabs(delta) <= 3.0);
            }
            if (fabs(raw_delta) >
                (double)machine.buffer.capacity_frames * 0.5) {
                if (rate > 0.0) {
                    assert(previous.head_position[head] >
                           (double)machine.buffer.capacity_frames - 3.0);
                    assert(current.head_position[head] < 3.0);
                } else {
                    assert(previous.head_position[head] < 3.0);
                    assert(current.head_position[head] >
                           (double)machine.buffer.capacity_frames - 3.0);
                }
            }
            assert_snapshot_matches_audio_read(&machine, &current, head);
        }
        previous = current;
    }
    ts_sister_machine_free(&machine);
}

static void stationary_free_head_matrix(void)
{
    stationary_free_head_case(5.0, 1u, 4, 0.0f, 0.0f, 0.0f, 1.0f);
    stationary_free_head_case(17.0, 2u, 5, 0.5f, 0.5f, 1.0f, 0.0f);
    stationary_free_head_case(31.0, 2u, 7, 1.0f, 1.0f, 1.0f, 1.0f);
    stationary_free_head_case(46.0, 2u, 0, 0.5f, 0.5f, 0.0f, 0.0f);
    stationary_free_head_case(60.0, 1u, 9, 1.0f, 1.0f, 1.0f, 1.0f);
}

static void live_gestures_and_transport_remain_continuous(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterSnapshot previous;
    TsSisterSnapshot current;

    assert(ts_sister_machine_init(&machine, 1000u, 2u, 46.0));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = 46.0f;
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 0.25f;
    parameters.head2_rate_index = 6;
    parameters.head3_level = 1.0f;
    parameters.head3_span = 0.25f;
    parameters.head3_rate_index = 8;
    parameters.wow = 0.0f;
    parameters.drop = 0.0f;
    parameters.decorrelation_enabled = 1;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 0.5f, 0.25f);
    (void)ts_sister_machine_process_frame(
        &machine, sister_silence(), sister_silence());
    assert(ts_sister_machine_get_snapshot(&machine, &previous));

    parameters.head2_scrub = 0.75f;
    parameters.head3_span = 0.75f;
    ts_sister_machine_set_parameters(&machine, &parameters);
    for (size_t frame = 0u; frame < 80u; ++frame) {
        double old_h2_offset = machine.head[1].previous_offset;
        double old_h3_offset = machine.head[2].previous_offset;
        double expected_h2;
        double expected_h3;
        TsSisterOutput output;
        if (frame == 10u) ts_sister_machine_set_hold(&machine, 1);
        if (frame == 20u) ts_sister_machine_set_rolling(&machine, 0);
        if (frame == 30u) ts_sister_machine_set_hold(&machine, 0);
        if (frame == 40u) ts_sister_machine_set_rolling(&machine, 1);
        output = ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        assert(sister_frame_finite(output.head[1]));
        assert(sister_frame_finite(output.head[2]));
        assert(ts_sister_machine_get_snapshot(&machine, &current));
        expected_h2 = ts_sister_rate_value(parameters.head2_rate_index) -
            (machine.head[1].previous_offset - old_h2_offset);
        expected_h3 = ts_sister_rate_value(parameters.head3_rate_index) -
            (machine.head[2].previous_offset - old_h3_offset);
        assert(fabs(circular_position_delta(
            current.head_position[1], previous.head_position[1],
            machine.buffer.capacity_frames) - expected_h2) < 1e-5);
        assert(fabs(circular_position_delta(
            current.head_position[2], previous.head_position[2],
            machine.buffer.capacity_frames) - expected_h3) < 1e-5);
        assert_snapshot_matches_audio_read(&machine, &current, 1u);
        assert_snapshot_matches_audio_read(&machine, &current, 2u);
        previous = current;
    }
    ts_sister_machine_free(&machine);
}

static void free_head_crosses_physical_store_midpoint_continuously(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterSnapshot before;
    TsSisterSnapshot after;
    const double initial_age = 29999.0;

    assert(ts_sister_machine_init(&machine, 1000u, 2u, 46.0));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = 46.0f;
    parameters.head1_level = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.head2_rate_index = 0;
    parameters.head3_rate_index = 0;
    parameters.wow = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 0.5f, 0.25f);
    ts_sister_machine_set_rolling(&machine, 0);

    for (size_t head = 1u; head < TS_SISTER_HEAD_COUNT; ++head) {
        machine.master_clock = 0u;
        set_free_head_age(&machine, head, initial_age);
        machine.head[head].guard_initialized = 0;
        machine.head[head].guard_remaining = 0u;

        (void)ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        assert(ts_sister_machine_get_snapshot(&machine, &before));
        (void)ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        assert(ts_sister_machine_get_snapshot(&machine, &after));

        assert(fabs(circular_position_delta(
            after.head_position[head], before.head_position[head],
            machine.buffer.capacity_frames) -
            ts_sister_rate_value(0)) < 1e-6);
        assert_snapshot_matches_audio_read(&machine, &after, head);
    }
    ts_sister_machine_free(&machine);
}

static void rate_seams_and_retargeted_h1_are_declicked(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    TsStereoFrame previous = {0.0f, 0.0f};
    float worst_wrap = 0.0f;
    float worst_h1 = 0.0f;

    assert(ts_sister_machine_init(&machine, 1000u, 1u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = 0.100f;
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 0.02f;
    parameters.head2_rate_index = 9;
    parameters.head3_level = 0.0f;
    parameters.wow = parameters.drop = 0.0f;
    parameters.headroom = 1.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 1.0f, 1.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    set_free_head_age(&machine, 1u, 2.0);
    for (size_t frame = 0u; frame < 30u; ++frame) {
        output = ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        if (frame > 0u) {
            float jump = fabsf(output.head[1].l - previous.l);
            if (jump > worst_wrap) worst_wrap = jump;
        }
        previous = output.head[1];
    }
    ts_sister_machine_free(&machine);
    /* The seeded ramp has a 0.98 seam. The 10 ms handoff must turn that
       full-scale edge into a bounded series rather than one audible impulse. */
    assert(worst_wrap < 0.11f);

    assert(ts_sister_machine_init(&machine, 1000u, 1u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = 0.100f;
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 5.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = parameters.head3_level = 0.0f;
    parameters.headroom = 1.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 1.0f, 1.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = 50u;
    previous = ts_sister_machine_process_frame(
        &machine, sister_silence(), sister_silence()).head[0];
    for (size_t frame = 0u; frame < 60u; ++frame) {
        if (frame % 3u == 0u) {
            parameters.head1_time_ms = (frame & 4u) ? 80.0f : 20.0f;
            ts_sister_machine_set_parameters(&machine, &parameters);
        }
        output = ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        {
            float jump = fabsf(output.head[0].l - previous.l);
            if (jump > worst_h1) worst_h1 = jump;
        }
        previous = output.head[0];
    }
    ts_sister_machine_free(&machine);
    /* Retarget every three samples so each new TIME command interrupts the
       previous 15 ms handoff. Continuity must survive that hostile gesture. */
    assert(worst_h1 < 0.25f);
}

static void head_to_head_crossing_is_continuous(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput previous;
    TsSisterSnapshot snapshot;
    float worst_mix_jump = 0.0f;
    double previous_difference;
    int crossed = 0;

    assert(ts_sister_machine_init(&machine, 1000u, 1u, 1.0));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = 1.0f;
    parameters.head1_level = 0.35f;
    parameters.head1_time_ms = 200.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.35f;
    parameters.head2_scrub = 0.22f;
    parameters.head2_rate_index = 9;
    parameters.head2_feedback = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.wow = parameters.drop = 0.0f;
    parameters.headroom = 1.0f;
    sister_configure_immediate(&machine, &parameters);
    for (size_t frame = 0u; frame < machine.buffer.capacity_frames; ++frame) {
        float value = 0.6f * sinf(6.28318530718f *
                                  (float)frame /
                                  (float)machine.buffer.capacity_frames);
        assert(ts_sister_buffer_write(
            &machine.buffer, frame, (TsStereoFrame){value, value}));
    }
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = 500u;
    set_free_head_age(&machine, 1u, 220.0);
    previous = ts_sister_machine_process_frame(
        &machine, sister_silence(), sister_silence());
    assert(ts_sister_machine_get_snapshot(&machine, &snapshot));
    previous_difference = circular_position_delta(
        snapshot.head_position[1], snapshot.head_position[0],
        machine.buffer.capacity_frames);
    for (size_t frame = 0u; frame < 50u; ++frame) {
        TsSisterOutput current = ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
        double difference;
        float jump = fabsf(current.mix.l - previous.mix.l);
        if (jump > worst_mix_jump) worst_mix_jump = jump;
        assert(ts_sister_machine_get_snapshot(&machine, &snapshot));
        difference = circular_position_delta(
            snapshot.head_position[1], snapshot.head_position[0],
            machine.buffer.capacity_frames);
        if (difference * previous_difference <= 0.0) crossed = 1;
        previous_difference = difference;
        previous = current;
    }
    assert(crossed);
    assert(worst_mix_jump < 0.02f);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    default_contract();
    h1_delay_for_rate(44100u);
    h1_delay_for_rate(48000u);
    h1_delay_for_rate(96000u);
    minimum_delay_and_jump_crossfade();
    scrub_span_rates_and_taps();
    shared_position_and_write_guard();
    free_head_crosses_physical_store_midpoint_continuously();
    stationary_free_head_matrix();
    live_gestures_and_transport_remain_continuous();
    rate_seams_and_retargeted_h1_are_declicked();
    head_to_head_crossing_is_continuous();
    puts("sister head tests passed");
    return 0;
}
