#include "tapesister/sample.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
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
static atomic_uint next_visual_revision = ATOMIC_VAR_INIT(1u);

static uint32_t sample_visual_revision(void)
{
    uint32_t revision = (uint32_t)atomic_fetch_add_explicit(
        &next_visual_revision, 1u, memory_order_relaxed);
    if (revision == 0u)
        revision = (uint32_t)atomic_fetch_add_explicit(
            &next_visual_revision, 1u, memory_order_relaxed);
    return revision;
}

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
    TsFmPatch safe = *patch;
    ts_fm_patch_sanitize(&safe);
    put32(f, (uint32_t)safe.structure);
    put32(f, (uint32_t)safe.ratio_family);
    put_float(f, safe.depth); put_float(f, safe.shape);
    put_float(f, safe.feedback); put_float(f, safe.transient_mix);
    for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op) put_float(f, safe.ratios[op]);
    put32(f, safe.genome_version);
    put32(f, safe.active_mask);
    put32(f, safe.mutation_mask);
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
        put32(f, (uint32_t)safe.waveforms[voice]);
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
        put_float(f, safe.lfo_rates[voice]);
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
        put_float(f, safe.lfo_depths[voice]);
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
        put32(f, (uint32_t)safe.lfo_types[voice]);
    put32(f, (uint32_t)safe.filter_mode);
    put_float(f, safe.filter_cutoff_hz);
    put_float(f, safe.filter_resonance);
    put_float(f, safe.filter_attack_seconds);
    put_float(f, safe.filter_release_seconds);
    put_float(f, safe.filter_envelope_amount);
    put32(f, (uint32_t)safe.interaction);
    put_float(f, safe.interaction_mix);
    put32(f, (uint32_t)safe.drone_mode);
    put32(f, (uint32_t)safe.extreme_mode);
    put32(f, (uint32_t)safe.pitch_lock);
    put32(f, (uint32_t)safe.pitch_root);
    put32(f, (uint32_t)safe.pitch_scale);
}

static int get_fm_patch(FILE *f, TsFmPatch *patch, int version)
{
    uint32_t value;
    memset(patch, 0, sizeof(*patch));
    if (!get32(f, &value)) return 0;
    patch->structure = (int)value;
    if (!get32(f, &value)) return 0;
    patch->ratio_family = (int)value;
    if (!get_float(f, &patch->depth) || !get_float(f, &patch->shape) ||
        !get_float(f, &patch->feedback) || !get_float(f, &patch->transient_mix)) return 0;
    for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op)
        if (!get_float(f, &patch->ratios[op])) return 0;
    if (version >= 22) {
        uint32_t expected_genome = version >= 26 ? 4u : version >= 23 ? 3u : 2u;
        if (!get32(f, &patch->genome_version) ||
            patch->genome_version != expected_genome ||
            !get32(f, &patch->active_mask) || !get32(f, &patch->mutation_mask)) return 0;
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            if (!get32(f, &value)) return 0;
            patch->waveforms[voice] = (int)value;
        }
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
            if (!get_float(f, &patch->lfo_rates[voice])) return 0;
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
            if (!get_float(f, &patch->lfo_depths[voice])) return 0;
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            if (!get32(f, &value)) return 0;
            patch->lfo_types[voice] = (int)value;
        }
        if (!get32(f, &value)) return 0;
        patch->filter_mode = (int)value;
        if (!get_float(f, &patch->filter_cutoff_hz) ||
            !get_float(f, &patch->filter_resonance) ||
            !get_float(f, &patch->filter_attack_seconds) ||
            !get_float(f, &patch->filter_release_seconds) ||
            !get_float(f, &patch->filter_envelope_amount) ||
            !get32(f, &value)) return 0;
        patch->interaction = (int)value;
        if (!get_float(f, &patch->interaction_mix)) return 0;
        if (version >= 23) {
            if (!get32(f, &value) || value > 1u) return 0;
            patch->drone_mode = (int)value;
            if (!get32(f, &value) || value > 1u) return 0;
            patch->extreme_mode = (int)value;
        } else {
            patch->drone_mode = 0;
            patch->extreme_mode = 0;
            patch->genome_version = 3u;
        }
        if (version >= 26) {
            if (!get32(f, &value) || value > 1u) return 0;
            patch->pitch_lock = (int)value;
            if (!get32(f, &value) || value > 11u) return 0;
            patch->pitch_root = (int)value;
            if (!get32(f, &value) || value >= TS_FM_PITCH_SCALE_COUNT) return 0;
            patch->pitch_scale = (int)value;
        } else {
            patch->pitch_lock = 1;
            patch->pitch_root = 0;
            patch->pitch_scale = TS_FM_PITCH_SCALE_MAJOR;
            patch->genome_version = 4u;
        }
        if ((patch->active_mask & ~((1u << TS_FM_OPERATOR_COUNT) - 1u)) != 0u ||
            (patch->mutation_mask & ~TS_FM_MUTATE_ALL) != 0u ||
            patch->filter_mode < 0 || patch->filter_mode >= TS_FILTER_MODE_COUNT ||
            patch->interaction < 0 || patch->interaction >= TS_FM_INTERACTION_COUNT ||
            (patch->pitch_lock != 0 && patch->pitch_lock != 1) ||
            patch->pitch_root < 0 || patch->pitch_root > 11 ||
            patch->pitch_scale < 0 ||
            patch->pitch_scale >= TS_FM_PITCH_SCALE_COUNT ||
            patch->filter_cutoff_hz < 20.0f || patch->filter_cutoff_hz > 20000.0f ||
            patch->filter_resonance < 0.0f ||
            patch->filter_resonance > (patch->extreme_mode ? 0.995f : 0.95f) ||
            patch->filter_attack_seconds < 0.001f || patch->filter_attack_seconds > 4.0f ||
            patch->filter_release_seconds < 0.01f || patch->filter_release_seconds > 8.0f ||
            patch->filter_envelope_amount < (patch->extreme_mode ? -2.0f : -1.0f) ||
            patch->filter_envelope_amount > (patch->extreme_mode ? 2.0f : 1.0f) ||
            patch->interaction_mix < 0.0f || patch->interaction_mix > 1.0f) return 0;
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
            if (patch->waveforms[voice] < 0 ||
                patch->waveforms[voice] >= TS_FM_WAVEFORM_COUNT ||
                patch->ratios[voice] < 0.05f ||
                patch->ratios[voice] > (patch->extreme_mode ? 64.0f : 16.0f) ||
                patch->lfo_rates[voice] < 0.03f ||
                patch->lfo_rates[voice] > (patch->extreme_mode ? 1000.0f : 160.0f) ||
                patch->lfo_depths[voice] < 0.0f ||
                patch->lfo_depths[voice] > (patch->extreme_mode ? 2.0f : 1.0f) ||
                patch->lfo_types[voice] < 0 ||
                patch->lfo_types[voice] >= TS_FM_LFO_TYPE_COUNT) return 0;
    } else ts_fm_patch_sanitize(patch);
    return patch->structure >= 0 && patch->structure < TS_FM_STRUCTURE_COUNT &&
           patch->ratio_family >= 0 && patch->ratio_family < TS_FM_RATIO_FAMILY_COUNT &&
           patch->depth >= 0.15f &&
           patch->depth <= (patch->extreme_mode ? 48.0f : 12.0f) &&
           patch->shape >= 0.0f && patch->shape <= 1.0f &&
           patch->feedback >= 0.0f &&
           patch->feedback <= (patch->extreme_mode ? 0.99f : 0.82f) &&
           patch->transient_mix >= 0.0f &&
           patch->transient_mix <= (patch->extreme_mode ? 1.0f : 0.60f);
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
    sample->visual_revision = sample_visual_revision();
}

void ts_sample_free(TsSample *sample)
{
    free(sample->data);
    ts_sample_init(sample);
}

void ts_sample_touch(TsSample *sample)
{
    if (sample != NULL) sample->visual_revision = sample_visual_revision();
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
    if (source == NULL) {
        set_error(error, error_size, "No sample to copy");
        return 0;
    }
    if (source->data == NULL && source->frames == 0u) {
        ts_sample_free(destination);
        destination->sample_rate = source->sample_rate;
        destination->visual_revision = source->visual_revision;
        snprintf(destination->name, sizeof(destination->name), "%s", source->name);
        set_error(error, error_size, "");
        return 1;
    }
    if (source->data == NULL || source->frames == 0u) {
        set_error(error, error_size, "Invalid sample storage");
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

int ts_sample_save_wav32f(const TsSample *sample, const char *path,
                          char *error, size_t error_size)
{
    FILE *f;
    uint32_t data_bytes;
    if (sample == NULL || sample->data == NULL || sample->frames == 0u ||
        sample->sample_rate == 0u) {
        set_error(error, error_size, "No sample to archive");
        return 0;
    }
    if (sample->frames > (UINT32_MAX - 36u) / sizeof(float)) {
        set_error(error, error_size, "Capture is too long for a RIFF WAV");
        return 0;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        set_error(error, error_size, "Could not create capture WAV");
        return 0;
    }
    data_bytes = (uint32_t)(sample->frames * sizeof(float));
    fwrite("RIFF", 1, 4, f); put32(f, 36u + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16u); put16(f, 3u); put16(f, 1u);
    put32(f, sample->sample_rate);
    put32(f, sample->sample_rate * (uint32_t)sizeof(float));
    put16(f, (uint16_t)sizeof(float)); put16(f, 32u);
    fwrite("data", 1, 4, f); put32(f, data_bytes);
    for (size_t frame = 0; frame < sample->frames; ++frame) {
        float value = isfinite(sample->data[frame]) ? sample->data[frame] : 0.0f;
        put_float(f, value);
    }
    {
        int write_failed = ferror(f);
        int close_failed = fclose(f) != 0;
        if (write_failed || close_failed) {
            set_error(error, error_size, "Could not finish capture WAV");
            return 0;
        }
    }
    set_error(error, error_size, "");
    return 1;
}

const char *ts_generator_name(TsGeneratorKind kind)
{
    static const char *names[] = {"TONAL", "METALLIC", "NOISE", "PULSE", "FM"};
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
    float phase = 0.0f, mod_phase = 0.0f, aux_phase = 0.0f;
    TsFmPatch fm_patch;
    float noise_lp = 0.0f, noise_slow = 0.0f;
    float seed_a = rng_unit(&rng);
    float seed_b = rng_unit(&rng);
    float seed_c = rng_unit(&rng);
    float seed_d = rng_unit(&rng);
    unsigned variation = rng & 3u;
    ts_fm_patch_from_recipe(recipe, &fm_patch);
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while generating sample");
        return 0;
    }
    if (recipe->kind == TS_GENERATOR_FM) {
        free(data);
        return ts_fm_render_sample(sample, &fm_patch, seconds, frequency, rate,
                                   recipe->seed, error, error_size);
    }
    {
        static const float pitch_ratios[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
        frequency = clampf(frequency * pitch_ratios[(rng >> 2) % 5u],
                           30.0f, 2000.0f);
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
        case TS_GENERATOR_FM:
            value = 0.0f;
            break;
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
        float body_amount = (body - 0.5f) * 2.0f;
        float shaped;
        /* BODY is a broad spectral-weight macro, not a gain control. Positive
           values reinforce the slow/low component; negative values remove it
           and retain more of the fast component. Center remains bit-neutral. */
        if (body_amount >= 0.0f)
            shaped = input + body_amount * (1.60f * low - 0.18f * (input - fast));
        else
            shaped = input + body_amount * 1.20f * low +
                     (-body_amount) * 0.35f * (input - fast);
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
    result.playhead_frame = instrument->playhead_frame;
    result.view_first = instrument->view_first;
    result.view_last = instrument->view_last;
    result.loop_first = instrument->loop_first;
    result.loop_last = instrument->loop_last;
    result.loop_crossfade_ms = instrument->loop_crossfade_ms;
    result.loop_mode = instrument->loop_mode;
    result.has_selection = instrument->has_selection;
    result.has_playhead = instrument->has_playhead;
    result.has_loop = instrument->has_loop;
    result.grid_divisions = instrument->grid_divisions;
    result.grid_snap = instrument->grid_snap;
    result.tuning = instrument->tuning;
    result.audible_tuning = instrument->audible_tuning;
    result.process = instrument->process;
    result.process_first = instrument->process_first;
    result.process_last = instrument->process_last;
    result.has_process_range = instrument->has_process_range;
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

static int ensure_edit_graph_capacity(TsInstrument *instrument, int needs_patch,
                                      char *error, size_t error_size);

static void reset_editor(TsInstrument *instrument)
{
    instrument->crop_first = 0;
    instrument->crop_last = instrument->parent.frames;
    instrument->selection_first = 0;
    instrument->selection_last = 0;
    instrument->has_selection = 0;
    instrument->playhead_frame = 0;
    instrument->has_playhead = 0;
    instrument->view_first = 0;
    instrument->view_last = instrument->parent.frames;
    instrument->loop_first = 0;
    instrument->loop_last = 0;
    instrument->loop_crossfade_ms = 8.0f;
    instrument->loop_mode = TS_LOOP_FORWARD;
    instrument->has_loop = 0;
    instrument->grid_divisions = TS_GRID_DIVISION_DEFAULT;
    instrument->grid_snap = 0;
    instrument->process_first = 0u;
    instrument->process_last = 0u;
    instrument->has_process_range = 0;
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

static int delete_range(TsSample *sample, size_t first, size_t last,
                        char *error, size_t error_size)
{
    float *data;
    size_t removed;
    size_t frames;
    if (sample == NULL || sample->data == NULL || first >= last || last > sample->frames) {
        set_error(error, error_size, "Invalid Cut range");
        return 0;
    }
    removed = last - first;
    frames = sample->frames - removed;
    if (frames == 0) {
        set_error(error, error_size, "Cut cannot remove the entire tile");
        return 0;
    }
    data = (float *)malloc(frames * sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory while cutting selection");
        return 0;
    }
    if (first > 0) memcpy(data, sample->data, first * sizeof(*data));
    if (last < sample->frames)
        memcpy(data + first, sample->data + last,
               (sample->frames - last) * sizeof(*data));
    free(sample->data);
    sample->data = data;
    sample->frames = frames;
    return 1;
}

static int resize_canvas_sample(TsSample *sample, int edge, int64_t delta,
                                char *error, size_t error_size)
{
    float *data;
    size_t amount;
    size_t frames;
    if (sample == NULL || sample->data == NULL || sample->frames < TS_CANVAS_MIN_FRAMES ||
        (edge != 1 && edge != 2)) {
        set_error(error, error_size, "Invalid audio canvas");
        return 0;
    }
    if (delta == 0) return 1;
    if (delta > 0) {
        amount = (uint64_t)delta > SIZE_MAX ? SIZE_MAX : (size_t)delta;
        if (amount > SIZE_MAX - sample->frames ||
            sample->frames + amount > TS_CANVAS_MAX_FRAMES ||
            sample->frames + amount > SIZE_MAX / sizeof(*data)) {
            set_error(error, error_size, "Audio canvas is too large");
            return 0;
        }
        frames = sample->frames + amount;
    } else {
        uint64_t magnitude = (uint64_t)(-(delta + 1)) + 1u;
        if (magnitude > SIZE_MAX) {
            set_error(error, error_size, "Invalid audio canvas contraction");
            return 0;
        }
        amount = (size_t)magnitude;
        if (amount > sample->frames - TS_CANVAS_MIN_FRAMES) {
            set_error(error, error_size, "Audio canvas reached its minimum length");
            return 0;
        }
        frames = sample->frames - amount;
    }
    data = (float *)calloc(frames, sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory resizing audio canvas");
        return 0;
    }
    if (delta > 0) {
        size_t offset = edge == 1 ? amount : 0u;
        memcpy(data + offset, sample->data, sample->frames * sizeof(*data));
    } else if (edge == 1) {
        memcpy(data, sample->data + amount, frames * sizeof(*data));
    } else {
        memcpy(data, sample->data, frames * sizeof(*data));
    }
    free(sample->data);
    sample->data = data;
    sample->frames = frames;
    return 1;
}

static int patch_range(TsSample *sample, const TsSample *patch,
                       size_t first, size_t last, int fit,
                       char *error, size_t error_size)
{
    float *resampled;
    float *output;
    size_t target_frames;
    size_t patch_frames;
    size_t output_frames;
    size_t gap_frames = 0;
    if (sample == NULL || sample->data == NULL || patch == NULL || patch->data == NULL ||
        patch->frames == 0 || patch->sample_rate == 0 || sample->sample_rate == 0 ||
        first > last ||
        (last > sample->frames && (fit || first != last || first < sample->frames)) ||
        (fit && first == last)) {
        set_error(error, error_size, "Invalid Paste range");
        return 0;
    }
    if (first > sample->frames) gap_frames = first - sample->frames;
    target_frames = last - first;
    if (fit) patch_frames = target_frames;
    else {
        double converted = (double)patch->frames * (double)sample->sample_rate /
                           (double)patch->sample_rate;
        if (converted > (double)SIZE_MAX) {
            set_error(error, error_size, "Clipboard is too large to paste");
            return 0;
        }
        patch_frames = (size_t)llround(converted);
        if (patch_frames == 0) patch_frames = 1;
    }
    if (sample->frames > SIZE_MAX - gap_frames ||
        sample->frames + gap_frames > SIZE_MAX - patch_frames ||
        sample->frames + gap_frames - target_frames + patch_frames >
        SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Pasted tile would be too large");
        return 0;
    }
    output_frames = sample->frames + gap_frames - target_frames + patch_frames;
    resampled = (float *)malloc(patch_frames * sizeof(*resampled));
    output = (float *)malloc(output_frames * sizeof(*output));
    if (resampled == NULL || output == NULL) {
        free(resampled); free(output);
        set_error(error, error_size, "Out of memory while pasting selection");
        return 0;
    }
    for (size_t frame = 0; frame < patch_frames; ++frame) {
        double position = patch_frames > 1u ?
                          (double)frame * (double)(patch->frames - 1u) /
                          (double)(patch_frames - 1u) : 0.0;
        size_t at = (size_t)position;
        double fraction = position - (double)at;
        float a = patch->data[at];
        float b = at + 1u < patch->frames ? patch->data[at + 1u] : a;
        resampled[frame] = a + (b - a) * (float)fraction;
    }
    if (sample->frames > 0)
        memcpy(output, sample->data,
               (first < sample->frames ? first : sample->frames) * sizeof(*output));
    if (gap_frames > 0)
        memset(output + sample->frames, 0, gap_frames * sizeof(*output));
    memcpy(output + first, resampled, patch_frames * sizeof(*output));
    if (last < sample->frames)
        memcpy(output + first + patch_frames, sample->data + last,
               (sample->frames - last) * sizeof(*output));
    free(resampled);
    free(sample->data);
    sample->data = output;
    sample->frames = output_frames;
    return 1;
}

static int replace_patch_range_joined(TsSample *sample, const TsSample *patch,
                                      size_t first, size_t last, size_t fade,
                                      char *error, size_t error_size)
{
    float *edges;
    size_t original_frames;
    size_t left_fade;
    size_t right_fade;
    size_t right_frames;
    size_t inserted_last;
    if (fade == 0u)
        return patch_range(sample, patch, first, last, 0, error, error_size);
    if (sample == NULL || sample->data == NULL || patch == NULL ||
        patch->data == NULL || first >= last || last > sample->frames) {
        set_error(error, error_size, "Invalid joined Paste range");
        return 0;
    }
    original_frames = sample->frames;
    left_fade = fade < first ? fade : first;
    right_frames = original_frames - last;
    right_fade = fade < right_frames ? fade : right_frames;
    if (left_fade > SIZE_MAX - right_fade ||
        left_fade + right_fade > SIZE_MAX / sizeof(*edges)) {
        set_error(error, error_size, "Joined Paste crossfade is too large");
        return 0;
    }
    edges = malloc((left_fade + right_fade) * sizeof(*edges));
    if (edges == NULL && left_fade + right_fade > 0u) {
        set_error(error, error_size, "Out of memory smoothing Paste joins");
        return 0;
    }
    if (left_fade > 0u)
        memcpy(edges, sample->data + first - left_fade,
               left_fade * sizeof(*edges));
    if (right_fade > 0u)
        memcpy(edges + left_fade, sample->data + last,
               right_fade * sizeof(*edges));
    if (!patch_range(sample, patch, first, last, 0, error, error_size)) {
        free(edges);
        return 0;
    }
    inserted_last = sample->frames - right_frames;
    for (size_t frame = 0u; frame < left_fade; ++frame) {
        double phase = (double)(frame + 1u) / (double)(left_fade + 1u);
        float wet = (float)(0.5 - 0.5 * cos(M_PI * phase));
        size_t at = first - left_fade + frame;
        sample->data[at] = edges[frame] * (1.0f - wet) +
                           sample->data[first] * wet;
    }
    for (size_t frame = 0u; frame < right_fade; ++frame) {
        double phase = (double)(frame + 1u) / (double)(right_fade + 1u);
        float dry = (float)(0.5 - 0.5 * cos(M_PI * phase));
        size_t at = inserted_last + frame;
        sample->data[at] = sample->data[inserted_last - 1u] * (1.0f - dry) +
                           edges[left_fade + frame] * dry;
    }
    free(edges);
    return 1;
}

static int fit_patch_range_crossfaded(TsSample *sample, const TsSample *patch,
                                      size_t first, size_t last, size_t fade,
                                      char *error, size_t error_size)
{
    float *edges;
    size_t length;
    if (fade == 0u)
        return patch_range(sample, patch, first, last, 1, error, error_size);
    if (sample == NULL || sample->data == NULL || first >= last ||
        last > sample->frames) {
        set_error(error, error_size, "Invalid crossfaded stamp range");
        return 0;
    }
    length = last - first;
    if (fade > length / 4u) fade = length / 4u;
    if (fade == 0u)
        return patch_range(sample, patch, first, last, 1, error, error_size);
    if (fade > SIZE_MAX / (2u * sizeof(*edges))) {
        set_error(error, error_size, "Crossfaded stamp is too large");
        return 0;
    }
    edges = malloc(fade * 2u * sizeof(*edges));
    if (edges == NULL) {
        set_error(error, error_size, "Out of memory crossfading audio stamp");
        return 0;
    }
    memcpy(edges, sample->data + first, fade * sizeof(*edges));
    memcpy(edges + fade, sample->data + last - fade,
           fade * sizeof(*edges));
    if (!patch_range(sample, patch, first, last, 1, error, error_size)) {
        free(edges);
        return 0;
    }
    for (size_t frame = 0u; frame < fade; ++frame) {
        float wet = (float)(frame + 1u) / (float)(fade + 1u);
        size_t at = first + frame;
        sample->data[at] = edges[frame] * (1.0f - wet) +
                           sample->data[at] * wet;
    }
    for (size_t frame = 0u; frame < fade; ++frame) {
        float wet = (float)(fade - frame) / (float)(fade + 1u);
        size_t at = last - fade + frame;
        sample->data[at] = edges[fade + frame] * (1.0f - wet) +
                           sample->data[at] * wet;
    }
    free(edges);
    return 1;
}

static const TsAudioPatch *audio_patch_for_operation(const TsInstrument *instrument,
                                                      const TsPostEdit *operation)
{
    const TsBankSlot *slot;
    if (instrument == NULL || operation == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) return NULL;
    slot = &instrument->bank[instrument->selected_slot];
    if (operation->patch_index >= (uint32_t)slot->patch_count) return NULL;
    return &slot->patches[operation->patch_index];
}

static int stretch_patch_range(TsSample *sample, const TsSample *patch,
                               size_t original_first, size_t original_last,
                               size_t target_first, int contracting,
                               size_t fade, char *error, size_t error_size)
{
    if (sample == NULL || sample->data == NULL || patch == NULL || patch->data == NULL ||
        original_first >= original_last || original_last > sample->frames ||
        patch->frames == 0 || target_first > sample->frames ||
        patch->frames > sample->frames - target_first) {
        set_error(error, error_size, "Invalid tape stretch range");
        return 0;
    }
    if (fade > patch->frames / 2u) fade = patch->frames / 2u;
    if (contracting) {
        for (size_t frame = original_first; frame < original_last; ++frame)
            sample->data[frame] = 0.0f;
        for (size_t i = 0; i < fade; ++i) {
            float gain = (float)(i + 1u) / (float)(fade + 1u);
            if (original_first > i)
                sample->data[original_first - 1u - i] *= gain;
            if (original_last + i < sample->frames)
                sample->data[original_last + i] *= gain;
        }
        for (size_t i = 0; i < patch->frames; ++i) {
            float gain = 1.0f;
            if (fade > 0u) {
                if (i < fade) gain = (float)(i + 1u) / (float)(fade + 1u);
                if (patch->frames - 1u - i < fade) {
                    float tail = (float)(patch->frames - i) / (float)(fade + 1u);
                    if (tail < gain) gain = tail;
                }
            }
            sample->data[target_first + i] = patch->data[i] * gain;
        }
    } else {
        float source_peak = 0.0f;
        float destination_peak = 0.0f;
        float summed_peak = 0.0f;
        float mix_scale = 1.0f;
        for (size_t i = 0; i < patch->frames; ++i) {
            size_t at = target_first + i;
            float edge_gain = 1.0f;
            float source;
            float summed;
            if (at >= original_first && at < original_last) continue;
            if (fade > 0u) {
                if (i < fade) edge_gain = (float)(i + 1u) / (float)(fade + 1u);
                if (patch->frames - 1u - i < fade) {
                    float tail = (float)(patch->frames - i) / (float)(fade + 1u);
                    if (tail < edge_gain) edge_gain = tail;
                }
            }
            source = patch->data[i] * edge_gain;
            summed = sample->data[at] + source;
            if (fabsf(source) > source_peak) source_peak = fabsf(source);
            if (fabsf(sample->data[at]) > destination_peak)
                destination_peak = fabsf(sample->data[at]);
            if (fabsf(summed) > summed_peak) summed_peak = fabsf(summed);
        }
        if (summed_peak > 0.0000001f) {
            float wanted = source_peak > destination_peak ? source_peak : destination_peak;
            if (wanted > 0.0000001f && wanted < summed_peak) mix_scale = wanted / summed_peak;
        }
        for (size_t i = 0; i < patch->frames; ++i) {
            size_t at = target_first + i;
            if (at >= original_first && at < original_last) {
                sample->data[at] = patch->data[i];
            } else {
                float edge_gain = 1.0f;
                if (fade > 0u) {
                    if (i < fade) edge_gain = (float)(i + 1u) / (float)(fade + 1u);
                    if (patch->frames - 1u - i < fade) {
                        float tail = (float)(patch->frames - i) / (float)(fade + 1u);
                        if (tail < edge_gain) edge_gain = tail;
                    }
                }
                sample->data[at] = clampf(sample->data[at] +
                                          patch->data[i] * edge_gain * mix_scale,
                                          -1.0f, 1.0f);
            }
        }
    }
    return 1;
}

static int render_snapshot(TsSample *destination, const TsInstrument *instrument,
                           const TsEditSnapshot *state, char *error, size_t error_size)
{
    TsSample edited;
    TsSample processed;
    int ok;
    ts_sample_init(&edited);
    ts_sample_init(&processed);
    if (!render_edit_source(&edited, &instrument->parent, state->crop_first,
                            state->crop_last, state->sample_edits,
                            state->sample_edit_count, error, error_size)) return 0;
    /* Rendered Transforms are canonical editable material, not an effect placed
       after the native process. Replay their checkpoint before BODY/EDGE/DRIFT
       and the native effect sections so later parameter changes always rebuild
       deterministically from the accepted audio. */
    for (int index = 0; index < state->post_edit_count; ++index) {
        const TsPostEdit *operation = &state->post_edits[index];
        const TsAudioPatch *patch;
        if (operation->kind != TS_POST_MATERIAL_REPLACE) continue;
        patch = audio_patch_for_operation(instrument, operation);
        if (patch == NULL) {
            ts_sample_free(&edited);
            set_error(error, error_size,
                      "Transform material is missing from this tile");
            return 0;
        }
        if (!patch_range(&edited, &patch->sample, operation->first,
                         operation->last, 0, error, error_size)) {
            ts_sample_free(&edited);
            return 0;
        }
    }
    if (state->has_process_range) {
        size_t first = state->process_first;
        size_t last = state->process_last;
        if (last > edited.frames) last = edited.frames;
        if (first >= last) {
            ts_sample_free(&edited);
            set_error(error, error_size,
                      "Native shelf selection is outside the material");
            return 0;
        }
        if (!ts_sample_clone(destination, &edited, error, error_size)) {
            ts_sample_free(&edited);
            return 0;
        }
        if (!ts_sample_process(&processed, &edited, first, last,
                               &state->process, error, error_size)) {
            ts_sample_free(destination);
            ts_sample_free(&edited);
            ts_sample_free(&processed);
            return 0;
        }
        memcpy(destination->data + first, processed.data,
               processed.frames * sizeof(*destination->data));
        ts_sample_free(&processed);
        ok = 1;
    } else {
        ok = ts_sample_process(destination, &edited, 0, edited.frames,
                               &state->process, error, error_size);
    }
    ts_sample_free(&edited);
    if (!ok) return 0;
    for (int index = 0; index < state->post_edit_count; ++index) {
        const TsPostEdit *operation = &state->post_edits[index];
        size_t first = operation->first;
        size_t last = operation->last;
        size_t length;
        if (operation->kind == TS_POST_MATERIAL_REPLACE) continue;
        if (operation->kind == TS_POST_CANVAS_LEFT_RESIZE ||
            operation->kind == TS_POST_CANVAS_RIGHT_RESIZE) {
            if (!resize_canvas_sample(
                    destination,
                    operation->kind == TS_POST_CANVAS_LEFT_RESIZE ? 1 : 2,
                    operation->destination, error, error_size))
                return 0;
            continue;
        }
        if (operation->kind == TS_POST_DELETE) {
            if (!delete_range(destination, first, last, error, error_size)) return 0;
            continue;
        }
        if (operation->kind == TS_POST_PATCH_REPLACE ||
            operation->kind == TS_POST_PATCH_FIT) {
            const TsAudioPatch *patch = audio_patch_for_operation(instrument, operation);
            if (patch == NULL) {
                set_error(error, error_size, "Paste source is missing from this tile");
                return 0;
            }
            if (operation->kind == TS_POST_PATCH_REPLACE &&
                operation->crossfade_frames > 0u) {
                if (!replace_patch_range_joined(
                        destination, &patch->sample, first, last,
                        operation->crossfade_frames, error, error_size)) return 0;
            } else if (operation->kind == TS_POST_PATCH_FIT &&
                operation->crossfade_frames > 0u) {
                if (!fit_patch_range_crossfaded(
                        destination, &patch->sample, first, last,
                        operation->crossfade_frames, error, error_size)) return 0;
            } else if (!patch_range(destination, &patch->sample, first, last,
                                    operation->kind == TS_POST_PATCH_FIT,
                                    error, error_size)) return 0;
            continue;
        }
        if (operation->kind == TS_POST_PATCH_STRETCH_EXPAND ||
            operation->kind == TS_POST_PATCH_STRETCH_CONTRACT) {
            const TsAudioPatch *patch = audio_patch_for_operation(instrument, operation);
            if (patch == NULL || operation->destination < 0) {
                set_error(error, error_size, "Tape stretch source is missing from this tile");
                return 0;
            }
            if (!stretch_patch_range(destination, &patch->sample, first, last,
                                     (size_t)operation->destination,
                                     operation->kind == TS_POST_PATCH_STRETCH_CONTRACT,
                                     operation->crossfade_frames,
                                     error, error_size)) return 0;
            continue;
        }
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
    if (kind == TS_BANK_CAPTURE_PERFORMANCE) return "PERFORM";
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
    slot->edit.grid_divisions = TS_GRID_DIVISION_DEFAULT;
    slot->edit.grid_snap = 0;
    ts_process_recipe_reset(&slot->process);
}

static void bank_slot_free(TsBankSlot *slot)
{
    ts_sample_free(&slot->sample);
    ts_sample_free(&slot->edit_parent);
    for (int i = 0; i < slot->patch_count; ++i)
        ts_sample_free(&slot->patches[i].sample);
    free(slot->patches);
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
    instrument->generator.frequency = 261.625565f;
    instrument->family_relation = TS_FAMILY_CHILD;
    instrument->family_mutation = 0.35f;
    instrument->family_locks = TS_FAMILY_LOCK_LOOP |
                               TS_FAMILY_LOCK_DURATION |
                               TS_FAMILY_LOCK_PITCH;
    instrument->family_anchor_slot = 0;
    instrument->family_last_slot = -1;
    instrument->selected_slot = 0;
    instrument->grid_divisions = TS_GRID_DIVISION_DEFAULT;
    instrument->grid_snap = 0;
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
    (void)generator;
    return default_tuning();
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
    if (instrument->bank[slot].locked) {
        set_error(error, error_size, "Tile is locked - unlock it before loading a WAV");
        return 0;
    }
    bank_slot_init(&imported);
    if (!ts_sample_load_wav_metadata(&imported.sample, &tuning, &has_loop,
                                     &loop_first, &loop_last, &loop_mode,
                                     path, error, error_size) ||
        !ts_sample_clone(&imported.edit_parent, &imported.sample,
                         error, error_size)) {
        bank_slot_free(&imported);
        return 0;
    }
    /* Audio stays exactly as imported. MIDI 60 is TapeSister's portable unity
       key; imported sampler metadata must not silently transpose that native
       playback contract. Loop metadata remains intact. */
    tuning = default_tuning();
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
    int process_changed;
    if (instrument == NULL || process == NULL || !tuning_valid(tuning) ||
        !tuning_valid(audible_tuning)) {
        set_error(error, error_size, "Invalid process or tuning settings");
        return 0;
    }
    target = snapshot(instrument);
    ts_sample_init(&current);
    process_changed = memcmp(process, &instrument->process,
                             sizeof(*process)) != 0;
    target.process = *process;
    target.tuning = *tuning;
    target.audible_tuning = *audible_tuning;
    if (process_changed) {
        if (instrument->has_selection &&
            instrument->selection_first < instrument->selection_last &&
            instrument->selection_last <= instrument->current.frames) {
            target.process_first = instrument->selection_first;
            target.process_last = instrument->selection_last;
            target.has_process_range = 1;
        } else {
            target.process_first = 0u;
            target.process_last = 0u;
            target.has_process_range = 0;
        }
    }
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = current;
    instrument->process = *process;
    instrument->process_first = target.process_first;
    instrument->process_last = target.process_last;
    instrument->has_process_range = target.has_process_range;
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
    target.process_first = 0u;
    target.process_last = 0u;
    target.has_process_range = 0;
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
    instrument->process_first = 0u;
    instrument->process_last = 0u;
    instrument->has_process_range = 0;
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
    uint32_t grid_divisions;
    int grid_snap;
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
    grid_divisions = instrument->grid_divisions;
    grid_snap = instrument->grid_snap;
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
    instrument->grid_divisions = grid_divisions;
    instrument->grid_snap = grid_snap;
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

int ts_instrument_select_all(TsInstrument *instrument)
{
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0)
        return 0;
    ts_instrument_set_selection(instrument, 0, instrument->current.frames);
    return instrument->has_selection && instrument->selection_first == 0 &&
           instrument->selection_last == instrument->current.frames;
}

int ts_instrument_select_wave(TsInstrument *instrument)
{
    size_t first;
    size_t last;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0)
        return 0;
    first = 0;
    while (first < instrument->current.frames &&
           instrument->current.data[first] == 0.0f)
        ++first;
    if (first == instrument->current.frames) return 0;
    last = instrument->current.frames;
    while (last > first && instrument->current.data[last - 1u] == 0.0f)
        --last;
    ts_instrument_set_selection(instrument, first, last);
    return instrument->has_selection && instrument->selection_first == first &&
           instrument->selection_last == last;
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

size_t ts_sample_nearest_zero_crossing_in_range(const TsSample *sample,
                                                size_t target,
                                                size_t first, size_t last)
{
    size_t maximum_distance;
    size_t closest;
    float closest_level;
    if (sample == NULL || sample->data == NULL || sample->frames == 0)
        return 0;
    if (first >= sample->frames) first = sample->frames - 1u;
    if (last >= sample->frames) last = sample->frames - 1u;
    if (first > last) {
        size_t swap = first;
        first = last;
        last = swap;
    }
    if (target < first) target = first;
    if (target > last) target = last;
    maximum_distance = target - first > last - target ?
                       target - first : last - target;
    for (size_t distance = 0; distance <= maximum_distance; ++distance) {
        if (distance <= target - first) {
            size_t left = target - distance;
            if (is_zero_crossing(sample, left)) return left;
        }
        if (distance > 0 && distance <= last - target) {
            size_t right = target + distance;
            if (is_zero_crossing(sample, right)) return right;
        }
    }
    closest = first;
    closest_level = fabsf(sample->data[first]);
    for (size_t frame = first + 1u; frame <= last; ++frame) {
        float level = fabsf(sample->data[frame]);
        if (level < closest_level) {
            closest = frame;
            closest_level = level;
        }
    }
    return closest;
}

static int make_drone_from_split(TsSample *destination, const TsSample *source,
                                 size_t first, size_t last, size_t split,
                                 size_t overlap, char *error, size_t error_size)
{
    TsSample made;
    size_t left_frames;
    size_t right_frames;
    size_t output_frames;
    size_t at = 0;
    left_frames = split - first;
    right_frames = last - split;
    output_frames = last - first - overlap;
    ts_sample_init(&made);
    made.data = (float *)malloc(output_frames * sizeof(*made.data));
    if (made.data == NULL) {
        set_error(error, error_size, "Out of memory creating Drone loop");
        return 0;
    }
    memcpy(made.data, source->data + split,
           (right_frames - overlap) * sizeof(*made.data));
    at += right_frames - overlap;
    for (size_t i = 0; i < overlap; ++i) {
        double phase = (double)(i + 1u) / (double)(overlap + 1u);
        float fade_in = (float)(0.5 - 0.5 * cos(M_PI * phase));
        float fade_out = 1.0f - fade_in;
        made.data[at++] = source->data[last - overlap + i] * fade_out +
                          source->data[first + i] * fade_in;
    }
    memcpy(made.data + at, source->data + first + overlap,
           (left_frames - overlap) * sizeof(*made.data));
    made.frames = output_frames;
    made.sample_rate = source->sample_rate;
    snprintf(made.name, sizeof(made.name), "DRONE %.120s", source->name);
    ts_sample_free(destination);
    *destination = made;
    set_error(error, error_size, "");
    return 1;
}

static size_t quiet_drone_split(const TsSample *source, size_t target,
                                size_t first, size_t last, size_t radius)
{
    size_t best = target;
    size_t best_distance = SIZE_MAX;
    size_t search_first;
    size_t search_last;
    double best_level = HUGE_VAL;
    int found = 0;
    /* The outer Drone seam is an untouched adjacent source pair. Choose the
       lowest-energy crossing in a narrow center neighborhood, using distance
       from center as the tiebreaker, so committed loops start and stop quietly
       without moving the established midpoint split to an unrelated passage. */
    search_first = target > radius ? target - radius : 0u;
    if (search_first < first) search_first = first;
    search_last = radius > SIZE_MAX - target ? SIZE_MAX : target + radius;
    if (search_last > last) search_last = last;
    for (size_t frame = search_first; frame <= search_last; ++frame) {
        size_t distance;
        double level;
        if (!is_zero_crossing(source, frame)) continue;
        level = fabs((double)source->data[frame]) +
                fabs((double)source->data[frame - 1u]);
        if (!isfinite(level)) continue;
        distance = frame > target ? frame - target : target - frame;
        if (!found || level < best_level ||
            (level == best_level && distance < best_distance)) {
            best = frame;
            best_level = level;
            best_distance = distance;
            found = 1;
        }
    }
    if (found) return best;
    return ts_sample_nearest_zero_crossing_in_range(source, target, first, last);
}

int ts_sample_make_drone_at_split(TsSample *destination, const TsSample *source,
                                  size_t first, size_t last, size_t split_frame,
                                  size_t requested_overlap_frames,
                                  size_t *effective_overlap_frames,
                                  char *error, size_t error_size)
{
    size_t selection_frames;
    size_t maximum_overlap;
    size_t overlap;
    if (effective_overlap_frames != NULL) *effective_overlap_frames = 0;
    if (destination == NULL || source == NULL || source->data == NULL ||
        source->sample_rate == 0 || first >= last || last > source->frames) {
        set_error(error, error_size, "Drone needs a valid nonempty selection");
        return 0;
    }
    selection_frames = last - first;
    if (selection_frames < 4u) {
        set_error(error, error_size, "Drone selection is too short");
        return 0;
    }
    if (split_frame <= first || split_frame >= last) {
        set_error(error, error_size, "Drone split is outside the selection");
        return 0;
    }
    maximum_overlap = selection_frames / 4u;
    if (maximum_overlap >= split_frame - first)
        maximum_overlap = split_frame - first - 1u;
    if (maximum_overlap >= last - split_frame)
        maximum_overlap = last - split_frame - 1u;
    if (maximum_overlap == 0) {
        set_error(error, error_size, "Drone split cannot support a crossfade");
        return 0;
    }
    overlap = requested_overlap_frames;
    if (overlap < 1u) overlap = 1u;
    if (overlap > maximum_overlap) overlap = maximum_overlap;
    if (!make_drone_from_split(destination, source, first, last, split_frame,
                               overlap, error, error_size))
        return 0;
    if (effective_overlap_frames != NULL) *effective_overlap_frames = overlap;
    return 1;
}

int ts_sample_make_drone(TsSample *destination, const TsSample *source,
                         size_t first, size_t last, int crossfade_ms,
                         size_t *split_frame, size_t *overlap_frames,
                         char *error, size_t error_size)
{
    size_t selection_frames;
    size_t overlap;
    size_t split;
    if (split_frame != NULL) *split_frame = 0;
    if (overlap_frames != NULL) *overlap_frames = 0;
    if (destination == NULL || source == NULL || source->data == NULL ||
        source->sample_rate == 0 || first >= last || last > source->frames) {
        set_error(error, error_size, "Drone needs a valid nonempty selection");
        return 0;
    }
    selection_frames = last - first;
    if (selection_frames < 4u) {
        set_error(error, error_size, "Drone selection is too short");
        return 0;
    }
    if (crossfade_ms < 0) {
        set_error(error, error_size, "Drone crossfade cannot be negative");
        return 0;
    }
    overlap = selection_frames / 4u;
    {
        double requested = (double)source->sample_rate *
                           (double)crossfade_ms / 1000.0;
        if (requested < 1.0) requested = 1.0;
        if (requested < (double)overlap)
            overlap = (size_t)llround(requested);
    }
    if (overlap == 0) {
        set_error(error, error_size, "Drone selection cannot support a crossfade");
        return 0;
    }
    split = quiet_drone_split(source, first + selection_frames / 2u,
                              first + overlap + 1u,
                              last - overlap - 1u,
                              selection_frames / 8u);
    if (!ts_sample_make_drone_at_split(destination, source, first, last, split,
                                       overlap, &overlap, error, error_size))
        return 0;
    if (split_frame != NULL) *split_frame = split;
    if (overlap_frames != NULL) *overlap_frames = overlap;
    return 1;
}

static int valid_grid_divisions(uint32_t divisions)
{
    return divisions >= TS_GRID_DIVISION_MIN &&
           divisions <= TS_GRID_DIVISION_MAX &&
           (divisions & (divisions - 1u)) == 0u;
}

static size_t grid_target_for_frames(size_t frames, uint32_t divisions, size_t frame)
{
    size_t index;
    size_t quotient;
    size_t remainder;
    if (frames == 0) return 0;
    if (frame >= frames) return frames;
    if (!valid_grid_divisions(divisions)) divisions = TS_GRID_DIVISION_DEFAULT;
    if (frame <= (SIZE_MAX - frames / 2u) / divisions)
        index = (frame * divisions + frames / 2u) / frames;
    else
        index = (size_t)floorl((long double)frame * divisions / frames + 0.5L);
    if (index > divisions) index = divisions;
    quotient = frames / divisions;
    remainder = frames % divisions;
    return quotient * index + remainder * index / divisions;
}

static size_t resolve_sample_boundary(const TsSample *sample,
                                      uint32_t divisions, int grid_snap,
                                      size_t frame)
{
    size_t target;
    if (sample == NULL || sample->data == NULL || sample->frames == 0) return 0;
    if (frame >= sample->frames) return sample->frames;
    target = grid_snap ? grid_target_for_frames(sample->frames, divisions, frame) : frame;
    if (target == 0 || target >= sample->frames) return target;
    return ts_sample_nearest_zero_crossing(sample, target);
}

size_t ts_instrument_grid_target(const TsInstrument *instrument, size_t frame)
{
    if (instrument == NULL) return 0;
    return grid_target_for_frames(instrument->current.frames,
                                  instrument->grid_divisions, frame);
}

size_t ts_instrument_resolve_boundary(const TsInstrument *instrument, size_t frame)
{
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0) return 0;
    return resolve_sample_boundary(&instrument->current,
                                   instrument->grid_divisions,
                                   instrument->grid_snap == TS_GRID_SNAP_ALL,
                                   frame);
}

int ts_instrument_grid_moves_snap(const TsInstrument *instrument)
{
    return instrument != NULL && instrument->grid_snap != TS_GRID_SNAP_OFF;
}

static void sync_selected_grid_state(TsInstrument *instrument)
{
    TsBankSlot *slot;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) return;
    slot = &instrument->bank[instrument->selected_slot];
    if (!slot->occupied) return;
    slot->edit.grid_divisions = instrument->grid_divisions;
    slot->edit.grid_snap = instrument->grid_snap;
}

int ts_instrument_set_grid_divisions(TsInstrument *instrument, uint32_t divisions)
{
    if (instrument == NULL || !valid_grid_divisions(divisions) ||
        instrument->grid_divisions == divisions) return 0;
    instrument->grid_divisions = divisions;
    sync_selected_grid_state(instrument);
    return 1;
}

int ts_instrument_cycle_grid_divisions(TsInstrument *instrument, int direction)
{
    uint32_t divisions;
    if (instrument == NULL || direction == 0) return 0;
    divisions = valid_grid_divisions(instrument->grid_divisions) ?
                instrument->grid_divisions : TS_GRID_DIVISION_DEFAULT;
    if (direction < 0) {
        if (divisions <= TS_GRID_DIVISION_MIN) return 0;
        divisions /= 2u;
    } else {
        if (divisions >= TS_GRID_DIVISION_MAX) return 0;
        divisions *= 2u;
    }
    instrument->grid_divisions = divisions;
    sync_selected_grid_state(instrument);
    return 1;
}

int ts_instrument_toggle_grid_snap(TsInstrument *instrument)
{
    if (instrument == NULL) return 0;
    instrument->grid_snap = (instrument->grid_snap + 1) % TS_GRID_SNAP_MODE_COUNT;
    sync_selected_grid_state(instrument);
    return 1;
}

void ts_instrument_set_selection_snapped(TsInstrument *instrument, size_t first, size_t last)
{
    size_t snapped_first;
    size_t snapped_last;
    if (instrument == NULL || instrument->current.data == NULL) return;
    snapped_first = ts_instrument_resolve_boundary(instrument, first);
    snapped_last = ts_instrument_resolve_boundary(instrument, last);
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

void ts_instrument_set_playhead(TsInstrument *instrument, size_t frame)
{
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0) return;
    if (frame >= instrument->current.frames) frame = instrument->current.frames - 1u;
    instrument->playhead_frame = frame;
    instrument->has_playhead = 1;
}

void ts_instrument_set_playhead_snapped(TsInstrument *instrument, size_t frame)
{
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0) return;
    if (frame >= instrument->current.frames) frame = instrument->current.frames - 1u;
    ts_instrument_set_playhead(
        instrument, ts_sample_nearest_zero_crossing(&instrument->current, frame));
}

void ts_instrument_clear_playhead(TsInstrument *instrument)
{
    if (instrument == NULL) return;
    instrument->playhead_frame = 0;
    instrument->has_playhead = 0;
}

int ts_instrument_reset_selection_playhead(TsInstrument *instrument)
{
    int changed;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0) return 0;
    changed = instrument->has_selection || !instrument->has_playhead ||
              instrument->playhead_frame != 0;
    ts_instrument_clear_selection(instrument);
    ts_instrument_set_playhead(instrument, 0);
    return changed;
}

size_t ts_sample_zero_crossing_in_direction(const TsSample *sample, size_t frame,
                                            int direction, size_t count)
{
    size_t at;
    if (sample == NULL || sample->data == NULL || sample->frames == 0 || count == 0)
        return frame;
    at = frame > sample->frames ? sample->frames : frame;
    while (count > 0) {
        if (direction < 0) {
            if (at == 0) return 0;
            --at;
            while (at > 0 && !is_zero_crossing(sample, at)) --at;
        } else {
            if (at >= sample->frames) return sample->frames;
            ++at;
            while (at < sample->frames && !is_zero_crossing(sample, at)) ++at;
        }
        --count;
    }
    return at;
}

int ts_instrument_resize_selection(TsInstrument *instrument, int endpoint,
                                   int expand, size_t crossing_count)
{
    size_t moved;
    if (instrument == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first ||
        (endpoint != 1 && endpoint != 2) || crossing_count == 0) return 0;
    if (endpoint == 1) {
        moved = ts_sample_zero_crossing_in_direction(
            &instrument->current, instrument->selection_first,
            expand ? -1 : 1, crossing_count);
        if (moved >= instrument->selection_last) return 0;
        if (moved == instrument->selection_first) return 0;
        instrument->selection_first = moved;
    } else {
        moved = ts_sample_zero_crossing_in_direction(
            &instrument->current, instrument->selection_last,
            expand ? 1 : -1, crossing_count);
        if (moved <= instrument->selection_first) return 0;
        if (moved == instrument->selection_last) return 0;
        instrument->selection_last = moved;
    }
    return 1;
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
    snapped = ts_instrument_resolve_boundary(instrument, frame);
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
    size_t old_frames;
    size_t view_span;
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
    if (!ensure_edit_graph_capacity(instrument, 0, error, error_size)) return 0;
    length = last - first;
    old_frames = instrument->current.frames;
    view_span = instrument->view_last > instrument->view_first ?
                instrument->view_last - instrument->view_first : old_frames;
    if (ts_instrument_grid_moves_snap(instrument) && destination >= 0 &&
        (uint64_t)destination <= instrument->current.frames)
        destination = (int64_t)ts_instrument_grid_target(
            instrument, (size_t)destination);
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
    if (prepend > 0u && target.has_playhead) target.playhead_frame += prepend;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    if (current.frames > old_frames) {
        size_t wanted = view_span > length ? view_span : length;
        if (wanted > current.frames) wanted = current.frames;
        if (prepend > 0u) {
            target.view_first = 0u;
            target.view_last = wanted;
        } else {
            target.view_last = current.frames;
            target.view_first = current.frames - wanted;
        }
    }
    begin_edit(instrument);
    replace_current_preserving_view(instrument, &current);
    instrument->selection_first = target.selection_first;
    instrument->selection_last = target.selection_last;
    instrument->has_selection = 1;
    instrument->playhead_frame = target.playhead_frame;
    instrument->has_playhead = target.has_playhead;
    instrument->loop_first = target.loop_first;
    instrument->loop_last = target.loop_last;
    instrument->view_first = target.view_first;
    instrument->view_last = target.view_last;
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

int ts_instrument_bank_next_empty(const TsInstrument *instrument)
{
    int start;
    if (instrument == NULL) return -1;
    start = instrument->selected_slot >= 0 &&
            instrument->selected_slot < TS_BANK_SLOT_COUNT ?
            instrument->selected_slot : TS_BANK_SLOT_COUNT - 1;
    for (int offset = 1; offset <= TS_BANK_SLOT_COUNT; ++offset) {
        int slot = (start + offset) % TS_BANK_SLOT_COUNT;
        if (!instrument->bank[slot].occupied) return slot;
    }
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
    copy.patches = NULL;
    copy.patch_capacity = 0;
    if (!ts_sample_clone(&copy.sample, &source->sample, error, error_size) ||
        !ts_sample_clone(&copy.edit_parent, source->edit_parent.data != NULL ?
                         &source->edit_parent : &source->sample, error, error_size)) goto failed;
    if (source->patch_count > 0) {
        copy.patches = calloc((size_t)source->patch_count, sizeof(*copy.patches));
        if (copy.patches == NULL) goto failed;
        copy.patch_capacity = source->patch_count;
    }
    for (int i = 0; i < source->patch_count; ++i) {
        copy.patches[i].generator = source->patches[i].generator;
        copy.patches[i].has_generator = source->patches[i].has_generator;
        if (!ts_sample_clone(&copy.patches[i].sample, &source->patches[i].sample,
                             error, error_size)) goto failed;
    }
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
    if (copy.patches != NULL)
        for (int i = 0; i < source->patch_count; ++i)
            ts_sample_free(&copy.patches[i].sample);
    free(copy.patches);
    free(copy.undo); free(copy.redo);
    set_error(error, error_size, "Out of memory deep-copying tile state"); return 0;
}

int ts_instrument_clone(TsInstrument *destination,
                        const TsInstrument *source,
                        char *error, size_t error_size)
{
    TsInstrument copy;
    if (destination == NULL || source == NULL) {
        set_error(error, error_size, "Instrument clone storage is unavailable");
        return 0;
    }
    if (destination == source) {
        set_error(error, error_size, "");
        return 1;
    }
    copy = *source;
    ts_sample_init(&copy.parent);
    ts_sample_init(&copy.current);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        bank_slot_init(&copy.bank[slot]);
    if (!ts_sample_clone(&copy.parent, &source->parent, error, error_size) ||
        !ts_sample_clone(&copy.current, &source->current, error, error_size))
        goto failed;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        if (!source->bank[slot].occupied) continue;
        if (!bank_slot_deep_clone(&copy.bank[slot], &source->bank[slot],
                                  error, error_size)) goto failed;
    }
    ts_instrument_free(destination);
    *destination = copy;
    set_error(error, error_size, "");
    return 1;

failed:
    ts_instrument_free(&copy);
    return 0;
}

int ts_instrument_select_bank(TsInstrument *instrument, int slot,
                              char *error, size_t error_size)
{
    TsSample parent, current;
    TsProcessRecipe neutral;
    TsBankSlot *chosen;
    size_t previous_playhead = 0;
    size_t previous_frames = 0;
    int previous_has_playhead = 0;
    int switching;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank tile"); return 0;
    }
    switching = instrument->selected_slot != slot;
    if (switching) {
        previous_playhead = instrument->playhead_frame;
        previous_frames = instrument->current.frames;
        previous_has_playhead = instrument->has_playhead;
    }
    if (switching &&
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
        instrument->playhead_frame = state.playhead_frame;
        instrument->view_first = state.view_first; instrument->view_last = state.view_last;
        instrument->loop_first = state.loop_first; instrument->loop_last = state.loop_last;
        instrument->loop_crossfade_ms = state.loop_crossfade_ms; instrument->loop_mode = state.loop_mode;
        instrument->has_selection = state.has_selection;
        instrument->has_playhead = state.has_playhead;
        instrument->has_loop = state.has_loop;
        instrument->grid_divisions = state.grid_divisions;
        instrument->grid_snap = state.grid_snap;
        instrument->process = state.process;
        instrument->process_first = state.process_first;
        instrument->process_last = state.process_last;
        instrument->has_process_range = state.has_process_range;
        memcpy(instrument->sample_edits, state.sample_edits, sizeof(instrument->sample_edits));
        instrument->sample_edit_count = state.sample_edit_count;
        memcpy(instrument->post_edits, state.post_edits, sizeof(instrument->post_edits));
        instrument->post_edit_count = state.post_edit_count;
    } else reset_editor(instrument);
    if (switching && !instrument->has_playhead && previous_has_playhead &&
        previous_frames > 0 && instrument->current.frames > 0) {
        if (previous_frames > 1u && instrument->current.frames > 1u) {
            double position = (double)previous_playhead /
                              (double)(previous_frames - 1u);
            instrument->playhead_frame = (size_t)llround(
                position * (double)(instrument->current.frames - 1u));
        } else instrument->playhead_frame = 0;
        instrument->has_playhead = 1;
    }
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
    if (instrument->bank[slot].locked) {
        set_error(error, error_size, "Tile is locked - unlock it before Create");
        return 0;
    }
    bank_slot_init(&made);
    recipe = instrument->generator;
    recipe.kind = TS_GENERATOR_FM; recipe.seed = seed;
    if (instrument->generator.kind == TS_GENERATOR_FM &&
        instrument->generator.has_fm_patch &&
        seed != instrument->generator.seed) {
        TsFmPatch source_patch;
        ts_fm_patch_from_recipe(&instrument->generator, &source_patch);
        recipe.has_fm_patch = 1;
        ts_fm_patch_vary(&source_patch, seed, 1.0f, &recipe.fm_patch);
    }
    if (seed != instrument->generator.seed || !instrument->generator.has_fm_patch) {
        recipe.seconds = 0.1f + rng_unit(&rng) * 7.9f;
        (void)rng_unit(&rng);
    }
    recipe.frequency = 261.625565f;
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

int ts_instrument_apply_fm_patch(TsInstrument *instrument,
                                 const TsFmPatch *patch,
                                 char *error, size_t error_size)
{
    TsGeneratorRecipe recipe;
    uint32_t seed;
    if (instrument == NULL || patch == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Select a tile before applying FM logic");
        return 0;
    }
    recipe = instrument->generator;
    if (instrument->bank[instrument->selected_slot].occupied &&
        instrument->bank[instrument->selected_slot].has_generator &&
        instrument->bank[instrument->selected_slot].generator.kind == TS_GENERATOR_FM)
        recipe = instrument->bank[instrument->selected_slot].generator;
    seed = recipe.seed != 0u ? recipe.seed :
           advance_seed(instrument->family_sequence ^ 0x464d4c47u);
    recipe.kind = TS_GENERATOR_FM;
    recipe.seed = seed;
    recipe.has_fm_patch = 1;
    recipe.fm_patch = *patch;
    recipe.frequency = 261.625565f;
    ts_fm_patch_sanitize(&recipe.fm_patch);
    instrument->generator = recipe;
    return ts_instrument_create_selected(instrument, seed, error, error_size);
}

int ts_instrument_make_fm_bank(TsInstrument *instrument,
                               const TsFmPatch *patch,
                               char *error, size_t error_size)
{
    TsInstrument made;
    TsFmPatch anchor;
    TsFmPatch previous;
    uint32_t seed;
    float mutation;
    int chain;
    if (instrument == NULL || patch == NULL) {
        set_error(error, error_size, "FM Bank Maker input is unavailable");
        return 0;
    }
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        if (instrument->bank[slot].locked) {
            set_error(error, error_size,
                      "Unlock protected tiles or make the bank on a new page");
            return 0;
        }
    }
    anchor = *patch;
    ts_fm_patch_sanitize(&anchor);
    previous = anchor;
    mutation = clampf(instrument->family_mutation, 0.0f, 1.0f);
    chain = instrument->family_trajectory != 0;
    seed = instrument->generator.seed != 0u ? instrument->generator.seed :
           advance_seed(instrument->family_sequence ^ 0x42414e4bu);
    ts_instrument_init(&made);
    made.family_mutation = mutation;
    made.family_trajectory = chain;
    made.family_relation = instrument->family_relation;
    made.family_locks = instrument->family_locks;
    made.generator.seconds = instrument->generator.seconds >= 0.1f &&
                             instrument->generator.seconds <= 8.0f ?
                             instrument->generator.seconds : 2.0f;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        TsFmPatch next = anchor;
        if (slot > 0) {
            seed = advance_seed(seed ^ (uint32_t)(slot * 0x9e3779b9u));
            ts_fm_patch_vary(chain ? &previous : &anchor,
                             seed, mutation, &next);
        }
        made.selected_slot = slot;
        made.generator.kind = TS_GENERATOR_FM;
        made.generator.seed = seed;
        made.generator.frequency = 261.625565f;
        made.generator.has_fm_patch = 1;
        made.generator.fm_patch = next;
        if (!ts_instrument_apply_fm_patch(&made, &next, error, error_size)) {
            ts_instrument_free(&made);
            return 0;
        }
        made.bank[slot].relation = slot == 0 ? TS_FAMILY_ROOT :
                                   chain ? TS_FAMILY_CHILD : TS_FAMILY_COUSIN;
        made.bank[slot].parent_slot = slot == 0 ? -1 : chain ? slot - 1 : 0;
        made.bank[slot].trajectory_step = (uint32_t)slot;
        made.bank[slot].lineage_mutation = mutation;
        made.bank[slot].locked = 0;
        previous = next;
    }
    if (!ts_instrument_select_bank(&made, 0, error, error_size)) {
        ts_instrument_free(&made);
        return 0;
    }
    made.family_anchor_slot = 0;
    made.family_last_slot = TS_BANK_SLOT_COUNT - 1;
    ts_instrument_free(instrument);
    *instrument = made;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_activate_silence(TsInstrument *instrument, size_t frames,
                                   uint32_t sample_rate,
                                   char *error, size_t error_size)
{
    TsBankSlot made;
    int slot;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Select an empty bank tile first");
        return 0;
    }
    slot = instrument->selected_slot;
    if (instrument->bank[slot].occupied) {
        set_error(error, error_size, "Silent activation needs an empty selected tile");
        return 0;
    }
    if (frames == 0 || sample_rate == 0 || frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Invalid silent tile duration");
        return 0;
    }
    bank_slot_init(&made);
    made.sample.data = (float *)calloc(frames, sizeof(*made.sample.data));
    if (made.sample.data == NULL) {
        set_error(error, error_size, "Out of memory creating silent tile");
        return 0;
    }
    made.sample.frames = frames;
    made.sample.sample_rate = sample_rate;
    snprintf(made.sample.name, sizeof(made.sample.name), "SILENCE");
    if (!ts_sample_clone(&made.edit_parent, &made.sample, error, error_size)) {
        bank_slot_free(&made);
        return 0;
    }
    made.occupied = 1;
    made.capture_kind = TS_BANK_CAPTURE_CURRENT;
    made.relation = TS_FAMILY_CAPTURED;
    made.parent_slot = -1;
    made.has_generator = 0;
    made.lineage_seed = (uint32_t)ts_sample_hash(&made.sample);
    made.lineage_locks = TS_FAMILY_LOCK_ALL;
    made.lineage_mutation = 0.0f;
    bank_slot_free(&instrument->bank[slot]);
    instrument->bank[slot] = made;
    instrument->source_kind = TS_SOURCE_IMPORTED;
    memset(&instrument->generator, 0, sizeof(instrument->generator));
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
    instrument->bank[destination_slot].locked = 0;
    instrument->bank[destination_slot].parent_slot = source;
    return ts_instrument_select_bank(instrument, destination_slot, error, error_size);
}

int ts_instrument_copy_bank_slot_from(TsInstrument *destination,
                                      int destination_slot,
                                      TsInstrument *source, int source_slot,
                                      char *error, size_t error_size)
{
    TsBankSlot *copied;
    if (destination == NULL || source == NULL ||
        destination_slot < 0 || destination_slot >= TS_BANK_SLOT_COUNT ||
        source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT ||
        destination->bank[destination_slot].occupied ||
        !source->bank[source_slot].occupied) {
        set_error(error, error_size,
                  "Copy needs an occupied source and empty destination tile");
        return 0;
    }
    if (!ts_instrument_select_bank(source, source_slot, error, error_size) ||
        !bank_sync_selected(source, error, error_size) ||
        !bank_slot_deep_clone(&destination->bank[destination_slot],
                              &source->bank[source_slot], error, error_size))
        return 0;
    copied = &destination->bank[destination_slot];
    if (destination != source) copied->parent_slot = -1;
    return ts_instrument_select_bank(destination, destination_slot,
                                     error, error_size);
}

static size_t replace_point(size_t point, size_t first, size_t last,
                            size_t inserted)
{
    if (point <= first) return point;
    if (point >= last) {
        if (inserted >= last - first) return point + inserted - (last - first);
        return point - ((last - first) - inserted);
    }
    if (point - first > inserted) return first + inserted;
    return first + (point - first);
}

static void update_snapshot_after_replace(TsEditSnapshot *target,
                                          size_t first, size_t last,
                                          size_t inserted, size_t frames,
                                          int select_inserted)
{
    size_t view_first = replace_point(target->view_first, first, last, inserted);
    size_t view_last = replace_point(target->view_last, first, last, inserted);
    if (view_last > frames) view_last = frames;
    if (view_first >= view_last) {
        view_first = 0;
        view_last = frames;
    }
    target->view_first = view_first;
    target->view_last = view_last;
    if (target->has_playhead) {
        target->playhead_frame = replace_point(target->playhead_frame,
                                               first, last, inserted);
        if (frames == 0) {
            target->playhead_frame = 0;
            target->has_playhead = 0;
        } else if (target->playhead_frame >= frames) {
            target->playhead_frame = frames - 1u;
        }
    }
    if (target->has_loop) {
        if (target->loop_last <= first) {
            /* The loop is before the edit. */
        } else if (target->loop_first >= last) {
            target->loop_first = replace_point(target->loop_first, first, last, inserted);
            target->loop_last = replace_point(target->loop_last, first, last, inserted);
        } else {
            target->has_loop = 0;
            target->loop_first = 0;
            target->loop_last = 0;
        }
    }
    target->has_selection = select_inserted && inserted > 0;
    target->selection_first = target->has_selection ? first : 0;
    target->selection_last = target->has_selection ? first + inserted : 0;
    if (target->has_selection &&
        (target->selection_first < target->view_first ||
         target->selection_last > target->view_last)) {
        target->view_first = 0;
        target->view_last = frames;
    }
}

static void free_audio_patches(TsAudioPatch *patches, int count)
{
    if (patches != NULL)
        for (int index = 0; index < count; ++index)
            ts_sample_free(&patches[index].sample);
    free(patches);
}

static void checkpoint_snapshot(TsEditSnapshot *state, size_t baseline_frames,
                                uint32_t patch_index)
{
    TsPostEdit checkpoint;
    state->crop_first = 0;
    state->crop_last = baseline_frames;
    ts_process_recipe_reset(&state->process);
    state->process_first = 0u;
    state->process_last = 0u;
    state->has_process_range = 0;
    memset(state->sample_edits, 0, sizeof(state->sample_edits));
    state->sample_edit_count = 0;
    memset(state->post_edits, 0, sizeof(state->post_edits));
    memset(&checkpoint, 0, sizeof(checkpoint));
    checkpoint.kind = TS_POST_MATERIAL_REPLACE;
    checkpoint.first = 0;
    checkpoint.last = baseline_frames;
    checkpoint.patch_index = patch_index;
    state->post_edits[0] = checkpoint;
    state->post_edit_count = 1;
}

static int compact_edit_graph(TsInstrument *instrument,
                              char *error, size_t error_size)
{
    TsBankSlot *slot;
    TsAudioPatch *checkpoints = NULL;
    TsSample baseline;
    int checkpoint_count;
    int built = 0;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[instrument->selected_slot].occupied) {
        set_error(error, error_size, "Cannot compact history without an active tile");
        return 0;
    }
    checkpoint_count = instrument->undo_count + instrument->redo_count;
    if (checkpoint_count > TS_AUDIO_PATCH_DEPTH) {
        set_error(error, error_size, "Retained history exceeds checkpoint capacity");
        return 0;
    }
    ts_sample_init(&baseline);
    if (!ts_sample_clone(&baseline, &instrument->current,
                         error, error_size)) return 0;
    if (checkpoint_count > 0) {
        checkpoints = calloc((size_t)checkpoint_count, sizeof(*checkpoints));
        if (checkpoints == NULL) {
            ts_sample_free(&baseline);
            set_error(error, error_size, "Out of memory checkpointing tile history");
            return 0;
        }
    }
    for (int index = 0; index < instrument->undo_count; ++index) {
        if (!render_snapshot(&checkpoints[built].sample, instrument,
                             &instrument->undo[index], error, error_size))
            goto failed;
        ++built;
    }
    for (int index = 0; index < instrument->redo_count; ++index) {
        if (!render_snapshot(&checkpoints[built].sample, instrument,
                             &instrument->redo[index], error, error_size))
            goto failed;
        ++built;
    }

    slot = &instrument->bank[instrument->selected_slot];
    free_audio_patches(slot->patches, slot->patch_count);
    slot->patches = checkpoints;
    slot->patch_count = checkpoint_count;
    slot->patch_capacity = checkpoint_count;
    checkpoints = NULL;
    ts_sample_free(&instrument->parent);
    instrument->parent = baseline;
    ts_sample_init(&baseline);
    for (int index = 0; index < instrument->undo_count; ++index)
        checkpoint_snapshot(&instrument->undo[index], instrument->parent.frames,
                            (uint32_t)index);
    for (int index = 0; index < instrument->redo_count; ++index)
        checkpoint_snapshot(&instrument->redo[index], instrument->parent.frames,
                            (uint32_t)(instrument->undo_count + index));
    instrument->crop_first = 0;
    instrument->crop_last = instrument->current.frames;
    ts_process_recipe_reset(&instrument->process);
    memset(instrument->sample_edits, 0, sizeof(instrument->sample_edits));
    instrument->sample_edit_count = 0;
    memset(instrument->post_edits, 0, sizeof(instrument->post_edits));
    instrument->post_edit_count = 0;
    instrument->source_kind = TS_SOURCE_COMMITTED;
    if (!bank_sync_selected(instrument, error, error_size)) return 0;
    set_error(error, error_size, "");
    return 1;

failed:
    free_audio_patches(checkpoints, built);
    ts_sample_free(&baseline);
    return 0;
}

static int ensure_edit_graph_capacity(TsInstrument *instrument, int needs_patch,
                                      char *error, size_t error_size)
{
    TsBankSlot *slot;
    int full;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "No active tile for edit history");
        return 0;
    }
    slot = &instrument->bank[instrument->selected_slot];
    full = instrument->post_edit_count >= TS_POST_EDIT_DEPTH ||
           instrument->sample_edit_count >= TS_SAMPLE_EDIT_DEPTH ||
           (needs_patch && slot->patch_count >= TS_AUDIO_PATCH_DEPTH);
    if (!full) return 1;
    if (!compact_edit_graph(instrument, error, error_size)) return 0;
    slot = &instrument->bank[instrument->selected_slot];
    if (instrument->post_edit_count >= TS_POST_EDIT_DEPTH ||
        instrument->sample_edit_count >= TS_SAMPLE_EDIT_DEPTH ||
        (needs_patch && slot->patch_count >= TS_AUDIO_PATCH_DEPTH)) {
        set_error(error, error_size, "Tile history could not free edit capacity");
        return 0;
    }
    return 1;
}

static int ensure_edit_graph_capacity_for(TsInstrument *instrument,
                                          int post_edit_slots,
                                          int needs_patch,
                                          char *error, size_t error_size)
{
    TsBankSlot *slot;
    int full;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        post_edit_slots < 1 || post_edit_slots > TS_POST_EDIT_DEPTH) {
        set_error(error, error_size, "Invalid edit capacity request");
        return 0;
    }
    slot = &instrument->bank[instrument->selected_slot];
    full = instrument->post_edit_count > TS_POST_EDIT_DEPTH - post_edit_slots ||
           instrument->sample_edit_count >= TS_SAMPLE_EDIT_DEPTH ||
           (needs_patch && slot->patch_count >= TS_AUDIO_PATCH_DEPTH);
    if (full && !compact_edit_graph(instrument, error, error_size)) return 0;
    slot = &instrument->bank[instrument->selected_slot];
    if (instrument->post_edit_count > TS_POST_EDIT_DEPTH - post_edit_slots ||
        instrument->sample_edit_count >= TS_SAMPLE_EDIT_DEPTH ||
        (needs_patch && slot->patch_count >= TS_AUDIO_PATCH_DEPTH)) {
        set_error(error, error_size, "Tile history could not free edit capacity");
        return 0;
    }
    return 1;
}

static int append_audio_patch(TsInstrument *instrument, const TsSample *sample,
                              const TsGeneratorRecipe *generator,
                              uint32_t *patch_index,
                              char *error, size_t error_size)
{
    TsBankSlot *slot;
    TsAudioPatch *patch;
    if (instrument == NULL || sample == NULL || sample->data == NULL ||
        instrument->selected_slot < 0 || instrument->selected_slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Audio checkpoint needs a selected tile");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    slot = &instrument->bank[instrument->selected_slot];
    if (slot->patch_count >= TS_AUDIO_PATCH_DEPTH) {
        set_error(error, error_size, "This tile has reached its audio stamp limit");
        return 0;
    }
    if (slot->patch_count >= slot->patch_capacity) {
        int capacity = slot->patch_capacity > 0 ? slot->patch_capacity * 2 : 4;
        TsAudioPatch *grown;
        if (capacity > TS_AUDIO_PATCH_DEPTH) capacity = TS_AUDIO_PATCH_DEPTH;
        grown = realloc(slot->patches, (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            set_error(error, error_size, "Out of memory storing audio stamp");
            return 0;
        }
        memset(grown + slot->patch_capacity, 0,
               (size_t)(capacity - slot->patch_capacity) * sizeof(*grown));
        slot->patches = grown;
        slot->patch_capacity = capacity;
    }
    patch = &slot->patches[slot->patch_count];
    ts_sample_init(&patch->sample);
    if (!ts_sample_clone(&patch->sample, sample, error, error_size)) return 0;
    patch->has_generator = generator != NULL;
    if (generator != NULL) patch->generator = *generator;
    else memset(&patch->generator, 0, sizeof(patch->generator));
    *patch_index = (uint32_t)slot->patch_count++;
    return 1;
}

static void discard_last_audio_patch(TsInstrument *instrument, uint32_t patch_index)
{
    TsBankSlot *slot;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT) return;
    slot = &instrument->bank[instrument->selected_slot];
    if (slot->patch_count > 0 && patch_index == (uint32_t)(slot->patch_count - 1)) {
        ts_sample_free(&slot->patches[patch_index].sample);
        memset(&slot->patches[patch_index], 0, sizeof(slot->patches[patch_index]));
        --slot->patch_count;
    }
}

static int commit_post_snapshot(TsInstrument *instrument, TsEditSnapshot *target,
                                TsSample *current,
                                char *error, size_t error_size)
{
    begin_edit(instrument);
    ts_sample_free(&instrument->current);
    instrument->current = *current;
    ts_sample_init(current);
    instrument->crop_first = target->crop_first;
    instrument->crop_last = target->crop_last;
    instrument->selection_first = target->selection_first;
    instrument->selection_last = target->selection_last;
    instrument->playhead_frame = target->playhead_frame;
    instrument->view_first = target->view_first;
    instrument->view_last = target->view_last;
    instrument->loop_first = target->loop_first;
    instrument->loop_last = target->loop_last;
    instrument->loop_crossfade_ms = target->loop_crossfade_ms;
    instrument->loop_mode = target->loop_mode;
    instrument->has_selection = target->has_selection;
    instrument->has_playhead = target->has_playhead;
    instrument->has_loop = target->has_loop;
    instrument->grid_divisions = target->grid_divisions;
    instrument->grid_snap = target->grid_snap;
    instrument->tuning = target->tuning;
    instrument->audible_tuning = target->audible_tuning;
    instrument->process = target->process;
    instrument->process_first = target->process_first;
    instrument->process_last = target->process_last;
    instrument->has_process_range = target->has_process_range;
    memcpy(instrument->sample_edits, target->sample_edits,
           sizeof(instrument->sample_edits));
    instrument->sample_edit_count = target->sample_edit_count;
    memcpy(instrument->post_edits, target->post_edits, sizeof(instrument->post_edits));
    instrument->post_edit_count = target->post_edit_count;
    return bank_sync_selected(instrument, error, error_size);
}

static int commit_material_checkpoint(TsInstrument *instrument,
                                      const TsSample *material,
                                      TsEditSnapshot *target,
                                      char *error, size_t error_size)
{
    TsPostEdit operation;
    TsSample current;
    uint32_t patch_index;
    uint32_t process_seed;
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size) ||
        !append_audio_patch(instrument, material, NULL, &patch_index,
                            error, error_size))
        return 0;
    process_seed = target->process.seed;
    target->crop_first = 0u;
    target->crop_last = instrument->parent.frames;
    memset(target->sample_edits, 0, sizeof(target->sample_edits));
    target->sample_edit_count = 0;
    ts_process_recipe_reset(&target->process);
    target->process.seed = process_seed;
    target->process_first = 0u;
    target->process_last = 0u;
    target->has_process_range = 0;
    memset(target->post_edits, 0, sizeof(target->post_edits));
    target->post_edit_count = 0;
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_MATERIAL_REPLACE;
    operation.first = 0u;
    operation.last = instrument->parent.frames;
    operation.patch_index = patch_index;
    target->post_edits[target->post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, target, error, error_size)) {
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    if (!commit_post_snapshot(instrument, target, &current, error, error_size)) {
        ts_sample_free(&current);
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

static int resample_selection_patch(TsSample *patch, const TsSample *source,
                                    size_t first, size_t last, size_t frames,
                                    size_t source_pivot, size_t target_pivot,
                                    char *error, size_t error_size)
{
    float *data;
    size_t source_frames;
    if (patch == NULL || source == NULL || source->data == NULL ||
        first >= last || last > source->frames || frames == 0) {
        set_error(error, error_size, "Invalid selection for tape stretch");
        return 0;
    }
    if (frames > SIZE_MAX / sizeof(*data)) {
        set_error(error, error_size, "Stretched selection is too large");
        return 0;
    }
    data = malloc(frames * sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory stretching selection");
        return 0;
    }
    source_frames = last - first;
    if (source_pivot >= source_frames) source_pivot = source_frames - 1u;
    if (target_pivot >= frames) target_pivot = frames - 1u;
    for (size_t i = 0; i < frames; ++i) {
        double position;
        if (i <= target_pivot) {
            position = target_pivot > 0u ?
                (double)i * (double)source_pivot / (double)target_pivot : 0.0;
        } else {
            size_t target_right = frames - 1u - target_pivot;
            size_t source_right = source_frames - 1u - source_pivot;
            position = (double)source_pivot +
                (target_right > 0u ?
                 (double)(i - target_pivot) * (double)source_right /
                 (double)target_right : 0.0);
        }
        size_t at = (size_t)position;
        double fraction = position - (double)at;
        float a = source->data[first + at];
        float b = at + 1u < source_frames ? source->data[first + at + 1u] : a;
        data[i] = a + (b - a) * (float)fraction;
    }
    ts_sample_free(patch);
    patch->data = data;
    patch->frames = frames;
    patch->sample_rate = source->sample_rate;
    snprintf(patch->name, sizeof(patch->name), "STRETCH %.112s", source->name);
    return 1;
}

static int stretch_target_range(const TsSample *source, size_t first, size_t last,
                                size_t pivot, float duration_ratio,
                                size_t *target_first_out,
                                size_t *target_last_out,
                                float *actual_ratio_out,
                                char *error, size_t error_size)
{
    size_t old_frames, wanted_frames, target_first, target_last, left_frames;
    double pivot_fraction;
    int expanding;
    if (source == NULL || source->data == NULL || first >= last ||
        last > source->frames || !isfinite(duration_ratio) ||
        duration_ratio <= 0.0f) {
        set_error(error, error_size, "Invalid selection for tape stretch");
        return 0;
    }
    old_frames = last - first;
    expanding = duration_ratio > 1.0f;
    if (pivot < first || pivot >= last) pivot = first + old_frames / 2u;
    wanted_frames = (size_t)llround((double)old_frames * (double)duration_ratio);
    if (wanted_frames < 2u) wanted_frames = 2u;
    if (wanted_frames > source->frames) wanted_frames = source->frames;
    pivot_fraction = (double)(pivot - first) / (double)old_frames;
    left_frames = (size_t)llround((double)wanted_frames * pivot_fraction);
    if (left_frames > pivot) target_first = 0;
    else target_first = pivot - left_frames;
    if (wanted_frames > source->frames - target_first)
        target_first = source->frames - wanted_frames;
    target_last = target_first + wanted_frames;
    target_first = target_first == 0 ? 0 :
                   ts_sample_nearest_zero_crossing(source, target_first);
    target_last = target_last >= source->frames ? source->frames :
                  ts_sample_nearest_zero_crossing(source, target_last);
    if (expanding) {
        if (target_first >= first && first > 0)
            target_first = ts_sample_zero_crossing_in_direction(source, first, -1, 1);
        if (target_last <= last && last < source->frames)
            target_last = ts_sample_zero_crossing_in_direction(source, last, 1, 1);
    } else {
        if (target_first <= first)
            target_first = ts_sample_zero_crossing_in_direction(source, first, 1, 1);
        if (target_last >= last)
            target_last = ts_sample_zero_crossing_in_direction(source, last, -1, 1);
    }
    if (target_first >= pivot && pivot > 0)
        target_first = ts_sample_zero_crossing_in_direction(source, pivot, -1, 1);
    if (target_last <= pivot)
        target_last = ts_sample_zero_crossing_in_direction(source, pivot, 1, 1);
    if (target_last <= target_first + 1u) {
        set_error(error, error_size, "Tape length reached its zero-crossing limit");
        return 0;
    }
    wanted_frames = target_last - target_first;
    if (wanted_frames == old_frames) {
        set_error(error, error_size, "Tape length unchanged at the nearest zero crossings");
        return 0;
    }
    *target_first_out = target_first;
    *target_last_out = target_last;
    *actual_ratio_out = (float)((double)wanted_frames / (double)old_frames);
    return 1;
}

int ts_instrument_stretch_selection(TsInstrument *instrument, size_t pivot,
                                    float duration_ratio, float *pitch_semitones,
                                    char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsPostEdit operation;
    TsSample patch;
    TsSample current;
    size_t first, last, old_frames, wanted_frames;
    size_t target_first, target_last;
    uint32_t patch_index;
    float actual_ratio;
    if (instrument == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select audio before changing its tape length");
        return 0;
    }
    if (!isfinite(duration_ratio) || duration_ratio <= 0.0f) {
        set_error(error, error_size, "Invalid tape length ratio");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    first = instrument->selection_first;
    last = instrument->selection_last;
    old_frames = last - first;
    if (pivot < first || pivot >= last) pivot = first + old_frames / 2u;
    if (!stretch_target_range(&instrument->current, first, last, pivot,
                              duration_ratio, &target_first, &target_last,
                              &actual_ratio, error, error_size)) return 0;
    wanted_frames = target_last - target_first;
    ts_sample_init(&patch);
    if (!resample_selection_patch(&patch, &instrument->current, first, last,
                                  wanted_frames, pivot - first,
                                  pivot - target_first,
                                  error, error_size)) return 0;
    if (!append_audio_patch(instrument, &patch, NULL, &patch_index,
                            error, error_size)) {
        ts_sample_free(&patch);
        return 0;
    }
    ts_sample_free(&patch);
    memset(&operation, 0, sizeof(operation));
    operation.kind = wanted_frames > old_frames ?
                     TS_POST_PATCH_STRETCH_EXPAND :
                     TS_POST_PATCH_STRETCH_CONTRACT;
    operation.first = first;
    operation.last = last;
    operation.destination = (int64_t)target_first;
    operation.amount = actual_ratio;
    operation.patch_index = patch_index;
    operation.crossfade_frames = instrument->current.sample_rate / 1000u;
    if (operation.crossfade_frames < 8u) operation.crossfade_frames = 8u;
    if (operation.crossfade_frames > 64u) operation.crossfade_frames = 64u;
    target = snapshot(instrument);
    target.post_edits[target.post_edit_count++] = operation;
    target.selection_first = target_first;
    target.selection_last = target_last;
    target.has_selection = 1;
    if (target.has_loop && target.loop_last > (first < target_first ? first : target_first) &&
        target.loop_first < (last > target_last ? last : target_last)) {
        target.has_loop = 0;
        target.loop_first = target.loop_last = 0;
    }
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) {
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    if (!commit_post_snapshot(instrument, &target, &current, error, error_size))
        return 0;
    if (pitch_semitones != NULL)
        *pitch_semitones = -12.0f * log2f(actual_ratio);
    set_error(error, error_size, "");
    return 1;
}

void ts_stretch_gesture_init(TsStretchGesture *gesture)
{
    if (gesture == NULL) return;
    memset(gesture, 0, sizeof(*gesture));
    ts_sample_init(&gesture->original);
}

static int stretch_gesture_owns(const TsInstrument *instrument,
                                const TsStretchGesture *gesture)
{
    return instrument != NULL && gesture != NULL && gesture->active &&
           instrument->selected_slot == gesture->owner_slot &&
           instrument->generation == gesture->owner_generation &&
           instrument->parent.data == gesture->owner_parent_data &&
           instrument->current.frames == gesture->original.frames;
}

static void stretch_gesture_clear(TsStretchGesture *gesture)
{
    ts_sample_free(&gesture->original);
    memset(gesture, 0, sizeof(*gesture));
    ts_sample_init(&gesture->original);
}

static void restore_stretch_snapshot(TsInstrument *instrument,
                                     const TsEditSnapshot *state)
{
    instrument->crop_first = state->crop_first;
    instrument->crop_last = state->crop_last;
    instrument->selection_first = state->selection_first;
    instrument->selection_last = state->selection_last;
    instrument->playhead_frame = state->playhead_frame;
    instrument->view_first = state->view_first;
    instrument->view_last = state->view_last;
    instrument->loop_first = state->loop_first;
    instrument->loop_last = state->loop_last;
    instrument->loop_crossfade_ms = state->loop_crossfade_ms;
    instrument->loop_mode = state->loop_mode;
    instrument->has_selection = state->has_selection;
    instrument->has_playhead = state->has_playhead;
    instrument->has_loop = state->has_loop;
    instrument->grid_divisions = state->grid_divisions;
    instrument->grid_snap = state->grid_snap;
    instrument->tuning = state->tuning;
    instrument->audible_tuning = state->audible_tuning;
    instrument->process = state->process;
    instrument->process_first = state->process_first;
    instrument->process_last = state->process_last;
    instrument->has_process_range = state->has_process_range;
    memcpy(instrument->sample_edits, state->sample_edits,
           sizeof(instrument->sample_edits));
    instrument->sample_edit_count = state->sample_edit_count;
    memcpy(instrument->post_edits, state->post_edits,
           sizeof(instrument->post_edits));
    instrument->post_edit_count = state->post_edit_count;
}

static size_t stretch_crossfade_frames(const TsSample *sample)
{
    size_t frames = sample != NULL ? sample->sample_rate / 1000u : 0u;
    if (frames < 8u) frames = 8u;
    if (frames > 64u) frames = 64u;
    return frames;
}

int ts_instrument_stretch_gesture_begin(TsInstrument *instrument,
                                        TsStretchGesture *gesture, size_t pivot,
                                        char *error, size_t error_size)
{
    if (instrument == NULL || gesture == NULL || gesture->active ||
        instrument->current.data == NULL || !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select audio before changing its tape length");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    if (instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[instrument->selected_slot].occupied) {
        set_error(error, error_size, "This tile has reached its audio stamp limit");
        return 0;
    }
    gesture->start = snapshot(instrument);
    if (!ts_sample_clone(&gesture->original, &instrument->current,
                         error, error_size)) return 0;
    gesture->owner_parent_data = instrument->parent.data;
    gesture->owner_generation = instrument->generation;
    gesture->owner_slot = instrument->selected_slot;
    gesture->pivot = pivot >= instrument->selection_first &&
                     pivot < instrument->selection_last ? pivot :
                     instrument->selection_first +
                     (instrument->selection_last - instrument->selection_first) / 2u;
    gesture->target_first = instrument->selection_first;
    gesture->target_last = instrument->selection_last;
    gesture->requested_ratio = 1.0f;
    gesture->actual_ratio = 1.0f;
    gesture->pitch_semitones = 0.0f;
    gesture->active = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_stretch_gesture_preview(TsInstrument *instrument,
                                          TsStretchGesture *gesture,
                                          float duration_ratio,
                                          float *pitch_semitones,
                                          char *error, size_t error_size)
{
    TsSample patch;
    TsSample preview;
    size_t target_first, target_last, wanted_frames;
    float actual_ratio;
    if (!stretch_gesture_owns(instrument, gesture)) {
        set_error(error, error_size, "Tape-length gesture no longer owns Current");
        return 0;
    }
    if (!isfinite(duration_ratio) || duration_ratio <= 0.0f) {
        set_error(error, error_size, "Invalid tape length ratio");
        return 0;
    }
    ts_sample_init(&patch);
    ts_sample_init(&preview);
    if (fabsf(duration_ratio - 1.0f) < 0.000001f) {
        if (!ts_sample_clone(&preview, &gesture->original,
                             error, error_size)) return 0;
        replace_current_preserving_view(instrument, &preview);
        restore_stretch_snapshot(instrument, &gesture->start);
        gesture->target_first = gesture->start.selection_first;
        gesture->target_last = gesture->start.selection_last;
        gesture->requested_ratio = 1.0f;
        gesture->actual_ratio = 1.0f;
        gesture->pitch_semitones = 0.0f;
        if (pitch_semitones != NULL) *pitch_semitones = 0.0f;
        set_error(error, error_size, "");
        return 1;
    }
    if (!stretch_target_range(&gesture->original,
                              gesture->start.selection_first,
                              gesture->start.selection_last,
                              gesture->pivot, duration_ratio,
                              &target_first, &target_last, &actual_ratio,
                              error, error_size)) return 0;
    wanted_frames = target_last - target_first;
    if (!resample_selection_patch(
            &patch, &gesture->original,
            gesture->start.selection_first, gesture->start.selection_last,
            wanted_frames, gesture->pivot - gesture->start.selection_first,
            gesture->pivot - target_first, error, error_size) ||
        !ts_sample_clone(&preview, &gesture->original, error, error_size)) {
        ts_sample_free(&patch);
        ts_sample_free(&preview);
        return 0;
    }
    if (!stretch_patch_range(
            &preview, &patch, gesture->start.selection_first,
            gesture->start.selection_last, target_first,
            wanted_frames < gesture->start.selection_last -
                            gesture->start.selection_first,
            stretch_crossfade_frames(&gesture->original), error, error_size)) {
        ts_sample_free(&patch);
        ts_sample_free(&preview);
        return 0;
    }
    ts_sample_free(&patch);
    replace_current_preserving_view(instrument, &preview);
    restore_stretch_snapshot(instrument, &gesture->start);
    instrument->selection_first = target_first;
    instrument->selection_last = target_last;
    instrument->has_selection = 1;
    if (instrument->has_loop &&
        instrument->loop_last > (gesture->start.selection_first < target_first ?
                                 gesture->start.selection_first : target_first) &&
        instrument->loop_first < (gesture->start.selection_last > target_last ?
                                  gesture->start.selection_last : target_last)) {
        instrument->has_loop = 0;
        instrument->loop_first = instrument->loop_last = 0;
    }
    gesture->target_first = target_first;
    gesture->target_last = target_last;
    gesture->requested_ratio = duration_ratio;
    gesture->actual_ratio = actual_ratio;
    gesture->pitch_semitones = -12.0f * log2f(actual_ratio);
    if (pitch_semitones != NULL) *pitch_semitones = gesture->pitch_semitones;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_stretch_gesture_cancel(TsInstrument *instrument,
                                         TsStretchGesture *gesture,
                                         char *error, size_t error_size)
{
    TsSample restored;
    if (!stretch_gesture_owns(instrument, gesture)) {
        stretch_gesture_clear(gesture);
        set_error(error, error_size, "Tape-length gesture no longer owns Current");
        return 0;
    }
    ts_sample_init(&restored);
    if (!ts_sample_clone(&restored, &gesture->original,
                         error, error_size)) return 0;
    replace_current_preserving_view(instrument, &restored);
    restore_stretch_snapshot(instrument, &gesture->start);
    stretch_gesture_clear(gesture);
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_stretch_gesture_commit(TsInstrument *instrument,
                                         TsStretchGesture *gesture,
                                         char *error, size_t error_size)
{
    TsSample patch;
    TsPostEdit operation;
    uint32_t patch_index;
    size_t wanted_frames;
    int ok;
    if (!stretch_gesture_owns(instrument, gesture)) {
        stretch_gesture_clear(gesture);
        set_error(error, error_size, "Tape-length gesture no longer owns Current");
        return 0;
    }
    if (fabsf(gesture->requested_ratio - 1.0f) < 0.000001f ||
        gesture->target_last - gesture->target_first ==
        gesture->start.selection_last - gesture->start.selection_first)
        return ts_instrument_stretch_gesture_cancel(instrument, gesture,
                                                     error, error_size);
    wanted_frames = gesture->target_last - gesture->target_first;
    ts_sample_init(&patch);
    if (!resample_selection_patch(
            &patch, &gesture->original,
            gesture->start.selection_first, gesture->start.selection_last,
            wanted_frames, gesture->pivot - gesture->start.selection_first,
            gesture->pivot - gesture->target_first, error, error_size)) return 0;
    if (!append_audio_patch(instrument, &patch, NULL, &patch_index,
                            error, error_size)) {
        ts_sample_free(&patch);
        return 0;
    }
    ts_sample_free(&patch);
    stack_push(instrument->undo, &instrument->undo_count, gesture->start);
    instrument->redo_count = 0;
    memcpy(instrument->post_edits, gesture->start.post_edits,
           sizeof(instrument->post_edits));
    instrument->post_edit_count = gesture->start.post_edit_count;
    memset(&operation, 0, sizeof(operation));
    operation.kind = wanted_frames > gesture->start.selection_last -
                                      gesture->start.selection_first ?
                     TS_POST_PATCH_STRETCH_EXPAND :
                     TS_POST_PATCH_STRETCH_CONTRACT;
    operation.first = gesture->start.selection_first;
    operation.last = gesture->start.selection_last;
    operation.destination = (int64_t)gesture->target_first;
    operation.amount = gesture->actual_ratio;
    operation.patch_index = patch_index;
    operation.crossfade_frames = stretch_crossfade_frames(&gesture->original);
    instrument->post_edits[instrument->post_edit_count++] = operation;
    ok = bank_sync_selected(instrument, error, error_size);
    stretch_gesture_clear(gesture);
    if (ok) set_error(error, error_size, "");
    return ok;
}

void ts_canvas_gesture_init(TsCanvasGesture *gesture)
{
    if (gesture == NULL) return;
    memset(gesture, 0, sizeof(*gesture));
    ts_sample_init(&gesture->original);
}

static int canvas_gesture_owns(const TsInstrument *instrument,
                               const TsCanvasGesture *gesture)
{
    return instrument != NULL && gesture != NULL && gesture->active &&
           instrument->selected_slot == gesture->owner_slot &&
           instrument->generation == gesture->owner_generation &&
           instrument->parent.data == gesture->owner_parent_data;
}

static void canvas_gesture_clear(TsCanvasGesture *gesture)
{
    ts_sample_free(&gesture->original);
    memset(gesture, 0, sizeof(*gesture));
    ts_sample_init(&gesture->original);
}

static size_t canvas_contraction_boundary(const TsCanvasGesture *gesture,
                                          size_t target, size_t first, size_t last)
{
    size_t macro = gesture->start.grid_snap != TS_GRID_SNAP_OFF ?
                   grid_target_for_frames(gesture->original.frames,
                                          gesture->start.grid_divisions,
                                          target) : target;
    return ts_sample_nearest_zero_crossing_in_range(
        &gesture->original, macro, first, last);
}

static void canvas_adjust_range(size_t *first, size_t *last, int *active,
                                int edge, int64_t delta, size_t new_frames,
                                size_t minimum_span)
{
    size_t amount;
    size_t adjusted_first = *first;
    size_t adjusted_last = *last;
    if (!*active) return;
    if (delta > 0 && edge == 1) {
        amount = (size_t)delta;
        adjusted_first += amount;
        adjusted_last += amount;
    } else if (delta < 0 && edge == 1) {
        amount = (size_t)((uint64_t)(-(delta + 1)) + 1u);
        if (adjusted_last <= amount) {
            *first = *last = 0;
            *active = 0;
            return;
        }
        adjusted_first = adjusted_first > amount ? adjusted_first - amount : 0u;
        adjusted_last -= amount;
    } else if (delta < 0 && edge == 2) {
        if (adjusted_first >= new_frames) {
            *first = *last = 0;
            *active = 0;
            return;
        }
        if (adjusted_last > new_frames) adjusted_last = new_frames;
    }
    if (adjusted_last > new_frames) adjusted_last = new_frames;
    if (adjusted_first > adjusted_last) adjusted_first = adjusted_last;
    if (adjusted_last - adjusted_first < minimum_span) {
        *first = *last = 0;
        *active = 0;
        return;
    }
    *first = adjusted_first;
    *last = adjusted_last;
}

static void canvas_adjust_view(TsInstrument *instrument, int edge, int64_t delta,
                               size_t old_frames, size_t new_frames)
{
    size_t first = instrument->view_first;
    size_t last = instrument->view_last;
    size_t span = last > first ? last - first : old_frames;
    int showing_all = first == 0u && last >= old_frames;
    if (showing_all) {
        instrument->view_first = 0u;
        instrument->view_last = new_frames;
        return;
    }
    if (span > new_frames) span = new_frames;
    if (delta > 0 && edge == 1) {
        size_t amount = (size_t)delta;
        if (first == 0u) {
            first = 0u;
            last = span;
        } else {
            first += amount;
            last += amount;
        }
    } else if (delta > 0 && edge == 2 && last >= old_frames) {
        last = new_frames;
        first = last > span ? last - span : 0u;
    } else if (delta < 0 && edge == 1) {
        size_t amount = (size_t)((uint64_t)(-(delta + 1)) + 1u);
        if (first >= amount) {
            first -= amount;
            last = first + span;
        } else {
            first = 0;
            last = span;
        }
    } else if (delta < 0 && edge == 2 && last > new_frames) {
        last = new_frames;
        first = last > span ? last - span : 0u;
    }
    if (last > new_frames) last = new_frames;
    if (first >= last) {
        last = new_frames;
        first = last > span ? last - span : 0u;
    }
    instrument->view_first = first;
    instrument->view_last = last;
}

static void canvas_apply_state(TsInstrument *instrument, int edge, int64_t delta,
                               size_t old_frames, size_t new_frames)
{
    canvas_adjust_range(&instrument->selection_first,
                        &instrument->selection_last,
                        &instrument->has_selection,
                        edge, delta, new_frames, 1u);
    canvas_adjust_range(&instrument->loop_first, &instrument->loop_last,
                        &instrument->has_loop,
                        edge, delta, new_frames, 2u);
    if (instrument->has_playhead) {
        if (delta > 0 && edge == 1)
            instrument->playhead_frame += (size_t)delta;
        else if (delta < 0 && edge == 1) {
            size_t amount = (size_t)((uint64_t)(-(delta + 1)) + 1u);
            instrument->playhead_frame = instrument->playhead_frame > amount ?
                                         instrument->playhead_frame - amount : 0u;
        }
        if (instrument->playhead_frame >= new_frames)
            instrument->playhead_frame = new_frames - 1u;
    }
    canvas_adjust_view(instrument, edge, delta, old_frames, new_frames);
}

int ts_instrument_canvas_gesture_begin(TsInstrument *instrument,
                                       TsCanvasGesture *gesture, int edge,
                                       char *error, size_t error_size)
{
    size_t span;
    if (instrument == NULL || gesture == NULL || gesture->active ||
        instrument->current.data == NULL ||
        instrument->current.frames < TS_CANVAS_MIN_FRAMES ||
        (edge != 1 && edge != 2)) {
        set_error(error, error_size, "Select an occupied tile before resizing its canvas");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 0, error, error_size)) return 0;
    gesture->start = snapshot(instrument);
    if (!ts_sample_clone(&gesture->original, &instrument->current,
                         error, error_size)) return 0;
    gesture->owner_parent_data = instrument->parent.data;
    gesture->owner_generation = instrument->generation;
    gesture->owner_slot = instrument->selected_slot;
    gesture->edge = edge;
    gesture->delta_frames = 0;
    span = gesture->start.view_last > gesture->start.view_first ?
           gesture->start.view_last - gesture->start.view_first :
           instrument->current.frames;
    if (span > instrument->current.frames) span = instrument->current.frames;
    if (gesture->start.view_first == 0u &&
        gesture->start.view_last >= instrument->current.frames) {
        gesture->focus_view_first = 0u;
        gesture->focus_view_last = instrument->current.frames;
    } else if (edge == 1) {
        gesture->focus_view_first = 0u;
        gesture->focus_view_last = span;
    } else {
        gesture->focus_view_last = instrument->current.frames;
        gesture->focus_view_first = instrument->current.frames - span;
    }
    instrument->view_first = gesture->focus_view_first;
    instrument->view_last = gesture->focus_view_last;
    gesture->active = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_canvas_gesture_preview(TsInstrument *instrument,
                                         TsCanvasGesture *gesture,
                                         int64_t delta_frames,
                                         char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsPostEdit operation;
    TsSample preview;
    size_t old_frames;
    size_t new_frames;
    if (!canvas_gesture_owns(instrument, gesture)) {
        set_error(error, error_size, "Canvas gesture no longer owns Current");
        return 0;
    }
    old_frames = gesture->original.frames;
    if (delta_frames > 0) {
        if ((uint64_t)delta_frames > SIZE_MAX - old_frames ||
            old_frames + (size_t)delta_frames > TS_CANVAS_MAX_FRAMES ||
            old_frames + (size_t)delta_frames > SIZE_MAX / sizeof(float)) {
            set_error(error, error_size, "Audio canvas is too large");
            return 0;
        }
    } else if (delta_frames < 0) {
        uint64_t requested = (uint64_t)(-(delta_frames + 1)) + 1u;
        size_t maximum = old_frames - TS_CANVAS_MIN_FRAMES;
        size_t boundary;
        if (requested > maximum) requested = maximum;
        if (gesture->edge == 1) {
            boundary = canvas_contraction_boundary(
                gesture, (size_t)requested, 0u, maximum);
            if (boundary > maximum) boundary = maximum;
            delta_frames = -(int64_t)boundary;
        } else {
            size_t proposed = old_frames - (size_t)requested;
            boundary = canvas_contraction_boundary(
                gesture, proposed, TS_CANVAS_MIN_FRAMES, old_frames - 1u);
            if (boundary < TS_CANVAS_MIN_FRAMES) boundary = TS_CANVAS_MIN_FRAMES;
            delta_frames = (int64_t)boundary - (int64_t)old_frames;
        }
    }
    if (delta_frames == 0) {
        ts_sample_init(&preview);
        if (!ts_sample_clone(&preview, &gesture->original,
                             error, error_size)) return 0;
        replace_current_preserving_view(instrument, &preview);
        restore_stretch_snapshot(instrument, &gesture->start);
        instrument->view_first = gesture->focus_view_first;
        instrument->view_last = gesture->focus_view_last;
        gesture->delta_frames = 0;
        set_error(error, error_size, "");
        return 1;
    }
    new_frames = delta_frames > 0 ? old_frames + (size_t)delta_frames :
                 old_frames - (size_t)((uint64_t)(-(delta_frames + 1)) + 1u);
    target = gesture->start;
    memset(&operation, 0, sizeof(operation));
    operation.kind = gesture->edge == 1 ?
                     TS_POST_CANVAS_LEFT_RESIZE :
                     TS_POST_CANVAS_RIGHT_RESIZE;
    operation.destination = delta_frames;
    target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&preview);
    if (!render_snapshot(&preview, instrument, &target, error, error_size)) return 0;
    replace_current_preserving_view(instrument, &preview);
    restore_stretch_snapshot(instrument, &gesture->start);
    instrument->view_first = gesture->focus_view_first;
    instrument->view_last = gesture->focus_view_last;
    memcpy(instrument->post_edits, target.post_edits,
           sizeof(instrument->post_edits));
    instrument->post_edit_count = target.post_edit_count;
    canvas_apply_state(instrument, gesture->edge, delta_frames,
                       old_frames, new_frames);
    gesture->delta_frames = delta_frames;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_canvas_gesture_cancel(TsInstrument *instrument,
                                        TsCanvasGesture *gesture,
                                        char *error, size_t error_size)
{
    TsSample restored;
    if (!canvas_gesture_owns(instrument, gesture)) {
        canvas_gesture_clear(gesture);
        set_error(error, error_size, "Canvas gesture no longer owns Current");
        return 0;
    }
    ts_sample_init(&restored);
    if (!ts_sample_clone(&restored, &gesture->original,
                         error, error_size)) return 0;
    replace_current_preserving_view(instrument, &restored);
    restore_stretch_snapshot(instrument, &gesture->start);
    canvas_gesture_clear(gesture);
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_canvas_gesture_commit(TsInstrument *instrument,
                                        TsCanvasGesture *gesture,
                                        char *error, size_t error_size)
{
    int ok;
    if (!canvas_gesture_owns(instrument, gesture)) {
        canvas_gesture_clear(gesture);
        set_error(error, error_size, "Canvas gesture no longer owns Current");
        return 0;
    }
    if (gesture->delta_frames == 0)
        return ts_instrument_canvas_gesture_cancel(instrument, gesture,
                                                   error, error_size);
    stack_push(instrument->undo, &instrument->undo_count, gesture->start);
    instrument->redo_count = 0;
    ok = bank_sync_selected(instrument, error, error_size);
    canvas_gesture_clear(gesture);
    if (ok) set_error(error, error_size, "");
    return ok;
}

int ts_instrument_resize_canvas(TsInstrument *instrument, int edge,
                                int64_t delta_frames,
                                char *error, size_t error_size)
{
    TsCanvasGesture gesture;
    int ok;
    ts_canvas_gesture_init(&gesture);
    if (!ts_instrument_canvas_gesture_begin(instrument, &gesture, edge,
                                            error, error_size)) return 0;
    ok = ts_instrument_canvas_gesture_preview(instrument, &gesture, delta_frames,
                                              error, error_size) &&
         gesture.delta_frames != 0 &&
         ts_instrument_canvas_gesture_commit(instrument, &gesture,
                                             error, error_size);
    if (!ok && gesture.active)
        (void)ts_instrument_canvas_gesture_cancel(instrument, &gesture, NULL, 0);
    return ok;
}

int ts_instrument_double_canvas(TsInstrument *instrument,
                                char *error, size_t error_size)
{
    size_t frames;
    uint32_t divisions;
    if (instrument == NULL || instrument->current.data == NULL) {
        set_error(error, error_size, "Select an occupied tile before doubling its canvas");
        return 0;
    }
    frames = instrument->current.frames;
    if (frames > (size_t)INT64_MAX || frames > SIZE_MAX - frames) {
        set_error(error, error_size, "Audio canvas is too large to double");
        return 0;
    }
    divisions = instrument->grid_divisions;
    if (!ts_instrument_resize_canvas(instrument, 2, (int64_t)frames,
                                     error, error_size)) return 0;
    if (valid_grid_divisions(divisions) && divisions <= TS_GRID_DIVISION_MAX / 2u)
        instrument->grid_divisions = divisions * 2u;
    return bank_sync_selected(instrument, error, error_size);
}

int ts_instrument_half_canvas(TsInstrument *instrument,
                              char *error, size_t error_size)
{
    size_t frames;
    size_t wanted;
    uint32_t divisions;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames <= TS_CANVAS_MIN_FRAMES) {
        set_error(error, error_size, "Audio canvas is already at its minimum length");
        return 0;
    }
    frames = instrument->current.frames;
    wanted = frames / 2u;
    if (wanted < TS_CANVAS_MIN_FRAMES) wanted = TS_CANVAS_MIN_FRAMES;
    divisions = instrument->grid_divisions;
    if (!ts_instrument_resize_canvas(
            instrument, 2, -(int64_t)(frames - wanted),
            error, error_size)) return 0;
    if (instrument->current.frames <= SIZE_MAX / 2u &&
        instrument->current.frames * 2u == frames &&
        valid_grid_divisions(divisions) && divisions > TS_GRID_DIVISION_MIN)
        instrument->grid_divisions = divisions / 2u;
    return bank_sync_selected(instrument, error, error_size);
}

int ts_instrument_copy_selection(const TsInstrument *instrument,
                                 TsSample *clipboard, size_t *origin_first,
                                 char *error, size_t error_size)
{
    TsSample copied;
    if (instrument == NULL || clipboard == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range before Copy");
        return 0;
    }
    ts_sample_init(&copied);
    if (!sample_clone_range(&copied, &instrument->current,
                            instrument->selection_first, instrument->selection_last,
                            error, error_size)) return 0;
    snprintf(copied.name, sizeof(copied.name), "CLIP %.116s", instrument->current.name);
    ts_sample_free(clipboard);
    *clipboard = copied;
    if (origin_first != NULL) *origin_first = instrument->selection_first;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_cut_selection(TsInstrument *instrument,
                                TsSample *clipboard, size_t *origin_first,
                                char *error, size_t error_size)
{
    TsSample copied;
    TsSample current;
    TsEditSnapshot target;
    TsPostEdit operation;
    size_t first;
    size_t last;
    if (instrument == NULL || clipboard == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range before Cut");
        return 0;
    }
    first = instrument->selection_first;
    last = instrument->selection_last;
    if (first == 0 && last == instrument->current.frames) {
        set_error(error, error_size, "Cut cannot remove the entire tile - Clear the tile instead");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 0, error, error_size)) return 0;
    ts_sample_init(&copied);
    ts_sample_init(&current);
    if (!sample_clone_range(&copied, &instrument->current, first, last,
                            error, error_size)) return 0;
    snprintf(copied.name, sizeof(copied.name), "CLIP %.116s", instrument->current.name);
    target = snapshot(instrument);
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_DELETE;
    operation.first = first;
    operation.last = last;
    target.post_edits[target.post_edit_count++] = operation;
    update_snapshot_after_replace(&target, first, last, 0,
                                  instrument->current.frames - (last - first), 0);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) {
        ts_sample_free(&copied);
        return 0;
    }
    if (!commit_post_snapshot(instrument, &target, &current, error, error_size)) {
        ts_sample_free(&copied);
        ts_sample_free(&current);
        return 0;
    }
    ts_sample_free(clipboard);
    *clipboard = copied;
    if (origin_first != NULL) *origin_first = first;
    set_error(error, error_size, "");
    return 1;
}

static int instrument_paste(TsInstrument *instrument, const TsSample *clipboard,
                            size_t origin_first, int fit_selection,
                            size_t join_crossfade_frames,
                            char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsPostEdit operation;
    TsSample current;
    uint32_t patch_index;
    size_t first;
    size_t last;
    size_t inserted;
    size_t output_frames;
    int has_target_selection;
    if (instrument == NULL || clipboard == NULL || clipboard->data == NULL ||
        clipboard->frames == 0 || clipboard->sample_rate == 0) {
        set_error(error, error_size, "Clipboard is empty");
        return 0;
    }
    if (instrument->current.data == NULL || instrument->current.frames == 0) {
        set_error(error, error_size, "Paste currently needs an occupied target tile");
        return 0;
    }
    if (fit_selection && (!instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first)) {
        set_error(error, error_size, "Fit Paste needs a target selection");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    has_target_selection = instrument->has_selection &&
                           instrument->selection_last > instrument->selection_first;
    if (has_target_selection) {
        first = instrument->selection_first;
        last = instrument->selection_last;
    } else {
        first = origin_first;
        last = first;
    }
    if (fit_selection) inserted = last - first;
    else {
        inserted = (size_t)llround((double)clipboard->frames *
                   (double)instrument->current.sample_rate /
                   (double)clipboard->sample_rate);
        if (inserted == 0) inserted = 1;
    }
    if (inserted > SIZE_MAX - first) {
        set_error(error, error_size, "Pasted tile would be too large");
        return 0;
    }
    if (!has_target_selection) {
        if (first <= instrument->current.frames) {
            size_t paste_end = first + inserted;
            last = paste_end < instrument->current.frames ? paste_end :
                   instrument->current.frames;
            output_frames = instrument->current.frames - (last - first) + inserted;
        } else output_frames = first + inserted;
    } else {
        if (instrument->current.frames - (last - first) > SIZE_MAX - inserted) {
            set_error(error, error_size, "Pasted tile would be too large");
            return 0;
        }
        output_frames = instrument->current.frames - (last - first) + inserted;
    }
    if (!append_audio_patch(instrument, clipboard, NULL, &patch_index,
                            error, error_size)) return 0;
    target = snapshot(instrument);
    memset(&operation, 0, sizeof(operation));
    operation.kind = fit_selection ? TS_POST_PATCH_FIT : TS_POST_PATCH_REPLACE;
    operation.first = first;
    operation.last = last;
    operation.patch_index = patch_index;
    operation.crossfade_frames = join_crossfade_frames > 65536u ?
                                 65536u : (uint32_t)join_crossfade_frames;
    target.post_edits[target.post_edit_count++] = operation;
    update_snapshot_after_replace(&target, first, last, inserted, output_frames, 1);
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) {
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    if (!commit_post_snapshot(instrument, &target, &current, error, error_size)) {
        ts_sample_free(&current);
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_paste(TsInstrument *instrument, const TsSample *clipboard,
                        size_t origin_first, int fit_selection,
                        char *error, size_t error_size)
{
    return instrument_paste(instrument, clipboard, origin_first, fit_selection,
                            0u, error, error_size);
}

int ts_instrument_apply_rendered_replacement(TsInstrument *instrument,
                                             const TsSample *rendered,
                                             size_t first, size_t last,
                                             char *error, size_t error_size)
{
    TsEditSnapshot target;
    TsPostEdit operation;
    TsSample material;
    TsSample current;
    uint32_t patch_index;
    uint32_t process_seed;
    size_t inserted;
    size_t output_frames;
    size_t original_view_first;
    size_t original_view_last;
    if (instrument == NULL || rendered == NULL || rendered->data == NULL ||
        rendered->frames == 0u || rendered->sample_rate == 0u ||
        instrument->current.data == NULL || first > last ||
        last > instrument->current.frames ||
        rendered->sample_rate != instrument->current.sample_rate) {
        set_error(error, error_size, "Invalid rendered replacement");
        return 0;
    }
    inserted = rendered->frames;
    if (instrument->current.frames - (last - first) > SIZE_MAX - inserted ||
        instrument->current.frames - (last - first) + inserted > TS_CANVAS_MAX_FRAMES) {
        set_error(error, error_size, "Rendered replacement is too large");
        return 0;
    }
    output_frames = instrument->current.frames - (last - first) + inserted;
    if (output_frames < TS_CANVAS_MIN_FRAMES) {
        set_error(error, error_size, "Rendered replacement is too short");
        return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    ts_sample_init(&material);
    if (!ts_sample_clone(&material, &instrument->current, error, error_size) ||
        !patch_range(&material, rendered, first, last, 0, error, error_size)) {
        ts_sample_free(&material);
        return 0;
    }
    if (!append_audio_patch(instrument, &material, NULL, &patch_index,
                            error, error_size)) {
        ts_sample_free(&material);
        return 0;
    }
    ts_sample_free(&material);
    target = snapshot(instrument);
    original_view_first = target.view_first;
    original_view_last = target.view_last;
    update_snapshot_after_replace(&target, first, last, inserted, output_frames, 1);
    if (inserted == last - first && original_view_last > original_view_first &&
        original_view_last <= output_frames) {
        target.view_first = original_view_first;
        target.view_last = original_view_last;
    } else if (original_view_last > original_view_first) {
        size_t view_first = replace_point(original_view_first, first, last, inserted);
        size_t view_last = replace_point(original_view_last, first, last, inserted);
        size_t span = original_view_last - original_view_first;
        if (view_last > output_frames) view_last = output_frames;
        if (view_first >= view_last) {
            view_first = first < output_frames ? first : output_frames - 1u;
            view_last = view_first + 1u;
        }
        if (first < view_first || first + inserted > view_last) {
            size_t wanted = span > inserted ? span : inserted;
            if (wanted > output_frames) wanted = output_frames;
            view_first = first;
            if (view_first + wanted > output_frames) view_first = output_frames - wanted;
            view_last = view_first + wanted;
        }
        target.view_first = view_first;
        target.view_last = view_last;
    }
    /* The CDP/DSP preview was rendered from Current, which already contains the
       old native process and all earlier edits. Accept the complete replacement
       result as the next canonical material checkpoint, then restart the live
       native stage from neutral. This prevents both the old post-patch masking
       bug and a second application of the process baked into the preview. Undo
       retains the complete prior graph; Redo replays this checkpoint. */
    process_seed = target.process.seed;
    target.crop_first = 0u;
    target.crop_last = instrument->parent.frames;
    memset(target.sample_edits, 0, sizeof(target.sample_edits));
    target.sample_edit_count = 0;
    ts_process_recipe_reset(&target.process);
    target.process.seed = process_seed;
    target.process_first = 0u;
    target.process_last = 0u;
    target.has_process_range = 0;
    memset(target.post_edits, 0, sizeof(target.post_edits));
    target.post_edit_count = 0;
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_MATERIAL_REPLACE;
    operation.first = 0u;
    operation.last = instrument->parent.frames;
    operation.patch_index = patch_index;
    target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) {
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    if (!commit_post_snapshot(instrument, &target, &current, error, error_size)) {
        ts_sample_free(&current);
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_apply_pitch_shift(TsInstrument *instrument,
                                    float semitones,
                                    char *error, size_t error_size)
{
    TsSample rendered;
    TsTuning unity = default_tuning();
    size_t first;
    size_t last;
    size_t source_frames;
    size_t target_frames;
    long double wanted;
    double pitch_ratio;
    int ok;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames < 2u || !isfinite(semitones)) {
        set_error(error, error_size, "Select an occupied tile before tuning");
        return 0;
    }
    if (fabsf(semitones) < 0.0001f) {
        set_error(error, error_size, "Audio is already at the reference pitch");
        return 0;
    }
    first = instrument->has_selection &&
            instrument->selection_last > instrument->selection_first ?
            instrument->selection_first : 0u;
    last = instrument->has_selection &&
           instrument->selection_last > instrument->selection_first ?
           instrument->selection_last : instrument->current.frames;
    source_frames = last - first;
    pitch_ratio = pow(2.0, (double)semitones / 12.0);
    wanted = (long double)source_frames / (long double)pitch_ratio;
    if (!isfinite(pitch_ratio) || pitch_ratio <= 0.0 || wanted < 1.0L ||
        wanted > (long double)TS_CANVAS_MAX_FRAMES) {
        set_error(error, error_size, "Tune amount exceeds the canvas limits");
        return 0;
    }
    target_frames = (size_t)llroundl(wanted);
    if (target_frames < 2u) target_frames = 2u;
    if (instrument->current.frames - source_frames >
        TS_CANVAS_MAX_FRAMES - target_frames) {
        set_error(error, error_size, "Tuned audio would exceed the canvas limit");
        return 0;
    }
    ts_sample_init(&rendered);
    if (!resample_selection_patch(&rendered, &instrument->current,
                                  first, last, target_frames,
                                  source_frames / 2u, target_frames / 2u,
                                  error, error_size))
        return 0;
    snprintf(rendered.name, sizeof(rendered.name), "TUNED %.114s",
             instrument->current.name);
    ok = ts_instrument_apply_rendered_replacement(
        instrument, &rendered, first, last, error, error_size);
    ts_sample_free(&rendered);
    if (!ok) return 0;
    /* apply_rendered_replacement already opened the single Undo transaction.
       Store unity in that resulting state so Undo restores both waveform and
       prior mapping, while Redo restores the tuned waveform at MIDI 60. */
    instrument->tuning = unity;
    instrument->audible_tuning = unity;
    if (!bank_sync_selected(instrument, error, error_size)) return 0;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_replace_selection_with_drone(TsInstrument *instrument,
                                                const TsSample *drone,
                                                char *error, size_t error_size)
{
    size_t first;
    size_t join_crossfade_frames;
    if (instrument == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Drone needs a valid selection");
        return 0;
    }
    first = instrument->selection_first;
    join_crossfade_frames = instrument->current.sample_rate / 500u;
    if (join_crossfade_frames < 1u) join_crossfade_frames = 1u;
    return instrument_paste(instrument, drone, first, 0,
                            join_crossfade_frames, error, error_size);
}

int ts_instrument_copy_drone_to_new_tile(TsInstrument *instrument,
                                          const TsSample *drone,
                                          int *destination_slot,
                                          char *error, size_t error_size)
{
    char name[sizeof(drone->name)];
    TsTuning unity = default_tuning();
    int source;
    int destination;
    if (destination_slot != NULL) *destination_slot = -1;
    if (instrument == NULL || drone == NULL || drone->data == NULL ||
        drone->frames == 0 || drone->sample_rate == 0) {
        set_error(error, error_size, "No Drone loop is ready to copy");
        return 0;
    }
    source = instrument->selected_slot;
    if (source < 0 || source >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[source].occupied) {
        set_error(error, error_size, "Drone source tile is unavailable");
        return 0;
    }
    destination = ts_instrument_bank_first_empty(instrument);
    if (destination < 0) {
        set_error(error, error_size, "No empty tile is available for Drone");
        return 0;
    }
    snprintf(name, sizeof(name), "%.127s", drone->name);
    if (!bank_sync_selected(instrument, error, error_size) ||
        !ts_instrument_select_bank(instrument, destination, error, error_size) ||
        !ts_instrument_activate_silence(instrument, drone->frames,
                                       drone->sample_rate, error, error_size))
        goto failed;
    ts_instrument_set_selection(instrument, 0, drone->frames);
    if (!ts_instrument_paste(instrument, drone, 0, 0, error, error_size))
        goto failed;
    snprintf(instrument->current.name, sizeof(instrument->current.name), "%s", name);
    snprintf(instrument->parent.name, sizeof(instrument->parent.name), "%s", name);
    snprintf(instrument->bank[destination].sample.name,
             sizeof(instrument->bank[destination].sample.name), "%s", name);
    snprintf(instrument->bank[destination].edit_parent.name,
             sizeof(instrument->bank[destination].edit_parent.name), "%s", name);
    instrument->tuning = unity;
    instrument->audible_tuning = unity;
    instrument->has_loop = 1;
    instrument->loop_first = 0;
    instrument->loop_last = drone->frames;
    instrument->loop_mode = TS_LOOP_FORWARD;
    instrument->loop_crossfade_ms = 0.0f;
    instrument->source_kind = TS_SOURCE_COMMITTED;
    if (!bank_sync_selected(instrument, error, error_size)) goto failed;
    if (destination_slot != NULL) *destination_slot = destination;
    set_error(error, error_size, "");
    return 1;

failed:
    if (destination >= 0 && destination < TS_BANK_SLOT_COUNT &&
        instrument->bank[destination].occupied)
        bank_slot_free(&instrument->bank[destination]);
    if (source >= 0 && source < TS_BANK_SLOT_COUNT)
        (void)ts_instrument_select_bank(instrument, source, NULL, 0);
    return 0;
}

static void keep_stamp_destination_visible(TsEditSnapshot *target,
                                           size_t first, size_t last,
                                           size_t old_frames, size_t new_frames)
{
    size_t span;
    if (target->view_last <= target->view_first || old_frames == 0u) {
        target->view_first = 0u;
        target->view_last = new_frames;
        return;
    }
    if (target->view_first == 0u && target->view_last >= old_frames) {
        target->view_first = 0u;
        target->view_last = new_frames;
        return;
    }
    span = target->view_last - target->view_first;
    if (span < last - first) span = last - first;
    if (span > new_frames) span = new_frames;
    if (first < target->view_first) {
        target->view_first = first;
        target->view_last = first + span;
    } else if (last > target->view_last) {
        target->view_last = last;
        target->view_first = last > span ? last - span : 0u;
    }
    if (target->view_last > new_frames) {
        target->view_last = new_frames;
        target->view_first = new_frames > span ? new_frames - span : 0u;
    }
}

static size_t chain_stamp_crossfade_frames(const TsInstrument *instrument,
                                           int crossfade_ms)
{
    uint64_t requested;
    size_t maximum;
    size_t length;
    if (instrument == NULL || crossfade_ms <= 0 ||
        instrument->current.sample_rate == 0u || !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first)
        return 0u;
    length = instrument->selection_last - instrument->selection_first;
    maximum = length / 4u;
    if (maximum > 65536u) maximum = 65536u;
    requested = ((uint64_t)instrument->current.sample_rate *
                 (uint64_t)crossfade_ms + 500u) / 1000u;
    if (requested > maximum) requested = maximum;
    return (size_t)requested;
}

static int stamp_generated_patch(TsInstrument *instrument,
                                 const TsGeneratorRecipe *recipe,
                                 size_t crossfade_frames,
                                 int advance_selection,
                                 char *error, size_t error_size)
{
    TsSample generated;
    TsSample current;
    TsEditSnapshot target;
    TsPostEdit operation;
    uint32_t patch_index;
    size_t first;
    size_t last;
    size_t length;
    size_t next_first;
    size_t next_last;
    size_t output_frames;
    size_t extension;
    if (instrument == NULL || recipe == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range on an occupied tile before stamping");
        return 0;
    }
    first = instrument->selection_first;
    last = instrument->selection_last;
    length = last - first;
    if (crossfade_frames > length / 4u) crossfade_frames = length / 4u;
    if (crossfade_frames > 65536u) crossfade_frames = 65536u;
    next_first = advance_selection ? last - crossfade_frames : first;
    if (length > SIZE_MAX - next_first) {
        set_error(error, error_size, "Chain stamp destination is too large");
        return 0;
    }
    next_last = advance_selection ? next_first + length : last;
    output_frames = next_last > instrument->current.frames ?
                    next_last : instrument->current.frames;
    if (output_frames > TS_CANVAS_MAX_FRAMES) {
        set_error(error, error_size, "Chain stamp reached the maximum canvas length");
        return 0;
    }
    extension = output_frames - instrument->current.frames;
    if (!ensure_edit_graph_capacity_for(instrument, extension > 0u ? 2 : 1,
                                        1, error, error_size)) return 0;
    ts_sample_init(&generated);
    ts_sample_init(&current);
    if (!ts_sample_generate(&generated, recipe, error, error_size)) return 0;
    if (!append_audio_patch(instrument, &generated, recipe, &patch_index,
                            error, error_size)) {
        ts_sample_free(&generated);
        return 0;
    }
    ts_sample_free(&generated);
    target = snapshot(instrument);
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_PATCH_FIT;
    operation.first = first;
    operation.last = last;
    operation.crossfade_frames = (uint32_t)crossfade_frames;
    operation.patch_index = patch_index;
    target.post_edits[target.post_edit_count++] = operation;
    update_snapshot_after_replace(&target, first, last, last - first,
                                  instrument->current.frames, 1);
    if (extension > 0u) {
        memset(&operation, 0, sizeof(operation));
        operation.kind = TS_POST_CANVAS_RIGHT_RESIZE;
        operation.destination = (int64_t)extension;
        target.post_edits[target.post_edit_count++] = operation;
    }
    if (advance_selection) {
        target.has_selection = 1;
        target.selection_first = next_first;
        target.selection_last = next_last;
        keep_stamp_destination_visible(&target, next_first, next_last,
                                       instrument->current.frames, output_frames);
    }
    if (!render_snapshot(&current, instrument, &target, error, error_size)) {
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    if (!commit_post_snapshot(instrument, &target, &current, error, error_size)) {
        ts_sample_free(&current);
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    return 1;
}

int ts_instrument_stamp_create(TsInstrument *instrument, uint32_t seed,
                               char *error, size_t error_size)
{
    TsGeneratorRecipe recipe;
    uint32_t rng = seed;
    if (instrument == NULL || instrument->current.sample_rate == 0 ||
        !instrument->has_selection || instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range before Create stamping");
        return 0;
    }
    memset(&recipe, 0, sizeof(recipe));
    recipe.kind = TS_GENERATOR_FM;
    recipe.seed = seed;
    recipe.seconds = clampf(
        (float)(instrument->selection_last - instrument->selection_first) /
        (float)instrument->current.sample_rate, 0.1f, 8.0f);
    recipe.frequency = 30.0f * powf(2000.0f / 30.0f, rng_unit(&rng));
    if (!stamp_generated_patch(instrument, &recipe, 0u, 0,
                               error, error_size)) return 0;
    ++instrument->family_sequence;
    return 1;
}

static int stamp_vary(TsInstrument *instrument, int chained,
                      int crossfade_ms, char *error, size_t error_size)
{
    const TsBankSlot *slot;
    const TsGeneratorRecipe *source_recipe = NULL;
    TsGeneratorRecipe recipe;
    TsFmPatch source_patch;
    uint32_t seed;
    if (instrument == NULL || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->has_selection || instrument->selection_last <= instrument->selection_first) {
        set_error(error, error_size, "Select a range before Vary stamping");
        return 0;
    }
    slot = &instrument->bank[instrument->selected_slot];
    for (int i = slot->patch_count - 1; i >= 0; --i) {
        if (slot->patches[i].has_generator &&
            slot->patches[i].generator.kind == TS_GENERATOR_FM) {
            source_recipe = &slot->patches[i].generator;
            break;
        }
    }
    if (source_recipe == NULL && slot->has_generator &&
        slot->generator.kind == TS_GENERATOR_FM) source_recipe = &slot->generator;
    if (source_recipe == NULL) {
        set_error(error, error_size, "Use Create once before Varying a selection");
        return 0;
    }
    recipe = *source_recipe;
    seed = advance_seed(source_recipe->seed ^ advance_seed(instrument->family_sequence + 1u));
    recipe.seed = seed;
    recipe.has_fm_patch = 1;
    ts_fm_patch_from_recipe(source_recipe, &source_patch);
    ts_fm_patch_vary(&source_patch, seed, instrument->family_mutation,
                     &recipe.fm_patch);
    recipe.seconds = clampf(
        (float)(instrument->selection_last - instrument->selection_first) /
        (float)instrument->current.sample_rate, 0.1f, 8.0f);
    if (!stamp_generated_patch(
            instrument, &recipe,
            chained ? chain_stamp_crossfade_frames(instrument, crossfade_ms) : 0u,
            chained, error, error_size)) return 0;
    ++instrument->family_sequence;
    return 1;
}

int ts_instrument_stamp_vary(TsInstrument *instrument,
                             char *error, size_t error_size)
{
    return stamp_vary(instrument, 0, 0, error, error_size);
}

int ts_instrument_stamp_vary_chained(TsInstrument *instrument,
                                     int crossfade_ms,
                                     char *error, size_t error_size)
{
    return stamp_vary(instrument, 1, crossfade_ms, error, error_size);
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
    if (!chain && instrument->bank[source].locked) {
        set_error(error, error_size, "Tile is locked - enable Chain or unlock it before Vary");
        return 0;
    }
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
        instrument->bank[destination].locked = 0;
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
        if (destination != source) made.locked = 0;
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
    candidate.tuning = default_tuning();
    candidate.audible_tuning = candidate.tuning;
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
    captured.tuning = default_tuning();
    captured.audible_tuning = captured.tuning;
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

int ts_instrument_bank_is_blank_canvas(const TsInstrument *instrument, int slot)
{
    const TsBankSlot *target;
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) return 0;
    target = &instrument->bank[slot];
    if (!target->occupied || target->sample.data == NULL ||
        target->sample.frames < TS_CANVAS_MIN_FRAMES ||
        target->sample.sample_rate == 0u) return 0;
    for (size_t frame = 0; frame < target->sample.frames; ++frame)
        if (target->sample.data[frame] != 0.0f) return 0;
    return 1;
}

int ts_instrument_capture_target_frames(const TsInstrument *instrument, int slot,
                                        uint32_t output_rate,
                                        size_t *capacity_frames,
                                        char *error, size_t error_size)
{
    const TsSample *canvas;
    long double converted;
    if (capacity_frames != NULL) *capacity_frames = 0u;
    if (ts_instrument_bank_is_locked(instrument, slot)) {
        set_error(error, error_size,
                  "Capture destination is protected - unlock it first");
        return 0;
    }
    if (!ts_instrument_bank_is_blank_canvas(instrument, slot)) {
        set_error(error, error_size,
                  "Capture needs a blank silent canvas - it will not overwrite audio");
        return 0;
    }
    if (output_rate == 0u || capacity_frames == NULL) {
        set_error(error, error_size, "Capture audio rate is unavailable");
        return 0;
    }
    canvas = &instrument->bank[slot].sample;
    converted = (long double)canvas->frames * (long double)output_rate /
                (long double)canvas->sample_rate;
    if (converted < 1.0L || converted > (long double)TS_CANVAS_MAX_FRAMES ||
        converted > (long double)SIZE_MAX) {
        set_error(error, error_size, "Capture canvas duration is out of range");
        return 0;
    }
    *capacity_frames = (size_t)llroundl(converted);
    if (*capacity_frames == 0u) *capacity_frames = 1u;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_commit_capture(TsInstrument *instrument, int destination_slot,
                                 int source_slot, const float *captured,
                                 size_t recorded_frames, uint32_t capture_rate,
                                 int stopped_early,
                                 char *error, size_t error_size)
{
    TsSample patch;
    TsSample current;
    TsEditSnapshot target;
    TsPostEdit operation;
    TsBankSlot *destination;
    TsTuning source_tuning;
    TsTuning source_audible_tuning;
    uint32_t patch_index;
    size_t target_frames;
    int operation_count;
    int synth_source;
    char name[128];
    synth_source = source_slot == TS_CAPTURE_SOURCE_SYNTH;
    if (instrument == NULL || captured == NULL || recorded_frames == 0u ||
        capture_rate == 0u || destination_slot < 0 ||
        destination_slot >= TS_BANK_SLOT_COUNT ||
        (!synth_source && (source_slot < 0 ||
                           source_slot >= TS_BANK_SLOT_COUNT ||
                           source_slot == destination_slot ||
                           !instrument->bank[source_slot].occupied)) ||
        !ts_instrument_bank_is_blank_canvas(instrument, destination_slot) ||
        instrument->bank[destination_slot].locked) {
        set_error(error, error_size, "Invalid Capture source, destination, or audio");
        return 0;
    }
    source_tuning = default_tuning();
    source_audible_tuning = default_tuning();
    if (!ts_instrument_select_bank(instrument, destination_slot,
                                   error, error_size)) return 0;
    target_frames = instrument->current.frames;
    if (stopped_early) {
        long double converted = (long double)recorded_frames *
                                (long double)instrument->current.sample_rate /
                                (long double)capture_rate;
        if (converted < (long double)TS_CANVAS_MIN_FRAMES)
            target_frames = TS_CANVAS_MIN_FRAMES;
        else if (converted < (long double)target_frames)
            target_frames = (size_t)llroundl(converted);
        if (target_frames < TS_CANVAS_MIN_FRAMES)
            target_frames = TS_CANVAS_MIN_FRAMES;
        if (target_frames > instrument->current.frames)
            target_frames = instrument->current.frames;
    }
    operation_count = target_frames < instrument->current.frames ? 2 : 1;
    if (instrument->post_edit_count > TS_POST_EDIT_DEPTH - operation_count &&
        !compact_edit_graph(instrument, error, error_size)) return 0;
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    ts_sample_init(&patch);
    patch.data = (float *)captured;
    patch.frames = recorded_frames;
    patch.sample_rate = capture_rate;
    if (synth_source)
        snprintf(name, sizeof(name), "CAPTURE %02d FROM SYNTH",
                 destination_slot + 1);
    else
        snprintf(name, sizeof(name), "CAPTURE %02d FROM %02d",
                 destination_slot + 1, source_slot + 1);
    snprintf(patch.name, sizeof(patch.name), "%s", name);
    if (!append_audio_patch(instrument, &patch, NULL, &patch_index,
                            error, error_size)) return 0;
    target = snapshot(instrument);
    if (target_frames < instrument->current.frames) {
        memset(&operation, 0, sizeof(operation));
        operation.kind = TS_POST_CANVAS_RIGHT_RESIZE;
        operation.destination = -(int64_t)(instrument->current.frames - target_frames);
        target.post_edits[target.post_edit_count++] = operation;
    }
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_PATCH_FIT;
    operation.first = 0u;
    operation.last = target_frames;
    operation.patch_index = patch_index;
    target.post_edits[target.post_edit_count++] = operation;
    target.crop_first = 0u;
    target.crop_last = instrument->parent.frames;
    target.selection_first = 0u;
    target.selection_last = target_frames;
    target.has_selection = 1;
    target.playhead_frame = 0u;
    target.has_playhead = 0;
    target.view_first = 0u;
    target.view_last = target_frames;
    target.loop_first = 0u;
    target.loop_last = 0u;
    target.has_loop = 0;
    target.tuning = source_tuning;
    target.audible_tuning = source_audible_tuning;
    snprintf(instrument->parent.name, sizeof(instrument->parent.name), "%s", name);
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) {
        discard_last_audio_patch(instrument, patch_index);
        return 0;
    }
    snprintf(current.name, sizeof(current.name), "%s", name);
    if (!commit_post_snapshot(instrument, &target, &current, error, error_size)) {
        ts_sample_free(&current);
        return 0;
    }
    instrument->source_kind = TS_SOURCE_COMMITTED;
    instrument->tuning = source_tuning;
    instrument->audible_tuning = source_audible_tuning;
    destination = &instrument->bank[destination_slot];
    destination->capture_kind = TS_BANK_CAPTURE_PERFORMANCE;
    destination->relation = TS_FAMILY_CAPTURED;
    destination->parent_slot = source_slot;
    destination->has_generator = 0;
    memset(&destination->generator, 0, sizeof(destination->generator));
    destination->lineage_seed = (uint32_t)ts_sample_hash(&destination->sample);
    destination->lineage_locks = TS_FAMILY_LOCK_ALL;
    destination->lineage_mutation = 0.0f;
    destination->tuning = source_tuning;
    destination->audible_tuning = source_audible_tuning;
    destination->has_loop = 0;
    destination->loop_first = 0u;
    destination->loop_last = 0u;
    instrument->family_anchor_slot = destination_slot;
    if (!bank_sync_selected(instrument, error, error_size)) return 0;
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
    if (instrument->bank[slot].locked) {
        set_error(error, error_size, "Tile is locked - unlock it before clearing");
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
    int first_locked = -1;
    if (instrument == NULL) { set_error(error, error_size, "No collection to clear"); return 0; }
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        if (instrument->bank[slot].occupied && instrument->bank[slot].locked) {
            if (first_locked < 0) first_locked = slot;
        } else bank_slot_free(&instrument->bank[slot]);
    }
    if (first_locked >= 0) {
        instrument->selected_slot = first_locked;
        instrument->family_anchor_slot = first_locked;
        if (!ts_instrument_select_bank(instrument, first_locked,
                                       error, error_size)) return 0;
    } else {
        ts_sample_free(&instrument->parent); ts_sample_init(&instrument->parent);
        ts_sample_free(&instrument->current); ts_sample_init(&instrument->current);
        instrument->selected_slot = 0;
        instrument->family_anchor_slot = 0;
    }
    instrument->family_last_slot = -1;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_is_locked(const TsInstrument *instrument, int slot)
{
    return instrument != NULL && slot >= 0 && slot < TS_BANK_SLOT_COUNT &&
           instrument->bank[slot].occupied && instrument->bank[slot].locked;
}

int ts_instrument_bank_set_locked(TsInstrument *instrument, int slot, int locked,
                                  char *error, size_t error_size)
{
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        set_error(error, error_size, "Invalid bank slot");
        return 0;
    }
    if (!instrument->bank[slot].occupied) {
        set_error(error, error_size, "Capture audio before locking a tile");
        return 0;
    }
    instrument->bank[slot].locked = locked != 0;
    set_error(error, error_size, "");
    return 1;
}

int ts_instrument_bank_toggle_locked(TsInstrument *instrument, int slot,
                                     char *error, size_t error_size)
{
    if (instrument == NULL || slot < 0 || slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[slot].occupied) {
        set_error(error, error_size, "Capture audio before locking a tile");
        return 0;
    }
    return ts_instrument_bank_set_locked(instrument, slot,
                                         !instrument->bank[slot].locked,
                                         error, error_size);
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
    snapped = resolve_sample_boundary(
        &bank_slot->sample, bank_slot->edit.grid_divisions,
        bank_slot->edit.grid_snap == TS_GRID_SNAP_ALL, frame);
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
    const TsTuning unity = {TS_KEYBOARD_BASE_NOTE, 0.0f};
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
                &slot->sample, &unity,
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
        if (!ensure_edit_graph_capacity(instrument, 0,
                                        error, error_size)) return 0;
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
        if (target.has_playhead && target.playhead_frame >= operation.first &&
            target.playhead_frame < operation.last)
            target.playhead_frame -= operation.first;
        else target.has_playhead = 0;
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
        instrument->playhead_frame = target.playhead_frame;
        instrument->has_playhead = target.has_playhead;
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
    if (target.has_playhead && target.playhead_frame >= instrument->selection_first &&
        target.playhead_frame < instrument->selection_last)
        target.playhead_frame -= instrument->selection_first;
    else target.has_playhead = 0;
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
    instrument->playhead_frame = target.playhead_frame;
    instrument->has_playhead = target.has_playhead;
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
        if (!ensure_edit_graph_capacity(instrument, 0,
                                        error, error_size)) return 0;
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
    if (!ensure_edit_graph_capacity(instrument, 0, error, error_size)) return 0;
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
    if (!ensure_edit_graph_capacity(instrument, 0, error, error_size)) return 0;

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
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
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
    target = snapshot(instrument);
    if (!commit_material_checkpoint(instrument, &current, &target,
                                    error, error_size)) {
        ts_sample_free(&current);
        return 0;
    }
    ts_sample_free(&current);
    return 1;
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
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
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
    TsEditSnapshot target;
    int ok;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture);
        set_error(error, error_size, "WARP gesture no longer owns Current");
        return 0;
    }
    if (gesture->amount == 0.0f)
        return ts_instrument_warp_gesture_cancel(instrument, gesture,
                                                 error, error_size);
    target = gesture->start;
    ok = commit_material_checkpoint(instrument, &instrument->current, &target,
                                    error, error_size);
    if (ok) {
        warp_gesture_clear(gesture);
        set_error(error, error_size, "");
    }
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
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    target = snapshot(instrument);
    memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_SMEAR;
    operation.first = instrument->has_selection ? instrument->selection_first : 0u;
    operation.last = instrument->has_selection ? instrument->selection_last : instrument->current.frames;
    operation.amount = amount;
    target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    target = snapshot(instrument);
    if (!commit_material_checkpoint(instrument, &current, &target,
                                    error, error_size)) {
        ts_sample_free(&current);
        return 0;
    }
    ts_sample_free(&current);
    return 1;
}

void ts_smear_gesture_init(TsSmearGesture *gesture) { ts_warp_gesture_init(gesture); }

int ts_instrument_smear_gesture_begin(TsInstrument *instrument, TsSmearGesture *gesture,
                                      char *error, size_t error_size)
{
    if (instrument == NULL || gesture == NULL || gesture->active || instrument->current.data == NULL) {
        set_error(error, error_size, "Could not begin SMEAR gesture"); return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
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
    TsEditSnapshot target;
    int ok;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture); set_error(error, error_size, "SMEAR gesture no longer owns Current"); return 0;
    }
    if (gesture->amount == 0.0f)
        return ts_instrument_smear_gesture_cancel(instrument, gesture, error, error_size);
    target = gesture->start;
    ok = commit_material_checkpoint(instrument, &instrument->current, &target,
                                    error, error_size);
    if (ok) {
        warp_gesture_clear(gesture);
        set_error(error, error_size, "");
    }
    return ok;
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
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
    target = snapshot(instrument); memset(&operation, 0, sizeof(operation));
    operation.kind = TS_POST_TEAR;
    operation.first = instrument->has_selection ? instrument->selection_first : 0u;
    operation.last = instrument->has_selection ? instrument->selection_last : instrument->current.frames;
    operation.amount = amount; target.post_edits[target.post_edit_count++] = operation;
    ts_sample_init(&current);
    if (!render_snapshot(&current, instrument, &target, error, error_size)) return 0;
    target = snapshot(instrument);
    if (!commit_material_checkpoint(instrument, &current, &target,
                                    error, error_size)) {
        ts_sample_free(&current);
        return 0;
    }
    ts_sample_free(&current);
    return 1;
}

void ts_tear_gesture_init(TsTearGesture *gesture) { ts_warp_gesture_init(gesture); }

int ts_instrument_tear_gesture_begin(TsInstrument *instrument, TsTearGesture *gesture,
                                     char *error, size_t error_size)
{
    if (instrument == NULL || gesture == NULL || gesture->active || instrument->current.data == NULL) {
        set_error(error, error_size, "Could not begin TEAR gesture"); return 0;
    }
    if (!ensure_edit_graph_capacity(instrument, 1, error, error_size)) return 0;
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
    TsEditSnapshot target; int ok;
    if (!warp_gesture_owns(instrument, gesture)) {
        warp_gesture_clear(gesture); set_error(error, error_size, "TEAR gesture no longer owns Current"); return 0;
    }
    if (gesture->amount == 0.0f)
        return ts_instrument_tear_gesture_cancel(instrument, gesture, error, error_size);
    target = gesture->start;
    ok = commit_material_checkpoint(instrument, &instrument->current, &target,
                                    error, error_size);
    if (ok) {
        warp_gesture_clear(gesture);
        set_error(error, error_size, "");
    }
    return ok;
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
    instrument->playhead_frame = target.playhead_frame;
    instrument->view_first = target.view_first;
    instrument->view_last = target.view_last;
    instrument->loop_first = target.loop_first;
    instrument->loop_last = target.loop_last;
    instrument->loop_crossfade_ms = target.loop_crossfade_ms;
    instrument->loop_mode = target.loop_mode;
    instrument->has_selection = target.has_selection;
    instrument->has_playhead = target.has_playhead;
    instrument->has_loop = target.has_loop;
    instrument->grid_divisions = target.grid_divisions;
    instrument->grid_snap = target.grid_snap;
    instrument->tuning = target.tuning;
    instrument->audible_tuning = target.audible_tuning;
    instrument->process = target.process;
    instrument->process_first = target.process_first;
    instrument->process_last = target.process_last;
    instrument->has_process_range = target.has_process_range;
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

static int get_generator_recipe(FILE *f, TsGeneratorRecipe *recipe, int version)
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
    return !recipe->has_fm_patch || get_fm_patch(f, &recipe->fm_patch, version);
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
    put64(f, state->playhead_frame); put32(f, (uint32_t)state->has_playhead);
    put64(f, state->view_first); put64(f, state->view_last);
    put64(f, state->loop_first); put64(f, state->loop_last);
    put_float(f, state->loop_crossfade_ms);
    put32(f, (uint32_t)state->loop_mode);
    put32(f, (uint32_t)state->has_selection); put32(f, (uint32_t)state->has_loop);
    put32(f, state->grid_divisions); put32(f, (uint32_t)state->grid_snap);
    put32(f, (uint32_t)state->tuning.root_note); put_float(f, state->tuning.fine_tune_cents);
    put32(f, (uint32_t)state->audible_tuning.root_note);
    put_float(f, state->audible_tuning.fine_tune_cents);
    put_process_recipe(f, &state->process);
    put64(f, state->process_first);
    put64(f, state->process_last);
    put32(f, (uint32_t)state->has_process_range);
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
        put32(f, edit->patch_index);
    }
}

static int get_edit_snapshot(FILE *f, TsEditSnapshot *state, int version)
{
    const size_t frame_limit = 100000000u;
    uint32_t value;
    uint64_t wide;
#define GET_STATE_SIZE(field) do { if (!get64(f, &wide) || wide > SIZE_MAX) return 0; (field) = (size_t)wide; } while (0)
#define GET_STATE_U32(field) do { if (!get32(f, &value)) return 0; (field) = value; } while (0)
    memset(state, 0, sizeof(*state));
    GET_STATE_SIZE(state->crop_first); GET_STATE_SIZE(state->crop_last);
    GET_STATE_SIZE(state->selection_first); GET_STATE_SIZE(state->selection_last);
    if (version >= 17) {
        GET_STATE_SIZE(state->playhead_frame); GET_STATE_U32(state->has_playhead);
    }
    GET_STATE_SIZE(state->view_first); GET_STATE_SIZE(state->view_last);
    GET_STATE_SIZE(state->loop_first); GET_STATE_SIZE(state->loop_last);
    if (!get_float(f, &state->loop_crossfade_ms)) return 0;
    GET_STATE_U32(state->loop_mode); GET_STATE_U32(state->has_selection);
    GET_STATE_U32(state->has_loop);
    if (version >= 18) {
        GET_STATE_U32(state->grid_divisions); GET_STATE_U32(state->grid_snap);
    } else {
        state->grid_divisions = TS_GRID_DIVISION_DEFAULT;
        state->grid_snap = 0;
    }
    GET_STATE_U32(state->tuning.root_note);
    if (!get_float(f, &state->tuning.fine_tune_cents)) return 0;
    GET_STATE_U32(state->audible_tuning.root_note);
    if (!get_float(f, &state->audible_tuning.fine_tune_cents) ||
        !get_process_recipe(f, &state->process)) return 0;
    if (version >= 24) {
        GET_STATE_SIZE(state->process_first);
        GET_STATE_SIZE(state->process_last);
        GET_STATE_U32(state->has_process_range);
    }
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
            (version >= 16 && !get32(f, &edit->patch_index)) ||
            edit->kind > (version >= 21 ? TS_POST_MATERIAL_REPLACE :
                          version >= 18 ? TS_POST_CANVAS_RIGHT_RESIZE :
                          version >= 17 ? TS_POST_PATCH_STRETCH_CONTRACT :
                          version >= 16 ? TS_POST_PATCH_FIT : TS_POST_TEAR) ||
            ((edit->kind != TS_POST_PATCH_REPLACE &&
              edit->kind != TS_POST_CANVAS_LEFT_RESIZE &&
              edit->kind != TS_POST_CANVAS_RIGHT_RESIZE) &&
             edit->last <= edit->first) ||
            (edit->kind == TS_POST_PATCH_REPLACE && edit->last < edit->first) ||
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
           (state->has_process_range == 0 ||
            (state->has_process_range == 1 &&
             state->process_first < state->process_last &&
             state->process_last <= frame_limit)) &&
           (state->has_playhead == 0 || state->has_playhead == 1) &&
           (state->has_loop == 0 || state->has_loop == 1) &&
           valid_grid_divisions(state->grid_divisions) &&
           (state->grid_snap == TS_GRID_SNAP_OFF ||
            state->grid_snap == TS_GRID_SNAP_ALL ||
            (version >= 19 && state->grid_snap == TS_GRID_SNAP_MOVE_ONLY)) &&
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
           (!state->has_playhead || state->playhead_frame < slot->sample.frames) &&
           (!state->has_selection || state->selection_first < state->selection_last) &&
           (!state->has_process_range ||
            (state->process_first < state->process_last &&
             state->process_last <= slot->sample.frames)) &&
           (!state->has_loop || state->loop_first < state->loop_last) &&
           valid_grid_divisions(state->grid_divisions) &&
           state->grid_snap >= TS_GRID_SNAP_OFF &&
           state->grid_snap < TS_GRID_SNAP_MODE_COUNT;
}

static int save_tsr26(const TsInstrument *instrument, FILE *f)
{
    fwrite("TSR26\r\n\032", 1, 8, f);
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
        put32(f, (uint32_t)slot->locked);
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
            default_edit.grid_divisions = TS_GRID_DIVISION_DEFAULT;
            default_edit.grid_snap = 0;
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
        put32(f, (uint32_t)slot->patch_count);
        for (int patch = 0; patch < slot->patch_count; ++patch) {
            put32(f, (uint32_t)slot->patches[patch].has_generator);
            if (slot->patches[patch].has_generator)
                put_generator_recipe(f, &slot->patches[patch].generator);
            put_sample_block(f, &slot->patches[patch].sample);
        }
        put_edit_snapshot(f, edit);
        put32(f, (uint32_t)undo_count);
        for (int h = 0; h < undo_count; ++h) put_edit_snapshot(f, &undo[h]);
        put32(f, (uint32_t)redo_count);
        for (int h = 0; h < redo_count; ++h) put_edit_snapshot(f, &redo[h]);
    }
    return !ferror(f);
}

static int snapshot_patches_valid(const TsEditSnapshot *state, const TsBankSlot *slot)
{
    for (int i = 0; i < state->post_edit_count; ++i) {
        const TsPostEdit *edit = &state->post_edits[i];
        if ((edit->kind == TS_POST_PATCH_REPLACE ||
             edit->kind == TS_POST_MATERIAL_REPLACE ||
             edit->kind == TS_POST_PATCH_FIT ||
             edit->kind == TS_POST_PATCH_STRETCH_EXPAND ||
             edit->kind == TS_POST_PATCH_STRETCH_CONTRACT) &&
            edit->patch_index >= (uint32_t)slot->patch_count) return 0;
    }
    return 1;
}

static int load_tsr15_or_newer(FILE *f, int version, TsInstrument *instrument,
                            char *error, size_t error_size)
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
        if (version >= 25) {
            if (!get32(f, &value) || value > 1u) goto malformed;
            slot->locked = (int)value;
        }
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
        if (slot->has_generator && !get_generator_recipe(f, &slot->generator, version)) goto malformed;
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
        if (!sample_result) goto malformed;
        if (version >= 16) {
            if (!get32(f, &value) || value > TS_AUDIO_PATCH_DEPTH) goto malformed;
            slot->patch_count = (int)value;
            if (slot->patch_count > 0) {
                slot->patches = calloc((size_t)slot->patch_count, sizeof(*slot->patches));
                if (slot->patches == NULL) goto out_of_memory;
                slot->patch_capacity = slot->patch_count;
            }
            for (int patch = 0; patch < slot->patch_count; ++patch) {
                if (!get32(f, &value) || value > 1u) goto malformed;
                slot->patches[patch].has_generator = (int)value;
                if (slot->patches[patch].has_generator &&
                    !get_generator_recipe(f, &slot->patches[patch].generator, version)) goto malformed;
                sample_result = get_sample_block(f, &slot->patches[patch].sample);
                if (sample_result < 0) goto out_of_memory;
                if (!sample_result) goto malformed;
            }
        }
        if (!get_edit_snapshot(f, &slot->edit, version)) goto malformed;
        if (!get32(f, &value) ||
            value > (uint32_t)(version >= 19 ? TS_HISTORY_DEPTH :
                                                 TS_LEGACY_HISTORY_DEPTH))
            goto malformed;
        slot->undo_count = (int)value;
        if (slot->undo_count > 0) {
            slot->undo = malloc((size_t)slot->undo_count * sizeof(*slot->undo));
            if (slot->undo == NULL) goto out_of_memory;
            for (int h = 0; h < slot->undo_count; ++h)
                if (!get_edit_snapshot(f, &slot->undo[h], version)) goto malformed;
            if (slot->undo_count > TS_HISTORY_DEPTH) {
                memmove(slot->undo,
                        slot->undo + (slot->undo_count - TS_HISTORY_DEPTH),
                        (size_t)TS_HISTORY_DEPTH * sizeof(*slot->undo));
                slot->undo_count = TS_HISTORY_DEPTH;
            }
        }
        if (!get32(f, &value) ||
            value > (uint32_t)(version >= 19 ? TS_HISTORY_DEPTH :
                                                 TS_LEGACY_HISTORY_DEPTH))
            goto malformed;
        slot->redo_count = (int)value;
        if (slot->redo_count > 0) {
            slot->redo = malloc((size_t)slot->redo_count * sizeof(*slot->redo));
            if (slot->redo == NULL) goto out_of_memory;
            for (int h = 0; h < slot->redo_count; ++h)
                if (!get_edit_snapshot(f, &slot->redo[h], version)) goto malformed;
            if (slot->redo_count > TS_HISTORY_DEPTH) {
                memmove(slot->redo,
                        slot->redo + (slot->redo_count - TS_HISTORY_DEPTH),
                        (size_t)TS_HISTORY_DEPTH * sizeof(*slot->redo));
                slot->redo_count = TS_HISTORY_DEPTH;
            }
        }
        slot->process = slot->edit.process;
        if (slot->capture_kind > (version >= 20 ? TS_BANK_CAPTURE_PERFORMANCE :
                                                 TS_BANK_CAPTURE_LOOP) ||
            slot->relation >= TS_FAMILY_RELATION_COUNT || slot->parent_slot < -1 ||
            slot->parent_slot >= TS_BANK_SLOT_COUNT ||
            (slot->lineage_locks & ~TS_FAMILY_LOCK_ALL) != 0u ||
            slot->lineage_mutation < 0.0f || slot->lineage_mutation > 1.0f ||
            (slot->has_generator != 0 && slot->has_generator != 1) ||
            (slot->locked != 0 && slot->locked != 1) ||
            !tuning_valid(&slot->tuning) || !tuning_valid(&slot->audible_tuning) ||
            slot->loop_mode >= TS_LOOP_MODE_COUNT ||
            (slot->has_loop != 0 && slot->has_loop != 1) ||
            slot->loop_crossfade_ms < 0.0f || slot->loop_crossfade_ms > 50.0f ||
            (slot->has_loop && (slot->loop_first >= slot->loop_last ||
                                slot->loop_last > slot->sample.frames)) ||
            !snapshot_fits_tile(&slot->edit, slot) ||
            !snapshot_patches_valid(&slot->edit, slot)) goto malformed;
        for (int h = 0; h < slot->undo_count; ++h)
            if (!snapshot_patches_valid(&slot->undo[h], slot)) goto malformed;
        for (int h = 0; h < slot->redo_count; ++h)
            if (!snapshot_patches_valid(&slot->redo[h], slot)) goto malformed;
    }
    if (fgetc(f) != EOF) goto malformed;
    loaded.selected_slot = selected;
    if (!ts_instrument_select_bank(&loaded, selected, error, error_size)) goto failed;
    ts_instrument_free(instrument);
    *instrument = loaded;
    set_error(error, error_size, "");
    return 1;
out_of_memory:
    set_error(error, error_size, "Out of memory while loading TSR15-TSR26 project");
    goto failed;
malformed:
    set_error(error, error_size, "Malformed or unsupported TSR15-TSR26 project");
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
    if (!save_tsr26(instrument, f)) {
        fclose(f);
        set_error(error, error_size, "Could not write TSR26 project");
        return 0;
    }
    if (fclose(f) != 0) {
        set_error(error, error_size, "Could not finish TSR26 project");
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
    if (memcmp(magic, "TSR26\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR25\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR24\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR23\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR22\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR21\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR20\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR19\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR18\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR17\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR16\r\n\032", 8) == 0 ||
        memcmp(magic, "TSR15\r\n\032", 8) == 0) {
        int self_contained_version = memcmp(magic, "TSR26\r\n\032", 8) == 0 ? 26 :
                                     memcmp(magic, "TSR25\r\n\032", 8) == 0 ? 25 :
                                     memcmp(magic, "TSR24\r\n\032", 8) == 0 ? 24 :
                                     memcmp(magic, "TSR23\r\n\032", 8) == 0 ? 23 :
                                     memcmp(magic, "TSR22\r\n\032", 8) == 0 ? 22 :
                                     magic[4] == '1' ? 21 :
                                     magic[4] == '0' ? 20 :
                                     magic[4] == '9' ? 19 :
                                     magic[4] == '8' ? 18 :
                                     magic[4] == '7' ? 17 :
                                     magic[4] == '6' ? 16 : 15;
        int ok = load_tsr15_or_newer(f, self_contained_version, instrument,
                                  error, error_size);
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
                  "Not a self-contained TSR6-TSR26 project");
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
        if (loaded.generator.has_fm_patch && !get_fm_patch(f, &loaded.generator.fm_patch, version)) goto malformed;
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
                            !get_fm_patch(f, &slot->generator.fm_patch, version)) goto malformed;
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
    set_error(error, error_size, "Malformed or unsupported TSR6-TSR26 project");
failed:
    fclose(f);
    ts_instrument_free(&loaded);
#undef GET_U32
#undef GET_U64
#undef GET_FLOAT
    return 0;
}
