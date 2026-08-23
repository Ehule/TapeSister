#include "sister_test_helpers.h"

#include <stdint.h>
#include <stdio.h>

static void allocation_and_dimensions(void)
{
    static const uint32_t rates[] = {44100u, 48000u, 96000u};
    size_t i;
    for (i = 0u; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        TsSisterBuffer mono;
        TsSisterBuffer stereo;
        size_t expected = (size_t)ceil((double)rates[i] * 0.001);
        assert(ts_sister_buffer_init(&mono, rates[i], 1u, 0.001));
        assert(ts_sister_buffer_init(&stereo, rates[i], 2u, 0.001));
        assert(mono.capacity_frames == expected && stereo.capacity_frames == expected);
        assert(mono.channels == 1u && stereo.channels == 2u);
        ts_sister_buffer_free(&mono);
        ts_sister_buffer_free(&stereo);
    }
    {
        TsSisterBuffer invalid;
        assert(!ts_sister_buffer_init(&invalid, 0u, 2u, 1.0));
        assert(!ts_sister_buffer_init(&invalid, 48000u, 0u, 1.0));
        assert(!ts_sister_buffer_init(&invalid, 48000u, 3u, 1.0));
        assert(!ts_sister_buffer_init(&invalid, 48000u, 2u, 0.0));
        assert(!ts_sister_buffer_init(&invalid, 48000u, 2u,
                                      TS_SISTER_MAX_SECONDS + 1.0));
    }
}

static void interpolation_and_channels(void)
{
    TsSisterBuffer stereo;
    TsSisterBuffer mono;
    TsStereoFrame frame;
    assert(ts_sister_buffer_init(&stereo, 1000u, 2u, 0.004));
    assert(stereo.capacity_frames == 4u);
    assert(ts_sister_buffer_write(&stereo, 0u, (TsStereoFrame){0.0f, 1.0f}));
    assert(ts_sister_buffer_write(&stereo, 1u, (TsStereoFrame){0.5f, 0.5f}));
    assert(ts_sister_buffer_write(&stereo, 3u, (TsStereoFrame){1.0f, -1.0f}));
    frame = ts_sister_buffer_read(&stereo, 0.5);
    assert(sister_close(frame.l, 0.25f, 1e-6f));
    assert(sister_close(frame.r, 0.75f, 1e-6f));
    frame = ts_sister_buffer_read(&stereo, 3.5);
    assert(sister_close(frame.l, 0.5f, 1e-6f));
    assert(sister_close(frame.r, 0.0f, 1e-6f));
    frame = ts_sister_buffer_read(&stereo, -0.5);
    assert(sister_close(frame.l, 0.5f, 1e-6f));
    assert(sister_close(frame.r, 0.0f, 1e-6f));
    ts_sister_buffer_free(&stereo);

    assert(ts_sister_buffer_init(&mono, 1000u, 1u, 0.004));
    assert(ts_sister_buffer_write(&mono, 1u, (TsStereoFrame){0.2f, 0.8f}));
    frame = ts_sister_buffer_read(&mono, 1.0);
    assert(frame.l == 0.5f && frame.r == 0.5f);
    ts_sister_buffer_free(&mono);
}

static void read_before_write_and_long_clock(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    TsStereoFrame silence = sister_silence();
    size_t last;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.008));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    last = machine.buffer.capacity_frames - 1u;
    assert(ts_sister_buffer_write(&machine.buffer, last,
                                  (TsStereoFrame){0.25f, 0.75f}));
    output = ts_sister_machine_process_frame(&machine,
                                             (TsStereoFrame){-0.5f, 0.5f}, silence);
    assert(sister_close(output.head[0].l, 0.25f, 1e-5f));
    assert(sister_close(output.head[0].r, 0.75f, 1e-5f));
    assert(machine.buffer.data[0] != 0.0f || machine.buffer.data[1] != 0.0f);

    machine.master_clock = (uint64_t)UINT32_MAX + 1234u;
    ts_sister_machine_process_block(&machine, NULL, NULL, NULL, 2000000u);
    assert(machine.master_clock == (uint64_t)UINT32_MAX + 1234u + 2000000u);
    output = machine.last_output;
    assert(sister_frame_finite(output.mix));
    assert(ts_sister_positive_modulo(-1.0, 8u) == 7.0);
    assert(ts_sister_positive_modulo(17.5, 8u) == 1.5);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    allocation_and_dimensions();
    interpolation_and_channels();
    read_before_write_and_long_clock();
    puts("sister buffer tests passed");
    return 0;
}
