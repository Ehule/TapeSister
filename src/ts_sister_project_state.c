#include "tapesister/sister_project_state.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void state_error(char *error, size_t size, const char *message)
{
    if (error != NULL && size > 0u)
        snprintf(error, size, "%s", message != NULL ? message : "");
}

static int state_path(const char *project, char *path, size_t size)
{
    int written;
    if (project == NULL || project[0] == '\0') return 0;
    written = snprintf(path, size, "%s.samples/sister-state.ini", project);
    return written >= 0 && (size_t)written < size;
}

void ts_sister_project_state_init(TsSisterProjectState *state,
                                  uint32_t sample_rate)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->page_count = 1u;
    ts_sister_parameters_default(&state->parameters, sample_rate);
}

void ts_sister_project_state_capture(TsSisterProjectState *state,
                                     const TsSisterRuntime *runtime,
                                     size_t page_count,
                                     const char *selected_preset)
{
    if (state == NULL || runtime == NULL) return;
    ts_sister_project_state_init(state,
        runtime->enabled ? runtime->machine.buffer.sample_rate : 48000u);
    state->source_switches = runtime->source_switches & TS_SISTER_SOURCE_ALL;
    state->page_count = page_count > TS_SISTER_RUNTIME_PAGE_LIMIT ?
                        TS_SISTER_RUNTIME_PAGE_LIMIT : page_count;
    if (state->page_count == 0u) state->page_count = 1u;
    state->active_page = runtime->active_page < state->page_count ?
                         runtime->active_page : 0u;
    memcpy(state->page_masks, runtime->page_source_masks,
           state->page_count * sizeof(state->page_masks[0]));
    state->parameters = runtime->parameters;
    ts_sister_parameters_sanitize(&state->parameters,
        runtime->enabled ? runtime->machine.buffer.sample_rate : 48000u);
    if (selected_preset == NULL) selected_preset = runtime->selected_preset;
    if (selected_preset != NULL)
        snprintf(state->selected_preset, sizeof(state->selected_preset),
                 "%.47s", selected_preset);
}

int ts_sister_project_state_apply(const TsSisterProjectState *state,
                                  TsSisterRuntime *runtime,
                                  const TsInstrument *active_instrument)
{
    if (state == NULL || runtime == NULL || state->page_count == 0u ||
        state->page_count > TS_SISTER_RUNTIME_PAGE_LIMIT ||
        state->active_page >= state->page_count) return 0;
    runtime->source_switches = state->source_switches & TS_SISTER_SOURCE_ALL;
    memset(runtime->page_source_masks, 0, sizeof(runtime->page_source_masks));
    memcpy(runtime->page_source_masks, state->page_masks,
           state->page_count * sizeof(state->page_masks[0]));
    runtime->active_page = state->active_page;
    ts_sister_runtime_set_parameters(runtime, &state->parameters);
    ts_sister_runtime_set_selected_preset(runtime, state->selected_preset);
    if (active_instrument != NULL)
        (void)ts_sister_runtime_validate_source_mask(runtime, active_instrument);
    /* Publish once more after active-page validation so snapshots never expose
       a restored bit whose tile is absent or malformed in the loaded page. */
    ts_sister_runtime_set_sources(runtime, runtime->source_switches);
    return 1;
}

static int write_parameters(FILE *file, const TsSisterParameters *p)
{
    return fprintf(file,
        "H1Level=%.9g\nH1TimeMs=%.9g\nH1Feedback=%.9g\n"
        "H2Level=%.9g\nH2Scrub=%.9g\nH2Rate=%d\nH2Feedback=%.9g\n"
        "H3Level=%.9g\nH3Span=%.9g\nH3Rate=%d\nWow=%.9g\nDrop=%.9g\n"
        "DuckEnabled=%d\nDuckMode=%d\nDuckSensitivity=%.9g\nDecor=%d\n"
        "Width=%.9g\nFilterType=%d\nFilterCutoff=%.9g\nFilterQ=%.9g\n"
        "FilterGain=%.9g\nInput=%.9g\nDry=%.9g\nWet=%.9g\nOut=%.9g\n"
        "Erase=%.9g\nGhostTone=%.9g\nSoak=%.9g\nBleed=%.9g\n"
        "SoakTargets=%u\n",
        p->head1_level, p->head1_time_ms, p->head1_feedback,
        p->head2_level, p->head2_scrub, p->head2_rate_index, p->head2_feedback,
        p->head3_level, p->head3_span, p->head3_rate_index, p->wow, p->drop,
        p->duck_enabled, p->duck_mode, p->duck_sensitivity,
        p->decorrelation_enabled, p->width, p->filter_type,
        p->filter_cutoff_hz, p->filter_q, p->filter_gain_db,
        p->input_gain, p->monitor_dry, p->monitor_wet, p->mix_output_gain,
        p->write_erase, p->ghost_tone, p->soak, p->bleed,
        (unsigned)p->soak_targets) >= 0;
}

int ts_sister_project_state_save(const TsSisterProjectState *state,
                                 const char *project_path,
                                 char *error, size_t error_size)
{
    char path[1200], temporary[1224];
    FILE *file;
    int failed;
    if (state == NULL || state->page_count == 0u ||
        state->page_count > TS_SISTER_RUNTIME_PAGE_LIMIT ||
        state->active_page >= state->page_count ||
        !state_path(project_path, path, sizeof(path)) ||
        snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", path) < 0 ||
        strlen(path) + 20u >= sizeof(temporary)) {
        state_error(error, error_size, "Invalid Sister project state");
        return 0;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        state_error(error, error_size, "Could not create Sister project state");
        return 0;
    }
    failed = fprintf(file,
        "TapeSister Sister Project State\nVersion=%d\nPageCount=%zu\n"
        "ActivePage=%zu\nRoutes=%u\nSelectedPreset=%s\n",
        TS_SISTER_PROJECT_STATE_VERSION, state->page_count, state->active_page,
        (unsigned)(state->source_switches & TS_SISTER_SOURCE_ALL),
        state->selected_preset) < 0;
    for (size_t page = 0u; page < state->page_count && !failed; ++page)
        failed = fprintf(file, "Mask.%zu=%04X\n", page,
                         state->page_masks[page]) < 0;
    if (!failed) failed = !write_parameters(file, &state->parameters);
    if (fclose(file) != 0) failed = 1;
    if (!failed) {
#ifdef _WIN32
        failed = !MoveFileExA(temporary, path,
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        failed = rename(temporary, path) != 0;
#endif
    }
    if (failed) {
        remove(temporary);
        state_error(error, error_size,
                    "Could not atomically save Sister project state");
        return 0;
    }
    state_error(error, error_size, "");
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

static int parse_float_value(const char *text, float *value)
{
    char *end;
    double parsed;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *trim(end) != '\0' || !isfinite(parsed)) return 0;
    *value = (float)parsed;
    return isfinite(*value);
}

static int parse_size_value(const char *text, size_t *value)
{
    char *end;
    unsigned long long parsed;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0' || parsed > SIZE_MAX) return 0;
    *value = (size_t)parsed;
    return 1;
}

static int parse_int_value(const char *text, int *value)
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

static int assign_parameter(TsSisterParameters *p, const char *key,
                            const char *value)
{
    int integer;
#define PF(name, member) if (strcmp(key, name) == 0) return parse_float_value(value, &p->member)
#define PI(name, member) if (strcmp(key, name) == 0) return parse_int_value(value, &p->member)
    PF("H1Level", head1_level); PF("H1TimeMs", head1_time_ms); PF("H1Feedback", head1_feedback);
    PF("H2Level", head2_level); PF("H2Scrub", head2_scrub); PI("H2Rate", head2_rate_index);
    PF("H2Feedback", head2_feedback); PF("H3Level", head3_level); PF("H3Span", head3_span);
    PI("H3Rate", head3_rate_index); PF("Wow", wow); PF("Drop", drop);
    PI("DuckEnabled", duck_enabled);
    if (strcmp(key, "DuckMode") == 0) { if (!parse_int_value(value, &integer)) return 0; p->duck_mode = (TsSisterDuckMode)integer; return 1; }
    PF("DuckSensitivity", duck_sensitivity); PI("Decor", decorrelation_enabled);
    PF("Width", width);
    if (strcmp(key, "FilterType") == 0) { if (!parse_int_value(value, &integer)) return 0; p->filter_type = (TsSisterFilterType)integer; return 1; }
    PF("FilterCutoff", filter_cutoff_hz); PF("FilterQ", filter_q); PF("FilterGain", filter_gain_db);
    PF("Input", input_gain); PF("Dry", monitor_dry); PF("Wet", monitor_wet);
    PF("Out", mix_output_gain); PF("Erase", write_erase); PF("GhostTone", ghost_tone);
    PF("Soak", soak); PF("Bleed", bleed);
    if (strcmp(key, "SoakTargets") == 0) {
        if (!parse_int_value(value, &integer) || integer < 0 ||
            integer > 255) return 0;
        p->soak_targets = (uint8_t)integer;
        return 1;
    }
#undef PF
#undef PI
    return 1;
}

int ts_sister_project_state_load(TsSisterProjectState *state,
                                 const char *project_path,
                                 uint32_t sample_rate, int *present,
                                 char *error, size_t error_size)
{
    TsSisterProjectState loaded;
    char path[1200], line[512];
    FILE *file;
    int line_number = 0, header = 0, version = 0, have_pages = 0;
    if (present != NULL) *present = 0;
    if (state == NULL || !state_path(project_path, path, sizeof(path))) return 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            ts_sister_project_state_init(state, sample_rate);
            state_error(error, error_size, "");
            return 1;
        }
        state_error(error, error_size, "Could not open Sister project state");
        return 0;
    }
    ts_sister_project_state_init(&loaded, sample_rate);
    memset(loaded.page_masks, 0, sizeof(loaded.page_masks));
    while (fgets(line, sizeof(line), file) != NULL) {
        char *key = trim(line), *value;
        ++line_number;
        if (*key == '\0' || *key == ';' || *key == '#') continue;
        if (!header) {
            if (strcmp(key, "TapeSister Sister Project State") != 0) goto malformed;
            header = 1;
            continue;
        }
        value = strchr(key, '=');
        if (value == NULL) goto malformed;
        *value++ = '\0'; key = trim(key); value = trim(value);
        if (strcmp(key, "Version") == 0) {
            int parsed;
            if (!parse_int_value(value, &parsed) || parsed < 1) goto malformed;
            version = 1;
        } else if (strcmp(key, "PageCount") == 0) {
            if (!parse_size_value(value, &loaded.page_count) || loaded.page_count == 0u ||
                loaded.page_count > TS_SISTER_RUNTIME_PAGE_LIMIT) goto malformed;
            have_pages = 1;
        } else if (strcmp(key, "ActivePage") == 0) {
            if (!parse_size_value(value, &loaded.active_page)) goto malformed;
        } else if (strcmp(key, "Routes") == 0) {
            int parsed;
            if (!parse_int_value(value, &parsed) || parsed < 0) goto malformed;
            loaded.source_switches = (uint8_t)parsed & TS_SISTER_SOURCE_ALL;
        } else if (strcmp(key, "SelectedPreset") == 0) {
            if (strlen(value) > TS_SISTER_PROJECT_PRESET_NAME_MAX) goto malformed;
            snprintf(loaded.selected_preset, sizeof(loaded.selected_preset), "%s", value);
        } else if (strncmp(key, "Mask.", 5u) == 0) {
            size_t page;
            char *end;
            unsigned long parsed_mask;
            if (!parse_size_value(key + 5, &page) || page >= TS_SISTER_RUNTIME_PAGE_LIMIT) goto malformed;
            errno = 0; parsed_mask = strtoul(value, &end, 16);
            if (errno != 0 || end == value || *trim(end) != '\0' || parsed_mask > 0xffffu) goto malformed;
            loaded.page_masks[page] = (uint16_t)parsed_mask;
        } else if (!assign_parameter(&loaded.parameters, key, value)) goto malformed;
    }
    if (ferror(file) || !header || !version || !have_pages ||
        loaded.active_page >= loaded.page_count) goto malformed;
    fclose(file);
    ts_sister_parameters_sanitize(&loaded.parameters, sample_rate);
    *state = loaded;
    if (present != NULL) *present = 1;
    state_error(error, error_size, "");
    return 1;
malformed:
    fclose(file);
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size,
                 "Malformed Sister project state at line %d", line_number);
    return 0;
}
