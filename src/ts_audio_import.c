#include "tapesister/audio_import.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TapeSister uses miniaudio only as a pinned, in-process file decoder. Audio
   devices remain owned by the existing SDL lifecycle. */
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static const char *path_basename(const char *path)
{
    const char *slash = path != NULL ? strrchr(path, '/') : NULL;
    const char *backslash = path != NULL ? strrchr(path, '\\') : NULL;
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    return slash != NULL ? slash + 1 : path != NULL ? path : "IMPORT";
}

void ts_raw_import_settings_default(TsRawImportSettings *settings)
{
    if (settings == NULL) return;
    settings->encoding = TS_RAW_SIGNED_8;
    settings->byte_order = TS_RAW_LITTLE_ENDIAN;
    settings->sample_rate = 44100u;
    settings->channels = 1u;
    settings->byte_offset = 0u;
    settings->normalize = 1;
}

const char *ts_raw_encoding_name(TsRawEncoding encoding)
{
    static const char *names[TS_RAW_ENCODING_COUNT] = {
        "UNSIGNED 8", "SIGNED 8", "SIGNED 16", "SIGNED 24",
        "SIGNED 32", "FLOAT 32"
    };
    return encoding >= 0 && encoding < TS_RAW_ENCODING_COUNT ?
           names[encoding] : "SIGNED 8";
}

size_t ts_raw_encoding_bytes(TsRawEncoding encoding)
{
    switch (encoding) {
    case TS_RAW_UNSIGNED_8:
    case TS_RAW_SIGNED_8: return 1u;
    case TS_RAW_SIGNED_16: return 2u;
    case TS_RAW_SIGNED_24: return 3u;
    case TS_RAW_SIGNED_32:
    case TS_RAW_FLOAT_32: return 4u;
    default: return 0u;
    }
}

const char *ts_audio_import_kind_name(TsAudioImportKind kind)
{
    switch (kind) {
    case TS_AUDIO_IMPORT_WAV: return "WAV";
    case TS_AUDIO_IMPORT_FLAC: return "FLAC";
    case TS_AUDIO_IMPORT_MP3: return "MP3";
    case TS_AUDIO_IMPORT_OGG_VORBIS: return "OGG VORBIS";
    case TS_AUDIO_IMPORT_RAW: return "RAW DATA";
    default: return "UNKNOWN";
    }
}

void ts_audio_import_init(TsAudioImport *imported)
{
    if (imported == NULL) return;
    memset(imported, 0, sizeof(*imported));
    ts_sample_init(&imported->sample);
    imported->tuning.root_note = TS_KEYBOARD_BASE_NOTE;
    imported->loop_mode = TS_LOOP_FORWARD;
}

void ts_audio_import_free(TsAudioImport *imported)
{
    if (imported == NULL) return;
    ts_sample_free(&imported->sample);
    ts_audio_import_init(imported);
}

TsAudioImportKind ts_audio_import_detect_kind(const char *path)
{
    unsigned char header[16] = {0};
    FILE *file;
    size_t count;
    if (path == NULL || path[0] == '\0') return TS_AUDIO_IMPORT_UNKNOWN;
    file = fopen(path, "rb");
    if (file == NULL) return TS_AUDIO_IMPORT_UNKNOWN;
    count = fread(header, 1u, sizeof(header), file);
    fclose(file);
    if (count >= 12u &&
        (memcmp(header, "RIFF", 4u) == 0 || memcmp(header, "RF64", 4u) == 0) &&
        memcmp(header + 8u, "WAVE", 4u) == 0) return TS_AUDIO_IMPORT_WAV;
    if (count >= 4u && memcmp(header, "fLaC", 4u) == 0) return TS_AUDIO_IMPORT_FLAC;
    if (count >= 4u && memcmp(header, "OggS", 4u) == 0) return TS_AUDIO_IMPORT_OGG_VORBIS;
    if ((count >= 3u && memcmp(header, "ID3", 3u) == 0) ||
        (count >= 2u && header[0] == 0xffu && (header[1] & 0xe0u) == 0xe0u))
        return TS_AUDIO_IMPORT_MP3;
    return TS_AUDIO_IMPORT_UNKNOWN;
}

static int decode_miniaudio(TsSample *sample, const char *path,
                            TsAudioImportCancelCheck cancel_check,
                            void *cancel_userdata,
                            char *error, size_t error_size)
{
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
    float *data = NULL;
    size_t frames = 0u;
    size_t capacity = 0u;
    ma_result result;
    result = ma_decoder_init_file(path, &config, &decoder);
    if (result != MA_SUCCESS) {
        set_error(error, error_size, "Unsupported or damaged audio file");
        return 0;
    }
    if (decoder.outputChannels > 2u) {
        ma_decoder_uninit(&decoder);
        config = ma_decoder_config_init(ma_format_f32, 2u, 0u);
        result = ma_decoder_init_file(path, &config, &decoder);
        if (result != MA_SUCCESS) {
            set_error(error, error_size,
                      "Could not downmix multichannel audio to stereo");
            return 0;
        }
    }
    if (decoder.outputChannels == 0u || decoder.outputChannels > 2u ||
        decoder.outputSampleRate < 1000u) {
        ma_decoder_uninit(&decoder);
        set_error(error, error_size,
                  "Decoded audio must contain one or two channels");
        return 0;
    }
    for (;;) {
        enum { CHUNK_FRAMES = 4096 };
        ma_uint64 read = 0u;
        size_t wanted = frames + CHUNK_FRAMES;
        if (cancel_check != NULL && cancel_check(cancel_userdata)) {
            free(data);
            ma_decoder_uninit(&decoder);
            set_error(error, error_size, "Import cancelled");
            return 0;
        }
        if (wanted > TS_CANVAS_MAX_FRAMES) wanted = TS_CANVAS_MAX_FRAMES;
        if (wanted > capacity) {
            size_t next = capacity > 0u ? capacity : CHUNK_FRAMES;
            size_t scalar_count;
            float *grown;
            while (next < wanted && next <= TS_CANVAS_MAX_FRAMES / 2u) next *= 2u;
            if (next < wanted) next = wanted;
            if (!ts_sample_dimensions(next, (uint8_t)decoder.outputChannels,
                                      &scalar_count, NULL)) {
                free(data);
                ma_decoder_uninit(&decoder);
                set_error(error, error_size, "Decoded audio is too large");
                return 0;
            }
            grown = (float *)realloc(data, scalar_count * sizeof(*grown));
            if (grown == NULL) {
                free(data);
                ma_decoder_uninit(&decoder);
                set_error(error, error_size, "Out of memory while decoding audio");
                return 0;
            }
            data = grown;
            capacity = next;
        }
        if (frames >= TS_CANVAS_MAX_FRAMES) {
            free(data);
            ma_decoder_uninit(&decoder);
            set_error(error, error_size, "Decoded audio exceeds TapeSister's frame limit");
            return 0;
        }
        result = ma_decoder_read_pcm_frames(
            &decoder, data + frames * decoder.outputChannels,
            (ma_uint64)(wanted - frames), &read);
        frames += (size_t)read;
        if (cancel_check != NULL && cancel_check(cancel_userdata)) {
            free(data);
            ma_decoder_uninit(&decoder);
            set_error(error, error_size, "Import cancelled");
            return 0;
        }
        if (read == 0u || result == MA_AT_END) break;
        if (result != MA_SUCCESS) {
            free(data);
            ma_decoder_uninit(&decoder);
            set_error(error, error_size, "Audio decoder stopped before end of file");
            return 0;
        }
    }
    if (frames == 0u) {
        free(data);
        ma_decoder_uninit(&decoder);
        set_error(error, error_size, "Audio file contains no decoded frames");
        return 0;
    }
    {
        size_t scalar_count = frames * decoder.outputChannels;
        for (size_t i = 0u; i < scalar_count; ++i)
            data[i] = isfinite(data[i]) ?
                      (data[i] < -1.0f ? -1.0f : data[i] > 1.0f ? 1.0f : data[i]) : 0.0f;
    }
    {
        TsSample decoded;
        ts_sample_init(&decoded);
        decoded.data = data;
        decoded.frames = frames;
        decoded.sample_rate = decoder.outputSampleRate;
        decoded.channels = (uint8_t)decoder.outputChannels;
        snprintf(decoded.name, sizeof(decoded.name), "%s", path_basename(path));
        ma_decoder_uninit(&decoder);
        ts_sample_free(sample);
        *sample = decoded;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_audio_import_decode_cancelable(
    TsAudioImport *imported, const char *path,
    TsAudioImportCancelCheck cancel_check, void *cancel_userdata,
    char *error, size_t error_size)
{
    TsAudioImport decoded;
    TsAudioImportKind kind;
    int ok;
    if (imported == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "Choose an audio file to import");
        return 0;
    }
    kind = ts_audio_import_detect_kind(path);
    if (kind == TS_AUDIO_IMPORT_UNKNOWN) {
        set_error(error, error_size,
                  "Not WAV, FLAC, MP3, or Ogg Vorbis; try RAW DATA");
        return 0;
    }
    ts_audio_import_init(&decoded);
    decoded.kind = kind;
    if (kind == TS_AUDIO_IMPORT_WAV) {
        char wav_error[160];
        ok = ts_sample_load_wav_metadata(
            &decoded.sample, &decoded.tuning, &decoded.has_loop,
            &decoded.loop_first, &decoded.loop_last, &decoded.loop_mode,
            path, wav_error, sizeof(wav_error));
        if (!ok) {
            ok = decode_miniaudio(&decoded.sample, path,
                                  cancel_check, cancel_userdata,
                                  error, error_size);
            if (!ok && strcmp(error, "Unsupported or damaged audio file") == 0)
                set_error(error, error_size, wav_error);
        }
    } else {
        ok = decode_miniaudio(&decoded.sample, path,
                              cancel_check, cancel_userdata,
                              error, error_size);
    }
    if (!ok) {
        ts_audio_import_free(&decoded);
        return 0;
    }
    ts_audio_import_free(imported);
    *imported = decoded;
    set_error(error, error_size, "");
    return 1;
}

int ts_audio_import_decode(TsAudioImport *imported, const char *path,
                           char *error, size_t error_size)
{
    return ts_audio_import_decode_cancelable(imported, path, NULL, NULL,
                                             error, error_size);
}

int ts_audio_import_copy_range(TsAudioImport *destination,
                               const TsAudioImport *source,
                               size_t first, size_t last,
                               char *error, size_t error_size)
{
    TsAudioImport copied;
    size_t frames;
    size_t scalar_count;
    size_t byte_count;
    if (destination == NULL || source == NULL || source->sample.data == NULL ||
        source->sample.channels == 0u || first >= last ||
        last > source->sample.frames) {
        set_error(error, error_size, "Choose a nonempty preview selection");
        return 0;
    }
    frames = last - first;
    if (!ts_sample_dimensions(frames, source->sample.channels,
                              &scalar_count, &byte_count)) {
        set_error(error, error_size, "Preview selection is too large");
        return 0;
    }
    ts_audio_import_init(&copied);
    copied.sample.data = (float *)malloc(byte_count);
    if (copied.sample.data == NULL) {
        set_error(error, error_size, "Out of memory copying preview selection");
        return 0;
    }
    memcpy(copied.sample.data,
           source->sample.data + first * source->sample.channels,
           scalar_count * sizeof(*copied.sample.data));
    copied.sample.frames = frames;
    copied.sample.channels = source->sample.channels;
    copied.sample.sample_rate = source->sample.sample_rate;
    snprintf(copied.sample.name, sizeof(copied.sample.name), "%s",
             source->sample.name);
    copied.kind = source->kind;
    copied.tuning = source->tuning;
    copied.loop_mode = source->loop_mode;
    if (source->has_loop && source->loop_first >= first &&
        source->loop_last <= last && source->loop_first < source->loop_last) {
        copied.has_loop = 1;
        copied.loop_first = source->loop_first - first;
        copied.loop_last = source->loop_last - first;
    }
    ts_audio_import_free(destination);
    *destination = copied;
    set_error(error, error_size, "");
    return 1;
}

static uint32_t read_word(const unsigned char *source, size_t bytes,
                          TsRawByteOrder order)
{
    uint32_t value = 0u;
    if (order == TS_RAW_BIG_ENDIAN) {
        for (size_t i = 0u; i < bytes; ++i) value = (value << 8u) | source[i];
    } else {
        for (size_t i = 0u; i < bytes; ++i) value |= (uint32_t)source[i] << (i * 8u);
    }
    return value;
}

static float decode_raw_scalar(const unsigned char *source,
                               const TsRawImportSettings *settings)
{
    uint32_t word;
    switch (settings->encoding) {
    case TS_RAW_UNSIGNED_8:
        return ((float)source[0] - 128.0f) / 128.0f;
    case TS_RAW_SIGNED_8:
        return (float)(int8_t)source[0] / 128.0f;
    case TS_RAW_SIGNED_16:
        word = read_word(source, 2u, settings->byte_order);
        return (float)(int16_t)word / 32768.0f;
    case TS_RAW_SIGNED_24: {
        int32_t value;
        word = read_word(source, 3u, settings->byte_order);
        value = (int32_t)word;
        if ((word & 0x00800000u) != 0u) value |= (int32_t)0xff000000u;
        return (float)value / 8388608.0f;
    }
    case TS_RAW_SIGNED_32:
        word = read_word(source, 4u, settings->byte_order);
        return (float)(int32_t)word / 2147483648.0f;
    case TS_RAW_FLOAT_32:
        word = read_word(source, 4u, settings->byte_order);
        {
            float value;
            memcpy(&value, &word, sizeof(value));
            return isfinite(value) ?
                   (value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value) : 0.0f;
        }
    default:
        return 0.0f;
    }
}

static int measure_file(FILE *file, uint64_t *size)
{
#ifdef _WIN32
    __int64 measured;
    if (_fseeki64(file, 0, SEEK_END) != 0 ||
        (measured = _ftelli64(file)) < 0) return 0;
#else
    long measured;
    if (fseek(file, 0, SEEK_END) != 0 || (measured = ftell(file)) < 0) return 0;
#endif
    *size = (uint64_t)measured;
    return 1;
}

static int seek_file(FILE *file, uint64_t offset)
{
#ifdef _WIN32
    return offset <= (uint64_t)INT64_MAX &&
           _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
#else
    return offset <= (uint64_t)LONG_MAX &&
           fseek(file, (long)offset, SEEK_SET) == 0;
#endif
}

int ts_audio_import_decode_raw_cancelable(
    TsAudioImport *imported, const char *path,
    const TsRawImportSettings *settings,
    TsAudioImportCancelCheck cancel_check, void *cancel_userdata,
    char *error, size_t error_size)
{
    FILE *file;
    uint64_t measured_size;
    size_t file_size;
    size_t bytes_per_scalar;
    size_t bytes_per_frame;
    size_t frames;
    size_t scalar_count;
    unsigned char *bytes;
    TsAudioImport decoded;
    float peak = 0.0f;
    if (imported == NULL || path == NULL || settings == NULL ||
        settings->encoding < 0 || settings->encoding >= TS_RAW_ENCODING_COUNT ||
        (settings->byte_order != TS_RAW_LITTLE_ENDIAN &&
         settings->byte_order != TS_RAW_BIG_ENDIAN) ||
        (settings->channels != 1u && settings->channels != 2u) ||
        settings->sample_rate < 1000u || settings->sample_rate > 384000u) {
        set_error(error, error_size, "Invalid raw-data import settings");
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        char message[192];
        snprintf(message, sizeof(message), "Could not open raw data: %s", strerror(errno));
        set_error(error, error_size, message);
        return 0;
    }
    if (!measure_file(file, &measured_size)) {
        fclose(file);
        set_error(error, error_size, "Could not measure raw-data file");
        return 0;
    }
    if (measured_size > (uint64_t)SIZE_MAX) {
        fclose(file);
        set_error(error, error_size, "Raw-data file is too large");
        return 0;
    }
    file_size = (size_t)measured_size;
    bytes_per_scalar = ts_raw_encoding_bytes(settings->encoding);
    bytes_per_frame = bytes_per_scalar * settings->channels;
    if (settings->byte_offset >= file_size ||
        file_size - settings->byte_offset < bytes_per_frame) {
        fclose(file);
        set_error(error, error_size, "Raw offset leaves no complete audio frame");
        return 0;
    }
    frames = (file_size - settings->byte_offset) / bytes_per_frame;
    if (frames > TS_CANVAS_MAX_FRAMES ||
        !ts_sample_dimensions(frames, settings->channels, &scalar_count, NULL)) {
        fclose(file);
        set_error(error, error_size, "Raw-data interpretation is too large");
        return 0;
    }
    bytes = (unsigned char *)malloc(scalar_count * bytes_per_scalar);
    ts_audio_import_init(&decoded);
    decoded.sample.data = (float *)malloc(scalar_count * sizeof(float));
    if (bytes == NULL || decoded.sample.data == NULL) {
        free(bytes);
        fclose(file);
        ts_audio_import_free(&decoded);
        set_error(error, error_size, "Out of memory while interpreting raw data");
        return 0;
    }
    if (!seek_file(file, (uint64_t)settings->byte_offset) ||
        fread(bytes, bytes_per_scalar, scalar_count, file) != scalar_count) {
        free(bytes);
        fclose(file);
        ts_audio_import_free(&decoded);
        set_error(error, error_size, "Raw-data file ended unexpectedly");
        return 0;
    }
    fclose(file);
    for (size_t i = 0u; i < scalar_count; ++i) {
        float value = decode_raw_scalar(bytes + i * bytes_per_scalar, settings);
        float magnitude = fabsf(value);
        if ((i & 4095u) == 0u && cancel_check != NULL &&
            cancel_check(cancel_userdata)) {
            free(bytes);
            ts_audio_import_free(&decoded);
            set_error(error, error_size, "Import cancelled");
            return 0;
        }
        decoded.sample.data[i] = value;
        if (magnitude > peak) peak = magnitude;
    }
    free(bytes);
    if (settings->normalize && peak > 0.000001f) {
        float gain = 0.95f / peak;
        for (size_t i = 0u; i < scalar_count; ++i) {
            if ((i & 4095u) == 0u && cancel_check != NULL &&
                cancel_check(cancel_userdata)) {
                ts_audio_import_free(&decoded);
                set_error(error, error_size, "Import cancelled");
                return 0;
            }
            decoded.sample.data[i] *= gain;
        }
    }
    decoded.sample.frames = frames;
    decoded.sample.channels = settings->channels;
    decoded.sample.sample_rate = settings->sample_rate;
    snprintf(decoded.sample.name, sizeof(decoded.sample.name), "%s", path_basename(path));
    decoded.kind = TS_AUDIO_IMPORT_RAW;
    ts_audio_import_free(imported);
    *imported = decoded;
    set_error(error, error_size, "");
    return 1;
}

int ts_audio_import_decode_raw(TsAudioImport *imported, const char *path,
                               const TsRawImportSettings *settings,
                               char *error, size_t error_size)
{
    return ts_audio_import_decode_raw_cancelable(
        imported, path, settings, NULL, NULL, error, error_size);
}
