Warning: truncated output (original token count: 114120)
Total output lines: 9201

#include "tapesister/sample.h"
#include "tapesister/capture.h"
#include "tapesister/capture_archive.h"
#include "tapesister/input_monitor.h"
#include "tapesister/note_bank.h"
#include "tapesister/sample_pages.h"
#include "tapesister/dsp_transform.h"
#include "tapesister/render_damage.h"
#include "tapesister/ui.h"

#include <SDL2/SDL.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

enum { TS_SPLASH_MILLISECONDS = 5000 };

typedef struct {
    TsInstrument before;
    TsInstrument after;
    uint64_t before_hash;
    uint64_t after_hash;
    size_t page_before;
    size_t page_count_before;
    int valid;
    int applied;
    int added_page;
} FmBankHistory;

static SDL_Texture *load_splash_texture(SDL_Renderer *renderer, int *width, int *height)
{
    static const char relative_path[] = "assets/tapesister_splash.png";
    SDL_Texture *texture = NULL;
    SDL_Surface *surface = NULL;
    unsigned char *pixels = NULL;
    char *base = SDL_GetBasePath();
    char path[1024];
    int channels = 0;

    if (base != NULL) {
        int written = snprintf(path, sizeof(path), "%s%s", base, relative_path);
        if (written > 0 && (size_t)written < sizeof(path))
            pixels = stbi_load(path, width, height, &channels, STBI_rgb_alpha);
        SDL_free(base);
    }
    if (pixels == NULL)
        pixels = stbi_load(relative_path, width, height, &channels, STBI_rgb_alpha);
    if (pixels == NULL) {
        fprintf(stderr, "TapeSister splash: %s\n", stbi_failure_reason());
        return NULL;
    }

    surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, *width, *height, 32, *width * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface != NULL) texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL)
        fprintf(stderr, "TapeSister splash texture: %s\n", SDL_GetError());
    if (surface != NULL) SDL_FreeSurface(surface);
    stbi_image_free(pixels);
    return texture;
}

static int show_splash(SDL_Renderer *renderer)
{
    SDL_Texture *splash;
    int image_width = 0;
    int image_height = 0;
    Uint32 started;
    int showing = 1;

    splash = load_splash_texture(renderer, &image_width, &image_height);
    if (splash == NULL) return 1;
    started = SDL_GetTicks();
    while (showing && SDL_GetTicks() - started < TS_SPLASH_MILLISECONDS) {
        SDL_Event event;
        int output_width;
        int output_height;
        SDL_Rect destination;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                SDL_DestroyTexture(splash);
                return 0;
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN)
                showing = 0;
        }
        SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
        destination.w = output_width;
        destination.h = image_width > 0 ? output_width * image_height / image_width : 0;
        if (destination.h > output_height) {
            destination.h = output_height;
            destination.w = image_height > 0 ? output_height * image_width / image_height : 0;
        }
        destination.x = (output_width - destination.w) / 2;
        destination.y = (output_height - destination.h) / 2;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, splash, NULL, &destination);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
    SDL_DestroyTexture(splash);
    return 1;
}

static int runtime_asset_path(const char *relative_path, char *path, size_t path_size)
{
    char *base = SDL_GetBasePath();
    struct stat info;
    if (base != NULL) {
        int written = snprintf(path, path_size, "%s%s", base, relative_path);
        SDL_free(base);
        if (written > 0 && (size_t)written < path_size && stat(path, &info) == 0)
            return 1;
    }
    if (snprintf(path, path_size, "%s", relative_path) > 0 &&
        stat(path, &info) == 0) return 1;
    return 0;
}

static void diagnostic_log(const char *format, ...)
{
    FILE *file = fopen("tapesister-diagnostic.log", "ab");
    va_list arguments;
    if (file == NULL) return;
    fprintf(file, "[runtime] ");
    va_start(arguments, format);
    vfprintf(file, format, arguments);
    va_end(arguments);
    fputc('\n', file);
    fflush(file);
    fclose(file);
}

static int update_texture_damage(SDL_Texture *texture,
                                 const TsFramebuffer *framebuffer,
                                 uint32_t *snapshot,
                                 int *snapshot_valid)
{
    TsRenderDamagePlan damage;
    const uint32_t *previous;
    if (texture == NULL || framebuffer == NULL || snapshot == NULL ||
        snapshot_valid == NULL)
        return 0;
    previous = *snapshot_valid ? snapshot : NULL;
    if (!ts_render_damage_plan(framebuffer->pixels, previous,
                               TS_UI_WIDTH, TS_UI_HEIGHT, &damage))
        return 1;

    for (int i = 0; i < damage.count; ++i) {
        const TsRenderDamageRect *rect = &damage.rects[i];
        SDL_Rect target = {rect->x, rect->y, rect->w, rect->h};
        const uint32_t *pixels = framebuffer->pixels +
            (size_t)rect->y * TS_UI_WIDTH + (size_t)rect->x;
        if (SDL_UpdateTexture(texture, &target, pixels,
                              TS_UI_WIDTH * (int)sizeof(*pixels)) != 0) {
            *snapshot_valid = 0;
            return 0;
        }
    }
    ts_render_damage_snapshot_commit(snapshot, framebuffer->pixels,
                                     TS_UI_WIDTH, &damage);
    *snapshot_valid = 1;
    return 1;
}

static void pace_frame_60hz(Uint64 started)
{
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 elapsed = SDL_GetPerformanceCounter() - started;
    Uint64 target = frequency / 60u;
    if (frequency == 0u || elapsed >= target) return;
    {
        Uint64 remaining = target - elapsed;
        Uint32 delay = (Uint32)((remaining * 1000u + frequency - 1u) /
                                frequency);
        if (delay > 0u) SDL_Delay(delay);
    }
}

static const char *config_file_path(void)
{
    const char *override = getenv("TAPESISTER_CONFIG");
    return override != NULL && override[0] != '\0' ? override : "tapesister.ini";
}

static const char *tapesister_palette_path(void)
{
    const char *override = getenv("TAPESISTER_PALETTE");
    return override != NULL && override[0] != '\0' ? override : "tapesister.pal";
}

static const char *tapehead_palette_path(void)
{
    return "tapehead.pal";
}

static const char *capture_archive_directory(void)
{
    const char *override = getenv("TAPESISTER_CAPTURES");
    return override != NULL && override[0] != '\0' ? override : "Captures";
}

static int path_is_directory(const char *path)
{
#ifdef _WIN32
    struct _stat info;
    return path != NULL && path[0] != '\0' && _stat(path, &info) == 0 &&
           (info.st_mode & _S_IFDIR) != 0;
#else
    struct stat info;
    return path != NULL && path[0] != '\0' && stat(path, &info) == 0 &&
           S_ISDIR(info.st_mode);
#endif
}

static int parent_directory_of(const char *path, char *directory, size_t directory_size)
{
    char *slash;
    char *backslash;
    if (path == NULL || path[0] == '\0' || directory == NULL || directory_size == 0)
        return 0;
    if (snprintf(directory, directory_size, "%s", path) < 0 ||
        strlen(path) >= directory_size) return 0;
    slash = strrchr(directory, '/');
    backslash = strrchr(directory, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash == NULL) return 0;
    if (slash == directory) slash[1] = '\0';
    else if (slash == directory + 2 && directory[1] == ':') slash[1] = '\0';
    else *slash = '\0';
    return path_is_directory(directory);
}

static void state_hash_bytes(uint64_t *hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= bytes[i];
        *hash *= 1099511628211ull;
    }
}

static void state_hash_sample(uint64_t *hash, const TsSample *sample)
{
    uint64_t waveform = ts_sample_hash(sample);
    size_t name_length = strlen(sample->name);
    state_hash_bytes(hash, &waveform, sizeof(waveform));
    state_hash_bytes(hash, &name_length, sizeof(name_length));
    state_hash_bytes(hash, sample->name, name_length);
}

static uint64_t instrument_state_hash(const TsInstrument *instrument)
{
    uint64_t hash = 1469598103934665603ull;
    state_hash_sample(&hash, &instrument->parent);
    state_hash_sample(&hash, &instrument->current);
    state_hash_bytes(&hash, &instrument->source_kind, sizeof(instrument->source_kind));
    state_hash_bytes(&hash, &instrument->generator, sizeof(instrument->generator));
    state_hash_bytes(&hash, &instrument->process, sizeof(instrument->process));
    state_hash_bytes(&hash, &instrument->family_relation,
                     sizeof(instrument->family_relation));
    state_hash_bytes(&hash, &instrument->family_mutation,
                     sizeof(instrument->family_mutation));
    state_hash_bytes(&hash, &instrument->family_locks, sizeof(instrument->family_locks));
    state_hash_bytes(&hash, &instrument->family_sequence,
                     sizeof(instrument->family_sequence));
    state_hash_bytes(&hash, &instrument->family_trajectory,
                     sizeof(instrument->family_trajectory));
    state_hash_bytes(&hash, &instrument->family_anchor_slot,
                     sizeof(instrument->family_anchor_slot));
    state_hash_bytes(&hash, &instrument->family_last_slot,
                     sizeof(instrument->family_last_slot));
    state_hash_bytes(&hash, &instrument->generation, sizeof(instrument->generation));
    state_hash_bytes(&hash, &instrument->ancestor_hash, sizeof(instrument->ancestor_hash));
    state_hash_bytes(&hash, &instrument->tuning, sizeof(instrument->tuning));
    state_hash_bytes(&hash, &instrument->audible_tuning,
                     sizeof(instrument->audible_tuning));
    state_hash_bytes(&hash, &instrument->crop_first, sizeof(instrument->crop_first));
    state_hash_bytes(&hash, &instrument->crop_last, sizeof(instrument->crop_last));
    state_hash_bytes(&hash, &instrument->loop_first, sizeof(instrument->loop_first));
    state_hash_bytes(&hash, &instrument->loop_last, sizeof(instrument->loop_last));
    state_hash_bytes(&hash, &instrument->loop_crossfade_ms,
                     sizeof(instrument->loop_crossfade_ms));
    state_hash_bytes(&hash, &instrument->loop_mode, sizeof(instrument->loop_mode));
    state_hash_bytes(&hash, &instrument->has_loop, sizeof(instrument->has_loop));
    state_hash_bytes(&hash, &instrument->grid_divisions,
                     sizeof(instrument->grid_divisions));
    state_hash_bytes(&hash, &instrument->grid_snap,
                     sizeof(instrument->grid_snap));
    state_hash_bytes(&hash, &instrument->sample_edit_count,
                     sizeof(instrument->sample_edit_count));
    state_hash_bytes(&hash, instrument->sample_edits,
                     (size_t)instrument->sample_edit_count * sizeof(instrument->sample_edits[0]));
    state_hash_bytes(&hash, &instrument->post_edit_count,
                     sizeof(instrument->post_edit_count));
    state_hash_bytes(&hash, instrument->post_edits,
                     (size_t)instrument->post_edit_count * sizeof(instrument->post_edits[0]));
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        const TsBankSlot *bank = &instrument->bank[slot];
        state_hash_bytes(&hash, &bank->occupied, sizeof(bank->occupied));
        if (!bank->occupied) continue;
        state_hash_bytes(&hash, &bank->locked, sizeof(bank->locked));
        state_hash_sample(&hash, &bank->sample);
        state_hash_bytes(&hash, &bank->tuning, sizeof(bank->tuning));
        state_hash_bytes(&hash, &bank->audible_tuning, sizeof(bank->audible_tuning));
        state_hash_bytes(&hash, &bank->loop_first, sizeof(bank->loop_first));
        state_hash_bytes(&hash, &bank->loop_last, sizeof(bank->loop_last));
        state_hash_bytes(&hash, &bank->loop_crossfade_ms, sizeof(bank->loop_crossfade_ms));
        state_hash_bytes(&hash, &bank->capture_kind, sizeof(bank->capture_kind));
        state_hash_bytes(&hash, &bank->relation, sizeof(bank->relation));
        state_hash_bytes(&hash, &bank->generator, sizeof(bank->generator));
        state_hash_bytes(&hash, &bank->lineage_seed, sizeof(bank->lineage_seed));
        state_hash_bytes(&hash, &bank->lineage_locks, sizeof(bank->lineage_locks));
        state_hash_bytes(&hash, &bank->trajectory_step, sizeof(bank->trajectory_step));
        state_hash_bytes(&hash, &bank->lineage_mutation, sizeof(bank->lineage_mutation));
        state_hash_bytes(&hash, &bank->parent_slot, sizeof(bank->parent_slot));
        state_hash_bytes(&hash, &bank->has_generator, sizeof(bank->has_generator));
        state_hash_bytes(&hash, &bank->loop_mode, sizeof(bank->loop_mode));
        state_hash_bytes(&hash, &bank->has_loop, sizeof(bank->has_loop));
        state_hash_bytes(&hash, &bank->edit.grid_divisions,
                         sizeof(bank->edit.grid_divisions));
        state_hash_bytes(&hash, &bank->edit.grid_snap,
                         sizeof(bank->edit.grid_snap));
    }
    return hash;
}

static void fm_bank_history_init(FmBankHistory *history)
{
    if (history == NULL) return;
    memset(history, 0, sizeof(*history));
    ts_instrument_init(&history->before);
    ts_instrument_init(&history->after);
}

static void fm_bank_history_free(FmBankHistory *history)
{
    if (history == NULL) return;
    ts_instrument_free(&history->before);
    ts_instrument_free(&history->after);
    memset(history, 0, sizeof(*history));
}

/* Returns 1 when the bank transaction moved, 0 on a matching failure, and -1
   when ordinary tile history should handle the request. */
static int fm_bank_history_move(FmBankHistory *history,
                                TsSamplePages *pages,
                                TsInstrument *instrument,
                                int redo,
                                char *error, size_t error_size)
{
    size_t made_page = 0u;
    if (history == NULL || pages == NULL || instrument == NULL ||
        !history->valid) return -1;
    if (!redo) {
        if (!history->applied ||
            ts_sample_pages_active(pages) !=
                (history->added_page ? history->page_count_before :
                                       history->page_before) ||
            instrument_state_hash(instrument) != history->after_hash)
            return -1;
        if (history->added_page) {
            if (!ts_sample_pages_remove_last_and_switch(
                    pages, instrument, history->page_before,
                    error, error_size)) return 0;
        } else if (!ts_instrument_clone(instrument, &history->before,
                                        error, error_size)) return 0;
        history->applied = 0;
        return 1;
    }
    if (history->applied ||
        ts_sample_pages_active(pages) != history->page_before ||
        ts_sample_pages_count(pages) != history->page_count_before ||
        instrument_state_hash(instrument) != history->before_hash)
        return -1;
    if (history->added_page) {
        if (!ts_sample_pages_append_and_switch(
                pages, instrument, &made_page, error, error_size)) return 0;
        if (made_page != history->page_count_before ||
            !ts_instrument_clone(instrument, &history->after,
                                 error, error_size)) {
            char ignored[80];
            (void)ts_sample_pages_remove_last_and_switch(
                pages, instrument, history->page_before,
                ignored, sizeof(ignored));
            if (made_page != history->page_count_before)
                snprintf(error, error_size, "Could not restore FM Bank Maker page");
            return 0;
        }
    } else if (!ts_instrument_clone(instrument, &history->after,
                                    error, error_size)) return 0;
    history->applied = 1;
    return 1;
}

static uint64_t paged_project_state_hash(const TsSamplePages *pages,
                                         const TsInstrument *active_sample,
                                         const TsInstrument *record_bank)
{
    uint64_t hash = 1469598103934665603ull;
    size_t count = ts_sample_pages_count(pages);
    size_t active = ts_sample_pages_active(pages);
    state_hash_bytes(&hash, &count, sizeof(count));
    state_hash_bytes(&hash, &active, sizeof(active));
    for (size_t page = 0; page < count; ++page) {
        const TsInstrument *instrument = ts_sample_pages_page(
            pages, active_sample, page);
        uint64_t page_hash = instrument != NULL ?
                             instrument_state_hash(instrument) : 0u;
        state_hash_bytes(&hash, &page_hash, sizeof(page_hash));
    }
    {
        uint64_t record_hash = record_bank != NULL ?
                               instrument_state_hash(record_bank) : 0u;
        state_hash_bytes(&hash, &record_hash, sizeof(record_hash));
    }
    return hash;
}

static uint64_t runtime_project_state_hash(const TsSamplePages *pages,
                                           const TsInstrument *instrument,
                                           const TsInstrument *parked_record,
                                           int record_bank_active)
{
    return paged_project_state_hash(
        pages, record_bank_active ? NULL : instrument,
        record_bank_active ? instrument : parked_record);
}

static int launch_program(const char *path, char *error, size_t error_size)
{
    if (path == NULL || path[0] == '\0') {
        snprintf(error, error_size, "FastTracker executable path is blank");
        return 0;
    }
#ifdef _WIN32
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        snprintf(error, error_size, "FastTracker executable was not found");
        return 0;
    }
#else
    if (access(path, X_OK) != 0) {
        snprintf(error, error_size, "FastTracker executable is not runnable: %s",
                 strerror(errno));
        return 0;
    }
#endif
#ifdef _WIN32
    {
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        char command[TS_CONFIG_PATH_MAX + 4];
        int written = snprintf(command, sizeof(command), "\"%s\"", path);
        if (written < 0 || (size_t)written >= sizeof(command)) {
            snprintf(error, error_size, "FastTracker executable path is too long");
            return 0;
        }
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        if (!CreateProcessA(path, command, NULL, NULL, FALSE, 0,
                            NULL, NULL, &startup, &process)) {
            snprintf(error, error_size, "Could not launch FastTracker (Windows error %lu)",
                     (unsigned long)GetLastError());
            return 0;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#else
    {
        pid_t first = fork();
        int status = 0;
        if (first < 0) {
            snprintf(error, error_size, "Could not launch FastTracker: %s", strerror(errno));
            return 0;
        }
        if (first == 0) {
            pid_t second = fork();
            if (second < 0) _exit(126);
            if (second > 0) _exit(0);
            setsid();
            execl(path, path, (char *)NULL);
            _exit(127);
        }
        if (waitpid(first, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            snprintf(error, error_size, "Could not start FastTracker launch process");
            return 0;
        }
    }
#endif
    error[0] = '\0';
    return 1;
}

typedef struct {
    const TsSample *sample;
    double position;
    double step;
    double pitch;
    size_t range_start;
    size_t range_end;
    TsAuditionSource source;
    TsAuditionRange range;
    size_t crossfade_frames;
    TsLoopMode loop_mode;
    int looping;
    int loop_direction;
    int playing;
    int output_rate;
    int bank_slot;
    TsNoteBank notes;
    TsCaptureRecorder capture;
    TsInputMonitor *input_monitor;
    TsExternalRecorder *record_bank_recorder;
    _Atomic int *record_source;
    double tune_reference_phase;
    double tune_reference_frequency;
    float tune_reference_level;
    float tune_reference_target;
    int tune_reference_enabled;
} AudioState;

typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t cancel;
    SDL_atomic_t done;
    TsTransformBackend backend;
    TsCdpRuntime runtime;
    const TsCdpRecipe *recipe;
    TsCdpRecipeValues values;
    TsTransformIdentity identity;
    TsSample input;
    TsCdpRunOptions options;
    TsCdpRunResult result;
    TsDspTransformIdentity dsp_identity;
    const TsDspRecipe *dsp_recipe;
    TsDspRecipeValues dsp_values;
    TsSample dsp_output;
    TsCdpSafetyStatus dsp_safety;
    float dsp_peak;
    double dsp_dc_offset;
    int dsp_clipped_samples;
    int dsp_ok;
    char error[160];
} TransformWorker;

typedef struct {
    TsCdpRuntime runtime;
    TransformWorker *worker;
    TsTransformPreview preview;
    TsDspTransformPreview dsp_preview;
    uint64_t render_generation;
    uint64_t next_job_id;
    int rerender_requested;
    int quick_apply;
} TransformController;

static float loop_lock_silence_data[TS_DEFAULT_CANVAS_FRAMES];
static const TsSample loop_lock_silence = {
    loop_lock_silence_data,
    TS_DEFAULT_CANVAS_FRAMES,
    TS_DEFAULT_CANVAS_RATE,
    "LOOP LOCK SILENCE",
    0u
};

static int path_is_tsr(const char *path)
{
    const char *extension = path != NULL ? strrchr(path, '.') : NULL;
    return extension != NULL && strlen(extension) == 4u &&
           tolower((unsigned char)extension[1]) == 't' &&
           tolower((unsigned char)extension[2]) == 's' &&
           tolower((unsigned char)extension[3]) == 'r';
}

static int path_is_tsp(const char *path)
{
    const char *extension = path != NULL ? strrchr(path, '.') : NULL;
    return extension != NULL && strlen(extension) == 4u &&
           tolower((unsigned char)extension[1]) == 't' &&
           tolower((unsigned char)extension[2]) == 's' &&
           tolower((unsigned char)extension[3]) == 'p';
}

static void audio_callback(void *userdata, Uint8 *stream, int bytes)
{
    AudioState *audio = (AudioState *)userdata;
    float *out = (float *)stream;
    int values = bytes / (int)sizeof(float);
    float synth_block_peak = 0.0f;
    for (int i = 0; i < values; i += 2) {
        float value = 0.0f;
        float synth_value = 0.0f;
        float reference_value = 0.0f;
        if (audio->playing && audio->sample && audio->sample->data && audio->sample->frames > 1) {
            if (audio->looping) {
                audio->position = ts_audition_loop_position(
                    audio->position, audio->range_start, audio->range_end,
                    audio->crossfade_frames, audio->loop_mode,
                    &audio->loop_direction);
                value = ts_audition_read_looped_mode(
                    audio->sample, audio->position, audio->range_start,
                    audio->range_end, audio->crossfade_frames, audio->loop_mode);
                audio->position += audio->step * audio->loop_direction;
            } else {
                size_t at = (size_t)audio->position;
                if (at + 1 < audio->range_end && at + 1 < audio->sample->frames) {
                    float fraction = (float)(audio->position - (double)at);
                    value = audio->sample->data[at] +
                            (audio->sample->data[at + 1] - audio->sample->data[at]) * fraction;
                    audio->position += audio->step;
                } else {
                    audio->playing = 0;
                }
            }
        }
        value += ts_note_bank_read_split(&audio->notes, &synth_value);
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        if (ts_capture_write_sample(&audio->capture, value)) {
            audio->playing = 0;
            audio->bank_slot = -1;
            ts_note_bank_clear(&audio->notes);
        }
        if (audio->record_bank_recorder != NULL && audio->record_source != NULL &&
            atomic_load_explicit(audio->record_source, memory_order_acquire) ==
                TS_RECORD_SOURCE_SYNTH) {
            (void)ts_external_recorder_write_sample(audio->record_bank_recorder,
                                                    synth_value);
            if (fabsf(synth_value) > synth_block_peak)
                synth_block_peak = fabsf(synth_value);
        }
        {
            float ramp = audio->output_rate > 0 ?
                         1.0f / ((float)audio->output_rate * 0.01f) : 1.0f;
            if (audio->tune_reference_level < audio->tune_reference_target) {
                audio->tune_reference_level += ramp;
                if (audio->tune_reference_level > audio->tune_reference_target)
                    audio->tune_reference_level = audio->tune_reference_target;
            } else if (audio->tune_reference_level > audio->tune_reference_target) {
                audio->tune_reference_level -= ramp;
                if (audio->tune_reference_level < audio->tune_reference_target)
                    audio->tune_reference_level = audio->tune_reference_target;
            }
            if (audio->tune_reference_level > 0.0f && audio->output_rate > 0) {
                reference_value = audio->tune_reference_level *
                                  sinf((float)audio->tune_reference_phase);
                audio->tune_reference_phase +=
                    2.0 * M_PI * audio->tune_reference_frequency /
                    (double)audio->output_rate;
                while (audio->tune_reference_phase >= 2.0 * M_PI)
                    audio->tune_reference_phase -= 2.0 * M_PI;
            }
        }
        {
            float monitored = audio->input_monitor != NULL ?
                              ts_input_monitor_read(audio->input_monitor,
                                                    (uint32_t)audio->output_rate) : 0.0f;
            float output = value * 0.8f + monitored + reference_value;
            if (output > 1.0f) output = 1.0f;
            if (output < -1.0f) output = -1.0f;
            out[i] = output;
            if (i + 1 < values) out[i + 1] = output;
        }
    }
    if (audio->input_monitor != NULL && audio->record_source != NULL &&
        atomic_load_explicit(audio->record_source, memory_order_acquire) ==
            TS_RECORD_SOURCE_SYNTH)
        ts_input_monitor_publish_level(audio->input_monitor, synth_block_peak);
}

static int audition_plan_ui(const TsInstrument *instrument, const TsUiState *ui,
                            TsAuditionSource source, TsAuditionRange range,
                            TsAuditionPlan *plan)
{
    if (source == TS_AUDITION_PARENT && range == TS_AUDITION_DISPLAYED) {
        size_t first = ui->parent_view_first;
        size_t last = ui->parent_view_last;
        if (last <= first || last > instrument->parent.frames) {
            first = 0;
            last = instrument->parent.frames;
        }
        if (instrument->parent.data == NULL || last <= first) return 0;
        plan->sample = &instrument->parent;
        plan->first = first;
        plan->last = last;
        return 1;
    }
    return ts_audition_plan(instrument, source, range, plan);
}

static int note_for_key(SDL_Keycode key)
{
    const SDL_Keycode keys[] = {
        SDLK_z, SDLK_s, SDLK_x, SDLK_d, SDLK_c, SDLK_v, SDLK_g, SDLK_b, SDLK_h, SDLK_n, SDLK_j, SDLK_m,
        SDLK_q, SDLK_2, SDLK_w, SDLK_3, SDLK_e, SDLK_r, SDLK_5, SDLK_t, SDLK_6, SDLK_y, SDLK_7, SDLK_u
    };
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i)
        if (key == keys[i]) return i;
    return -1;
}

static void show_overlay(TsUiState *ui, const char *message, uint32_t milliseconds)
{
    if (ui == NULL) return;
    snprintf(ui->overlay, sizeof(ui->overlay), "%s", message != NULL ? message : "");
    ui->overlay_until_ms = SDL_GetTicks() + milliseconds;
}

static void begin_audition(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           const TsInstrument *instrument, TsAuditionRange range,
                           double pitch, int output_rate)
{
    TsAuditionPlan plan;
    int capture_started = 0;
    ui->bank_view_slot = -1;
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "AUDIO UNAVAILABLE");
        return;
    }
    if (!audition_plan_ui(instrument, ui, ui->audition_source, range, &plan)) {
        snprintf(ui->status, sizeof(ui->status),
                 range == TS_AUDITION_SELECTION ? "SELECT A RANGE FIRST" :
                 range == TS_AUDITION_LOOP ? "SET A LOOP FIRST" :
                 "NOTHING TO AUDITION");
        return;
    }
    pitch *= ts_instrument_audition_pitch(instrument);
    SDL_LockAudioDevice(device);
    ts_note_bank_clear(&audio->notes);
    audio->bank_slot = -1;
    audio->sample = plan.sample;
    audio->loop_mode = instrument->loop_mode;
    audio->loop_direction = audio->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
    audio->position = range == TS_AUDITION_LOOP && audio->loop_direction < 0 ?
                      (double)(plan.last - 1u) : (double)plan.first;
    audio->pitch = pitch;
    audio->range_start = plan.first;
    audio->range_end = plan.last;
    audio->source = ui->audition_source;
    audio->range = range;
    audio->looping = range == TS_AUDITION_LOOP ||
                     range == TS_AUDITION_WORKBENCH_LOOP;
    audio->crossfade_frames = audio->looping ?
                              ts_audition_crossfade_frames(
                                  &plan, range == TS_AUDITION_WORKBENCH_LOOP ?
                                  2.0f : instrument->loop_crossfade_ms) : 0;
    audio->step = ((double)plan.sample->sample_rate / output_rate) * pitch;
    audio->playing = 1;
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
        audio->capture.source_slot == instrument->selected_slot)
        capture_started = ts_capture_trigger(&audio->capture, NULL, 0);
    SDL_UnlockAudioDevice(device);
    if (capture_started) {
        show_overlay(ui, "CAPTURE STARTED", 650u);
        snprintf(ui->status, sizeof(ui->status),
                 "CAPTURE RECORDING TILE %02d FROM %02d",
                 audio->capture.destination_slot + 1,
                 audio->capture.source_slot + 1);
    } else
        snprintf(ui->status, sizeof(ui->status), "PLAYING %s %s",
                 ts_audition_source_name(ui->audition_source),
                 ts_audition_range_name(range));
}

static void begin_playhead_audition(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, const TsInstrument *instrument,
                                    int output_rate)
{
    int capture_started = 0;
    if (instrument == NULL || !instrument->has_playhead ||
        instrument->current.data == NULL ||
        instrument->playhead_frame >= instrument->current.frames) {
        snprintf(ui->status, sizeof(ui->status), "PLACE A PLAYHEAD FIRST");
        return;
    }
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "PLAYHEAD PLACED - AUDIO UNAVAILABLE");
        return;
    }
    SDL_LockAudioDevice(device);
    ts_note_bank_clear(&audio->notes);
    audio->sample = &instrument->current;
    audio->position = (double)instrument->playhead_frame;
    audio->pitch = ts_instrument_audition_pitch(instrument);
    audio->range_start = instrument->playhead_frame;
    audio->range_end = instrument->current.frames;
    audio->source = TS_AUDITION_CURRENT;
    audio->range = TS_AUDITION_ALL;
    audio->looping = 0;
    audio->loop_mode = TS_LOOP_FORWARD;
    audio->loop_direction = 1;
    audio->crossfade_frames = 0;
    audio->bank_slot = -1;
    audio->step = (double)instrument->current.sample_rate / (double)output_rate *
                  audio->pitch;
    audio->playing = 1;
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
        audio->capture.source_slot == instrument->selected_slot)
        capture_started = ts_capture_trigger(&audio->capture, NULL, 0);
    SDL_UnlockAudioDevice(device);
    if (capture_started) {
        show_overlay(ui, "CAPTURE STARTED", 650u);
        snprintf(ui->status, sizeof(ui->status),
                 "CAPTURE RECORDING FROM PLAYHEAD %zu", instrument->playhead_frame);
    } else
        snprintf(ui->status, sizeof(ui->status), "PLAYING FROM PLAYHEAD %zu",
                 instrument->playhead_frame);
}

static void stop_all(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui);
static void stop_all_force(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui);

static void set_loop_lock_silence(SDL_AudioDeviceID device, AudioState *audio)
{
    if (device) SDL_LockAudioDevice(device);
    audio->sample = &loop_lock_silence;
    audio->position = ts_audition_map_progress(
        audio->position, audio->range_start, audio->range_end,
        0, loop_lock_silence.frames);
    if (audio->position >= (double)loop_lock_silence.frames)
        audio->position = 0.0;
    audio->pitch = 1.0;
    audio->range_start = 0;
    audio->range_end = loop_lock_silence.frames;
    audio->source = TS_AUDITION_CURRENT;
    audio->range = TS_AUDITION_WORKBENCH_LOOP;
    audio->looping = 1;
    audio->loop_mode = TS_LOOP_FORWARD;
    audio->loop_direction = 1;
    audio->crossfade_frames = 0;
    audio->bank_slot = -1;
    audio->step = audio->output_rate > 0 ?
                  (double)loop_lock_silence.sample_rate / audio->output_rate : 1.0;
    audio->playing = 1;
    if (device) SDL_UnlockAudioDevice(device);
}

static void toggle_workbench_loop(SDL_AudioDeviceID device, AudioState *audio,
                                  TsUiState *ui, const TsInstrument *instrument,
                                  int output_rate, int persistent)
{
    TsUiLoopCommand command = ts_ui_loop_command(ui, persistent);
    if (command == TS_UI_LOOP_LOCKED) {
        snprintf(ui->status, sizeof(ui->status),
                 "LOOP LOCKED - SHIFT+LOOP TO RELEASE");
        return;
    }
    if (command == TS_UI_LOOP_LOCK_RELEASE) {
        stop_all_force(device, audio, ui);
        snprintf(ui->status, sizeof(ui->status), "LOOP LOCK RELEASED");
        return;
    }
    if (!ts_ui_transform_auto_audition_allowed(ui)) {
        if (command == TS_UI_LOOP_LOCK_START) stop_all_force(device, audio, ui);
        else {
            stop_all(device, audio, ui);
            return;
        }
    }
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->workbench_loop_active = 1;
    ui->workbench_loop_persistent = command == TS_UI_LOOP_LOCK_START;
    begin_audition(device, audio, ui, instrument,
                   TS_AUDITION_WORKBENCH_LOOP, 1.0, output_rate);
    if (!audio->playing) {
        if (ui->workbench_loop_persistent) {
            set_loop_lock_silence(device, audio);
            snprintf(ui->status, sizeof(ui->status),
                     "LOOP LOCKED: SILENT TILE VIEW");
        } else ui->workbench_loop_active = 0;
    } else if (audio->capture.state == TS_CAPTURE_RECORDING)
        snprintf(ui->status, sizeof(ui->status),
                 "CAPTURE RECORDING %s LOOP TO TILE %02d",
                 instrument->has_selection ? "SELECTION" : "VIEW",
                 audio->capture.destination_slot + 1);
    else snprintf(ui->status, sizeof(ui->status), "%s: %s",
                  ui->workbench_loop_persistent ? "LOOP LOCKED" : "LOOP",
                  instrument->has_selection ? "SELECTION" : "VIEW");
}

static void refresh_workbench_loop(SDL_AudioDeviceID device, AudioState *audio,
                                   TsUiState *ui, const TsInstrument *instrument)
{
    TsAuditionPlan plan;
    if (!ui->workbench_loop_active) return;
    if (!audition_plan_ui(instrument, ui, TS_AUDITION_CURRENT,
                          TS_AUDITION_WORKBENCH_LOOP, &plan)) {
        if (ui->workbench_loop_persistent) set_loop_lock_silence(device, audio);
        else stop_all(device, audio, ui);
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    audio->position = ts_audition_map_progress(
        audio->position, audio->range_start, audio->range_end, plan.first, plan.last);
    if (audio->position >= (double)plan.last) audio->position = (double)plan.first;
    audio->sample = plan.sample;
    audio->range_start = plan.first;
    audio->range_end = plan.last;
    audio->range = TS_AUDITION_WORKBENCH_LOOP;
    audio->looping = audio->playing = 1;
    audio->loop_mode = TS_LOOP_FORWARD;
    audio->loop_direction = 1;
    audio->crossfade_frames = ts_audition_crossfade_frames(&plan, 2.0f);
    audio->pitch = ts_instrument_audition_pitch(instrument);
    if (audio->output_rate > 0)
        audio->step = (double)plan.sample->sample_rate / audio->output_rate *
                      audio->pitch;
    if (device) SDL_UnlockAudioDevice(device);
}

static void begin_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                       const TsInstrument *instrument, int note, int output_rate, int latched)
{
    TsNoteStartResult result;
    int voice_count;
    int capture_started = 0;
    ui->bank_view_slot = -1;
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "AUDIO UNAVAILABLE");
        return;
    }
    SDL_LockAudioDevice(device);
    if (audio->capture.state != TS_CAPTURE_RECORDING)
        audio->playing = 0;
    audio->bank_slot = -1;
    result = ts_note_bank_start_tuned_at(
        &audio->notes, instrument, ts_ui_audition_tuning(ui, instrument),
        ui->audition_source, note, ts_ui_keyboard_base_note(ui),
        latched, output_rate);
    if (result == TS_NOTE_STARTED &&
        audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
        audio->capture.source_slot == instrument->selected_slot)
        capture_started = ts_capture_trigger(&audio->capture, NULL, 0);
    voice_count = ts_note_bank_count(&audio->notes);
    SDL_UnlockAudioDevice(device);
    if (result == TS_NOTE_LIMIT_REACHED)
        snprintf(ui->status, sizeof(ui->status), "CHORD LIMIT %d NOTES", TS_NOTE_VOICE_LIMIT);
    else if (result == TS_NOTE_TOGGLED_OFF)
        snprintf(ui->status, sizeof(ui->status), "CHORD NOTE REMOVED");
    else if (result == TS_NOTE_STARTED && latched)
        snprintf(ui->status, sizeof(ui->status), "CHORD %d/%d - SHIFT+CLICK TO TOGGLE",
                 voice_count, TS_NOTE_VOICE_LIMIT);
    else if (capture_started) {
        show_overlay(ui, "CAPTURE STARTED", 650u);
        snprintf(ui->status, sizeof(ui->status),
                 "CAPTURE RECORDING LIVE NOTES TO TILE %02d",
                 audio->capture.destination_slot + 1);
    } else if (result == TS_NOTE_STARTED)
        snprintf(ui->status, sizeof(ui->status), "PLAYING %s NOTE",
                 ts_audition_source_name(ui->audition_source));
}

static void stage_capture_note(SDL_AudioDeviceID device, AudioState *audio,
                               TsUiState *ui, int note)
{
    char error[160];
    int count;
    if (device) SDL_LockAudioDevice(device);
    if (!ts_capture_toggle_staged_note(&audio->capture, note,
                                       error, sizeof(error))) {
        if (device) SDL_UnlockAudioDevice(device);
        snprintf(ui->status, sizeof(ui->status), "STAGE FAILED: %.140s", error);
        return;
    }
    count = 0;
    for (uint32_t mask = audio->capture.staged_notes; mask != 0u; mask >>= 1u)
        count += (int)(mask & 1u);
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status),
             "STAGED CHORD %d/%d - CLICK A STAGED KEY TO LAUNCH",
             count, TS_NOTE_VOICE_LIMIT);
}

static void launch_staged_capture(SDL_AudioDeviceID device, AudioState *audio,
                                  TsUiState *ui, const TsInstrument *instrument,
                                  int note, int output_rate)
{
    uint32_t staged;
    int started = 0;
    if (!device || output_rate <= 0) return;
    SDL_LockAudioDevice(device);
    staged = audio->capture.staged_notes;
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
        (staged & (1u << note)) != 0u &&
        audio->capture.source_slot == instrument->selected_slot) {
        audio->playing = 0;
        audio->bank_slot = -1;
        started = ts_note_bank_start_staged_chord(
            &audio->notes, instrument, ts_ui_audition_tuning(ui, instrument),
            ui->audition_source, staged, ts_ui_keyboard_base_note(ui),
            output_rate);
        if (started > 0 && !ts_capture_trigger(&audio->capture, NULL, 0)) {
            ts_note_bank_clear(&audio->notes);
            started = 0;
        }
    }
    SDL_UnlockAudioDevice(device);
    if (started > 0) {
        show_overlay(ui, "CAPTURE STARTED", 650u);
        snprintf(ui->status, sizeof(ui->status),
                 "CAPTURE RECORDING SYNCHRONIZED %d-NOTE CHORD", started);
    } else
        snprintf(ui->status, sizeof(ui->status),
                 "CLICK ONE OF THE STAGED KEYS TO LAUNCH THE CHORD");
}

static void release_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, int note)
{
    if (device) SDL_LockAudioDevice(device);
    ts_note_bank_release(&audio->notes, note);
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "NOTE RELEASED");
}

static void stop_all_force(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
{
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    audio->bank_slot = -1;
    ts_note_bank_clear(&audio->notes);
    if (device) SDL_UnlockAudioDevice(device);
    ui->active_notes = 0;
    ui->mouse_note = -1;
    ui->tape_dragging = 0;
    ui->tape_drag_button = 0;
    ui->selecting = 0;
    ui->wave_pointer_pending = 0;
    ui->wave_pointer_button = 0;
    ui->dragging_loop_endpoint = 0;
    ui->loop_drag_started = 0;
    ui->workbench_loop_active = 0;
    ui->workbench_loop_persistent = 0;
    ui->drone_preview_active = 0;
    snprintf(ui->status, sizeof(ui->status),
             ui->tune_reference_active ?
             "AUDITION STOPPED - REFERENCE TONE CONTINUES" : "STOPPED");
}

static void stop_all(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
{
    if (!ts_ui_loop_transport_can_stop(ui, 0)) {
        if (device) SDL_LockAudioDevice(device);
        ts_note_bank_clear(&audio->notes);
        audio->bank_slot = -1;
        if (device) SDL_UnlockAudioDevice(device);
        ui->active_notes = 0;
        ui->mouse_note = -1;
        ui->tape_dragging = 0;
        ui->tape_drag_button = 0;
        ui->selecting = 0;
        ui->wave_pointer_pending = 0;
        ui->wave_pointer_button = 0;
        ui->dragging_loop_endpoint = 0;
        ui->loop_drag_started = 0;
        ui->drone_preview_active = 0;
        snprintf(ui->status, sizeof(ui->status),
                 "LOOP LOCKED - SHIFT+LOOP TO RELEASE");
        return;
    }
    stop_all_force(device, audio, ui);
}

static void sync_capture_ui(SDL_AudioDeviceID device, AudioState *audio,
                            TsUiState *ui)
{
    if (device) SDL_LockAudioDevice(device);
    ui->capture_state = audio->capture.state;
    ui->capture_destination_slot = audio->capture.destination_slot;
    ui->capture_source_slot = audio->capture.source_slot;
    ui->capture_recorded_frames = audio->capture.recorded_frames;
    ui->capture_capacity_frames = audio->capture.capacity_frames;
    ui->staged_notes = audio->capture.staged_notes;
    if (device) SDL_UnlockAudioDevice(device);
}

static void arm_capture(SDL_AudioDeviceID device, AudioState *audio,
                        TsUiState *ui, TsInstrument *instrument,
                        int output_rate)
{
    char error[160];
    size_t capacity = 0u;
    int destination = instrument->selected_slot;
    int ok;
    if (audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(ui->status, sizeof(ui->status),
                 audio->capture.state == TS_CAPTURE_RECORDING ?
                 "CAPTURE IS RECORDING - CLICK STOP TO KEEP IT" :
                 "CAPTURE ARMED - SELECT A SOURCE AND PERFORM  ESC CANCELS");
        return;
    }
    if (!ts_instrument_capture_target_frames(instrument, destination,
                                              (uint32_t)output_rate,
                                              &capacity, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "CAPTURE ARM FAILED: %.126s", error);
        return;
    }
    stop_all_force(device, audio, ui);
    if (device) SDL_LockAudioDevice(device);
    ok = ts_capture_arm(&audio->capture, destination, capacity,
                        (uint32_t)output_rate, error, sizeof(error));
    if (device) SDL_UnlockAudioDevice(device);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "CAPTURE ARM FAILED: %.126s", error);
        return;
    }
    sync_capture_ui(device, audio, ui);
    show_overlay(ui, "CAPTURE ARMED", 850u);
    snprintf(ui->status, sizeof(ui->status),
             "TILE %02d ARMED %.3F S - SELECT SOURCE  PLAY/LOOP/NOTE STARTS",
             destination + 1, (double)capacity / output_rate);
}

static void cancel_capture(SDL_AudioDeviceID device, AudioState *audio,
                           TsUiState *ui)
{
    int canceled;
    if (device) SDL_LockAudioDevice(device);
    canceled = ts_capture_cancel(&audio->capture);
    audio->playing = 0;
    audio->bank_slot = -1;
    ts_note_bank_clear(&audio->notes);
    if (canceled) ts_capture_free(&audio->capture);
    if (device) SDL_UnlockAudioDevice(device);
    if (!canceled) return;
    ui->workbench_loop_active = 0;
    ui->workbench_loop_persistent = 0;
    ui->active_notes = 0u;
    sync_capture_ui(device, audio, ui);
    show_overlay(ui, "CAPTURE CANCELED", 950u);
    snprintf(ui->status, sizeof(ui->status),
             "CAPTURE CANCELED - BLANK DESTINATION UNCHANGED");
}

static void stop_capture_early(SDL_AudioDeviceID device, AudioState *audio,
                               TsUiState *ui)
{
    char error[160];
    int ok;
    if (device) SDL_LockAudioDevice(device);
    ok = ts_capture_stop(&audio->capture, error, sizeof(error));
    if (ok) {
        audio->playing = 0;
        audio->bank_slot = -1;
        ts_note_bank_clear(&audio->notes);
    }
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) {
        ui->workbench_loop_active = 0;
        ui->workbench_loop_persistent = 0;
        snprintf(ui->status, sizeof(ui->status), "CAPTURE STOPPING - KEEPING AUDIO");
    } else snprintf(ui->status, sizeof(ui->status), "CAPTURE STOP FAILED: %.126s", error);
}

static void capture_button(SDL_AudioDeviceID device, AudioState *audio,
                           TsUiState *ui, TsInstrument *instrument,
                           int output_rate)
{
    if (audio->capture.state == TS_CAPTURE_IDLE)
        arm_capture(device, audio, ui, instrument, output_rate);
    else if (audio->capture.state == TS_CAPTURE_RECORDING)
        stop_capture_early(device, audio, ui);
    else if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER)
        snprintf(ui->status, sizeof(ui->status),
                 "CAPTURE ARMED - SELECT SOURCE  STAGE OR PLAY  ESC CANCELS");
}

static void finalize_capture(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                             TsInstrument *instrument)
{
    char error[160];
    char archive_error[160];
    char archive_path[1200];
    char overlay[80];
    int destination;
    int source;
    int stopped_early;
    size_t frames;
    uint32_t rate;
    int ok;
    int archived;
    if (audio->capture.state != TS_CAPTURE_COMPLETED) return;
    destination = audio->capture.destination_slot;
    source = audio->capture.source_slot;
    stopped_early = audio->capture.stopped_early;
    frames = audio->capture.recorded_frames;
    rate = audio->capture.sample_rate;
    archived = ts_capture_archive_write(
        capture_archive_directory(), TS_CAPTURE_ARCHIVE_INTERNAL,
        audio->capture.buffer, frames, rate,
        archive_path, sizeof(archive_path), archive_error, sizeof(archive_error));
    ok = ts_instrument_commit_capture(instrument, destination, source,
                                      audio->capture.buffer, frames, rate,
                                      stopped_early, error, sizeof(error));
    if (device) SDL_LockAudioDevice(device);
    ts_capture_free(&audio->capture);
    if (device) SDL_UnlockAudioDevice(device);
    ui->workbench_loop_active = 0;
    ui->workbench_loop_persistent = 0;
    ui->active_notes = 0u;
    ui->mouse_note = -1;
    if (ok) {
        ui->bank_view_slot = -1;
        ui->audition_source = TS_AUDITION_CURRENT;
        snprintf(overlay, sizeof(overlay), "%s TILE %02d  %.3F S",
                 stopped_early ? "CAPTURE STOPPED" : "CAPTURE COMPLETE",
                 destination + 1, (double)frames / rate);
        show_overlay(ui, overlay, 1400u);
        if (!archived)
            snprintf(ui->status, sizeof(ui->status),
                     "CAPTURE KEPT IN TILE %02d - ARCHIVE FAILED: %.92s",
                     destination + 1, archive_error);
        else if (stopped_early)
            snprintf(ui->status, sizeof(ui->status),
                     "CAPTURE STOPPED - %.3F S KEPT IN TILE %02d",
                     (double)frames / rate, destination + 1);
        else
            snprintf(ui->status, sizeof(ui->status),
                     "CAPTURE COMPLETE - TILE %02d %.3F S",
                     destination + 1, (double)frames / rate);
    } else {
        show_overlay(ui, archived ? "CAPTURE ARCHIVED" : "CAPTURE FAILED", 1200u);
        snprintf(ui->status, sizeof(ui->status),
                 archived ? "CAPTURE ARCHIVED - TILE COMMIT FAILED: %.104s" :
                            "CAPTURE COMMIT AND ARCHIVE FAILED: %.102s",
                 archived ? error : archive_error);
    }
}

static void begin_exit_confirmation(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument,
                                    const TsSamplePages *sample_pages,
                                    const TsInstrument *parked_record,
                                    int record_bank_active)
{
    stop_all_force(device, audio, ui);
    if (audio->capture.state == TS_CAPTURE_COMPLETED)
        finalize_capture(device, audio, ui, instrument);
    if (device) SDL_LockAudioDevice(device);
    if (audio->capture.state != TS_CAPTURE_IDLE)
        ts_capture_free(&audio->capture);
    if (device) SDL_UnlockAudioDevice(device);
    sync_capture_ui(device, audio, ui);
    ui->exit_has_unsaved = runtime_project_state_hash(
        sample_pages, instrument, parked_record,
        record_bank_active) != ui->saved_state_hash;
    ui->exit_confirm_open = 1;
    snprintf(ui->status, sizeof(ui->status), "%s",
             ui->exit_has_unsaved ?
             "EXIT? UNSAVED CHANGES WILL BE LOST" : "EXIT TAPESISTER?");
}

static void lock_edit(SDL_AudioDeviceID device, AudioState *audio)
{
    if (device) SDL_LockAudioDevice(device);
    (void)audio;
}

static void unlock_edit(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                        TsInstrument *instrument)
{
    TsAuditionPlan plan;
    ui->bank_view_slot = -1;
    ui->has_pitch_suggestion = 0;
    if (audio->playing && audio->bank_slot >= 0) {
        /* Bank slots own stable buffers, but tuning metadata is live. The
           selected tile may not have been stored back into its slot yet. */
        if (audio->bank_slot == instrument->selected_slot)
            audio->pitch = ts_instrument_audition_pitch(instrument);
        else if (audio->bank_slot < TS_BANK_SLOT_COUNT)
            audio->pitch = ts_tuning_pair_audition_pitch(
                &instrument->bank[audio->bank_slot].tuning,
                &instrument->bank[audio->bank_slot].audible_tuning);
        if (audio->sample != NULL && audio->output_rate > 0)
            audio->step = (double)audio->sample->sample_rate /
                          audio->output_rate * audio->pitch;
    } else if (audio->playing && audition_plan_ui(instrument, ui, audio->source,
                                           audio->range, &plan)) {
        audio->position = ts_audition_map_progress(
            audio->position, audio->range_start, audio->range_end,
            plan.first, plan.last);
        if (audio->position >= (double)plan.last) audio->position = (double)plan.first;
        audio->sample = plan.sample;
        audio->range_start = plan.first;
        audio->range_end = plan.last;
        audio->looping = audio->range == TS_AUDITION_WORKBENCH_LOOP ||
                         (audio->range == TS_AUDITION_LOOP && instrument->has_loop);
        audio->loop_mode = instrument->loop_mode;
        if (audio->loop_mode == TS_LOOP_REVERSE) audio->loop_direction = -1;
        else if (audio->loop_mode == TS_LOOP_FORWARD) audio->loop_direction = 1;
        else if (audio->loop_direction == 0) audio->loop_direction = 1;
        audio->crossfade_frames = audio->looping ?
                                  ts_audition_crossfade_frames(
                                      &plan, instrument->loop_crossfade_ms) : 0;
        audio->pitch = ts_instrument_audition_pitch(instrument);
        if (audio->output_rate > 0)
            audio->step = (double)plan.sample->sample_rate /
                          audio->output_rate * audio->pitch;
    } else if (audio->playing) {
        audio->playing = 0;
    }
    ts_note_bank_sync(&audio->notes, instrument, audio->output_rate);
    audio->sample = audio->playing ? audio->sample :
                    (ui->audition_source == TS_AUDITION_PARENT ?
                     &instrument->parent : &instrument->current);
    audio->source = ui->audition_source;
    if (device) SDL_UnlockAudioDevice(device);
}

static void stop_drone_preview(SDL_AudioDeviceID device, AudioState *audio,
                               TsUiState *ui, const TsSample *drone)
{
    if (device) SDL_LockAudioDevice(device);
    if (audio->sample == drone) {
        audio->playing = 0;
        audio->looping = 0;
        audio->bank_slot = -1;
        audio->sample = NULL;
        audio->range_start = 0;
        audio->range_end = 0;
    }
    if (device) SDL_UnlockAudioDevice(device);
    ui->drone_preview_active = 0;
}

static void close_drone_dialog(SDL_AudioDeviceID device, AudioState *audio,
                               TsUiState *ui, TsSample *drone)
{
    stop_drone_preview(device, audio, ui, drone);
    ui->drone_preview_sample = NULL;
    ts_sample_free(drone);
    ts_ui_waveform_cache_invalidate(ui, TS_UI_WAVEFORM_DRONE);
    ui->drone_open = 0;
    ui->drone_crossfade_dragging = 0;
    ui->drone_crossfade_drag_start_x = 0;
    ui->drone_crossfade_drag_start_frames = 0;
    ui->drone_source_slot = -1;
    ui->drone_source_first = 0;
    ui->drone_source_last = 0;
    ui->drone_split_frame = 0;
    ui->drone_output_frames = 0;
    ui->drone_overlap_frames = 0;
    ui->drone_source_hash = 0;
    ui->drone_effective_crossfade_ms = 0.0f;
}

static int drone_context_matches(const TsUiState *ui,
                                 const TsInstrument *instrument)
{
    return ui->drone_open && instrument->selected_slot == ui->drone_source_slot &&
           instrument->has_selection &&
           instrument->selection_first == ui->drone_source_first &&
           instrument->selection_last == ui->drone_source_last &&
           ts_sample_hash(&instrument->current) == ui->drone_source_hash;
}

static size_t drone_maximum_overlap(size_t first, size_t last, size_t split)
{
    size_t maximum;
    if (last <= first || split <= first || split >= last) return 0;
    maximum = (last - first) / 4u;
    if (maximum >= split - first) maximum = split - first - 1u;
    if (maximum >= last - split) maximum = last - split - 1u;
    return maximum;
}

static size_t drone_snap_overlap(const TsSample *source,
                                 size_t first, size_t last, size_t split,
                                 size_t requested, int handle,
                                 int overlap_direction, size_t current)
{
    size_t maximum = drone_maximum_overlap(first, last, split);
    size_t range_first;
    size_t range_last;
    size_t target;
    size_t boundary;
    size_t overlap;
    if (maximum == 0 || source == NULL || source->data == NULL)
        return 0;
    if (requested < 1u) requested = 1u;
    if (requested > maximum) requested = maximum;
    if (handle == 1) {
        range_first = last - maximum;
        range_last = last - 1u;
        target = last - requested;
    } else {
        range_first = first + 1u;
        range_last = first + maximum;
        target = first + requested;
    }
    boundary = ts_sample_nearest_zero_crossing_in_range(
        source, target, range_first, range_last);
    overlap = handle == 1 ? last - boundary : boundary - first;
    if (overlap_direction != 0 && overlap == current) {
        int sample_direction = handle == 1 ? -overlap_direction : overlap_direction;
        size_t current_boundary = handle == 1 ? last - current : first + current;
        size_t moved = ts_sample_zero_crossing_in_direction(
            source, current_boundary, sample_direction, 1u);
        if (moved >= range_first && moved <= range_last)
            overlap = handle == 1 ? last - moved : moved - first;
    }
    if (overlap < 1u) overlap = 1u;
    if (overlap > maximum) overlap = maximum;
    return overlap;
}

static void begin_drone_dialog(SDL_AudioDeviceID device, AudioState *audio,
                               TsUiState *ui, const TsInstrument *instrument,
                               TsSample *drone)
{
    char error[160];
    size_t split = 0;
    size_t overlap = 0;
    if (instrument == NULL || instrument->current.data == NULL ||
        !instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        snprintf(ui->status, sizeof(ui->status),
                 "SELECT A NONEMPTY RANGE BEFORE DRONE");
        return;
    }
    stop_all(device, audio, ui);
    ui->drone_preview_sample = NULL;
    ts_sample_free(drone);
    if (!ts_sample_make_drone(drone, &instrument->current,
                              instrument->selection_first,
                              instrument->selection_last,
                              ui->config.drone_crossfade_ms,
                              &split, &overlap, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "DRONE FAILED: %.142s", error);
        return;
    }
    {
        size_t snapped = drone_snap_overlap(
            &instrument->current, instrument->selection_first,
            instrument->selection_last, split, overlap, 2, 0, overlap);
        if (snapped != 0 && snapped != overlap &&
            !ts_sample_make_drone_at_split(
                drone, &instrument->current, instrument->selection_first,
                instrument->selection_last, split, snapped, &overlap,
                error, sizeof(error))) {
            snprintf(ui->status, sizeof(ui->status),
                     "DRONE FAILED: %.142s", error);
            ts_sample_free(drone);
            return;
        }
    }
    ui->drone_open = 1;
    ui->drone_preview_active = 0;
    ui->drone_source_slot = instrument->selected_slot;
    ui->drone_source_first = instrument->selection_first;
    ui->drone_source_last = instrument->selection_last;
    ui->drone_split_frame = split;
    ui->drone_output_frames = drone->frames;
    ui->drone_overlap_frames = overlap;
    ui->drone_preview_sample = drone;
    ts_ui_waveform_cache_invalidate(ui, TS_UI_WAVEFORM_DRONE);
    ui->drone_source_hash = ts_sample_hash(&instrument->current);
    ui->drone_effective_crossfade_ms =
        (float)((double)overlap * 1000.0 / (double)drone->sample_rate);
    snprintf(ui->status, sizeof(ui->status),
             "DRONE READY - PREVIEW, COPY, OR REPLACE");
}

static void preview_drone(SDL_AudioDeviceID device, AudioState *audio,
                          TsUiState *ui, const TsInstrument *instrument,
                          TsSample *drone, int output_rate)
{
    if (!drone_context_matches(ui, instrument)) {
        close_drone_dialog(device, audio, ui, drone);
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE CANCELLED - EDITING CONTEXT CHANGED");
        return;
    }
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "DRONE READY - AUDIO UNAVAILABLE");
        return;
    }
    SDL_LockAudioDevice(device);
    ts_note_bank_clear(&audio->notes);
    audio->sample = drone;
    audio->position = 0.0;
    audio->pitch = ts_instrument_audition_pitch(instrument);
    audio->range_start = 0;
    audio->range_end = drone->frames;
    audio->source = TS_AUDITION_CURRENT;
    audio->range = TS_AUDITION_LOOP;
    audio->looping = 1;
    audio->loop_mode = TS_LOOP_FORWARD;
    audio->loop_direction = 1;
    audio->crossfade_frames = 0;
    audio->bank_slot = -1;
    audio->step = (double)drone->sample_rate / (double)output_rate *
                  audio->pitch;
    audio->playing = 1;
    SDL_UnlockAudioDevice(device);
    ui->drone_preview_active = 1;
    snprintf(ui->status, sizeof(ui->status),
             "PREVIEWING DRONE LOOP - SPACE OR STOP ENDS PREVIEW");
}

static int adjust_drone_crossfade(SDL_AudioDeviceID device, AudioState *audio,
                                  TsUiState *ui, const TsInstrument *instrument,
                                  TsSample *drone, size_t requested_overlap,
                                  int handle, int overlap_direction,
                                  int output_rate)
{
    char error[160];
    size_t snapped;
    size_t effective = 0;
    int was_previewing;
    if (!drone_context_matches(ui, instrument)) {
        close_drone_dialog(device, audio, ui, drone);
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE CANCELLED - EDITING CONTEXT CHANGED");
        return 0;
    }
    snapped = drone_snap_overlap(
        &instrument->current, ui->drone_source_first, ui->drone_source_last,
        ui->drone_split_frame, requested_overlap, handle,
        overlap_direction, ui->drone_overlap_frames);
    if (snapped == 0 || snapped == ui->drone_overlap_frames) {
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE CROSSFADE ZERO-SNAP LIMIT");
        return 0;
    }
    was_previewing = ui->drone_preview_active;
    stop_drone_preview(device, audio, ui, drone);
    if (!ts_sample_make_drone_at_split(
            drone, &instrument->current, ui->drone_source_first,
            ui->drone_source_last, ui->drone_split_frame, snapped,
            &effective, error, sizeof(error))) {
        if (was_previewing)
            preview_drone(device, audio, ui, instrument, drone, output_rate);
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE CROSSFADE FAILED: %.132s", error);
        return 0;
    }
    ui->drone_preview_sample = drone;
    ts_ui_waveform_cache_invalidate(ui, TS_UI_WAVEFORM_DRONE);
    ui->drone_overlap_frames = effective;
    ui->drone_output_frames = drone->frames;
    ui->drone_effective_crossfade_ms =
        (float)((double)effective * 1000.0 / (double)drone->sample_rate);
    if (was_previewing)
        preview_drone(device, audio, ui, instrument, drone, output_rate);
    snprintf(ui->status, sizeof(ui->status),
             "DRONE CROSSFADE %.2F MS - %s EDGE ZERO SNAPPED",
             ui->drone_effective_crossfade_ms,
             handle == 1 ? "LEFT" : "RIGHT");
    return 1;
}

static int transform_cancel_check(void *userdata)
{
    TransformWorker *worker = userdata;
    return worker != NULL && SDL_AtomicGet(&worker->cancel) != 0;
}

static int transform_worker_main(void *userdata)
{
    TransformWorker *worker = userdata;
    if (worker->backend == TS_TRANSFORM_BACKEND_DSP) {
        worker->dsp_ok = ts_dsp_transform_render_recipe(
            &worker->input, worker->dsp_recipe, &worker->dsp_values,
            &worker->dsp_output,
            &worker->dsp_safety, &worker->dsp_peak, &worker->dsp_dc_offset,
            &worker->dsp_clipped_samples, worker->error, sizeof(worker->error));
        if (SDL_AtomicGet(&worker->cancel) != 0) {
            ts_sample_free(&worker->dsp_output);
            worker->dsp_ok = 0;
            snprintf(worker->error, sizeof(worker->error),
                     "NATIVE DSP RENDER CANCELED - TILE UNCHANGED");
        }
    } else
        (void)ts_cdp_run_recipe(&worker->runtime, worker->recipe, &worker->values,
                                &worker->input, &worker->options, &worker->result,
                                worker->error, sizeof(worker->error));
    SDL_AtomicSet(&worker->done, 1);
    return worker->backend == TS_TRANSFORM_BACKEND_DSP ?
           (worker->dsp_ok ? 0 : 1) :
           (worker->result.status == TS_CDP_RUN_OK ? 0 : 1);
}

static void transform_controller_init(TransformController *controller)
{
    memset(controller, 0, sizeof(*controller));
    ts_cdp_runtime_init(&controller->runtime);
    ts_transform_preview_init(&controller->preview);
    ts_dsp_transform_preview_init(&controller->dsp_preview);
    controller->next_job_id = 1u;
}

static void stop_transform_preview(SDL_AudioDeviceID device, AudioState *audio,
                                   TsUiState *ui, TransformController *controller)
{
    if (device) SDL_LockAudioDevice(device);
    if (audio->sample == &controller->preview.sample ||
        audio->sample == &controller->dsp_preview.sample) {
        audio->playing = 0;
        audio->looping = 0;
        audio->sample = NULL;
        audio->range_start = 0u;
        audio->range_end = 0u;
        audio->bank_slot = -1;
    }
    if (device) SDL_UnlockAudioDevice(device);
    ui->transform_preview_active = 0;
}

static void discard_transform_preview(SDL_AudioDeviceID device, AudioState *audio,
                                      TsUiState *ui, TransformController *controller)
{
    stop_transform_preview(device, audio, ui, controller);
    ts_transform_preview_free(&controller->preview);
    ts_dsp_transform_preview_free(&controller->dsp_preview);
    ui->transform_preview_sample = NULL;
    ui->transform_preview_first = 0u;
    ui->transform_preview_last = 0u;
    ui->transform_preview_available = 0;
    ui->transform_safety = TS_CDP_SAFETY_INVALID;
    ts_ui_waveform_cache_invalidate(ui, TS_UI_WAVEFORM_TRANSFORM);
}

static void mark_transform_stale(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TransformController *controller,
                                 const char *message)
{
    ++controller->render_generation;
    controller->rerender_requested = 0;
    if (controller->worker != NULL)
        SDL_AtomicSet(&controller->worker->cancel, 1);
    discard_transform_preview(device, audio, ui, controller);
    snprintf(ui->transform_message, sizeof(ui->transform_message), "%s",
             message != NULL ? message : "CHANGED - RENDER AGAIN");
}

static void discover_transform_runtime(TsUiState *ui,
                                       TransformController *controller)
{
    char error[160];
    char *base = SDL_GetBasePath();
    int ok = ts_cdp_runtime_discover(&controller->runtime,
                                     ui->config.cdp_bin_path,
                                     base != NULL ? base : ".",
                                     error, sizeof(error));
    if (base != NULL) SDL_free(base);
    ui->transform_runtime_available = ok;
    if (!ok)
        snprintf(ui->transform_message, sizeof(ui->transform_message), "%.92s", error);
}

static const TsCdpRecipe *active_transform_recipe(const TsUiState *ui)
{
    const TsCdpRecipe *recipe = ui != NULL && ui->transform_recipe_index >= 0 ?
        ts_cdp_factory_recipe_at((size_t)ui->transform_recipe_index) : NULL;
    return recipe != NULL ? recipe : ts_cdp_factory_recipe_at(0u);
}

static void begin_transform_workspace(TsUiState *ui,
                                      const TsInstrument *instrument,
                                      TransformController *controller,
                                      int recipe_index)
{
    const TsCdpRecipe *recipe = recipe_index >= 0 ?
        ts_cdp_factory_recipe_at((size_t)recipe_index) : NULL;
    if (recipe == NULL) {
        snprintf(ui->status, sizeof(ui->status),
                 "THAT TRANSFORM RECIPE IS NOT AVAILABLE");
        return;
    }
    if (controller->quick_apply && controller->worker != NULL) {
        ++controller->render_generation;
        SDL_AtomicSet(&controller->worker->cancel, 1);
    }
    controller->quick_apply = 0;
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0u) {
        snprintf(ui->status, sizeof(ui->status),
                 "%s NEEDS AN OCCUPIED ACTIVE TILE", recipe->display_name);
        return;
    }
    ui->transform_values = ui->cdp_presets[recipe_index];
    ui->transform_values.tuning_hz =
        (float)ts_tuning_frequency(&instrument->audible_tuning);
    ui->transform_backend = TS_TRANSFORM_BACKEND_CDP;
    ui->transform_recipe_index = recipe_index;
    ui->transform_dsp_slot = -1;
    ui->transform_open = 1;
    ui->transform_scope = instrument->has_selection &&
                          instrument->selection_last > instrument->selection_first ?
                          TS_TRANSFORM_SELECTION : TS_TRANSFORM_WHOLE;
    ui->transform_selection_dragging = 0;
    discover_transform_runtime(ui, controller);
    if (ui->transform_runtime_available) {
        char runtime_error[160];
        ui->transform_runtime_available = ts_cdp_runtime_recipe_available(
            &controller->runtime, recipe, runtime_error, sizeof(runtime_error));
        if (!ui->transform_runtime_available)
            snprintf(ui->transform_message, sizeof(ui->transform_message),
                     "%.92s", runtime_error);
    }
    if (ui->transform_runtime_available)
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "%s READY - RENDER IS NON-DESTRUCTIVE", recipe->display_name);
    snprintf(ui->status, sizeof(ui->status),
             "%s TRANSFORM OPEN - SELECTION AND VIEW ARE SHARED",
             recipe->display_name);
}

static int begin_dsp_transform_workspace(TsUiState *ui,
                                         const TsInstrument *instrument,
                                         TransformController *controller,
                                         int slot)
{
    const TsDspRecipe *preset = slot >= 0 ?
        ts_dsp_factory_recipe_at((size_t)slot) : NULL;
    if (ui == NULL || preset == NULL) {
        if (ui != NULL) snprintf(ui->status, sizeof(ui->status),
                                "THAT DSP PRESET IS EMPTY");
        return 0;
    }
    if (instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0u) {
        snprintf(ui->status, sizeof(ui->status),
                 "%s NEEDS AN OCCUPIED ACTIVE TILE", preset->display_name);
        return 0;
    }
    if (controller->quick_apply && controller->worker != NULL) {
        ++controller->render_generation;
        SDL_AtomicSet(&controller->worker->cancel, 1);
    }
    controller->quick_apply = 0;
    ui->transform_backend = TS_TRANSFORM_BACKEND_DSP;
    ui->transform_dsp_slot = slot;
    ui->transform_dsp_values = ui->dsp_presets[slot];
    ui->transform_dsp_values.tuning_hz =
        (float)ts_tuning_frequency(&instrument->audible_tuning);
    ui->transform_recipe_index = -1;
    ui->transform_open = 1;
    ui->transform_runtime_available = 1;
    ui->transform_scope = instrument->has_selection &&
                          instrument->selection_last > instrument->selection_first ?
                          TS_TRANSFORM_SELECTION : TS_TRANSFORM_WHOLE;
    ui->transform_selection_dragging = 0;
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "%s PREVIEW QUEUED - SOURCE IS UNCHANGED", preset->display_name);
    snprintf(ui->status, sizeof(ui->status),
             "%s DSP TRANSFORM OPEN - MIDDLE CLICK EDITS THE PRESET",
             preset->display_name);
    return 1;
}

static void close_transform_workspace(SDL_AudioDeviceID device, AudioState *audio,
                                      TsUiState *ui, TransformController *controller)
{
    ++controller->render_generation;
    controller->rerender_requested = 0;
    if (controller->worker != NULL)
        SDL_AtomicSet(&controller->worker->cancel, 1);
    discard_transform_preview(device, audio, ui, controller);
    ui->transform_open = 0;
    ui->transform_rendering = 0;
    ui->transform_selection_dragging = 0;
    snprintf(ui->status, sizeof(ui->status),
             "TRANSFORM CLOSED - TILE AND VIEW PRESERVED");
}

static int launch_transform_worker(TsUiState *ui, TsInstrument *instrument,
                                   TransformController *controller)
{
    TransformWorker *worker;
    const int dsp = ui->transform_backend == TS_TRANSFORM_BACKEND_DSP;
    const TsCdpRecipe *recipe = dsp ? NULL : active_transform_recipe(ui);
    const TsDspRecipe *dsp_recipe = dsp && ui->transform_dsp_slot >= 0 ?
        ts_dsp_factory_recipe_at((size_t)ui->transform_dsp_slot) : NULL;
    const char *name = dsp && dsp_recipe != NULL ? dsp_recipe->display_name :
                       recipe != NULL ? recipe->display_name : "TRANSFORM";
    char error[160];
    if ((!dsp && recipe == NULL) ||
        (dsp && dsp_recipe == NULL)) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "TRANSFORM RECIPE IS NOT AVAILABLE");
        return 0;
    }
    if (!dsp && (!ui->transform_runtime_available || !controller->runtime.available)) {
        discover_transform_runtime(ui, controller);
        if (!ui->transform_runtime_available) return 0;
    }
    if (!dsp && !ts_cdp_runtime_recipe_available(&controller->runtime, recipe,
                                                  error, sizeof(error))) {
        ui->transform_runtime_available = 0;
        snprintf(ui->transform_message, sizeof(ui->transform_message), "%.92s", error);
        return 0;
    }
    worker = calloc(1u, sizeof(*worker));
    if (worker == NULL) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "OUT OF MEMORY STARTING RENDER");
        return 0;
    }
    ts_sample_init(&worker->input);
    ts_sample_init(&worker->dsp_output);
    ts_cdp_run_options_init(&worker->options);
    ts_cdp_run_result_init(&worker->result);
    worker->backend = ui->transform_backend;
    worker->runtime = controller->runtime;
    worker->recipe = recipe;
    worker->dsp_recipe = dsp_recipe;
    if (!dsp)
        ui->transform_values.tuning_hz =
            (float)ts_tuning_frequency(&instrument->audible_tuning);
    else
        ui->transform_dsp_values.tuning_hz =
            (float)ts_tuning_frequency(&instrument->audible_tuning);
    worker->values = ui->transform_values;
    worker->dsp_values = ui->transform_dsp_values;
    worker->options.job_id = controller->next_job_id++;
    worker->options.cancel_check = transform_cancel_check;
    worker->options.cancel_userdata = worker;
    ++controller->render_generation;
    if ((dsp &&
         (!ts_dsp_transform_identity_capture_recipe(
              &worker->dsp_identity, instrument, ui->transform_scope,
              dsp_recipe, &worker->dsp_values,
              worker->options.job_id, controller->render_generation,
              error, sizeof(error)) ||
          !ts_dsp_transform_extract_input(instrument, &worker->dsp_identity,
                                          &worker->input,
                                          error, sizeof(error)))) ||
        (!dsp &&
         (!ts_transform_identity_capture(
              &worker->identity, instrument, ui->transform_scope, recipe,
              &worker->values, worker->options.job_id,
              controller->render_generation, error, sizeof(error)) ||
          !ts_transform_extract_input(instrument, &worker->identity,
                                      &worker->input, error, sizeof(error)) ||
          !ts_cdp_recipe_input_valid(recipe, worker->input.frames,
                                     worker->input.sample_rate,
                                     error, sizeof(error))))) {
        snprintf(ui->transform_message, sizeof(ui->transform_message), "%.92s", error);
        ts_sample_free(&worker->input);
        ts_sample_free(&worker->dsp_output);
        ts_cdp_run_result_free(&worker->result);
        free(worker);
        return 0;
    }
    worker->thread = SDL_CreateThread(transform_worker_main,
                                      dsp ? "TapeSister DSP" : "TapeSister CDP",
                                      worker);
    if (worker->thread == NULL) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "COULD NOT START RENDER WORKER: %.54s", SDL_GetError());
        ts_sample_free(&worker->input);
        ts_sample_free(&worker->dsp_output);
        ts_cdp_run_result_free(&worker->result);
        free(worker);
        return 0;
    }
    controller->worker = worker;
    ui->transform_rendering = 1;
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "RENDERING %s JOB %llu", name,
             (unsigned long long)(dsp ? worker->dsp_identity.job_id :
                                       worker->identity.job_id));
    return 1;
}

static void request_transform_render(SDL_AudioDeviceID device, AudioState *audio,
                                     TsUiState *ui, TsInstrument *instrument,
                                     TransformController *controller)
{
    discard_transform_preview(device, audio, ui, controller);
    if (controller->worker != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 1;
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "CANCELING OLD JOB - NEWEST REQUEST QUEUED");
        return;
    }
    (void)launch_transform_worker(ui, instrument, controller);
}

static void request_cdp_quick_apply(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument,
                                    TransformController *controller,
                                    int recipe_index)
{
    const TsCdpRecipe *recipe = recipe_index >= 0 ?
        ts_cdp_factory_recipe_at((size_t)recipe_index) : NULL;
    if (recipe == NULL || instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0u) {
        snprintf(ui->status, sizeof(ui->status),
                 "CDP QUICK APPLY NEEDS AN OCCUPIED TILE");
        return;
    }
    if (audio != NULL && audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL CAPTURE BEFORE CDP QUICK APPLY");
        return;
    }
    discard_transform_preview(device, audio, ui, controller);
    ui->transform_backend = TS_TRANSFORM_BACKEND_CDP;
    ui->transform_recipe_index = recipe_index;
    ui->transform_dsp_slot = -1;
    ui->transform_values = ui->cdp_presets[recipe_index];
    ui->transform_values.tuning_hz =
        (float)ts_tuning_frequency(&instrument->audible_tuning);
    ui->transform_scope = instrument->has_selection &&
                          instrument->selection_last > instrument->selection_first ?
                          TS_TRANSFORM_SELECTION : TS_TRANSFORM_WHOLE;
    ui->transform_open = 0;
    controller->quick_apply = 1;
    if (controller->worker != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 1;
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->status, sizeof(ui->status),
                 "%s QUEUED - CANCELING OLDER CDP JOB", recipe->display_name);
        return;
    }
    if (launch_transform_worker(ui, instrument, controller))
        snprintf(ui->status, sizeof(ui->status),
                 "%s RENDERING OFFLINE - TILE UNCHANGED UNTIL READY",
                 recipe->display_name);
    else controller->quick_apply = 0;
}

static void request_dsp_quick_apply(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument,
                                    TransformController *controller,
                                    int recipe_index)
{
    const TsDspRecipe *recipe = recipe_index >= 0 ?
        ts_dsp_factory_recipe_at((size_t)recipe_index) : NULL;
    if (recipe == NULL || instrument == NULL || instrument->current.data == NULL ||
        instrument->current.frames == 0u) {
        snprintf(ui->status, sizeof(ui->status),
                 "DSP QUICK APPLY NEEDS AN OCCUPIED TILE OR SILENT CANVAS");
        return;
    }
    if (audio != NULL && audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL CAPTURE BEFORE DSP QUICK APPLY");
        return;
    }
    discard_transform_preview(device, audio, ui, controller);
    ui->transform_backend = TS_TRANSFORM_BACKEND_DSP;
    ui->transform_recipe_index = -1;
    ui->transform_dsp_slot = recipe_index;
    ui->transform_dsp_values = ui->dsp_presets[recipe_index];
    ui->transform_dsp_values.tuning_hz =
        (float)ts_tuning_frequency(&instrument->audible_tuning);
    ui->transform_scope = instrument->has_selection &&
                          instrument->selection_last > instrument->selection_first ?
                          TS_TRANSFORM_SELECTION : TS_TRANSFORM_WHOLE;
    ui->transform_open = 0;
    controller->quick_apply = 1;
    if (controller->worker != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 1;
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->status, sizeof(ui->status),
                 "%s QUEUED - CANCELING OLDER TRANSFORM JOB",
                 recipe->display_name);
        return;
    }
    if (launch_transform_worker(ui, instrument, controller))
        snprintf(ui->status, sizeof(ui->status),
                 "%s RENDERING OFFLINE - TILE UNCHANGED UNTIL READY",
                 recipe->display_name);
    else controller->quick_apply = 0;
}

static void poll_transform_worker(SDL_AudioDeviceID device, AudioState *audio,
                                  TsUiState *ui, TsInstrument *instrument,
                                  TransformController *controller)
{
    TransformWorker *worker = controller->worker;
    const TsCdpRecipe *recipe = controller->quick_apply && worker != NULL ?
                                worker->recipe :
                                ui->transform_backend == TS_TRANSFORM_BACKEND_CDP ?
                                active_transform_recipe(ui) : NULL;
    char error[160];
    int rerender;
    int published = 0;
    error[0] = '\0';
    (void)device;
    (void)audio;
    if (worker == NULL || SDL_AtomicGet(&worker->done) == 0) return;
    SDL_WaitThread(worker->thread, NULL);
    worker->thread = NULL;
    rerender = controller->rerender_requested;
    controller->rerender_requested = 0;
    ui->transform_rendering = 0;
    if (!rerender &&
        ((ui->transform_open && worker->backend == ui->transform_backend) ||
         controller->quick_apply)) {
        if (worker->backend == TS_TRANSFORM_BACKEND_DSP && worker->dsp_ok &&
            ts_dsp_transform_identity_matches_recipe(
                &worker->dsp_identity, instrument, ui->transform_scope,
                worker->dsp_recipe,
                controller->quick_apply ? &worker->dsp_values :
                                          &ui->transform_dsp_values,
                controller->render_generation, error, sizeof(error)) &&
            ts_dsp_transform_prepare_preview(
                instrument, &worker->dsp_identity, &worker->dsp_output,
                worker->dsp_safety, worker->dsp_peak, worker->dsp_dc_offset,
                worker->dsp_clipped_samples, &controller->dsp_preview,
                error, sizeof(error))) {
            ui->transform_preview_sample = &controller->dsp_preview.sample;
            ui->transform_preview_first = controller->dsp_preview.replacement_first;
            ui->transform_preview_last = controller->dsp_preview.replacement_last;
            ui->transform_safety = controller->dsp_preview.safety;
            published = 1;
        } else if (worker->backend == TS_TRANSFORM_BACKEND_CDP &&
                   worker->result.status == TS_CDP_RUN_OK && recipe != NULL &&
                   ts_transform_identity_matches(
                       &worker->identity, instrument,
                       controller->quick_apply ? worker->identity.scope : ui->transform_scope,
                       recipe,
                       controller->quick_apply ? &worker->values : &ui->transform_values,
                       controller->render_generation,
                       error, sizeof(error)) &&
                   ts_transform_prepare_preview(
                       instrument, &worker->identity, recipe, &worker->result,
                       &controller->preview, error, sizeof(error))) {
            ui->transform_preview_sample = &controller->preview.sample;
            ui->transform_preview_first = controller->preview.replacement_first;
            ui->transform_preview_last = controller->preview.replacement_last;
            ui->transform_safety = controller->preview.safety;
            published = 1;
        }
    }
    if (published) {
        /* Worker output becomes visible only after the thread has joined and
           the newest-render identity checks above have accepted it. */
        ts_ui_waveform_cache_invalidate(ui, TS_UI_WAVEFORM_TRANSFORM);
        if (controller->quick_apply) {
            int applied;
            if (audio != NULL && audio->capture.state != TS_CAPTURE_IDLE) {
                applied = 0;
                snprintf(error, sizeof(error),
                         "CAPTURE STARTED - QUICK APPLY DISCARDED");
            } else {
                lock_edit(device, audio);
                if (worker->backend == TS_TRANSFORM_BACKEND_DSP)
                    applied = ts_dsp_transform_apply_preview_recipe(
                        instrument, &controller->dsp_preview,
                        worker->dsp_identity.scope, worker->dsp_recipe,
                        &worker->dsp_values, controller->render_generation,
                        error, sizeof(error));
                else
                    applied = ts_transform_apply_preview(
                        instrument, &controller->preview, worker->identity.scope,
                        worker->recipe, &worker->values,
                        controller->render_generation, error, sizeof(error));
                unlock_edit(device, audio, ui, instrument);
            }
            if (applied) {
                snprintf(ui->status, sizeof(ui->status),
                         "%s APPLIED TO %s - ONE STEP UNDO",
                         worker->backend == TS_TRANSFORM_BACKEND_DSP ?
                         worker->dsp_recipe->display_name :
                         worker->recipe->display_name,
                         (worker->backend == TS_TRANSFORM_BACKEND_DSP ?
                          worker->dsp_identity.scope : worker->identity.scope) ==
                         TS_TRANSFORM_SELECTION ?
                         "SELECTION" : "WHOLE TILE");
                ts_transform_preview_free(&controller->preview);
                ts_dsp_transform_preview_free(&controller->dsp_preview);
                ui->transform_preview_sample = NULL;
                ui->transform_preview_available = 0;
                ui->transform_safety = TS_CDP_SAFETY_INVALID;
                ++controller->render_generation;
            } else {
                snprintf(ui->status, sizeof(ui->status),
                         "%s QUICK APPLY REJECTED: %.100s",
                         worker->backend == TS_TRANSFORM_BACKEND_DSP ?
                         worker->dsp_recipe->display_name :
                         worker->recipe->display_name, error);
                ts_transform_preview_free(&controller->preview);
                ts_dsp_transform_preview_free(&controller->dsp_preview);
                ui->transform_preview_sample = NULL;
                ui->transform_preview_available = 0;
                ui->transform_safety = TS_CDP_SAFETY_INVALID;
            }
            controller->quick_apply = 0;
        } else {
            ui->transform_preview_available = 1;
            snprintf(ui->transform_message, sizeof(ui->transform_message),
                     "PREVIEW READY - %s PEAK %.3F",
                     ts_cdp_safety_name(ui->transform_safety),
                     worker->backend == TS_TRANSFORM_BACKEND_DSP ?
                     controller->dsp_preview.peak : controller->preview.peak);
        }
    } else if (!rerender && (ui->transform_open || controller->quick_apply)) {
        const char *message = error[0] != '\0' ? error : worker->error[0] != '\0' ?
                              worker->error :
                              worker->backend == TS_TRANSFORM_BACKEND_CDP &&
                              worker->result.status == TS_CDP_RUN_CANCELLED ?
                              "RENDER CANCELED - TILE UNCHANGED" :
                              worker->backend == TS_TRANSFORM_BACKEND_CDP &&
                              worker->result.status == TS_CDP_RUN_TIMEOUT ?
                              "RENDER TIMED OUT - TILE UNCHANGED" :
                              "RENDER FAILED - TILE UNCHANGED";
        snprintf(ui->transform_message, sizeof(ui->transform_message), "%.92s", message);
        if (controller->quick_apply) {
            snprintf(ui->status, sizeof(ui->status), "DSP/CDP QUICK APPLY FAILED: %.116s",
                     message);
            controller->quick_apply = 0;
        }
    }
    ts_sample_free(&worker->input);
    ts_sample_free(&worker->dsp_output);
    ts_cdp_run_result_free(&worker->result);
    free(worker);
    controller->worker = NULL;
    if (rerender && (ui->transform_open || controller->quick_apply))
        (void)launch_transform_worker(ui, instrument, controller);
}

static void audition_transform_preview(SDL_AudioDeviceID device, AudioState *audio,
                                       TsUiState *ui, const TsInstrument *instrument,
                                       TransformController *controller,
                                       int output_rate)
{
    TsSample *preview = ui->transform_backend == TS_TRANSFORM_BACKEND_DSP ?
                        &controller->dsp_preview.sample :
                        &controller->preview.sample;
    if (ui->transform_preview_active) {
        stop_transform_preview(device, audio, ui, controller);
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "PREVIEW STOPPED - TILE UNCHANGED");
        return;
    }
    if (!ui->transform_preview_available || preview->data == NULL || !device ||
        output_rate <= 0) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "RENDER A PREVIEW BEFORE AUDITION");
        return;
    }
    SDL_LockAudioDevice(device);
    ts_note_bank_clear(&audio->notes);
    audio->sample = preview;
    audio->position = 0.0;
    audio->pitch = ts_instrument_audition_pitch(instrument);
    audio->range_start = 0u;
    audio->range_end = preview->frames;
    audio->source = TS_AUDITION_CURRENT;
    audio->range = TS_AUDITION_ALL;
    audio->looping = 0;
    audio->loop_mode = TS_LOOP_FORWARD;
    audio->loop_direction = 1;
    audio->crossfade_frames = 0u;
    audio->bank_slot = -1;
    audio->step = (double)preview->sample_rate / (double)output_rate *
                  audio->pitch;
    audio->playing = 1;
    SDL_UnlockAudioDevice(device);
    ui->transform_preview_active = 1;
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "AUDITIONING IMMUTABLE PREVIEW - TILE UNCHANGED");
}

static void apply_transform_preview(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument,
                                    TransformController *controller)
{
    const int dsp = ui->transform_backend == TS_TRANSFORM_BACKEND_DSP;
    const TsCdpRecipe *recipe = dsp ? NULL : active_transform_recipe(ui);
    const TsDspRecipe *dsp_recipe = dsp && ui->transform_dsp_slot >= 0 ?
        ts_dsp_factory_recipe_at((size_t)ui->transform_dsp_slot) : NULL;
    const char *name = dsp && dsp_recipe != NULL ? dsp_recipe->display_name :
                       recipe != NULL ? recipe->display_name : "TRANSFORM";
    char error[160];
    int ok;
    if (audio != NULL && audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "FINISH OR CANCEL CAPTURE BEFORE APPLY");
        return;
    }
    stop_transform_preview(device, audio, ui, controller);
    lock_edit(device, audio);
    ok = dsp ? ts_dsp_transform_apply_preview_recipe(
                   instrument, &controller->dsp_preview, ui->transform_scope,
                   dsp_recipe, &ui->transform_dsp_values,
                   controller->render_generation, error, sizeof(error)) :
               ts_transform_apply_preview(instrument, &controller->preview,
                                          ui->transform_scope, recipe,
                                          &ui->transform_values,
                                          controller->render_generation,
                                          error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->transform_message, sizeof(ui->transform_message), "%.92s", error);
        return;
    }
    if (dsp) ts_dsp_transform_preview_free(&controller->dsp_preview);
    else ts_transform_preview_free(&controller->preview);
    ui->transform_preview_sample = NULL;
    ui->transform_preview_first = 0u;
    ui->transform_preview_last = 0u;
    ui->transform_preview_available = 0;
    ui->transform_safety = TS_CDP_SAFETY_INVALID;
    ++controller->render_generation;
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "%s APPLIED - ONE STEP UNDO", name);
    snprintf(ui->status, sizeof(ui->status),
             "%s APPLIED TO %s - SELECTION COVERS RESULT", name,
             ui->transform_scope == TS_TRANSFORM_SELECTION ? "SELECTION" : "WHOLE TILE");
}

static void adjust_transform_control(SDL_AudioDeviceID device, AudioState *audio,
                                     TsUiState *ui, TsInstrument *instrument,
                                     TransformController *controller,
                                     int index, int direction, int coarse)
{
    if (ui->transform_backend == TS_TRANSFORM_BACKEND_DSP) {
        const TsDspRecipe *recipe = ui->transform_dsp_slot >= 0 ?
            ts_dsp_factory_recipe_at((size_t)ui->transform_dsp_slot) : NULL;
        float value;
        float amount;
        if (recipe == NULL || index < 0 ||
            (size_t)index >= recipe->control_count || direction == 0)
            return;
        value = ui->transform_dsp_values.controls[index];
        amount = coarse ? 0.05f : 0.01f;
        value += direction > 0 ? amount : -amount;
        if (!ts_dsp_recipe_set_control(recipe, &ui->transform_dsp_values,
                                       (size_t)index, value)) return;
        mark_transform_stale(device, audio, ui, controller,
                             "DSP CONTROL CHANGED - PREVIEW QUEUED");
        request_transform_render(device, audio, ui, instrument, controller);
        return;
    }
    const TsCdpRecipe *recipe = active_transform_recipe(ui);
    const TsCdpControlSpec *control;
    float value;
    float step;
    if (recipe == NULL || index < 0 || (size_t)index >= recipe->control_count ||
        direction == 0) return;
    control = &recipe->controls[index];
    value = ui->transform_values.controls[index];
    if (control->type == TS_CDP_CONTROL_ENUMERATED) {
        size_t at = 0u;
        for (size_t i = 0; i < control->valid_value_count; ++i)
            if (control->valid_values[i] == value) at = i;
        if (direction > 0 && at + 1u < control->valid_value_count) ++at;
        if (direction < 0 && at > 0u) --at;
        value = control->valid_values[at];
    } else {
        step = control->step > 0.0f ? control->step :
               (control->maximum - control->minimum) * (coarse ? 0.1f : 0.02f);
        if (coarse && control->step > 0.0f) step *= 4.0f;
        value += direction > 0 ? step : -step;
    }
    ts_cdp_recipe_set_control(recipe, &ui->transform_values, (size_t)index, value);
    mark_transform_stale(device, audio, ui, controller,
                         "CONTROL CHANGED - RENDER AGAIN");
}

static void set_transform_control_from_x(SDL_AudioDeviceID device, AudioState *audio,
                                         TsUiState *ui, TsInstrument *instrument,
                                         TransformController *controller,
                                         int index, int x)
{
    if (ui->transform_backend == TS_TRANSFORM_BACKEND_DSP) {
        const TsDspRecipe *recipe = ui->transform_dsp_slot >= 0 ?
            ts_dsp_factory_recipe_at((size_t)ui->transform_dsp_slot) : NULL;
        int left = 20 + index * 150;
        float normalized;
        if (recipe == NULL || index < 0 ||
            (size_t)index >= recipe->control_count) return;
        normalized = (float)(x - left) / 140.0f;
        if (!ts_dsp_recipe_set_control(recipe, &ui->transform_dsp_values,
                                       (size_t)index, normalized)) return;
        mark_transform_stale(device, audio, ui, controller,
                             "DSP CONTROL CHANGED - PREVIEW QUEUED");
        request_transform_render(device, audio, ui, instrument, controller);
        return;
    }
    const TsCdpRecipe *recipe = active_transform_recipe(ui);
    const TsCdpControlSpec *control;
    int left = 20 + index * 150;
    float normalized = (float)(x - left) / 140.0f;
    float value;
    if (recipe == NULL || index < 0 || (size_t)index >= recipe->control_count) return;
    control = &recipe->controls[index];
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    if (control->type == TS_CDP_CONTROL_ENUMERATED) {
        size_t at = (size_t)lrintf(normalized *
                    (float)(control->valid_value_count - 1u));
        value = control->valid_values[at];
    } else value = control->minimum + normalized *
                   (control-…64120 tokens truncated…            size_t selection_frames = ui.drone_source_last - ui.drone_source_first;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                delta_x = x - ui.drone_crossfade_drag_start_x;
                delta_frames = (int64_t)delta_x * (int64_t)selection_frames /
                               TS_DRONE_WAVE_W;
                if (ui.drone_crossfade_dragging == 1) delta_frames = -delta_frames;
                requested = (int64_t)ui.drone_crossfade_drag_start_frames +
                            delta_frames;
                if (requested < 1) requested = 1;
                (void)adjust_drone_crossfade(
                    device, &audio, &ui, &instrument, &drone_preview,
                    (size_t)requested, ui.drone_crossfade_dragging,
                    requested > (int64_t)ui.drone_overlap_frames ? 1 :
                    requested < (int64_t)ui.drone_overlap_frames ? -1 : 0,
                    obtained.freq);
            } else if (event.type == SDL_MOUSEMOTION &&
                       (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||
                        ui.config_open || ui.palette_open || ui.export_choice_open ||
                        ui.exchange_dialog != TS_UI_EXCHANGE_NONE ||
                        ui.load_selection_choice_open ||
                        ui.fm_open ||
                        ui.transform_open ||
                        ui.drone_open ||
                        ui.exit_confirm_open)) {
                /* Modal dialogs own pointer input. */
            } else if (event.type == SDL_MOUSEMOTION && ui.browser.dragging_scrollbar) {
                int x, y;
                int maximum = ui.browser.entry_count - TS_BROWSER_VISIBLE_ROWS;
                int thumb_h = maximum <= 0 ? TS_BROWSER_SCROLL_H :
                              TS_BROWSER_SCROLL_H * TS_BROWSER_VISIBLE_ROWS /
                              ui.browser.entry_count;
                int travel;
                int thumb_top;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)x;
                if (thumb_h < 18) thumb_h = 18;
                travel = TS_BROWSER_SCROLL_H - thumb_h;
                thumb_top = y - ui.browser.scrollbar_drag_offset - TS_BROWSER_LIST_Y;
                if (thumb_top < 0) thumb_top = 0;
                if (thumb_top > travel) thumb_top = travel;
                ts_browser_set_scroll(&ui.browser, travel > 0 ?
                                      (thumb_top * maximum + travel / 2) / travel : 0);
            } else if (event.type == SDL_MOUSEMOTION && ui.tape_dragging) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                update_tape_drag(&ui, &instrument, x - TS_WAVE_X);
            } else if (event.type == SDL_MOUSEMOTION && ui.dragging_loop_endpoint) {
                int x, y;
                size_t frame;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                if (ui.bank_view_slot >= 0 && ui.bank_view_slot < TS_BANK_SLOT_COUNT &&
                    instrument.bank[ui.bank_view_slot].occupied) {
                    TsBankSlot *slot = &instrument.bank[ui.bank_view_slot];
                    frame = bank_frame_from_x(slot, x - TS_WAVE_X);
                    ui.dragging_loop_endpoint = ts_instrument_bank_move_loop_endpoint(
                        &instrument, ui.bank_view_slot,
                        ui.dragging_loop_endpoint, frame);
                    sync_playing_loop(device, &audio, &instrument);
                    snprintf(ui.status, sizeof(ui.status),
                             "BANK %02d LOOP FLAGS %zu - %zu ZERO SNAPPED",
                             ui.bank_view_slot + 1, slot->loop_first, slot->loop_last);
                } else {
                    frame = selection_frame_from_x(&instrument, &ui, x - TS_WAVE_X);
                    if (!ui.loop_drag_started) {
                        ts_instrument_begin_loop_drag(&instrument);
                        ui.loop_drag_started = 1;
                    }
                    ui.dragging_loop_endpoint = ts_instrument_move_loop_endpoint(
                        &instrument, ui.dragging_loop_endpoint, frame);
                    sync_playing_loop(device, &audio, &instrument);
                    snprintf(ui.status, sizeof(ui.status),
                             "LOOP FLAGS %zu - %zu ZERO SNAPPED",
                             instrument.loop_first, instrument.loop_last);
                }
            } else if (event.type == SDL_MOUSEMOTION && ui.wave_pointer_pending) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                if (abs(x - ui.wave_pointer_start_x) >= 2) {
                    size_t at = selection_frame_from_x(&instrument, &ui,
                                                       x - TS_WAVE_X);
                    ui.selecting = 1;
                    ui.selecting_button = ui.wave_pointer_button;
                    ui.wave_pointer_pending = 0;
                    ts_instrument_set_selection_snapped(
                        &instrument, ui.selection_anchor, at);
                    if (ui.selecting_button == SDL_BUTTON_RIGHT &&
                        instrument.has_selection)
                        ts_instrument_set_playhead(
                            &instrument, ts_ui_right_drag_playhead_frame(
                                ui.selection_anchor, at,
                                instrument.selection_first,
                                instrument.selection_last,
                                instrument.current.frames));
                    snprintf(ui.status, sizeof(ui.status),
                             ui.selecting_button == SDL_BUTTON_RIGHT ?
                             "RIGHT-DRAG SELECTION - PLAYHEAD AT START EDGE" :
                             (instrument.grid_snap == TS_GRID_SNAP_ALL ?
                              "SELECTING TILE - GRID + ZERO SNAP" :
                              "SELECTING TILE - ZERO SNAP"));
                }
            } else if (event.type == SDL_MOUSEMOTION && ui.selecting) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                size_t at = selection_frame_from_x(&instrument, &ui, x - TS_WAVE_X);
                ts_instrument_set_selection_snapped(&instrument, ui.selection_anchor, at);
                if (ui.selecting_button == SDL_BUTTON_RIGHT && instrument.has_selection)
                    ts_instrument_set_playhead(
                        &instrument, ts_ui_right_drag_playhead_frame(
                            ui.selection_anchor, at,
                            instrument.selection_first,
                            instrument.selection_last,
                            instrument.current.frames));
            } else if (event.type == SDL_MOUSEBUTTONDOWN && ui.canvas_gesture.active) {
                snprintf(ui.status, sizeof(ui.status),
                         "RELEASE MOUSE TO COMMIT CANVAS - ESC CANCELS");
                continue;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && ui.stretch_gesture.active) {
                end_stretch_gesture(device, &audio, &ui, &instrument, 0);
                continue;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && ui.smear_gesture.active) {
                end_smear_gesture(device, &audio, &ui, &instrument, 1);
                continue;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && ui.warp_gesture.active) {
                end_warp_gesture(device, &audio, &ui, &instrument, 1);
                continue;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && ui.tear_gesture.active) {
                end_tear_gesture(device, &audio, &ui, &instrument, 1);
                continue;
            } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                       event.button.button == SDL_BUTTON_MIDDLE &&
                       !ui.config_open && !ui.palette_open &&
                       ui.exchange_dialog == TS_UI_EXCHANGE_NONE &&
                       !ui.load_selection_choice_open &&
                       !ui.fm_open &&
                       !ui.transform_open &&
                       !ui.drone_open &&
                       ui.browser.mode == TS_BROWSER_CLOSED) {
                int x, y;
                int recipe_slot;
                int cdp_slot;
                logical_mouse(window, event.button.x, event.button.y, &x, &y);
                recipe_slot = ui.show_ingredients ?
                              ts_ui_recipe_slot_from_point(x, y) : -1;
                cdp_slot = ui.show_recipes ? ts_ui_cdp_slot_from_point(x, y) : -1;
                ui.bank_clear_armed = 0;
                if (ui.exit_confirm_open || ui.renaming_bank_slot >= 0 ||
                    ui.renaming_recipe_slot >= 0 || ui.export_choice_open ||
                    ui.exchange_dialog != TS_UI_EXCHANGE_NONE) {
                    snprintf(ui.status, sizeof(ui.status),
                             "FINISH OR CANCEL THE OPEN DIALOG FIRST");
                } else if (recipe_slot >= 0) {
                    int recipe_index = ui.dsp_page * TS_DSP_BANK_SLOT_COUNT +
                                       recipe_slot;
                    if (begin_dsp_transform_workspace(
                            &ui, &instrument, &transform, recipe_index))
                        request_transform_render(device, &audio, &ui,
                                                 &instrument, &transform);
                } else if (cdp_slot >= 0) {
                    int recipe_index = ui.cdp_page * TS_CDP_BANK_SLOT_COUNT + cdp_slot;
                    begin_transform_workspace(&ui, &instrument, &transform,
                                              recipe_index);
                } else if (ui.input_meter_active &&
                           x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    snprintf(ui.status, sizeof(ui.status),
                             "LIVE INPUT DISPLAY IS READ-ONLY WHILE REC IS ARMED");
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    cancel_pitch_preview(device, &audio, &ui, &instrument);
                    ui.bank_view_slot = -1;
                    ui.selecting = 0;
                    ui.selecting_button = 0;
                    ui.wave_pointer_pending = 0;
                    ui.wave_pointer_button = 0;
                    ui.has_stretch_readout = 0;
                    if (ts_instrument_reset_selection_playhead(&instrument))
                        snprintf(ui.status, sizeof(ui.status),
                                 "SELECTION CLEARED - PLAYHEAD AT START");
                    else snprintf(ui.status, sizeof(ui.status),
                                  "PLAYHEAD ALREADY AT START");
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                SDL_Keymod mod = SDL_GetModState();
                TsUiWaveAction wave_action;
                TsUiCanvasAction canvas_action;
                int canvas_edge;
                logical_mouse(window, event.button.x, event.button.y, &x, &y);
                wave_action = ts_ui_wave_action_from_point(x, y);
                canvas_action = ts_ui_canvas_action_from_point(x, y);
                canvas_edge = ts_ui_canvas_edge_from_point(x, y);
                if (!(wave_action == TS_UI_WAVE_ACTION_CLEAR_ALL &&
                      !ui.show_keyboard && !ui.show_recipes &&
                      !ui.show_ingredients))
                    ui.bank_clear_armed = 0;
                if (ui.exit_confirm_open) {
                    if (x >= 172 && x < 308 && y >= 188 && y < 211) {
                        running = 0;
                    } else if (x >= 324 && x < 468 && y >= 188 && y < 211) {
                        ui.exit_confirm_open = 0;
                        snprintf(ui.status, sizeof(ui.status), "EXIT CANCELLED");
                    }
                } else if (ui.fm_open) {
                    if (ui.fm_bank_choice_open) {
                        TsUiFmAction bank_action =
                            ts_ui_fm_bank_action_from_point(x, y);
                        if (bank_action == TS_UI_FM_ACTION_BANK_REPLACE)
                            make_fm_bank_workspace(
                                device, &audio, &ui, &instrument,
                                &sample_pages, &fm_bank_history, 0);
                        else if (bank_action == TS_UI_FM_ACTION_BANK_NEW_PAGE)
                            make_fm_bank_workspace(
                                device, &audio, &ui, &instrument,
                                &sample_pages, &fm_bank_history, 1);
                        else if (bank_action == TS_UI_FM_ACTION_BANK_CANCEL) {
                            ui.fm_bank_choice_open = 0;
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "FM BANK MAKER CANCELLED");
                        } else
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "CHOOSE REPLACE PAGE, NEW SAMPLE PAGE, OR CANCEL");
                        continue;
                    }
                    if (ui.fm_full_choice_open) {
                        TsUiFmAction full_action =
                            ts_ui_fm_full_action_from_point(x, y);
                        if (full_action == TS_UI_FM_ACTION_OVERWRITE)
                            apply_fm_workspace(device, &audio, &ui, &instrument,
                                               &sample_pages, 1);
                        else if (full_action == TS_UI_FM_ACTION_NEW_PAGE)
                            apply_fm_workspace(device, &audio, &ui, &instrument,
                                               &sample_pages, 2);
                        else if (full_action == TS_UI_FM_ACTION_CANCEL_FULL) {
                            ui.fm_full_choice_open = 0;
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "FM APPLY CANCELLED - TILES UNCHANGED");
                        } else
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "CHOOSE OVERWRITE, NEW SAMPLE PAGE, OR CANCEL");
                        continue;
                    }
                    TsFmPage page = ts_ui_fm_page_from_point(x, y);
                    int control = ts_ui_fm_control_from_point(x, y);
                    int voice = ts_ui_fm_voice_from_point(x, y);
                    uint32_t mutation = ui.fm_page == TS_FM_PAGE_PITCH ? 0u :
                                        ts_ui_fm_mutation_from_point(x, y);
                    TsUiFmAction fm_action = ts_ui_fm_action_from_point(x, y);
                    if ((int)page >= 0) {
                        ui.fm_page = page;
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 "%s PAGE - SAME SIX CONTROLS",
                                 ts_fm_page_name(page));
                    } else if (control >= 0) {
                        float amount = (float)(x - (20 + control * 100)) / 94.0f;
                        if (fm_control_disabled(&ui.fm_patch, ui.fm_page,
                                                control))
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "ENVELOPE CONTROL DISABLED IN DRONE MODE");
                        else if (ts_fm_set_control_normalized(
                                     &ui.fm_patch, ui.fm_page, control, amount))
                            (void)render_fm_workspace(device, &audio, &ui,
                                                      &instrument, &fm_preview);
                    } else if (voice >= 0) {
                        ui.fm_patch.active_mask ^= 1u << voice;
                        (void)render_fm_workspace(device, &audio, &ui,
                                                  &instrument, &fm_preview);
                    } else if (mutation != 0u) {
                        ui.fm_patch.mutation_mask ^= mutation;
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 "MUTATION PERMISSIONS UPDATED");
                    } else if (fm_action == TS_UI_FM_ACTION_RANDOMIZE) {
                        randomize_fm_workspace(device, &audio, &ui,
                                               &instrument, &fm_preview);
                    } else if (fm_action == TS_UI_FM_ACTION_BANK_MAKER) {
                        ui.fm_bank_choice_open = 1;
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 "CONFIRM 16-SOUND BANK DESTINATION");
                    } else if (ui.fm_page == TS_FM_PAGE_PITCH &&
                               fm_action == TS_UI_FM_ACTION_PITCH_LOCK) {
                        ui.fm_patch.pitch_lock = !ui.fm_patch.pitch_lock;
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 ui.fm_patch.pitch_lock ?
                                 "PITCH LOCKED - RANDOMIZE AND BANK KEEP RATIOS" :
                                 "PITCH OPEN - RANDOMIZE USES ROOT AND SCALE");
                    } else if (ui.fm_page == TS_FM_PAGE_PITCH &&
                               fm_action == TS_UI_FM_ACTION_PITCH_ROOT) {
                        ts_fm_step_pitch_root(&ui.fm_patch, 1);
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 "RANDOM PITCH ROOT UPDATED");
                    } else if (ui.fm_page == TS_FM_PAGE_PITCH &&
                               fm_action == TS_UI_FM_ACTION_PITCH_SCALE) {
                        ts_fm_step_pitch_scale(&ui.fm_patch, 1);
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 "RANDOM PITCH SCALE %s",
                                 ts_fm_pitch_scale_name(ui.fm_patch.pitch_scale));
                    } else if (ui.fm_page == TS_FM_PAGE_PITCH &&
                               fm_action == TS_UI_FM_ACTION_APPLY_PITCHES) {
                        int changed = ts_fm_apply_pitch_scale(&ui.fm_patch);
                        (void)render_fm_workspace(device, &audio, &ui,
                                                  &instrument, &fm_preview);
                        if (changed > 0)
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "APPLIED %s PITCHES TO %d ACTIVE VOICE%s",
                                     ts_fm_pitch_scale_name(
                                         ui.fm_patch.pitch_scale),
                                     changed, changed == 1 ? "" : "S");
                        else
                            snprintf(ui.fm_message, sizeof(ui.fm_message),
                                     "ACTIVE VOICE PITCHES ALREADY MATCH %s",
                                     ts_fm_pitch_scale_name(
                                         ui.fm_patch.pitch_scale));
                    } else if (fm_action == TS_UI_FM_ACTION_APPLY) {
                        apply_fm_workspace(device, &audio, &ui, &instrument,
                                           &sample_pages, 0);
                    } else if (fm_action == TS_UI_FM_ACTION_AUDITION) {
                        begin_fm_note(device, &audio, &ui, &instrument,
                                      &fm_preview, 0, obtained.freq, 0);
                    } else if (fm_action == TS_UI_FM_ACTION_HOLD) {
                        toggle_fm_hold(device, &audio, &ui);
                    } else if (fm_action == TS_UI_FM_ACTION_DRONE) {
                        ui.fm_patch.drone_mode = !ui.fm_patch.drone_mode;
                        ts_fm_patch_sanitize(&ui.fm_patch);
                        (void)render_fm_workspace(device, &audio, &ui,
                                                  &instrument, &fm_preview);
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 ui.fm_patch.drone_mode ?
                                 "DRONE ON - ENVELOPES BYPASSED, EDGES ZEROED" :
                                 "DRONE OFF - ATTACK AND RELEASE RESTORED");
                    } else if (fm_action == TS_UI_FM_ACTION_EXTREME) {
                        ui.fm_patch.extreme_mode = !ui.fm_patch.extreme_mode;
                        ts_fm_patch_sanitize(&ui.fm_patch);
                        (void)render_fm_workspace(device, &audio, &ui,
                                                  &instrument, &fm_preview);
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 ui.fm_patch.extreme_mode ?
                                 "EXTREME ON - FULL SYNTH RANGE, LIMITER ACTIVE" :
                                 "EXTREME OFF - VALUES RETURNED TO MUSICAL RANGE");
                    } else if (fm_action == TS_UI_FM_ACTION_CHAIN) {
                        instrument.family_trajectory =
                            !instrument.family_trajectory;
                        snprintf(ui.fm_message, sizeof(ui.fm_message),
                                 instrument.family_trajectory ?
                                 "CHAIN ON - APPLY USES THE NEXT EMPTY TILE" :
                                 "CHAIN OFF - APPLY OVERWRITES THE CURRENT TILE");
                    } else if (ts_ui_fm_range_contains(x, y)) {
                        instrument.family_mutation = (float)(x - 386) / 234.0f;
                        if (instrument.family_mutation < 0.0f)
                            instrument.family_mutation = 0.0f;
                        if (instrument.family_mutation > 1.0f)
                            instrument.family_mutation = 1.0f;
                        snprintf(ui.fm_message, sizeof(ui.fm_message), "RANGE %d",
                                 (int)lrintf(instrument.family_mutation * 100.0f));
                    } else if (fm_action == TS_UI_FM_ACTION_BACK) {
                        close_fm_workspace(device, &audio, &ui, &fm_preview);
                    }
                } else if (ui.transform_open) {
                    TsUiTransformAction action =
                        ts_ui_transform_action_from_point(x, y);
                    int control = ts_ui_transform_control_from_point(x, y);
                    int bank_slot = !ui.show_keyboard && !ui.show_recipes &&
                                    !ui.show_ingredients ?
                                    ts_ui_bank_slot_from_point(x, y) : -1;
                    if (bank_slot >= 0) {
                        char select_error[160];
                        int selected;
                        mark_transform_stale(device, &audio, &ui, &transform,
                                             "TILE CHANGED - RENDER AGAIN");
                        lock_edit(device, &audio);
                        selected = ts_ui_execute_bank_action(
                            &instrument, bank_slot, TS_UI_BANK_ACTION_AUDITION,
                            select_error, sizeof(select_error));
                        unlock_edit(device, &audio, &ui, &instrument);
                        if (selected) {
                            ui.transform_scope = instrument.has_selection ?
                                                 TS_TRANSFORM_SELECTION :
                                                 TS_TRANSFORM_WHOLE;
                            snprintf(ui.transform_message,
                                     sizeof(ui.transform_message),
                                     instrument.current.data != NULL ?
                                     "TILE %02d READY - SELECTION IS TILE LOCAL" :
                                     "TILE %02d EMPTY - CHOOSE AN OCCUPIED TILE",
                                     bank_slot + 1);
                        } else snprintf(ui.transform_message,
                                        sizeof(ui.transform_message),
                                        "TILE SWITCH FAILED: %.68s", select_error);
                    } else if (ts_ui_transform_waveform_contains(x, y)) {
                        begin_transform_selection_drag(
                            device, &audio, &ui, &instrument, &transform, x);
                    } else if (control >= 0) {
                        set_transform_control_from_x(
                            device, &audio, &ui, &instrument, &transform, control, x);
                    } else if (ts_ui_transform_mix_contains(x, y)) {
                        const TsCdpRecipe *recipe = ui.transform_backend ==
                                                    TS_TRANSFORM_BACKEND_CDP ?
                                                    active_transform_recipe(&ui) : NULL;
                        if (recipe == NULL) {
                            snprintf(ui.transform_message,
                                     sizeof(ui.transform_message),
                                     "DSP PREVIEW UPDATES FROM THE FOUR MACROS");
                        } else if (recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED) {
                            snprintf(ui.transform_message,
                                     sizeof(ui.transform_message),
                                     "MIX DISABLED - %s USES NATURAL LENGTH",
                                     recipe->display_name);
                        } else {
                            ui.transform_values.mix = (float)(x - 20) / 112.0f;
                            if (ui.transform_values.mix < 0.0f)
                                ui.transform_values.mix = 0.0f;
                            if (ui.transform_values.mix > 1.0f)
                                ui.transform_values.mix = 1.0f;
                            mark_transform_stale(device, &audio, &ui, &transform,
                                                 "MIX CHANGED - RENDER AGAIN");
                        }
                    } else handle_transform_action(
                        device, &audio, &ui, &instrument, &transform,
                        action, obtained.freq);
                } else if (ui.drone_open) {
                    TsUiDroneAction action = ts_ui_drone_action_from_point(x, y);
                    int handle = ts_ui_drone_crossfade_handle_from_point(&ui, x, y);
                    if (handle != 0) {
                        ui.drone_crossfade_dragging = handle;
                        ui.drone_crossfade_drag_start_x = x;
                        ui.drone_crossfade_drag_start_frames =
                            ui.drone_overlap_frames;
                        snprintf(ui.status, sizeof(ui.status),
                                 "DRAG %s CROSSFADE EDGE - ZERO SNAPPED",
                                 handle == 1 ? "LEFT" : "RIGHT");
                    } else if (action == TS_UI_DRONE_ACTION_PREVIEW)
                        preview_drone(device, &audio, &ui, &instrument,
                                      &drone_preview, obtained.freq);
                    else if (action == TS_UI_DRONE_ACTION_STOP) {
                        stop_drone_preview(device, &audio, &ui, &drone_preview);
                        snprintf(ui.status, sizeof(ui.status), "DRONE PREVIEW STOPPED");
                    } else if (action == TS_UI_DRONE_ACTION_COPY)
                        commit_drone(device, &audio, &ui, &instrument,
                                     &drone_preview, 1);
                    else if (action == TS_UI_DRONE_ACTION_REPLACE)
                        commit_drone(device, &audio, &ui, &instrument,
                                     &drone_preview, 0);
                    else if (action == TS_UI_DRONE_ACTION_CANCEL) {
                        close_drone_dialog(device, &audio, &ui, &drone_preview);
                        snprintf(ui.status, sizeof(ui.status),
                                 "DRONE CANCELLED - SOURCE UNCHANGED");
                    }
                } else if (ui.exchange_dialog != TS_UI_EXCHANGE_NONE) {
                    TsUiExchangeDialog dialog = ui.exchange_dialog;
                    TsUiExchangeAction action =
                        ts_ui_exchange_action_from_point(dialog, x, y);
                    if (action == TS_UI_EXCHANGE_ACTION_SEND_ONE_INSTRUMENT) {
                        ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                        send_to_fasttracker(
                            &ui, &instrument,
                            TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES);
                    } else if (action ==
                               TS_UI_EXCHANGE_ACTION_SEND_SEPARATE_INSTRUMENTS) {
                        ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                        send_to_fasttracker(
                            &ui, &instrument,
                            TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS);
                    } else if (action ==
                               TS_UI_EXCHANGE_ACTION_SEND_ALL_PAGES) {
                        ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                        send_pages_to_fasttracker(
                            &ui, &sample_pages, &instrument);
                    } else if (action == TS_UI_EXCHANGE_ACTION_CHECK_INBOX) {
                        ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                        (void)stage_incoming_exchange(
                            &ui, &exchange_offer, ignored_exchange, 1);
                    } else if (action ==
                               TS_UI_EXCHANGE_ACTION_TOGGLE_NEW_INSTANCE) {
                        ui.exchange_force_new_instance =
                            !ui.exchange_force_new_instance;
                        snprintf(ui.status, sizeof(ui.status),
                                 ui.exchange_force_new_instance ?
                                 "NEXT SEND WILL LAUNCH A NEW TAPEHEAD INSTANCE" :
                                 "NEXT SEND WILL REUSE AN OPEN TAPEHEAD");
                    } else if (action == TS_UI_EXCHANGE_ACTION_IMPORT) {
                        ignored_exchange[0] = '\0';
                        (void)import_incoming_exchange(
                            device, &audio, &ui, &instrument, &exchange_offer);
                    } else if (action == TS_UI_EXCHANGE_ACTION_LATER) {
                        if (dialog == TS_UI_EXCHANGE_RECEIVE)
                            snprintf(ignored_exchange, sizeof(ignored_exchange), "%s",
                                     exchange_offer.folder);
                        ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                        snprintf(ui.status, sizeof(ui.status),
                                 dialog == TS_UI_EXCHANGE_RECEIVE ?
                                 "TAPEHEAD TRANSFER LEFT IN INBOX FOR LATER" :
                                 "FT2 LINK CANCELLED");
                    }
                } else if (ui.load_selection_choice_open) {
                    TsUiLoadSelectionAction action =
                        ts_ui_load_selection_action_from_point(x, y);
                    if (action == TS_UI_LOAD_SELECTION_PASTE)
                        apply_selection_load(device, &audio, &ui, &instrument,
                                             &pending_selection_load, 0);
                    else if (action == TS_UI_LOAD_SELECTION_FIT)
                        apply_selection_load(device, &audio, &ui, &instrument,
                                             &pending_selection_load, 1);
                    else if (action == TS_UI_LOAD_SELECTION_CANCEL)
                        cancel_selection_load(&ui, &pending_selection_load);
                } else if (ui.palette_open) {
                    int value = -1;
                    int selected = ts_ui_palette_entry_from_point(x, y);
                    int channel = ts_ui_palette_channel_from_point(x, y, &value);
                    TsUiPaletteAction action = ts_ui_palette_action_from_point(x, y);
                    if (selected >= 0) ui.palette_entry = selected;
                    else if (channel >= 0) {
                        ui.palette_channel = channel;
                        if (value >= 0 && channel < 3)
                            ts_palette_set_component(&ui.palette,
                                (TsPaletteColor)ui.palette_entry, channel,
                                (uint8_t)value);
                        else if (value >= 0 && channel == 3)
                            ui.palette.desktop_contrast = value;
                        else if (value >= 0)
                            ui.palette.buttons_contrast = value;
                    } else if (action == TS_UI_PALETTE_ACTION_IMPORT_TAPEHEAD)
                        palette_import(&ui);
                    else if (action == TS_UI_PALETTE_ACTION_SAVE_TAPESISTER)
                        palette_save(&ui, 0);
                    else if (action == TS_UI_PALETTE_ACTION_EXPORT_TAPEHEAD)
                        palette_save(&ui, 1);
                    else if (action == TS_UI_PALETTE_ACTION_RESET) {
                        ts_palette_default(&ui.palette);
                        snprintf(ui.status, sizeof(ui.status),
                                 "RESTORED FACTORY TAPESISTER PALETTE");
                    } else if (action == TS_UI_PALETTE_ACTION_DONE)
                        finish_palette(&ui, 0);
                    else if (action == TS_UI_PALETTE_ACTION_CANCEL)
                        finish_palette(&ui, 1);
                } else if (ui.config_open) {
                    int selected = ts_ui_config_field_from_point(x, y);
                    TsUiConfigAction action = ts_ui_config_action_from_point(x, y);
                    if (selected >= 0) {
                        if (selected != (int)ui.config_field)
                            select_config_field(&ui, (TsConfigField)selected);
                        ui.config_cursor = ts_ui_config_cursor_from_point(
                                               &ui, ui.config_field, x);
                        if (event.button.clicks >= 2)
                            browser_open_config_path(&ui, ui.config_field);
                    } else if (action == TS_UI_CONFIG_ACTION_SAVE)
                        save_config(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_USE_CWD)
                        config_use_cwd(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_PALETTE)
                        begin_palette(&ui);
                    else if (action == TS_UI_CONFIG_ACTION_CANCEL)
                        cancel_config(&ui);
                } else if (ui.renaming_bank_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "FINISH BANK NAME WITH ENTER OR CANCEL WITH ESC");
                } else if (ui.renaming_recipe_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "FINISH RECIPE NAME WITH ENTER OR CANCEL WITH ESC");
                } else if (ui.export_choice_open) {
                    if (x >= 172 && x < 308 && y >= 176 && y < 199) {
                        ui.export_choice_open = 0;
                        browser_open(&ui, TS_BROWSER_EXPORT_WAV);
                    } else if (x >= 324 && x < 468 && y >= 176 && y < 199) {
                        ui.export_choice_open = 0;
                        browser_open_bank(&ui, &instrument);
                    }
                } else if (ui.browser.mode != TS_BROWSER_CLOSED) {
                    if (x >= TS_BROWSER_LIST_X && x < TS_BROWSER_LIST_X + TS_BROWSER_LIST_W &&
                        y >= TS_BROWSER_LIST_Y && y < TS_BROWSER_LIST_Y + TS_BROWSER_SCROLL_H) {
                        int row = (y - TS_BROWSER_LIST_Y) / TS_BROWSER_ROW_H;
                        int index = ui.browser.scroll + row;
                        ts_browser_select(&ui.browser, index);
                        ui.browser.filename_focus = 0;
                        if (event.button.clicks >= 2)
                            browser_activate_selection(device, &audio, &ui, &instrument,
                                                       &pending_selection_load,
                                                       &sample_pages, parked_instrument,
                                                       record_bank_active);
                    } else if (x >= TS_BROWSER_SCROLL_X &&
                               x < TS_BROWSER_SCROLL_X + TS_BROWSER_SCROLL_W &&
                               y >= TS_BROWSER_LIST_Y &&
                               y < TS_BROWSER_LIST_Y + TS_BROWSER_SCROLL_H) {
                        int maximum = ui.browser.entry_count - TS_BROWSER_VISIBLE_ROWS;
                        int thumb_h = maximum <= 0 ? TS_BROWSER_SCROLL_H :
                                      TS_BROWSER_SCROLL_H * TS_BROWSER_VISIBLE_ROWS /
                                      ui.browser.entry_count;
                        int travel;
                        int thumb_y;
                        if (thumb_h < 18) thumb_h = 18;
                        travel = TS_BROWSER_SCROLL_H - thumb_h;
                        thumb_y = TS_BROWSER_LIST_Y + (maximum > 0 ?
                                  ui.browser.scroll * travel / maximum : 0);
                        ui.browser.dragging_scrollbar = 1;
                        ui.browser.scrollbar_drag_offset = y >= thumb_y && y < thumb_y + thumb_h ?
                                                           y - thumb_y : thumb_h / 2;
                        if (!(y >= thumb_y && y < thumb_y + thumb_h)) {
                            int top = y - ui.browser.scrollbar_drag_offset - TS_BROWSER_LIST_Y;
                            if (top < 0) top = 0;
                            if (top > travel) top = travel;
                            ts_browser_set_scroll(&ui.browser, travel > 0 ?
                                                  (top * maximum + travel / 2) / travel : 0);
                        }
                    } else if (ts_browser_mode_edits_filename(ui.browser.mode) &&
                               x >= 58 && x < 576 && y >= 294 && y < 318) {
                        size_t length = strlen(ui.browser.filename);
                        size_t cursor = ui.browser.filename_cursor > length ? length :
                                        ui.browser.filename_cursor;
                        size_t first = length > 78 ? length - 78 : 0;
                        size_t clicked;
                        if (cursor < first) first = cursor;
                        if (cursor > first + 78) first = cursor - 78;
                        clicked = first + (size_t)((x - 64 + 3) / 6);
                        ts_browser_set_filename_cursor(&ui.browser, clicked);
                        ui.browser.filename_focus = 1;
                        ui.browser.overwrite_armed = 0;
                        SDL_StartTextInput();
                    } else if (x >= 58 && x < 130 && y >= 326 && y < 349) {
                        ts_browser_parent(&ui.browser);
                    } else if (x >= 135 && x < 219 && y >= 326 && y < 349 &&
                               ts_browser_mode_allows_create_directory(ui.browser.mode)) {
                        if (ui.browser.creating_directory)
                            ts_browser_cancel_create_directory(&ui.browser);
                        else (void)ts_browser_begin_create_directory(&ui.browser);
                    } else if (x >= 224 && x < 344 && y >= 326 && y < 349) {
                        browser_action(device, &audio, &ui, &instrument,
                                       &pending_selection_load,
                                       &sample_pages, parked_instrument,
                                       record_bank_active);
                    } else if (x >= 349 && x < 433 && y >= 326 && y < 349) {
                        browser_cancel(&ui);
                    }
                } else if (y >= 4 && y < 28 && x >= 350 && x < 426) {
                    begin_config(&ui);
                } else if (y >= 4 && y < 28 && x >= 431 && x < 511) {
                    begin_exchange_send(&ui, &instrument, &sample_pages);
                } else if (y >= 4 && y < 28 && x >= 516 && x < 568) {
                    browser_open(&ui, ui.show_ingredients ?
                                 TS_BROWSER_SAVE_PRESET : TS_BROWSER_SAVE_RECIPE);
                } else if (y >= 4 && y < 28 && x >= 573 && x < 630) {
                    begin_export_choice(&ui);
                } else if (ui.input_meter_active &&
                           x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    snprintf(ui.status, sizeof(ui.status),
                             "LIVE INPUT DISPLAY IS READ-ONLY WHILE REC IS ARMED");
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    TsBankSlot *shown_bank = ui.bank_view_slot >= 0 &&
                                             ui.bank_view_slot < TS_BANK_SLOT_COUNT &&
                                             instrument.bank[ui.bank_view_slot].occupied ?
                                             &instrument.bank[ui.bank_view_slot] : NULL;
                    int has_visible_loop = shown_bank != NULL ? shown_bank->has_loop :
                                           instrument.has_loop;
                    int first_x = has_visible_loop ?
                                  loop_marker_x(&instrument, &ui, 1) : -1000;
                    int last_x = has_visible_loop ?
                                 loop_marker_x(&instrument, &ui, 2) : -1000;
                    int editing_canvas = shown_bank == NULL &&
                                         ui.audition_source == TS_AUDITION_CURRENT &&
                                         instrument.current.data != NULL &&
                                         instrument.current.frames >= TS_CANVAS_MIN_FRAMES;
                    if (editing_canvas &&
                        canvas_action != TS_UI_CANVAS_ACTION_NONE) {
                        apply_canvas_action(device, &audio, &ui, &instrument,
                                            canvas_action);
                    } else if (editing_canvas && canvas_edge != 0 &&
                               bank_modifiers(mod) == 0) {
                        (void)begin_canvas_gesture(
                            window, device, &audio, &ui, &instrument,
                            canvas_edge, event.button.x, event.button.y);
                    } else if (event.button.clicks >= 2 &&
                               bank_modifiers(mod) == 0) {
                        select_current_tile(device, &audio, &ui, &instrument, 0);
                    } else if (shown_bank != NULL && has_visible_loop &&
                        abs(x - first_x) <= 6) {
                        cancel_pitch_preview(device, &audio, &ui, &instrument);
                        ui.dragging_loop_endpoint = 1;
                        ui.loop_drag_started = 0;
                        snprintf(ui.status, sizeof(ui.status),
                                 "DRAG BANK %02d LOOP START - ZERO SNAPPED",
                                 ui.bank_view_slot + 1);
                    } else if (shown_bank != NULL && has_visible_loop &&
                               abs(x - last_x) <= 6) {
                        cancel_pitch_preview(device, &audio, &ui, &instrument);
                        ui.dragging_loop_endpoint = 2;
                        ui.loop_drag_started = 0;
                        snprintf(ui.status, sizeof(ui.status),
                                 "DRAG BANK %02d LOOP END - ZERO SNAPPED",
                                 ui.bank_view_slot + 1);
                    } else if (shown_bank != NULL) {
                        snprintf(ui.status, sizeof(ui.status),
                                 shown_bank->has_loop ?
                                 "BANK %02d LOOP SAVED - DRAG CYAN FLAGS" :
                                 "BANK %02d IS ONE-SHOT - USE SET LOOP",
                                 ui.bank_view_slot + 1);
                    } else {
                        ui.bank_view_slot = -1;
                        if (begin_tape_drag(&ui, &instrument, SDL_BUTTON_LEFT,
                                            mod, x - TS_WAVE_X)) {
                            /* Modifier drag owns this gesture. */
                        } else if (instrument.has_loop && abs(x - first_x) <= 6) {
                            cancel_pitch_preview(device, &audio, &ui, &instrument);
                            ui.dragging_loop_endpoint = 1;
                            ui.loop_drag_started = 0;
                            snprintf(ui.status, sizeof(ui.status),
                                     "DRAG LOOP START - ZERO SNAPPED");
                        } else if (instrument.has_loop && abs(x - last_x) <= 6) {
                            cancel_pitch_preview(device, &audio, &ui, &instrument);
                            ui.dragging_loop_endpoint = 2;
                            ui.loop_drag_started = 0;
                            snprintf(ui.status, sizeof(ui.status),
                                     "DRAG LOOP END - ZERO SNAPPED");
                        } else {
                            cancel_pitch_preview(device, &audio, &ui, &instrument);
                            ui.selection_anchor = selection_frame_from_x(
                                &instrument, &ui, x - TS_WAVE_X);
                            ui.wave_pointer_pending = 1;
                            ui.wave_pointer_button = SDL_BUTTON_LEFT;
                            ui.wave_pointer_start_x = x;
                            snprintf(ui.status, sizeof(ui.status),
                                     "CLICK PLACES PLAYHEAD - DRAG SELECTS");
                        }
                    }
                } else if (y >= 205 && y < 228 && x >= 10 && x < 80) {
                    ui.load_bank_slot = instrument.selected_slot;
                    browser_open(&ui, TS_BROWSER_LOAD_WAV);
                } else if (y >= 205 && y < 228 && x >= 85 && x < 167) {
                    generate_family_candidate(device, &audio, &ui, &instrument,
                                              0, (mod & KMOD_SHIFT) != 0,
                                              (mod & KMOD_CTRL) != 0);
                } else if (y >= 205 && y < 228 && x >= 172 && x < 242) {
                    generate_family_candidate(device, &audio, &ui, &instrument,
                                              1, (mod & KMOD_SHIFT) != 0, 0);
                } else if (y >= 205 && y < 228 && x >= 247 && x < 325) {
                    toggle_workbench_loop(device, &audio, &ui, &instrument,
                                          obtained.freq, (mod & KMOD_SHIFT) != 0);
                } else if (y >= 205 && y < 228 && x >= 330 && x < 402) {
                    begin_drone_dialog(device, &audio, &ui, &instrument,
                                       &drone_preview);
                } else if (y >= 205 && y < 229 && x >= 505 && x < 630) {
                    float amount = (float)(x - 505) / 125.0f;
                    if (begin_smear_gesture(device, &audio, &ui, &instrument, 0))
                        preview_smear_gesture(device, &audio, &ui, &instrument, amount, obtained.freq);
                    continue;
                } else if (y >= 205 && y < 229 && x >= 407 && x < 500) {
                    float amount = (float)(x - 407) / 93.0f;
                    if (begin_tear_gesture(device, &audio, &ui, &instrument, 0))
                        preview_tear_gesture(device, &audio, &ui, &instrument, amount, obtained.freq);
                    continue;
                } else if (y >= 233 && y < 257 && x >= 10 && x < 330) {
                    TsProcessRecipe process = instrument.process;
                    const char *label;
                    int start;
                    int width;
                    float *control;
                    if (x >= 244) {
                        float amount = (float)(x - 244) / 86.0f;
                        if (amount > 1.0f) amount = 1.0f;
                        if (begin_warp_gesture(device, &audio, &ui, &instrument, 0))
                            preview_warp_gesture(device, &audio, &ui, &instrument,
                                                 amount, obtained.freq);
                        continue;
                    } else if (x < 82) {
                        control = &process.body; start = 10; width = 72; label = "BODY";
                    } else if (x >= 88 && x < 160) {
                        control = &process.edge; start = 88; width = 72; label = "EDGE";
                    } else if (x >= 166 && x < 238) {
                        control = &process.drift; start = 166; width = 72; label = "DRIFT";
                    } else continue;
                    *control = (float)(x - start) / (float)width;
                    if (*control < 0.0f) *control = 0.0f;
                    if (*control > 1.0f) *control = 1.0f;
                    apply_process(device, &audio, &ui, &instrument, process, label);
                } else if (y >= 233 && y < 256 && x >= 335 && x < 369) {
                    ui.fx_page = TS_FX_EDIT;
                    snprintf(ui.status, sizeof(ui.status), "SAMPLE EDITING PAGE");
                } else if (y >= 233 && y < 256 && x >= 372 && x < 406) {
                    ui.fx_page = TS_FX_TUNE;
                    snprintf(ui.status, sizeof(ui.status), "ROOT NOTE AND FINE TUNING");
                } else if (y >= 233 && y < 256 && x >= 409 && x < 445) {
                    ui.fx_page = TS_FX_NOISE;
                    snprintf(ui.status, sizeof(ui.status), "NOISE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 448 && x < 486) {
                    ui.fx_page = TS_FX_SHAPE;
                    snprintf(ui.status, sizeof(ui.status), "FILTER AND SHAPER PAGE");
                } else if (y >= 233 && y < 256 && x >= 489 && x < 523) {
                    ui.fx_page = TS_FX_FAMILY;
                    snprintf(ui.status, sizeof(ui.status),
                             "VARIATION RANGE AMOUNT LOCKS AND CHAIN");
                } else if (y >= 233 && y < 256 && x >= 526 && x < 562) {
                    ui.fx_page = TS_FX_DELAY;
                    snprintf(ui.status, sizeof(ui.status), "DELAY PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 565 && x < 594) {
                    ui.fx_page = TS_FX_SPACE;
                    snprintf(ui.status, sizeof(ui.status), "SPACE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 597 && x < 630) {
                    ui.fx_page = TS_FX_LOOP;
                    snprintf(ui.status, sizeof(ui.status), "LOOP EDITING PAGE");
                } else if (y >= 261 && y < 285) {
                    TsProcessRecipe process = instrument.process;
                    int changed = 0;
                    const char *label = "DSP";
                    if (ui.fx_page == TS_FX_EDIT) {
                        if (x >= 10 && x < 65)
                            copy_selection_to_clipboard(device, &audio, &ui, &instrument,
                                                        &clipboard, &clipboard_origin_first,
                                                        &clipboard_source_frames,
                                                        &clipboard_source_rate);
                        else if (x >= 69 && x < 116)
                            cut_selection_to_clipboard(device, &audio, &ui, &instrument,
                                                       &clipboard, &clipboard_origin_first,
                                                       &clipboard_source_frames,
                                                       &clipboard_source_rate);
                        else if (x >= 120 && x < 181)
                            paste_from_clipboard(device, &audio, &ui, &instrument,
                                                 &clipboard, clipboard_origin_first, 0);
                        else if (x >= 185 && x < 232)
                            paste_from_clipboard(device, &audio, &ui, &instrument,
                                                 &clipboard, clipboard_origin_first, 1);
                        else if (x >= 236 && x < 291)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_REVERSE, 1.0f);
                        else if (x >= 295 && x < 360)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_NORMALIZE, 0.98f);
                        else if (x >= 364 && x < 419)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_GAIN, 0.7079458f);
                        else if (x >= 423 && x < 478)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_GAIN, 1.4125376f);
                        else if (x >= 482 && x < 551)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_FADE_IN, 1.0f);
                        else if (x >= 555 && x < 630)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_FADE_OUT, 1.0f);
                    } else if (ui.fx_page == TS_FX_TUNE) {
                        if (x >= 10 && x < 50 &&
                            ui.tune_reference.root_note > 0)
                            set_tune_reference(
                                &ui, ui.tune_reference.root_note - 1,
                                ui.tune_reference.fine_tune_cents);
                        else if (x >= 134 && x < 174 &&
                                 ui.tune_reference.root_note < 127)
                            set_tune_reference(
                                &ui, ui.tune_reference.root_note + 1,
                                ui.tune_reference.fine_tune_cents);
                        else if (x >= 180 && x < 292)
                            set_tune_reference(
                                &ui, ui.tune_reference.root_note,
                                (float)(x - 180) / 112.0f * 200.0f - 100.0f);
                        else if (x >= 298 && x < 380)
                            toggle_tune_reference(device, &audio, &ui,
                                                  obtained.freq);
                        else if (x >= 386 && x < 466)
                            set_tune_reference_volume(
                                &ui, (float)(x - 386) / 80.0f);
                        else if (x >= 472 && x < 630)
                            suggest_or_accept_pitch(device, &audio, &ui, &instrument);
                    } else if (ui.fx_page == TS_FX_NOISE) {
                        label = "NOISE";
                        if (x >= 10 && x < 104) { process.noise_enabled = !process.noise_enabled; changed = 1; }
                        else if (x >= 118 && x < 298) {
                            process.noise_amount = (float)(x - 118) / 180.0f; changed = 1;
                        } else if (x >= 312 && x < 462) {
                            process.noise_color = (TsNoiseColor)((process.noise_color + 1) % TS_NOISE_COLOR_COUNT);
                            changed = 1;
                        }
                    } else if (ui.fx_page == TS_FX_SHAPE) {
                        label = "SHAPE";
                        if (x >= 10 && x < 100) {
                            if (!process.filter_enabled) {
                                process.filter_enabled = 1;
                                process.filter_mode = TS_FILTER_LOWPASS;
                            } else if (process.filter_mode + 1 < TS_FILTER_MODE_COUNT) {
                                process.filter_mode = (TsFilterMode)(process.filter_mode + 1);
                            } else process.filter_enabled = 0;
                            changed = 1;
                        } else if (x >= 104 && x < 198) {
                            float normalized = (float)(x - 104) / 94.0f;
                            process.filter_cutoff_hz = 20.0f * powf(1000.0f, normalized);
                            changed = 1;
                        } else if (x >= 202 && x < 282) {
                            process.filter_resonance = (float)(x - 202) / 80.0f;
                            changed = 1;
                        } else if (x >= 286 && x < 380) {
                            if (!process.shaper_enabled) {
                                process.shaper_enabled = 1;
                                process.shaper_mode = TS_SHAPER_TAPE;
                            } else if (process.shaper_mode + 1 < TS_SHAPER_MODE_COUNT) {
                                process.shaper_mode = (TsShaperMode)(process.shaper_mode + 1);
                            } else process.shaper_enabled = 0;
                            changed = 1;
                        } else if (x >= 384 && x < 476) {
                            process.shaper_drive = 1.0f + (float)(x - 384) / 92.0f * 15.0f;
                            changed = 1;
                        } else if (x >= 480 && x < 572) {
                            process.shaper_mix = (float)(x - 480) / 92.0f;
                            changed = 1;
                        }
                    } else if (ui.fx_page == TS_FX_FAMILY) {
                        if (x >= 10 && x < 390) {
                            instrument.family_mutation = (float)(x - 10) / 380.0f;
                            snprintf(ui.status, sizeof(ui.status), "RANGE %d",
                                     (int)lrintf(instrument.family_mutation * 100.0f));
                        } else if (x >= 398 && x < 498) {
                            instrument.family_trajectory = !instrument.family_trajectory;
                            snprintf(ui.status, sizeof(ui.status), instrument.family_trajectory ?
                                     "CHAIN ON" : "CHAIN OFF");
                        } else if (ts_ui_fm_button_from_point(x, y)) {
                            begin_fm_workspace(device, &audio, &ui, &instrument,
                                               &fm_preview);
                        }
                    } else if (ui.fx_page == TS_FX_DELAY) {
                        label = "DELAY";
                        if (x >= 10 && x < 104) { process.delay_enabled = !process.delay_enabled; changed = 1; }
                        else if (x >= 118 && x < 210) {
                            process.delay_seconds = 0.005f + (float)(x - 118) / 92.0f * 0.995f; changed = 1;
                        } else if (x >= 220 && x < 312) {
                            process.delay_feedback = (float)(x - 220) / 92.0f * 0.85f; changed = 1;
                        } else if (x >= 322 && x < 414) {
                            process.delay_damping = (float)(x - 322) / 92.0f; changed = 1;
                        } else if (x >= 424 && x < 516) {
                            process.delay_mix = (float)(x - 424) / 92.0f; changed = 1;
                        }
                    } else if (ui.fx_page == TS_FX_SPACE) {
                        label = "SPACE";
                        if (x >= 10 && x < 104) { process.reverb_enabled = !process.reverb_enabled; changed = 1; }
                        else if (x >= 118 && x < 238) {
                            process.reverb_decay = (float)(x - 118) / 120.0f * 0.9f; changed = 1;
                        } else if (x >= 250 && x < 370) {
                            process.reverb_damping = (float)(x - 250) / 120.0f; changed = 1;
                        } else if (x >= 382 && x < 502) {
                            process.reverb_mix = (float)(x - 382) / 120.0f; changed = 1;
                        }
                    } else {
                        if (x >= 10 && x < 84)
                            set_loop(device, &audio, &ui, &instrument);
                        else if (x >= 89 && x < 153)
                            clear_loop(device, &audio, &ui, &instrument);
                        else if (x >= 158 && x < 242) {
                            int slot = ui.bank_view_slot;
                            if (slot >= 0 && slot < TS_BANK_SLOT_COUNT &&
                                instrument.bank[slot].occupied) {
                                if (instrument.bank[slot].has_loop)
                                    begin_bank_audition(device, &audio, &ui,
                                                        &instrument, slot,
                                                        obtained.freq);
                                else snprintf(ui.status, sizeof(ui.status),
                                              "BANK %02d HAS NO LOOP - USE SET LOOP",
                                              slot + 1);
                            } else begin_audition(device, &audio, &ui, &instrument,
                                                  TS_AUDITION_LOOP, 1.0,
                                                  obtained.freq);
                        }
                        else if (x >= 247 && x < 355)
                            cycle_loop_mode(device, &audio, &ui, &instrument);
                        else if (x >= 365 && x < 577)
                            set_loop_crossfade(device, &audio, &ui, &instrument,
                                               (float)(x - 365) / 212.0f * 50.0f);
                    }
                    if (changed) apply_process(device, &audio, &ui, &instrument, process, label);
                } else if (wave_action == TS_UI_WAVE_ACTION_PLAY_ALL) {
                    begin_audition(device, &audio, &ui, &instrument,
                                   TS_AUDITION_ALL, 1.0, obtained.freq);
                } else if (wave_action == TS_UI_WAVE_ACTION_PLAY_SELECTION) {
                    begin_audition(device, &audio, &ui, &instrument,
                                   TS_AUDITION_SELECTION, 1.0, obtained.freq);
                } else if (wave_action == TS_UI_WAVE_ACTION_PLAY_VIEW) {
                    begin_audition(device, &audio, &ui, &instrument,
                                   TS_AUDITION_DISPLAYED, 1.0, obtained.freq);
                } else if (wave_action == TS_UI_WAVE_ACTION_CROP) {
                    ui.bank_clear_armed = 0;
                    crop_selection(device, &audio, &ui, &instrument);
                } else if (wave_action == TS_UI_WAVE_ACTION_ZOOM_SELECTION) {
                    ui.bank_clear_armed = 0;
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT &&
                        instrument.has_selection) {
                        ui.parent_view_first = instrument.crop_first +
                                               instrument.selection_first;
                        ui.parent_view_last = instrument.crop_first +
                                              instrument.selection_last;
                        snprintf(ui.status, sizeof(ui.status), "ZOOMED SOURCE TO SELECTION");
                    } else {
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_instrument_zoom_selection(&instrument) ?
                                 "ZOOMED TO SELECTION" : "SELECT A RANGE FIRST");
                    }
                } else if (wave_action == TS_UI_WAVE_ACTION_SELECT_ALL) {
                    select_current_tile(device, &audio, &ui, &instrument, 0);
                } else if (wave_action == TS_UI_WAVE_ACTION_SELECT_WAVE) {
                    select_current_tile(device, &audio, &ui, &instrument, 1);
                } else if (wave_action == TS_UI_WAVE_ACTION_SHOW_ALL) {
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT) {
                        ts_ui_reset_parent_view(&ui, instrument.parent.frames);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL SOURCE");
                    } else {
                        ts_instrument_show_all(&instrument);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL TILE");
                    }
                } else if (wave_action == TS_UI_WAVE_ACTION_CLEAR_ALL &&
                           !ui.show_keyboard && !ui.show_recipes &&
                           !ui.show_ingredients) {
                    if (record_bank_active && external_capture_busy(&external_input))
                        snprintf(ui.status, sizeof(ui.status),
                                 "CANCEL REC BEFORE CLEAR ALL");
                    else
                        clear_all_bank_slots(device, &audio, &ui, &instrument);
                } else if (wave_action == TS_UI_WAVE_ACTION_CYCLE_PANEL) {
                    ts_ui_cycle_panel(&ui);
                    ui.bank_view_slot = -1;
                    snprintf(ui.status, sizeof(ui.status), "%s PANEL",
                             ui.show_keyboard ? "KEYS" :
                             ui.show_recipes ? "CDP" :
                             ui.show_ingredients ? "DSP" : "SAMPLE TILES");
                } else {
                    int note = ui.show_keyboard ? ts_ui_key_from_point(x, y) : -1;
                    int bank_slot = !ui.show_keyboard && !ui.show_recipes &&
                                    !ui.show_ingredients ?
                                    ts_ui_bank_slot_from_point(x, y) : -1;
                    int recipe_slot = ui.show_ingredients ?
                                      ts_ui_recipe_slot_from_point(x, y) : -1;
                    int cdp_slot = ui.show_recipes ?
                        ts_ui_cdp_slot_from_point(x, y) : -1;
                    int cdp_page = ui.show_recipes ?
                        ts_ui_cdp_page_from_point(x, y) : -1;
                    int dsp_page = ui.show_ingredients ?
                        ts_ui_dsp_page_from_point(x, y) : -1;
                    int capture_control = !ui.show_keyboard && !ui.show_recipes &&
                                          !ui.show_ingredients &&
                                          ts_ui_capture_button_from_point(x, y);
                    int new_page_control = !record_bank_active &&
                                           !ui.show_keyboard && !ui.show_recipes &&
                                           !ui.show_ingredients &&
                                           ts_ui_new_page_button_from_point(x, y);
                    int keep_control = record_bank_active &&
                                       !ui.show_keyboard && !ui.show_recipes &&
                                       !ui.show_ingredients &&
                                       ts_ui_record_keep_button_from_point(x, y);
                    int monitor_control = record_bank_active &&
                                          !ui.show_keyboard && !ui.show_recipes &&
                                          !ui.show_ingredients &&
                                          ui.record_source == TS_RECORD_SOURCE_EXT &&
                                          ts_ui_monitor_button_from_point(x, y);
                    int record_source_control = record_bank_active &&
                                                !ui.show_keyboard && !ui.show_recipes &&
                                                !ui.show_ingredients &&
                                                ts_ui_record_source_button_from_point(x, y);
                    if (dsp_page >= 0) {
                        ui.dsp_page = dsp_page;
                        snprintf(ui.status, sizeof(ui.status),
                                 "DSP %d %s PAGE - SAMPLE AND SELECTION PRESERVED",
                                 ui.dsp_page + 1,
                                 ui.dsp_page == 0 ? "PROCESS" : "PRIMITIVES");
                    } else if (cdp_page >= 0) {
                        ui.cdp_page = cdp_page;
                        snprintf(ui.status, sizeof(ui.status),
                                 "CDP %d PAGE - SAMPLE AND SELECTION PRESERVED",
                                 ui.cdp_page + 1);
                    } else if (record_source_control) {
                        if (external_capture_busy(&external_input)) {
                            snprintf(ui.status, sizeof(ui.status),
                                     "STOP OR CANCEL REC BEFORE CHANGING SOURCE");
                        } else {
                            if (ui.record_source == TS_RECORD_SOURCE_EXT &&
                                ui.monitor_enabled)
                                toggle_external_monitor(device, &input_device,
                                                        &audio, &external_input,
                                                        &ui);
                            ui.record_source = ui.record_source == TS_RECORD_SOURCE_EXT ?
                                               TS_RECORD_SOURCE_SYNTH :
                                               TS_RECORD_SOURCE_EXT;
                            atomic_store_explicit(&external_input.record_source,
                                                  ui.record_source,
                                                  memory_order_release);
                            snprintf(ui.status, sizeof(ui.status),
                                     ui.record_source == TS_RECORD_SOURCE_SYNTH ?
                                     "REC SOURCE SYNTH - INTERNAL SIGNAL, MONITOR IS INHERENT" :
                                     "REC SOURCE EXT - INPUT MONITOR AVAILABLE");
                        }
                    } else if (keep_control) {
                        keep_record_bank(device, &external_input, &audio,
                                         &instrument, &sample_pages, &ui,
                                         &transform);
                    } else if (monitor_control) {
                        toggle_external_monitor(device, &input_device, &audio,
                                                &external_input, &ui);
                    } else if (new_page_control) {
                        (void)append_sample_page(device, &external_input,
                                                 &audio, &ui, &instrument,
                                                 &sample_pages, &transform);
                    } else if (capture_control) {
                        if (record_bank_active)
                            external_capture_button(device, &input_device, &audio,
                                                    &external_input, &ui, &instrument);
                        else
                            capture_button(device, &audio, &ui, &instrument, obtained.freq);
                    } else if (cdp_slot >= 0) {
                        int recipe_index = ui.cdp_page * TS_CDP_BANK_SLOT_COUNT + cdp_slot;
                        request_cdp_quick_apply(device, &audio, &ui, &instrument,
                                                &transform, recipe_index);
                    } else if (recipe_slot >= 0) {
                        int recipe_index = ui.dsp_page * TS_DSP_BANK_SLOT_COUNT +
                                           recipe_slot;
                        if (bank_modifiers(mod) == 0)
                            request_dsp_quick_apply(
                                device, &audio, &ui, &instrument, &transform,
                                recipe_index);
                        else snprintf(ui.status, sizeof(ui.status),
                                      "LEFT APPLY  MIDDLE EDIT  4 TOGGLES DSP PAGE");
                    } else if (bank_slot >= 0) {
                        TsUiBankAction action = ts_ui_bank_action(
                            0, bank_modifiers(mod));
                        if (record_bank_active && external_capture_busy(&external_input)) {
                            snprintf(ui.status, sizeof(ui.status),
                                     "REC TILE %02d LOCKED - STOP OR ESC FIRST",
                                     external_input.recorder.destination_slot + 1);
                            continue;
                        }
                        if (action == TS_UI_BANK_ACTION_AUDITION) {
                            char select_error[160];
                            int occupied = instrument.bank[bank_slot].occupied;
                            int selected;
                            int activated_silence = 0;
                            int attempted_silence = 0;
                            int capture_source_selected = 0;
                            if (audio.capture.state == TS_CAPTURE_RECORDING) {
                                snprintf(ui.status, sizeof(ui.status),
                                         "CAPTURE RECORDING - SOURCE TILE %02d IS LOCKED",
                                         audio.capture.source_slot + 1);
                                continue;
                            }
                            if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
                                bank_slot == audio.capture.destination_slot) {
                                snprintf(ui.status, sizeof(ui.status),
                                         "TILE %02d IS THE ARMED DESTINATION - SELECT ANOTHER SOURCE",
                                         bank_slot + 1);
                                continue;
                            }
                            lock_edit(device, &audio);
                            selected = ts_ui_execute_bank_action(
                                &instrument, bank_slot, action,
                                select_error, sizeof(select_error));
                            if (selected && occupied &&
                                audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER)
                                capture_source_selected = ts_capture_set_source(
                                    &audio.capture, bank_slot,
                                    select_error, sizeof(select_error));
                            if (selected && !occupied && event.button.clicks >= 2 &&
                                audio.capture.state == TS_CAPTURE_IDLE) {
                                size_t silent_frames = clipboard_source_frames > 0 ?
                                                       clipboard_source_frames :
                                                       TS_DEFAULT_CANVAS_FRAMES;
                                uint32_t silent_rate = clipboard_source_rate > 0 ?
                                                       clipboard_source_rate :
                                                       TS_DEFAULT_CANVAS_RATE;
                                attempted_silence = 1;
                                activated_silence = ts_instrument_activate_silence(
                                    &instrument, silent_frames, silent_rate,
                                    select_error, sizeof(select_error));
                            }
                            unlock_edit(device, &audio, &ui, &instrument);
                            if (selected) ui.has_stretch_readout = 0;
                            if (capture_source_selected) {
                                ui.audition_source = TS_AUDITION_CURRENT;
                                ui.bank_view_slot = -1;
                                snprintf(ui.status, sizeof(ui.status),
                                         "SOURCE TILE %02d READY - STAGE OR PLAY TO START CAPTURE",
                                         bank_slot + 1);
                            }
                            else if (activated_silence) {
                                if (ui.workbench_loop_active &&
                                    ui.workbench_loop_persistent) {
                                    ui.audition_source = TS_AUDITION_CURRENT;
                                    refresh_workbench_loop(device, &audio, &ui,
                                                           &instrument);
                                    snprintf(ui.status, sizeof(ui.status),
                                             "LOOP LOCKED TO SILENT BANK %02d VIEW",
                                             bank_slot + 1);
                                } else {
                                    stop_all(device, &audio, &ui);
                                    snprintf(ui.status, sizeof(ui.status),
                                             "BANK %02d SILENT CANVAS - %zu FRAMES AT %u HZ",
                                             bank_slot + 1, instrument.current.frames,
                                             instrument.current.sample_rate);
                                }
                            }
                            else if (selected && occupied) {
                                if (ui.workbench_loop_active &&
                                    ui.workbench_loop_persistent) {
                                    ui.audition_source = TS_AUDITION_CURRENT;
                                    ui.bank_view_slot = -1;
                                    refresh_workbench_loop(device, &audio, &ui,
                                                           &instrument);
                                    snprintf(ui.status, sizeof(ui.status),
                                             "LOOP LOCKED TO BANK %02d %s",
                                             bank_slot + 1,
                                             instrument.has_selection ?
                                             "SELECTION" : "VIEW");
                                } else {
                                    if (ui.workbench_loop_active)
                                        stop_all(device, &audio, &ui);
                                    /* Keep the selected tile in Current-edit mode. Bank-preview
                                       mode intentionally makes the waveform read-only. */
                                    ui.audition_source = TS_AUDITION_CURRENT;
                                    begin_audition(device, &audio, &ui, &instrument,
                                                   instrument.has_loop ? TS_AUDITION_LOOP :
                                                   TS_AUDITION_ALL,
                                                   1.0, obtained.freq);
                                }
                            }
                            else if (attempted_silence) {
                                stop_all(device, &audio, &ui);
                                snprintf(ui.status, sizeof(ui.status),
                                         "SILENT TILE FAILED: %.132s", select_error);
                            }
                            else if (selected) {
                                stop_all(device, &audio, &ui);
                                snprintf(ui.status, sizeof(ui.status),
                                         "BANK %02d EMPTY - DOUBLE CLICK FOR SILENCE",
                                         bank_slot + 1);
                            }
                            else
                                snprintf(ui.status, sizeof(ui.status),
                                         "BANK %02d: %.100s - ACTIVE TILE UNCHANGED",
                                         bank_slot + 1, select_error);
                        } else if (action == TS_UI_BANK_ACTION_CAPTURE_CURRENT) {
                            capture_bank_slot(device, &ui, &instrument, bank_slot,
                                              action);
                        } else if (action == TS_UI_BANK_ACTION_CAPTURE_LOOP) {
                            capture_bank_slot(device, &ui, &instrument, bank_slot,
                                              action);
                        } else if (action == TS_UI_BANK_ACTION_CAPTURE_SELECTION) {
                            capture_bank_slot(device, &ui, &instrument, bank_slot,
                                              action);
                        } else if (action == TS_UI_BANK_ACTION_CLONE) {
                            clone_bank_slot(device, &audio, &ui, &instrument, bank_slot);
                        } else if (action == TS_UI_BANK_ACTION_TOGGLE_LOCK) {
                            char lock_error[160];
                            int ok = ts_ui_execute_bank_action(
                                &instrument, bank_slot, action,
                                lock_error, sizeof(lock_error));
                            if (ok)
                                snprintf(ui.status, sizeof(ui.status),
                                         "TILE %02d %s - CTRL+ALT CLICK TO TOGGLE",
                                         bank_slot + 1,
                                         instrument.bank[bank_slot].locked ?
                                         "PROTECTED" : "UNLOCKED");
                            else snprintf(ui.status, sizeof(ui.status),
                                          "TILE LOCK FAILED: %.132s", lock_error);
                        } else {
                            snprintf(ui.status, sizeof(ui.status),
                                     "CLICK PLAY  SHIFT FULL  ALT LOOP  CTRL SEL");
                        }
                    } else if (ui.show_keyboard && note >= 0 && device) {
                        if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
                            (mod & KMOD_SHIFT)) {
                            ui.mouse_note = -1;
                            stage_capture_note(device, &audio, &ui, note);
                        } else if (audio.capture.state ==
                                   TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
                                   audio.capture.staged_notes != 0u) {
                            ui.mouse_note = -1;
                            launch_staged_capture(device, &audio, &ui, &instrument,
                                                  note, obtained.freq);
                        } else if (mod & KMOD_SHIFT) {
                            ui.mouse_note = -1;
                            begin_note(device, &audio, &ui, &instrument,
                                       note, obtained.freq, 1);
                        } else {
                            if ((mod & (KMOD_CTRL | KMOD_ALT)) == 0) {
                                SDL_LockAudioDevice(device);
                                ts_note_bank_clear(&audio.notes);
                                SDL_UnlockAudioDevice(device);
                            }
                            ui.mouse_note = note;
                            begin_note(device, &audio, &ui, &instrument,
                                       note, obtained.freq, 0);
                        }
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                       event.button.button == SDL_BUTTON_RIGHT &&
                       !ui.config_open && !ui.palette_open &&
                       ui.exchange_dialog == TS_UI_EXCHANGE_NONE &&
                       !ui.load_selection_choice_open &&
                       !ui.transform_open &&
                       !ui.drone_open &&
                       ui.browser.mode == TS_BROWSER_CLOSED) {
                int x, y;
                int bank_slot;
                int recipe_slot;
                int note;
                TsUiBankAction action;
                SDL_Keymod mod = SDL_GetModState();
                logical_mouse(window, event.button.x, event.button.y, &x, &y);
                ui.bank_clear_armed = 0;
                bank_slot = ts_ui_bank_slot_from_point(x, y);
                recipe_slot = ts_ui_recipe_slot_from_point(x, y);
                note = ui.show_keyboard ? ts_ui_key_from_point(x, y) : -1;
                action = ts_ui_bank_action(1, bank_modifiers(mod));
                if (record_bank_active && bank_slot >= 0 &&
                    external_capture_busy(&external_input)) {
                    snprintf(ui.status, sizeof(ui.status),
                             "REC TILE %02d LOCKED - STOP OR ESC FIRST",
                             external_input.recorder.destination_slot + 1);
                } else if (ui.exit_confirm_open) {
                    snprintf(ui.status, sizeof(ui.status),
                             "CHOOSE EXIT OR CANCEL - ESC CANCELS");
                } else if (ui.renaming_bank_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "FINISH BANK NAME WITH ENTER OR CANCEL WITH ESC");
                } else if (ui.renaming_recipe_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "FINISH RECIPE NAME WITH ENTER OR CANCEL WITH ESC");
                } else if (ui.export_choice_open) {
                    snprintf(ui.status, sizeof(ui.status),
                             "CHOOSE SELECTED TILE OR COLLECTION  ESC CANCELS");
                } else if (ui.input_meter_active &&
                           x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    snprintf(ui.status, sizeof(ui.status),
                             "LIVE INPUT DISPLAY IS READ-ONLY WHILE REC IS ARMED");
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    if (begin_tape_drag(&ui, &instrument, SDL_BUTTON_RIGHT,
                                        mod, x - TS_WAVE_X)) {
                        /* Modifier drag owns this gesture. */
                    } else if ((mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) == 0) {
                        ui.bank_view_slot = -1;
                        cancel_pitch_preview(device, &audio, &ui, &instrument);
                        ui.selection_anchor = selection_frame_from_x(
                            &instrument, &ui, x - TS_WAVE_X);
                        ui.wave_pointer_pending = 1;
                        ui.wave_pointer_button = SDL_BUTTON_RIGHT;
                        ui.wave_pointer_start_x = x;
                        snprintf(ui.status, sizeof(ui.status),
                                 "RMB PLAYS FROM PLAYHEAD - DRAG SELECTS");
                    } else
                        snprintf(ui.status, sizeof(ui.status),
                                 "SHIFT+RMB COPY OVERWRITE  CTRL+RMB MOVE OVERWRITE");
                } else if (note >= 0 && (mod & KMOD_SHIFT)) {
                    set_tune_reference(&ui,
                                       ts_ui_keyboard_base_note(&ui) + note,
                                       ui.tune_reference.fine_tune_cents);
                } else if (ui.show_ingredients && recipe_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "DSP TILES ARE CURATED - MIDDLE EDITS AND SAVE/UPDATE STORES");
                } else if (!ui.show_keyboard && !ui.show_recipes &&
                           !ui.show_ingredients && bank_slot >= 0 &&
                           action == TS_UI_BANK_ACTION_CLEAR) {
                    clear_bank_slot(device, &audio, &ui, &instrument, bank_slot);
                } else if (!ui.show_keyboard && !ui.show_recipes &&
                           !ui.show_ingredients && bank_slot >= 0 &&
                           action == TS_UI_BANK_ACTION_RENAME) {
                    begin_bank_rename(&ui, &instrument, bank_slot);
                } else if (!ui.show_keyboard && !ui.show_recipes &&
                           !ui.show_ingredients && bank_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "RMB RENAME  SHIFT+RMB CLEAR");
                }
            } else if (event.type == SDL_MOUSEBUTTONUP &&
                       (event.button.button == SDL_BUTTON_LEFT ||
                        event.button.button == SDL_BUTTON_RIGHT)) {
                if (ui.transform_selection_dragging &&
                    event.button.button == SDL_BUTTON_LEFT) {
                    ui.transform_selection_dragging = 0;
                    ui.transform_selection_drag_mode = 0;
                    if (!instrument.has_selection &&
                        ui.transform_scope == TS_TRANSFORM_SELECTION)
                        ui.transform_scope = TS_TRANSFORM_WHOLE;
                    snprintf(ui.transform_message, sizeof(ui.transform_message),
                             instrument.has_selection ?
                             "SELECTION %zu:%zu - RENDER AGAIN" :
                             "SELECTION IS EMPTY - WHOLE REMAINS AVAILABLE",
                             instrument.selection_first, instrument.selection_last);
                    if (ui.transform_backend == TS_TRANSFORM_BACKEND_DSP)
                        request_transform_render(device, &audio, &ui,
                                                 &instrument, &transform);
                    continue;
                }
                if (ui.canvas_gesture.active &&
                    event.button.button == SDL_BUTTON_LEFT) {
                    end_canvas_gesture(window, device, &audio, &ui, &instrument, 0);
                    continue;
                }
                if (ui.drone_crossfade_dragging &&
                    event.button.button == SDL_BUTTON_LEFT) {
                    ui.drone_crossfade_dragging = 0;
                    ui.drone_crossfade_drag_start_x = 0;
                    ui.drone_crossfade_drag_start_frames = 0;
                    snprintf(ui.status, sizeof(ui.status),
                             "DRONE CROSSFADE %.2F MS - ZERO SNAPPED",
                             ui.drone_effective_crossfade_ms);
                    continue;
                }
                if (ui.warp_dragging && event.button.button == SDL_BUTTON_LEFT) {
                    end_warp_gesture(device, &audio, &ui, &instrument, 0);
                    continue;
                }
                if (ui.smear_dragging && event.button.button == SDL_BUTTON_LEFT) {
                    end_smear_gesture(device, &audio, &ui, &instrument, 0);
                    continue;
                }
                if (ui.tear_dragging && event.button.button == SDL_BUTTON_LEFT) {
                    end_tear_gesture(device, &audio, &ui, &instrument, 0);
                    continue;
                }
                if (ui.tape_dragging && event.button.button == ui.tape_drag_button) {
                    finish_tape_drag(device, &audio, &ui, &instrument);
                    continue;
                }
                ui.browser.dragging_scrollbar = 0;
                ui.dragging_loop_endpoint = 0;
                ui.loop_drag_started = 0;
                if (ui.selecting && event.button.button == ui.selecting_button) {
                    ui.selecting = 0;
                    ui.selecting_button = 0;
                    snprintf(ui.status, sizeof(ui.status), "SELECTED %zu FRAMES",
                             instrument.selection_last - instrument.selection_first);
                }
                if (ui.wave_pointer_pending &&
                    event.button.button == ui.wave_pointer_button) {
                    int button = ui.wave_pointer_button;
                    ui.wave_pointer_pending = 0;
                    ui.wave_pointer_button = 0;
                    if (ui.config.playhead_zero_snap)
                        ts_instrument_set_playhead_snapped(&instrument,
                                                          ui.selection_anchor);
                    else ts_instrument_set_playhead(&instrument,
                                                    ui.selection_anchor);
                    if (button == SDL_BUTTON_RIGHT)
                        begin_playhead_audition(device, &audio, &ui, &instrument,
                                                obtained.freq);
                    else snprintf(ui.status, sizeof(ui.status),
                                  "PLAYHEAD %zu %s - SPACE PLAYS FROM HERE",
                                  instrument.playhead_frame,
                                  ui.config.playhead_zero_snap ?
                                  "ZERO SNAPPED" : "EXACT");
                }
                if (event.button.button == SDL_BUTTON_LEFT && ui.mouse_note >= 0) {
                    release_note(device, &audio, &ui, ui.mouse_note);
                    ui.mouse_note = -1;
                }
            }
        }

        /* Some window managers can drop the button-up event after a captured,
           warped drag. Never leave the resize gesture owning the pointer once
           SDL reports that the physical button is no longer down. */
        if (ui.canvas_gesture.active &&
            (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) == 0u)
            end_canvas_gesture(window, device, &audio, &ui, &instrument, 0);

        {
            int minimized_now =
                (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0u;
            if (window_minimized && !minimized_now)
                frame_snapshot_valid = 0;
            window_minimized = minimized_now;
        }

        if (SDL_GetTicks() - last_exchange_poll >= 1000u) {
            last_exchange_poll = SDL_GetTicks();
            (void)ts_exchange_presence_touch(
                exchange_directory(&ui), "tapesister");
            if (!ui_dialog_open(&ui) && !ui.canvas_gesture.active &&
                !ui.stretch_gesture.active && !ui.warp_gesture.active &&
                !ui.smear_gesture.active && !ui.tear_gesture.active)
                (void)stage_incoming_exchange(
                    &ui, &exchange_offer, ignored_exchange, 0);
        }
        if (record_bank_active &&
            external_input.recorder.state == TS_EXTERNAL_CAPTURE_RECORDING &&
            ui.capture_state != TS_CAPTURE_RECORDING) {
            show_overlay(&ui, "REC STARTED", 650u);
            snprintf(ui.status, sizeof(ui.status),
                     "REC %02d RECORDING - SILENCE WILL AUTO STOP",
                     external_input.recorder.destination_slot + 1);
        }
        if (record_bank_active &&
            external_input.recorder.state == TS_EXTERNAL_CAPTURE_COMPLETED)
            finalize_external_recording(device, &input_device, &audio,
                                        &external_input, &ui, &instrument);
        if (audio.capture.state == TS_CAPTURE_COMPLETED)
            finalize_capture(device, &audio, &ui, &instrument);
        poll_transform_worker(device, &audio, &ui, &instrument, &transform);
        if (record_bank_active)
            sync_external_capture_ui(device, input_device, &external_input, &ui);
        else
            sync_capture_ui(device, &audio, &ui);
        if (ui.overlay[0] != '\0' &&
            (Sint32)(SDL_GetTicks() - ui.overlay_until_ms) >= 0)
            ui.overlay[0] = '\0';
        refresh_workbench_loop(device, &audio, &ui, &instrument);
        if (device) SDL_LockAudioDevice(device);
        {
            const TsNoteVoice *voice = ts_note_bank_display_voice(&audio.notes);
            if (ui.transform_preview_active &&
                (audio.sample == &transform.preview.sample ||
                 audio.sample == &transform.dsp_preview.sample) &&
                !audio.playing)
                ui.transform_preview_active = 0;
            audio.tune_reference_frequency =
                ts_tuning_frequency(&ui.tune_reference);
            audio.tune_reference_target = audio.tune_reference_enabled ?
                (float)ui.config.reference_tone_volume / 100.0f : 0.0f;
            ui.tune_reference_active = audio.tune_reference_enabled;
            ui.active_notes = ts_note_bank_visible_mask(
                &audio.notes, ts_ui_keyboard_base_note(&ui));
            ui.fm_held_notes = ts_note_bank_latched_synth_count(&audio.notes);
            ui.playback_active = audio.playing || voice != NULL;
            if (audio.playing) {
                ui.playhead_source = audio.source;
                ui.playhead_bank_slot = audio.bank_slot;
                ui.playhead_frame = audio.position > 0.0 ? (size_t)audio.position : 0;
                ui.playhead_frames = audio.sample ? audio.sample->frames : 0;
                ui.playhead_sample = audio.sample;
            } else if (voice != NULL) {
                ui.playhead_source = voice->source;
                ui.playhead_bank_slot = -1;
                ui.playhead_frame = voice->position > 0.0 ? (size_t)voice->position : 0;
                ui.playhead_frames = voice->sample ? voice->sample->frames : 0;
                ui.playhead_sample = voice->sample;
            } else {
                ui.playhead_bank_slot = -1;
                ui.playhead_frame = 0;
                ui.playhead_frames = 0;
                ui.playhead_sample = NULL;
            }
        }
        if (device) SDL_UnlockAudioDevice(device);
        ui.text_cursor_visible = ((SDL_GetTicks() / 500u) & 1u) == 0u;
        if (!window_minimized) {
            ts_ui_render(&framebuffer, &ui, &instrument);
            if (update_texture_damage(texture, &framebuffer, frame_snapshot,
                                      &frame_snapshot_valid)) {
                /* The texture copy covers the complete destination. A clear
                   would only write the same output a second time. */
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
        }
        if (window_minimized || !renderer_vsync || !frame_snapshot_valid)
            pace_frame_60hz(frame_started);
    }

    if (ui.canvas_gesture.active)
        end_canvas_gesture(window, device, &audio, &ui, &instrument, 1);
    if (transform.worker != NULL) {
        SDL_AtomicSet(&transform.worker->cancel, 1);
        SDL_WaitThread(transform.worker->thread, NULL);
        ts_sample_free(&transform.worker->input);
        ts_sample_free(&transform.worker->dsp_output);
        ts_cdp_run_result_free(&transform.worker->result);
        free(transform.worker);
        transform.worker = NULL;
    }
    discard_transform_preview(device, &audio, &ui, &transform);
    if (input_device) SDL_PauseAudioDevice(input_device, 1);
    if (input_device) SDL_CloseAudioDevice(input_device);
    if (device) SDL_CloseAudioDevice(device);
    ts_external_recorder_free(&external_input.recorder);
    ts_capture_free(&audio.capture);
    ts_sample_free(&fm_preview);
    ts_sample_free(&drone_preview);
    ts_sample_free(&pending_selection_load);
    ts_sample_free(&clipboard);
    ts_sample_pages_free(&sample_pages);
    fm_bank_history_free(&fm_bank_history);
    if (parked_instrument != NULL) {
        ts_instrument_free(parked_instrument);
        free(parked_instrument);
        parked_instrument = NULL;
    }
    ts_instrument_free(&instrument);
    free(frame_snapshot);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    diagnostic_log("shutdown complete diagnostic_failed=%d", diagnostic_failed);
    return diagnostic_failed ? 2 : 0;
}
