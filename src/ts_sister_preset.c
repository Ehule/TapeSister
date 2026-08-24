#include "tapesister/sister_preset.h"

#include <ctype.h>
#include <errno.h>
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
    if (bank == NULL || parameters == NULL || index >= bank->count) return 0;
    *parameters = bank->entries[index].parameters;
    return 1;
}

int ts_sister_preset_save_new(TsSisterPresetBank *bank, const char *name,
                              const TsSisterParameters *parameters,
                              uint32_t sample_rate,
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
    ts_sister_parameters_sanitize(&preset->parameters, sample_rate);
    set_error(error, error_size, "");
    return 1;
}

int ts_sister_preset_overwrite(TsSisterPresetBank *bank, size_t index,
                               const TsSisterParameters *parameters,
                               uint32_t sample_rate,
                               char *error, size_t error_size)
{
    if (bank == NULL || parameters == NULL || index >= bank->count ||
        bank->entries[index].factory) {
        set_error(error, error_size, "Factory presets cannot be overwritten");
        return 0;
    }
    bank->entries[index].parameters = *parameters;
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
        "filter_q=%.9g\nfilter_gain=%.9g\ninput=%.9g\ndry=%.9g\nwet=%.9g\n"
        "out=%.9g\nerase=%.9g\nghost_tone=%.9g\n",
        p->head1_level, p->head1_time_ms, p->head1_feedback,
        p->head2_level, p->head2_scrub, p->head2_rate_index, p->head2_feedback,
        p->head3_level, p->head3_span, p->head3_rate_index,
        p->wow, p->drop, p->duck_enabled, p->duck_mode, p->duck_sensitivity,
        p->decorrelation_enabled, p->width, p->filter_type,
        p->filter_cutoff_hz, p->filter_q, p->filter_gain_db,
        p->input_gain, p->monitor_dry, p->monitor_wet, p->mix_output_gain,
        p->write_erase, p->ghost_tone) >= 0;
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
        failed = fprintf(file, "\n[Preset]\nName=%s\n", bank->entries[i].name) < 0 ||
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
    FLOAT_FIELD("dry", monitor_dry); FLOAT_FIELD("wet", monitor_wet);
    FLOAT_FIELD("out", mix_output_gain); FLOAT_FIELD("erase", write_erase);
    FLOAT_FIELD("ghost_tone", ghost_tone);
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
                    !ts_sister_preset_save_new(&loaded, current_name, &current,
                                               sample_rate, error, error_size))
                    goto fail;
            }
            in_preset = 1;
            current_name[0] = '\0';
            ts_sister_parameters_default(&current, sample_rate);
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
        } else if (in_preset && !assign_field(&current, text, equals)) {
            goto malformed;
        }
    }
    if (ferror(file)) goto malformed;
    if (!saw_header || !saw_version) goto malformed;
    if (in_preset) {
        if (!valid_name(current_name) ||
            !ts_sister_preset_save_new(&loaded, current_name, &current,
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
