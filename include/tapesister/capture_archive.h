#ifndef TAPESISTER_CAPTURE_ARCHIVE_H
#define TAPESISTER_CAPTURE_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TS_CAPTURE_ARCHIVE_INPUT = 0,
    TS_CAPTURE_ARCHIVE_INTERNAL,
    TS_CAPTURE_ARCHIVE_SYNTH
} TsCaptureArchiveKind;

int ts_capture_archive_unique_path(const char *directory, const char *prefix,
                                   char *written_path,
                                   size_t written_path_size,
                                   char *error, size_t error_size);

int ts_capture_archive_write(const char *directory,
                             TsCaptureArchiveKind kind,
                             const float *samples, size_t frames,
                             uint32_t sample_rate,
                             char *written_path, size_t written_path_size,
                             char *error, size_t error_size);
int ts_capture_archive_write_channels(
    const char *directory, TsCaptureArchiveKind kind,
    const float *samples, size_t frames, uint32_t sample_rate,
    uint8_t channels, char *written_path, size_t written_path_size,
    char *error, size_t error_size);

#endif
