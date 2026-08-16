#include "tapesister/sample.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define ts_mkdir(path) _mkdir(path)
#define ts_rmdir(path) _rmdir(path)
#else
#include <unistd.h>
#define ts_mkdir(path) mkdir(path, 0755)
#define ts_rmdir(path) rmdir(path)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int is_zero_crossing(const TsSample *sample, size_t frame);

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

static void put_fm_patch(FILE *f, const TsFmPatch *patch)
{
    put32(f, (uint32_t)patch->structure);
    put32(f, (uint32_t)patch->ratio_family);
    put_float(f, patch->depth); put_float(f, patch->shape);
    put_float(f, patch->feedback); put_float(f, patch->transient_mix);
    for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op) put_float(f, patch->ratios[op]);
}

static int get_fm_patch(FILE *f, TsFmPatch *patch)
{
    uint32_t value;
    if (!get32(f, &value)) return 0;
    patch->structure = (int)value;
    if (!get32(f, &value)) return 0;
    patch->ratio_family = (int)value;
    if (!get_float(f, &patch->depth) || !get_float(f, &patch->shape) ||
        !get_float(f, &patch->feedback) || !get_float(f, &patch->transient_mix)) return 0;
    for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op)
        if (!get_float(f, &patch->ratios[op]) || patch->ratios[op] < 0.05f ||
            patch->ratios[op] > 16.0f) return 0;
    return patch->structure >= 0 && patch->structure < TS_FM_STRUCTURE_COUNT &&
           patch->ratio_family >= 0 && patch->ratio_family < TS_FM_RATIO_FAMILY_COUNT &&
           patch->depth >= 0.15f && patch->depth <= 12.0f &&
           patch->shape >= 0.0f && patch->shape <= 1.0f &&
           patch->feedback >= 0.0f && patch->feedback <= 0.82f &&
           patch->transient_mix >= 0.0f && patch->transient_mix <= 0.60f;
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

static TsTuning default_tuning(void)
{
    TsTuning tuning = {TS_KEYBOARD_BASE_NOTE, 0.0f};
    return tuning;
}

static TsTuning tuning_from_frequency(double frequency);

static TsTuning tuning_from_midi(double midi)
{
    TsTuning tuning;
    int root = (int)floor(midi + 0.5);
    if (root < 0) root = 0;
    if (root > 127) root = 127;
    tuning.root_note = root;
    tuning.fine_tune_cents = (float)((midi - root) * 100.0);
    if (tuning.fine_tune_cents < -100.0f) tuning.fine_tune_cents = -100.0f;
    if (tuning.fine_tune_cents > 100.0f) tuning.fine_tune_cents = 100.0f;
    return tuning;
}

static int tuning_valid(const TsTuning *tuning)
{
    return tuning != NULL && tuning->root_note >= 0 && tuning->root_note <= 127 &&
           isfinite(tuning->fine_tune_cents) &&
           tuning->fine_tune_cents >= -100.0f && tuning->fine_tune_cents <= 100.0f;
}

double ts_tuning_frequency(const TsTuning *tuning)
{
    if (!tuning_valid(tuning)) return 0.0;
    return 440.0 * pow(2.0, ((double)tuning->root_note - 69.0 +
                            (double)tuning->fine_tune_cents / 100.0) / 12.0);
}

double ts_tuning_note_pitch(const TsTuning *tuning, int keyboard_note)
{
    if (!tuning_valid(tuning)) return 1.0;
    return pow(2.0, ((double)(TS_KEYBOARD_BASE_NOTE + keyboard_note -
                              tuning->root_note) -
                     (double)tuning->fine_tune_cents / 100.0) / 12.0);
}

const char *ts_midi_note_name(int note, char *name, size_t size)
{
    static const char *classes[] = {"C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B"};
    if (name == NULL || size == 0) return "";
    if (note < 0 || note > 127) {
        snprintf(name, size, "---");
        return name;
    }
    snprintf(name, size, "%s%d", classes[note % 12], note / 12 - 1);
    return name;
}

int ts_instrument_suggest_pitch(const TsInstrument *instrument, TsTuning *suggestion,
                                float *confidence, char *error, size_t error_size)
{
    const TsSample *sample;
    size_t first;
    size_t last;
    size_t frames;
    size_t lag_min;
    size_t lag_max;
    float *correlation;
    double mean = 0.0;
    double energy = 0.0;
    float strongest = -1.0f;
    size_t strongest_lag = 0;
    size_t chosen_lag = 0;
    if (instrument == NULL || suggestion == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current audio to analyze");
        return 0;
    }
    sample = &instrument->current;
    if (instrument->has_selection) {
        first = instrument->selection_first;
        last = instrument->selection_last;
    } else if (instrument->has_loop) {
        first = instrument->loop_first;
        last = instrument->loop_last;
    } else {
        first = 0;
        last = sample->frames;
    }
    if (last <= first + 128u || last > sample->frames) {
        set_error(error, error_size, "Choose at least 128 frames for pitch analysis");
        return 0;
    }
    frames = last - first;
    if (frames > 16384u) frames = 16384u;
    lag_min = sample->sample_rate / 2000u;
    if (lag_min < 2u) lag_min = 2u;
    lag_max = sample->sample_rate / 30u;
    if (lag_max > frames / 2u) lag_max = frames / 2u;
    if (lag_max <= lag_min + 2u) {
        set_error(error, error_size, "Selection is too short for pitch analysis");
        return 0;
    }
    correlation = (float *)calloc(lag_max + 1u, sizeof(float));
    if (correlation == NULL) {
        set_error(error, error_size, "Out of memory while suggesting pitch");
        return 0;
    }
    for (size_t i = 0; i < frames; ++i) mean += sample->data[first + i];
    mean /= (double)frames;
    for (size_t i = 0; i < frames; ++i) {
        double value = sample->data[first + i] - mean;
        energy += value * value;
    }
    if (energy < 0.00000001) {
        free(correlation);
        set_error(error, error_size, "Audio is too quiet for a pitch suggestion");
        return 0;
    }
    for (size_t lag = lag_min; lag <= lag_max; ++lag) {
        double numerator = 0.0;
        double left_energy = 0.0;
        double right_energy = 0.0;
        for (size_t i = 0; i + lag < frames; ++i) {
            double left_value = sample->data[first + i] - mean;
            double right_value = sample->data[first + i + lag] - mean;
            numerator += left_value * right_value;
            left_energy += left_value * left_value;
            right_energy += right_value * right_value;
        }
        if (left_energy > 0.0 && right_energy > 0.0)
            correlation[lag] = (float)(numerator / sqrt(left_energy * right_energy));
        if (correlation[lag] > strongest) {
            strongest = correlation[lag];
            strongest_lag = lag;
        }
    }
    if (strongest < 0.35f) {
        free(correlation);
        set_error(error, error_size, "No stable pitch found - try a tonal selection or loop");
        return 0;
    }
    for (size_t lag = lag_min + 1u; lag < lag_max; ++lag) {
        if (correlation[lag] >= 0.35f && correlation[lag] >= strongest * 0.85f &&
            correlation[lag] >= correlation[lag - 1u] &&
            correlation[lag] > correlation[lag + 1u]) {
            chosen_lag = lag;
            break;
        }
    }
    if (chosen_lag == 0) chosen_lag = strongest_lag;
    {
        double refined = (double)chosen_lag;
        double left_value = correlation[chosen_lag - 1u];
        double center_value = correlation[chosen_lag];
        double right_value = correlation[chosen_lag + 1u];
        double divisor = left_value - 2.0 * center_value + right_value;
        double frequency;
        if (fabs(divisor) > 0.0000001)
            refined += 0.5 * (left_value - right_value) / divisor;
        frequency = sample->sample_rate / refined;
        *suggestion = tuning_from_frequency(frequency);
    }
    if (confidence != NULL) *confidence = correlation[chosen_lag];
    free(correlation);
    set_error(error, error_size, "");
    return 1;
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

int ts_sample_load_wav_metadata(TsSample *sample, TsTuning *tuning,
                                int *has_loop, size_t *loop_first,
                                size_t *loop_last, TsLoopMode *loop_mode,
                                const char *path, char *error, size_t error_size)
{
    FILE *f = fopen(path, "rb");
    unsigned char header[12];
    unsigned char fmt[40];
    uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
    uint32_t rate = 0, data_size = 0;
    long data_offset = -1;
    float *decoded = NULL;
    TsTuning loaded_tuning = default_tuning();
    uint32_t loaded_loop_start = 0;
    uint32_t loaded_loop_end = 0;
    uint32_t loaded_loop_type = 0;
    int loaded_has_loop = 0;

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
        } else if (memcmp(chunk, "smpl", 4) == 0 && size >= 20u) {
            unsigned char smpl[60] = {0};
            size_t keep = size < sizeof(smpl) ? size : sizeof(smpl);
            uint32_t unity;
            uint32_t fraction_bits;
            double midi;
            int root;
            if (fread(smpl, 1, keep, f) != keep) break;
            unity = le32(smpl + 12);
            fraction_bits = le32(smpl + 16);
            midi = (double)unity + (double)fraction_bits / 4294967296.0;
            root = (int)floor(midi + 0.5);
            if (root < 0) root = 0;
            if (root > 127) root = 127;
            loaded_tuning.root_note = root;
            loaded_tuning.fine_tune_cents = (float)((midi - root) * 100.0);
            if (loaded_tuning.fine_tune_cents < -100.0f)
                loaded_tuning.fine_tune_cents = -100.0f;
            if (loaded_tuning.fine_tune_cents > 100.0f)
                loaded_tuning.fine_tune_cents = 100.0f;
            if (keep >= 60u && le32(smpl + 28) > 0u) {
                loaded_loop_type = le32(smpl + 40);
                loaded_loop_start = le32(smpl + 44);
                loaded_loop_end = le32(smpl + 48);
                loaded_has_loop = 1;
            }
            if (size > keep) fseek(f, (long)(size - keep), SEEK_CUR);
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
    if (tuning != NULL) *tuning = loaded_tuning;
    if (loaded_has_loop && loaded_loop_start < frames &&
        loaded_loop_end >= loaded_loop_start && loaded_loop_end < frames) {
        if (has_loop != NULL) *has_loop = 1;
        if (loop_first != NULL) *loop_first = loaded_loop_start;
        if (loop_last != NULL) *loop_last = (size_t)loaded_loop_end + 1u;
        if (loop_mode != NULL)
            *loop_mode = loaded_loop_type == 0u ? TS_LOOP_FORWARD :
                         loaded_loop_type == 2u ? TS_LOOP_REVERSE :
                         TS_LOOP_PING_PONG;
    } else {
        if (has_loop != NULL) *has_loop = 0;
        if (loop_first != NULL) *loop_first = 0;
        if (loop_last != NULL) *loop_last = 0;
        if (loop_mode != NULL) *loop_mode = TS_LOOP_FORWARD;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_sample_load_wav_tuned(TsSample *sample, TsTuning *tuning, const char *path,
                             char *error, size_t error_size)
{
    return ts_sample_load_wav_metadata(sample, tuning, NULL, NULL, NULL, NULL,
                                       path, error, error_size);
}

int ts_sample_load_wav(TsSample *sample, const char *path, char *error, size_t error_size)
{
    return ts_sample_load_wav_tuned(sample, NULL, path, error, error_size);
}

int ts_sample_save_wav16_tuned_looped(const TsSample *sample,
                                      const TsTuning *tuning,
                                      int has_loop, size_t loop_first,
                                      size_t loop_last, TsLoopMode loop_mode,
                                      const char *path,
                                      char *error, size_t error_size)
{
    TsTuning actual = tuning_valid(tuning) ? *tuning : default_tuning();
    if (sample == NULL || sample->data == NULL || sample->frames == 0 || sample->sample_rate == 0) {
        set_error(error, error_size, "No sample to export");
        return 0;
    }
    has_loop = has_loop && loop_first < loop_last && loop_last <= sample->frames;
    if (sample->frames > (UINT32_MAX - 104u) / 2u) {
        set_error(error, error_size, "Sample is too long for a RIFF WAV");
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        set_error(error, error_size, "Could not create WAV");
        return 0;
    }
    uint32_t data_bytes = (uint32_t)(sample->frames * 2u);
    uint32_t loop_bytes = has_loop ? 24u : 0u;
    fwrite("RIFF", 1, 4, f); put32(f, 80u + loop_bytes + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, sample->sample_rate); put32(f, sample->sample_rate * 2u); put16(f, 2); put16(f, 16);
    {
        double midi = (double)actual.root_note + (double)actual.fine_tune_cents / 100.0;
        int unity = (int)floor(midi);
        double fraction = midi - unity;
        uint64_t fraction_bits;
        if (unity < 0) unity = 0, fraction = 0.0;
        if (unity > 127) unity = 127, fraction = 0.0;
        fraction_bits = (uint64_t)llround(fraction * 4294967296.0);
        if (fraction_bits >= UINT64_C(4294967296)) {
            if (unity < 127) ++unity;
            fraction_bits = 0;
        }
        fwrite("smpl", 1, 4, f); put32(f, 36u + loop_bytes);
        put32(f, 0); put32(f, 0);
        put32(f, (uint32_t)llround(1000000000.0 / sample->sample_rate));
        put32(f, (uint32_t)unity); put32(f, (uint32_t)fraction_bits);
        put32(f, 0); put32(f, 0); put32(f, has_loop ? 1u : 0u); put32(f, 0);
        if (has_loop) {
            uint32_t type = loop_mode == TS_LOOP_PING_PONG ? 1u :
                            loop_mode == TS_LOOP_REVERSE ? 2u : 0u;
            put32(f, 0);
            put32(f, type);
            put32(f, (uint32_t)loop_first);
            put32(f, (uint32_t)(loop_last - 1u));
            put32(f, 0);
            put32(f, 0);
        }
    }
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

int ts_sample_save_wav16_tuned(const TsSample *sample, const TsTuning *tuning,
                               const char *path, char *error, size_t error_size)
{
    return ts_sample_save_wav16_tuned_looped(sample, tuning, 0, 0, 0,
                                             TS_LOOP_FORWARD, path,
                                             error, error_size);
}

int ts_sample_save_wav16(const TsSample *sample, const char *path,
                         char *error, size_t error_size)
{
    TsTuning tuning = default_tuning();
    return ts_sample_save_wav16_tuned(sample, &tuning, path, error, error_size);
}

const char *ts_generator_name(TsGeneratorKind kind)
{
    static const char *names[] = {"TONAL", "METALLIC", "NOISE", "PULSE", "FM"};
    return kind >= 0 && kind < TS_GENERATOR_COUNT ? names[kind] : "UNKNOWN";
}

const char *ts_fm_structure_name(int structure)
{
    static const char *names[] = {
        "CHAIN", "BRANCH", "TWIN", "PARALLEL", "STRIKE", "CLUSTER"
    };
    return structure >= 0 && structure < TS_FM_STRUCTURE_COUNT ?
           names[structure] : "UNKNOWN";
}

const char *ts_fm_ratio_family_name(int family)
{
    static const char *names[] = {
        "HARMONIC", "FIFTHS", "SUBHARMONIC", "CLUSTERED", "METALLIC", "MIXED"
    };
    return family >= 0 && family < TS_FM_RATIO_FAMILY_COUNT ?
           names[family] : "UNKNOWN";
}

void ts_fm_patch_from_recipe(const TsGeneratorRecipe *recipe, TsFmPatch *patch)
{
    static const float ratio_families[TS_FM_RATIO_FAMILY_COUNT][TS_FM_OPERATOR_COUNT] = {
        {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
        {1.0f, 1.5f, 2.25f, 3.0f, 4.5f, 6.75f},
        {1.0f, 0.5f, 0.333333f, 0.25f, 0.2f, 1.5f},
        {1.0f, 1.006f, 0.994f, 2.01f, 1.99f, 3.03f},
        {1.0f, 1.414214f, 2.718282f, 3.141593f, 4.236068f, 0.618034f},
        {1.0f, 2.5f, 1.333333f, 3.75f, 0.75f, 5.125f}
    };
    uint32_t rng;
    if (patch == NULL) return;
    memset(patch, 0, sizeof(*patch));
    if (recipe == NULL) return;
    if (recipe->kind == TS_GENERATOR_FM && recipe->has_fm_patch) {
        *patch = recipe->fm_patch;
        return;
    }
    rng = recipe->seed ^ 0x464d3655u;
    patch->structure = (int)(rng_next(&rng) % TS_FM_STRUCTURE_COUNT);
    patch->ratio_family = (int)(rng_next(&rng) % TS_FM_RATIO_FAMILY_COUNT);
    patch->depth = 0.8f + rng_unit(&rng) * 7.2f;
    patch->shape = rng_unit(&rng);
    patch->feedback = rng_unit(&rng) * 0.82f;
    patch->transient_mix = 0.08f + rng_unit(&rng) * 0.42f;
    for (int i = 0; i < TS_FM_OPERATOR_COUNT; ++i) {
        float spread = 1.0f + rng_bipolar(&rng) *
                       (patch->ratio_family == 3 ? 0.004f : 0.018f);
        patch->ratios[i] = ratio_families[patch->ratio_family][i] * spread;
    }
}

void ts_fm_patch_vary(const TsFmPatch *source, uint32_t seed, float range,
                      TsFmPatch *varied)
{
    uint32_t rng = seed ^ 0x56415259u;
    float amount = clampf(range, 0.0f, 1.0f);
    if (varied == NULL) return;
    memset(varied, 0, sizeof(*varied));
    if (source == NULL) return;
    *varied = *source;
    if (amount <= 0.0f) return;
    /* Structure and ratio family are the retained FM architecture. Range controls
       both the number of synthesis decisions rolled and their maximum distance. */
    varied->depth = clampf(source->depth * exp2f(rng_bipolar(&rng) * amount * 1.35f),
                           0.15f, 12.0f);
    if (amount >= 0.08f)
        varied->shape = clampf(source->shape + rng_bipolar(&rng) * amount * 0.55f, 0.0f, 1.0f);
    if (amount >= 0.16f)
        varied->feedback = clampf(source->feedback + rng_bipolar(&rng) * amount * 0.42f,
                                  0.0f, 0.82f);
    if (amount >= 0.24f)
        varied->transient_mix = clampf(source->transient_mix +
                                       rng_bipolar(&rng) * amount * 0.28f,
                                       0.0f, 0.60f);
    for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op) {
        float threshold = 0.12f + (float)op * 0.11f;
        if (amount >= threshold) {
            float distance = rng_bipolar(&rng) * amount * 0.42f;
            varied->ratios[op] = clampf(source->ratios[op] * exp2f(distance),
                                        0.05f, 16.0f);
        }
    }
}

float ts_fm_patch_distance(const TsFmPatch *source, const TsFmPatch *varied)
{
    float distance;
    if (source == NULL || varied == NULL || source->structure != varied->structure ||
        source->ratio_family != varied->ratio_family) return INFINITY;
    distance = fabsf(varied->depth - source->depth) / 11.85f +
               fabsf(varied->shape - source->shape) +
               fabsf(varied->feedback - source->feedback) / 0.82f +
               fabsf(varied->transient_mix - source->transient_mix) / 0.60f;
    for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op)
        distance += fabsf(log2f(varied->ratios[op] / source->ratios[op])) / 4.0f;
    return distance;
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
    float phase = 0.0f, mod_phase = 0.0f, aux_phase = 0.0f;
    float fm_phase[TS_FM_OPERATOR_COUNT] = {0.0f};
    float fm_previous[TS_FM_OPERATOR_COUNT] = {0.0f};
    TsFmPatch fm_patch;
    float noise_lp = 0.0f, noise_slow = 0.0f;
    float seed_a = rng_unit(&rng);
    float seed_b = rng_unit(&rng);
    float seed_c = rng_unit(&rng);
    float seed_d = rng_unit(&rng);
    unsigned variation = rng & 3u;
    static const float pitch_ratios[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    frequency = clampf(frequency * pitch_ratios[(rng >> 2) % 5u], 30.0f, 2000.0f);
    ts_fm_patch_from_recipe(recipe, &fm_patch);
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while generating sample");
        return 0;
    }
    for (size_t i = 0; i < frames; ++i) {
        float t = (float)i / (float)rate;
        float attack = fminf(1.0f, t * (35.0f + seed_a * 420.0f));
        float tail = fminf(1.0f, (seconds - t) * (12.0f + seed_d * 48.0f));
        float decay = expf(-t * (0.45f + seed_b * 6.5f));
        float random = rng_bipolar(&rng);
        float value;
        noise_lp += (random - noise_lp) * (0.015f + seed_a * 0.08f);
        noise_slow += (random - noise_slow) * 0.00015f;
        switch (recipe->kind) {
        case TS_GENERATOR_FM: {
            float operators[TS_FM_OPERATOR_COUNT] = {0.0f};
            float carriers = 0.0f;
            float carrier_count = 1.0f;
            for (int op = TS_FM_OPERATOR_COUNT - 1; op >= 0; --op) {
                float modulation = 0.0f;
                float op_attack = fminf(1.0f, t * (45.0f + fm_patch.shape * 720.0f));
                float op_decay = 0.35f + fm_patch.shape * 3.8f;
                int carrier = 0;
                if (fm_patch.structure == 0) {
                    if (op < 5) modulation = operators[op + 1];
                    carrier = op == 0;
                } else if (fm_patch.structure == 1) {
                    if (op == 4 || op == 3) modulation = operators[5];
                    else if (op == 2) modulation = operators[3];
                    else if (op == 1) modulation = operators[4];
                    else if (op == 0) modulation = operators[1] + operators[2];
                    carrier = op == 0;
                } else if (fm_patch.structure == 2) {
                    if (op == 4) modulation = operators[5];
                    else if (op == 2) modulation = operators[3];
                    else if (op == 1) modulation = operators[2];
                    else if (op == 0) modulation = operators[4];
                    carrier = op <= 1;
                } else if (fm_patch.structure == 3) {
                    if (op <= 2) modulation = operators[op + 3];
                    carrier = op <= 2;
                } else if (fm_patch.structure == 4) {
                    if (op == 4) modulation = operators[5];
                    else if (op == 3) modulation = operators[4];
                    else if (op <= 2) modulation = operators[3] * (1.0f - op * 0.18f);
                    carrier = op <= 2;
                } else {
                    if (op == 2) modulation = operators[5] + operators[4] * 0.3f;
                    else if (op == 1) modulation = operators[4] + operators[3] * 0.3f;
                    else if (op == 0) modulation = operators[3] + operators[5] * 0.3f;
                    carrier = op <= 2;
                }
                if (op == 5)
                    modulation += fm_previous[5] * fm_patch.feedback;
                if (!carrier)
                    op_decay += 0.9f + (float)op * 0.38f + fm_patch.shape * 8.0f;
                fm_phase[op] += (float)(2.0 * M_PI) * frequency *
                                fm_patch.ratios[op] / (float)rate;
                if (fm_phase[op] > (float)(2.0 * M_PI))
                    fm_phase[op] -= (float)(2.0 * M_PI);
                operators[op] = sinf(fm_phase[op] +
                                     modulation * fm_patch.depth) *
                                op_attack * tail * expf(-t * op_decay);
                fm_previous[op] = operators[op];
                if (carrier) carriers += operators[op];
            }
            if (fm_patch.structure == 2) carrier_count = 2.0f;
            else if (fm_patch.structure >= 3) carrier_count = 3.0f;
            value = carriers / sqrtf(carrier_count);
            value += (random - noise_lp) *
                     expf(-t * (24.0f + fm_patch.shape * 92.0f)) *
                     fm_patch.transient_mix;
            break;
        }
        case TS_GENERATOR_METALLIC: {
            float ratio = 1.37f + seed_a * 6.1f;
            float sweep = 1.0f + expf(-t * (4.0f + seed_c * 15.0f)) *
                                      (0.4f + seed_b * 6.5f);
            phase += (float)(2.0 * M_PI) * frequency * sweep / (float)rate;
            mod_phase += (float)(2.0 * M_PI) * frequency * ratio / (float)rate;
            aux_phase += (float)(2.0 * M_PI) * frequency *
                         (2.11f + seed_d * 3.8f) / (float)rate;
            if (variation == 0) {
                value = sinf(phase + sinf(mod_phase) * (2.0f + seed_b * 8.0f));
                value += sinf(aux_phase) * 0.28f;
            } else if (variation == 1) {
                value = sinf(phase) * 0.56f + sinf(mod_phase) * 0.38f +
                        sinf(aux_phase) * 0.32f;
                value *= 0.45f + fabsf(sinf(mod_phase * 0.173f)) * 0.75f;
            } else if (variation == 2) {
                value = sinf(phase + sinf(aux_phase) * (0.8f + seed_c * 3.2f));
                value += (random - noise_lp) * expf(-t * 22.0f) * 0.72f;
            } else {
                value = random * sinf(phase) * 0.66f + sinf(mod_phase) * 0.42f;
                value += sinf(aux_phase + noise_slow * 12.0f) * 0.25f;
            }
            value *= attack * tail * expf(-t * (0.7f + seed_b * 7.4f));
            break;
        }
        case TS_GENERATOR_NOISE: {
            float resonant = sinf((float)(2.0 * M_PI) * frequency * t + noise_slow * 8.0f);
            if (variation == 0)
                value = random * 0.50f + noise_lp * 1.2f + resonant * 0.28f;
            else if (variation == 1)
                value = (random - noise_lp) * 0.9f + resonant * noise_lp * 0.8f;
            else if (variation == 2)
                value = noise_lp * 1.6f + noise_slow * 3.5f + resonant * 0.18f;
            else {
                float crack = fabsf(random) > 0.92f - seed_c * 0.12f ? random : 0.0f;
                value = crack * 1.5f + noise_lp * 0.42f + resonant * 0.25f;
            }
            value *= attack * tail * expf(-t * (0.8f + seed_a * 8.0f));
            break;
        }
        case TS_GENERATOR_PULSE: {
            float sweep_hz = frequency * (1.0f + expf(-t * 9.0f) * (0.5f + seed_b * 3.0f));
            phase += (float)(2.0 * M_PI) * sweep_hz / (float)rate;
            if (phase > (float)(2.0 * M_PI)) phase -= (float)(2.0 * M_PI);
            if (variation == 0) {
                value = phase < (0.22f + seed_a * 0.56f) * (float)(2.0 * M_PI) ?
                        0.72f : -0.72f;
            } else if (variation == 1) {
                float duty = 0.12f + seed_a * 0.24f +
                             sinf((float)(2.0 * M_PI) * t * (0.3f + seed_c * 4.0f)) * 0.09f;
                value = phase < duty * (float)(2.0 * M_PI) ? 0.88f : -0.52f;
            } else if (variation == 2) {
                value = phase / (float)M_PI - 1.0f;
                value += sinf(phase * (2.0f + floorf(seed_d * 5.0f))) * 0.3f;
            } else {
                float duty = 0.015f + seed_a * 0.055f;
                value = phase < duty * (float)(2.0 * M_PI) ? 1.0f : -0.08f;
                value += random * (phase < duty * (float)(2.0 * M_PI) ? 0.7f : 0.04f);
            }
            value += noise_lp * (0.08f + seed_d * 0.3f);
            value *= attack * tail * decay;
            break;
        }
        case TS_GENERATOR_TONAL:
        default: {
            float pitch_drop = 1.0f + expf(-t * 14.0f) * (0.04f + seed_a * 0.3f);
            phase += (float)(2.0 * M_PI) * frequency * pitch_drop / (float)rate;
            mod_phase += (float)(2.0 * M_PI) * frequency *
                         (1.49f + seed_c * 3.6f) / (float)rate;
            aux_phase += (float)(2.0 * M_PI) * frequency *
                         (0.498f + seed_d * 1.51f) / (float)rate;
            if (variation == 0) {
                value = sinf(phase) + sinf(aux_phase) * (0.12f + seed_a * 0.34f);
                value += sinf(phase * 2.0f + seed_b) * (0.06f + seed_b * 0.18f);
            } else if (variation == 1) {
                value = sinf(phase) * 0.72f + sinf(phase * 3.0f) * 0.28f +
                        sinf(phase * 5.0f) * 0.16f;
                value *= 0.72f + sinf((float)(2.0 * M_PI) * t *
                                      (1.0f + seed_c * 6.0f)) * 0.18f;
            } else if (variation == 2) {
                value = sinf(phase + sinf(mod_phase) *
                             (0.35f + seed_b * 2.8f) * expf(-t * 1.8f));
                value += sinf(aux_phase) * 0.3f;
            } else {
                value = sinf(phase) * 0.62f + sinf(aux_phase) * 0.48f;
                value += sinf(mod_phase) * 0.18f + noise_lp * 0.12f;
                value *= expf(-t * (0.25f + seed_d * 2.4f));
            }
            value += noise_lp * (0.02f + seed_c * 0.10f);
            value *= attack * tail * (0.25f + decay * 0.75f);
            break;
        }
        }
        data[i] = tanhf(value * 1.15f) * 0.78f;
    }
    ts_sample_free(sample);
    sample->data = data;
    sample->frames = frames;
    sample->sample_rate = rate;
    if (recipe->kind == TS_GENERATOR_FM)
        snprintf(sample->name, sizeof(sample->name), "FM %.8s %.8s %08X",
                 ts_fm_structure_name(fm_patch.structure),
                 ts_fm_ratio_family_name(fm_patch.ratio_family), recipe->seed);
    else
        snprintf(sample->name, sizeof(sample->name), "%s V%u %08X",
                 ts_generator_name(recipe->kind), variation + 1u, recipe->seed);
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

static float sample_cubic_range(const float *data, size_t first, size_t last,
                                double position)
{
    size_t i1, i0, i2, i3;
    float fraction, a, b, c, d;
    if (position < (double)first) position = (double)first;
    if (position > (double)(last - 1u)) position = (double)(last - 1u);
    i1 = (size_t)position;
    i0 = i1 > first ? i1 - 1u : i1;
    i2 = i1 + 1u < last ? i1 + 1u : i1;
    i3 = i2 + 1u < last ? i2 + 1u : i2;
    fraction = (float)(position - (double)i1);
    a = -0.5f * data[i0] + 1.5f * data[i1] - 1.5f * data[i2] + 0.5f * data[i3];
    b = data[i0] - 2.5f * data[i1] + 2.0f * data[i2] - 0.5f * data[i3];
    c = -0.5f * data[i0] + 0.5f * data[i2];
    d = data[i1];
    return ((a * fraction + b) * fraction + c) * fraction + d;
}

static int warp_range(TsSample *sample, size_t first, size_t last, float amount,
                      char *error, size_t error_size)
{
    float *source;
    size_t length = last - first;
    double phase = 0.0;
    double rate;
    double depth;
    if (amount <= 0.0f || length < 2u) return 1;
    source = (float *)malloc(length * sizeof(float));
    if (source == NULL) {
        set_error(error, error_size, "Out of memory while warping waveform");
        return 0;
    }
    memcpy(source, sample->data + first, length * sizeof(float));
    amount = clampf(amount, 0.0f, 1.0f);
    rate = 31.0 + 1969.0 * (double)amount * (double)amount;
    depth = 0.15 + 191.85 * (double)amount * (double)amount;
    for (size_t i = 0; i < length; ++i) {
        double edge = (double)(i < length - 1u - i ? i : length - 1u - i);
        double p = phase + (double)i * 2.0 * M_PI * rate / sample->sample_rate;
        double simple = sin(p);
        double metallic = sin(p * 1.61803398875 + 1.35 * sin(p * 0.503));
        double unstable = tanh(1.8 * sin(p * 2.414 + 2.2 * metallic));
        double middle = clampf((amount - 0.20f) / 0.55f, 0.0f, 1.0f);
        double high = clampf((amount - 0.68f) / 0.32f, 0.0f, 1.0f);
        double modulator = simple + (metallic - simple) * middle;
        double displacement;
        float value;
        modulator += (unstable - modulator) * high;
        displacement = depth * modulator;
        if (displacement > edge) displacement = edge;
        if (displacement < -edge) displacement = -edge;
        value = sample_cubic_range(source, 0u, length, (double)i + displacement);
        if (!isfinite(value)) {
            free(source);
            set_error(error, error_size, "WARP produced non-finite audio");
            return 0;
        }
        sample->data[first + i] = value;
    }
    free(source);
    return 1;
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
    float filter_x1 = 0.0f, filter_x2 = 0.0f, filter_y1 = 0.0f, filter_y2 = 0.0f;
    float filter_b0 = 1.0f, filter_b1 = 0.0f, filter_b2 = 0.0f;
    float filter_a1 = 0.0f, filter_a2 = 0.0f;
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

    if (recipe->filter_enabled) {
        double cutoff = clampf(recipe->filter_cutoff_hz, 20.0f,
                               parent->sample_rate * 0.45f);
        double q = 0.5 + clampf(recipe->filter_resonance, 0.0f, 1.0f) * 11.5;
        double omega = 2.0 * M_PI * cutoff / parent->sample_rate;
        double cosine = cos(omega);
        double sine = sin(omega);
        double alpha = sine / (2.0 * q);
        double a0 = 1.0 + alpha;
        if (recipe->filter_mode == TS_FILTER_HIGHPASS) {
            filter_b0 = (float)((1.0 + cosine) * 0.5 / a0);
            filter_b1 = (float)(-(1.0 + cosine) / a0);
            filter_b2 = filter_b0;
        } else if (recipe->filter_mode == TS_FILTER_BANDPASS) {
            filter_b0 = (float)(alpha / a0);
            filter_b1 = 0.0f;
            filter_b2 = -filter_b0;
        } else {
            filter_b0 = (float)((1.0 - cosine) * 0.5 / a0);
            filter_b1 = (float)((1.0 - cosine) / a0);
            filter_b2 = filter_b0;
        }
        filter_a1 = (float)(-2.0 * cosine / a0);
        filter_a2 = (float)((1.0 - alpha) / a0);
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

        if (recipe->shaper_enabled && recipe->shaper_mix > 0.0f) {
            float dry = shaped;
            float drive = clampf(recipe->shaper_drive, 1.0f, 16.0f);
            float wet = shaped * drive;
            float mix = clampf(recipe->shaper_mix, 0.0f, 1.0f);
            if (recipe->shaper_mode == TS_SHAPER_CLIP) {
                wet = clampf(wet, -1.0f, 1.0f);
            } else if (recipe->shaper_mode == TS_SHAPER_FOLD) {
                for (int fold = 0; fold < 16 && (wet > 1.0f || wet < -1.0f); ++fold)
                    wet = wet > 1.0f ? 2.0f - wet : -2.0f - wet;
                wet = clampf(wet, -1.0f, 1.0f);
            } else {
                wet = tanhf(wet) / tanhf(drive);
            }
            shaped = dry * (1.0f - mix) + wet * mix;
        }

        if (recipe->filter_enabled) {
            float filtered = filter_b0 * shaped + filter_b1 * filter_x1 +
                             filter_b2 * filter_x2 - filter_a1 * filter_y1 -
                             filter_a2 * filter_y2;
            filter_x2 = filter_x1;
            filter_x1 = shaped;
            filter_y2 = filter_y1;
            filter_y1 = filtered;
            shaped = filtered;
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
    result.loop_mode = instrument->loop_mode;
    result.has_selection = instrument->has_selection;
    result.has_loop = instrument->has_loop;
    result.tuning = instrument->tuning;
    result.audible_tuning = instrument->audible_tuning;
    result.process = instrument->process;
    memcpy(result.sample_edits, instrument->sample_edits, sizeof(result.sample_edits));
    result.sample_edit_count = instrument->sample_edit_count;
    memcpy(result.post_edits, instrument->post_edits, sizeof(result.post_edits));
    result.post_edit_count = instrument->post_edit_count;
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
    instrument->loop_mode = TS_LOOP_FORWARD;
    instrument->has_loop = 0;
    instrument->sample_edit_count = 0;
    instrument->post_edit_count = 0;
    instrument->undo_count = 0;
    instrument->redo_count = 0;
}

static void replace_current_preserving_view(TsInstrument *instrument,
                                            TsSample *replacement)
{
    size_t first = instrument->view_first;
    size_t last = instrument->view_last;
    size_t frames = replacement->frames;
    ts_sample_free(&instrument->current);
    instrument->current = *replacement;
    ts_sample_init(replacement);
    if (frames == 0) {
        instrument->view_first = instrument->view_last = 0;
        return;
    }
    if (last > frames) last = frames;
    if (first >= last) first = last > 0 ? last - 1u : 0u;
    instrument->view_first = first;
    instrument->view_last = last;
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

typedef struct { double re, im; } TsComplex;

static void smear_fft(TsComplex *values, size_t count, int inverse)
{
    for (size_t i = 1, j = 0; i < count; ++i) {
        size_t bit = count >> 1u;
        while (j & bit) { j ^= bit; bit >>= 1u; }
        j ^= bit;
        if (i < j) { TsComplex swap = values[i]; values[i] = values[j]; values[j] = swap; }
    }
    for (size_t length = 2; length <= count; length <<= 1u) {
        double angle = (inverse ? 2.0 : -2.0) * M_PI / (double)length;
        TsComplex step = { cos(angle), sin(angle) };
        for (size_t base = 0; base < count; base += length) {
            TsComplex twiddle = { 1.0, 0.0 };
            for (size_t j = 0; j < length / 2u; ++j) {
                TsComplex even = values[base + j];
                TsComplex odd = values[base + j + length / 2u];
                TsComplex product = { odd.re * twiddle.re - odd.im * twiddle.im,
                                      odd.re * twiddle.im + odd.im * twiddle.re };
                values[base + j].re = even.re + product.re;
                values[base + j].im = even.im + product.im;
                values[base + j + length / 2u].re = even.re - product.re;
                values[base + j + length / 2u].im = even.im - product.im;
                product.re = twiddle.re * step.re - twiddle.im * step.im;
                twiddle.im = twiddle.re * step.im + twiddle.im * step.re;
                twiddle.re = product.re;
            }
        }
    }
    if (inverse)
        for (size_t i = 0; i < count; ++i) { values[i].re /= count; values[i].im /= count; }
}

/* A causal wet path: a window is emitted one window after it is observed. */
static int smear_range(TsSample *sample, size_t first, size_t last, float amount,
                       char *error, size_t error_size)
{
    enum { WINDOW = 512, HOP = 128, BINS = WINDOW / 2 + 1 };
    size_t length = last - first;
    TsComplex *spectrum;
    double *memory, *phase, *wet, *weight;
    float *source;
    double shaped, seconds, decay, mix;
    if (amount == 0.0f || length == 0u) return 1;
    spectrum = (TsComplex *)calloc(WINDOW, sizeof(*spectrum));
    memory = (double *)calloc(BINS, sizeof(*memory));
    phase = (double *)calloc(BINS, sizeof(*phase));
    wet = (double *)calloc(length, sizeof(*wet));
    weight = (double *)calloc(length, sizeof(*weight));
    source = (float *)malloc(length * sizeof(*source));
    if (!spectrum || !memory || !phase || !wet || !weight || !source) {
        free(spectrum); free(memory); free(phase); free(wet); free(weight); free(source);
        set_error(error, error_size, "Out of memory while smearing spectrum");
        return 0;
    }
    memcpy(source, sample->data + first, length * sizeof(*source));
    shaped = pow((double)amount, 0.72);
    seconds = 0.025 * pow(480.0, shaped);
    decay = exp(-(double)HOP / ((double)sample->sample_rate * seconds));
    mix = 0.10 + 0.78 * shaped;
    for (size_t position = 0; position < length; position += HOP) {
        for (size_t i = 0; i < WINDOW; ++i) {
            double window = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (WINDOW - 1));
            spectrum[i].re = position + i < length ? source[position + i] * window : 0.0;
            spectrum[i].im = 0.0;
        }
        smear_fft(spectrum, WINDOW, 0);
        for (size_t bin = 0; bin < BINS; ++bin) {
            double magnitude = hypot(spectrum[bin].re, spectrum[bin].im);
            memory[bin] *= decay;
            if (magnitude > memory[bin]) {
                memory[bin] = magnitude;
                phase[bin] = atan2(spectrum[bin].im, spectrum[bin].re);
            } else phase[bin] += 2.0 * M_PI * (double)bin * HOP / WINDOW;
            spectrum[bin].re = memory[bin] * cos(phase[bin]);
            spectrum[bin].im = memory[bin] * sin(phase[bin]);
            if (bin == 0 || bin == WINDOW / 2) spectrum[bin].im = 0.0;
            if (bin > 0 && bin < WINDOW / 2) {
                spectrum[WINDOW - bin].re = spectrum[bin].re;
                spectrum[WINDOW - bin].im = -spectrum[bin].im;
            }
        }
        smear_fft(spectrum, WINDOW, 1);
        for (size_t i = 0; i < WINDOW && position + WINDOW + i < length; ++i) {
            size_t output = position + WINDOW + i;
            double window = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (WINDOW - 1));
            wet[output] += spectrum[i].re * window;
            weight[output] += window * window;
        }
    }
    for (size_t i = 0; i < length; ++i) {
        double processed = weight[i] > 1e-12 ? wet[i] / weight[i] : source[i];
        sample->data[first + i] = (float)(source[i] * (1.0 - mix) + processed * mix);
    }
    free(spectrum); free(memory); free(phase); free(wet); free(weight); free(source);
    set_error(error, error_size, "");
    return 1;
}

static uint32_t tear_hash(uint32_t value)
{
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static int tear_range(TsSample *sample, size_t first, size_t last, float amount,
                      char *error, size_t error_size)
{
    size_t length = last - first;
    size_t minimum;
    size_t *boundary;
    float *source;
    float *output;
    size_t count = 1;
    if (amount <= 0.0f || length < 8u) return 1;
    minimum = sample->sample_rate / 1000u;
    if (minimum < 24u) minimum = 24u;
    if (minimum > 128u) minimum = 128u;
    boundary = malloc((length / minimum + 3u) * sizeof(*boundary));
    source = malloc(length * sizeof(*source));
    output = malloc(length * sizeof(*output));
    if (boundary == NULL || source == NULL || output == NULL) {
        free(boundary); free(source); free(output);
        set_error(error, error_size, "Out of memory while tearing waveform");
        return 0;
    }
    memcpy(source, sample->data + first, length * sizeof(*source));
    memcpy(output, source, length * sizeof(*output));
    boundary[0] = 0;
    for (size_t frame = minimum; frame + minimum < length; ++frame) {
        size_t absolute = first + frame;
        if (frame - boundary[count - 1u] >= minimum &&
            is_zero_crossing(sample, absolute)) boundary[count++] = frame;
    }
    if (length - boundary[count - 1u] < minimum && count > 1u) --count;
    boundary[count++] = length;
    for (size_t packet = 0; packet + 2u < count; packet += 2u) {
        size_t a = boundary[packet], b = boundary[packet + 1u];
        size_t c = boundary[packet + 2u];
        uint32_t field = tear_hash((uint32_t)packet ^ 0x54454152u);
        float threshold = (float)(field & 0xffffu) / 65535.0f;
        if (threshold > amount) continue;
        if (field & 0x10000u) {
            memcpy(output + a, source + b, (c - b) * sizeof(*output));
            memcpy(output + a + c - b, source + a, (b - a) * sizeof(*output));
        } else {
            for (size_t i = a; i < b; ++i) output[i] = source[b - 1u - (i - a)];
            for (size_t i = b; i < c; ++i) output[i] = source[c - 1u - (i - b)];
        }
    }
    memcpy(sample->data + first, output, length * sizeof(*output));
    free(boundary); free(source); free(output);
    set_error(error, error_size, "");
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
    if (!ok) return 0;
    for (int index = 0; index < state->post_edit_count; ++index) {
        const TsPostEdit *operation = &state->post_edits[index];
        size_t first = operation->first;
        size_t last = operation->last;
        size_t length;
        if (first > destination->frames) first = destination->frames;
        if (last > destination->frames) last = destination->frames;
        if (last <= first) continue;
        length = last - first;
        if (operation->kind >= TS_POST_REVERSE) {
            if (operation->kind == TS_POST_CROP) {
                float *cropped = (float *)malloc(length * sizeof(float));
                if (cropped == NULL) {
                    set_error(error, error_size, "Out of memory while cropping tape");
                    return 0;
                }
                memcpy(cropped, destination->data + first, length * sizeof(float));
                ts_sample_free(destination);
                destination->data = cropped;
                destination->frames = length;
                destination->sample_rate = instrument->parent.sample_rate;
                snprintf(destination->name, sizeof(destination->name), "%s",
                         instrument->parent.name);
            } else if (operation->kind == TS_POST_REVERSE) {
                for (size_t i = 0; i < length / 2u; ++i) {
                    float swap = destination->data[first + i];
                    destination->data[first + i] = destination->data[last - 1u - i];
                    destination->data[last - 1u - i] = swap;
                }
            } else if (operation->kind == TS_POST_ROTATE) {
                size_t offset = (size_t)operation->destination;
                float *rotated;
                if (offset == 0 || offset >= length) continue;
                rotated = (float *)malloc(length * sizeof(float));
                if (rotated == NULL) {
                    set_error(error, error_size, "Out of memory while rotating waveform");
                    return 0;
                }
                memcpy(rotated, destination->data + first + offset,
                       (length - offset) * sizeof(float));
                memcpy(rotated + length - offset, destination->data + first,
                       offset * sizeof(float));
                memcpy(destination->data + first, rotated, length * sizeof(float));
                free(rotated);
            } else if (operation->kind == TS_POST_WARP) {
                if (!warp_range(destination, first, last, operation->amount,
                                error, error_size)) return 0;
            } else if (operation->kind == TS_POST_SMEAR) {
                if (!smear_range(destination, first, last, operation->amount,
                                 error, error_size)) return 0;
            } else if (operation->kind == TS_POST_TEAR) {
                if (!tear_range(destination, first, last, operation->amount,
                                error, error_size)) return 0;
            } else if (operation->kind == TS_POST_NORMALIZE) {
                float peak = 0.0f;
                float target = clampf(operation->amount, 0.0f, 1.0f);
                for (size_t i = first; i < last; ++i)
                    if (fabsf(destination->data[i]) > peak) peak = fabsf(destination->data[i]);
                if (peak > 0.0000001f) {
                    float gain = target / peak;
                    for (size_t i = first; i < last; ++i)
                        destination->data[i] = clampf(destination->data[i] * gain, -1.0f, 1.0f);
                }
            } else if (operation->kind == TS_POST_GAIN) {
                for (size_t i = first; i < last; ++i)
                    destination->data[i] = clampf(destination->data[i] * operation->amount,
                                                  -1.0f, 1.0f);
            } else if (operation->kind == TS_POST_FADE_IN ||
                       operation->kind == TS_POST_FADE_OUT) {
                for (size_t i = 0; i < length; ++i) {
                    float gain = length > 1u ?
                        (operation->kind == TS_POST_FADE_IN ?
                         (float)i / (float)(length - 1u) :
                         (float)(length - 1u - i) / (float)(length - 1u)) : 1.0f;
                    destination->data[first + i] *= gain;
                }
            }
        } else {
            float *source;
            float *output;
            int64_t left = operation->destination < 0 ? operation->destination : 0;
            int64_t right = operation->destination + (int64_t)length;
            size_t prepend = left < 0 ? (size_t)(-left) : 0u;
            size_t output_frames;
            size_t destination_first;
            size_t fade = operation->crossfade_frames;
            float mix_scale = 1.0f;
            int moving = operation->kind == TS_POST_MOVE_MIX ||
                         operation->kind == TS_POST_MOVE_OVERWRITE;
            int overwrite = operation->kind == TS_POST_COPY_OVERWRITE ||
                            operation->kind == TS_POST_MOVE_OVERWRITE;
            if (right < (int64_t)destination->frames) right = (int64_t)destination->frames;
            if (right < left || (uint64_t)(right - left) > SIZE_MAX / sizeof(float)) {
                set_error(error, error_size, "Tape placement is too large");
                return 0;
            }
            output_frames = (size_t)(right - left);
            source = (float *)malloc(length * sizeof(float));
            output = (float *)calloc(output_frames, sizeof(float));
            if (source == NULL || output == NULL) {
                free(source); free(output);
                set_error(error, error_size, "Out of memory while moving tape");
                return 0;
            }
            memcpy(source, destination->data + first, length * sizeof(float));
            memcpy(output + prepend, destination->data,
                   destination->frames * sizeof(float));
            if (fade > length / 2u) fade = length / 2u;
            if (moving) {
                size_t cleared = prepend + first;
                for (size_t i = 0; i < length; ++i) output[cleared + i] = 0.0f;
                for (size_t i = 0; i < fade; ++i) {
                    float gain = (float)(i + 1u) / (float)(fade + 1u);
                    if (cleared > prepend + i)
                        output[cleared - 1u - i] *= gain;
                    if (cleared + length + i < prepend + destination->frames)
                        output[cleared + length + i] *= gain;
                }
            }
            destination_first = (size_t)(operation->destination - left);
            if (!overwrite) {
                float source_peak = 0.0f;
                float destination_peak = 0.0f;
                float summed_peak = 0.0f;
                for (size_t i = 0; i < length; ++i)
                    if (fabsf(source[i]) > source_peak) source_peak = fabsf(source[i]);
                for (size_t i = 0; i < length; ++i) {
                    size_t at = destination_first + i;
                    float edge_gain = 1.0f;
                    float summed;
                    if (at < prepend || at >= prepend + destination->frames) continue;
                    if (fade > 0u) {
                        if (i < fade)
                            edge_gain = (float)(i + 1u) / (float)(fade + 1u);
                        if (length - 1u - i < fade) {
                            float tail = (float)(length - i) / (float)(fade + 1u);
                            if (tail < edge_gain) edge_gain = tail;
                        }
                    }
                    if (fabsf(output[at]) > destination_peak)
                        destination_peak = fabsf(output[at]);
                    summed = output[at] + source[i] * edge_gain;
                    if (fabsf(summed) > summed_peak) summed_peak = fabsf(summed);
                }
                if (summed_peak > 0.0000001f) {
                    float target_peak = source_peak > destination_peak ?
                                        source_peak : destination_peak;
                    mix_scale = target_peak / summed_peak;
                }
            }
            for (size_t i = 0; i < length; ++i) {
                size_t at = destination_first + i;
                float gain = 1.0f;
                float value;
                if (fade > 0u) {
                    if (i < fade) gain = (float)(i + 1u) / (float)(fade + 1u);
                    if (length - 1u - i < fade) {
                        float tail = (float)(length - i) / (float)(fade + 1u);
                        if (tail < gain) gain = tail;
                    }
                }
                value = source[i] * gain;
                if (overwrite)
                    output[at] = output[at] * (1.0f - gain) + value;
                else if (at >= prepend && at < prepend + destination->frames)
                    output[at] = clampf((output[at] + value) * mix_scale,
                                        -1.0f, 1.0f);
                else
                    output[at] = source[i];
            }
            free(source);
            ts_sample_free(destination);
            destination->data = output;
            destination->frames = output_frames;
            destination->sample_rate = instrument->parent.sample_rate;
            snprintf(destination->name, sizeof(destination->name), "%s",
                     instrument->parent.name);
        }
    }
    return 1;
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
    process->filter_enabled = 0;
    process->filter_mode = TS_FILTER_LOWPASS;
    process->filter_cutoff_hz = 8000.0f;
    process->filter_resonance = 0.20f;
    process->shaper_enabled = 0;
    process->shaper_mode = TS_SHAPER_TAPE;
    process->shaper_drive = 2.5f;
    process->shaper_mix = 0.65f;
}

const char *ts_filter_mode_name(TsFilterMode mode)
{
    if (mode == TS_FILTER_HIGHPASS) return "HIGH";
    if (mode == TS_FILTER_BANDPASS) return "BAND";
    return "LOW";
}

const char *ts_shaper_mode_name(TsShaperMode mode)
{
    if (mode == TS_SHAPER_CLIP) return "CLIP";
    if (mode == TS_SHAPER_FOLD) return "FOLD";
    return "TAPE";
}

const char *ts_bank_capture_name(TsBankCaptureKind kind)
{
    if (kind == TS_BANK_CAPTURE_ROOT) return "ROOT";
    if (kind == TS_BANK_CAPTURE_SELECTION) return "SEL";
    if (kind == TS_BANK_CAPTURE_LOOP) return "LOOP";
    return "FULL";
}

const char *ts_family_relation_name(TsFamilyRelation relation)
{
    static const char *names[] = {
        "SOURCE", "KEPT", "CLOSE", "WIDE", "RADICAL"
    };
    return relation >= TS_FAMILY_ROOT && relation < TS_FAMILY_RELATION_COUNT ?
           names[relation] : "UNKNOWN";
}

static void bank_slot_init(TsBankSlot *slot)
{
    memset(slot, 0, sizeof(*slot));
    ts_sample_init(&slot->sample);
    ts_sample_init(&slot->edit_parent);
    slot->loop_crossfade_ms = 8.0f;
    slot->tuning = default_tuning();
    slot->audible_tuning = default_tuning();
    slot->relation = TS_FAMILY_CAPTURED;
    slot->parent_slot = -1;
    slot->lineage_mutation = 0.35f;
    ts_process_recipe_reset(&slot->process);
}

static void bank_slot_free(TsBankSlot *slot)
{
    ts_sample_free(&slot->sample);
    ts_sample_free(&slot->edit_parent);
    free(slot->undo);
    free(slot->redo);
    bank_slot_init(slot);
}

static void bank_free(TsInstrument *instrument)
{
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) bank_slot_free(&instrument->bank[i]);
}

static int bank_root_clone(TsBankSlot *slot, const TsSample *parent,
                           const TsTuning *tuning,
                           char *error, size_t error_size)
{
    bank_slot_init(slot);
    if (!ts_sample_clone(&slot->sample, parent, error, error_size)) return 0;
    slot->capture_kind = TS_BANK_CAPTURE_ROOT;
    slot->relation = TS_FAMILY_ROOT;
    slot->lineage_seed = (uint32_t)ts_sample_hash(parent);
    slot->tuning = tuning_valid(tuning) ? *tuning : default_tuning();
    slot->audible_tuning = slot->tuning;
    slot->occupied = 1;
    return 1;
}

static int sample_clone_range(TsSample *destination, const TsSample *source,
                              size_t first, size_t last, char *error, size_t error_size)
{
    size_t frames;
    if (source == NULL || source->data == NULL || first >= last || last > source->frames) {
        set_error(error, error_size, "Invalid bank capture range");
        return 0;
    }
    frames = last - first;
    destination->data = (float *)malloc(frames * sizeof(float));
    if (destination->data == NULL) {
        set_error(error, error_size, "Out of memory while capturing bank slot");
        return 0;
    }
    memcpy(destination->data, source->data + first, frames * sizeof(float));
    destination->frames = frames;
    destination->sample_rate = source->sample_rate;
    return 1;
}

void ts_instrument_init(TsInstrument *instrument)
{
    memset(instrument, 0, sizeof(*instrument));
    ts_sample_init(&instrument->parent);
    ts_sample_init(&instrument->current);
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) bank_slot_init(&instrument->bank[i]);
    instrument->generator.seed = 0x54415045u;
    instrument->generator.kind = TS_GENERATOR_TONAL;
    instrument->generator.seconds = 2.0f;
    instrument->generator.frequency = 130.8128f;
    instrument->family_relation = TS_FAMILY_CHILD;
    instrument->family_mutation = 0.35f;
    instrument->family_locks = TS_FAMILY_LOCK_LOOP |
                               TS_FAMILY_LOCK_DURATION |
                               TS_FAMILY_LOCK_PITCH;
    instrument->family_anchor_slot = 0;
    instrument->family_last_slot = -1;
    instrument->selected_slot = 0;
    instrument->tuning = default_tuning();
    instrument->audible_tuning = default_tuning();
    ts_process_recipe_reset(&instrument->process);
}

static TsTuning tuning_from_frequency(double frequency)
{
    TsTuning tuning = default_tuning();
    double midi;
    int root;
    if (!isfinite(frequency) || frequency <= 0.0) return tuning;
    midi = 69.0 + 12.0 * log2(frequency / 440.0);
    root = (int)floor(midi + 0.5);
    if (root < 0) root = 0;
    if (root > 127) root = 127;
    tuning.root_note = root;
    tuning.fine_tune_cents = (float)((midi - root) * 100.0);
    return tuning;
}

static float generator_pitch_ratio(uint32_t seed)
{
    static const float pitch_ratios[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    uint32_t rng = seed;
    for (int i = 0; i < 4; ++i) (void)rng_unit(&rng);
    return pitch_ratios[(rng >> 2) % 5u];
}

static TsTuning generator_tuning(const TsGeneratorRecipe *generator)
{
    return tuning_from_frequency(generator->frequency *
                                 generator_pitch_ratio(generator->seed));
}

void ts_instrument_free(TsInstrument *instrument)
{
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    bank_free(instrument);
    memset(instrument, 0, sizeof(*instrument));
}

int ts_instrument_generate(TsInstrument *instrument, TsGeneratorKind kind, uint32_t seed,
                           char *error, size_t error_size)
{
    TsSample parent, current;
    TsBankSlot root;
    TsGeneratorRecipe generator = instrument->generator;
    TsProcessRecipe neutral;
    TsTuning tuning;
    ts_sample_init(&parent);
    ts_sample_init(&current);
    bank_slot_init(&root);
    generator.kind = kind;
    generator.seed = seed;
    tuning = generator_tuning(&generator);
    ts_process_recipe_reset(&neutral);
    if (!ts_sample_generate(&parent, &generator, error, error_size) ||
        !ts_sample_process(&current, &parent, 0, parent.frames, &neutral,
                           error, error_size) ||
        !bank_root_clone(&root, &parent, &tuning, error, error_size)) {
        ts_sample_free(&parent);
        ts_sample_free(&current);
        bank_slot_free(&root);
        return 0;
    }
    root.generator = generator;
    root.has_generator = 1;
    root.lineage_seed = generator.seed;
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    bank_free(instrument);
    instrument->parent = parent;
    instrument->current = current;
    instrument->bank[0] = root;
    instrument->source_kind = TS_SOURCE_GENERATED;
    instrument->generator = generator;
    instrument->tuning = tuning;
    instrument->audible_tuning = tuning;
    instrument->process = neutral;
    instrument->generation = 0;
    instrument->ancestor_hash = 0;
    instrument->family_sequence = 0;
    instrument->family_anchor_slot = 0;
    instrument->family_last_slot = -1;
    reset_editor(instrument);
    return 1;
}

static int bank_sync_selected(TsInstrument *instrument, char *error, size_t error_size);

int ts_instrument_load_wav(TsInstrument *instrument, const char *path,
                           char *error, size_t error_size)
{
    TsBankSlot imported;
    TsTuning tuning = default_tuning();
    size_t loop_first = 0, loop_last = 0;
    TsLoopMode loop_mode = TS_LOOP_FORWARD;
    int has_loop = 0;
    int slot;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Select a bank tile before loading a WAV");
        return 0;
    }
    slot = instrument->selected_slot;
    bank_slot_init(&imported);
    if (!ts_sample_load_wav_metadata(&imported.sample, &tuning, &has_loop,
                                     &loop_first, &loop_last, &loop_mode,
                                     path, error, error_size) ||
        !ts_sample_clone(&imported.edit_parent, &imported.sample,
                         error, error_size)) {
        bank_slot_free(&imported);
        return 0;
    }
    imported.occupied = 1;
    imported.capture_kind = TS_BANK_CAPTURE_CURRENT;
    imported.relation = TS_FAMILY_CAPTURED;
    imported.parent_slot = -1;
    imported.has_generator = 0;
    memset(&imported.generator, 0, sizeof(imported.generator));
    imported.lineage_seed = (uint32_t)ts_sample_hash(&imported.sample);
    imported.lineage_locks = TS_FAMILY_LOCK_ALL;
    imported.lineage_mutation = 0.0f;
    imported.tuning = tuning;
    imported.audible_tuning = tuning;
    imported.has_loop = has_loop;
    imported.loop_first = loop_first;
    imported.loop_last = loop_last;
    imported.loop_mode = loop_mode;
    bank_slot_free(&instrument->bank[slot]);
    instrument->bank[slot] = imported;
    if (!ts_instrument_select_bank(instrument, slot, error, error_size)) return 0;
    instrument->source_kind = TS_SOURCE_IMPORTED;
    memset(&instrument->generator, 0, sizeof(instrument->generator));
    instrument->has_loop = has_loop;
    instrument->loop_first = loop_first;
    instrument->loop_last = loop_last;
    instrument->loop_mode = loop_mode;
    instrument->undo_count = 0;
    instrument->redo_count = 0;
    if (!bank_sync_selected(instrument, error, error_size)) return 0;
    set_error(error, error_size, "");
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
    if (instrument == NULL) {
        set_error(error, error_size, "No instrument to process");
        return 0;
    }
    return ts_instrument_set_process_and_tuning(instrument, process,
                                                &instrument->tuning,
                                                error, error_size);
}

int ts_instrument_set_process_and_tuning(TsInstrument *instrument,
                                         const TsProcessRecipe *process,
                                         const TsTuning *tuning,
                                         char *error, size_t error_size)
{
    return ts_instrument_set_process_and_tunings(instrument, process, tuning,
                                                  tuning, error, error_size);
}

int ts_instrument_set_process_and_tunings(TsInstrument *instrument,
                                          const TsProcessRecipe *process,
                                          const TsTuning *tuning,
                                          const TsTuning *audible_tuning,
                                          char *error, size_t error_size)
{
    TsSample current;
    TsEditSnapshot target;
    if (instrument == NULL || process == NULL || !tuning_valid(tuning) ||
        !tuning_valid(audible_tuning)) {
        set_error(error, error_size, "Invalid process or tuning settings");
        return 0;
    }
    target = snapshot(instrument);
    ts_sample_init(&current);
    target.process = *process;
    target.tuning = *tuning;
    target.audible_tuning = *audible_tuning;
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->process = *process;
    instrument->tuning = *tuning;
    instrument->audible_tuning = *audible_tuning;
    return 1;
}

int ts_instrument_set_tuning(TsInstrument *instrument, int root_note,
                             float fine_tune_cents, char *error, size_t error_size)
{
    TsTuning tuning = {root_note, fine_tune_cents};
    if (instrument == NULL || instrument->current.data == NULL || !tuning_valid(&tuning)) {
        set_error(error, error_size, "Root must be MIDI 0-127 and fine tune -100 to +100 cents");
        return 0;
    }
    if (instrument->tuning.root_note == root_note &&
        fabsf(instrument->tuning.fine_tune_cents - fine_tune_cents) < 0.0001f) {
        set_error(error, error_size, "Tuning is already set");
        return 0;
    }
    begin_edit(instrument);
    instrument->tuning = tuning;
    instrument->audible_tuning = tuning;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_set_audible_tuning(TsInstrument *instrument, int root_note,
                                     float fine_tune_cents,
                                     char *error, size_t error_size)
{
    TsTuning audible = {root_note, fine_tune_cents};
    double old_audible;
    double new_audible;
    double mapping;
    TsTuning adjusted;
    if (instrument == NULL || instrument->current.data == NULL ||
        !tuning_valid(&audible) || !tuning_valid(&instrument->audible_tuning)) {
        set_error(error, error_size,
                  "Pitch must be MIDI 0-127 and trim -100 to +100 cents");
        return 0;
    }
    if (instrument->audible_tuning.root_note == root_note &&
        fabsf(instrument->audible_tuning.fine_tune_cents - fine_tune_cents) < 0.0001f) {
        set_error(error, error_size, "Audible tuning is already set");
        return 0;
    }
    old_audible = instrument->audible_tuning.root_note +
                   instrument->audible_tuning.fine_tune_cents / 100.0;
    new_audible = root_note + fine_tune_cents / 100.0;
    mapping = instrument->tuning.root_note +
              instrument->tuning.fine_tune_cents / 100.0 -
              (new_audible - old_audible);
    adjusted = tuning_from_midi(mapping);
    begin_edit(instrument);
    instrument->tuning = adjusted;
    instrument->audible_tuning = audible;
    set_error(error, error_size, "");
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
    target.loop_mode = TS_LOOP_FORWARD;
    target.has_selection = 0;
    target.has_loop = 0;
    target.sample_edit_count = 0;
    target.post_edit_count = 0;
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
    instrument->loop_mode = target.loop_mode;
    instrument->has_selection = 0;
    instrument->has_loop = 0;
    instrument->process = target.process;
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    instrument->post_edit_count = 0;
    return bank_sync_selected(instrument, error, error_size);
}

int ts_instrument_commit_current(TsInstrument *instrument, char *error, size_t error_size)
{
    TsSample parent, current;
    TsProcessRecipe neutral;
    uint64_t prior_hash;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsLoopMode loop_mode;
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
    loop_mode = instrument->loop_mode;
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
    instrument->loop_mode = loop_mode;
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
    snapped_first = first == 0 ? 0 :
                    first >= instrument->current.frames ? instrument->current.frames :
                    ts_sample_nearest_zero_crossing(&instrument->current, first);
    snapped_last = last == 0 ? 0 :
                   last >= instrument->current.frames ? instrument->current.frames :
                   ts_sample_nearest_zero_crossing(&instrument->current, last);
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
    int whole_sample;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames < 2u) {
        set_error(error, error_size, "No Current audio to loop");
        return 0;
    }
    whole_sample = !instrument->has_selection ||
                   (instrument->selection_first == 0 &&
                    instrument->selection_last == instrument->current.frames);
    if (whole_sample) {
        first = 0;
        last = instrument->current.frames;
    } else {
        first = ts_sample_nearest_zero_crossing(&instrument->current,
                                                instrument->selection_first);
        last = ts_sample_nearest_zero_crossing(&instrument->current,
                                               instrument->selection_last);
    }
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

const char *ts_loop_mode_name(TsLoopMode mode)
{
    if (mode == TS_LOOP_REVERSE) return "REVERSE";
    if (mode == TS_LOOP_PING_PONG) return "PING-PONG";
    return "FORWARD";
}

int ts_instrument_set_loop_mode(TsInstrument *instrument, TsLoopMode mode,
                                char *error, size_t error_size)
{
    if (instrument == NULL || mode < TS_LOOP_FORWARD || mode >= TS_LOOP_MODE_COUNT) {
        set_error(error, error_size, "Invalid loop direction mode");
        return 0;
    }
    if (instrument->loop_mode == mode) {
        set_error(error, error_size, "Loop mode unchanged");
        return 0;
    }
    begin_edit(instrument);
    instrument->loop_mode = mode;
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

int64_t ts_sample_snap_tape_destination(const TsSample *sample, int64_t target,
                                        size_t source_frames)
{
    size_t wanted;
    size_t maximum;
    size_t radius;
    size_t best;
    int best_crossings = -1;
    float best_level = 0.0f;
    size_t best_distance = 0;
    if (sample == NULL || sample->data == NULL || sample->frames == 0 ||
        target < 0 || (uint64_t)target + source_frames > sample->frames)
        return target;
    wanted = (size_t)target;
    maximum = sample->frames - source_frames;
    radius = sample->sample_rate / 20u;
    if (radius < 64u) radius = 64u;
    if (radius > maximum) radius = maximum;
    best = wanted;
    for (size_t distance = 0; distance <= radius; ++distance) {
        size_t candidates[2];
        int count = 0;
        if (distance <= wanted) candidates[count++] = wanted - distance;
        if (distance > 0 && wanted + distance <= maximum)
            candidates[count++] = wanted + distance;
        for (int candidate_index = 0; candidate_index < count; ++candidate_index) {
            size_t candidate = candidates[candidate_index];
            size_t end = candidate + source_frames;
            int crossings = is_zero_crossing(sample, candidate) +
                            (end < sample->frames && is_zero_crossing(sample, end));
            float level = fabsf(sample->data[candidate]) +
                          (end < sample->frames ? fabsf(sample->data[end]) : 0.0f);
            if (crossings > best_crossings ||
                (crossings == best_crossings &&
                 (level < best_level ||
                  (fabsf(level - best_level) < 0.0000001f && distance < best_distance)))) {
                best = candidate;
                best_crossings = crossings;
                best_level = level;
                best_distance = distance;
            }
        }
        if (best_crossings == 2) break;
    }
    return (int64_t)best;
}

int ts_instrument_apply_tape_drag(TsInstrument *instrument, TsPostEditKind kind,
                                  size_t first, size_t last, int64_t destination,
                                  char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsSample current;
    TsPostEdit operation;
    size_t length;
    size_t prepend;
    size_t placed_first;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current sample for tape drag");
        return 0;
    }
    if (kind < TS_POST_COPY_MIX || kind > TS_POST_MOVE_OVERWRITE) {
        set_error(error, error_size, "Invalid tape drag gesture");
        return 0;
    }
    if (first >= last || last > instrument->current.frames) {
        set_error(error, error_size, "Select a valid tape range first");
        return 0;
    }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more tape operations");
        return 0;
    }
    length = last - first;
    destination = ts_sample_snap_tape_destination(&instrument->current,
                                                   destination, length);
    memset(&operation, 0, sizeof(operation));
    operation.kind = kind;
    operation.first = first;
    operation.last = last;
    operation.destination = destination;
    operation.crossfade_frames = instrument->current.sample_rate / 1000u;
    if (operation.crossfade_frames < 8u) operation.crossfade_frames = 8u;
    if (operation.crossfade_frames > 64u) operation.crossfade_frames = 64u;
    target = snapshot(instrument);
    target.post_edits[target.post_edit_count++] = operation;
    prepend = destination < 0 ? (size_t)(-destination) : 0u;
    placed_first = destination < 0 ? 0u : (size_t)destination + prepend;
    target.selection_first = placed_first;
    target.selection_last = placed_first + length;
    target.has_selection = 1;
    if (prepend > 0u && target.has_loop) {
        target.loop_first += prepend;
        target.loop_last += prepend;
    }
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    replace_current_preserving_view(instrument, &current);
    instrument->selection_first = target.selection_first;
    instrument->selection_last = target.selection_last;
    instrument->has_selection = 1;
    instrument->loop_first = target.loop_first;
    instrument->loop_last = target.loop_last;
    memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
    if (!bank_sync_selected(instrument, error, error_size)) return 0;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_count(const TsInstrument *instrument)
{
    int count = 0;
    if (instrument == NULL) return 0;
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i)
        if (instrument->bank[i].occupied) ++count;
    return count;
}

int ts_instrument_bank_first_empty(const TsInstrument *instrument)
{
    if (instrument == NULL) return -1;
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i)
        if (!instrument->bank[i].occupied) return i;
    return -1;
}

static TsTuning tuning_shifted(TsTuning source, float semitones)
{
    double note = (double)source.root_note +
                  (double)source.fine_tune_cents / 100.0 + semitones;
    int root = (int)floor(note + 0.5);
    if (root < 0) root = 0;
    if (root > 127) root = 127;
    source.root_note = root;
    source.fine_tune_cents = (float)((note - root) * 100.0);
    if (source.fine_tune_cents < -100.0f) source.fine_tune_cents = -100.0f;
    if (source.fine_tune_cents > 100.0f) source.fine_tune_cents = 100.0f;
    return source;
}

static int family_mutate_sample(TsSample *destination, const TsSample *source,
                                TsFamilyRelation relation, uint32_t seed,
                                float mutation, uint32_t locks,
                                char *error, size_t error_size)
{
    uint32_t rng = seed;
    float relation_scale = relation == TS_FAMILY_CHILD ? 0.34f : 0.72f;
    float strength = clampf(0.06f + mutation * relation_scale, 0.02f, 0.92f);
    size_t frames = source->frames;
    float *data;
    float envelope = 0.0f;
    float prior_noise = 0.0f;
    double phase = rng_unit(&rng) * 2.0 * M_PI;
    double modulation_hz = 0.4 + rng_unit(&rng) * (9.0 + strength * 31.0);
    if ((locks & TS_FAMILY_LOCK_DURATION) == 0u) {
        float maximum = relation == TS_FAMILY_CHILD ? 0.18f : 0.42f;
        float scale = 1.0f + rng_bipolar(&rng) * maximum * mutation;
        frames = (size_t)lrintf((float)source->frames * clampf(scale, 0.55f, 1.55f));
        if (frames < 32u) frames = 32u;
    }
    if (frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Variation is too large");
        return 0;
    }
    data = (float *)malloc(frames * sizeof(float));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while creating variation");
        return 0;
    }
    for (size_t i = 0; i < frames; ++i) {
        size_t source_at;
        float base;
        float random = rng_bipolar(&rng);
        float colored;
        float wet;
        float value;
        if (i < source->frames) source_at = i;
        else {
            size_t tail_first = source->frames > 8u ? source->frames / 2u : 0u;
            size_t tail_frames = source->frames - tail_first;
            source_at = tail_first + (tail_frames > 0u ?
                        (i - tail_first) % tail_frames : 0u);
        }
        base = source->data[source_at];
        envelope += (fabsf(base) - envelope) * 0.0125f;
        colored = random - prior_noise * 0.82f;
        prior_noise = random;
        phase += 2.0 * M_PI * modulation_hz / (double)source->sample_rate;
        if ((locks & TS_FAMILY_LOCK_SPECTRAL) != 0u) {
            value = base * (1.0f + sinf((float)phase) * strength * 0.16f);
        } else {
            float drive = 1.0f + strength * 6.0f;
            float shaped = tanhf(base * drive) / tanhf(drive);
            wet = shaped * 0.82f + colored * strength * 0.28f;
            value = base * (1.0f - strength) + wet * strength;
        }
        if ((locks & TS_FAMILY_LOCK_PITCH) == 0u)
            value *= 1.0f + sinf((float)phase * 0.37f) * strength * 0.23f;
        if ((locks & TS_FAMILY_LOCK_ENVELOPE) != 0u) {
            float limit = envelope * 1.8f + 0.002f;
            value = clampf(value, -limit, limit);
        }
        data[i] = clampf(value, -1.0f, 1.0f);
    }
    if (frames > 16u) {
        size_t fade = frames < 256u ? frames / 8u : 32u;
        for (size_t i = 0; i < fade; ++i)
            data[frames - 1u - i] *= (float)i / (float)fade;
    }
    {
        float source_peak = ts_sample_peak(source);
        float output_peak = 0.0f;
        for (size_t i = 0; i < frames; ++i)
            if (fabsf(data[i]) > output_peak) output_peak = fabsf(data[i]);
        if (source_peak > 0.0000001f && output_peak > 0.0000001f) {
            float gain = source_peak / output_peak;
            for (size_t i = 0; i < frames; ++i)
                data[i] = clampf(data[i] * gain, -1.0f, 1.0f);
        }
    }
    ts_sample_free(destination);
    destination->data = data;
    destination->frames = frames;
    destination->sample_rate = source->sample_rate;
    set_error(error, error_size, "");
    return 1;
}

static void family_copy_or_vary_loop(TsBankSlot *candidate,
                                     const TsBankSlot *anchor,
                                     uint32_t seed)
{
    uint32_t rng = seed;
    if ((candidate->lineage_locks & TS_FAMILY_LOCK_LOOP) != 0u) {
        if (!anchor->has_loop) return;
        candidate->has_loop = 1;
        candidate->loop_first = anchor->loop_first * candidate->sample.frames /
                                anchor->sample.frames;
        candidate->loop_last = anchor->loop_last * candidate->sample.frames /
                               anchor->sample.frames;
        candidate->loop_mode = anchor->loop_mode;
        candidate->loop_crossfade_ms = anchor->loop_crossfade_ms;
    } else if (anchor->has_loop && rng_unit(&rng) > candidate->lineage_mutation * 0.35f) {
        size_t first = anchor->loop_first * candidate->sample.frames /
                       anchor->sample.frames;
        size_t last = anchor->loop_last * candidate->sample.frames /
                      anchor->sample.frames;
        size_t span = last > first ? last - first : 0u;
        size_t wander = (size_t)((float)span * candidate->lineage_mutation * 0.16f);
        if (wander > 0u) {
            size_t before = (size_t)(rng_unit(&rng) * (float)wander);
            size_t after = (size_t)(rng_unit(&rng) * (float)wander);
            first = first > before ? first - before : 0u;
            last += after;
            if (last > candidate->sample.frames) last = candidate->sample.frames;
        }
        if (last > first + 1u) {
            candidate->has_loop = 1;
            candidate->loop_first = first;
            candidate->loop_last = last;
            candidate->loop_mode = (TsLoopMode)(rng_next(&rng) % TS_LOOP_MODE_COUNT);
            candidate->loop_crossfade_ms = anchor->loop_crossfade_ms;
        }
    }
}

static int bank_store_edit_state(TsBankSlot *slot, const TsInstrument *instrument,
                                 char *error, size_t error_size)
{
    TsEditSnapshot *undo = NULL, *redo = NULL;
    if (instrument->undo_count > 0) {
        undo = malloc((size_t)instrument->undo_count * sizeof(*undo));
        if (undo == NULL) { set_error(error, error_size, "Out of memory saving tile Undo"); return 0; }
        memcpy(undo, instrument->undo, (size_t)instrument->undo_count * sizeof(*undo));
    }
    if (instrument->redo_count > 0) {
        redo = malloc((size_t)instrument->redo_count * sizeof(*redo));
        if (redo == NULL) { free(undo); set_error(error, error_size, "Out of memory saving tile Redo"); return 0; }
        memcpy(redo, instrument->redo, (size_t)instrument->redo_count * sizeof(*redo));
    }
    free(slot->undo); free(slot->redo);
    slot->undo = undo; slot->redo = redo;
    slot->edit = snapshot(instrument);
    slot->process = instrument->process;
    slot->tuning = instrument->tuning;
    slot->audible_tuning = instrument->audible_tuning;
    slot->has_loop = instrument->has_loop;
    slot->loop_first = instrument->loop_first;
    slot->loop_last = instrument->loop_last;
    slot->loop_mode = instrument->loop_mode;
    slot->loop_crossfade_ms = instrument->loop_crossfade_ms;
    slot->undo_count = instrument->undo_count;
    slot->redo_count = instrument->redo_count;
    return 1;
}

static int bank_sync_selected(TsInstrument *instrument, char *error, size_t error_size)
{
    TsBankSlot *slot;
    TsSample current, parent;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[instrument->selected_slot].occupied) return 1;
    slot = &instrument->bank[instrument->selected_slot];
    ts_sample_init(&current); ts_sample_init(&parent);
    if (!ts_sample_clone(&current, &instrument->current, error, error_size) ||
        !ts_sample_clone(&parent, &instrument->parent, error, error_size)) {
        ts_sample_free(&current); ts_sample_free(&parent); return 0;
    }
    if (!bank_store_edit_state(slot, instrument, error, error_size)) {
        ts_sample_free(&current); ts_sample_free(&parent); return 0;
    }
    ts_sample_free(&slot->sample); ts_sample_free(&slot->edit_parent);
    slot->sample = current; slot->edit_parent = parent;
    return 1;
}

static int bank_slot_deep_clone(TsBankSlot *destination, const TsBankSlot *source,
                                char *error, size_t error_size)
{
    TsBankSlot copy = *source;
    ts_sample_init(&copy.sample); ts_sample_init(&copy.edit_parent);
    copy.undo = NULL; copy.redo = NULL;
    if (!ts_sample_clone(&copy.sample, &source->sample, error, error_size) ||
        !ts_sample_clone(&copy.edit_parent, source->edit_parent.data != NULL ?
                         &source->edit_parent : &source->sample, error, error_size)) goto failed;
    if (source->undo_count > 0) {
        copy.undo = malloc((size_t)source->undo_count * sizeof(*copy.undo));
        if (copy.undo == NULL) goto failed;
        memcpy(copy.undo, source->undo, (size_t)source->undo_count * sizeof(*copy.undo));
    }
    if (source->redo_count > 0) {
        copy.redo = malloc((size_t)source->redo_count * sizeof(*copy.redo));
        if (copy.redo == NULL) goto failed;
        memcpy(copy.redo, source->redo, (size_t)source->redo_count * sizeof(*copy.redo));
    }
    bank_slot_free(destination); *destination = copy; return 1;
failed:
    ts_sample_free(&copy.sample); ts_sample_free(&copy.edit_parent);
    free(copy.undo); free(copy.redo);
    set_error(error, error_size, "Out of memory deep-copying tile state"); return 0;
}

int ts_instrument_select_bank(TsInstrument *instrument, int slot,
                              char *error, size_t error_size)
{
    TsSample parent, current;
    TsProcessRecipe neutral;
    TsBankSlot *chosen;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank tile"); return 0;
    }
    if (instrument->selected_slot != slot &&
        !bank_sync_selected(instrument, error, error_size)) return 0;
    instrument->selected_slot = slot;
    instrument->family_anchor_slot = slot;
    chosen = &instrument->bank[slot];
    if (!chosen->occupied) {
        ts_sample_free(&instrument->parent); ts_sample_init(&instrument->parent);
        ts_sample_free(&instrument->current); ts_sample_init(&instrument->current);
        reset_editor(instrument);
        set_error(error, error_size, ""); return 1;
    }
    ts_sample_init(&parent); ts_sample_init(&current);
    if (!ts_sample_clone(&parent, chosen->edit_parent.data != NULL ?
                         &chosen->edit_parent : &chosen->sample, error, error_size) ||
        !ts_sample_clone(&current, &chosen->sample, error, error_size)) {
        ts_sample_free(&parent); ts_sample_free(&current); return 0;
    }
    ts_sample_free(&instrument->parent); ts_sample_free(&instrument->current);
    instrument->parent = parent; instrument->current = current;
    instrument->generator = chosen->generator;
    instrument->tuning = chosen->tuning; instrument->audible_tuning = chosen->audible_tuning;
    ts_process_recipe_reset(&neutral); instrument->process = neutral;
    if (chosen->edit.crop_last > chosen->edit.crop_first &&
        chosen->edit.crop_last <= instrument->parent.frames) {
        TsEditSnapshot state = chosen->edit;
        instrument->crop_first = state.crop_first; instrument->crop_last = state.crop_last;
        instrument->selection_first = state.selection_first; instrument->selection_last = state.selection_last;
        instrument->view_first = state.view_first; instrument->view_last = state.view_last;
        instrument->loop_first = state.loop_first; instrument->loop_last = state.loop_last;
        instrument->loop_crossfade_ms = state.loop_crossfade_ms; instrument->loop_mode = state.loop_mode;
        instrument->has_selection = state.has_selection; instrument->has_loop = state.has_loop;
        instrument->process = state.process;
        memcpy(instrument->sample_edits, state.sample_edits, sizeof(instrument->sample_edits));
        instrument->sample_edit_count = state.sample_edit_count;
        memcpy(instrument->post_edits, state.post_edits, sizeof(instrument->post_edits));
        instrument->post_edit_count = state.post_edit_count;
    } else reset_editor(instrument);
    instrument->undo_count = chosen->undo_count; instrument->redo_count = chosen->redo_count;
    if (chosen->undo_count > 0) memcpy(instrument->undo, chosen->undo, (size_t)chosen->undo_count * sizeof(*chosen->undo));
    if (chosen->redo_count > 0) memcpy(instrument->redo, chosen->redo, (size_t)chosen->redo_count * sizeof(*chosen->redo));
    set_error(error, error_size, ""); return 1;
}

int ts_instrument_create_selected(TsInstrument *instrument, uint32_t seed,
                                  char *error, size_t error_size)
{
    TsBankSlot made;
    TsGeneratorRecipe recipe;
    uint32_t rng = seed;
    int slot;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Select a bank tile before Create"); return 0;
    }
    slot = instrument->selected_slot;
    bank_slot_init(&made);
    recipe = instrument->generator;
    recipe.kind = TS_GENERATOR_FM; recipe.seed = seed;
    recipe.seconds = 0.1f + rng_unit(&rng) * 7.9f;
    recipe.frequency = 30.0f * powf(2000.0f / 30.0f, rng_unit(&rng));
    if (!ts_sample_generate(&made.sample, &recipe, error, error_size) ||
        !ts_sample_clone(&made.edit_parent, &made.sample, error, error_size)) {
        bank_slot_free(&made); return 0;
    }
    made.occupied = 1; made.capture_kind = TS_BANK_CAPTURE_CURRENT;
    made.relation = TS_FAMILY_ROOT; made.parent_slot = -1;
    made.generator = recipe; made.has_generator = 1; made.lineage_seed = seed;
    made.lineage_mutation = instrument->family_mutation;
    made.tuning = generator_tuning(&recipe); made.audible_tuning = made.tuning;
    bank_slot_free(&instrument->bank[slot]); instrument->bank[slot] = made;
    instrument->generator = recipe; instrument->source_kind = TS_SOURCE_GENERATED;
    ++instrument->family_sequence;
    return ts_instrument_select_bank(instrument, slot, error, error_size);
}

int ts_instrument_copy_selected(TsInstrument *instrument, int destination_slot,
                                char *error, size_t error_size)
{
    int source;
    if (instrument == NULL || destination_slot < 0 || destination_slot >= TS_BANK_SLOT_COUNT ||
        instrument->bank[destination_slot].occupied) {
        set_error(error, error_size, "Clone needs an empty destination tile"); return 0;
    }
    source = instrument->selected_slot;
    if (source < 0 || source >= TS_BANK_SLOT_COUNT || !instrument->bank[source].occupied) {
        set_error(error, error_size, "Select an occupied tile before Clone"); return 0;
    }
    if (!ts_instrument_select_bank(instrument, source, error, error_size) ||
        !bank_sync_selected(instrument, error, error_size)) return 0;
    if (!bank_slot_deep_clone(&instrument->bank[destination_slot], &instrument->bank[source],
                              error, error_size)) return 0;
    instrument->bank[destination_slot].parent_slot = source;
    return ts_instrument_select_bank(instrument, destination_slot, error, error_size);
}

int ts_instrument_vary_selected(TsInstrument *instrument, int chain,
                                int *destination_slot, char *error, size_t error_size)
{
    TsBankSlot made;
    TsFmPatch source_patch;
    TsGeneratorRecipe recipe;
    uint32_t seed;
    int source, destination, steps;
    if (destination_slot != NULL) *destination_slot = -1;
    if (instrument == NULL || (source = instrument->selected_slot) < 0 ||
        source >= TS_BANK_SLOT_COUNT || !instrument->bank[source].occupied ||
        !instrument->bank[source].has_generator ||
        instrument->bank[source].generator.kind != TS_GENERATOR_FM) {
        set_error(error, error_size, "Vary needs a selected FM tile"); return 0;
    }
    destination = source;
    if (chain) {
        destination = -1;
        for (int n = 1; n < TS_BANK_SLOT_COUNT; ++n) {
            int candidate = (source + n) % TS_BANK_SLOT_COUNT;
            if (!instrument->bank[candidate].occupied) { destination = candidate; break; }
        }
        if (destination < 0) { set_error(error, error_size, "Bank is full - Chain cannot vary"); return 0; }
    }
    if (!ts_instrument_select_bank(instrument, source, error, error_size) ||
        !bank_sync_selected(instrument, error, error_size)) return 0;
    if (instrument->family_mutation <= 0.0f) {
        if (!chain) { if (destination_slot) *destination_slot = source; return 1; }
        if (!bank_slot_deep_clone(&instrument->bank[destination], &instrument->bank[source], error, error_size)) return 0;
    } else {
        TsSample fm_source, rebuilt;
        TsInstrument replay;
        recipe = instrument->bank[source].generator;
        ts_fm_patch_from_recipe(&recipe, &source_patch);
        seed = advance_seed(instrument->bank[source].lineage_seed ^
                            advance_seed(instrument->family_sequence + 1u));
        steps = 1 + (int)lrintf(instrument->family_mutation * 31.0f);
        for (int step = 1; step < steps; ++step) seed = advance_seed(seed);
        recipe.kind = TS_GENERATOR_FM;
        recipe.has_fm_patch = 1;
        ts_fm_patch_vary(&source_patch, seed, instrument->family_mutation,
                         &recipe.fm_patch);
        ts_sample_init(&fm_source); ts_sample_init(&rebuilt);
        if (!ts_sample_generate(&fm_source, &recipe, error, error_size)) return 0;
        replay = *instrument;
        replay.parent = fm_source;
        if (!render_snapshot(&rebuilt, &replay, &instrument->bank[source].edit,
                             error, error_size)) {
            ts_sample_free(&fm_source); return 0;
        }
        bank_slot_init(&made);
        if (!bank_slot_deep_clone(&made, &instrument->bank[source], error, error_size)) {
            ts_sample_free(&fm_source); ts_sample_free(&rebuilt); return 0;
        }
        ts_sample_free(&made.sample); ts_sample_free(&made.edit_parent);
        made.sample = rebuilt; made.edit_parent = fm_source;
        made.generator = recipe; made.lineage_seed = seed; made.parent_slot = source;
        made.lineage_mutation = instrument->family_mutation; made.trajectory_step++;
        if (destination == source) bank_slot_free(&instrument->bank[source]);
        instrument->bank[destination] = made;
    }
    ++instrument->family_sequence;
    if (destination_slot) *destination_slot = destination;
    return ts_instrument_select_bank(instrument, destination, error, error_size);
}

int ts_instrument_generate_family_candidate(TsInstrument *instrument,
                                            int anchor_slot, int reseed,
                                            int *created_slot,
                                            char *error, size_t error_size)
{
    TsBankSlot candidate;
    const TsBankSlot *anchor;
    TsFamilyRelation relation;
    uint32_t locks;
    uint32_t seed;
    uint32_t rng;
    float mutation;
    int slot;
    if (created_slot != NULL) *created_slot = -1;
    if (instrument == NULL || instrument->parent.data == NULL) {
        set_error(error, error_size, "Create material before making variations");
        return 0;
    }
    slot = ts_instrument_bank_first_empty(instrument);
    if (slot < 0) {
        set_error(error, error_size, "Sound collection is full - clear a slot first");
        return 0;
    }
    if (instrument->family_trajectory && instrument->family_last_slot >= 0 &&
        instrument->family_last_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[instrument->family_last_slot].occupied)
        anchor_slot = instrument->family_last_slot;
    if (anchor_slot < 0 || anchor_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[anchor_slot].occupied)
        anchor_slot = instrument->family_anchor_slot;
    if (anchor_slot < 0 || anchor_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[anchor_slot].occupied)
        anchor_slot = 0;
    relation = instrument->family_relation;
    locks = instrument->family_locks & TS_FAMILY_LOCK_ALL;
    mutation = clampf(instrument->family_mutation, 0.0f, 1.0f);
    if (relation != TS_FAMILY_CHILD && relation != TS_FAMILY_COUSIN &&
        relation != TS_FAMILY_STRANGER)
        relation = TS_FAMILY_CHILD;
    if (reseed && instrument->family_last_slot > 0 &&
        instrument->family_last_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[instrument->family_last_slot].occupied) {
        const TsBankSlot *last = &instrument->bank[instrument->family_last_slot];
        if (!instrument->family_trajectory && last->parent_slot >= 0 &&
            last->parent_slot < TS_BANK_SLOT_COUNT &&
            instrument->bank[last->parent_slot].occupied)
            anchor_slot = last->parent_slot;
        relation = last->relation;
        locks = last->lineage_locks;
        mutation = last->lineage_mutation;
    }
    anchor = &instrument->bank[anchor_slot];
    seed = reseed && instrument->family_last_slot > 0 &&
           instrument->bank[instrument->family_last_slot].occupied ?
           advance_seed(instrument->bank[instrument->family_last_slot].lineage_seed) :
           advance_seed(anchor->lineage_seed ^ 0x9e3779b9u ^
                        advance_seed(instrument->family_sequence + 1u));
    rng = seed;
    bank_slot_init(&candidate);
    candidate.capture_kind = TS_BANK_CAPTURE_CURRENT;
    candidate.relation = relation;
    candidate.parent_slot = anchor_slot;
    candidate.lineage_seed = seed;
    candidate.lineage_locks = locks;
    candidate.lineage_mutation = mutation;
    candidate.trajectory_step = instrument->family_trajectory ?
                                anchor->trajectory_step + 1u : 0u;
    if (relation == TS_FAMILY_STRANGER) {
        const TsBankSlot *last = reseed && instrument->family_last_slot > 0 &&
                                 instrument->family_last_slot < TS_BANK_SLOT_COUNT &&
                                 instrument->bank[instrument->family_last_slot].occupied ?
                                 &instrument->bank[instrument->family_last_slot] : NULL;
        TsGeneratorRecipe recipe = last != NULL && last->has_generator ?
                                   last->generator : anchor->has_generator ?
                                   anchor->generator : instrument->generator;
        double anchor_frequency = ts_tuning_frequency(&anchor->audible_tuning);
        float duration = (float)anchor->sample.frames /
                         (float)anchor->sample.sample_rate;
        if (!reseed)
            recipe.kind = (TsGeneratorKind)((recipe.kind + 1) % TS_GENERATOR_COUNT);
        recipe.seed = seed;
        if ((locks & TS_FAMILY_LOCK_DURATION) != 0u)
            recipe.seconds = duration;
        else
            recipe.seconds = duration * (0.55f + rng_unit(&rng) * 1.25f);
        if ((locks & TS_FAMILY_LOCK_PITCH) != 0u)
            recipe.frequency = (float)(anchor_frequency /
                               generator_pitch_ratio(recipe.seed));
        else
            recipe.frequency = (float)(anchor_frequency *
                               pow(2.0, rng_bipolar(&rng) * mutation));
        recipe.seconds = clampf(recipe.seconds, 0.1f, 8.0f);
        recipe.frequency = clampf(recipe.frequency, 30.0f, 2000.0f);
        if (!ts_sample_generate(&candidate.sample, &recipe, error, error_size))
            return 0;
        candidate.generator = recipe;
        candidate.has_generator = 1;
        candidate.tuning = (locks & TS_FAMILY_LOCK_PITCH) != 0u ?
                           anchor->tuning : generator_tuning(&recipe);
        candidate.audible_tuning = (locks & TS_FAMILY_LOCK_PITCH) != 0u ?
                                   anchor->audible_tuning : candidate.tuning;
    } else {
        float semitone_shift = rng_bipolar(&rng) * mutation *
                               (relation == TS_FAMILY_CHILD ? 3.0f : 9.0f);
        if (!family_mutate_sample(&candidate.sample, &anchor->sample,
                                  relation, seed, mutation, locks,
                                  error, error_size))
            return 0;
        candidate.generator = anchor->generator;
        candidate.has_generator = anchor->has_generator;
        candidate.tuning = (locks & TS_FAMILY_LOCK_PITCH) != 0u ?
                           anchor->tuning : tuning_shifted(anchor->tuning,
                                                          semitone_shift);
        candidate.audible_tuning = (locks & TS_FAMILY_LOCK_PITCH) != 0u ?
                                   anchor->audible_tuning :
                                   tuning_shifted(anchor->audible_tuning,
                                                  semitone_shift);
    }
    if (relation == TS_FAMILY_STRANGER && candidate.has_generator) {
        if (candidate.generator.kind != TS_GENERATOR_FM)
            snprintf(candidate.sample.name, sizeof(candidate.sample.name),
                     "%s RAD %02u %08X",
                     ts_generator_name(candidate.generator.kind),
                     candidate.trajectory_step, seed);
        /* FM already carries its structure and ratio family in the name. */
    } else {
        snprintf(candidate.sample.name, sizeof(candidate.sample.name), "%s %02u %08X",
                 ts_family_relation_name(relation), candidate.trajectory_step, seed);
    }
    family_copy_or_vary_loop(&candidate, anchor, seed);
    candidate.occupied = 1;
    instrument->bank[slot] = candidate;
    instrument->family_last_slot = slot;
    instrument->family_anchor_slot = instrument->family_trajectory ? slot : anchor_slot;
    ++instrument->family_sequence;
    if (created_slot != NULL) *created_slot = slot;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_capture(TsInstrument *instrument, int slot,
                               TsBankCaptureKind kind, char *error, size_t error_size)
{
    TsBankSlot captured;
    size_t first = 0;
    size_t last;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current sample to capture");
        return 0;
    }
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank slot");
        return 0;
    }
    if (instrument->bank[slot].occupied) {
        set_error(error, error_size, "Clear the occupied bank slot before capturing");
        return 0;
    }
    last = instrument->current.frames;
    if (kind == TS_BANK_CAPTURE_SELECTION) {
        if (!instrument->has_selection ||
            instrument->selection_last <= instrument->selection_first) {
            set_error(error, error_size, "Select a range before capturing Selection");
            return 0;
        }
        first = instrument->selection_first;
        last = instrument->selection_last;
    } else if (kind == TS_BANK_CAPTURE_LOOP) {
        if (!instrument->has_loop || instrument->loop_last <= instrument->loop_first) {
            set_error(error, error_size, "Set a loop before capturing Loop");
            return 0;
        }
        first = instrument->loop_first;
        last = instrument->loop_last;
    } else if (kind != TS_BANK_CAPTURE_CURRENT) {
        set_error(error, error_size, "Unsupported bank capture type");
        return 0;
    }
    bank_slot_init(&captured);
    if (!sample_clone_range(&captured.sample, &instrument->current,
                            first, last, error, error_size)) return 0;
    snprintf(captured.sample.name, sizeof(captured.sample.name), "%s G%02u",
             ts_bank_capture_name(kind), instrument->generation);
    captured.capture_kind = kind;
    captured.relation = TS_FAMILY_CAPTURED;
    captured.parent_slot = instrument->family_anchor_slot;
    captured.lineage_seed = advance_seed(instrument->process.seed ^
                                         (uint32_t)ts_sample_hash(&captured.sample));
    captured.lineage_locks = TS_FAMILY_LOCK_ALL;
    captured.lineage_mutation = 0.0f;
    if (captured.parent_slot >= 0 && captured.parent_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[captured.parent_slot].occupied &&
        instrument->bank[captured.parent_slot].has_generator) {
        captured.generator = instrument->bank[captured.parent_slot].generator;
        captured.has_generator = 1;
    }
    captured.tuning = instrument->tuning;
    captured.audible_tuning = instrument->audible_tuning;
    captured.loop_mode = instrument->loop_mode;
    captured.occupied = 1;
    captured.loop_crossfade_ms = instrument->loop_crossfade_ms;
    if (kind == TS_BANK_CAPTURE_LOOP) {
        captured.has_loop = 1;
        captured.loop_first = 0;
        captured.loop_last = captured.sample.frames;
    } else if (kind == TS_BANK_CAPTURE_CURRENT && instrument->has_loop) {
        captured.has_loop = 1;
        captured.loop_first = instrument->loop_first;
        captured.loop_last = instrument->loop_last;
    }
    instrument->bank[slot] = captured;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_clear(TsInstrument *instrument, int slot,
                             char *error, size_t error_size)
{
    int was_selected;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank slot");
        return 0;
    }
    if (!instrument->bank[slot].occupied) {
        set_error(error, error_size, "Bank slot is already empty");
        return 0;
    }
    was_selected = instrument->selected_slot == slot;
    bank_slot_free(&instrument->bank[slot]);
    if (instrument->family_last_slot == slot) instrument->family_last_slot = -1;
    if (instrument->family_anchor_slot == slot) {
        instrument->family_anchor_slot = instrument->selected_slot != slot &&
                                         instrument->selected_slot >= 0 &&
                                         instrument->selected_slot < TS_BANK_SLOT_COUNT &&
                                         instrument->bank[instrument->selected_slot].occupied ?
                                         instrument->selected_slot : 0;
        for (int candidate = 0; candidate < TS_BANK_SLOT_COUNT; ++candidate)
            if (instrument->bank[candidate].occupied) {
                instrument->family_anchor_slot = candidate;
                break;
            }
    }
    if (was_selected)
        return ts_instrument_select_bank(instrument, slot, error, error_size);
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_clear_all(TsInstrument *instrument,
                                 char *error, size_t error_size)
{
    if (instrument == NULL) { set_error(error, error_size, "No collection to clear"); return 0; }
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        bank_slot_free(&instrument->bank[slot]);
    ts_sample_free(&instrument->parent); ts_sample_init(&instrument->parent);
    ts_sample_free(&instrument->current); ts_sample_init(&instrument->current);
    instrument->selected_slot = 0;
    instrument->family_anchor_slot = 0;
    instrument->family_last_slot = -1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_rename(TsInstrument *instrument, int slot, const char *name,
                              char *error, size_t error_size)
{
    const char *first;
    const char *last;
    size_t length;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank slot");
        return 0;
    }
    if (!instrument->bank[slot].occupied) {
        set_error(error, error_size, "Capture audio before naming this bank slot");
        return 0;
    }
    if (name == NULL) {
        set_error(error, error_size, "Enter a bank slot name");
        return 0;
    }
    first = name;
    while (*first == ' ' || *first == '\t' || *first == '\r' || *first == '\n') ++first;
    last = first + strlen(first);
    while (last > first && (last[-1] == ' ' || last[-1] == '\t' ||
                            last[-1] == '\r' || last[-1] == '\n')) --last;
    length = (size_t)(last - first);
    if (length == 0) {
        set_error(error, error_size, "Bank slot name cannot be empty");
        return 0;
    }
    if (length >= sizeof(instrument->bank[slot].sample.name)) {
        set_error(error, error_size, "Bank slot name is too long");
        return 0;
    }
    memmove(instrument->bank[slot].sample.name, first, length);
    instrument->bank[slot].sample.name[length] = '\0';
    set_error(error, error_size, "");
    return 1;
}

static TsBankSlot *editable_bank_slot(TsInstrument *instrument, int slot,
                                      char *error, size_t error_size)
{
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank slot");
        return NULL;
    }
    if (!instrument->bank[slot].occupied || instrument->bank[slot].sample.data == NULL ||
        instrument->bank[slot].sample.frames < 2u) {
        set_error(error, error_size, "Capture audio before editing this bank loop");
        return NULL;
    }
    return &instrument->bank[slot];
}

int ts_instrument_bank_set_loop_full(TsInstrument *instrument, int slot,
                                     char *error, size_t error_size)
{
    TsBankSlot *bank_slot = editable_bank_slot(instrument, slot, error, error_size);
    if (bank_slot == NULL) return 0;
    bank_slot->loop_first = 0;
    bank_slot->loop_last = bank_slot->sample.frames;
    bank_slot->has_loop = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_clear_loop(TsInstrument *instrument, int slot,
                                  char *error, size_t error_size)
{
    TsBankSlot *bank_slot = editable_bank_slot(instrument, slot, error, error_size);
    if (bank_slot == NULL) return 0;
    if (!bank_slot->has_loop) {
        set_error(error, error_size, "Bank slot has no loop to clear");
        return 0;
    }
    bank_slot->loop_first = 0;
    bank_slot->loop_last = 0;
    bank_slot->has_loop = 0;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_set_loop_crossfade(TsInstrument *instrument, int slot,
                                          float milliseconds,
                                          char *error, size_t error_size)
{
    TsBankSlot *bank_slot = editable_bank_slot(instrument, slot, error, error_size);
    if (bank_slot == NULL) return 0;
    milliseconds = clampf(milliseconds, 0.0f, 50.0f);
    if (fabsf(milliseconds - bank_slot->loop_crossfade_ms) < 0.0001f) {
        set_error(error, error_size, "Bank loop crossfade unchanged");
        return 0;
    }
    bank_slot->loop_crossfade_ms = milliseconds;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_set_loop_mode(TsInstrument *instrument, int slot,
                                     TsLoopMode mode,
                                     char *error, size_t error_size)
{
    TsBankSlot *bank_slot = editable_bank_slot(instrument, slot, error, error_size);
    if (bank_slot == NULL) return 0;
    if (mode < TS_LOOP_FORWARD || mode >= TS_LOOP_MODE_COUNT) {
        set_error(error, error_size, "Invalid bank loop direction mode");
        return 0;
    }
    if (bank_slot->loop_mode == mode) {
        set_error(error, error_size, "Bank loop mode unchanged");
        return 0;
    }
    bank_slot->loop_mode = mode;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_move_loop_endpoint(TsInstrument *instrument, int slot,
                                          int endpoint, size_t frame)
{
    TsBankSlot *bank_slot = editable_bank_slot(instrument, slot, NULL, 0);
    size_t snapped;
    if (bank_slot == NULL || !bank_slot->has_loop ||
        (endpoint != 1 && endpoint != 2)) return 0;
    snapped = ts_sample_nearest_zero_crossing(&bank_slot->sample, frame);
    if (endpoint == 1) {
        if (snapped < bank_slot->loop_last) bank_slot->loop_first = snapped;
        else if (snapped == bank_slot->loop_last) return endpoint;
        else {
            bank_slot->loop_first = bank_slot->loop_last;
            bank_slot->loop_last = snapped;
            endpoint = 2;
        }
    } else {
        if (snapped > bank_slot->loop_first) bank_slot->loop_last = snapped;
        else if (snapped == bank_slot->loop_first) return endpoint;
        else {
            bank_slot->loop_last = bank_slot->loop_first;
            bank_slot->loop_first = snapped;
            endpoint = 1;
        }
    }
    return endpoint;
}

int ts_instrument_set_bank_as_current(TsInstrument *instrument, int slot,
                                      char *error, size_t error_size)
{
    TsSample parent;
    TsSample current;
    TsProcessRecipe neutral;
    const TsBankSlot *source;
    uint64_t prior_hash;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsLoopMode loop_mode;
    int has_loop;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[slot].occupied) {
        set_error(error, error_size, "Audition a filled bank slot first");
        return 0;
    }
    source = &instrument->bank[slot];
    ts_sample_init(&parent);
    ts_sample_init(&current);
    if (!ts_sample_clone(&parent, &source->sample, error, error_size)) return 0;
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument->process.seed;
    if (!ts_sample_process(&current, &parent, 0, parent.frames,
                           &neutral, error, error_size)) {
        ts_sample_free(&parent);
        return 0;
    }
    prior_hash = ts_sample_hash(&instrument->parent);
    loop_first = source->loop_first;
    loop_last = source->loop_last;
    loop_crossfade_ms = source->loop_crossfade_ms;
    loop_mode = source->loop_mode;
    has_loop = source->has_loop;
    ts_sample_free(&instrument->parent);
    ts_sample_free(&instrument->current);
    instrument->parent = parent;
    instrument->current = current;
    instrument->source_kind = TS_SOURCE_COMMITTED;
    instrument->process = neutral;
    instrument->tuning = source->tuning;
    instrument->audible_tuning = source->audible_tuning;
    if (source->has_generator) instrument->generator = source->generator;
    instrument->ancestor_hash = prior_hash;
    ++instrument->generation;
    reset_editor(instrument);
    instrument->loop_first = loop_first;
    instrument->loop_last = loop_last;
    instrument->loop_crossfade_ms = loop_crossfade_ms;
    instrument->loop_mode = loop_mode;
    instrument->has_loop = has_loop && loop_first < loop_last &&
                           loop_last <= current.frames;
    instrument->family_anchor_slot = slot;
    set_error(error, error_size, "");
    return 1;
}

static void bank_safe_name(const char *source, char *destination, size_t size)
{
    size_t used = 0;
    if (size == 0) return;
    while (source != NULL && *source != '\0' && used + 1u < size) {
        unsigned char c = (unsigned char)*source++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            destination[used++] = (char)c;
        else if (used > 0 && destination[used - 1u] != '_')
            destination[used++] = '_';
    }
    while (used > 0 && destination[used - 1u] == '_') --used;
    destination[used] = '\0';
    if (used == 0) snprintf(destination, size, "sample");
}

int ts_instrument_family_folder_name(const TsInstrument *instrument,
                                     char *name, size_t name_size)
{
    const char *source;
    const char *extension;
    char stem[128];
    size_t length;
    if (instrument == NULL || name == NULL || name_size == 0) return 0;
    source = instrument->selected_slot >= 0 &&
             instrument->selected_slot < TS_BANK_SLOT_COUNT &&
             instrument->bank[instrument->selected_slot].occupied ?
             instrument->bank[instrument->selected_slot].sample.name :
             instrument->parent.name;
    extension = strrchr(source, '.');
    length = extension != NULL && extension > source ?
             (size_t)(extension - source) : strlen(source);
    if (length >= sizeof(stem)) length = sizeof(stem) - 1u;
    memcpy(stem, source, length);
    stem[length] = '\0';
    bank_safe_name(stem, stem, sizeof(stem));
    {
        int written = snprintf(name, name_size, "%s_set", stem);
        return written >= 0 && (size_t)written < name_size;
    }
}

static int export_path_exists(const char *path)
{
    struct stat info;
    return path != NULL && stat(path, &info) == 0;
}

int ts_instrument_next_family_path(const TsInstrument *instrument,
                                   const char *directory,
                                   char *path, size_t path_size,
                                   char *error, size_t error_size)
{
    char base[256];
    char name[256];
    size_t directory_length;
    int suffix = 1;
    int written;
    if (directory == NULL || directory[0] == '\0' || path == NULL || path_size == 0 ||
        !ts_instrument_family_folder_name(instrument, base, sizeof(base))) {
        set_error(error, error_size, "Invalid collection handoff path");
        return 0;
    }
    directory_length = strlen(directory);
    snprintf(name, sizeof(name), "%s", base);
    for (;;) {
        written = snprintf(path, path_size, "%s%s%s", directory,
                           directory[directory_length - 1u] == '/' ||
                           directory[directory_length - 1u] == '\\' ? "" : "/",
                           name);
        if (written < 0 || (size_t)written >= path_size) {
            set_error(error, error_size, "Collection handoff path is too long");
            return 0;
        }
        if (!export_path_exists(path)) break;
        if (++suffix >= 999) {
            set_error(error, error_size, "Could not find a free collection handoff name");
            return 0;
        }
        snprintf(name, sizeof(name), "%.240s_%02d", base, suffix);
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_export_bank(const TsInstrument *instrument, const char *folder,
                              char *error, size_t error_size)
{
    char created[TS_BANK_SLOT_COUNT][1152];
    int created_count = 0;
    if (instrument == NULL || folder == NULL || folder[0] == '\0' ||
        ts_instrument_bank_count(instrument) == 0) {
        set_error(error, error_size, "No sound collection to export");
        return 0;
    }
    if (ts_mkdir(folder) != 0) {
        snprintf(error, error_size, errno == EEXIST ?
                 "Bank folder already exists; choose a new name" :
                 "Could not create bank folder: %s", strerror(errno));
        return 0;
    }
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
        const TsBankSlot *slot = &instrument->bank[i];
        char safe[96];
        int written;
        if (!slot->occupied) continue;
        bank_safe_name(slot->sample.name, safe, sizeof(safe));
        written = snprintf(created[created_count], sizeof(created[created_count]),
                           "%s/%02d_%s.wav", folder, i + 1, safe);
        if (written < 0 || (size_t)written >= sizeof(created[created_count])) {
            set_error(error, error_size, "Bank export path is too long");
            goto failed;
        }
        if (!ts_sample_save_wav16_tuned_looped(
                &slot->sample, &slot->tuning,
                slot->has_loop, slot->loop_first, slot->loop_last,
                slot->loop_mode, created[created_count], error, error_size)) {
            remove(created[created_count]);
            goto failed;
        }
        ++created_count;
    }
    set_error(error, error_size, "");
    return 1;
failed:
    for (int i = 0; i < created_count; ++i) remove(created[i]);
    ts_rmdir(folder);
    return 0;
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
    if (instrument->post_edit_count > 0) {
        TsPostEdit operation;
        if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
            set_error(error, error_size, "Commit before adding more tape operations");
            return 0;
        }
        target = snapshot(instrument);
        memset(&operation, 0, sizeof(operation));
        operation.kind = TS_POST_CROP;
        operation.first = instrument->selection_first;
        operation.last = instrument->selection_last;
        target.post_edits[target.post_edit_count++] = operation;
        if (instrument->has_loop) {
            size_t kept_first = instrument->loop_first > operation.first ?
                                instrument->loop_first : operation.first;
            size_t kept_last = instrument->loop_last < operation.last ?
                               instrument->loop_last : operation.last;
            target.has_loop = kept_last > kept_first + 1u;
            target.loop_first = target.has_loop ? kept_first - operation.first : 0u;
            target.loop_last = target.has_loop ? kept_last - operation.first : 0u;
        }
        target.selection_first = 0;
        target.selection_last = 0;
        target.has_selection = 0;
        target.view_first = 0;
        target.view_last = operation.last - operation.first;
        ts_sample_init(&current);
        if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
        begin_edit(instrument);
        ts_sample_free(&instrument->current);
        instrument->current = current;
        instrument->loop_first = target.loop_first;
        instrument->loop_last = target.loop_last;
        instrument->has_loop = target.has_loop;
        memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
        instrument->post_edit_count = target.post_edit_count;
        ts_instrument_clear_selection(instrument);
        ts_instrument_show_all(instrument);
        set_error(error, error_size, "");
        return 1;
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
    if (instrument->post_edit_count > 0) {
        TsPostEdit operation;
        static const TsPostEditKind kinds[] = {
            TS_POST_REVERSE, TS_POST_NORMALIZE, TS_POST_GAIN,
            TS_POST_FADE_IN, TS_POST_FADE_OUT
        };
        if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
            set_error(error, error_size, "Commit before adding more post-DSP edits");
            return 0;
        }
        target = snapshot(instrument);
        memset(&operation, 0, sizeof(operation));
        operation.kind = kinds[kind];
        operation.first = instrument->has_selection ? instrument->selection_first : 0;
        operation.last = instrument->has_selection ? instrument->selection_last :
                                                       instrument->current.frames;
        operation.amount = amount;
        target.post_edits[target.post_edit_count++] = operation;
        ts_sample_init(&current);
        if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
        begin_edit(instrument);
        replace_current_preserving_view(instrument, &current);
        memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
        instrument->post_edit_count = target.post_edit_count;
        set_error(error, error_size, "");
        return 1;
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
    replace_current_preserving_view(instrument, &current);
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_rotate_zero_crossing(TsInstrument *instrument, int direction,
                                       size_t crossing_count,
                                       char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsSample base;
    TsPostEdit operation;
    const TsPostEdit *previous = NULL;
    size_t first, last, usable = 0, wanted, offset = 0;
    int64_t logical_steps;
    int coalescing = 0;
    if (instrument == NULL || instrument->current.data == NULL ||
        direction == 0 || crossing_count == 0) {
        set_error(error, error_size, "No Current sample to rotate");
        return 0;
    }
    first = instrument->has_selection ? instrument->selection_first : 0u;
    last = instrument->has_selection ? instrument->selection_last :
                                       instrument->current.frames;
    if (last <= first + 1u) {
        set_error(error, error_size, "Waveform range is too short to rotate");
        return 0;
    }

    target = snapshot(instrument);
    if (target.post_edit_count > 0) {
        previous = &target.post_edits[target.post_edit_count - 1];
        coalescing = previous->kind == TS_POST_ROTATE && previous->first == first &&
                     previous->last == last;
    }
    ts_sample_init(&base);
    if (coalescing) {
        logical_steps = (int64_t)previous->amount;
        --target.post_edit_count;
        if (!render_snapshot(&base, instrument, &target, error, error_size)) return 0;
    } else {
        logical_steps = 0;
        if (!ts_sample_clone(&base, &instrument->current, error, error_size)) return 0;
    }

    for (size_t frame = first + 1u; frame < last; ++frame)
        if (is_zero_crossing(&base, frame)) ++usable;
    if (usable == 0) {
        TsSample range = base;
        range.data += first;
        range.frames = last - first;
        offset = ts_sample_nearest_zero_crossing(
            &range, direction > 0 ? 1u : range.frames - 1u);
        if (offset == 0 || offset >= range.frames) {
            ts_sample_free(&base);
            set_error(error, error_size, "No usable zero crossing in waveform range");
            return 0;
        }
        usable = 1;
    }

    if (crossing_count > (size_t)INT64_MAX) crossing_count = (size_t)INT64_MAX;
    if (direction > 0) {
        if (logical_steps > INT64_MAX - (int64_t)crossing_count)
            logical_steps = (int64_t)((uint64_t)logical_steps % usable);
        logical_steps += (int64_t)crossing_count;
    } else {
        if (logical_steps < INT64_MIN + (int64_t)crossing_count)
            logical_steps = -(int64_t)((uint64_t)(-logical_steps) % usable);
        logical_steps -= (int64_t)crossing_count;
    }

    if (logical_steps == 0) {
        TsSample current = base;
        begin_edit(instrument);
        replace_current_preserving_view(instrument, &current);
        memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
        instrument->post_edit_count = target.post_edit_count;
        set_error(error, error_size, "");
        return bank_sync_selected(instrument, error, error_size);
    }
    if (offset == 0) {
        uint64_t magnitude = logical_steps > 0 ? (uint64_t)logical_steps :
                                                (uint64_t)(-(logical_steps + 1)) + 1u;
        if (logical_steps > 0)
            wanted = (size_t)((magnitude - 1u) % usable);
        else
            wanted = usable - 1u - (size_t)((magnitude - 1u) % usable);
        for (size_t frame = first + 1u, index = 0; frame < last; ++frame) {
            if (!is_zero_crossing(&base, frame)) continue;
            if (index++ == wanted) {
                offset = frame - first;
                break;
            }
        }
    }
    ts_sample_free(&base);
    if (!coalescing && target.post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits");
        return 0;
    }
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_ROTATE;
    operation.first = first;
    operation.last = last;
    operation.destination = (int64_t)offset;
    operation.amount = (float)logical_steps;
    target.post_edits[target.post_edit_count++] = operation;
    {
        TsSample current;
        ts_sample_init(&current);
        if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
        begin_edit(instrument);
        replace_current_preserving_view(instrument, &current);
    }
    memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
    set_error(error, error_size, "");
    return bank_sync_selected(instrument, error, error_size);
}

int ts_instrument_apply_warp(TsInstrument *instrument, float amount,
                             char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsPostEdit operation;
    TsSample current;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current sample to warp");
        return 0;
    }
    if (!isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        set_error(error, error_size, "WARP amount must be between zero and one");
        return 0;
    }
    if (amount == 0.0f) {
        set_error(error, error_size, "");
        return 1;
    }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits");
        return 0;
    }
    target = snapshot(instrument);
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_WARP;
    operation.first = instrument->has_selection ? instrument->selection_first : 0u;
    operation.last = instrument->has_selection ? instrument->selection_last :
                                                  instrument->current.frames;
    operation.amount = amount;
    target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    replace_current_preserving_view(instrument, &current);
    memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
    set_error(error, error_size, "");
    return bank_sync_selected(instrument, error, error_size);
}

void ts_warp_gesture_init(TsWarpGesture *gesture)
{
    if (gesture == NULL) return;
    memset(gesture, 0, sizeof(*gesture));
    ts_sample_init(&gesture->original);
}

static int warp_gesture_owns(const TsInstrument *instrument,
                             const TsWarpGesture *gesture)
{
    return instrument != NULL && gesture != NULL && gesture->active &&
           instrument->selected_slot == gesture->owner_slot &&
           instrument->generation == gesture->owner_generation &&
           instrument->parent.data == gesture->owner_parent_data &&
           instrument->current.frames == gesture->original.frames;
}

static void warp_gesture_clear(TsWarpGesture *gesture)
{
    ts_sample_free(&gesture->original);
    memset(gesture, 0, sizeof(*gesture));
    ts_sample_init(&gesture->original);
}

int ts_instrument_warp_gesture_begin(TsInstrument *instrument,
                                     TsWarpGesture *gesture,
                                     char *error, size_t error_size)
{
    if (instrument == NULL || gesture == NULL || gesture->active ||
        instrument->current.data == NULL) {
        set_error(error, error_size, "Could not begin WARP gesture");
        return 0;
    }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits");
        return 0;
    }
    gesture->start = snapshot(instrument);
    if (!ts_sample_clone(&gesture->original, &instrument->current,
                         error, error_size)) return 0;
    gesture->owner_parent_data = instrument->parent.data;
    gesture->owner_generation = instrument->generation;
    gesture->owner_slot = instrument->selected_slot;
    gesture->amount = 0.0f;
    gesture->active = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_warp_gesture_preview(TsInstrument *instrument,
                                       TsWarpGesture *gesture, float amount,
                                       char *error, size_t error_size)
{
    TsSample preview;
    TsEditSnapshot target;
    TsPostEdit operation;
    if (!warp_gesture_owns(instrument, gesture)) {
        set_error(error, error_size, "WARP gesture no longer owns Current");
        return 0;
    }
    if (!isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        set_error(error, error_size, "WARP amount must be between zero and one");
        return 0;
    }
    ts_sample_init(&preview);
    if (amount == 0.0f) {
        if (!ts_sample_clone(&preview, &gesture->original, error, error_size)) return 0;
    } else {
        target = gesture->start;
        memset(&operation, 0, sizeof(operation));
        operation.kind = TS_POST_WARP;
        operation.first = target.has_selection ? target.selection_first : 0u;
        operation.last = target.has_selection ? target.selection_last :
                                                 gesture->original.frames;
        operation.amount = amount;
        target.post_edits[target.post_edit_count++] = operation;
        if (!render_snapshot(&preview, instrument, &target, error, error_size)) return 0;
    }
    replace_current_preserving_view(instrument, &preview);
    gesture->amount = amount;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_warp_gesture_cancel(TsInstrument *instrument,
                                      TsWarpGesture *gesture,
                                      char *error, size_t error_size)
{
    TsSample restored;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture);
        set_error(error, error_size, "WARP gesture no longer owns Current");
        return 0;
    }
    ts_sample_init(&restored);
    if (!ts_sample_clone(&restored, &gesture->original, error, error_size)) return 0;
    replace_current_preserving_view(instrument, &restored);
    warp_gesture_clear(gesture);
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_warp_gesture_commit(TsInstrument *instrument,
                                      TsWarpGesture *gesture,
                                      char *error, size_t error_size)
{
    TsPostEdit operation;
    int ok;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture);
        set_error(error, error_size, "WARP gesture no longer owns Current");
        return 0;
    }
    if (gesture->amount == 0.0f)
        return ts_instrument_warp_gesture_cancel(instrument, gesture,
                                                 error, error_size);
    stack_push(instrument->undo, &instrument->undo_count, gesture->start);
    instrument->redo_count = 0;
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_WARP;
    operation.first = gesture->start.has_selection ?
                      gesture->start.selection_first : 0u;
    operation.last = gesture->start.has_selection ?
                     gesture->start.selection_last : gesture->original.frames;
    operation.amount = gesture->amount;
    memcpy(instrument->post_edits, gesture->start.post_edits,
           sizeof(instrument->post_edits));
    instrument->post_edit_count = gesture->start.post_edit_count;
    instrument->post_edits[instrument->post_edit_count++] = operation;
    ok = bank_sync_selected(instrument, error, error_size);
    warp_gesture_clear(gesture);
    if (ok) set_error(error, error_size, "");
    return ok;
}

int ts_instrument_apply_smear(TsInstrument *instrument, float amount,
                              char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsPostEdit operation;
    TsSample current;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current sample to smear"); return 0;
    }
    if (!isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        set_error(error, error_size, "SMEAR amount must be between zero and one"); return 0;
    }
    if (amount == 0.0f) { set_error(error, error_size, ""); return 1; }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits"); return 0;
    }
    target = snapshot(instrument);
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_SMEAR;
    operation.first = instrument->has_selection ? instrument->selection_first : 0u;
    operation.last = instrument->has_selection ? instrument->selection_last : instrument->current.frames;
    operation.amount = amount;
    target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    replace_current_preserving_view(instrument, &current);
    memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
    return bank_sync_selected(instrument, error, error_size);
}

void ts_smear_gesture_init(TsSmearGesture *gesture) { ts_warp_gesture_init(gesture); }

int ts_instrument_smear_gesture_begin(TsInstrument *instrument, TsSmearGesture *gesture,
                                      char *error, size_t error_size)
{
    if (instrument == NULL || gesture == NULL || gesture->active || instrument->current.data == NULL) {
        set_error(error, error_size, "Could not begin SMEAR gesture"); return 0;
    }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits"); return 0;
    }
    gesture->start = snapshot(instrument);
    if (!ts_sample_clone(&gesture->original, &instrument->current, error, error_size)) return 0;
    gesture->owner_parent_data = instrument->parent.data;
    gesture->owner_generation = instrument->generation;
    gesture->owner_slot = instrument->selected_slot;
    gesture->active = 1; gesture->amount = 0.0f;
    set_error(error, error_size, ""); return 1;
}

int ts_instrument_smear_gesture_preview(TsInstrument *instrument, TsSmearGesture *gesture,
                                        float amount, char *error, size_t error_size)
{
    TsSample preview;
    TsEditSnapshot target;
    TsPostEdit operation;
    if (!warp_gesture_owns(instrument, gesture)) {
        set_error(error, error_size, "SMEAR gesture no longer owns Current"); return 0;
    }
    if (!isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        set_error(error, error_size, "SMEAR amount must be between zero and one"); return 0;
    }
    ts_sample_init(&preview);
    if (amount == 0.0f) {
        if (!ts_sample_clone(&preview, &gesture->original, error, error_size)) return 0;
    } else {
        target = gesture->start;
        memset(&operation, 0, sizeof(operation)); operation.kind = TS_POST_SMEAR;
        operation.first = target.has_selection ? target.selection_first : 0u;
        operation.last = target.has_selection ? target.selection_last : gesture->original.frames;
        operation.amount = amount; target.post_edits[target.post_edit_count++] = operation;
        if (!render_snapshot(&preview, instrument, &target, error, error_size)) return 0;
    }
    replace_current_preserving_view(instrument, &preview);
    gesture->amount = amount; set_error(error, error_size, ""); return 1;
}

int ts_instrument_smear_gesture_cancel(TsInstrument *instrument, TsSmearGesture *gesture,
                                       char *error, size_t error_size)
{
    TsSample restored;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture); set_error(error, error_size, "SMEAR gesture no longer owns Current"); return 0;
    }
    ts_sample_init(&restored);
    if (!ts_sample_clone(&restored, &gesture->original, error, error_size)) return 0;
    replace_current_preserving_view(instrument, &restored);
    warp_gesture_clear(gesture); set_error(error, error_size, ""); return 1;
}

int ts_instrument_smear_gesture_commit(TsInstrument *instrument, TsSmearGesture *gesture,
                                       char *error, size_t error_size)
{
    TsPostEdit operation;
    int ok;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture); set_error(error, error_size, "SMEAR gesture no longer owns Current"); return 0;
    }
    if (gesture->amount == 0.0f)
        return ts_instrument_smear_gesture_cancel(instrument, gesture, error, error_size);
    stack_push(instrument->undo, &instrument->undo_count, gesture->start);
    instrument->redo_count = 0;
    memset(&operation, 0, sizeof(operation)); operation.kind = TS_POST_SMEAR;
    operation.first = gesture->start.has_selection ? gesture->start.selection_first : 0u;
    operation.last = gesture->start.has_selection ? gesture->start.selection_last : gesture->original.frames;
    operation.amount = gesture->amount;
    memcpy(instrument->post_edits, gesture->start.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = gesture->start.post_edit_count;
    instrument->post_edits[instrument->post_edit_count++] = operation;
    ok = bank_sync_selected(instrument, error, error_size);
    warp_gesture_clear(gesture); if (ok) set_error(error, error_size, ""); return ok;
}

int ts_instrument_apply_tear(TsInstrument *instrument, float amount,
                             char *error, size_t error_size)
{
    TsEditSnapshot target; TsPostEdit operation; TsSample current;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "No Current sample to tear"); return 0;
    }
    if (!isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        set_error(error, error_size, "TEAR amount must be between zero and one"); return 0;
    }
    if (amount == 0.0f) { set_error(error, error_size, ""); return 1; }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits"); return 0;
    }
    target = snapshot(instrument); memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_TEAR;
    operation.first = instrument->has_selection ? instrument->selection_first : 0u;
    operation.last = instrument->has_selection ? instrument->selection_last : instrument->current.frames;
    operation.amount = amount; target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument); replace_current_preserving_view(instrument, &current);
    memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
    return bank_sync_selected(instrument, error, error_size);
}

void ts_tear_gesture_init(TsTearGesture *gesture) { ts_warp_gesture_init(gesture); }

int ts_instrument_tear_gesture_begin(TsInstrument *instrument, TsTearGesture *gesture,
                                     char *error, size_t error_size)
{
    if (instrument == NULL || gesture == NULL || gesture->active || instrument->current.data == NULL) {
        set_error(error, error_size, "Could not begin TEAR gesture"); return 0;
    }
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Commit before adding more post-DSP edits"); return 0;
    }
    gesture->start = snapshot(instrument);
    if (!ts_sample_clone(&gesture->original, &instrument->current, error, error_size)) return 0;
    gesture->owner_parent_data = instrument->parent.data;
    gesture->owner_generation = instrument->generation;
    gesture->owner_slot = instrument->selected_slot;
    gesture->active = 1; gesture->amount = 0.0f;
    set_error(error, error_size, ""); return 1;
}

int ts_instrument_tear_gesture_preview(TsInstrument *instrument, TsTearGesture *gesture,
                                       float amount, char *error, size_t error_size)
{
    TsSample preview; TsEditSnapshot target; TsPostEdit operation;
    if (!warp_gesture_owns(instrument, gesture)) {
        set_error(error, error_size, "TEAR gesture no longer owns Current"); return 0;
    }
    if (!isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        set_error(error, error_size, "TEAR amount must be between zero and one"); return 0;
    }
    ts_sample_init(&preview);
    if (amount == 0.0f) {
        if (!ts_sample_clone(&preview, &gesture->original, error, error_size)) return 0;
    } else {
        target = gesture->start; memset(&operation, 0, sizeof(operation));
        operation.kind = TS_POST_TEAR;
        operation.first = target.has_selection ? target.selection_first : 0u;
        operation.last = target.has_selection ? target.selection_last : gesture->original.frames;
        operation.amount = amount; target.post_edits[target.post_edit_count++] = operation;
        if (!render_snapshot(&preview, instrument, &target, error, error_size)) return 0;
    }
    replace_current_preserving_view(instrument, &preview);
    gesture->amount = amount; set_error(error, error_size, ""); return 1;
}

int ts_instrument_tear_gesture_cancel(TsInstrument *instrument, TsTearGesture *gesture,
                                      char *error, size_t error_size)
{
    TsSample restored;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture); set_error(error, error_size, "TEAR gesture no longer owns Current"); return 0;
    }
    ts_sample_init(&restored);
    if (!ts_sample_clone(&restored, &gesture->original, error, error_size)) return 0;
    replace_current_preserving_view(instrument, &restored);
    warp_gesture_clear(gesture); set_error(error, error_size, ""); return 1;
}

int ts_instrument_tear_gesture_commit(TsInstrument *instrument, TsTearGesture *gesture,
                                      char *error, size_t error_size)
{
    TsPostEdit operation; int ok;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture); set_error(error, error_size, "TEAR gesture no longer owns Current"); return 0;
    }
    if (gesture->amount == 0.0f)
        return ts_instrument_tear_gesture_cancel(instrument, gesture, error, error_size);
    stack_push(instrument->undo, &instrument->undo_count, gesture->start);
    instrument->redo_count = 0; memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_TEAR;
    operation.first = gesture->start.has_selection ? gesture->start.selection_first : 0u;
    operation.last = gesture->start.has_selection ? gesture->start.selection_last : gesture->original.frames;
    operation.amount = gesture->amount;
    memcpy(instrument->post_edits, gesture->start.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = gesture->start.post_edit_count;
    instrument->post_edits[instrument->post_edit_count++] = operation;
    ok = bank_sync_selected(instrument, error, error_size);
    warp_gesture_clear(gesture); if (ok) set_error(error, error_size, ""); return ok;
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
    instrument->loop_mode = target.loop_mode;
    instrument->has_selection = target.has_selection;
    instrument->has_loop = target.has_loop;
    instrument->tuning = target.tuning;
    instrument->audible_tuning = target.audible_tuning;
    instrument->process = target.process;
    memcpy(instrument->sample_edits, target.sample_edits, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target.sample_edit_count;
    memcpy(instrument->post_edits, target.post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
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
    return bank_sync_selected(instrument, error, error_size);
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
    return bank_sync_selected(instrument, error, error_size);
}

size_t ts_instrument_frame_from_view_x(const TsInstrument *instrument, int x, int width)
{
    size_t first = instrument->view_first;
    size_t last = instrument->view_last;
    if (width <= 0 || last <= first) return first;
    if (x < 0) x = 0;
    if (x >= width - 1) return last;
    return first + (size_t)x * (last - first) / (size_t)width;
}

static void put_generator_recipe(FILE *f, const TsGeneratorRecipe *recipe)
{
    put32(f, recipe->seed);
    put32(f, (uint32_t)recipe->kind);
    put_float(f, recipe->seconds);
    put_float(f, recipe->frequency);
    put32(f, (uint32_t)recipe->has_fm_patch);
    if (recipe->has_fm_patch) put_fm_patch(f, &recipe->fm_patch);
}

static int get_generator_recipe(FILE *f, TsGeneratorRecipe *recipe)
{
    uint32_t value;
    if (!get32(f, &recipe->seed) || !get32(f, &value)) return 0;
    recipe->kind = (TsGeneratorKind)value;
    if (!get_float(f, &recipe->seconds) || !get_float(f, &recipe->frequency) ||
        !get32(f, &value)) return 0;
    recipe->has_fm_patch = (int)value;
    if (recipe->kind >= TS_GENERATOR_COUNT || recipe->seconds < 0.1f ||
        recipe->seconds > 8.0f || recipe->frequency < 30.0f ||
        recipe->frequency > 2000.0f ||
        (recipe->has_fm_patch != 0 && recipe->has_fm_patch != 1)) return 0;
    return !recipe->has_fm_patch || get_fm_patch(f, &recipe->fm_patch);
}

static void put_process_recipe(FILE *f, const TsProcessRecipe *process)
{
    put32(f, process->seed);
    put_float(f, process->body); put_float(f, process->edge); put_float(f, process->drift);
    put32(f, (uint32_t)process->noise_enabled); put_float(f, process->noise_amount);
    put32(f, (uint32_t)process->noise_color);
    put32(f, (uint32_t)process->delay_enabled);
    put_float(f, process->delay_seconds); put_float(f, process->delay_feedback);
    put_float(f, process->delay_damping); put_float(f, process->delay_mix);
    put32(f, (uint32_t)process->reverb_enabled);
    put_float(f, process->reverb_decay); put_float(f, process->reverb_damping);
    put_float(f, process->reverb_mix);
    put32(f, (uint32_t)process->filter_enabled);
    put32(f, (uint32_t)process->filter_mode);
    put_float(f, process->filter_cutoff_hz); put_float(f, process->filter_resonance);
    put32(f, (uint32_t)process->shaper_enabled);
    put32(f, (uint32_t)process->shaper_mode);
    put_float(f, process->shaper_drive); put_float(f, process->shaper_mix);
}

static int get_process_recipe(FILE *f, TsProcessRecipe *process)
{
    uint32_t value;
#define GET_PROCESS_U32(field) do { if (!get32(f, &value)) return 0; (field) = value; } while (0)
    if (!get32(f, &process->seed) ||
        !get_float(f, &process->body) || !get_float(f, &process->edge) ||
        !get_float(f, &process->drift)) return 0;
    GET_PROCESS_U32(process->noise_enabled);
    if (!get_float(f, &process->noise_amount)) return 0;
    GET_PROCESS_U32(process->noise_color);
    GET_PROCESS_U32(process->delay_enabled);
    if (!get_float(f, &process->delay_seconds) ||
        !get_float(f, &process->delay_feedback) ||
        !get_float(f, &process->delay_damping) ||
        !get_float(f, &process->delay_mix)) return 0;
    GET_PROCESS_U32(process->reverb_enabled);
    if (!get_float(f, &process->reverb_decay) ||
        !get_float(f, &process->reverb_damping) ||
        !get_float(f, &process->reverb_mix)) return 0;
    GET_PROCESS_U32(process->filter_enabled);
    GET_PROCESS_U32(process->filter_mode);
    if (!get_float(f, &process->filter_cutoff_hz) ||
        !get_float(f, &process->filter_resonance)) return 0;
    GET_PROCESS_U32(process->shaper_enabled);
    GET_PROCESS_U32(process->shaper_mode);
    if (!get_float(f, &process->shaper_drive) ||
        !get_float(f, &process->shaper_mix)) return 0;
#undef GET_PROCESS_U32
    return process->body >= 0.0f && process->body <= 1.0f &&
           process->edge >= 0.0f && process->edge <= 1.0f &&
           process->drift >= 0.0f && process->drift <= 1.0f &&
           (process->noise_enabled == 0 || process->noise_enabled == 1) &&
           process->noise_amount >= 0.0f && process->noise_amount <= 1.0f &&
           process->noise_color < TS_NOISE_COLOR_COUNT &&
           (process->delay_enabled == 0 || process->delay_enabled == 1) &&
           process->delay_seconds >= 0.0f && process->delay_seconds <= 1.0f &&
           process->delay_feedback >= 0.0f && process->delay_feedback <= 0.95f &&
           process->delay_damping >= 0.0f && process->delay_damping <= 1.0f &&
           process->delay_mix >= 0.0f && process->delay_mix <= 1.0f &&
           (process->reverb_enabled == 0 || process->reverb_enabled == 1) &&
           process->reverb_decay >= 0.0f && process->reverb_decay <= 1.0f &&
           process->reverb_damping >= 0.0f && process->reverb_damping <= 1.0f &&
           process->reverb_mix >= 0.0f && process->reverb_mix <= 1.0f &&
           (process->filter_enabled == 0 || process->filter_enabled == 1) &&
           process->filter_mode < TS_FILTER_MODE_COUNT &&
           process->filter_cutoff_hz >= 20.0f && process->filter_cutoff_hz <= 20000.0f &&
           process->filter_resonance >= 0.0f && process->filter_resonance <= 1.0f &&
           (process->shaper_enabled == 0 || process->shaper_enabled == 1) &&
           process->shaper_mode < TS_SHAPER_MODE_COUNT &&
           process->shaper_drive >= 1.0f && process->shaper_drive <= 16.0f &&
           process->shaper_mix >= 0.0f && process->shaper_mix <= 1.0f;
}

static void put_edit_snapshot(FILE *f, const TsEditSnapshot *state)
{
    put64(f, state->crop_first); put64(f, state->crop_last);
    put64(f, state->selection_first); put64(f, state->selection_last);
    put64(f, state->view_first); put64(f, state->view_last);
    put64(f, state->loop_first); put64(f, state->loop_last);
    put_float(f, state->loop_crossfade_ms);
    put32(f, (uint32_t)state->loop_mode);
    put32(f, (uint32_t)state->has_selection); put32(f, (uint32_t)state->has_loop);
    put32(f, (uint32_t)state->tuning.root_note); put_float(f, state->tuning.fine_tune_cents);
    put32(f, (uint32_t)state->audible_tuning.root_note);
    put_float(f, state->audible_tuning.fine_tune_cents);
    put_process_recipe(f, &state->process);
    put32(f, (uint32_t)state->sample_edit_count);
    for (int i = 0; i < state->sample_edit_count; ++i) {
        const TsSampleEdit *edit = &state->sample_edits[i];
        put32(f, (uint32_t)edit->kind); put64(f, edit->first); put64(f, edit->last);
        put_float(f, edit->amount);
    }
    put32(f, (uint32_t)state->post_edit_count);
    for (int i = 0; i < state->post_edit_count; ++i) {
        const TsPostEdit *edit = &state->post_edits[i];
        put32(f, (uint32_t)edit->kind); put64(f, edit->first); put64(f, edit->last);
        put64(f, (uint64_t)edit->destination); put_float(f, edit->amount);
        put32(f, edit->crossfade_frames);
    }
}

static int get_edit_snapshot(FILE *f, TsEditSnapshot *state)
{
    const size_t frame_limit = 100000000u;
    uint32_t value;
    uint64_t wide;
#define GET_STATE_SIZE(field) do { if (!get64(f, &wide) || wide > SIZE_MAX) return 0; (field) = (size_t)wide; } while (0)
#define GET_STATE_U32(field) do { if (!get32(f, &value)) return 0; (field) = value; } while (0)
    memset(state, 0, sizeof(*state));
    GET_STATE_SIZE(state->crop_first); GET_STATE_SIZE(state->crop_last);
    GET_STATE_SIZE(state->selection_first); GET_STATE_SIZE(state->selection_last);
    GET_STATE_SIZE(state->view_first); GET_STATE_SIZE(state->view_last);
    GET_STATE_SIZE(state->loop_first); GET_STATE_SIZE(state->loop_last);
    if (!get_float(f, &state->loop_crossfade_ms)) return 0;
    GET_STATE_U32(state->loop_mode); GET_STATE_U32(state->has_selection);
    GET_STATE_U32(state->has_loop); GET_STATE_U32(state->tuning.root_note);
    if (!get_float(f, &state->tuning.fine_tune_cents)) return 0;
    GET_STATE_U32(state->audible_tuning.root_note);
    if (!get_float(f, &state->audible_tuning.fine_tune_cents) ||
        !get_process_recipe(f, &state->process)) return 0;
    GET_STATE_U32(state->sample_edit_count);
    if (state->sample_edit_count < 0 || state->sample_edit_count > TS_SAMPLE_EDIT_DEPTH) return 0;
    for (int i = 0; i < state->sample_edit_count; ++i) {
        TsSampleEdit *edit = &state->sample_edits[i];
        GET_STATE_U32(edit->kind); GET_STATE_SIZE(edit->first); GET_STATE_SIZE(edit->last);
        if (!get_float(f, &edit->amount) || edit->kind > TS_SAMPLE_EDIT_FADE_OUT ||
            edit->last <= edit->first || edit->last > frame_limit) return 0;
    }
    GET_STATE_U32(state->post_edit_count);
    if (state->post_edit_count < 0 || state->post_edit_count > TS_POST_EDIT_DEPTH) return 0;
    for (int i = 0; i < state->post_edit_count; ++i) {
        TsPostEdit *edit = &state->post_edits[i];
        GET_STATE_U32(edit->kind); GET_STATE_SIZE(edit->first); GET_STATE_SIZE(edit->last);
        if (!get64(f, &wide)) return 0;
        edit->destination = (int64_t)wide;
        if (!get_float(f, &edit->amount) || !get32(f, &edit->crossfade_frames) ||
            edit->kind > TS_POST_TEAR || edit->last <= edit->first ||
            edit->last > frame_limit || edit->destination < -(int64_t)frame_limit ||
            edit->destination > (int64_t)frame_limit || edit->crossfade_frames > 65536u) return 0;
    }
#undef GET_STATE_SIZE
#undef GET_STATE_U32
    return state->crop_first < state->crop_last && state->crop_last <= frame_limit &&
           state->selection_first <= state->selection_last &&
           state->selection_last <= frame_limit &&
           state->view_first < state->view_last && state->view_last <= frame_limit &&
           state->loop_first <= state->loop_last && state->loop_last <= frame_limit &&
           state->loop_crossfade_ms >= 0.0f && state->loop_crossfade_ms <= 50.0f &&
           state->loop_mode < TS_LOOP_MODE_COUNT &&
           (state->has_selection == 0 || state->has_selection == 1) &&
           (state->has_loop == 0 || state->has_loop == 1) &&
           tuning_valid(&state->tuning) && tuning_valid(&state->audible_tuning);
}

static void put_sample_block(FILE *f, const TsSample *sample)
{
    size_t name_length = strlen(sample->name);
    put32(f, sample->sample_rate); put64(f, sample->frames);
    put32(f, (uint32_t)name_length); fwrite(sample->name, 1, name_length, f);
    for (size_t frame = 0; frame < sample->frames; ++frame) put_float(f, sample->data[frame]);
    put64(f, ts_sample_hash(sample));
}

static int get_sample_block(FILE *f, TsSample *sample)
{
    uint32_t name_length;
    uint64_t frames;
    uint64_t stored_hash;
    if (!get32(f, &sample->sample_rate) || !get64(f, &frames) ||
        !get32(f, &name_length) || sample->sample_rate < 1000u ||
        frames == 0 || frames > 100000000u || frames > SIZE_MAX / sizeof(float) ||
        name_length >= sizeof(sample->name)) return 0;
    sample->frames = (size_t)frames;
    if (fread(sample->name, 1, name_length, f) != name_length) return 0;
    sample->name[name_length] = '\0';
    sample->data = (float *)malloc(sample->frames * sizeof(float));
    if (sample->data == NULL) return -1;
    for (size_t frame = 0; frame < sample->frames; ++frame)
        if (!get_float(f, &sample->data[frame])) return 0;
    return get64(f, &stored_hash) && stored_hash == ts_sample_hash(sample);
}

static int snapshot_fits_tile(const TsEditSnapshot *state, const TsBankSlot *slot)
{
    return state->crop_last <= slot->edit_parent.frames &&
           state->selection_last <= slot->sample.frames &&
           state->view_last <= slot->sample.frames &&
           state->loop_last <= slot->sample.frames &&
           (!state->has_selection || state->selection_first < state->selection_last) &&
           (!state->has_loop || state->loop_first < state->loop_last);
}

static int save_tsr15(const TsInstrument *instrument, FILE *f)
{
    fwrite("TSR15\r\n\032", 1, 8, f);
    put32(f, (uint32_t)instrument->selected_slot);
    put_float(f, instrument->family_mutation);
    put32(f, instrument->family_sequence);
    put32(f, (uint32_t)instrument->family_trajectory);
    put32(f, (uint32_t)instrument->family_relation);
    put32(f, instrument->family_locks);
    put32(f, (uint32_t)(instrument->family_anchor_slot + 1));
    put32(f, (uint32_t)(instrument->family_last_slot + 1));
    put32(f, (uint32_t)instrument->source_kind);
    put32(f, instrument->generation);
    put64(f, instrument->ancestor_hash);
    put32(f, TS_BANK_SLOT_COUNT);
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
        const TsBankSlot *slot = &instrument->bank[i];
        const TsSample *audio;
        const TsSample *baseline;
        const TsEditSnapshot *edit;
        const TsEditSnapshot *undo;
        const TsEditSnapshot *redo;
        TsEditSnapshot live_edit;
        TsEditSnapshot default_edit;
        int undo_count;
        int redo_count;
        put32(f, (uint32_t)slot->occupied);
        if (!slot->occupied) continue;
        audio = &slot->sample;
        baseline = slot->edit_parent.data != NULL ? &slot->edit_parent : &slot->sample;
        edit = &slot->edit;
        undo = slot->undo;
        redo = slot->redo;
        undo_count = slot->undo_count;
        redo_count = slot->redo_count;
        if (i == instrument->selected_slot && instrument->current.data != NULL &&
            instrument->parent.data != NULL) {
            audio = &instrument->current;
            baseline = &instrument->parent;
            live_edit = snapshot(instrument);
            edit = &live_edit;
            undo = instrument->undo;
            redo = instrument->redo;
            undo_count = instrument->undo_count;
            redo_count = instrument->redo_count;
        } else if (edit->crop_last <= edit->crop_first ||
                   edit->crop_last > baseline->frames ||
                   edit->view_last <= edit->view_first ||
                   edit->view_last > audio->frames) {
            memset(&default_edit, 0, sizeof(default_edit));
            default_edit.crop_last = baseline->frames;
            default_edit.view_last = audio->frames;
            default_edit.loop_first = slot->loop_first;
            default_edit.loop_last = slot->loop_last;
            default_edit.loop_crossfade_ms = slot->loop_crossfade_ms;
            default_edit.loop_mode = slot->loop_mode;
            default_edit.has_loop = slot->has_loop;
            default_edit.tuning = slot->tuning;
            default_edit.audible_tuning = slot->audible_tuning;
            ts_process_recipe_reset(&default_edit.process);
            edit = &default_edit;
            undo = NULL;
            redo = NULL;
            undo_count = 0;
            redo_count = 0;
        }
        put32(f, (uint32_t)slot->capture_kind); put32(f, (uint32_t)slot->relation);
        put32(f, (uint32_t)(slot->parent_slot + 1)); put32(f, slot->lineage_seed);
        put32(f, slot->lineage_locks); put32(f, slot->trajectory_step);
        put_float(f, slot->lineage_mutation); put32(f, (uint32_t)slot->has_generator);
        if (slot->has_generator) put_generator_recipe(f, &slot->generator);
        put32(f, (uint32_t)slot->tuning.root_note); put_float(f, slot->tuning.fine_tune_cents);
        put32(f, (uint32_t)slot->audible_tuning.root_note);
        put_float(f, slot->audible_tuning.fine_tune_cents);
        put32(f, (uint32_t)slot->loop_mode); put32(f, (uint32_t)slot->has_loop);
        put64(f, slot->loop_first); put64(f, slot->loop_last);
        put_float(f, slot->loop_crossfade_ms);
        put_sample_block(f, audio);
        put_sample_block(f, baseline);
        put_edit_snapshot(f, edit);
        put32(f, (uint32_t)undo_count);
        for (int h = 0; h < undo_count; ++h) put_edit_snapshot(f, &undo[h]);
        put32(f, (uint32_t)redo_count);
        for (int h = 0; h < redo_count; ++h) put_edit_snapshot(f, &redo[h]);
    }
    return !ferror(f);
}

static int load_tsr15(FILE *f, TsInstrument *instrument, char *error, size_t error_size)
{
    TsInstrument loaded;
    uint32_t value;
    uint64_t wide;
    int selected;
    ts_instrument_init(&loaded);
    if (!get32(f, &value)) goto malformed;
    selected = (int)value;
    if (!get_float(f, &loaded.family_mutation) ||
        !get32(f, &loaded.family_sequence) || !get32(f, &value)) goto malformed;
    loaded.family_trajectory = (int)value;
    if (!get32(f, &value)) goto malformed;
    loaded.family_relation = (TsFamilyRelation)value;
    if (!get32(f, &loaded.family_locks) || !get32(f, &value)) goto malformed;
    loaded.family_anchor_slot = (int)value - 1;
    if (!get32(f, &value)) goto malformed;
    loaded.family_last_slot = (int)value - 1;
    if (!get32(f, &value)) goto malformed;
    loaded.source_kind = (TsSourceKind)value;
    if (!get32(f, &loaded.generation) || !get64(f, &loaded.ancestor_hash) ||
        !get32(f, &value) || value != TS_BANK_SLOT_COUNT ||
        selected < 0 || selected >= TS_BANK_SLOT_COUNT ||
        loaded.family_mutation < 0.0f || loaded.family_mutation > 1.0f ||
        (loaded.family_trajectory != 0 && loaded.family_trajectory != 1) ||
        loaded.family_relation >= TS_FAMILY_RELATION_COUNT ||
        (loaded.family_locks & ~TS_FAMILY_LOCK_ALL) != 0u ||
        loaded.family_anchor_slot < -1 || loaded.family_anchor_slot >= TS_BANK_SLOT_COUNT ||
        loaded.family_last_slot < -1 || loaded.family_last_slot >= TS_BANK_SLOT_COUNT ||
        loaded.source_kind > TS_SOURCE_COMMITTED) goto malformed;
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i) {
        TsBankSlot *slot = &loaded.bank[i];
        int sample_result;
        if (!get32(f, &value) || value > 1u) goto malformed;
        slot->occupied = (int)value;
        if (!slot->occupied) continue;
        if (!get32(f, &value)) goto malformed;
        slot->capture_kind = (TsBankCaptureKind)value;
        if (!get32(f, &value)) goto malformed;
        slot->relation = (TsFamilyRelation)value;
        if (!get32(f, &value)) goto malformed;
        slot->parent_slot = (int)value - 1;
        if (!get32(f, &slot->lineage_seed) || !get32(f, &slot->lineage_locks) ||
            !get32(f, &slot->trajectory_step) || !get_float(f, &slot->lineage_mutation) ||
            !get32(f, &value)) goto malformed;
        slot->has_generator = (int)value;
        if (slot->has_generator && !get_generator_recipe(f, &slot->generator)) goto malformed;
        if (!get32(f, &value)) goto malformed;
        slot->tuning.root_note = (int)value;
        if (!get_float(f, &slot->tuning.fine_tune_cents) || !get32(f, &value)) goto malformed;
        slot->audible_tuning.root_note = (int)value;
        if (!get_float(f, &slot->audible_tuning.fine_tune_cents) ||
            !get32(f, &value)) goto malformed;
        slot->loop_mode = (TsLoopMode)value;
        if (!get32(f, &value)) goto malformed;
        slot->has_loop = (int)value;
        if (!get64(f, &wide) || wide > SIZE_MAX) goto malformed;
        slot->loop_first = (size_t)wide;
        if (!get64(f, &wide) || wide > SIZE_MAX) goto malformed;
        slot->loop_last = (size_t)wide;
        if (!get_float(f, &slot->loop_crossfade_ms)) goto malformed;
        sample_result = get_sample_block(f, &slot->sample);
        if (sample_result < 0) goto out_of_memory;
        if (!sample_result) goto malformed;
        sample_result = get_sample_block(f, &slot->edit_parent);
        if (sample_result < 0) goto out_of_memory;
        if (!sample_result || !get_edit_snapshot(f, &slot->edit)) goto malformed;
        if (!get32(f, &value) || value > TS_HISTORY_DEPTH) goto malformed;
        slot->undo_count = (int)value;
        if (slot->undo_count > 0) {
            slot->undo = malloc((size_t)slot->undo_count * sizeof(*slot->undo));
            if (slot->undo == NULL) goto out_of_memory;
            for (int h = 0; h < slot->undo_count; ++h)
                if (!get_edit_snapshot(f, &slot->undo[h])) goto malformed;
        }
        if (!get32(f, &value) || value > TS_HISTORY_DEPTH) goto malformed;
        slot->redo_count = (int)value;
        if (slot->redo_count > 0) {
            slot->redo = malloc((size_t)slot->redo_count * sizeof(*slot->redo));
            if (slot->redo == NULL) goto out_of_memory;
            for (int h = 0; h < slot->redo_count; ++h)
                if (!get_edit_snapshot(f, &slot->redo[h])) goto malformed;
        }
        slot->process = slot->edit.process;
        if (slot->capture_kind > TS_BANK_CAPTURE_LOOP ||
            slot->relation >= TS_FAMILY_RELATION_COUNT || slot->parent_slot < -1 ||
            slot->parent_slot >= TS_BANK_SLOT_COUNT ||
            (slot->lineage_locks & ~TS_FAMILY_LOCK_ALL) != 0u ||
            slot->lineage_mutation < 0.0f || slot->lineage_mutation > 1.0f ||
            (slot->has_generator != 0 && slot->has_generator != 1) ||
            !tuning_valid(&slot->tuning) || !tuning_valid(&slot->audible_tuning) ||
            slot->loop_mode >= TS_LOOP_MODE_COUNT ||
            (slot->has_loop != 0 && slot->has_loop != 1) ||
            slot->loop_crossfade_ms < 0.0f || slot->loop_crossfade_ms > 50.0f ||
            (slot->has_loop && (slot->loop_first >= slot->loop_last ||
                                slot->loop_last > slot->sample.frames)) ||
            !snapshot_fits_tile(&slot->edit, slot)) goto malformed;
    }
    if (fgetc(f) != EOF) goto malformed;
    loaded.selected_slot = selected;
    if (!ts_instrument_select_bank(&loaded, selected, error, error_size)) goto failed;
    ts_instrument_free(instrument);
    *instrument = loaded;
    set_error(error, error_size, "");
    return 1;
out_of_memory:
    set_error(error, error_size, "Out of memory while loading TSR15 project");
    goto failed;
malformed:
    set_error(error, error_size, "Malformed or unsupported TSR15 project");
failed:
    ts_instrument_free(&loaded);
    return 0;
}

int ts_instrument_save_recipe(const TsInstrument *instrument, const char *path,
                              char *error, size_t error_size)
{
    FILE *f;
    if (instrument == NULL) {
        set_error(error, error_size, "No project to save");
        return 0;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        set_error(error, error_size, "Could not create recipe file");
        return 0;
    }
    if (!save_tsr15(instrument, f)) {
        fclose(f);
        set_error(error, error_size, "Could not write TSR15 project");
        return 0;
    }
    if (fclose(f) != 0) {
        set_error(error, error_size, "Could not finish TSR15 project");
        return 0;
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
    uint32_t bank_slot_count;
    size_t frames;
    int version;
    memset(&state, 0, sizeof(state));
    ts_process_recipe_reset(&state.process);
    ts_instrument_init(&loaded);
    f = fopen(path, "rb");
    if (f == NULL) {
        set_error(error, error_size, "Could not open TSR project");
        return 0;
    }
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic)) {
        fclose(f);
        ts_instrument_free(&loaded);
        set_error(error, error_size, "Truncated TSR project");
        return 0;
    }
    if (memcmp(magic, "TSR15\r\n\032", 8) == 0) {
        int ok = load_tsr15(f, instrument, error, error_size);
        fclose(f);
        ts_instrument_free(&loaded);
        return ok;
    }
    if (memcmp(magic, "TSR14\r\n\032", 8) == 0) version = 14;
    else if (memcmp(magic, "TSR13\r\n\032", 8) == 0) version = 13;
    else if (memcmp(magic, "TSR12\r\n\032", 8) == 0) version = 12;
    else if (memcmp(magic, "TSR11\r\n\032", 8) == 0) version = 11;
    else if (memcmp(magic, "TSR10\r\n\032", 8) == 0) version = 10;
    else if (memcmp(magic, "TSR9\r\n\032\n", 8) == 0) version = 9;
    else if (memcmp(magic, "TSR8\r\n\032\n", 8) == 0) version = 8;
    else if (memcmp(magic, "TSR7\r\n\032\n", 8) == 0) version = 7;
    else if (memcmp(magic, "TSR6\r\n\032\n", 8) == 0) version = 6;
    else {
        fclose(f);
        ts_instrument_free(&loaded);
        set_error(error, error_size,
                  "Not a self-contained TSR6-TSR15 project");
        return 0;
    }
#define GET_U32(dst) do { if (!get32(f, &u32)) goto malformed; (dst) = u32; } while (0)
#define GET_U64(dst) do { if (!get64(f, &u64) || u64 > SIZE_MAX) goto malformed; (dst) = (size_t)u64; } while (0)
#define GET_FLOAT(dst) do { if (!get_float(f, &(dst))) goto malformed; } while (0)
    GET_U32(loaded.source_kind); GET_U32(loaded.generation);
    if (!get64(f, &loaded.ancestor_hash)) goto malformed;
    GET_U32(loaded.generator.seed); GET_U32(loaded.generator.kind);
    GET_FLOAT(loaded.generator.seconds); GET_FLOAT(loaded.generator.frequency);
    if (version >= 13) {
        GET_U32(loaded.generator.has_fm_patch);
        if (loaded.generator.has_fm_patch && !get_fm_patch(f, &loaded.generator.fm_patch)) goto malformed;
    }
    if (version >= 11) {
        GET_U32(loaded.family_relation);
        GET_FLOAT(loaded.family_mutation);
        GET_U32(loaded.family_locks);
        GET_U32(loaded.family_sequence);
        GET_U32(loaded.family_trajectory);
        GET_U32(u32);
        loaded.family_anchor_slot = (int)u32 - 1;
        GET_U32(u32);
        loaded.family_last_slot = (int)u32 - 1;
        if (version >= 12) { GET_U32(loaded.selected_slot); }
        else loaded.selected_slot = loaded.family_anchor_slot;
    }
    if (version >= 10) {
        GET_U32(loaded.tuning.root_note);
        GET_FLOAT(loaded.tuning.fine_tune_cents);
        GET_U32(loaded.audible_tuning.root_note);
        GET_FLOAT(loaded.audible_tuning.fine_tune_cents);
    }
    GET_U32(state.process.seed);
    GET_FLOAT(state.process.body); GET_FLOAT(state.process.edge); GET_FLOAT(state.process.drift);
    GET_U32(state.process.noise_enabled); GET_FLOAT(state.process.noise_amount);
    GET_U32(state.process.noise_color); GET_U32(state.process.delay_enabled);
    GET_FLOAT(state.process.delay_seconds); GET_FLOAT(state.process.delay_feedback);
    GET_FLOAT(state.process.delay_damping); GET_FLOAT(state.process.delay_mix);
    GET_U32(state.process.reverb_enabled); GET_FLOAT(state.process.reverb_decay);
    GET_FLOAT(state.process.reverb_damping); GET_FLOAT(state.process.reverb_mix);
    if (version >= 9) {
        GET_U32(state.process.filter_enabled); GET_U32(state.process.filter_mode);
        GET_FLOAT(state.process.filter_cutoff_hz);
        GET_FLOAT(state.process.filter_resonance);
        GET_U32(state.process.shaper_enabled); GET_U32(state.process.shaper_mode);
        GET_FLOAT(state.process.shaper_drive); GET_FLOAT(state.process.shaper_mix);
    }
    GET_U64(state.crop_first); GET_U64(state.crop_last);
    GET_U64(state.selection_first); GET_U64(state.selection_last);
    GET_U64(state.view_first); GET_U64(state.view_last);
    GET_U64(state.loop_first); GET_U64(state.loop_last);
    GET_FLOAT(state.loop_crossfade_ms);
    if (version >= 8) GET_U32(state.loop_mode);
    else state.loop_mode = TS_LOOP_FORWARD;
    GET_U32(state.has_selection); GET_U32(state.has_loop);
    GET_U32(state.sample_edit_count);
    if (loaded.source_kind < TS_SOURCE_GENERATED || loaded.source_kind > TS_SOURCE_COMMITTED ||
        loaded.generator.kind >= TS_GENERATOR_COUNT ||
        (loaded.generator.has_fm_patch != 0 && loaded.generator.has_fm_patch != 1) ||
        loaded.family_relation < TS_FAMILY_CHILD ||
        loaded.family_relation > TS_FAMILY_STRANGER ||
        !isfinite(loaded.family_mutation) || loaded.family_mutation < 0.0f ||
        loaded.family_mutation > 1.0f ||
        (loaded.family_locks & ~TS_FAMILY_LOCK_ALL) != 0u ||
        (loaded.family_trajectory != 0 && loaded.family_trajectory != 1) ||
        loaded.family_anchor_slot < 0 ||
        loaded.family_anchor_slot >= TS_BANK_SLOT_COUNT ||
        loaded.family_last_slot < -1 ||
        loaded.family_last_slot >= TS_BANK_SLOT_COUNT ||
        loaded.selected_slot < 0 || loaded.selected_slot >= TS_BANK_SLOT_COUNT ||
        !tuning_valid(&loaded.tuning) ||
        !tuning_valid(&loaded.audible_tuning) ||
        state.loop_mode >= TS_LOOP_MODE_COUNT ||
        state.process.noise_color >= TS_NOISE_COLOR_COUNT ||
        state.sample_edit_count < 0 || state.sample_edit_count > TS_SAMPLE_EDIT_DEPTH ||
        (state.has_selection != 0 && state.has_selection != 1) ||
        (state.has_loop != 0 && state.has_loop != 1) ||
        (state.process.noise_enabled != 0 && state.process.noise_enabled != 1) ||
        (state.process.delay_enabled != 0 && state.process.delay_enabled != 1) ||
        (state.process.reverb_enabled != 0 && state.process.reverb_enabled != 1) ||
        (state.process.filter_enabled != 0 && state.process.filter_enabled != 1) ||
        state.process.filter_mode >= TS_FILTER_MODE_COUNT ||
        !isfinite(state.process.filter_cutoff_hz) ||
        state.process.filter_cutoff_hz < 20.0f || state.process.filter_cutoff_hz > 20000.0f ||
        !isfinite(state.process.filter_resonance) ||
        state.process.filter_resonance < 0.0f || state.process.filter_resonance > 1.0f ||
        (state.process.shaper_enabled != 0 && state.process.shaper_enabled != 1) ||
        state.process.shaper_mode >= TS_SHAPER_MODE_COUNT ||
        !isfinite(state.process.shaper_drive) || state.process.shaper_drive < 1.0f ||
        state.process.shaper_drive > 16.0f || !isfinite(state.process.shaper_mix) ||
        state.process.shaper_mix < 0.0f || state.process.shaper_mix > 1.0f ||
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
    if (version >= 8) {
        GET_U32(state.post_edit_count);
        if (state.post_edit_count < 0 || state.post_edit_count > TS_POST_EDIT_DEPTH)
            goto malformed;
        for (int i = 0; i < state.post_edit_count; ++i) {
            TsPostEdit *edit = &state.post_edits[i];
            uint64_t destination_bits;
            GET_U32(edit->kind);
            GET_U64(edit->first); GET_U64(edit->last);
            if (!get64(f, &destination_bits)) goto malformed;
            edit->destination = (int64_t)destination_bits;
            GET_FLOAT(edit->amount); GET_U32(edit->crossfade_frames);
            if (edit->kind > (version >= 14 ? TS_POST_TEAR : TS_POST_SMEAR) ||
                edit->last <= edit->first ||
                edit->first > 100000000u || edit->last > 100000000u ||
                edit->destination < -100000000LL ||
                edit->destination > 100000000LL ||
                !isfinite(edit->amount) || edit->crossfade_frames > 65536u)
                goto malformed;
        }
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
    if (!get64(f, &stored_hash) ||
        stored_hash != ts_sample_hash(&loaded.parent)) goto malformed;
    if (version == 6) {
        if (fgetc(f) != EOF ||
            !bank_root_clone(&loaded.bank[0], &loaded.parent, &loaded.tuning,
                             error, error_size))
            goto malformed;
    } else {
        GET_U32(bank_slot_count);
        if (bank_slot_count != TS_BANK_SLOT_COUNT) goto malformed;
        for (int slot_index = 0; slot_index < TS_BANK_SLOT_COUNT; ++slot_index) {
            TsBankSlot *slot = &loaded.bank[slot_index];
            uint64_t slot_hash;
            GET_U32(slot->occupied);
            if (slot->occupied != 0 && slot->occupied != 1) goto malformed;
            if (!slot->occupied) continue;
            GET_U32(slot->capture_kind);
            if (version >= 11) {
                GET_U32(slot->relation);
                GET_U32(u32);
                slot->parent_slot = (int)u32 - 1;
                GET_U32(slot->lineage_seed);
                GET_U32(slot->lineage_locks);
                GET_U32(slot->trajectory_step);
                GET_FLOAT(slot->lineage_mutation);
                GET_U32(slot->has_generator);
                if (slot->has_generator) {
                    GET_U32(slot->generator.seed);
                    GET_U32(slot->generator.kind);
                    GET_FLOAT(slot->generator.seconds);
                    GET_FLOAT(slot->generator.frequency);
                    if (version >= 13) {
                        GET_U32(slot->generator.has_fm_patch);
                        if (slot->generator.has_fm_patch &&
                            !get_fm_patch(f, &slot->generator.fm_patch)) goto malformed;
                    }
                }
            }
            if (version >= 10) {
                GET_U32(slot->tuning.root_note);
                GET_FLOAT(slot->tuning.fine_tune_cents);
                GET_U32(slot->audible_tuning.root_note);
                GET_FLOAT(slot->audible_tuning.fine_tune_cents);
            }
            if (version >= 8) GET_U32(slot->loop_mode);
            else slot->loop_mode = TS_LOOP_FORWARD;
            GET_U32(slot->has_loop);
            GET_U64(slot->loop_first); GET_U64(slot->loop_last);
            GET_FLOAT(slot->loop_crossfade_ms);
            GET_U32(slot->sample.sample_rate); GET_U64(slot->sample.frames);
            GET_U32(name_length);
            if (slot->capture_kind > TS_BANK_CAPTURE_LOOP ||
                slot->relation >= TS_FAMILY_RELATION_COUNT ||
                slot->parent_slot < -1 || slot->parent_slot >= TS_BANK_SLOT_COUNT ||
                (slot->lineage_locks & ~TS_FAMILY_LOCK_ALL) != 0u ||
                !isfinite(slot->lineage_mutation) ||
                slot->lineage_mutation < 0.0f || slot->lineage_mutation > 1.0f ||
                (slot->has_generator != 0 && slot->has_generator != 1) ||
                (slot->has_generator &&
                 (slot->generator.kind >= TS_GENERATOR_COUNT ||
                  (slot->generator.has_fm_patch != 0 && slot->generator.has_fm_patch != 1) ||
                  !isfinite(slot->generator.seconds) ||
                  slot->generator.seconds < 0.1f || slot->generator.seconds > 8.0f ||
                  !isfinite(slot->generator.frequency) ||
                  slot->generator.frequency < 30.0f ||
                  slot->generator.frequency > 2000.0f)) ||
                slot->loop_mode >= TS_LOOP_MODE_COUNT ||
                !tuning_valid(&slot->tuning) ||
                !tuning_valid(&slot->audible_tuning) ||
                (slot->has_loop != 0 && slot->has_loop != 1) ||
                !isfinite(slot->loop_crossfade_ms) ||
                slot->loop_crossfade_ms < 0.0f || slot->loop_crossfade_ms > 50.0f ||
                slot->sample.sample_rate < 1000 || slot->sample.frames == 0 ||
                slot->sample.frames > 100000000u ||
                slot->sample.frames > SIZE_MAX / sizeof(float) ||
                name_length >= sizeof(slot->sample.name)) goto malformed;
            if (fread(slot->sample.name, 1, name_length, f) != name_length) goto malformed;
            slot->sample.name[name_length] = '\0';
            slot->sample.data = (float *)malloc(slot->sample.frames * sizeof(float));
            if (slot->sample.data == NULL) goto out_of_memory;
            for (size_t frame = 0; frame < slot->sample.frames; ++frame)
                if (!get_float(f, &slot->sample.data[frame])) goto malformed;
            if (!get64(f, &slot_hash) || slot_hash != ts_sample_hash(&slot->sample) ||
                (slot->has_loop && (slot->loop_last <= slot->loop_first ||
                                    slot->loop_last > slot->sample.frames)) ||
                (!slot->has_loop && (slot->loop_first != 0 || slot->loop_last != 0)))
                goto malformed;
            if (version < 11) {
                slot->relation = slot_index == 0 ? TS_FAMILY_ROOT : TS_FAMILY_CAPTURED;
                slot->parent_slot = slot_index == 0 ? -1 : 0;
                slot->lineage_seed = (uint32_t)slot_hash;
                slot->lineage_locks = TS_FAMILY_LOCK_ALL;
                slot->lineage_mutation = 0.0f;
                slot->has_generator = slot_index == 0 &&
                                      loaded.source_kind == TS_SOURCE_GENERATED;
                if (slot->has_generator) slot->generator = loaded.generator;
            }
        }
        if ((version < 12 && (!loaded.bank[0].occupied ||
                              loaded.bank[0].capture_kind != TS_BANK_CAPTURE_ROOT ||
                              loaded.bank[0].relation != TS_FAMILY_ROOT)) ||
            (version >= 12 && version < 14 &&
             !loaded.bank[loaded.selected_slot].occupied) ||
            !loaded.bank[loaded.family_anchor_slot].occupied ||
            (loaded.family_last_slot >= 0 &&
             !loaded.bank[loaded.family_last_slot].occupied) ||
            fgetc(f) != EOF)
            goto malformed;
    }
    if (state.crop_first >= state.crop_last || state.crop_last > loaded.parent.frames)
        goto malformed;
    if (!render_snapshot(&loaded.current, &loaded, &state, error, error_size)) goto failed;
    if (state.selection_first > state.selection_last ||
        state.selection_last > loaded.current.frames ||
        state.view_first >= state.view_last ||
        state.view_last > loaded.current.frames ||
        state.loop_last > loaded.current.frames ||
        (state.has_loop && state.loop_last <= state.loop_first) ||
        (!state.has_loop && (state.loop_first != 0 || state.loop_last != 0))) goto malformed;
    loaded.process = state.process;
    loaded.crop_first = state.crop_first; loaded.crop_last = state.crop_last;
    loaded.selection_first = state.selection_first; loaded.selection_last = state.selection_last;
    loaded.view_first = state.view_first; loaded.view_last = state.view_last;
    loaded.loop_first = state.loop_first; loaded.loop_last = state.loop_last;
    loaded.loop_crossfade_ms = state.loop_crossfade_ms;
    loaded.loop_mode = state.loop_mode;
    loaded.has_selection = state.has_selection; loaded.has_loop = state.has_loop;
    memcpy(loaded.sample_edits, state.sample_edits, sizeof(loaded.sample_edits));
    loaded.sample_edit_count = state.sample_edit_count;
    memcpy(loaded.post_edits, state.post_edits, sizeof(loaded.post_edits));
    loaded.post_edit_count = state.post_edit_count;
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
    set_error(error, error_size, "Malformed or unsupported TSR6-TSR15 project");
failed:
    fclose(f);
    ts_instrument_free(&loaded);
#undef GET_U32
#undef GET_U64
#undef GET_FLOAT
    return 0;
}
