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
    float low = 0.0f, fast = 0.0f, wander = 0.0f;
    float body = clampf(recipe->body, 0.0f, 1.0f);
    float edge = clampf(recipe->edge, 0.0f, 1.0f);
    float drift = clampf(recipe->drift, 0.0f, 1.0f);
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
        data[i] = clampf(shaped, -1.0f, 1.0f);
    }
    ts_sample_free(sample);
    sample->data = data;
    sample->frames = frames;
    sample->sample_rate = parent->sample_rate;
    snprintf(sample->name, sizeof(sample->name), "%s", parent->name);
    set_error(error, error_size, "");
    return 1;
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
    result.has_selection = instrument->has_selection;
    result.process = instrument->process;
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
    instrument->undo_count = 0;
    instrument->redo_count = 0;
}

static int render_snapshot(TsSample *destination, const TsInstrument *instrument,
                           const TsEditSnapshot *state, char *error, size_t error_size)
{
    return ts_sample_process(destination, &instrument->parent, state->crop_first,
                             state->crop_last, &state->process, error, error_size);
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
    instrument->process.seed = 0x53495354u;
    instrument->process.body = 0.5f;
    instrument->process.edge = 0.12f;
    instrument->process.drift = 0.23f;
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
    ts_sample_init(&parent);
    ts_sample_init(&current);
    generator.kind = kind;
    generator.seed = seed;
    if (!ts_sample_generate(&parent, &generator, error, error_size) ||
        !ts_sample_process(&current, &parent, 0, parent.frames, &instrument->process,
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
    reset_editor(instrument);
    return 1;
}

int ts_instrument_load_wav(TsInstrument *instrument, const char *path,
                           char *error, size_t error_size)
{
    TsSample parent, current;
    ts_sample_init(&parent);
    ts_sample_init(&current);
    if (!ts_sample_load_wav(&parent, path, error, error_size) ||
        !ts_sample_process(&current, &parent, 0, parent.frames, &instrument->process,
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
    reset_editor(instrument);
    return 1;
}

int ts_instrument_reseed(TsInstrument *instrument, char *error, size_t error_size)
{
    if (instrument->source_kind == TS_SOURCE_GENERATED) {
        return ts_instrument_generate(instrument, instrument->generator.kind,
                                      advance_seed(instrument->generator.seed), error, error_size);
    }
    if (instrument->source_kind == TS_SOURCE_IMPORTED) {
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
    ts_sample_init(&current);
    if (!ts_sample_process(&current, &instrument->parent, instrument->crop_first,
                           instrument->crop_last, process, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->process = *process;
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

void ts_instrument_clear_selection(TsInstrument *instrument)
{
    instrument->selection_first = 0;
    instrument->selection_last = 0;
    instrument->has_selection = 0;
}

int ts_instrument_crop_selection(TsInstrument *instrument, char *error, size_t error_size)
{
    TsSample current;
    size_t new_first, new_last;
    if (!instrument->has_selection || instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range before cropping");
        return 0;
    }
    new_first = instrument->crop_first + instrument->selection_first;
    new_last = instrument->crop_first + instrument->selection_last;
    ts_sample_init(&current);
    if (!ts_sample_process(&current, &instrument->parent, new_first, new_last,
                           &instrument->process, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->crop_first = new_first;
    instrument->crop_last = new_last;
    ts_instrument_clear_selection(instrument);
    ts_instrument_show_all(instrument);
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
    instrument->has_selection = target.has_selection;
    instrument->process = target.process;
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
