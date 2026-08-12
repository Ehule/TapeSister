#include "tapesister/sample.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void set_error(char *dst, size_t size, const char *message)
{
    if (dst != NULL && size > 0) snprintf(dst, size, "%s", message);
}

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
    fputc((int)(value & 255u), f);
    fputc((int)((value >> 8) & 255u), f);
}

static void put32(FILE *f, uint32_t value)
{
    put16(f, (uint16_t)(value & 65535u));
    put16(f, (uint16_t)(value >> 16));
}

static void put64(FILE *f, uint64_t value)
{
    put32(f, (uint32_t)value);
    put32(f, (uint32_t)(value >> 32));
}

static void put_float(FILE *f, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(f, bits);
}

static int get32(FILE *f, uint32_t *value)
{
    unsigned char bytes[4];
    if (fread(bytes, 1, sizeof(bytes), f) != sizeof(bytes)) return 0;
    *value = le32(bytes);
    return 1;
}

static int get64(FILE *f, uint64_t *value)
{
    uint32_t low;
    uint32_t high;
    if (!get32(f, &low) || !get32(f, &high)) return 0;
    *value = (uint64_t)low | ((uint64_t)high << 32);
    return 1;
}

static int get_float(FILE *f, float *value)
{
    uint32_t bits;
    if (!get32(f, &bits)) return 0;
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static size_t clamps(size_t value, size_t high)
{
    return value > high ? high : value;
}

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state == 0 ? 0x6d2b79f5u : *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t advance_seed(uint32_t seed)
{
    return seed * 1664525u + 1013904223u;
}

static float rng_unit(uint32_t *state)
{
    return (float)(rng_next(state) & 0x00ffffffu) / 16777215.0f;
}

static float rng_bipolar(uint32_t *state)
{
    return rng_unit(state) * 2.0f - 1.0f;
}

void ts_sample_init(TsSample *sample)
{
    memset(sample, 0, sizeof(*sample));
}

void ts_sample_free(TsSample *sample)
{
    free(sample->data);
    ts_sample_init(sample);
}

int ts_sample_clone(TsSample *destination, const TsSample *source, char *error, size_t error_size)
{
    float *copy;
    if (source == NULL || source->data == NULL || source->frames == 0) {
        set_error(error, error_size, "No sample to copy");
        return 0;
    }
    if (source->frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Sample is too large to copy");
        return 0;
    }
    copy = (float *)malloc(source->frames * sizeof(float));
    if (copy == NULL) {
        set_error(error, error_size, "Out of memory while copying sample");
        return 0;
    }
    memcpy(copy, source->data, source->frames * sizeof(float));
    ts_sample_free(destination);
    destination->data = copy;
    destination->frames = source->frames;
    destination->sample_rate = source->sample_rate;
    snprintf(destination->name, sizeof(destination->name), "%s", source->name);
    set_error(error, error_size, "");
    return 1;
}

static float decode_pcm(const unsigned char *p, uint16_t format, uint16_t bits)
{
    if (format == 3 && bits == 32) {
        float value;
        memcpy(&value, p, sizeof(value));
        return isfinite(value) ? clampf(value, -1.0f, 1.0f) : 0.0f;
    }
    if (format != 1) return 0.0f;
    if (bits == 8) return ((float)p[0] - 128.0f) / 128.0f;
    if (bits == 16) return (float)(int16_t)le16(p) / 32768.0f;
    if (bits == 24) {
        int32_t value = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
        if (value & 0x00800000) value |= (int32_t)0xff000000;
        return (float)value / 8388608.0f;
    }
    if (bits == 32) return (float)(int32_t)le32(p) / 2147483648.0f;
    return 0.0f;
}

int ts_sample_load_wav(TsSample *sample, const char *path, char *error, size_t error_size)
{
    FILE *f = fopen(path, "rb");
    unsigned char header[12];
    unsigned char fmt[40];
    uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
    uint32_t rate = 0, data_size = 0;
    long data_offset = -1;
    float *decoded = NULL;

    if (f == NULL) {
        char message[256];
        snprintf(message, sizeof(message), "Could not open WAV: %s", strerror(errno));
        set_error(error, error_size, message);
        return 0;
    }
    if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        set_error(error, error_size, "Not a RIFF/WAVE file");
        fclose(f);
        return 0;
    }

    while (!feof(f)) {
        unsigned char chunk[8];
        uint32_t size;
        if (fread(chunk, 1, 8, f) != 8) break;
        size = le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            size_t keep = size < sizeof(fmt) ? size : sizeof(fmt);
            if (keep < 16 || fread(fmt, 1, keep, f) != keep) break;
            format = le16(fmt);
            channels = le16(fmt + 2);
            rate = le32(fmt + 4);
            block_align = le16(fmt + 12);
            bits = le16(fmt + 14);
            if (size > keep) fseek(f, (long)(size - keep), SEEK_CUR);
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_offset = ftell(f);
            data_size = size;
            fseek(f, (long)size, SEEK_CUR);
        } else {
            fseek(f, (long)size, SEEK_CUR);
        }
        if (size & 1u) fseek(f, 1, SEEK_CUR);
    }

    if ((format != 1 && format != 3) || channels == 0 || channels > 32 || rate < 1000 ||
        block_align == 0 || data_offset < 0 || data_size < block_align ||
        !((format == 3 && bits == 32) ||
          (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)))) {
        set_error(error, error_size, "Unsupported or incomplete WAV (PCM/float required)");
        fclose(f);
        return 0;
    }

    size_t frames = data_size / block_align;
    if (frames > 100000000u || frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "WAV is too large");
        fclose(f);
        return 0;
    }
    decoded = (float *)malloc(frames * sizeof(float));
    if (decoded == NULL) {
        set_error(error, error_size, "Out of memory while loading WAV");
        fclose(f);
        return 0;
    }
    if (fseek(f, data_offset, SEEK_SET) != 0) {
        set_error(error, error_size, "Could not seek to WAV audio");
        free(decoded);
        fclose(f);
        return 0;
    }

    const size_t bytes = bits / 8u;
    unsigned char frame[128];
    if (block_align > sizeof(frame)) {
        set_error(error, error_size, "WAV frame layout is too large");
        free(decoded);
        fclose(f);
        return 0;
    }
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        if (fread(frame, 1, block_align, f) != block_align) {
            set_error(error, error_size, "WAV ended before its declared data size");
            free(decoded);
            fclose(f);
            return 0;
        }
        for (uint16_t ch = 0; ch < channels; ++ch)
            sum += decode_pcm(frame + ch * bytes, format, bits);
        decoded[i] = clampf(sum / (float)channels, -1.0f, 1.0f);
    }
    fclose(f);

    ts_sample_free(sample);
    sample->data = decoded;
    sample->frames = frames;
    sample->sample_rate = rate;
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = slash && (!backslash || slash > backslash) ? slash + 1 :
                       backslash ? backslash + 1 : path;
    snprintf(sample->name, sizeof(sample->name), "%s", base);
    set_error(error, error_size, "");
    return 1;
}

int ts_sample_save_wav16(const TsSample *sample, const char *path, char *error, size_t error_size)
{
    if (sample == NULL || sample->data == NULL || sample->frames == 0 || sample->sample_rate == 0) {
        set_error(error, error_size, "No sample to export");
        return 0;
    }
    if (sample->frames > (UINT32_MAX - 36u) / 2u) {
        set_error(error, error_size, "Sample is too long for a RIFF WAV");
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        set_error(error, error_size, "Could not create WAV");
        return 0;
    }
    uint32_t data_bytes = (uint32_t)(sample->frames * 2u);
    fwrite("RIFF", 1, 4, f); put32(f, 36u + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, sample->sample_rate); put32(f, sample->sample_rate * 2u); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data_bytes);
    for (size_t i = 0; i < sample->frames; ++i) {
        float value = clampf(sample->data[i], -1.0f, 1.0f);
        int32_t quantized = (int32_t)lrintf(value * 32767.0f);
        put16(f, (uint16_t)(int16_t)quantized);
    }
    if (fclose(f) != 0) {
        set_error(error, error_size, "Could not finish WAV export");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

const char *ts_generator_name(TsGeneratorKind kind)
{
    static const char *names[] = {"TONAL", "METALLIC", "NOISE", "PULSE"};
    return kind >= 0 && kind < TS_GENERATOR_COUNT ? names[kind] : "UNKNOWN";
}

const char *ts_noise_color_name(TsNoiseColor color)
{
    static const char *names[] = {"WHITE", "PINK", "BROWN", "METALLIC"};
    return color >= 0 && color < TS_NOISE_COLOR_COUNT ? names[color] : "UNKNOWN";
}

const char *ts_sample_edit_name(TsSampleEditKind kind)
{
    static const char *names[] = {"REVERSE", "NORMALIZE", "GAIN", "FADE_IN", "FADE_OUT"};
    return kind >= TS_SAMPLE_EDIT_REVERSE && kind <= TS_SAMPLE_EDIT_FADE_OUT ?
           names[kind] : "UNKNOWN";
}

int ts_sample_generate(TsSample *sample, const TsGeneratorRecipe *recipe,
                       char *error, size_t error_size)
{
    const uint32_t rate = 44100;
    float seconds = clampf(recipe->seconds, 0.1f, 8.0f);
    size_t frames = (size_t)(seconds * (float)rate);
    float *data = (float *)malloc(frames * sizeof(float));
    uint32_t rng = recipe->seed;
    float frequency = clampf(recipe->frequency, 30.0f, 2000.0f);
    float phase = 0.0f, mod_phase = 0.0f, noise_lp = 0.0f, noise_slow = 0.0f;
    float seed_a = rng_unit(&rng);
    float seed_b = rng_unit(&rng);
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while generating sample");
        return 0;
    }
    for (size_t i = 0; i < frames; ++i) {
        float t = (float)i / (float)rate;
        float attack = fminf(1.0f, t * (80.0f + seed_a * 180.0f));
        float tail = fminf(1.0f, (seconds - t) * 24.0f);
        float decay = expf(-t * (1.2f + seed_b * 3.8f));
        float random = rng_bipolar(&rng);
        float value;
        noise_lp += (random - noise_lp) * (0.015f + seed_a * 0.08f);
        noise_slow += (random - noise_slow) * 0.00015f;
        switch (recipe->kind) {
        case TS_GENERATOR_METALLIC: {
            float ratio = 2.37f + seed_a * 4.1f;
            float sweep = 1.0f + expf(-t * 7.0f) * (1.5f + seed_b * 4.0f);
            mod_phase += (float)(2.0 * M_PI) * frequency * ratio / (float)rate;
            phase += (float)(2.0 * M_PI) * frequency * sweep / (float)rate;
            value = sinf(phase + sinf(mod_phase) * (2.0f + seed_b * 7.0f));
            value += sinf(phase * (1.413f + seed_a * 0.11f)) * 0.34f;
            value *= attack * tail * decay;
            break;
        }
        case TS_GENERATOR_NOISE: {
            float resonant = sinf((float)(2.0 * M_PI) * frequency * t + noise_slow * 8.0f);
            float burst = random * 0.55f + noise_lp * 1.1f + resonant * 0.32f;
            value = burst * attack * tail * expf(-t * (2.2f + seed_a * 5.0f));
            break;
        }
        case TS_GENERATOR_PULSE: {
            float sweep_hz = frequency * (1.0f + expf(-t * 9.0f) * (0.5f + seed_b * 3.0f));
            phase += (float)(2.0 * M_PI) * sweep_hz / (float)rate;
            if (phase > (float)(2.0 * M_PI)) phase -= (float)(2.0 * M_PI);
            value = (phase < (0.35f + seed_a * 0.3f) * (float)(2.0 * M_PI) ? 0.72f : -0.72f);
            value += noise_lp * 0.22f;
            value *= attack * tail * decay;
            break;
        }
        case TS_GENERATOR_TONAL:
        default: {
            float pitch_drop = 1.0f + expf(-t * 14.0f) * (0.04f + seed_a * 0.3f);
            phase += (float)(2.0 * M_PI) * frequency * pitch_drop / (float)rate;
            value = sinf(phase) + sinf(phase * 0.5f) * (0.12f + seed_a * 0.28f);
            value += sinf(phase * 2.0f + seed_b) * (0.08f + seed_b * 0.16f);
            value += noise_lp * 0.06f;
            value *= attack * tail * (0.35f + decay * 0.65f);
            break;
        }
        }
        data[i] = tanhf(value * 1.15f) * 0.78f;
    }
    ts_sample_free(sample);
    sample->data = data;
    sample->frames = frames;
    sample->sample_rate = rate;
    snprintf(sample->name, sizeof(sample->name), "%s %08X", ts_generator_name(recipe->kind), recipe->seed);
    set_error(error, error_size, "");
    return 1;
}

static float sample_linear(const TsSample *sample, double position, size_t first, size_t last)
{
    if (position < (double)first) position = (double)first;
    if (position > (double)(last - 1)) position = (double)(last - 1);
    size_t at = (size_t)position;
    size_t next = at + 1 < last ? at + 1 : at;
    float fraction = (float)(position - (double)at);
    return sample->data[at] + (sample->data[next] - sample->data[at]) * fraction;
}

int ts_sample_process(TsSample *sample, const TsSample *parent, size_t first, size_t last,
                      const TsProcessRecipe *recipe, char *error, size_t error_size)
{
    float *data;
    float *delay = NULL;
    float *comb[3] = {NULL, NULL, NULL};
    float *allpass = NULL;
    const float comb_ms[3] = {29.7f, 37.1f, 41.1f};
    size_t comb_len[3] = {0, 0, 0};
    size_t comb_at[3] = {0, 0, 0};
    size_t delay_len = 0, delay_at = 0, allpass_len = 0, allpass_at = 0;
    float low = 0.0f, fast = 0.0f, wander = 0.0f;
    float pink = 0.0f, brown = 0.0f, delay_lp = 0.0f;
    float comb_lp[3] = {0.0f, 0.0f, 0.0f};
    float amplitude = 0.0f;
    float body = clampf(recipe->body, 0.0f, 1.0f);
    float edge = clampf(recipe->edge, 0.0f, 1.0f);
    float drift = clampf(recipe->drift, 0.0f, 1.0f);
    float noise_amount = clampf(recipe->noise_amount, 0.0f, 1.0f);
    float delay_feedback = clampf(recipe->delay_feedback, 0.0f, 0.85f);
    float delay_damping = clampf(recipe->delay_damping, 0.0f, 1.0f);
    float delay_mix = clampf(recipe->delay_mix, 0.0f, 1.0f);
    float reverb_decay = clampf(recipe->reverb_decay, 0.0f, 0.9f);
    float reverb_damping = clampf(recipe->reverb_damping, 0.0f, 1.0f);
    float reverb_mix = clampf(recipe->reverb_mix, 0.0f, 1.0f);
    uint32_t rng = recipe->seed;
    double phase = rng_unit(&rng) * 2.0 * M_PI;
    double drift_rate = 0.15 + rng_unit(&rng) * 0.9;
    if (parent == NULL || parent->data == NULL || parent->frames == 0 || first >= last || last > parent->frames) {
        set_error(error, error_size, "Invalid Parent range");
        return 0;
    }
    size_t frames = last - first;
    if (frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Current sample is too large");
        return 0;
    }
    data = (float *)malloc(frames * sizeof(float));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while rendering Current");
        return 0;
    }

    if (recipe->delay_enabled && delay_mix > 0.0f) {
        double wanted = clampf(recipe->delay_seconds, 0.005f, 1.0f) * parent->sample_rate;
        delay_len = (size_t)(wanted + 0.5);
        if (delay_len < 1) delay_len = 1;
        delay = (float *)calloc(delay_len, sizeof(float));
        if (delay == NULL) goto out_of_memory;
    }
    if (recipe->reverb_enabled && reverb_mix > 0.0f) {
        for (int line_index = 0; line_index < 3; ++line_index) {
            comb_len[line_index] = (size_t)(comb_ms[line_index] * 0.001f * parent->sample_rate) + 1u;
            comb[line_index] = (float *)calloc(comb_len[line_index], sizeof(float));
            if (comb[line_index] == NULL) goto out_of_memory;
        }
        allpass_len = (size_t)(0.005f * parent->sample_rate) + 1u;
        allpass = (float *)calloc(allpass_len, sizeof(float));
        if (allpass == NULL) goto out_of_memory;
    }

    for (size_t i = 0; i < frames; ++i) {
        float random = rng_bipolar(&rng);
        wander += (random - wander) * 0.00008f;
        double lfo = sin(phase + (double)i * (2.0 * M_PI * drift_rate) /
                         (double)parent->sample_rate);
        double offset = drift * (4.0 + drift * 120.0) * (lfo + wander * 0.7);
        float input = sample_linear(parent, (double)(first + i) + offset, first, last);
        low += (input - low) * 0.018f;
        fast += (input - fast) * 0.22f;
        float shaped = input + (body - 0.5f) * 1.5f * low;
        shaped += edge * 1.45f * (input - fast);
        if (edge > 0.001f) {
            float drive = 1.0f + edge * 3.5f;
            shaped = tanhf(shaped * drive) / tanhf(drive);
        }
        shaped += random * drift * 0.006f;

        if (recipe->noise_enabled && noise_amount > 0.0f) {
            float colored = random;
            pink += (random - pink) * 0.075f;
            brown = clampf(brown + random * 0.035f, -1.0f, 1.0f);
            if (recipe->noise_color == TS_NOISE_PINK) colored = pink * 2.6f;
            else if (recipe->noise_color == TS_NOISE_BROWN) colored = brown;
            else if (recipe->noise_color == TS_NOISE_METALLIC)
                colored = random * ((rng_next(&rng) & 31u) < 7u ? 1.0f : -0.28f);
            amplitude += (fabsf(shaped) - amplitude) *
                         (fabsf(shaped) > amplitude ? 0.08f : 0.0015f);
            shaped += colored * noise_amount * (0.04f + amplitude * 0.42f);
        }

        if (delay != NULL) {
            float delayed = delay[delay_at];
            float damp_rate = 0.02f + (1.0f - delay_damping) * 0.78f;
            delay_lp += (delayed - delay_lp) * damp_rate;
            delay[delay_at] = clampf(shaped + delay_lp * delay_feedback, -2.0f, 2.0f);
            delay_at = (delay_at + 1u) % delay_len;
            shaped = shaped * (1.0f - delay_mix) + delayed * delay_mix;
        }

        if (allpass != NULL) {
            float wet = 0.0f;
            for (int line_index = 0; line_index < 3; ++line_index) {
                float delayed = comb[line_index][comb_at[line_index]];
                float damp_rate = 0.02f + (1.0f - reverb_damping) * 0.72f;
                comb_lp[line_index] += (delayed - comb_lp[line_index]) * damp_rate;
                comb[line_index][comb_at[line_index]] =
                    clampf(shaped + comb_lp[line_index] * reverb_decay, -2.0f, 2.0f);
                comb_at[line_index] = (comb_at[line_index] + 1u) % comb_len[line_index];
                wet += delayed;
            }
            wet /= 3.0f;
            {
                const float stored = allpass[allpass_at];
                const float allpass_out = stored - wet * 0.5f;
                allpass[allpass_at] = clampf(wet + stored * 0.5f, -2.0f, 2.0f);
                allpass_at = (allpass_at + 1u) % allpass_len;
                shaped = shaped * (1.0f - reverb_mix) + allpass_out * reverb_mix;
            }
        }
        data[i] = clampf(shaped, -1.0f, 1.0f);
    }

    free(delay);
    for (int line_index = 0; line_index < 3; ++line_index) free(comb[line_index]);
    free(allpass);
    ts_sample_free(sample);
    sample->data = data;
    sample->frames = frames;
    sample->sample_rate = parent->sample_rate;
    snprintf(sample->name, sizeof(sample->name), "%s", parent->name);
    set_error(error, error_size, "");
    return 1;

out_of_memory:
    free(data);
    free(delay);
    for (int line_index = 0; line_index < 3; ++line_index) free(comb[line_index]);
    free(allpass);
    set_error(error, error_size, "Out of memory while creating DSP delay lines");
    return 0;
}

float ts_sample_peak(const TsSample *sample)
{
    float peak = 0.0f;
    if (sample == NULL || sample->data == NULL) return 0.0f;
    for (size_t i = 0; i < sample->frames; ++i) {
        float value = fabsf(sample->data[i]);
        if (value > peak) peak = value;
    }
    return peak;
}

uint64_t ts_sample_hash(const TsSample *sample)
{
    uint64_t hash = 1469598103934665603ull;
    if (sample == NULL) return hash;
    hash ^= sample->sample_rate; hash *= 1099511628211ull;
    hash ^= sample->frames; hash *= 1099511628211ull;
    for (size_t i = 0; i < sample->frames; ++i) {
        int32_t q = (int32_t)lrintf(clampf(sample->data[i], -1.0f, 1.0f) * 8388607.0f);
        hash ^= (uint32_t)q;
        hash *= 1099511628211ull;
    }
    return hash;
}

static TsEditSnapshot snapshot(const TsInstrument *instrument)
{
    TsEditSnapshot result;
    result.crop_first = instrument->crop_first;
    result.crop_last = instrument->crop_last;
    result.selection_first = instrument->selection_first;
    result.selection_last = instrument->selection_last;
    result.view_first = instrument->view_first;
    result.view_last = instrument->view_last;
    result.loop_first = instrument->loop_first;
    result.loop_last = instrument->loop_last;
    result.loop_crossfade_ms = instrument->loop_crossfade_ms;
    result.has_selection = instrument->has_selection;
    result.has_loop = instrument->has_loop;
    result.process = instrument->process;
    memcpy(result.sample_edits, instrument->sample_edits, sizeof(result.sample_edits));
    result.sample_edit_count = instrument->sample_edit_count;
    return result;
}

static void stack_push(TsEditSnapshot *stack, int *count, TsEditSnapshot value)
{
    if (*count == TS_HISTORY_DEPTH) {
        memmove(stack, stack + 1, (TS_HISTORY_DEPTH - 1) * sizeof(*stack));
        --*count;
    }
    stack[(*count)++] = value;
}

static void begin_edit(TsInstrument *instrument)
{
    stack_push(instrument->undo, &instrument->undo_count, snapshot(instrument));
    instrument->redo_count = 0;
}

static void reset_editor(TsInstrument *instrument)
{
    instrument->crop_first = 0;
    instrument->crop_last = instrument->parent.frames;
    instrument->selection_first = 0;
    instrument->selection_last = 0;
    instrument->has_selection = 0;
    instrument->view_first = 0;
    instrument->view_last = instrument->parent.frames;
    instrument->loop_first = 0;
    instrument->loop_last = 0;
    instrument->loop_crossfade_ms = 8.0f;
    instrument->has_loop = 0;
    instrument->sample_edit_count = 0;
    instrument->undo_count = 0;
    instrument->redo_count = 0;
}

static int render_edit_source(TsSample *destination, const TsSample *parent,
                              size_t first, size_t last,
                              const TsSampleEdit *edits, int edit_count,
                              char *error, size_t error_size)
{
    float *data;
    size_t frames;
    if (parent == NULL || parent->data == NULL || first >= last || last > parent->frames) {
        set_error(error, error_size, "Invalid Parent edit range");
        return 0;
    }
    frames = last - first;
    if (frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Edited sample is too large");
        return 0;
    }
    data = (float *)malloc(frames * sizeof(float));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while applying sample edits");
        return 0;
    }
    memcpy(data, parent->data + first, frames * sizeof(float));
    for (int edit_index = 0; edit_index < edit_count; ++edit_index) {
        const TsSampleEdit *edit = &edits[edit_index];
        size_t edit_first = clamps(edit->first, frames);
        size_t edit_last = clamps(edit->last, frames);
        size_t length;
        if (edit_last <= edit_first) continue;
        length = edit_last - edit_first;
        if (edit->kind == TS_SAMPLE_EDIT_REVERSE) {
            for (size_t i = 0; i < length / 2; ++i) {
                float swap = data[edit_first + i];
                data[edit_first + i] = data[edit_last - 1u - i];
                data[edit_last - 1u - i] = swap;
            }
        } else if (edit->kind == TS_SAMPLE_EDIT_NORMALIZE) {
            float peak = 0.0f;
            float target = clampf(edit->amount, 0.0f, 1.0f);
            for (size_t i = edit_first; i < edit_last; ++i)
                if (fabsf(data[i]) > peak) peak = fabsf(data[i]);
            if (peak > 0.0000001f) {
                float gain = target / peak;
                for (size_t i = edit_first; i < edit_last; ++i)
                    data[i] = clampf(data[i] * gain, -1.0f, 1.0f);
            }
        } else if (edit->kind == TS_SAMPLE_EDIT_GAIN) {
            for (size_t i = edit_first; i < edit_last; ++i)
                data[i] = clampf(data[i] * edit->amount, -1.0f, 1.0f);
        } else if (edit->kind == TS_SAMPLE_EDIT_FADE_IN) {
            for (size_t i = 0; i < length; ++i) {
                float gain = length > 1 ? (float)i / (float)(length - 1u) : 1.0f;
                data[edit_first + i] *= gain;
            }
        } else if (edit->kind == TS_SAMPLE_EDIT_FADE_OUT) {
            for (size_t i = 0; i < length; ++i) {
                float gain = length > 1 ? (float)(length - 1u - i) /
                                          (float)(length - 1u) : 1.0f;
                data[edit_first + i] *= gain;
            }
        }
    }
    ts_sample_free(destination);
    destination->data = data;
    destination->frames = frames;
    destination->sample_rate = parent->sample_rate;
    snprintf(destination->name, sizeof(destination->name), "%s", parent->name);
    return 1;
}

static int render_snapshot(TsSample *destination, const TsInstrument *instrument,
                           const TsEditSnapshot *state, char *error, size_t error_size)
{
    TsSample edited;
    int ok;
    ts_sample_init(&edited);
    if (!render_edit_source(&edited, &instrument->parent, state->crop_first,
                            state->crop_last, state->sample_edits,
                            state->sample_edit_count, error, error_size)) return 0;
    ok = ts_sample_process(destination, &edited, 0, edited.frames,
                           &state->process, error, error_size);
    ts_sample_free(&edited);
    return ok;
}

void ts_process_recipe_reset(TsProcessRecipe *process)
{
    process->seed = 0x53495354u;
    process->body = 0.5f;
    process->edge = 0.0f;
    process->drift = 0.0f;
    process->noise_enabled = 0;
    process->noise_amount = 0.16f;
    process->noise_color = TS_NOISE_PINK;
    process->delay_enabled = 0;
    process->delay_seconds = 0.18f;
    process->delay_feedback = 0.38f;
    process->delay_damping = 0.45f;
    process->delay_mix = 0.25f;
    process->reverb_enabled = 0;
    process->reverb_decay = 0.62f;
    process->reverb_damping = 0.50f;
    process->reverb_mix = 0.24f;
}

void ts_instrument_init(TsInstrument *instrument)
{
    memset(instrument, 0, sizeof(*instrument));
    ts_sample_init(&instrument->parent);
    ts_sample_init(&instrument->current);
    instrument->generator.seed = 0x54415045u;
    instrument->generator.kind = TS_GENERATOR_TONAL;
    instrument->generator.seconds = 2.0f;
    instrument->generator.frequency = 130.8128f;
    ts_process_recipe_reset(&instrument->process);
}

void ts_instrument_free(TsInstrument *instrument)
{
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    memset(instrument, 0, sizeof(*instrument));
}

int ts_instrument_generate(TsInstrument *instrument, TsGeneratorKind kind, uint32_t seed,
                           char *error, size_t error_size)
{
    TsSample parent, current;
    TsGeneratorRecipe generator = instrument->generator;
    TsProcessRecipe neutral;
    ts_sample_init(&parent);
    ts_sample_init(&current);
    generator.kind = kind;
    generator.seed = seed;
    ts_process_recipe_reset(&neutral);
    if (!ts_sample_generate(&parent, &generator, error, error_size) ||
        !ts_sample_process(&current, &parent, 0, parent.frames, &neutral,
                           error, error_size)) {
        ts_sample_free(&parent);
        ts_sample_free(&current);
        return 0;
    }
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    instrument->parent = parent;
    instrument->current = current;
    instrument->source_kind = TS_SOURCE_GENERATED;
    instrument->generator = generator;
    instrument->process = neutral;
    instrument->generation = 0;
    instrument->ancestor_hash = 0;
    reset_editor(instrument);
    return 1;
}

int ts_instrument_load_wav(TsInstrument *instrument, const char *path,
                           char *error, size_t error_size)
{
    TsSample parent, current;
    TsProcessRecipe neutral;
    ts_sample_init(&parent);
    ts_sample_init(&current);
    ts_process_recipe_reset(&neutral);
    if (!ts_sample_load_wav(&parent, path, error, error_size) ||
        !ts_sample_process(&current, &parent, 0, parent.frames, &neutral,
                           error, error_size)) {
        ts_sample_free(&parent);
        ts_sample_free(&current);
        return 0;
    }
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    instrument->parent = parent;
    instrument->current = current;
    instrument->source_kind = TS_SOURCE_IMPORTED;
    instrument->process = neutral;
    instrument->generation = 0;
    instrument->ancestor_hash = 0;
    reset_editor(instrument);
    return 1;
}

int ts_instrument_reseed(TsInstrument *instrument, char *error, size_t error_size)
{
    if (instrument->source_kind == TS_SOURCE_GENERATED) {
        return ts_instrument_generate(instrument, instrument->generator.kind,
                                      advance_seed(instrument->generator.seed), error, error_size);
    }
    if (instrument->source_kind == TS_SOURCE_IMPORTED ||
        instrument->source_kind == TS_SOURCE_COMMITTED) {
        TsProcessRecipe process = instrument->process;
        process.seed = advance_seed(process.seed);
        return ts_instrument_set_process(instrument, &process, error, error_size);
    }
    set_error(error, error_size, "No Parent to reseed");
    return 0;
}

int ts_instrument_set_process(TsInstrument *instrument, const TsProcessRecipe *process,
                              char *error, size_t error_size)
{
    TsSample current;
    TsEditSnapshot target = snapshot(instrument);
    ts_sample_init(&current);
    target.process = *process;
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->process = *process;
    return 1;
}

int ts_instrument_reset_current(TsInstrument *instrument, char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsSample current;
    if (instrument == NULL || instrument->parent.data == NULL) {
        set_error(error, error_size, "No Parent to reset to");
        return 0;
    }
    target = snapshot(instrument);
    target.crop_first = 0;
    target.crop_last = instrument->parent.frames;
    target.selection_first = 0;
    target.selection_last = 0;
    target.view_first = 0;
    target.view_last = instrument->parent.frames;
    target.loop_first = 0;
    target.loop_last = 0;
    target.loop_crossfade_ms = 8.0f;
    target.has_selection = 0;
    target.has_loop = 0;
    target.sample_edit_count = 0;
    ts_process_recipe_reset(&target.process);
    target.process.seed = instrument->process.seed;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->crop_first = target.crop_first;
    instrument->crop_last = target.crop_last;
    instrument->selection_first = 0;
    instrument->selection_last = 0;
    instrument->view_first = 0;
    instrument->view_last = current.frames;
    instrument->loop_first = 0;
    instrument->loop_last = 0;
    instrument->loop_crossfade_ms = target.loop_crossfade_ms;
    instrument->has_selection = 0;
    instrument->has_loop = 0;
    instrument->process = target.process;
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    return 1;
}

int ts_instrument_commit_current(TsInstrument *instrument, char *error, size_t error_size)
{
    TsSample parent, current;
    TsProcessRecipe neutral;
    uint64_t prior_hash;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    int has_loop;
    if (instrument == NULL || instrument->current.data == NULL || instrument->current.frames == 0) {
        set_error(error, error_size, "No Current to commit");
        return 0;
    }
    ts_sample_init(&parent);
    ts_sample_init(&current);
    loop_first = instrument->loop_first;
    loop_last = instrument->loop_last;
    loop_crossfade_ms = instrument->loop_crossfade_ms;
    has_loop = instrument->has_loop;
    prior_hash = ts_sample_hash(&instrument->parent);
    if (!ts_sample_clone(&parent, &instrument->current, error, error_size)) return 0;
    snprintf(parent.name, sizeof(parent.name), "G%02u %.116s", instrument->generation + 1u,
             instrument->parent.name);
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument->process.seed;
    if (!ts_sample_process(&current, &parent, 0, parent.frames, &neutral, error, error_size)) {
        ts_sample_free(&parent);
        return 0;
    }
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    instrument->parent = parent;
    instrument->current = current;
    instrument->source_kind = TS_SOURCE_COMMITTED;
    instrument->process = neutral;
    instrument->ancestor_hash = prior_hash;
    ++instrument->generation;
    reset_editor(instrument);
    instrument->loop_first = loop_first;
    instrument->loop_last = loop_last;
    instrument->loop_crossfade_ms = loop_crossfade_ms;
    instrument->has_loop = has_loop && loop_first < loop_last && loop_last <= current.frames;
    set_error(error, error_size, "");
    return 1;
}

void ts_instrument_set_selection(TsInstrument *instrument, size_t first, size_t last)
{
    size_t frames = instrument->current.frames;
    first = clamps(first, frames);
    last = clamps(last, frames);
    if (first > last) {
        size_t swap = first;
        first = last;
        last = swap;
    }
    if (first == last && last < frames) ++last;
    instrument->selection_first = first;
    instrument->selection_last = last;
    instrument->has_selection = first < last;
}

static int is_zero_crossing(const TsSample *sample, size_t frame)
{
    float before;
    float after;
    if (sample == NULL || sample->data == NULL || frame >= sample->frames) return 0;
    after = sample->data[frame];
    if (after == 0.0f) return 1;
    if (frame == 0) return 0;
    before = sample->data[frame - 1u];
    return before == 0.0f || (before < 0.0f && after > 0.0f) ||
           (before > 0.0f && after < 0.0f);
}

size_t ts_sample_nearest_zero_crossing(const TsSample *sample, size_t frame)
{
    size_t target;
    size_t maximum_distance;
    size_t closest = 0;
    float closest_level;
    if (sample == NULL || sample->data == NULL || sample->frames == 0) return 0;
    target = frame >= sample->frames ? sample->frames - 1u : frame;
    maximum_distance = target > sample->frames - 1u - target ?
                       target : sample->frames - 1u - target;
    for (size_t distance = 0; distance <= maximum_distance; ++distance) {
        if (distance <= target) {
            size_t left = target - distance;
            if (is_zero_crossing(sample, left)) return left;
        }
        if (distance > 0 && target + distance < sample->frames &&
            is_zero_crossing(sample, target + distance)) return target + distance;
    }
    closest_level = fabsf(sample->data[0]);
    for (size_t i = 1; i < sample->frames; ++i) {
        float level = fabsf(sample->data[i]);
        if (level < closest_level) {
            closest = i;
            closest_level = level;
        }
    }
    return closest;
}

void ts_instrument_set_selection_snapped(TsInstrument *instrument, size_t first, size_t last)
{
    size_t snapped_first;
    size_t snapped_last;
    if (instrument == NULL || instrument->current.data == NULL) return;
    snapped_first = ts_sample_nearest_zero_crossing(&instrument->current, first);
    snapped_last = ts_sample_nearest_zero_crossing(&instrument->current, last);
    if (snapped_first > snapped_last) {
        size_t swap = snapped_first;
        snapped_first = snapped_last;
        snapped_last = swap;
    }
    instrument->selection_first = snapped_first;
    instrument->selection_last = snapped_last;
    instrument->has_selection = snapped_first < snapped_last;
}

void ts_instrument_clear_selection(TsInstrument *instrument)
{
    instrument->selection_first = 0;
    instrument->selection_last = 0;
    instrument->has_selection = 0;
}

int ts_instrument_set_loop_from_selection(TsInstrument *instrument,
                                          char *error, size_t error_size)
{
    size_t first;
    size_t last;
    if (instrument == NULL || !instrument->has_selection) {
        set_error(error, error_size, "Select a range before setting a loop");
        return 0;
    }
    first = ts_sample_nearest_zero_crossing(&instrument->current,
                                            instrument->selection_first);
    last = ts_sample_nearest_zero_crossing(&instrument->current,
                                           instrument->selection_last);
    if (first > last) {
        size_t swap = first;
        first = last;
        last = swap;
    }
    if (last <= first + 1u) {
        set_error(error, error_size, "Selection is too short for a loop");
        return 0;
    }
    begin_edit(instrument);
    instrument->selection_first = first;
    instrument->selection_last = last;
    instrument->has_selection = 1;
    instrument->loop_first = first;
    instrument->loop_last = last;
    instrument->has_loop = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_clear_loop(TsInstrument *instrument, char *error, size_t error_size)
{
    if (instrument == NULL || !instrument->has_loop) {
        set_error(error, error_size, "No loop to clear");
        return 0;
    }
    begin_edit(instrument);
    instrument->loop_first = 0;
    instrument->loop_last = 0;
    instrument->has_loop = 0;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_set_loop_crossfade(TsInstrument *instrument, float milliseconds,
                                     char *error, size_t error_size)
{
    if (instrument == NULL) {
        set_error(error, error_size, "No instrument for loop crossfade");
        return 0;
    }
    milliseconds = clampf(milliseconds, 0.0f, 50.0f);
    if (fabsf(milliseconds - instrument->loop_crossfade_ms) < 0.0001f) {
        set_error(error, error_size, "Loop crossfade unchanged");
        return 0;
    }
    begin_edit(instrument);
    instrument->loop_crossfade_ms = milliseconds;
    set_error(error, error_size, "");
    return 1;
}

void ts_instrument_begin_loop_drag(TsInstrument *instrument)
{
    if (instrument != NULL && instrument->has_loop) begin_edit(instrument);
}

int ts_instrument_move_loop_endpoint(TsInstrument *instrument, int endpoint, size_t frame)
{
    size_t snapped;
    if (instrument == NULL || !instrument->has_loop ||
        (endpoint != 1 && endpoint != 2)) return 0;
    snapped = ts_sample_nearest_zero_crossing(&instrument->current, frame);
    if (endpoint == 1) {
        if (snapped < instrument->loop_last) instrument->loop_first = snapped;
        else if (snapped == instrument->loop_last) return endpoint;
        else {
            instrument->loop_first = instrument->loop_last;
            instrument->loop_last = snapped;
            endpoint = 2;
        }
    } else {
        if (snapped > instrument->loop_first) instrument->loop_last = snapped;
        else if (snapped == instrument->loop_first) return endpoint;
        else {
            instrument->loop_last = instrument->loop_first;
            instrument->loop_first = snapped;
            endpoint = 1;
        }
    }
    return endpoint;
}

int ts_instrument_crop_selection(TsInstrument *instrument, char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsSample current;
    size_t new_first, new_last;
    if (!instrument->has_selection || instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range before cropping");
        return 0;
    }
    new_first = instrument->crop_first + instrument->selection_first;
    new_last = instrument->crop_first + instrument->selection_last;
    target = snapshot(instrument);
    target.crop_first = new_first;
    target.crop_last = new_last;
    target.sample_edit_count = 0;
    for (int i = 0; i < instrument->sample_edit_count; ++i) {
        TsSampleEdit edit = instrument->sample_edits[i];
        size_t kept_first = edit.first > instrument->selection_first ?
                            edit.first : instrument->selection_first;
        size_t kept_last = edit.last < instrument->selection_last ?
                           edit.last : instrument->selection_last;
        if (kept_last <= kept_first) continue;
        edit.first = kept_first - instrument->selection_first;
        edit.last = kept_last - instrument->selection_first;
        target.sample_edits[target.sample_edit_count++] = edit;
    }
    if (instrument->has_loop) {
        size_t kept_first = instrument->loop_first > instrument->selection_first ?
                            instrument->loop_first : instrument->selection_first;
        size_t kept_last = instrument->loop_last < instrument->selection_last ?
                           instrument->loop_last : instrument->selection_last;
        target.has_loop = kept_last > kept_first + 1u;
        target.loop_first = target.has_loop ? kept_first - instrument->selection_first : 0;
        target.loop_last = target.has_loop ? kept_last - instrument->selection_first : 0;
    }
    target.selection_first = 0;
    target.selection_last = 0;
    target.view_first = 0;
    target.view_last = new_last - new_first;
    target.has_selection = 0;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->crop_first = new_first;
    instrument->crop_last = new_last;
    instrument->loop_first = target.loop_first;
    instrument->loop_last = target.loop_last;
    instrument->has_loop = target.has_loop;
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    ts_instrument_clear_selection(instrument);
    ts_instrument_show_all(instrument);
    return 1;
}

int ts_instrument_apply_sample_edit(TsInstrument *instrument, TsSampleEditKind kind,
                                    float amount, char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsSample current;
    TsSampleEdit edit;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current sample to edit");
        return 0;
    }
    if (kind < TS_SAMPLE_EDIT_REVERSE || kind > TS_SAMPLE_EDIT_FADE_OUT) {
        set_error(error, error_size, "Unknown sample edit");
        return 0;
    }
    if (instrument->sample_edit_count >= TS_SAMPLE_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more sample edits");
        return 0;
    }
    target = snapshot(instrument);
    edit.kind = kind;
    edit.first = instrument->has_selection ? instrument->selection_first : 0;
    edit.last = instrument->has_selection ? instrument->selection_last : instrument->current.frames;
    edit.amount = amount;
    target.sample_edits[target.sample_edit_count++] = edit;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_zoom_selection(TsInstrument *instrument)
{
    if (!instrument->has_selection || instrument->selection_last <= instrument->selection_first)
        return 0;
    instrument->view_first = instrument->selection_first;
    instrument->view_last = instrument->selection_last;
    return 1;
}

int ts_instrument_zoom_view(TsInstrument *instrument, size_t anchor_frame,
                            float anchor_ratio, float scale)
{
    size_t frames = instrument->current.frames;
    size_t old_span;
    size_t new_span;
    size_t first;
    if (frames == 0 || instrument->view_last <= instrument->view_first || scale <= 0.0f)
        return 0;
    old_span = instrument->view_last - instrument->view_first;
    new_span = (size_t)lrint((double)old_span * scale);
    if (new_span < 32u) new_span = frames < 32u ? frames : 32u;
    if (new_span > frames) new_span = frames;
    anchor_frame = clamps(anchor_frame, frames);
    anchor_ratio = clampf(anchor_ratio, 0.0f, 1.0f);
    {
        double wanted = (double)anchor_frame - (double)new_span * anchor_ratio;
        first = wanted <= 0.0 ? 0u : (size_t)wanted;
    }
    if (first + new_span > frames) first = frames - new_span;
    if (first == instrument->view_first && first + new_span == instrument->view_last)
        return 0;
    instrument->view_first = first;
    instrument->view_last = first + new_span;
    return 1;
}

int ts_instrument_pan_view(TsInstrument *instrument, ptrdiff_t frames)
{
    size_t total = instrument->current.frames;
    size_t span;
    size_t first;
    if (total == 0 || instrument->view_last <= instrument->view_first) return 0;
    span = instrument->view_last - instrument->view_first;
    if (span >= total || frames == 0) return 0;
    if (frames < 0) {
        size_t amount = (size_t)(-(frames + 1)) + 1u;
        first = amount > instrument->view_first ? 0u : instrument->view_first - amount;
    } else {
        size_t maximum = total - span;
        size_t amount = (size_t)frames;
        first = amount > maximum - instrument->view_first ? maximum :
                instrument->view_first + amount;
    }
    if (first == instrument->view_first) return 0;
    instrument->view_first = first;
    instrument->view_last = first + span;
    return 1;
}

void ts_instrument_show_all(TsInstrument *instrument)
{
    instrument->view_first = 0;
    instrument->view_last = instrument->current.frames;
}

static int restore_history(TsInstrument *instrument, TsEditSnapshot target,
                           TsEditSnapshot *destination, int *destination_count,
                           char *error, size_t error_size)
{
    TsSample current;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    stack_push(destination, destination_count, snapshot(instrument));
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->crop_first = target.crop_first;
    instrument->crop_last = target.crop_last;
    instrument->selection_first = target.selection_first;
    instrument->selection_last = target.selection_last;
    instrument->view_first = target.view_first;
    instrument->view_last = target.view_last;
    instrument->loop_first = target.loop_first;
    instrument->loop_last = target.loop_last;
    instrument->loop_crossfade_ms = target.loop_crossfade_ms;
    instrument->has_selection = target.has_selection;
    instrument->has_loop = target.has_loop;
    instrument->process = target.process;
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    return 1;
}

int ts_instrument_undo(TsInstrument *instrument, char *error, size_t error_size)
{
    TsEditSnapshot target;
    if (instrument->undo_count == 0) {
        set_error(error, error_size, "Nothing to undo");
        return 0;
    }
    target = instrument->undo[instrument->undo_count - 1];
    if (!restore_history(instrument, target, instrument->redo, &instrument->redo_count,
                         error, error_size)) return 0;
    --instrument->undo_count;
    return 1;
}

int ts_instrument_redo(TsInstrument *instrument, char *error, size_t error_size)
{
    TsEditSnapshot target;
    if (instrument->redo_count == 0) {
        set_error(error, error_size, "Nothing to redo");
        return 0;
    }
    target = instrument->redo[instrument->redo_count - 1];
    if (!restore_history(instrument, target, instrument->undo, &instrument->undo_count,
                         error, error_size)) return 0;
    --instrument->redo_count;
    return 1;
}

size_t ts_instrument_frame_from_view_x(const TsInstrument *instrument, int x, int width)
{
    size_t first = instrument->view_first;
    size_t last = instrument->view_last;
    if (width <= 0 || last <= first) return first;
    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    return first + (size_t)x * (last - first) / (size_t)width;
}

int ts_instrument_save_recipe(const TsInstrument *instrument, const char *path,
                              char *error, size_t error_size)
{
    FILE *f;
    size_t name_length;
    if (instrument == NULL || instrument->parent.data == NULL) {
        set_error(error, error_size, "No instrument recipe to save");
        return 0;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        set_error(error, error_size, "Could not create recipe file");
        return 0;
    }
    fwrite("TSR6\r\n\032\n", 1, 8, f);
    put32(f, (uint32_t)instrument->source_kind);
    put32(f, instrument->generation);
    put64(f, instrument->ancestor_hash);
    put32(f, instrument->generator.seed);
    put32(f, (uint32_t)instrument->generator.kind);
    put_float(f, instrument->generator.seconds);
    put_float(f, instrument->generator.frequency);
    put32(f, instrument->process.seed);
    put_float(f, instrument->process.body);
    put_float(f, instrument->process.edge);
    put_float(f, instrument->process.drift);
    put32(f, (uint32_t)instrument->process.noise_enabled);
    put_float(f, instrument->process.noise_amount);
    put32(f, (uint32_t)instrument->process.noise_color);
    put32(f, (uint32_t)instrument->process.delay_enabled);
    put_float(f, instrument->process.delay_seconds);
    put_float(f, instrument->process.delay_feedback);
    put_float(f, instrument->process.delay_damping);
    put_float(f, instrument->process.delay_mix);
    put32(f, (uint32_t)instrument->process.reverb_enabled);
    put_float(f, instrument->process.reverb_decay);
    put_float(f, instrument->process.reverb_damping);
    put_float(f, instrument->process.reverb_mix);
    put64(f, instrument->crop_first); put64(f, instrument->crop_last);
    put64(f, instrument->selection_first); put64(f, instrument->selection_last);
    put64(f, instrument->view_first); put64(f, instrument->view_last);
    put64(f, instrument->loop_first); put64(f, instrument->loop_last);
    put_float(f, instrument->loop_crossfade_ms);
    put32(f, (uint32_t)instrument->has_selection);
    put32(f, (uint32_t)instrument->has_loop);
    put32(f, (uint32_t)instrument->sample_edit_count);
    for (int i = 0; i < instrument->sample_edit_count; ++i) {
        const TsSampleEdit *edit = &instrument->sample_edits[i];
        put32(f, (uint32_t)edit->kind);
        put64(f, edit->first); put64(f, edit->last);
        put_float(f, edit->amount);
    }
    put32(f, instrument->parent.sample_rate);
    put64(f, instrument->parent.frames);
    name_length = strlen(instrument->parent.name);
    put32(f, (uint32_t)name_length);
    fwrite(instrument->parent.name, 1, name_length, f);
    for (size_t i = 0; i < instrument->parent.frames; ++i)
        put_float(f, instrument->parent.data[i]);
    put64(f, ts_sample_hash(&instrument->parent));
    {
        int failed = ferror(f);
        if (fclose(f) != 0) failed = 1;
        if (failed) {
            set_error(error, error_size, "Could not finish recipe file");
            return 0;
        }
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_load_recipe(TsInstrument *instrument, const char *path,
                              char *error, size_t error_size)
{
    FILE *f;
    char magic[8];
    TsInstrument loaded;
    TsEditSnapshot state;
    uint32_t u32;
    uint64_t u64;
    uint64_t stored_hash;
    uint32_t name_length;
    size_t frames;
    memset(&state, 0, sizeof(state));
    ts_instrument_init(&loaded);
    f = fopen(path, "rb");
    if (f == NULL) {
        set_error(error, error_size, "Could not open TSR project");
        return 0;
    }
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, "TSR6\r\n\032\n", 8) != 0) {
        fclose(f);
        ts_instrument_free(&loaded);
        set_error(error, error_size,
                  "Not a self-contained TSR6 project (older recipes did not embed Parent audio)");
        return 0;
    }
#define GET_U32(dst) do { if (!get32(f, &u32)) goto malformed; (dst) = u32; } while (0)
#define GET_U64(dst) do { if (!get64(f, &u64) || u64 > SIZE_MAX) goto malformed; (dst) = (size_t)u64; } while (0)
#define GET_FLOAT(dst) do { if (!get_float(f, &(dst))) goto malformed; } while (0)
    GET_U32(loaded.source_kind); GET_U32(loaded.generation);
    if (!get64(f, &loaded.ancestor_hash)) goto malformed;
    GET_U32(loaded.generator.seed); GET_U32(loaded.generator.kind);
    GET_FLOAT(loaded.generator.seconds); GET_FLOAT(loaded.generator.frequency);
    GET_U32(state.process.seed);
    GET_FLOAT(state.process.body); GET_FLOAT(state.process.edge); GET_FLOAT(state.process.drift);
    GET_U32(state.process.noise_enabled); GET_FLOAT(state.process.noise_amount);
    GET_U32(state.process.noise_color); GET_U32(state.process.delay_enabled);
    GET_FLOAT(state.process.delay_seconds); GET_FLOAT(state.process.delay_feedback);
    GET_FLOAT(state.process.delay_damping); GET_FLOAT(state.process.delay_mix);
    GET_U32(state.process.reverb_enabled); GET_FLOAT(state.process.reverb_decay);
    GET_FLOAT(state.process.reverb_damping); GET_FLOAT(state.process.reverb_mix);
    GET_U64(state.crop_first); GET_U64(state.crop_last);
    GET_U64(state.selection_first); GET_U64(state.selection_last);
    GET_U64(state.view_first); GET_U64(state.view_last);
    GET_U64(state.loop_first); GET_U64(state.loop_last);
    GET_FLOAT(state.loop_crossfade_ms);
    GET_U32(state.has_selection); GET_U32(state.has_loop);
    GET_U32(state.sample_edit_count);
    if (loaded.source_kind < TS_SOURCE_GENERATED || loaded.source_kind > TS_SOURCE_COMMITTED ||
        loaded.generator.kind >= TS_GENERATOR_COUNT ||
        state.process.noise_color >= TS_NOISE_COLOR_COUNT ||
        state.sample_edit_count < 0 || state.sample_edit_count > TS_SAMPLE_EDIT_DEPTH ||
        (state.has_selection != 0 && state.has_selection != 1) ||
        (state.has_loop != 0 && state.has_loop != 1) ||
        (state.process.noise_enabled != 0 && state.process.noise_enabled != 1) ||
        (state.process.delay_enabled != 0 && state.process.delay_enabled != 1) ||
        (state.process.reverb_enabled != 0 && state.process.reverb_enabled != 1) ||
        !isfinite(state.loop_crossfade_ms) || state.loop_crossfade_ms < 0.0f ||
        state.loop_crossfade_ms > 50.0f)
        goto malformed;
    for (int i = 0; i < state.sample_edit_count; ++i) {
        GET_U32(state.sample_edits[i].kind);
        GET_U64(state.sample_edits[i].first); GET_U64(state.sample_edits[i].last);
        GET_FLOAT(state.sample_edits[i].amount);
        if (state.sample_edits[i].kind > TS_SAMPLE_EDIT_FADE_OUT ||
            state.sample_edits[i].last <= state.sample_edits[i].first ||
            !isfinite(state.sample_edits[i].amount)) goto malformed;
    }
    GET_U32(loaded.parent.sample_rate); GET_U64(frames);
    GET_U32(name_length);
    if (loaded.parent.sample_rate < 1000 || frames == 0 || frames > 100000000u ||
        name_length >= sizeof(loaded.parent.name) || frames > SIZE_MAX / sizeof(float))
        goto malformed;
    if (fread(loaded.parent.name, 1, name_length, f) != name_length) goto malformed;
    loaded.parent.name[name_length] = '\0';
    loaded.parent.data = (float *)malloc(frames * sizeof(float));
    if (loaded.parent.data == NULL) goto out_of_memory;
    loaded.parent.frames = frames;
    for (size_t i = 0; i < frames; ++i)
        if (!get_float(f, &loaded.parent.data[i])) goto malformed;
    if (!get64(f, &stored_hash) || fgetc(f) != EOF ||
        stored_hash != ts_sample_hash(&loaded.parent)) goto malformed;
    if (state.crop_first >= state.crop_last || state.crop_last > loaded.parent.frames ||
        state.selection_first > state.selection_last ||
        state.selection_last > state.crop_last - state.crop_first ||
        state.view_first >= state.view_last ||
        state.view_last > state.crop_last - state.crop_first ||
        state.loop_last > state.crop_last - state.crop_first ||
        (state.has_loop && state.loop_last <= state.loop_first) ||
        (!state.has_loop && (state.loop_first != 0 || state.loop_last != 0))) goto malformed;
    if (!render_snapshot(&loaded.current, &loaded, &state, error, error_size)) goto failed;
    loaded.process = state.process;
    loaded.crop_first = state.crop_first; loaded.crop_last = state.crop_last;
    loaded.selection_first = state.selection_first; loaded.selection_last = state.selection_last;
    loaded.view_first = state.view_first; loaded.view_last = state.view_last;
    loaded.loop_first = state.loop_first; loaded.loop_last = state.loop_last;
    loaded.loop_crossfade_ms = state.loop_crossfade_ms;
    loaded.has_selection = state.has_selection; loaded.has_loop = state.has_loop;
    memcpy(loaded.sample_edits, state.sample_edits, sizeof(loaded.sample_edits));
    loaded.sample_edit_count = state.sample_edit_count;
    fclose(f);
    ts_instrument_free(instrument);
    *instrument = loaded;
    set_error(error, error_size, "");
#undef GET_U32
#undef GET_U64
#undef GET_FLOAT
    return 1;
out_of_memory:
    set_error(error, error_size, "Out of memory while loading TSR project");
    goto failed;
malformed:
    set_error(error, error_size, "Malformed or unsupported TSR6 project");
failed:
    fclose(f);
    ts_instrument_free(&loaded);
#undef GET_U32
#undef GET_U64
#undef GET_FLOAT
    return 0;
}
