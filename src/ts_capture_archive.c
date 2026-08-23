#include "tapesister/capture_archive.h"

#include "tapesister/sample.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define TS_MKDIR(path) _mkdir(path)
#define TS_GETPID() _getpid()
#else
#include <unistd.h>
#define TS_MKDIR(path) mkdir(path, 0775)
#define TS_GETPID() getpid()
#endif

static void archive_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static int path_exists(const char *path)
{
    struct stat info;
    return path != NULL && stat(path, &info) == 0;
}

static int ensure_directory(const char *path, char *error, size_t error_size)
{
    struct stat info;
    if (path == NULL || path[0] == '\0') {
        archive_error(error, error_size, "Capture directory is blank");
        return 0;
    }
    if (stat(path, &info) == 0) {
#ifdef _WIN32
        if ((info.st_mode & _S_IFDIR) != 0) return 1;
#else
        if (S_ISDIR(info.st_mode)) return 1;
#endif
        archive_error(error, error_size, "Captures path is not a directory");
        return 0;
    }
    if (TS_MKDIR(path) == 0 || errno == EEXIST) return 1;
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "Could not create Captures directory: %s",
                 strerror(errno));
    return 0;
}

static int finish_capture_file(const char *temporary, const char *destination,
                               char *error, size_t error_size)
{
#ifdef _WIN32
    if (rename(temporary, destination) == 0) return 1;
#else
    if (rename(temporary, destination) == 0) return 1;
#endif
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "Could not finish capture archive: %s",
                 strerror(errno));
    remove(temporary);
    return 0;
}

int ts_capture_archive_write(const char *directory,
                             TsCaptureArchiveKind kind,
                             const float *samples, size_t frames,
                             uint32_t sample_rate,
                             char *written_path, size_t written_path_size,
                             char *error, size_t error_size)
{
    return ts_capture_archive_write_channels(
        directory, kind, samples, frames, sample_rate, 1u,
        written_path, written_path_size, error, error_size);
}

int ts_capture_archive_write_channels(
    const char *directory, TsCaptureArchiveKind kind,
    const float *samples, size_t frames, uint32_t sample_rate,
    uint8_t channels, char *written_path, size_t written_path_size,
    char *error, size_t error_size)
{
    struct timespec now;
    struct tm clock_value;
    char timestamp[40];
    char destination[1200];
    char temporary[1240];
    const char *prefix = kind == TS_CAPTURE_ARCHIVE_INTERNAL ? "CAPTURE" :
                         kind == TS_CAPTURE_ARCHIVE_SYNTH ? "SYNTH" : "INPUT";
    TsSample sample;
    long milliseconds;
    int found = 0;
    if (written_path != NULL && written_path_size > 0u) written_path[0] = '\0';
    if (samples == NULL || frames == 0u || sample_rate == 0u ||
        !ts_sample_valid_channels(channels)) {
        archive_error(error, error_size, "Capture archive received no audio");
        return 0;
    }
    if (!ensure_directory(directory, error, error_size)) return 0;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) {
        archive_error(error, error_size, "Could not timestamp capture");
        return 0;
    }
#ifdef _WIN32
    if (localtime_s(&clock_value, &now.tv_sec) != 0) {
        archive_error(error, error_size, "Could not format capture timestamp");
        return 0;
    }
#else
    {
        struct tm *local = localtime(&now.tv_sec);
        if (local != NULL) clock_value = *local;
        if (local == NULL) {
            archive_error(error, error_size, "Could not format capture timestamp");
            return 0;
        }
    }
#endif
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H%M%S",
                 &clock_value) == 0u) {
        archive_error(error, error_size, "Could not format capture timestamp");
        return 0;
    }
    milliseconds = now.tv_nsec / 1000000L;
    for (int collision = 0; collision < 10000; ++collision) {
        int written = snprintf(destination, sizeof(destination),
                               "%s/%s_%s_%03ld_P%d_%03d.wav",
                               directory, prefix, timestamp, milliseconds,
                               (int)TS_GETPID(), collision);
        if (written < 0 || (size_t)written >= sizeof(destination)) {
            archive_error(error, error_size, "Capture archive path is too long");
            return 0;
        }
        if (!path_exists(destination)) {
            found = 1;
            break;
        }
    }
    if (!found) {
        archive_error(error, error_size, "Could not allocate a unique capture filename");
        return 0;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", destination) < 0 ||
        strlen(destination) + strlen(".tapesister-tmp") >= sizeof(temporary)) {
        archive_error(error, error_size, "Capture archive path is too long");
        return 0;
    }
    sample.data = (float *)samples;
    sample.frames = frames;
    sample.sample_rate = sample_rate;
    sample.visual_revision = 0u;
    sample.channels = channels;
    snprintf(sample.name, sizeof(sample.name), "%s", prefix);
    if (!ts_sample_save_wav32f(&sample, temporary, error, error_size)) {
        remove(temporary);
        return 0;
    }
    if (!finish_capture_file(temporary, destination, error, error_size)) return 0;
    if (written_path != NULL && written_path_size > 0u)
        snprintf(written_path, written_path_size, "%s", destination);
    archive_error(error, error_size, "");
    return 1;
}
