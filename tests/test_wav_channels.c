#include "tapesister/sample.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put16(FILE *f, uint16_t value)
{
    unsigned char p[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
    assert(fwrite(p, 1u, sizeof(p), f) == sizeof(p));
}

static void put32(FILE *f, uint32_t value)
{
    unsigned char p[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                          (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    assert(fwrite(p, 1u, sizeof(p), f) == sizeof(p));
}

static void check_layout(const char *path, uint16_t format, uint16_t channels,
                         uint16_t bits, uint32_t rate, uint32_t frames)
{
    unsigned char header[12];
    unsigned char chunk[8];
    unsigned char fmt[16];
    FILE *f = fopen(path, "rb");
    uint32_t data_size = 0u;
    int saw_fmt = 0;
    assert(f != NULL);
    assert(fread(header, 1u, sizeof(header), f) == sizeof(header));
    assert(memcmp(header, "RIFF", 4u) == 0 && memcmp(header + 8u, "WAVE", 4u) == 0);
    while (fread(chunk, 1u, sizeof(chunk), f) == sizeof(chunk)) {
        uint32_t size = le32(chunk + 4u);
        if (memcmp(chunk, "fmt ", 4u) == 0) {
            assert(size >= sizeof(fmt));
            assert(fread(fmt, 1u, sizeof(fmt), f) == sizeof(fmt));
            assert(le16(fmt) == format);
            assert(le16(fmt + 2u) == channels);
            assert(le32(fmt + 4u) == rate);
            assert(le32(fmt + 8u) == rate * channels * (bits / 8u));
            assert(le16(fmt + 12u) == channels * (bits / 8u));
            assert(le16(fmt + 14u) == bits);
            if (size > sizeof(fmt)) assert(fseek(f, (long)(size - sizeof(fmt)), SEEK_CUR) == 0);
            saw_fmt = 1;
        } else if (memcmp(chunk, "data", 4u) == 0) {
            data_size = size;
            assert(fseek(f, (long)size, SEEK_CUR) == 0);
        } else {
            assert(fseek(f, (long)size, SEEK_CUR) == 0);
        }
        if (size & 1u) assert(fseek(f, 1L, SEEK_CUR) == 0);
    }
    fclose(f);
    assert(saw_fmt);
    assert(data_size == frames * channels * (bits / 8u));
}

static void write_bad_wav(const char *path, uint16_t channels,
                          uint16_t block_align)
{
    FILE *f = fopen(path, "wb");
    uint32_t data_size = block_align;
    unsigned char zero[8] = {0};
    assert(f != NULL);
    assert(fwrite("RIFF", 1u, 4u, f) == 4u); put32(f, 36u + data_size);
    assert(fwrite("WAVEfmt ", 1u, 8u, f) == 8u); put32(f, 16u);
    put16(f, 1u); put16(f, channels); put32(f, 8000u);
    put32(f, 8000u * block_align); put16(f, block_align); put16(f, 16u);
    assert(fwrite("data", 1u, 4u, f) == 4u); put32(f, data_size);
    assert(fwrite(zero, 1u, data_size, f) == data_size);
    fclose(f);
}

static void check_close(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

int main(void)
{
    float mono_values[] = {-0.75f, 0.0f, 0.5f};
    float stereo_values[] = {0.1f, 0.8f, -0.2f, 0.6f, 0.3f, -0.7f};
    float single_values[] = {-0.125f, 0.875f};
    TsSample mono = {mono_values, 3u, 32000u, "mono", 1u, 1u};
    TsSample stereo = {stereo_values, 3u, 44100u, "stereo", 1u, 2u};
    TsSample single = {single_values, 1u, 8000u, "single", 1u, 2u};
    TsSample loaded;
    char error[256];

    ts_sample_init(&loaded);
    assert(ts_sample_save_wav16(&mono, "test-wav-mono16.wav", error, sizeof(error)));
    check_layout("test-wav-mono16.wav", 1u, 1u, 16u, 32000u, 3u);
    assert(ts_sample_load_wav(&loaded, "test-wav-mono16.wav", error, sizeof(error)));
    assert(loaded.channels == 1u && loaded.frames == 3u);
    for (size_t i = 0; i < 3u; ++i) check_close(loaded.data[i], mono_values[i], 0.00005f);
    ts_sample_free(&loaded);

    assert(ts_sample_save_wav16(&stereo, "test-wav-stereo16.wav", error, sizeof(error)));
    check_layout("test-wav-stereo16.wav", 1u, 2u, 16u, 44100u, 3u);
    assert(ts_sample_load_wav(&loaded, "test-wav-stereo16.wav", error, sizeof(error)));
    assert(loaded.channels == 2u && loaded.frames == 3u);
    for (size_t i = 0; i < 6u; ++i) check_close(loaded.data[i], stereo_values[i], 0.00005f);
    ts_sample_free(&loaded);

    assert(ts_sample_save_wav32f(&mono, "test-wav-mono32.wav", error, sizeof(error)));
    check_layout("test-wav-mono32.wav", 3u, 1u, 32u, 32000u, 3u);
    assert(ts_sample_load_wav(&loaded, "test-wav-mono32.wav", error, sizeof(error)));
    assert(loaded.channels == 1u && memcmp(loaded.data, mono_values, sizeof(mono_values)) == 0);
    ts_sample_free(&loaded);

    assert(ts_sample_save_wav32f(&single, "test-wav-stereo32.wav", error, sizeof(error)));
    check_layout("test-wav-stereo32.wav", 3u, 2u, 32u, 8000u, 1u);
    assert(ts_sample_load_wav(&loaded, "test-wav-stereo32.wav", error, sizeof(error)));
    assert(loaded.channels == 2u && loaded.frames == 1u);
    assert(loaded.data[0] == single_values[0] && loaded.data[1] == single_values[1]);
    ts_sample_free(&loaded);

    write_bad_wav("test-wav-3ch.wav", 3u, 6u);
    assert(!ts_sample_load_wav(&loaded, "test-wav-3ch.wav", error, sizeof(error)));
    write_bad_wav("test-wav-bad-align.wav", 2u, 2u);
    assert(!ts_sample_load_wav(&loaded, "test-wav-bad-align.wav", error, sizeof(error)));

    remove("test-wav-mono16.wav");
    remove("test-wav-stereo16.wav");
    remove("test-wav-mono32.wav");
    remove("test-wav-stereo32.wav");
    remove("test-wav-3ch.wav");
    remove("test-wav-bad-align.wav");
    puts("WAV channel tests passed");
    return 0;
}
