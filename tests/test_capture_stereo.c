#include "tapesister/capture.h"
#include "tapesister/sample.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) (fabsf((a) - (b)) < 0.0001f)

static int project(TsInstrument *instrument, char *error, size_t error_size)
{
    ts_instrument_init(instrument);
    if (!ts_instrument_activate_silence(instrument, 4u, 48000u,
                                        error, error_size)) return 0;
    for (size_t frame = 0; frame < 4u; ++frame)
        instrument->current.data[frame] = 0.1f;
    if (!ts_instrument_select_bank(instrument, 1, error, error_size) ||
        !ts_instrument_activate_silence(instrument, 4u, 48000u,
                                       error, error_size) ||
        !ts_instrument_select_bank(instrument, 0, error, error_size)) return 0;
    return 1;
}

int main(void)
{
    TsCaptureRecorder recorder;
    TsInstrument instrument;
    char error[160];
    const float stereo[] = {0.2f, -0.4f, 0.4f, -0.8f,
                            0.6f, -0.2f, 0.8f, 0.1f};

    ts_capture_init(&recorder);
    CHECK(ts_capture_arm_channels(&recorder, 1, 2u, 48000u, 1u,
                                  error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(!ts_capture_write_frame(&recorder, (TsStereoFrame){0.8f, 0.2f}));
    CHECK(CLOSE(recorder.buffer[0], 0.5f));
    ts_capture_free(&recorder);

    ts_capture_init(&recorder);
    CHECK(ts_capture_arm_channels(&recorder, 1, 2u, 48000u, 2u,
                                  error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(!ts_capture_write_frame(&recorder, (TsStereoFrame){0.8f, 0.2f}));
    CHECK(CLOSE(recorder.buffer[0], 0.8f) && CLOSE(recorder.buffer[1], 0.2f));
    ts_capture_free(&recorder);

    CHECK(project(&instrument, error, sizeof(error)));
    instrument.bank[0].tuning.root_note = 48;
    instrument.bank[0].audible_tuning.root_note = 50;
    CHECK(ts_instrument_commit_capture_channels(
              &instrument, 1, 0, stereo, 4u, 48000u, 2u, 0, 0,
              error, sizeof(error)));
    CHECK(instrument.current.channels == 2u);
    CHECK(instrument.tuning.root_note == 48);
    CHECK(instrument.audible_tuning.root_note == 50);
    CHECK(CLOSE(instrument.current.data[0], 0.2f));
    CHECK(CLOSE(instrument.current.data[1], -0.4f));
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.channels == 1u);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.channels == 2u);

    {
        float *base = malloc(8u * sizeof(float));
        float mono_layer[] = {0.1f, 0.1f, 0.1f, 0.1f};
        CHECK(base != NULL);
        memcpy(base, instrument.current.data, 8u * sizeof(float));
        CHECK(ts_instrument_commit_overdub_channels(
                  &instrument, 1, 0, base, 4u, 48000u, 2u,
                  mono_layer, 4u, 48000u, 1u, 0,
                  error, sizeof(error)));
        CHECK(instrument.current.channels == 2u);
        CHECK(CLOSE(instrument.current.data[0] - instrument.current.data[1],
                    0.6f));
        CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
        CHECK(!ts_instrument_commit_overdub_channels(
                   &instrument, 1, 0, base, 4u, 48000u, 1u,
                   mono_layer, 4u, 48000u, 1u, 0,
                   error, sizeof(error)));
        CHECK(strstr(error, "changed while armed") != NULL);
        free(base);
    }
    ts_instrument_free(&instrument);

    CHECK(project(&instrument, error, sizeof(error)));
    instrument.bank[1].locked = 1;
    CHECK(!ts_instrument_commit_capture_channels(
               &instrument, 1, 0, stereo, 4u, 48000u, 2u, 0, 0,
               error, sizeof(error)));
    ts_instrument_free(&instrument);

    if (failures) return 1;
    puts("capture stereo tests passed");
    return 0;
}
