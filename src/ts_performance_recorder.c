#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#define _FILE_OFFSET_BITS 64
#endif

#include "tapesister/performance_recorder.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/types.h>
#endif

enum {
    TS_PERFORMANCE_WAV_HEADER_BYTES = 80,
    TS_PERFORMANCE_WRITER_BATCH_FRAMES = 1024
};

static void recorder_error(char *error, size_t error_size,
                           const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static void put_u16(unsigned char *out, uint16_t value)
{
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8u);
}

static void put_u32(unsigned char *out, uint32_t value)
{
    for (unsigned byte = 0u; byte < 4u; ++byte)
        out[byte] = (unsigned char)(value >> (byte * 8u));
}

static void put_u64(unsigned char *out, uint64_t value)
{
    for (unsigned byte = 0u; byte < 8u; ++byte)
        out[byte] = (unsigned char)(value >> (byte * 8u));
}

static void put_f32(unsigned char *out, float value)
{
    uint32_t bits;
    if (!isfinite(value)) value = 0.0f;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(out, bits);
}

static int seek_file(FILE *file, uint64_t offset)
{
#ifdef _WIN32
    return file != NULL && offset <= (uint64_t)INT64_MAX &&
           _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
#else
    return file != NULL && offset <= (uint64_t)INT64_MAX &&
           fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}

int ts_performance_recorder_uses_rf64(uint64_t frames, uint8_t channels)
{
    uint64_t bytes_per_frame = channels == 1u ? 4u : 8u;
    return channels != 1u && channels != 2u ? 0 :
           frames > (UINT32_MAX - 72u) / bytes_per_frame;
}

static int write_header(FILE *file, uint64_t frames, uint32_t sample_rate,
                        uint8_t channels, int final)
{
    unsigned char header[TS_PERFORMANCE_WAV_HEADER_BYTES] = {0};
    uint64_t bytes_per_frame = (uint64_t)channels * 4u;
    uint64_t data_bytes = frames * bytes_per_frame;
    uint64_t riff_size = 72u + data_bytes;
    int rf64 = ts_performance_recorder_uses_rf64(frames, channels);
    if (file == NULL || sample_rate == 0u ||
        sample_rate > UINT32_MAX / (uint32_t)bytes_per_frame ||
        frames > (UINT64_MAX - 72u) / bytes_per_frame ||
        (channels != 1u && channels != 2u)) return 0;
    memcpy(header, rf64 ? "RF64" : "RIFF", 4u);
    put_u32(header + 4u, rf64 ? UINT32_MAX : (uint32_t)riff_size);
    memcpy(header + 8u, "WAVE", 4u);
    memcpy(header + 12u, rf64 ? "ds64" : "JUNK", 4u);
    put_u32(header + 16u, 28u);
    if (rf64) {
        put_u64(header + 20u, riff_size);
        put_u64(header + 28u, data_bytes);
        put_u64(header + 36u, frames);
        put_u32(header + 44u, 0u);
    }
    memcpy(header + 48u, "fmt ", 4u);
    put_u32(header + 52u, 16u);
    put_u16(header + 56u, 3u);
    put_u16(header + 58u, channels);
    put_u32(header + 60u, sample_rate);
    put_u32(header + 64u, sample_rate * (uint32_t)bytes_per_frame);
    put_u16(header + 68u, (uint16_t)bytes_per_frame);
    put_u16(header + 70u, 32u);
    memcpy(header + 72u, "data", 4u);
    put_u32(header + 76u, rf64 ? UINT32_MAX : (uint32_t)data_bytes);
    if (!seek_file(file, 0u) ||
        fwrite(header, 1u, sizeof(header), file) != sizeof(header)) return 0;
    if (!final && !seek_file(file, TS_PERFORMANCE_WAV_HEADER_BYTES + data_bytes))
        return 0;
    return 1;
}

static void fail_recorder(TsPerformanceRecorder *recorder, const char *message)
{
    uint64_t written;
    if (recorder == NULL) return;
    snprintf(recorder->error, sizeof(recorder->error), "%s",
             message != NULL ? message : "Performance file write failed");
    atomic_store_explicit(&recorder->state, TS_PERFORMANCE_FILE_FAILED,
                          memory_order_release);
    written = atomic_load_explicit(&recorder->written_frames,
                                   memory_order_acquire);
    if (recorder->file != NULL) {
        /* Best effort: even a full disk will often still permit rewriting the
           already allocated header, leaving all complete prior frames usable. */
        (void)write_header(recorder->file, written, recorder->sample_rate,
                           recorder->channels, 1);
        (void)fflush(recorder->file);
        fclose(recorder->file);
        recorder->file = NULL;
    }
}

void ts_performance_recorder_init(TsPerformanceRecorder *recorder)
{
    if (recorder == NULL) return;
    memset(recorder, 0, sizeof(*recorder));
    atomic_init(&recorder->write_cursor, 0u);
    atomic_init(&recorder->read_cursor, 0u);
    atomic_init(&recorder->accepted_frames, 0u);
    atomic_init(&recorder->written_frames, 0u);
    atomic_init(&recorder->dropped_frames, 0u);
    atomic_init(&recorder->state, TS_PERFORMANCE_FILE_IDLE);
}

void ts_performance_recorder_free(TsPerformanceRecorder *recorder)
{
    if (recorder == NULL) return;
    if (recorder->file != NULL) {
        fclose(recorder->file);
        recorder->file = NULL;
    }
    free(recorder->ring);
    recorder->ring = NULL;
    recorder->ring_capacity_frames = 0u;
    recorder->sample_rate = 0u;
    recorder->channels = 0u;
    atomic_store_explicit(&recorder->state, TS_PERFORMANCE_FILE_IDLE,
                          memory_order_release);
}

int ts_performance_recorder_start(TsPerformanceRecorder *recorder,
                                  const char *path, uint32_t sample_rate,
                                  uint8_t channels, size_t queue_frames,
                                  char *error, size_t error_size)
{
    TsStereoFrame *ring;
    FILE *file;
    if (recorder == NULL || path == NULL || path[0] == '\0' ||
        strlen(path) >= sizeof(recorder->path) ||
        sample_rate == 0u || (channels != 1u && channels != 2u) ||
        sample_rate > UINT32_MAX / ((uint32_t)channels * 4u) ||
        queue_frames < 2u) {
        recorder_error(error, error_size,
                       "Performance recorder configuration is invalid");
        return 0;
    }
    if (!atomic_is_lock_free(&recorder->write_cursor) ||
        !atomic_is_lock_free(&recorder->read_cursor) ||
        !atomic_is_lock_free(&recorder->accepted_frames) ||
        !atomic_is_lock_free(&recorder->dropped_frames) ||
        !atomic_is_lock_free(&recorder->state)) {
        recorder_error(error, error_size,
                       "This build cannot record files without audio-thread locks");
        return 0;
    }
    if (ts_performance_recorder_state(recorder) != TS_PERFORMANCE_FILE_IDLE) {
        recorder_error(error, error_size,
                       "A performance recording is already active");
        return 0;
    }
    if (queue_frames > SIZE_MAX / sizeof(*ring)) {
        recorder_error(error, error_size,
                       "Performance recorder queue is too large");
        return 0;
    }
    ring = (TsStereoFrame *)calloc(queue_frames, sizeof(*ring));
    if (ring == NULL) {
        recorder_error(error, error_size,
                       "Could not allocate the performance recording queue");
        return 0;
    }
    file = fopen(path, "w+b");
    if (file == NULL) {
        free(ring);
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "Could not create performance file: %s",
                     strerror(errno));
        return 0;
    }
    if (!write_header(file, 0u, sample_rate, channels, 0)) {
        fclose(file);
        remove(path);
        free(ring);
        recorder_error(error, error_size,
                       "Could not initialize performance WAV");
        return 0;
    }
    recorder->ring = ring;
    recorder->ring_capacity_frames = queue_frames;
    recorder->file = file;
    recorder->sample_rate = sample_rate;
    recorder->channels = channels;
    recorder->checkpoint_frames = sample_rate;
    snprintf(recorder->path, sizeof(recorder->path), "%s", path);
    recorder->error[0] = '\0';
    atomic_store_explicit(&recorder->write_cursor, 0u, memory_order_relaxed);
    atomic_store_explicit(&recorder->read_cursor, 0u, memory_order_relaxed);
    atomic_store_explicit(&recorder->accepted_frames, 0u, memory_order_relaxed);
    atomic_store_explicit(&recorder->written_frames, 0u, memory_order_relaxed);
    atomic_store_explicit(&recorder->dropped_frames, 0u, memory_order_relaxed);
    atomic_store_explicit(&recorder->state, TS_PERFORMANCE_FILE_RECORDING,
                          memory_order_release);
    recorder_error(error, error_size, "");
    return 1;
}

int ts_performance_recorder_push_frame(TsPerformanceRecorder *recorder,
                                       TsStereoFrame frame)
{
    uint64_t write;
    uint64_t read;
    if (recorder == NULL ||
        atomic_load_explicit(&recorder->state, memory_order_relaxed) !=
            TS_PERFORMANCE_FILE_RECORDING)
        return 0;
    write = atomic_load_explicit(&recorder->write_cursor, memory_order_relaxed);
    read = atomic_load_explicit(&recorder->read_cursor, memory_order_acquire);
    if (write - read >= recorder->ring_capacity_frames) {
        atomic_fetch_add_explicit(&recorder->dropped_frames, 1u,
                                  memory_order_relaxed);
        return 0;
    }
    recorder->ring[write % recorder->ring_capacity_frames] =
        ts_stereo_frame_sanitize(frame);
    atomic_store_explicit(&recorder->write_cursor, write + 1u,
                          memory_order_release);
    atomic_fetch_add_explicit(&recorder->accepted_frames, 1u,
                              memory_order_relaxed);
    return 1;
}

int ts_performance_recorder_request_stop(TsPerformanceRecorder *recorder)
{
    TsPerformanceFileState expected = TS_PERFORMANCE_FILE_RECORDING;
    return recorder != NULL && atomic_compare_exchange_strong_explicit(
        &recorder->state, &expected, TS_PERFORMANCE_FILE_STOPPING,
        memory_order_acq_rel, memory_order_acquire);
}

static int write_batch(TsPerformanceRecorder *recorder, uint64_t read,
                       size_t frames)
{
    unsigned char output[TS_PERFORMANCE_WRITER_BATCH_FRAMES * 2u * 4u];
    size_t scalar_count = frames * recorder->channels;
    size_t at = 0u;
    for (size_t frame_index = 0u; frame_index < frames; ++frame_index) {
        TsStereoFrame frame = recorder->ring[
            (read + frame_index) % recorder->ring_capacity_frames];
        if (recorder->channels == 1u) {
            put_f32(output + at, (frame.l + frame.r) * 0.5f);
            at += 4u;
        } else {
            put_f32(output + at, frame.l);
            put_f32(output + at + 4u, frame.r);
            at += 8u;
        }
    }
    return fwrite(output, 4u, scalar_count, recorder->file) == scalar_count;
}

int ts_performance_recorder_pump(TsPerformanceRecorder *recorder,
                                 size_t max_frames)
{
    TsPerformanceFileState state;
    uint64_t read;
    uint64_t write;
    size_t remaining;
    if (recorder == NULL || max_frames == 0u) return 0;
    state = ts_performance_recorder_state(recorder);
    if (state != TS_PERFORMANCE_FILE_RECORDING &&
        state != TS_PERFORMANCE_FILE_STOPPING) return 0;
    read = atomic_load_explicit(&recorder->read_cursor, memory_order_relaxed);
    write = atomic_load_explicit(&recorder->write_cursor, memory_order_acquire);
    remaining = (size_t)(write - read > max_frames ? max_frames : write - read);
    while (remaining > 0u) {
        size_t batch = remaining > TS_PERFORMANCE_WRITER_BATCH_FRAMES ?
                       TS_PERFORMANCE_WRITER_BATCH_FRAMES : remaining;
        if (!write_batch(recorder, read, batch)) {
            fail_recorder(recorder, "Could not write performance audio to disk");
            return 0;
        }
        read += batch;
        remaining -= batch;
        atomic_store_explicit(&recorder->read_cursor, read,
                              memory_order_release);
        atomic_store_explicit(&recorder->written_frames, read,
                              memory_order_release);
    }
    if (read >= recorder->checkpoint_frames && recorder->file != NULL) {
        if (!write_header(recorder->file, read, recorder->sample_rate,
                          recorder->channels, 0)) {
            fail_recorder(recorder,
                          "Could not checkpoint the performance WAV header");
            return 0;
        }
        recorder->checkpoint_frames = read + recorder->sample_rate;
    }
    state = ts_performance_recorder_state(recorder);
    write = atomic_load_explicit(&recorder->write_cursor, memory_order_acquire);
    if (state == TS_PERFORMANCE_FILE_STOPPING && read == write) {
        int ok = write_header(recorder->file, read, recorder->sample_rate,
                              recorder->channels, 1) &&
                 fflush(recorder->file) == 0;
        if (fclose(recorder->file) != 0) ok = 0;
        recorder->file = NULL;
        if (!ok) {
            fail_recorder(recorder,
                          "Could not finish the performance WAV");
            return 0;
        }
        atomic_store_explicit(&recorder->state,
                              TS_PERFORMANCE_FILE_COMPLETED,
                              memory_order_release);
        return 0;
    }
    return 1;
}

TsPerformanceFileState ts_performance_recorder_state(
    const TsPerformanceRecorder *recorder)
{
    return recorder != NULL ? atomic_load_explicit(
        &recorder->state, memory_order_acquire) : TS_PERFORMANCE_FILE_IDLE;
}

uint64_t ts_performance_recorder_frames(
    const TsPerformanceRecorder *recorder)
{
    return recorder != NULL ? atomic_load_explicit(
        &recorder->accepted_frames, memory_order_acquire) : 0u;
}

uint64_t ts_performance_recorder_dropped(
    const TsPerformanceRecorder *recorder)
{
    return recorder != NULL ? atomic_load_explicit(
        &recorder->dropped_frames, memory_order_acquire) : 0u;
}
