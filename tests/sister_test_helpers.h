#ifndef TAPESISTER_SISTER_TEST_HELPERS_H
#define TAPESISTER_SISTER_TEST_HELPERS_H

#include "tapesister/sister_runtime.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static inline TsStereoFrame sister_silence(void)
{
    TsStereoFrame frame = {0.0f, 0.0f};
    return frame;
}

static inline int sister_close(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

static inline int sister_frame_finite(TsStereoFrame frame)
{
    return isfinite(frame.l) && isfinite(frame.r);
}

static inline float sister_peak(TsStereoFrame frame)
{
    float left = fabsf(frame.l);
    float right = fabsf(frame.r);
    return left > right ? left : right;
}

static inline void sister_configure_immediate(
    TsSisterMachine *machine, const TsSisterParameters *parameters)
{
    ts_sister_machine_set_parameters(machine, parameters);
    ts_sister_machine_reset(machine);
}

static inline void sister_fill_buffer(TsSisterMachine *machine, float left_scale,
                                      float right_scale)
{
    size_t i;
    assert(machine != NULL && machine->buffer.capacity_frames > 0u);
    for (i = 0u; i < machine->buffer.capacity_frames; ++i) {
        float value = (float)i / (float)machine->buffer.capacity_frames;
        TsStereoFrame frame = {value * left_scale, value * right_scale};
        assert(ts_sister_buffer_write(&machine->buffer, i, frame));
    }
}

static inline int sister_test_make_tiles(TsInstrument *instrument,
                                         int source_count,
                                         int blank_count,
                                         uint32_t sample_rate,
                                         size_t frames)
{
    char error[160];
    int total = source_count + blank_count;
    if (instrument == NULL || source_count < 0 || blank_count < 0 ||
        total < 1 || total > TS_BANK_SLOT_COUNT) return 0;
    ts_instrument_init(instrument);
    for (int slot = 0; slot < total; ++slot) {
        if (slot > 0 && !ts_instrument_select_bank(
                instrument, slot, error, sizeof(error))) return 0;
        if (!ts_instrument_activate_silence(instrument, frames, sample_rate,
                                            error, sizeof(error))) return 0;
        if (slot < source_count) {
            for (size_t frame = 0u; frame < instrument->current.frames; ++frame)
                instrument->current.data[frame] =
                    (float)(slot + 1) * (frame & 1u ? -0.2f : 0.2f);
        }
    }
    return ts_instrument_select_bank(instrument, 0, error, sizeof(error));
}

static inline int sister_test_enable(TsSisterRuntime *runtime,
                                     uint32_t sample_rate,
                                     uint8_t buffer_channels,
                                     double seconds)
{
    char error[160];
    ts_sister_runtime_init(runtime);
    return ts_sister_runtime_enable(runtime, sample_rate, 2u,
                                    buffer_channels, seconds,
                                    error, sizeof(error));
}

#endif
