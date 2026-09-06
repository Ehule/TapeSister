#include "tapesister/audio_import.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

static void write_bytes(const char *path, const unsigned char *bytes, size_t count)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(bytes, 1u, count, file) == count);
    assert(fclose(file) == 0);
}

static void test_defaults_and_names(void)
{
    TsRawImportSettings settings;
    ts_raw_import_settings_default(&settings);
    assert(settings.encoding == TS_RAW_SIGNED_8);
    assert(settings.byte_order == TS_RAW_LITTLE_ENDIAN);
    assert(settings.sample_rate == 44100u);
    assert(settings.channels == 1u);
    assert(settings.byte_offset == 0u);
    assert(settings.normalize == 1);
    assert(strcmp(ts_raw_encoding_name(TS_RAW_SIGNED_24), "SIGNED 24") == 0);
    assert(ts_raw_encoding_bytes(TS_RAW_FLOAT_32) == 4u);
    assert(strcmp(ts_audio_import_kind_name(TS_AUDIO_IMPORT_OGG_VORBIS),
                  "OGG VORBIS") == 0);
}

static void test_signed_8_and_normalize(void)
{
    static const unsigned char bytes[] = {0x80u, 0x00u, 0x40u, 0x7fu};
    TsAudioImport imported;
    TsRawImportSettings settings;
    char error[160];
    write_bytes("test-import-s8.bin", bytes, sizeof(bytes));
    ts_audio_import_init(&imported);
    ts_raw_import_settings_default(&settings);
    settings.normalize = 0;
    assert(ts_audio_import_decode_raw(&imported, "test-import-s8.bin",
                                      &settings, error, sizeof(error)));
    assert(imported.kind == TS_AUDIO_IMPORT_RAW);
    assert(imported.sample.frames == 4u);
    assert(imported.sample.channels == 1u);
    assert(imported.sample.sample_rate == 44100u);
    assert(near(imported.sample.data[0], -1.0f));
    assert(near(imported.sample.data[1], 0.0f));
    assert(near(imported.sample.data[2], 0.5f));
    assert(near(imported.sample.data[3], 127.0f / 128.0f));

    settings.normalize = 1;
    assert(ts_audio_import_decode_raw(&imported, "test-import-s8.bin",
                                      &settings, error, sizeof(error)));
    assert(near(imported.sample.data[0], -0.95f));
    ts_audio_import_free(&imported);
    remove("test-import-s8.bin");
}

static void test_endian_stereo_and_offset(void)
{
    static const unsigned char bytes[] = {
        0xaau, 0xbbu,
        0x40u, 0x00u, 0xc0u, 0x00u,
        0x7fu, 0xffu, 0x80u, 0x00u
    };
    TsAudioImport imported;
    TsRawImportSettings settings;
    char error[160];
    write_bytes("test-import-s16.bin", bytes, sizeof(bytes));
    ts_audio_import_init(&imported);
    ts_raw_import_settings_default(&settings);
    settings.encoding = TS_RAW_SIGNED_16;
    settings.byte_order = TS_RAW_BIG_ENDIAN;
    settings.channels = 2u;
    settings.sample_rate = 22050u;
    settings.byte_offset = 2u;
    settings.normalize = 0;
    assert(ts_audio_import_decode_raw(&imported, "test-import-s16.bin",
                                      &settings, error, sizeof(error)));
    assert(imported.sample.frames == 2u);
    assert(imported.sample.channels == 2u);
    assert(imported.sample.sample_rate == 22050u);
    assert(near(imported.sample.data[0], 0.5f));
    assert(near(imported.sample.data[1], -0.5f));
    assert(imported.sample.data[2] > 0.999f);
    assert(near(imported.sample.data[3], -1.0f));
    ts_audio_import_free(&imported);
    remove("test-import-s16.bin");
}

static void test_signed_24_and_big_endian_float(void)
{
    static const unsigned char signed_24[] = {
        0x00u, 0x00u, 0x40u, 0x00u, 0x00u, 0xc0u
    };
    static const unsigned char float_32[] = {
        0x3fu, 0x00u, 0x00u, 0x00u,
        0xbfu, 0x40u, 0x00u, 0x00u
    };
    TsAudioImport imported;
    TsRawImportSettings settings;
    char error[160];
    ts_audio_import_init(&imported);
    ts_raw_import_settings_default(&settings);
    settings.normalize = 0;
    settings.encoding = TS_RAW_SIGNED_24;
    write_bytes("test-import-s24.bin", signed_24, sizeof(signed_24));
    assert(ts_audio_import_decode_raw(&imported, "test-import-s24.bin",
                                      &settings, error, sizeof(error)));
    assert(imported.sample.frames == 2u);
    assert(near(imported.sample.data[0], 0.5f));
    assert(near(imported.sample.data[1], -0.5f));
    settings.encoding = TS_RAW_FLOAT_32;
    settings.byte_order = TS_RAW_BIG_ENDIAN;
    write_bytes("test-import-f32.bin", float_32, sizeof(float_32));
    assert(ts_audio_import_decode_raw(&imported, "test-import-f32.bin",
                                      &settings, error, sizeof(error)));
    assert(imported.sample.frames == 2u);
    assert(near(imported.sample.data[0], 0.5f));
    assert(near(imported.sample.data[1], -0.75f));
    ts_audio_import_free(&imported);
    remove("test-import-s24.bin");
    remove("test-import-f32.bin");
}

static void test_failed_decode_is_transactional(void)
{
    static const unsigned char bytes[] = {1u, 2u, 3u, 4u};
    TsAudioImport imported;
    TsRawImportSettings settings;
    float *original;
    char error[160];
    write_bytes("test-import-transaction.bin", bytes, sizeof(bytes));
    ts_audio_import_init(&imported);
    ts_raw_import_settings_default(&settings);
    assert(ts_audio_import_decode_raw(&imported, "test-import-transaction.bin",
                                      &settings, error, sizeof(error)));
    original = imported.sample.data;
    settings.byte_offset = sizeof(bytes);
    assert(!ts_audio_import_decode_raw(&imported, "test-import-transaction.bin",
                                       &settings, error, sizeof(error)));
    assert(imported.sample.data == original);
    assert(imported.sample.frames == sizeof(bytes));
    ts_audio_import_free(&imported);
    remove("test-import-transaction.bin");
}

static void test_recognized_decoder_rejects_arbitrary_data(void)
{
    static const unsigned char bytes[] = {0u, 1u, 2u, 3u, 4u, 5u};
    TsAudioImport imported;
    char error[160];
    write_bytes("test-import-unknown.dat", bytes, sizeof(bytes));
    ts_audio_import_init(&imported);
    assert(!ts_audio_import_decode(&imported, "test-import-unknown.dat",
                                   error, sizeof(error)));
    assert(strstr(error, "RAW DATA") != NULL);
    assert(imported.sample.data == NULL);
    ts_audio_import_free(&imported);
    remove("test-import-unknown.dat");
}

static void test_wav_decoder_and_instrument_install(void)
{
    static float values[] = {-0.5f, 0.25f, 0.75f, -1.0f};
    TsSample source;
    TsAudioImport imported;
    TsInstrument instrument;
    char error[160];
    ts_sample_init(&source);
    source.data = values;
    source.frames = 2u;
    source.channels = 2u;
    source.sample_rate = 32000u;
    snprintf(source.name, sizeof(source.name), "SOURCE");
    assert(ts_sample_save_wav32f(&source, "test-import.wav", error, sizeof(error)));
    source.data = NULL;
    ts_audio_import_init(&imported);
    assert(ts_audio_import_decode(&imported, "test-import.wav", error, sizeof(error)));
    assert(imported.kind == TS_AUDIO_IMPORT_WAV);
    assert(imported.sample.frames == 2u && imported.sample.channels == 2u);
    assert(imported.sample.sample_rate == 32000u);
    ts_instrument_init(&instrument);
    assert(ts_instrument_import_sample(&instrument, &imported.sample, 0,
                                       0u, 0u, TS_LOOP_FORWARD,
                                       error, sizeof(error)));
    assert(instrument.bank[0].occupied && instrument.parent.frames == 2u);
    assert(instrument.parent.data != imported.sample.data);
    ts_instrument_free(&instrument);
    ts_audio_import_free(&imported);
    remove("test-import.wav");
}

int main(int argc, char **argv)
{
    test_defaults_and_names();
    test_signed_8_and_normalize();
    test_endian_stereo_and_offset();
    test_signed_24_and_big_endian_float();
    test_failed_decode_is_transactional();
    test_recognized_decoder_rejects_arbitrary_data();
    test_wav_decoder_and_instrument_install();
    for (int argument = 1; argument < argc; ++argument) {
        TsAudioImport imported;
        char error[160];
        ts_audio_import_init(&imported);
        assert(ts_audio_import_decode(&imported, argv[argument],
                                      error, sizeof(error)));
        assert(imported.kind != TS_AUDIO_IMPORT_UNKNOWN &&
               imported.kind != TS_AUDIO_IMPORT_RAW);
        assert(imported.sample.data != NULL && imported.sample.frames > 0u);
        ts_audio_import_free(&imported);
    }
    puts("Audio import tests passed");
    return 0;
}
