#include "sister_test_helpers.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) sister_close((a),(b),0.0002f)

/* Power only owns the rolling machine, not the already playing global FX. */
static void test_global_fx_survive_power(void)
{
    TsSisterRuntime runtime;char error[160];ts_sister_runtime_init(&runtime);
    CHECK(ts_sister_runtime_reconfigure(&runtime,1000u,2u,error,sizeof(error)));
    TsSisterParameters p=runtime.parameters;
    p.fx.transition=ts_sister_fx_transition_normalized(1000);
    p.fx.master_transition=ts_sister_fx_transition_normalized(1000);
    p.fx.slot[0].type=TS_SISTER_FX_DELAY;p.fx.slot[0].mix=.7f;p.fx.slot[0].parameter_a=.17f;
    p.fx.slot[1].enabled=0;p.fx.slot[2].parameter_a=.83f;
    p.fx.fallout.enabled=1;p.fx.fallout.mix=1;p.fx.fallout.noise=.4f;
    p.fx.fallout.master_transition=ts_sister_fallout_transition_normalized(1000);
    ts_sister_runtime_set_parameters(&runtime,&p);
    double difference=0;
    for(int i=0;i<32;++i) {
        TsStereoFrame f=ts_sister_runtime_process_ordinary_post_fx(&runtime,(TsStereoFrame){.2f,-.1f});
        difference+=fabsf(f.l-.2f)+fabsf(f.r+.1f);
    }
    CHECK(difference>0);CHECK(!runtime.enabled && runtime.fallout.write_clock>0);
    CHECK(runtime.post_fx.slot[0].has_pending && runtime.post_fx.slot[1].engage.remaining>0);
    TsSisterFxControls controls=runtime.parameters.fx;
    TsSisterPostFxEngine *fx=malloc(sizeof(*fx));CHECK(fx!=NULL);if(!fx){ts_sister_runtime_free(&runtime);return;}
    *fx=runtime.post_fx;TsSisterFalloutEngine fallout=runtime.fallout;
    runtime.fallout.buffer[0]=.12345f;runtime.post_fx.delay[0][4].data[0]=.4321f;
    CHECK(ts_sister_runtime_enable(&runtime,1000u,2u,2u,.1,error,sizeof(error)));
    CHECK(memcmp(&controls,&runtime.parameters.fx,sizeof(controls))==0);
    CHECK(memcmp(fx,&runtime.post_fx,sizeof(*fx))==0);
    CHECK(memcmp(&fallout,&runtime.fallout,sizeof(fallout))==0);
    CHECK(runtime.fallout.buffer[0]==.12345f && runtime.post_fx.delay[0][4].data[0]==.4321f);
    ts_sister_runtime_disable(&runtime);
    CHECK(memcmp(fx,&runtime.post_fx,sizeof(*fx))==0);
    CHECK(memcmp(&fallout,&runtime.fallout,sizeof(fallout))==0);
    CHECK(runtime.fallout.buffer[0]==.12345f);
    uint64_t clock=runtime.fallout.write_clock;
    ts_sister_runtime_process_ordinary_post_fx(&runtime,(TsStereoFrame){.2f,-.1f});
    CHECK(runtime.fallout.write_clock==clock+1);
    free(fx);ts_sister_runtime_free(&runtime);
}

int main(void)
{
    test_global_fx_survive_power();
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsSisterSourceFrames source = {0};
    TsSisterRuntimeFrame frame;
    TsSisterRoutingSnapshot snapshot;
    TsNoteEvent note;
    TsSisterParameters parameters;
    char error[160];

    CHECK(sister_test_make_tiles(&instrument, 1, 1, 1000u, 32u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_sources(
        &runtime, TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_EXT);
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    ts_sister_runtime_set_rolling(&runtime, 0);
    ts_sister_runtime_set_hold(&runtime, 1);
    parameters = runtime.parameters;
    parameters.wow = 3.0f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    runtime.machine.buffer.data[0] = 0.75f;
    CHECK(ts_sister_runtime_reconfigure(&runtime, 2000u, 2u,
                                        error, sizeof(error)));
    CHECK(runtime.enabled && runtime.machine.buffer.sample_rate == 2000u);
    CHECK(CLOSE(runtime.machine.buffer.data[0], 0.0f));
    CHECK(!runtime.rolling && runtime.held);
    CHECK(CLOSE(runtime.parameters.wow, 3.0f));
    CHECK(runtime.source_switches ==
          (TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_EXT));

    ts_sister_runtime_input_available(&runtime, 0);
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK((snapshot.warnings & TS_SISTER_WARNING_INPUT_UNAVAILABLE) != 0u);
    CHECK(ts_note_event_qwerty(&note, 0, TS_KEYBOARD_BASE_NOTE));
    ts_sister_runtime_set_rolling(&runtime, 1);
    ts_sister_runtime_set_hold(&runtime, 0);
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &note, 0,
                                    2000) == 1);
    frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(sister_frame_finite(frame.input));

    runtime.machine.buffer.data[0] = 0.75f;
    CHECK(ts_sister_runtime_request_clear(&runtime));
    for (int attempt = 0;
         attempt < 200 && !ts_sister_runtime_can_clear(&runtime); ++attempt)
        (void)ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(ts_sister_runtime_can_clear(&runtime));
    CHECK(ts_sister_runtime_perform_clear(&runtime));
    CHECK(CLOSE(runtime.machine.buffer.data[0], 0.0f));

    CHECK(ts_sister_runtime_set_page(&runtime, 1u, &instrument));
    CHECK(ts_performance_count(&runtime.performance) == 0);
    CHECK(ts_sister_runtime_set_page(&runtime, 0u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x1u);

    CHECK(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 1, 8u, 2000u, 1u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    ts_sister_runtime_project_close(&runtime);
    CHECK(runtime.capture.state == TS_CAPTURE_IDLE);
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0u);
    CHECK(ts_performance_count(&runtime.performance) == 0);

    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    source.preview = (TsStereoFrame){0.5f, -0.5f};
    ts_sister_runtime_fail_silent(&runtime, TS_SISTER_WARNING_CALLBACK);
    frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(CLOSE(frame.tap[TS_SISTER_TAP_MIX].l, 0.0f));
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK((snapshot.warnings & TS_SISTER_WARNING_CALLBACK) != 0u);

    CHECK(!ts_sister_runtime_reconfigure(&runtime, 48000u, 1u,
                                         error, sizeof(error)));
    CHECK(!runtime.enabled && runtime.machine.buffer.data == NULL);
    CHECK(!ts_sister_runtime_enable(
        &runtime, 48000u, 2u, 2u, TS_SISTER_MAX_SECONDS + 1.0,
        error, sizeof(error)));
    CHECK(!runtime.enabled);

    ts_sister_runtime_free(&runtime);
    CHECK(runtime.machine.buffer.data == NULL);
    ts_instrument_free(&instrument);
    if (failures) return 1;
    puts("sister lifecycle tests passed");
    return 0;
}
