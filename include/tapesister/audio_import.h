#ifndef TAPESISTER_AUDIO_IMPORT_H
#define TAPESISTER_AUDIO_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/sample.h"

typedef enum {
    TS_RAW_UNSIGNED_8 = 0,
    TS_RAW_SIGNED_8,
    TS_RAW_SIGNED_16,
    TS_RAW_SIGNED_24,
    TS_RAW_SIGNED_32,
    TS_RAW_FLOAT_32,
    TS_RAW_ENCODING_COUNT
} TsRawEncoding;

typedef enum {
    TS_RAW_LITTLE_ENDIAN = 0,
    TS_RAW_BIG_ENDIAN
} TsRawByteOrder;

typedef struct {
    TsRawEncoding encoding;
    TsRawByteOrder byte_order;
    uint32_t sample_rate;
    uint8_t channels;
    size_t byte_offset;
    int normalize;
} TsRawImportSettings;

typedef enum {
    TS_AUDIO_IMPORT_UNKNOWN = 0,
    TS_AUDIO_IMPORT_WAV,
    TS_AUDIO_IMPORT_FLAC,
    TS_AUDIO_IMPORT_MP3,
    TS_AUDIO_IMPORT_OGG_VORBIS,
    TS_AUDIO_IMPORT_RAW
} TsAudioImportKind;

typedef struct {
    TsSample sample;
    TsTuning tuning;
    size_t loop_first;
    size_t loop_last;
    TsLoopMode loop_mode;
    TsAudioImportKind kind;
    int has_loop;
} TsAudioImport;

void ts_raw_import_settings_default(TsRawImportSettings *settings);
const char *ts_raw_encoding_name(TsRawEncoding encoding);
size_t ts_raw_encoding_bytes(TsRawEncoding encoding);
const char *ts_audio_import_kind_name(TsAudioImportKind kind);

void ts_audio_import_init(TsAudioImport *imported);
void ts_audio_import_free(TsAudioImport *imported);

/* Decode a recognized audio file. WAV retains sampler tuning and loop chunks;
   FLAC, MP3 and Ogg Vorbis are decoded by pinned, in-process miniaudio. */
int ts_audio_import_decode(TsAudioImport *imported, const char *path,
                           char *error, size_t error_size);

/* Interpret any ordinary file as PCM-like sample data. The operation is
   transactional: imported is unchanged when validation or decoding fails. */
int ts_audio_import_decode_raw(TsAudioImport *imported, const char *path,
                               const TsRawImportSettings *settings,
                               char *error, size_t error_size);

#endif
