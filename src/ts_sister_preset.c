#include "tapesister/sister_preset.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void set_error(char *error, size_t size, const char *message)
{
    if (error != NULL && size > 0u)
        snprintf(error, size, "%s", message != NULL ? message : "");
}

static int valid_name(const char *name)
{
    size_t length;
    if (name == NULL || (length = strlen(name)) == 0u ||
        length > TS_SISTER_PRESET_NAME_MAX) return 0;
    for (size_t i = 0u; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32u || c == '=' || c == '[' || c == ']' || c == '\n' || c == '\r')
            return 0;
    }
    return 1;
}

static int name_exists(const TsSisterPresetBank *bank, const char *name,
                       size_t except)
{
    if (bank == NULL || name == NULL) return 0;
    for (size_t i = 0u; i < bank->count; ++i)
        if (i != except && strcmp(bank->entries[i].name, name) == 0) return 1;
    return 0;
}

static void factory_ghost(TsSisterParameters *p, uint32_t rate)
{
    ts_sister_parameters_kafka_start(p, rate);
    p->head1_level = 0.58f;
    p->head2_level = 0.42f;
    p->head3_level = 0.32f;
    p->head1_feedback = 0.38f;
    p->head2_feedback = 0.28f;
    p->write_erase = 0.24f;
    p->ghost_tone = 0.62f;
    p->filter_cutoff_hz = 12000.0f;
}

static void factory_reverse(TsSisterParameters *p, uint32_t rate)
{
    ts_sister_parameters_kafka_start(p, rate);
    p->head1_level = 0.28f;
    p->head2_level = 0.70f;
    p->head2_rate_index = 2;
    p->head2_scrub = 0.64f;
    p->head3_level = 0.48f;
    p->head3_rate_index = 3;
    p->head3_span = 0.78f;
    p->write_erase = 0.45f;
    p->ghost_tone = 0.34f;
}

void ts_sister_preset_bank_init(TsSisterPresetBank *bank,
                                uint32_t sample_rate)
{
    if (bank == NULL) return;
    memset(bank, 0, sizeof(*bank));
    bank->count = TS_SISTER_FACTORY_PRESET_COUNT;
    snprintf(bank->entries[0].name, sizeof(bank->entries[0].name), "KAFKA START");
    ts_sister_parameters_kafka_start(&bank->entries[0].parameters, sample_rate);
    snprintf(bank->entries[1].name, sizeof(bank->entries[1].name), "GHOST FIELD");
    factory_ghost(&bank->entries[1].parameters, sample_rate);
    snprintf(bank->entries[2].name, sizeof(bank->entries[2].name), "REVERSE MEMORY");
    factory_reverse(&bank->entries[2].parameters, sample_rate);
    for (size_t i = 0u; i < bank->count; ++i) {
        bank->entries[i].factory = 1;
        ts_sister_parameters_sanitize(&bank->entries[i].parameters, sample_rate);
    }
}

int ts_sister_preset_recall(const TsSisterPresetBank *bank, size_t index,
                            TsSisterParameters *parameters)
{
    return ts_sister_preset_recall_with_locks(bank, index, parameters, NULL);
}

int ts_sister_preset_recall_with_locks(const TsSisterPresetBank *bank,
                                       size_t index,
                                       TsSisterParameters *parameters,
                                       uint64_t *parameter_locks)
{
    return ts_sister_preset_recall_with_lock_words(
        bank, index, parameters, parameter_locks, NULL);
}

int ts_sister_preset_recall_with_lock_words(
    const TsSisterPresetBank *bank, size_t index,
    TsSisterParameters *parameters, uint64_t *parameter_locks,
    uint64_t *parameter_locks_high)
{
    if (bank == NULL || parameters == NULL || index >= bank->count) return 0;
    *parameters = bank->entries[index].parameters;
    if (parameter_locks != NULL)
        *parameter_locks = bank->entries[index].parameter_locks;
    if (parameter_locks_high != NULL)
        *parameter_locks_high = bank->entries[index].parameter_locks_high;
    return 1;
}

int ts_sister_preset_save_new(TsSisterPresetBank *bank, const char *name,
                              const TsSisterParameters *parameters,
                              uint32_t sample_rate,
                              char *error, size_t error_size)
{
    return ts_sister_preset_save_new_with_locks(
        bank, name, parameters, 0u, sample_rate, error, error_size);
}

int ts_sister_preset_save_new_with_locks(
    TsSisterPresetBank *bank, const char *name,
    const TsSisterParameters *parameters, uint64_t parameter_locks,
    uint32_t sample_rate, char *error, size_t error_size)
{
    return ts_sister_preset_save_new_with_lock_words(
        bank, name, parameters, parameter_locks, 0u, sample_rate,
        error, error_size);
}

int ts_sister_preset_save_new_with_lock_words(
    TsSisterPresetBank *bank, const char *name,
    const TsSisterParameters *parameters, uint64_t parameter_locks,
    uint64_t parameter_locks_high, uint32_t sample_rate,
    char *error, size_t error_size)
{
    TsSisterPreset *preset;
    if (bank == NULL || parameters == NULL || !valid_name(name)) {
        set_error(error, error_size, "Invalid Sister preset name or parameters");
        return 0;
    }
    if (bank->count >= TS_SISTER_PRESET_LIMIT) {
        set_error(error, error_size, "Sister preset bank is full");
        return 0;
    }
    if (name_exists(bank, name, SIZE_MAX)) {
        set_error(error, error_size, "A Sister preset already has that name");
        return 0;
    }
    preset = &bank->entries[bank->count++];
    memset(preset, 0, sizeof(*preset));
    snprintf(preset->name, sizeof(preset->name), "%s", name);
    preset->parameters = *parameters;
    preset->parameter_locks = parameter_locks;
    preset->parameter_locks_high = parameter_locks_high;
    ts_sister_parameters_sanitize(&preset->parameters, sample_rate);
    set_error(error, error_size, "");
    return 1;
}

int ts_sister_preset_overwrite(TsSisterPresetBank *bank, size_t index,
                               const TsSisterParameters *parameters,
                               uint32_t sample_rate,
                               char *error, size_t error_size)
{
    return ts_sister_preset_overwrite_with_locks(
        bank, index, parameters, 0u, sample_rate, error, error_size);
}

int ts_sister_preset_overwrite_with_locks(
    TsSisterPresetBank *bank, size_t index,
    const TsSisterParameters *parameters, uint64_t parameter_locks,
    uint32_t sample_rate, char *error, size_t error_size)
{
    return ts_sister_preset_overwrite_with_lock_words(
        bank, index, parameters, parameter_locks, 0u, sample_rate,
        error, error_size);
}

int ts_sister_preset_overwrite_with_lock_words(
    TsSisterPresetBank *bank, size_t index,
    const TsSisterParameters *parameters, uint64_t parameter_locks,
    uint64_t parameter_locks_high, uint32_t sample_rate,
    char *error, size_t error_size)
{
    if (bank == NULL || parameters == NULL || index >= bank->count ||
        bank->entries[index].factory) {
        set_error(error, error_size, "Factory presets cannot be overwritten");
        return 0;
    }
    bank->entries[index].parameters = *parameters;
    bank->entries[index].parameter_locks = parameter_locks;
    bank->entries[index].parameter_locks_high = parameter_locks_high;
    ts_sister_parameters_sanitize(&bank->entries[index].parameters, sample_rate);
    set_error(error, error_size, "");
    return 1;
}

int ts_sister_preset_rename(TsSisterPresetBank *bank, size_t index,
                            const char *name,
                            char *error, size_t error_size)
{
    if (bank == NULL || index >= bank->count || bank->entries[index].factory) {
        set_error(error, error_size, "Factory presets cannot be renamed");
        return 0;
    }
    if (!valid_name(name) || name_exists(bank, name, index)) {
        set_error(error, error_size, "Invalid or duplicate Sister preset name");
        return 0;
    }
    snprintf(bank->entries[index].name, sizeof(bank->entries[index].name), "%s", name);
    set_error(error, error_size, "");
    return 1;
}

int ts_sister_preset_delete(TsSisterPresetBank *bank, size_t index,
                            char *error, size_t error_size)
{
    if (bank == NULL || index >= bank->count || bank->entries[index].factory) {
        set_error(error, error_size, "Factory presets cannot be deleted");
        return 0;
    }
    if (index + 1u < bank->count)
        memmove(&bank->entries[index], &bank->entries[index + 1u],
                (bank->count - index - 1u) * sizeof(bank->entries[0]));
    --bank->count;
    memset(&bank->entries[bank->count], 0, sizeof(bank->entries[0]));
    set_error(error, error_size, "");
    return 1;
}

static int write_parameters(FILE *file, const TsSisterParameters *p)
{
    return fprintf(file,
        "h1_level=%.9g\nh1_time_ms=%.9g\nh1_feedback=%.9g\n"
        "h2_level=%.9g\nh2_scrub=%.9g\nh2_rate=%d\nh2_feedback=%.9g\n"
        "h3_level=%.9g\nh3_span=%.9g\nh3_rate=%d\n"
        "wow=%.9g\ndrop=%.9g\nduck_enabled=%d\nduck_mode=%d\nduck_sensitivity=%.9g\n"
        "decor=%d\nwidth=%.9g\nfilter_type=%d\nfilter_cutoff=%.9g\n"
        "filter_q=%.9g\nfilter_gain=%.9g\ninput=%.9g\n"
        "tiles_gain=%.9g\nfm_gain=%.9g\nexternal_gain=%.9g\npreview_gain=%.9g\n"
        "dry=%.9g\nwet=%.9g\nout=%.9g\nfx_return_gain=%.9g\n"
        "erase=%.9g\nghost_tone=%.9g\n"
        "soak=%.9g\nbleed=%.9g\nsoak_targets=%u\n"
        "reverb_type=%d\nreverb_size=%.9g\nreverb_mix=%.9g\nfx_enabled=%d\n"
        "reverb_enabled=%d\ndelay_enabled=%d\ndistortion_enabled=%d\n"
        "grain_enabled=%d\n"
        "fx_transition=%.9g\nmaster_fx_transition=%.9g\n"
        "reverb_decay=%.9g\nreverb_gain_db=%.9g\nreverb_targets=%u\n"
        "delay_time=%.9g\ndelay_feedback=%.9g\ndelay_mix=%.9g\n"
        "delay_gain_db=%.9g\ndelay_targets=%u\n"
        "distortion_drive=%.9g\ndistortion_tone=%.9g\ndistortion_mix=%.9g\n"
        "distortion_gain_db=%.9g\ndistortion_targets=%u\n"
        "grain_size=%.9g\ngrain_density=%.9g\ngrain_pitch=%.9g\n"
        "grain_mix=%.9g\ngrain_gain_db=%.9g\ngrain_targets=%u\n"
        "master_fx_feedback=%.9g\nbuffer_seconds=%.9g\n"
        "fallout_enabled=%d\nfallout_mix=%.9g\nfallout_feedback=%.9g\n"
        "fallout_noise=%.9g\nfallout_noise_type=%d\nfallout_transition=%.9g\n"
        "fallout_component_transition=%.9g\nfallout_master_transition=%.9g\n"
        "fallout_drop_enabled=%d\nfallout_drop_rate=%.9g\n"
        "fallout_pan_enabled=%d\nfallout_pan_rate=%.9g\n"
        "fallout_skip_enabled=%d\nfallout_skip_span=%.9g\nfallout_skip_rate=%.9g\n"
        "fallout_bit_enabled=%d\nfallout_bit_quality=%.9g\n"
        "fallout_bit_resolution=%.9g\nfallout_bit_rate=%.9g\n"
        "fallout_pitch_enabled=%d\nfallout_pitch=%.9g\n"
        "fallout_pitch_ramp=%.9g\nfallout_pitch_rate=%.9g\n"
        "fallout_lfo_rate=%.9g\nfallout_lfo_intensity=%.9g\n"
        "fallout_lfo_targets=%u\nfallout_rise_mode=%d\n"
        "fallout_rise_length=%.9g\nfallout_rise_intensity=%.9g\n"
        "fallout_rise_targets=%u\n",
        p->head1_level, p->head1_time_ms, p->head1_feedback,
        p->head2_level, p->head2_scrub, p->head2_rate_index, p->head2_feedback,
        p->head3_level, p->head3_span, p->head3_rate_index,
        p->wow, p->drop, p->duck_enabled, p->duck_mode, p->duck_sensitivity,
        p->decorrelation_enabled, p->width, p->filter_type,
        p->filter_cutoff_hz, p->filter_q, p->filter_gain_db,
        p->input_gain, p->tiles_gain, p->fm_gain, p->external_gain,
        p->preview_gain, p->monitor_dry, p->monitor_wet, p->mix_output_gain,
        p->fx_return_gain,
        p->write_erase, p->ghost_tone, p->soak, p->bleed,
        (unsigned)p->soak_targets, p->fx.reverb_type, p->fx.reverb_size,
        p->fx.reverb_mix,
        p->fx.enabled, p->fx.reverb_enabled, p->fx.delay_enabled,
        p->fx.distortion_enabled, p->fx.grain_enabled,
        p->fx.transition, p->fx.master_transition,
        p->fx.reverb_decay, p->fx.reverb_gain_db,
        (unsigned)p->fx.reverb_targets,
        p->fx.delay_time, p->fx.delay_feedback, p->fx.delay_mix,
        p->fx.delay_gain_db, (unsigned)p->fx.delay_targets,
        p->fx.distortion_drive,
        p->fx.distortion_tone, p->fx.distortion_mix,
        p->fx.distortion_gain_db, (unsigned)p->fx.distortion_targets,
        p->fx.grain_size, p->fx.grain_density, p->fx.grain_pitch,
        p->fx.grain_mix, p->fx.grain_gain_db,
        (unsigned)p->fx.grain_targets,
        p->fx.master_feedback,
        p->buffer_seconds, p->fx.fallout.enabled, p->fx.fallout.mix,
        p->fx.fallout.feedback, p->fx.fallout.noise,
        p->fx.fallout.noise_type, p->fx.fallout.transition,
        p->fx.fallout.component_transition, p->fx.fallout.master_transition,
        p->fx.fallout.drop_enabled, p->fx.fallout.drop_rate,
        p->fx.fallout.pan_enabled, p->fx.fallout.pan_rate,
        p->fx.fallout.skip_enabled, p->fx.fallout.skip_span,
        p->fx.fallout.skip_rate, p->fx.fallout.bit_enabled,
        p->fx.fallout.bit_quality, p->fx.fallout.bit_resolution,
        p->fx.fallout.bit_rate, p->fx.fallout.pitch_enabled,
        p->fx.fallout.pitch, p->fx.fallout.pitch_ramp,
        p->fx.fallout.pitch_rate, p->fx.fallout.lfo_rate,
        p->fx.fallout.lfo_intensity,
        (unsigned)p->fx.fallout.lfo_targets, p->fx.fallout.rise_mode,
        p->fx.fallout.rise_length, p->fx.fallout.rise_intensity,
        (unsigned)p->fx.fallout.rise_targets) >= 0;
}

static int replace_file(const char *temporary, const char *path)
{
#ifdef _WIN32
    return MoveFileExA(temporary, path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, path) == 0;
#endif
}

int ts_sister_preset_save(const TsSisterPresetBank *bank, const char *path,
                          char *error, size_t error_size)
{
    char temporary[1200];
    FILE *file;
    int failed = 0;
    if (bank == NULL || path == NULL || path[0] == '\0' ||
        snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", path) < 0 ||
        strlen(path) + 20u >= sizeof(temporary)) {
        set_error(error, error_size, "Invalid Sister preset path");
        return 0;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        set_error(error, error_size, "Could not create Sister preset file");
        return 0;
    }
    failed = fprintf(file, "TapeSister Sister Presets\nVersion=%d\n",
                     TS_SISTER_PRESET_VERSION) < 0;
    for (size_t i = TS_SISTER_FACTORY_PRESET_COUNT;
         i < bank->count && !failed; ++i) {
        failed = fprintf(file, "\n[Preset]\nName=%s\nlocks=%016" PRIX64
                         "\nlocks_high=%016" PRIX64 "\n",
                         bank->entries[i].name,
                         bank->entries[i].parameter_locks,
                         bank->entries[i].parameter_locks_high) < 0 ||
                 !write_parameters(file, &bank->entries[i].parameters);
    }
    if (fclose(file) != 0) failed = 1;
    if (failed || !replace_file(temporary, path)) {
        remove(temporary);
        set_error(error, error_size, "Could not atomically save Sister presets");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int parse_float(const char *text, float *value)
{
    char *end;
    double parsed;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *trim(end) != '\0' || !isfinite(parsed)) return 0;
    *value = (float)parsed;
    return isfinite(*value);
}

static int parse_int(const char *text, int *value)
{
    char *end;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0' ||
        parsed < -2147483647L - 1L || parsed > 2147483647L) return 0;
    *value = (int)parsed;
    return 1;
}

static int assign_field(TsSisterParameters *p, const char *key,
                        const char *value)
{
    int parsed_int;
#define FLOAT_FIELD(name, member) if (strcmp(key, name) == 0) return parse_float(value, &p->member)
#define INT_FIELD(name, member) if (strcmp(key, name) == 0) return parse_int(value, &p->member)
    FLOAT_FIELD("h1_level", head1_level); FLOAT_FIELD("h1_time_ms", head1_time_ms);
    FLOAT_FIELD("h1_feedback", head1_feedback); FLOAT_FIELD("h2_level", head2_level);
    FLOAT_FIELD("h2_scrub", head2_scrub); INT_FIELD("h2_rate", head2_rate_index);
    FLOAT_FIELD("h2_feedback", head2_feedback); FLOAT_FIELD("h3_level", head3_level);
    FLOAT_FIELD("h3_span", head3_span); INT_FIELD("h3_rate", head3_rate_index);
    FLOAT_FIELD("wow", wow); FLOAT_FIELD("drop", drop); INT_FIELD("duck_enabled", duck_enabled);
    if (strcmp(key, "duck_mode") == 0) {
        if (!parse_int(value, &parsed_int)) return 0;
        p->duck_mode = (TsSisterDuckMode)parsed_int;
        return 1;
    }
    FLOAT_FIELD("duck_sensitivity", duck_sensitivity);
    INT_FIELD("decor", decorrelation_enabled); FLOAT_FIELD("width", width);
    if (strcmp(key, "filter_type") == 0) {
        if (!parse_int(value, &parsed_int)) return 0;
        p->filter_type = (TsSisterFilterType)parsed_int;
        return 1;
    }
    FLOAT_FIELD("filter_cutoff", filter_cutoff_hz); FLOAT_FIELD("filter_q", filter_q);
    FLOAT_FIELD("filter_gain", filter_gain_db); FLOAT_FIELD("input", input_gain);
    FLOAT_FIELD("tiles_gain", tiles_gain); FLOAT_FIELD("fm_gain", fm_gain);
    FLOAT_FIELD("external_gain", external_gain);
    FLOAT_FIELD("preview_gain", preview_gain);
    FLOAT_FIELD("dry", monitor_dry); FLOAT_FIELD("wet", monitor_wet);
    FLOAT_FIELD("out", mix_output_gain);
    FLOAT_FIELD("fx_return_gain", fx_return_gain);
    FLOAT_FIELD("erase", write_erase);
    FLOAT_FIELD("ghost_tone", ghost_tone);
    FLOAT_FIELD("soak", soak); FLOAT_FIELD("bleed", bleed);
    if (strcmp(key, "soak_targets") == 0) {
        if (!parse_int(value, &parsed_int) || parsed_int < 0 ||
            parsed_int > 255) return 0;
        p->soak_targets = (uint8_t)parsed_int;
        return 1;
    }
    if (strcmp(key, "reverb_type") == 0) {
        if (!parse_int(value, &parsed_int)) return 0;
        p->fx.reverb_type = (TsSisterReverbType)parsed_int;
        p->fx.reverb_size = ts_sister_reverb_legacy_size(
            p->fx.reverb_type);
        return 1;
    }
    FLOAT_FIELD("reverb_size", fx.reverb_size);
    FLOAT_FIELD("reverb_mix", fx.reverb_mix);
    INT_FIELD("fx_enabled", fx.enabled);
    INT_FIELD("reverb_enabled", fx.reverb_enabled);
    INT_FIELD("delay_enabled", fx.delay_enabled);
    INT_FIELD("distortion_enabled", fx.distortion_enabled);
    INT_FIELD("grain_enabled", fx.grain_enabled);
    if (strcmp(key, "fx_transition") == 0) {
        if (!parse_float(value, &p->fx.transition)) return 0;
        /* Files written before the split used this one clock for both. */
        p->fx.master_transition = p->fx.transition;
        return 1;
    }
    FLOAT_FIELD("master_fx_transition", fx.master_transition);
    FLOAT_FIELD("reverb_decay", fx.reverb_decay);
    FLOAT_FIELD("reverb_gain_db", fx.reverb_gain_db);
    FLOAT_FIELD("delay_time", fx.delay_time);
    FLOAT_FIELD("delay_feedback", fx.delay_feedback);
    FLOAT_FIELD("delay_mix", fx.delay_mix);
    FLOAT_FIELD("delay_gain_db", fx.delay_gain_db);
    FLOAT_FIELD("distortion_drive", fx.distortion_drive);
    FLOAT_FIELD("distortion_tone", fx.distortion_tone);
    FLOAT_FIELD("distortion_mix", fx.distortion_mix);
    FLOAT_FIELD("distortion_gain_db", fx.distortion_gain_db);
    FLOAT_FIELD("grain_size", fx.grain_size);
    FLOAT_FIELD("grain_density", fx.grain_density);
    FLOAT_FIELD("grain_pitch", fx.grain_pitch);
    FLOAT_FIELD("grain_mix", fx.grain_mix);
    FLOAT_FIELD("grain_gain_db", fx.grain_gain_db);
    FLOAT_FIELD("master_fx_feedback", fx.master_feedback);
    FLOAT_FIELD("buffer_seconds", buffer_seconds);
    INT_FIELD("fallout_enabled", fx.fallout.enabled);
    FLOAT_FIELD("fallout_mix", fx.fallout.mix);
    FLOAT_FIELD("fallout_feedback", fx.fallout.feedback);
    FLOAT_FIELD("fallout_noise", fx.fallout.noise);
    if (strcmp(key, "fallout_noise_type") == 0) {
        if (!parse_int(value, &parsed_int)) return 0;
        p->fx.fallout.noise_type = (TsSisterFalloutNoiseType)parsed_int;
        return 1;
    }
    FLOAT_FIELD("fallout_transition", fx.fallout.transition);
    if (strcmp(key, "fallout_component_transition") == 0) {
        if (!parse_float(value, &p->fx.fallout.component_transition)) return 0;
        p->fx.fallout.master_transition =
            p->fx.fallout.component_transition;
        return 1;
    }
    FLOAT_FIELD("fallout_master_transition", fx.fallout.master_transition);
    INT_FIELD("fallout_drop_enabled", fx.fallout.drop_enabled);
    FLOAT_FIELD("fallout_drop_rate", fx.fallout.drop_rate);
    INT_FIELD("fallout_pan_enabled", fx.fallout.pan_enabled);
    FLOAT_FIELD("fallout_pan_rate", fx.fallout.pan_rate);
    INT_FIELD("fallout_skip_enabled", fx.fallout.skip_enabled);
    FLOAT_FIELD("fallout_skip_span", fx.fallout.skip_span);
    FLOAT_FIELD("fallout_skip_rate", fx.fallout.skip_rate);
    INT_FIELD("fallout_bit_enabled", fx.fallout.bit_enabled);
    FLOAT_FIELD("fallout_bit_quality", fx.fallout.bit_quality);
    FLOAT_FIELD("fallout_bit_resolution", fx.fallout.bit_resolution);
    FLOAT_FIELD("fallout_bit_rate", fx.fallout.bit_rate);
    INT_FIELD("fallout_pitch_enabled", fx.fallout.pitch_enabled);
    FLOAT_FIELD("fallout_pitch", fx.fallout.pitch);
    FLOAT_FIELD("fallout_pitch_ramp", fx.fallout.pitch_ramp);
    FLOAT_FIELD("fallout_pitch_rate", fx.fallout.pitch_rate);
    FLOAT_FIELD("fallout_lfo_rate", fx.fallout.lfo_rate);
    FLOAT_FIELD("fallout_lfo_intensity", fx.fallout.lfo_intensity);
    if (strcmp(key, "fallout_lfo_targets") == 0) {
        if (!parse_int(value, &parsed_int) || parsed_int < 0) return 0;
        p->fx.fallout.lfo_targets = (uint32_t)parsed_int;
        return 1;
    }
    if (strcmp(key, "fallout_rise_mode") == 0) {
        if (!parse_int(value, &parsed_int)) return 0;
        p->fx.fallout.rise_mode = (TsSisterFalloutRiseMode)parsed_int;
        return 1;
    }
    FLOAT_FIELD("fallout_rise_length", fx.fallout.rise_length);
    FLOAT_FIELD("fallout_rise_intensity", fx.fallout.rise_intensity);
    if (strcmp(key, "fallout_rise_targets") == 0) {
        if (!parse_int(value, &parsed_int) || parsed_int < 0) return 0;
        p->fx.fallout.rise_targets = (uint32_t)parsed_int;
        return 1;
    }
    if (strcmp(key, "reverb_targets") == 0 ||
        strcmp(key, "delay_targets") == 0 ||
        strcmp(key, "distortion_targets") == 0 ||
        strcmp(key, "grain_targets") == 0) {
        uint8_t *target = strcmp(key, "reverb_targets") == 0 ?
            &p->fx.reverb_targets : strcmp(key, "delay_targets") == 0 ?
            &p->fx.delay_targets : strcmp(key, "distortion_targets") == 0 ?
            &p->fx.distortion_targets : &p->fx.grain_targets;
        if (!parse_int(value, &parsed_int) || parsed_int < 0 ||
            parsed_int > 255) return 0;
        *target = (uint8_t)parsed_int; return 1;
    }
#undef FLOAT_FIELD
#undef INT_FIELD
    return 1; /* Unknown newer fields are intentionally ignored. */
}

int ts_sister_preset_load(TsSisterPresetBank *bank, const char *path,
                          uint32_t sample_rate,
                          char *error, size_t error_size)
{
    TsSisterPresetBank loaded;
    TsSisterParameters current;
    uint64_t current_locks = 0u;
    uint64_t current_locks_high = 0u;
    char current_name[TS_SISTER_PRESET_NAME_MAX + 1] = "";
    char line[512];
    FILE *file;
    int line_number = 0;
    int saw_header = 0;
    int saw_version = 0;
    int in_preset = 0;
    if (bank == NULL || path == NULL) return 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            ts_sister_preset_bank_init(bank, sample_rate);
            set_error(error, error_size, "");
            return 1;
        }
        set_error(error, error_size, "Could not open Sister preset file");
        return 0;
    }
    ts_sister_preset_bank_init(&loaded, sample_rate);
    ts_sister_parameters_default(&current, sample_rate);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = trim(line);
        char *equals;
        ++line_number;
        if (*text == '\0' || *text == ';' || *text == '#') continue;
        if (!saw_header) {
            if (strcmp(text, "TapeSister Sister Presets") != 0) goto malformed;
            saw_header = 1;
            continue;
        }
        if (strcmp(text, "[Preset]") == 0) {
            if (in_preset) {
                if (!valid_name(current_name) ||
                    !ts_sister_preset_save_new_with_lock_words(
                        &loaded, current_name, &current, current_locks,
                        current_locks_high,
                        sample_rate, error, error_size))
                    goto fail;
            }
            in_preset = 1;
            current_name[0] = '\0';
            ts_sister_parameters_default(&current, sample_rate);
            current_locks = 0u;
            current_locks_high = 0u;
            continue;
        }
        equals = strchr(text, '=');
        if (equals == NULL) goto malformed;
        *equals++ = '\0';
        text = trim(text);
        equals = trim(equals);
        if (!in_preset && strcmp(text, "Version") == 0) {
            int version;
            if (!parse_int(equals, &version) || version < 1) goto malformed;
            saw_version = 1;
        } else if (in_preset && strcmp(text, "Name") == 0) {
            if (!valid_name(equals)) goto malformed;
            snprintf(current_name, sizeof(current_name), "%s", equals);
        } else if (in_preset && strcmp(text, "locks") == 0) {
            char *end;
            unsigned long long parsed;
            errno = 0;
            parsed = strtoull(equals, &end, 16);
            if (errno != 0 || end == equals || *trim(end) != '\0')
                goto malformed;
            current_locks = (uint64_t)parsed;
        } else if (in_preset && strcmp(text, "locks_high") == 0) {
            char *end;
            unsigned long long parsed;
            errno = 0;
            parsed = strtoull(equals, &end, 16);
            if (errno != 0 || end == equals || *trim(end) != '\0')
                goto malformed;
            current_locks_high = (uint64_t)parsed;
        } else if (in_preset && !assign_field(&current, text, equals)) {
            goto malformed;
        }
    }
    if (ferror(file)) goto malformed;
    if (!saw_header || !saw_version) goto malformed;
    if (in_preset) {
        if (!valid_name(current_name) ||
            !ts_sister_preset_save_new_with_lock_words(
                &loaded, current_name, &current, current_locks,
                current_locks_high,
                sample_rate, error, error_size))
            goto fail;
    }
    fclose(file);
    *bank = loaded;
    set_error(error, error_size, "");
    return 1;
malformed:
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "Malformed Sister preset file at line %d",
                 line_number);
fail:
    fclose(file);
    return 0;
}
