#include "tapesister/sample.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(FILE *f, uint32_t value)
{
    unsigned char p[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                          (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    assert(fwrite(p, 1u, sizeof(p), f) == sizeof(p));
}

static void put64(FILE *f, uint64_t value)
{
    put32(f, (uint32_t)value);
    put32(f, (uint32_t)(value >> 32));
}

static void put_float(FILE *f, float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    put32(f, bits.u);
}

static uint64_t legacy_hash(uint32_t rate, const float *data, size_t frames)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash ^= rate; hash *= UINT64_C(1099511628211);
    hash ^= frames; hash *= UINT64_C(1099511628211);
    for (size_t i = 0u; i < frames; ++i) {
        float value = data[i];
        int32_t q;
        if (value < -1.0f) value = -1.0f;
        if (value > 1.0f) value = 1.0f;
        q = (int32_t)lrintf(value * 8388607.0f);
        hash ^= (uint32_t)q;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void write_tsr6(const char *path)
{
    static const float audio[] = {-0.5f, 0.25f, 0.75f};
    static const char name[] = "legacy";
    TsProcessRecipe process;
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    ts_process_recipe_reset(&process);
    assert(fwrite("TSR6\r\n\032\n", 1u, 8u, f) == 8u);
    put32(f, TS_SOURCE_IMPORTED); put32(f, 4u); put64(f, 0u);
    put32(f, 0x54415045u); put32(f, TS_GENERATOR_TONAL);
    put_float(f, 2.0f); put_float(f, 261.625565f);
    put32(f, process.seed);
    put_float(f, process.body); put_float(f, process.edge); put_float(f, process.drift);
    put32(f, (uint32_t)process.noise_enabled); put_float(f, process.noise_amount);
    put32(f, process.noise_color); put32(f, (uint32_t)process.delay_enabled);
    put_float(f, process.delay_seconds); put_float(f, process.delay_feedback);
    put_float(f, process.delay_damping); put_float(f, process.delay_mix);
    put32(f, (uint32_t)process.reverb_enabled); put_float(f, process.reverb_decay);
    put_float(f, process.reverb_damping); put_float(f, process.reverb_mix);
    put64(f, 0u); put64(f, 3u);
    put64(f, 0u); put64(f, 0u);
    put64(f, 0u); put64(f, 3u);
    put64(f, 0u); put64(f, 0u);
    put_float(f, 8.0f);
    put32(f, 0u); put32(f, 0u); put32(f, 0u);
    put32(f, 22050u); put64(f, 3u); put32(f, (uint32_t)strlen(name));
    assert(fwrite(name, 1u, strlen(name), f) == strlen(name));
    for (size_t i = 0u; i < 3u; ++i) put_float(f, audio[i]);
    put64(f, legacy_hash(22050u, audio, 3u));
    fclose(f);
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long length;
    assert(f != NULL);
    assert(fseek(f, 0L, SEEK_END) == 0);
    length = ftell(f);
    assert(length > 0 && fseek(f, 0L, SEEK_SET) == 0);
    data = malloc((size_t)length);
    assert(data != NULL);
    assert(fread(data, 1u, (size_t)length, f) == (size_t)length);
    fclose(f);
    *size = (size_t)length;
    return data;
}

static void write_file(const char *path, const unsigned char *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(data, 1u, size, f) == size);
    fclose(f);
}

static size_t find_sample_header(const unsigned char *data, size_t size,
                                 uint32_t rate, uint64_t frames,
                                 uint32_t channels)
{
    unsigned char wanted[16];
    for (size_t i = 0u; i < 4u; ++i) wanted[i] = (unsigned char)(rate >> (i * 8u));
    for (size_t i = 0u; i < 8u; ++i) wanted[4u + i] = (unsigned char)(frames >> (i * 8u));
    for (size_t i = 0u; i < 4u; ++i) wanted[12u + i] = (unsigned char)(channels >> (i * 8u));
    for (size_t at = 0u; at + sizeof(wanted) <= size; ++at)
        if (memcmp(data + at, wanted, sizeof(wanted)) == 0) return at;
    return SIZE_MAX;
}

static void check_project_roundtrip(const TsSample *source, const char *wav,
                                    const char *project)
{
    TsInstrument instrument;
    TsInstrument restored;
    char error[256];
    FILE *f;
    char magic[5];
    ts_instrument_init(&instrument);
    ts_instrument_init(&restored);
    assert(ts_sample_save_wav32f(source, wav, error, sizeof(error)));
    assert(ts_instrument_load_wav(&instrument, wav, error, sizeof(error)));
    assert(ts_instrument_save_recipe(&instrument, project, error, sizeof(error)));
    f = fopen(project, "rb");
    assert(f != NULL && fread(magic, 1u, sizeof(magic), f) == sizeof(magic));
    fclose(f);
    assert(memcmp(magic, "TSR27", 5u) == 0);
    assert(ts_instrument_load_recipe(&restored, project, error, sizeof(error)));
    assert(restored.current.channels == source->channels);
    assert(restored.current.frames == source->frames);
    assert(ts_sample_hash(&restored.current) == ts_sample_hash(&instrument.current));
    assert(restored.parent.channels == source->channels);
    assert(restored.bank[0].sample.channels == source->channels);
    ts_instrument_free(&instrument);
    ts_instrument_free(&restored);
}

int main(void)
{
    float mono_data[] = {-0.2f, 0.6f};
    float stereo_data[] = {0.1f, 0.9f, -0.2f, 0.7f, 0.3f, -0.8f};
    TsSample mono = {mono_data, 2u, 23456u, "mono", 1u, 1u};
    TsSample stereo = {stereo_data, 3u, 12345u, "stereo", 1u, 2u};
    TsInstrument loaded;
    unsigned char *project_data;
    size_t project_size;
    size_t sample_header;
    char error[256];

    check_project_roundtrip(&mono, "test-tsr27-mono.wav", "test-tsr27-mono.tsr");
    check_project_roundtrip(&stereo, "test-tsr27-stereo.wav", "test-tsr27-stereo.tsr");

    ts_instrument_init(&loaded);
    write_tsr6("test-tsr6.tsr");
    assert(ts_instrument_load_recipe(&loaded, "test-tsr6.tsr", error, sizeof(error)));
    assert(loaded.parent.channels == 1u && loaded.current.channels == 1u);
    assert(loaded.parent.frames == 3u && loaded.parent.data[1] == 0.25f);
    ts_instrument_free(&loaded);

    project_data = read_file("test-tsr27-stereo.tsr", &project_size);
    sample_header = find_sample_header(project_data, project_size, 12345u, 3u, 2u);
    assert(sample_header != SIZE_MAX);

    project_data[sample_header + 12u] = 3u;
    project_data[sample_header + 13u] = 0u;
    project_data[sample_header + 14u] = 0u;
    project_data[sample_header + 15u] = 0u;
    write_file("test-tsr27-bad-channels.tsr", project_data, project_size);
    ts_instrument_init(&loaded);
    assert(!ts_instrument_load_recipe(&loaded, "test-tsr27-bad-channels.tsr",
                                      error, sizeof(error)));
    ts_instrument_free(&loaded);

    project_data[sample_header + 12u] = 2u;
    memset(project_data + sample_header + 4u, 0xff, 8u);
    write_file("test-tsr27-overflow.tsr", project_data, project_size);
    ts_instrument_init(&loaded);
    assert(!ts_instrument_load_recipe(&loaded, "test-tsr27-overflow.tsr",
                                      error, sizeof(error)));
    ts_instrument_free(&loaded);

    memset(project_data + sample_header + 4u, 0, 8u);
    project_data[sample_header + 4u] = 3u;
    write_file("test-tsr27-truncated.tsr", project_data, project_size - 1u);
    ts_instrument_init(&loaded);
    assert(!ts_instrument_load_recipe(&loaded, "test-tsr27-truncated.tsr",
                                      error, sizeof(error)));
    ts_instrument_free(&loaded);
    free(project_data);

    remove("test-tsr27-mono.wav"); remove("test-tsr27-mono.tsr");
    remove("test-tsr27-stereo.wav"); remove("test-tsr27-stereo.tsr");
    remove("test-tsr6.tsr"); remove("test-tsr27-bad-channels.tsr");
    remove("test-tsr27-overflow.tsr"); remove("test-tsr27-truncated.tsr");
    puts("TSR27 tests passed");
    return 0;
}
