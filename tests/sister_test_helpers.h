#ifndef TAPESISTER_SISTER_TEST_HELPERS_H
#define TAPESISTER_SISTER_TEST_HELPERS_H

#include "tapesister/sister_machine.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

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

#endif
