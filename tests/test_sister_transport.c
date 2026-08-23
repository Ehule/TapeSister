#include "sister_test_helpers.h"

#include <stdio.h>

static void rolling_hold_resume(void)
{
    TsSisterMachine machine;
    TsStereoFrame input = {0.4f, -0.2f};
    TsStereoFrame held_value;
    uint64_t before_clock;
    size_t held_position;
    size_t resumed_position;
    TsSisterOutput held_output;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.010));
    for (resumed_position = 0u;
         resumed_position < machine.buffer.capacity_frames; ++resumed_position)
        assert(ts_sister_buffer_write(&machine.buffer, resumed_position,
                                      (TsStereoFrame){0.2f, -0.1f}));
    ts_sister_machine_process_frame(&machine, input, sister_silence());
    assert(machine.buffer.data[0] != 0.0f);
    ts_sister_machine_set_hold(&machine, 1);
    held_position = (size_t)(machine.master_clock % machine.buffer.capacity_frames);
    held_value.l = machine.buffer.data[held_position * 2u];
    held_value.r = machine.buffer.data[held_position * 2u + 1u];
    before_clock = machine.master_clock;
    held_output = ts_sister_machine_process_frame(
        &machine, (TsStereoFrame){0.9f, 0.7f}, sister_silence());
    assert(machine.master_clock == before_clock + 1u);
    assert(sister_peak(held_output.head[0]) > 0.0f);
    assert(machine.buffer.data[held_position * 2u] == held_value.l);
    assert(machine.buffer.data[held_position * 2u + 1u] == held_value.r);
    ts_sister_machine_set_hold(&machine, 0);
    resumed_position = (size_t)(machine.master_clock % machine.buffer.capacity_frames);
    ts_sister_machine_process_frame(&machine, input, sister_silence());
    assert(resumed_position != held_position);
    assert(machine.buffer.data[resumed_position * 2u] != 0.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    resumed_position = (size_t)(machine.master_clock % machine.buffer.capacity_frames);
    held_value.l = machine.buffer.data[resumed_position * 2u];
    ts_sister_machine_process_frame(&machine, input, sister_silence());
    assert(machine.buffer.data[resumed_position * 2u] == held_value.l);
    ts_sister_machine_free(&machine);
}

static void clear_handshake(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    size_t i;
    int saw_fade = 0;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.020));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.clear_ms = 10.0f;
    sister_configure_immediate(&machine, &parameters);
    for (i = 0u; i < machine.buffer.capacity_frames; ++i)
        assert(ts_sister_buffer_write(&machine.buffer, i, (TsStereoFrame){0.5f, -0.5f}));
    ts_sister_machine_set_rolling(&machine, 0);
    assert(ts_sister_machine_request_clear(&machine));
    assert(!ts_sister_machine_request_clear(&machine));
    assert(machine.clear_state == TS_SISTER_CLEAR_FADE_OUT);
    for (i = 0u; i < 20u && !ts_sister_machine_can_clear(&machine); ++i) {
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
        if (sister_peak(output.mix) < 0.2f) saw_fade = 1;
    }
    assert(saw_fade && ts_sister_machine_can_clear(&machine));
    assert(machine.buffer.data[0] != 0.0f);
    assert(ts_sister_machine_perform_clear(&machine));
    for (i = 0u; i < machine.buffer.capacity_frames * 2u; ++i)
        assert(machine.buffer.data[i] == 0.0f);
    assert(machine.clear_state == TS_SISTER_CLEAR_FADE_IN);
    for (i = 0u; i < 20u && machine.clear_state != TS_SISTER_CLEAR_IDLE; ++i)
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.clear_state == TS_SISTER_CLEAR_IDLE);
    assert(machine.clear_gain.current == 1.0f);
    assert(ts_sister_machine_clear_offline(&machine));
    ts_sister_machine_free(&machine);
}

static void reset_and_reconfigure(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    float *original_data;
    size_t original_capacity;
    assert(ts_sister_machine_init(&machine, 44100u, 1u, 0.010));
    ts_sister_parameters_default(&parameters, 44100u);
    parameters.head2_level = 0.7f;
    sister_configure_immediate(&machine, &parameters);
    ts_sister_machine_process_frame(&machine, (TsStereoFrame){0.5f, 0.5f},
                                    sister_silence());
    assert(machine.master_clock == 1u);
    ts_sister_machine_reset(&machine);
    assert(machine.master_clock == 0u && machine.rolling && !machine.held);
    original_data = machine.buffer.data;
    original_capacity = machine.buffer.capacity_frames;
    assert(!ts_sister_machine_reconfigure(&machine, 48000u, 3u, 1.0));
    assert(machine.buffer.data == original_data);
    assert(machine.buffer.capacity_frames == original_capacity);
    assert(!ts_sister_machine_reconfigure(&machine, 48000u, 2u,
                                          TS_SISTER_MAX_SECONDS + 1.0));
    assert(machine.buffer.data == original_data);
    ts_sister_machine_set_rolling(&machine, 0);
    ts_sister_machine_set_hold(&machine, 1);
    assert(ts_sister_machine_reconfigure(&machine, 48000u, 2u, 0.020));
    assert(machine.buffer.sample_rate == 48000u && machine.buffer.channels == 2u);
    assert(machine.buffer.capacity_frames == 960u);
    assert(!machine.rolling && machine.held);
    assert(machine.parameters.head2_level == 0.7f);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    rolling_hold_resume();
    clear_handshake();
    reset_and_reconfigure();
    puts("sister transport tests passed");
    return 0;
}
