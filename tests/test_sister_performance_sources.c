#include "sister_test_helpers.h"
#include "tapesister/audio_mixer.h"

#include <assert.h>
#include <stdio.h>

static float tape_peak(const TsSisterRuntime *runtime)
{
    float peak = 0.0f;
    size_t scalars = runtime->machine.buffer.capacity_frames *
                     runtime->machine.buffer.channels;
    for (size_t i = 0u; i < scalars; ++i)
        if (fabsf(runtime->machine.buffer.data[i]) > peak)
            peak = fabsf(runtime->machine.buffer.data[i]);
    return peak;
}

int main(void)
{
    TsSisterRuntime runtime;
    TsSisterSourceFrames sources = {0};
    TsSisterRuntimeFrame frame;
    TsInstrument empty;
    TsAudioMixer mixer;
    TsAudioBuses buses;
    char error[160];
    ts_instrument_init(&empty);
    assert(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_monitor(&runtime, 0);

    /* Live FM is an independent bus and requires no occupied tile. */
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM);
    sources.fm = (TsStereoFrame){0.25f, -0.125f};
    for (int i = 0; i < 8; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    assert(tape_peak(&runtime) > 0.01f);
    assert(frame.monitor_return.l == 0.0f && frame.monitor_return.r == 0.0f);
    sources.fm = sister_silence();
    frame = ts_sister_runtime_process_frame(&runtime, &sources);
    assert(frame.input.l == 0.0f && frame.input.r == 0.0f);

    assert(ts_sister_machine_clear_offline(&runtime.machine));
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_EXT);
    ts_sister_runtime_input_available(&runtime, 1);
    sources.external = (TsStereoFrame){0.2f, 0.4f};
    for (int i = 0; i < 8; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    assert(tape_peak(&runtime) > 0.01f);
    assert(frame.monitor_return.l == 0.0f && frame.monitor_return.r == 0.0f);

    /* Insert routing removes the selected direct bus and returns it once. */
    ts_audio_mixer_init(&mixer);
    ts_audio_buses_clear(&buses);
    buses.fm = (TsStereoFrame){0.3f, 0.3f};
    buses.sister = (TsStereoFrame){0.2f, 0.2f};
    ts_audio_buses_apply_source_dry(&buses, 0.0f, 0, 0, 1, 0);
    frame.monitor_return = ts_audio_mixer_render(&mixer, &buses);
    assert(sister_close(frame.monitor_return.l, 0.2f, 0.0001f));

    /* A wholly empty bank cannot accidentally become a tile prerequisite. */
    assert(ts_sister_runtime_set_page(&runtime, 0u, &empty));
    assert(ts_sister_runtime_source_mask(&runtime) == 0u);
    assert(!ts_sister_runtime_arm_capture(
        &runtime, &empty, 0, 8u, 1000u, 1u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&empty);
    puts("sister performance-source tests passed");
    return 0;
}
