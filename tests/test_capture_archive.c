#include "tapesister/capture_archive.h"
#include "tapesister/sample.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define TS_RMDIR(path) _rmdir(path)
#else
#include <unistd.h>
#define TS_RMDIR(path) rmdir(path)
#endif

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

int main(void)
{
    const char *directory = "test-Captures";
    float performance[] = {0.1f, -0.25f, 0.75f, 0.95f};
    char first[1200] = {0};
    char second[1200] = {0};
    char same_second[1200] = {0};
    char synth[1200] = {0};
    char error[160];
    TsSample loaded;
    ts_sample_init(&loaded);
    CHECK(ts_capture_archive_write(directory, TS_CAPTURE_ARCHIVE_INPUT,
                                   performance, 4u, 48000,
                                   first, sizeof(first), error, sizeof(error)));
    CHECK(strstr(first, "INPUT_") != NULL);
    performance[0] = -0.9f;
    CHECK(ts_sample_load_wav(&loaded, first, error, sizeof(error)));
    CHECK(fabsf(loaded.data[0] - 0.1f) < 0.00001f);
    CHECK(loaded.data[3] > 0.94f);
    CHECK(ts_capture_archive_write(directory, TS_CAPTURE_ARCHIVE_INTERNAL,
                                   performance, 4u, 48000,
                                   second, sizeof(second), error, sizeof(error)));
    CHECK(strstr(second, "CAPTURE_") != NULL);
    CHECK(strcmp(first, second) != 0);
    CHECK(ts_capture_archive_write(directory, TS_CAPTURE_ARCHIVE_INPUT,
                                   performance, 4u, 48000,
                                   same_second, sizeof(same_second),
                                   error, sizeof(error)));
    CHECK(strstr(same_second, "INPUT_") != NULL);
    CHECK(strcmp(first, same_second) != 0);
    CHECK(ts_capture_archive_write(directory, TS_CAPTURE_ARCHIVE_SYNTH,
                                   performance, 4u, 48000,
                                   synth, sizeof(synth), error, sizeof(error)));
    CHECK(strstr(synth, "SYNTH_") != NULL);
    /* Clearing or replacing working audio has no relationship to the archive. */
    memset(performance, 0, sizeof(performance));
    ts_sample_free(&loaded);
    ts_sample_init(&loaded);
    CHECK(ts_sample_load_wav(&loaded, first, error, sizeof(error)));
    CHECK(fabsf(loaded.data[0] - 0.1f) < 0.00001f);
    ts_sample_free(&loaded);
    if (first[0] != '\0') CHECK(remove(first) == 0);
    if (second[0] != '\0') CHECK(remove(second) == 0);
    if (same_second[0] != '\0') CHECK(remove(same_second) == 0);
    if (synth[0] != '\0') CHECK(remove(synth) == 0);
    CHECK(TS_RMDIR(directory) == 0);
    if (failures != 0) {
        fprintf(stderr, "%d capture archive test(s) failed\n", failures);
        return 1;
    }
    puts("immutable capture archive and collision-safe naming tests passed");
    return 0;
}
