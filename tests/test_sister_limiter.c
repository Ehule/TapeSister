#include "tapesister/sister_limiter.h"

#include <math.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static int close_enough(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static int test_constant_latency_and_bypass(void)
{
    TsSisterLimiter limiter;
    TsStereoFrame output = {0};
    ts_sister_limiter_init(&limiter);
    ts_sister_limiter_set_controls(&limiter, 0, -1.0f, 1.0f, 120.0f);
    CHECK(ts_sister_limiter_reconfigure(&limiter, 48000u));
    CHECK(limiter.delay_frames == 48u);
    for (size_t frame = 0u; frame <= limiter.delay_frames; ++frame) {
        TsStereoFrame input = frame == 0u ?
            (TsStereoFrame){1.25f, -0.5f} : (TsStereoFrame){0.0f, 0.0f};
        output = ts_sister_limiter_process(&limiter, input, NULL, NULL);
        if (frame < limiter.delay_frames)
            CHECK(output.l == 0.0f && output.r == 0.0f);
    }
    CHECK(close_enough(output.l, 1.25f, 0.00001f));
    CHECK(close_enough(output.r, -0.5f, 0.00001f));
    ts_sister_limiter_free(&limiter);
    return 1;
}

static int test_ceiling_and_stereo_link(void)
{
    TsSisterLimiter limiter;
    TsStereoFrame output = {0};
    float reduction = 0.0f;
    float ceiling = powf(10.0f, -1.0f / 20.0f);
    ts_sister_limiter_init(&limiter);
    CHECK(ts_sister_limiter_reconfigure(&limiter, 48000u));
    for (size_t frame = 0u; frame <= limiter.delay_frames; ++frame) {
        TsStereoFrame input = frame == 0u ?
            (TsStereoFrame){2.0f, 0.5f} : (TsStereoFrame){0.0f, 0.0f};
        output = ts_sister_limiter_process(&limiter, input, &reduction, NULL);
        CHECK(fabsf(output.l) <= ceiling + 0.00001f);
        CHECK(fabsf(output.r) <= ceiling + 0.00001f);
    }
    CHECK(close_enough(output.l, ceiling, 0.0001f));
    CHECK(close_enough(output.r / output.l, 0.25f, 0.0001f));
    CHECK(reduction > 6.0f);
    ts_sister_limiter_free(&limiter);
    return 1;
}

static int test_release_and_pathological_input(void)
{
    TsSisterLimiter limiter;
    float previous_reduction = 0.0f;
    float reduction = 0.0f;
    float ceiling = powf(10.0f, -6.0f / 20.0f);
    ts_sister_limiter_init(&limiter);
    ts_sister_limiter_set_controls(&limiter, 1, -6.0f, 0.1f, 10.0f);
    CHECK(ts_sister_limiter_reconfigure(&limiter, 48000u));
    for (int frame = 0; frame < 2000; ++frame) {
        TsStereoFrame input = frame < 12 ?
            (TsStereoFrame){4.0f, -3.0f} :
            (TsStereoFrame){0.1f, -0.1f};
        TsStereoFrame output = ts_sister_limiter_process(
            &limiter, input, &reduction, NULL);
        CHECK(isfinite(output.l) && isfinite(output.r));
        CHECK(fabsf(output.l) <= ceiling + 0.00001f);
        CHECK(fabsf(output.r) <= ceiling + 0.00001f);
        if (frame == 20) previous_reduction = reduction;
    }
    CHECK(previous_reduction > 10.0f);
    CHECK(reduction < previous_reduction);
    CHECK(reduction < 1.0f);
    (void)ts_sister_limiter_process(
        &limiter, (TsStereoFrame){NAN, INFINITY}, &reduction, NULL);
    CHECK(isfinite(reduction));
    ts_sister_limiter_free(&limiter);
    return 1;
}

static int test_live_controls_preserve_audio(void)
{
    TsSisterLimiter l;ts_sister_limiter_init(&l);CHECK(ts_sister_limiter_reconfigure(&l,48000));
    for(int i=0;i<1000;++i)ts_sister_limiter_process(&l,(TsStereoFrame){.2f,-.1f},NULL,NULL);
    TsStereoFrame *storage=l.delay;uint64_t frame=l.processed_frames;
    const float lookahead[]={10,.1f,5,1};
    for(int j=0;j<4;++j) {
        ts_sister_limiter_set_live_controls(&l,1,-6,lookahead[j],20+j*150);
        CHECK(l.delay==storage && l.processed_frames==frame);
        for(int i=0;i<1200;++i) {
            TsStereoFrame out=ts_sister_limiter_process(&l,(TsStereoFrame){.2f,-.1f},NULL,NULL);
            CHECK(close_enough(out.l,.2f,.00001f) && close_enough(out.r,-.1f,.00001f));
        }
        CHECK(!l.delay_ramp && l.live_delay==l.delay_frames);frame=l.processed_frames;
    }
    ts_sister_limiter_free(&l);return 1;
}

int main(void)
{
    if (!test_live_controls_preserve_audio()) return 1;
    if (!test_constant_latency_and_bypass()) return 1;
    if (!test_ceiling_and_stereo_link()) return 1;
    if (!test_release_and_pathological_input()) return 1;
    puts("Sister limiter tests passed");
    return 0;
}
