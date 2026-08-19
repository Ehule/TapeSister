#include "tapesister/sample.h"
#include "tapesister/capture.h"
#include "tapesister/capture_archive.h"
#include "tapesister/input_monitor.h"
#include "tapesister/note_bank.h"
#include "tapesister/sample_pages.h"
#include "tapesister/dsp_transform.h"
#include "tapesister/ui.h"

#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

enum { TS_SPLASH_MILLISECONDS = 5000 };

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
    "LOOP LOCK SILENCE"
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
    for (int i = 0; i < values; i += 2) {
        float value = 0.0f;
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
        value += ts_note_bank_read(&audio->notes);
        if (value > 1.0f) value = 1.0f;
        if (value < -1.0f) value = -1.0f;
        if (ts_capture_write_sample(&audio->capture, value)) {
            audio->playing = 0;
            audio->bank_slot = -1;
            ts_note_bank_clear(&audio->notes);
        }
        {
            float monitored = audio->input_monitor != NULL ?
                              ts_input_monitor_read(audio->input_monitor,
                                                    (uint32_t)audio->output_rate) : 0.0f;
            float output = value * 0.8f + monitored;
            if (output > 1.0f) output = 1.0f;
            if (output < -1.0f) output = -1.0f;
            out[i] = output;
            if (i + 1 < values) out[i + 1] = output;
        }
    }
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
    snprintf(ui->status, sizeof(ui->status), "STOPPED");
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
                   (control->maximum - control->minimum);
    ts_cdp_recipe_set_control(recipe, &ui->transform_values, (size_t)index, value);
    mark_transform_stale(device, audio, ui, controller,
                         "CONTROL CHANGED - RENDER AGAIN");
}

static size_t transform_frame_from_x(const TsInstrument *instrument, int x)
{
    return ts_instrument_frame_from_view_x(
        instrument, x - TS_TRANSFORM_WAVE_X, TS_TRANSFORM_WAVE_W);
}

static void begin_transform_selection_drag(SDL_AudioDeviceID device,
                                           AudioState *audio, TsUiState *ui,
                                           TsInstrument *instrument,
                                           TransformController *controller,
                                           int x)
{
    size_t pointer = transform_frame_from_x(instrument, x);
    size_t span = instrument->view_last > instrument->view_first ?
                  instrument->view_last - instrument->view_first :
                  instrument->current.frames;
    size_t handle = span / TS_TRANSFORM_WAVE_W * 6u + 1u;
    ui->transform_selection_dragging = 1;
    ui->transform_selection_anchor = pointer;
    ui->transform_selection_length = instrument->has_selection ?
                                     instrument->selection_last -
                                     instrument->selection_first : 0u;
    ui->transform_selection_grab = 0u;
    if (instrument->has_selection &&
        pointer + handle >= instrument->selection_first &&
        pointer <= instrument->selection_first + handle) {
        ui->transform_selection_drag_mode = 2;
        ui->transform_selection_anchor = instrument->selection_last;
    } else if (instrument->has_selection &&
               pointer + handle >= instrument->selection_last &&
               pointer <= instrument->selection_last + handle) {
        ui->transform_selection_drag_mode = 3;
        ui->transform_selection_anchor = instrument->selection_first;
    } else if (instrument->has_selection && pointer >= instrument->selection_first &&
               pointer < instrument->selection_last) {
        ui->transform_selection_drag_mode = 4;
        ui->transform_selection_grab = pointer - instrument->selection_first;
    } else {
        ui->transform_selection_drag_mode = 1;
        ts_instrument_set_selection(instrument, pointer, pointer);
    }
    mark_transform_stale(device, audio, ui, controller,
                         "SELECTION CHANGED - RENDER AGAIN");
}

static void update_transform_selection_drag(TsUiState *ui,
                                            TsInstrument *instrument, int x)
{
    size_t pointer = transform_frame_from_x(instrument, x);
    if (!ui->transform_selection_dragging) return;
    if (ui->transform_selection_drag_mode == 4) {
        size_t length = ui->transform_selection_length;
        size_t first = pointer >= ui->transform_selection_grab ?
                       pointer - ui->transform_selection_grab : 0u;
        if (ts_instrument_grid_moves_snap(instrument))
            first = ts_instrument_grid_target(instrument, first);
        if (length > instrument->current.frames) length = instrument->current.frames;
        if (first + length > instrument->current.frames)
            first = instrument->current.frames - length;
        ts_instrument_set_selection(instrument, first, first + length);
    } else if (ui->transform_selection_drag_mode == 2) {
        if (pointer >= ui->transform_selection_anchor)
            pointer = ui->transform_selection_anchor > 0u ?
                      ui->transform_selection_anchor - 1u : 0u;
        ts_instrument_set_selection_snapped(instrument, pointer,
                                            ui->transform_selection_anchor);
    } else if (ui->transform_selection_drag_mode == 3) {
        if (pointer <= ui->transform_selection_anchor)
            pointer = ui->transform_selection_anchor + 1u;
        ts_instrument_set_selection_snapped(instrument,
                                            ui->transform_selection_anchor,
                                            pointer);
    } else {
        ts_instrument_set_selection_snapped(instrument,
                                            ui->transform_selection_anchor,
                                            pointer);
    }
    if (instrument->has_selection && ui->transform_scope == TS_TRANSFORM_WHOLE) {
        /* WHOLE is intentionally retained while the persistent selection moves. */
    } else if (instrument->has_selection) ui->transform_scope = TS_TRANSFORM_SELECTION;
}

static void save_dsp_transform_preset(TsUiState *ui)
{
    int slot;
    const TsDspRecipe *recipe;
    TsConfig previous;
    char error[160];
    if (ui == NULL || ui->transform_backend != TS_TRANSFORM_BACKEND_DSP) return;
    slot = ui->transform_dsp_slot;
    recipe = slot >= 0 ? ts_dsp_factory_recipe_at((size_t)slot) : NULL;
    if (recipe == NULL) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "DSP PRESET CANNOT BE UPDATED");
        return;
    }
    previous = ui->config;
    ui->config.dsp_factory_overridden[slot] = 1;
    memcpy(ui->config.dsp_factory_controls[slot],
           ui->transform_dsp_values.controls,
           sizeof(ui->config.dsp_factory_controls[slot]));
    if (!ts_config_save(&ui->config, config_file_path(), error, sizeof(error))) {
        ui->config = previous;
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "PRESET UPDATE FAILED: %.69s", error);
        return;
    }
    ui->dsp_presets[slot] = ui->transform_dsp_values;
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "%s UPDATED IN TAPESISTER.INI", recipe->display_name);
    snprintf(ui->status, sizeof(ui->status),
             "DSP TILE %02d NOW USES THE EDITED SETTINGS", slot + 1);
}

static void save_cdp_transform_preset(TsUiState *ui)
{
    int slot;
    const TsCdpRecipe *recipe;
    TsConfig previous;
    char error[160];
    if (ui == NULL || ui->transform_backend != TS_TRANSFORM_BACKEND_CDP) return;
    slot = ui->transform_recipe_index;
    recipe = slot >= 0 ? ts_cdp_factory_recipe_at((size_t)slot) : NULL;
    if (recipe == NULL) {
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "CDP PRESET CANNOT BE UPDATED");
        return;
    }
    previous = ui->config;
    ui->config.cdp_factory_overridden[slot] = 1;
    memcpy(ui->config.cdp_factory_controls[slot], ui->transform_values.controls,
           sizeof(ui->config.cdp_factory_controls[slot]));
    ui->config.cdp_factory_mix[slot] = ui->transform_values.mix;
    ui->config.cdp_factory_seed[slot] = ui->transform_values.seed;
    if (!ts_config_save(&ui->config, config_file_path(), error, sizeof(error))) {
        ui->config = previous;
        snprintf(ui->transform_message, sizeof(ui->transform_message),
                 "PRESET UPDATE FAILED: %.69s", error);
        return;
    }
    ui->cdp_presets[slot] = ui->transform_values;
    snprintf(ui->transform_message, sizeof(ui->transform_message),
             "%s SETTINGS SAVED IN TAPESISTER.INI", recipe->display_name);
    snprintf(ui->status, sizeof(ui->status),
             "CDP %d TILE %02d NOW USES THE EDITED SETTINGS",
             recipe->bank + 1, recipe->slot + 1);
}

static void handle_transform_action(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument,
                                    TransformController *controller,
                                    TsUiTransformAction action,
                                    int output_rate)
{
    switch (action) {
    case TS_UI_TRANSFORM_ACTION_RECIPE:
        if (ui->transform_backend == TS_TRANSFORM_BACKEND_CDP) {
            const TsCdpRecipe *recipe = active_transform_recipe(ui);
            if (recipe != NULL && recipe->seed_supported) {
                ++ui->transform_values.seed;
                mark_transform_stale(device, audio, ui, controller,
                                     "NEW VARIATION REQUESTED - RENDER AGAIN");
            } else if (recipe != NULL && !recipe->deterministic) {
                mark_transform_stale(device, audio, ui, controller,
                                     "NEW NONDETERMINISTIC TAKE - RENDER AGAIN");
            } else {
                snprintf(ui->transform_message, sizeof(ui->transform_message),
                         "THIS RECIPE IS DETERMINISTIC - ADJUST A CONTROL TO VARY IT");
            }
        }
        break;
    case TS_UI_TRANSFORM_ACTION_SELECTION:
        if (!instrument->has_selection ||
            instrument->selection_last <= instrument->selection_first) {
            snprintf(ui->transform_message, sizeof(ui->transform_message),
                     "SELECTION SCOPE NEEDS A NONEMPTY SELECTION");
        } else if (ui->transform_scope != TS_TRANSFORM_SELECTION) {
            ui->transform_scope = TS_TRANSFORM_SELECTION;
            mark_transform_stale(device, audio, ui, controller,
                                 "SCOPE CHANGED - RENDER AGAIN");
            if (ui->transform_backend == TS_TRANSFORM_BACKEND_DSP)
                request_transform_render(device, audio, ui, instrument, controller);
        }
        break;
    case TS_UI_TRANSFORM_ACTION_WHOLE:
        if (ui->transform_scope != TS_TRANSFORM_WHOLE) {
            ui->transform_scope = TS_TRANSFORM_WHOLE;
            mark_transform_stale(device, audio, ui, controller,
                                 "SCOPE CHANGED - SELECTION RETAINED");
            if (ui->transform_backend == TS_TRANSFORM_BACKEND_DSP)
                request_transform_render(device, audio, ui, instrument, controller);
        }
        break;
    case TS_UI_TRANSFORM_ACTION_RENDER:
        request_transform_render(device, audio, ui, instrument, controller);
        break;
    case TS_UI_TRANSFORM_ACTION_APPLY:
        apply_transform_preview(device, audio, ui, instrument, controller);
        break;
    case TS_UI_TRANSFORM_ACTION_AUDITION:
        audition_transform_preview(device, audio, ui, instrument, controller,
                                   output_rate);
        break;
    case TS_UI_TRANSFORM_ACTION_SAVE:
        if (ui->transform_backend == TS_TRANSFORM_BACKEND_DSP)
            save_dsp_transform_preset(ui);
        else
            save_cdp_transform_preset(ui);
        break;
    case TS_UI_TRANSFORM_ACTION_BACK:
        if (controller->worker != NULL) {
            ++controller->render_generation;
            controller->rerender_requested = 0;
            SDL_AtomicSet(&controller->worker->cancel, 1);
            snprintf(ui->transform_message, sizeof(ui->transform_message),
                     "CANCELING RENDER - TILE UNCHANGED");
        } else close_transform_workspace(device, audio, ui, controller);
        break;
    default:
        break;
    }
}

static void commit_drone(SDL_AudioDeviceID device, AudioState *audio,
                         TsUiState *ui, TsInstrument *instrument,
                         TsSample *drone, int copy_to_new_tile)
{
    char error[160];
    int destination = -1;
    int ok;
    if (!drone_context_matches(ui, instrument)) {
        close_drone_dialog(device, audio, ui, drone);
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE CANCELLED - EDITING CONTEXT CHANGED");
        return;
    }
    stop_drone_preview(device, audio, ui, drone);
    lock_edit(device, audio);
    ok = copy_to_new_tile ?
         ts_instrument_copy_drone_to_new_tile(instrument, drone, &destination,
                                               error, sizeof(error)) :
         ts_instrument_replace_selection_with_drone(instrument, drone,
                                                     error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "DRONE FAILED: %.142s", error);
        return;
    }
    ui->drone_open = 0;
    ui->drone_source_slot = -1;
    ui->drone_preview_active = 0;
    ui->drone_crossfade_dragging = 0;
    ui->drone_preview_sample = NULL;
    ts_sample_free(drone);
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->bank_view_slot = -1;
    if (copy_to_new_tile) {
        ts_ui_reset_parent_view(ui, instrument->current.frames);
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE COPIED TO TILE %02d - UNDO RESTORES SILENCE",
                 destination + 1);
    } else {
        snprintf(ui->status, sizeof(ui->status),
                 "DRONE REPLACED SELECTION - UNDO RESTORES SOURCE");
    }
}

static int load_instrument(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument,
                           TsSamplePages *sample_pages,
                           TsInstrument *record_bank,
                           int record_bank_active,
                           const char *path)
{
    char error[160];
    int recipe = path_is_tsr(path);
    int preset = path_is_tsp(path);
    int ok;
    if (audio->capture.state == TS_CAPTURE_COMPLETED)
        finalize_capture(device, audio, ui, instrument);
    if (audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL CAPTURE BEFORE LOADING");
        return 0;
    }
    if (preset) {
        TsPortableRecipe loaded;
        int slot = 0;
        ok = ts_recipe_load(&loaded, path, error, sizeof(error));
        if (ok) {
            slot = ts_recipe_bank_add_user(&ui->recipes, &loaded, error, sizeof(error));
            ok = slot > 0;
        }
        if (ok) {
            lock_edit(device, audio);
            ok = loaded.has_tuning ?
                 ts_instrument_set_process_and_tunings(instrument, &loaded.process,
                                                       &loaded.tuning,
                                                       &loaded.audible_tuning,
                                                       error, sizeof(error)) :
                 ts_instrument_set_process(instrument, &loaded.process,
                                           error, sizeof(error));
            unlock_edit(device, audio, ui, instrument);
        }
        if (!ok && slot > 0) {
            char ignored[80];
            ts_recipe_bank_clear(&ui->recipes, slot - 1,
                                 ignored, sizeof(ignored));
        }
        if (ok) {
            ui->show_keyboard = 0;
            ui->show_recipes = 0;
            ui->show_ingredients = 1;
            ui->recipes.active_slot = slot - 1;
            ui->has_pitch_suggestion = 0;
            snprintf(ui->status, sizeof(ui->status), "LOADED RECIPE %.31s%s - UNDO RESTORES",
                     loaded.name, loaded.has_tuning ? " + TUNING" : "");
        } else snprintf(ui->status, sizeof(ui->status), "TSP LOAD FAILED: %.131s", error);
        return ok;
    }
    if (recipe && record_bank_active) {
        snprintf(ui->status, sizeof(ui->status),
                 "PRESS 1 TO RETURN TO SAMPLE BANK BEFORE OPENING A PROJECT");
        return 0;
    }
    if (ui->workbench_loop_active) stop_all(device, audio, ui);
    lock_edit(device, audio);
    ui->bank_view_slot = -1;
    if (audio->bank_slot >= 0) {
        audio->playing = 0;
        audio->bank_slot = -1;
    }
    if (recipe)
        ok = ts_sample_pages_load_project(sample_pages, instrument, record_bank,
                                          path, error, sizeof(error));
    else {
        int destination = ui->load_bank_slot;
        if (destination < 0 || destination >= TS_BANK_SLOT_COUNT)
            destination = instrument->selected_slot;
        ok = ts_instrument_select_bank(instrument, destination, error, sizeof(error)) &&
             ts_instrument_load_wav(instrument, path, error, sizeof(error));
    }
    unlock_edit(device, audio, ui, instrument);
    if (ok) ts_ui_reset_parent_view(ui, instrument->parent.frames);
    if (ok && recipe) {
        ui->sample_page = (int)ts_sample_pages_active(sample_pages);
        ui->sample_page_count = (int)ts_sample_pages_count(sample_pages);
        snprintf(ui->status, sizeof(ui->status), "OPENED TSR PROJECT %.112s",
                 instrument->parent.name);
    } else if (ok) {
        ui->audition_source = TS_AUDITION_CURRENT;
        ui->load_bank_slot = -1;
        snprintf(ui->status, sizeof(ui->status), "IMPORTED WAV INTO BANK %02d %.91s",
                 instrument->selected_slot + 1, instrument->parent.name);
    }
    else snprintf(ui->status, sizeof(ui->status), "LOAD FAILED: %.135s", error);
    if (ok && recipe)
        ui->saved_state_hash = paged_project_state_hash(
            sample_pages, instrument, record_bank);
    return ok;
}


static void begin_bank_audition(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, const TsInstrument *instrument,
                                int slot, int output_rate);

static void generate_family_candidate(SDL_AudioDeviceID device, AudioState *audio,
                                      TsUiState *ui, TsInstrument *instrument,
                                      int vary, int unused_promote, int unused_radical)
{
    char error[160];
    int slot = instrument->selected_slot;
    int stamp = instrument->has_selection && instrument->current.data != NULL &&
                instrument->selection_last > instrument->selection_first;
    size_t stamp_frames = stamp ? instrument->selection_last -
                                 instrument->selection_first : 0;
    int ok;
    (void)unused_promote; (void)unused_radical;
    if (ui->workbench_loop_active) stop_all(device, audio, ui);
    lock_edit(device, audio);
    audio->playing = 0; audio->bank_slot = -1;
    if (stamp && vary && instrument->family_trajectory)
        ok = ts_instrument_stamp_vary_chained(
            instrument, ui->config.chain_stamp_crossfade_ms,
            error, sizeof(error));
    else if (stamp && vary)
        ok = ts_instrument_stamp_vary(instrument, error, sizeof(error));
    else if (stamp)
        ok = ts_instrument_stamp_create(instrument,
                instrument->generator.seed * 1664525u + 1013904223u +
                instrument->family_sequence * 2246822519u,
                error, sizeof(error));
    else if (vary)
        ok = ts_instrument_vary_selected(instrument, instrument->family_trajectory,
                                         &slot, error, sizeof(error));
    else
        ok = ts_instrument_create_selected(instrument,
                instrument->generator.seed * 1664525u + 1013904223u,
                error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "%s FAILED: %.132s",
                 vary ? "VARY" : "CREATE", error);
        return;
    }
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    if (stamp && vary && instrument->family_trajectory)
        snprintf(ui->status, sizeof(ui->status),
                 "BANK %02d CHAIN STAMPED %zu FRAMES - NEXT %zu:%zu",
                 slot + 1, stamp_frames, instrument->selection_first,
                 instrument->selection_last);
    else if (stamp)
        snprintf(ui->status, sizeof(ui->status),
                 "BANK %02d %s FM STAMP IN %zu-FRAME SELECTION",
                 slot + 1, vary ? "VARIED" : "CREATED", stamp_frames);
    else {
        ts_ui_reset_parent_view(ui, instrument->current.frames);
        snprintf(ui->status, sizeof(ui->status), "BANK %02d %s FM - RANGE %d%s",
                 slot + 1, vary ? "VARIED" : "CREATED",
                 (int)lrintf(instrument->family_mutation * 100.0f),
                 vary && instrument->family_trajectory ? " CHAIN" : "");
    }
}

static void apply_process(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                          TsInstrument *instrument, TsProcessRecipe process, const char *label)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_set_process(instrument, &process, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) ui->recipes.active_slot = -1;
    if (ok && strcmp(label, "BODY") == 0)
        snprintf(ui->status, sizeof(ui->status), "BODY %.2F - SELECTED TILE UPDATED", process.body);
    else if (ok && strcmp(label, "EDGE") == 0)
        snprintf(ui->status, sizeof(ui->status), "EDGE %.2F - SELECTED TILE UPDATED", process.edge);
    else if (ok && strcmp(label, "DRIFT") == 0)
        snprintf(ui->status, sizeof(ui->status), "DRIFT %.2F - SELECTED TILE UPDATED", process.drift);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "%s UPDATED ON SELECTED TILE", label);
    else snprintf(ui->status, sizeof(ui->status), "PROCESS FAILED: %.130s", error);
}

static void apply_tuning(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                         TsInstrument *instrument, int root_note, float cents)
{
    char error[160];
    char note[12];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_set_tuning(instrument, root_note, cents, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->has_pitch_suggestion = 0;
        snprintf(ui->status, sizeof(ui->status), "ROOT %s  TRIM %+.1F CENTS  %.2F HZ",
                 ts_midi_note_name(instrument->audible_tuning.root_note,
                                   note, sizeof(note)),
                 instrument->audible_tuning.fine_tune_cents,
                 ts_tuning_frequency(&instrument->audible_tuning));
    } else snprintf(ui->status, sizeof(ui->status), "TUNING: %.145s", error);
}

static void apply_audible_tuning(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument,
                                 int root_note, float cents)
{
    char error[160];
    char note[12];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_set_audible_tuning(instrument, root_note, cents,
                                          error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok)
        snprintf(ui->status, sizeof(ui->status), "PITCH %s  TRIM %+.1F C  %.2F HZ",
                 ts_midi_note_name(instrument->audible_tuning.root_note,
                                   note, sizeof(note)),
                 instrument->audible_tuning.fine_tune_cents,
                 ts_tuning_frequency(&instrument->audible_tuning));
    else snprintf(ui->status, sizeof(ui->status), "TUNING: %.145s", error);
}

static void suggest_or_accept_pitch(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    char note[12];
    if (ui->has_pitch_suggestion) {
        TsTuning suggestion = ui->pitch_suggestion;
        apply_tuning(device, audio, ui, instrument, suggestion.root_note,
                     suggestion.fine_tune_cents);
        return;
    }
    if (ts_instrument_suggest_pitch(instrument, &ui->pitch_suggestion,
                                    &ui->pitch_confidence, error, sizeof(error))) {
        ui->has_pitch_suggestion = 1;
        if (device) SDL_LockAudioDevice(device);
        ts_note_bank_sync_tuned(&audio->notes, instrument, &ui->pitch_suggestion,
                                audio->output_rate);
        if (device) SDL_UnlockAudioDevice(device);
        snprintf(ui->status, sizeof(ui->status),
                 "PREVIEW %s %+.1F C  CONF %.0F%% - ACCEPT OR ESC CANCEL",
                 ts_midi_note_name(ui->pitch_suggestion.root_note, note, sizeof(note)),
                 ui->pitch_suggestion.fine_tune_cents,
                 ui->pitch_confidence * 100.0f);
    } else snprintf(ui->status, sizeof(ui->status), "PITCH SUGGESTION: %.137s", error);
}

static void cancel_pitch_preview(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, const TsInstrument *instrument)
{
    if (!ui->has_pitch_suggestion) return;
    ui->has_pitch_suggestion = 0;
    if (device) SDL_LockAudioDevice(device);
    ts_note_bank_sync(&audio->notes, instrument, audio->output_rate);
    if (device) SDL_UnlockAudioDevice(device);
}

static void select_current_tile(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, TsInstrument *instrument,
                                int wave_only)
{
    int selected;
    cancel_pitch_preview(device, audio, ui, instrument);
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->selecting = 0;
    ui->selecting_button = 0;
    ui->wave_pointer_pending = 0;
    ui->wave_pointer_button = 0;
    ui->has_stretch_readout = 0;
    selected = wave_only ? ts_instrument_select_wave(instrument) :
                           ts_instrument_select_all(instrument);
    if (selected) {
        if (wave_only)
            snprintf(ui->status, sizeof(ui->status),
                     "SELECTED WAVE %zu - %zu  SILENT MARGINS SKIPPED",
                     instrument->selection_first, instrument->selection_last);
        else
            snprintf(ui->status, sizeof(ui->status),
                     "SELECTED WHOLE TILE 0 - %zu", instrument->selection_last);
    } else {
        snprintf(ui->status, sizeof(ui->status),
                 wave_only ? "NO NON-SILENT WAVE IN TILE" :
                             "SELECT ALL NEEDS AN ACTIVE TILE");
    }
}

static void crop_selection(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_crop_selection(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CROPPED SELECTED TILE");
    else snprintf(ui->status, sizeof(ui->status), "CROP FAILED: %.135s", error);
}

static void apply_sample_edit(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                              TsInstrument *instrument, TsSampleEditKind kind, float amount)
{
    char error[160];
    int ok;
    int selected = instrument->has_selection;
    lock_edit(device, audio);
    ok = ts_instrument_apply_sample_edit(instrument, kind, amount, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "%s %s - SELECTED TILE UPDATED",
                     ts_sample_edit_name(kind), selected ? "SELECTION" : "ALL");
    else snprintf(ui->status, sizeof(ui->status), "EDIT FAILED: %.137s", error);
}

static void copy_selection_to_clipboard(SDL_AudioDeviceID device, AudioState *audio,
                                        TsUiState *ui, const TsInstrument *instrument,
                                        TsSample *clipboard, size_t *origin_first,
                                        size_t *source_frames, uint32_t *source_rate)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_copy_selection(instrument, clipboard, origin_first,
                                      error, sizeof(error));
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) {
        if (source_frames != NULL) *source_frames = instrument->current.frames;
        if (source_rate != NULL) *source_rate = instrument->current.sample_rate;
        snprintf(ui->status, sizeof(ui->status),
                 "COPIED %zu FRAMES FROM TILE %02d",
                 clipboard->frames, instrument->selected_slot + 1);
    } else snprintf(ui->status, sizeof(ui->status), "COPY FAILED: %.137s", error);
}

static void cut_selection_to_clipboard(SDL_AudioDeviceID device, AudioState *audio,
                                       TsUiState *ui, TsInstrument *instrument,
                                       TsSample *clipboard, size_t *origin_first,
                                       size_t *source_frames, uint32_t *source_rate)
{
    char error[160];
    int ok;
    size_t copied_source_frames = instrument->current.frames;
    uint32_t copied_source_rate = instrument->current.sample_rate;
    lock_edit(device, audio);
    ok = ts_instrument_cut_selection(instrument, clipboard, origin_first,
                                     error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        if (source_frames != NULL) *source_frames = copied_source_frames;
        if (source_rate != NULL) *source_rate = copied_source_rate;
        snprintf(ui->status, sizeof(ui->status),
                 "CUT %zu FRAMES FROM TILE %02d - UNDO AVAILABLE",
                 clipboard->frames, instrument->selected_slot + 1);
    } else snprintf(ui->status, sizeof(ui->status), "CUT FAILED: %.138s", error);
}

static void paste_from_clipboard(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument,
                                 const TsSample *clipboard, size_t origin_first,
                                 int fit_selection)
{
    char error[160];
    int ok;
    size_t target_frames = instrument->has_selection ?
                           instrument->selection_last - instrument->selection_first : 0;
    lock_edit(device, audio);
    ok = ts_instrument_paste(instrument, clipboard, origin_first, fit_selection,
                             error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok && fit_selection)
        snprintf(ui->status, sizeof(ui->status),
                 "FIT-PASTED %zu FRAMES INTO %zu ON TILE %02d",
                 clipboard->frames, target_frames, instrument->selected_slot + 1);
    else if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "PASTED %zu FRAMES ON TILE %02d - EXACT LENGTH",
                 clipboard->frames, instrument->selected_slot + 1);
    else snprintf(ui->status, sizeof(ui->status), "%s FAILED: %.132s",
                  fit_selection ? "FIT PASTE" : "PASTE", error);
}

static int begin_warp_gesture(SDL_AudioDeviceID device, AudioState *audio,
                              TsUiState *ui, TsInstrument *instrument, int wheel)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_warp_gesture_begin(instrument, &ui->warp_gesture,
                                          error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->warp_dragging = !wheel;
        ui->warp_wheel_active = wheel;
        ui->warp_amount = 0.0f;
        ui->warp_last_audition_ms = 0;
    } else snprintf(ui->status, sizeof(ui->status),
                    "WARP BEGIN FAILED: %.131s", error);
    return ok;
}

static int preview_warp_gesture(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, TsInstrument *instrument,
                                float amount, int output_rate)
{
    char error[160];
    uint32_t now;
    int ok;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    lock_edit(device, audio);
    ok = ts_instrument_warp_gesture_preview(instrument, &ui->warp_gesture,
                                            amount, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status),
                 "WARP PREVIEW FAILED: %.129s", error);
        return 0;
    }
    ui->warp_amount = amount;
    now = SDL_GetTicks();
    if (!ts_ui_transform_auto_audition_allowed(ui)) {
        snprintf(ui->status, sizeof(ui->status), "WARP PREVIEW %.2F - LOOP OWNS AUDITION", amount);
    } else if (ui->warp_last_audition_ms == 0 || now - ui->warp_last_audition_ms >= 100u) {
        TsAuditionRange range = ui->warp_gesture.start.has_selection ?
                                TS_AUDITION_SELECTION : TS_AUDITION_ALL;
        ui->audition_source = TS_AUDITION_CURRENT;
        begin_audition(device, audio, ui, instrument, range, 1.0, output_rate);
        ui->warp_last_audition_ms = now;
    } else snprintf(ui->status, sizeof(ui->status), "WARP PREVIEW %.2F", amount);
    return 1;
}

static void end_warp_gesture(SDL_AudioDeviceID device, AudioState *audio,
                             TsUiState *ui, TsInstrument *instrument, int cancel)
{
    char error[160];
    float amount = ui->warp_gesture.amount;
    int ok;
    lock_edit(device, audio);
    ok = cancel ? ts_instrument_warp_gesture_cancel(
                      instrument, &ui->warp_gesture, error, sizeof(error)) :
                  ts_instrument_warp_gesture_commit(
                      instrument, &ui->warp_gesture, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    ui->warp_dragging = 0;
    ui->warp_wheel_active = 0;
    ui->warp_amount = 0.0f;
    ui->warp_last_audition_ms = 0;
    if (cancel) stop_all(device, audio, ui);
    if (ok && cancel)
        snprintf(ui->status, sizeof(ui->status),
                 "WARP CANCELLED - ORIGINAL RESTORED");
    else if (ok && amount > 0.0f)
        snprintf(ui->status, sizeof(ui->status), "WARP %.2F COMMITTED", amount);
    else if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "WARP RETURNED TO ZERO - NO EDIT");
    else snprintf(ui->status, sizeof(ui->status),
                  "WARP END FAILED: %.133s", error);
}

static int begin_smear_gesture(SDL_AudioDeviceID device, AudioState *audio,
                               TsUiState *ui, TsInstrument *instrument, int wheel)
{
    char error[160]; int ok;
    lock_edit(device, audio);
    ok = ts_instrument_smear_gesture_begin(instrument, &ui->smear_gesture, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->smear_dragging = !wheel; ui->smear_wheel_active = wheel;
        ui->smear_amount = 0.0f; ui->smear_last_audition_ms = 0;
    } else snprintf(ui->status, sizeof(ui->status), "SMEAR BEGIN FAILED: %.130s", error);
    return ok;
}

static int preview_smear_gesture(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument,
                                 float amount, int output_rate)
{
    char error[160]; uint32_t now; int ok;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    lock_edit(device, audio);
    ok = ts_instrument_smear_gesture_preview(instrument, &ui->smear_gesture,
                                             amount, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) { snprintf(ui->status, sizeof(ui->status), "SMEAR PREVIEW FAILED: %.128s", error); return 0; }
    ui->smear_amount = amount; now = SDL_GetTicks();
    if (!ts_ui_transform_auto_audition_allowed(ui)) {
        snprintf(ui->status, sizeof(ui->status), "SMEAR PREVIEW %.2F - LOOP OWNS AUDITION", amount);
    } else if (ui->smear_last_audition_ms == 0 || now - ui->smear_last_audition_ms >= 150u) {
        TsAuditionRange range = ui->smear_gesture.start.has_selection ? TS_AUDITION_SELECTION : TS_AUDITION_ALL;
        ui->audition_source = TS_AUDITION_CURRENT;
        begin_audition(device, audio, ui, instrument, range, 1.0, output_rate);
        ui->smear_last_audition_ms = now;
    } else snprintf(ui->status, sizeof(ui->status), "SMEAR PREVIEW %.2F", amount);
    return 1;
}

static void end_smear_gesture(SDL_AudioDeviceID device, AudioState *audio,
                              TsUiState *ui, TsInstrument *instrument, int cancel)
{
    char error[160]; float amount = ui->smear_gesture.amount; int ok;
    lock_edit(device, audio);
    ok = cancel ? ts_instrument_smear_gesture_cancel(instrument, &ui->smear_gesture, error, sizeof(error)) :
                  ts_instrument_smear_gesture_commit(instrument, &ui->smear_gesture, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    ui->smear_dragging = ui->smear_wheel_active = 0; ui->smear_amount = 0.0f;
    ui->smear_last_audition_ms = 0;
    if (cancel) stop_all(device, audio, ui);
    if (ok && cancel) snprintf(ui->status, sizeof(ui->status), "SMEAR CANCELLED - ORIGINAL RESTORED");
    else if (ok && amount > 0.0f) snprintf(ui->status, sizeof(ui->status), "SMEAR %.2F COMMITTED", amount);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "SMEAR RETURNED TO ZERO - NO EDIT");
    else snprintf(ui->status, sizeof(ui->status), "SMEAR END FAILED: %.132s", error);
}

static int begin_tear_gesture(SDL_AudioDeviceID device, AudioState *audio,
                              TsUiState *ui, TsInstrument *instrument, int wheel)
{
    char error[160]; int ok;
    lock_edit(device, audio);
    ok = ts_instrument_tear_gesture_begin(instrument, &ui->tear_gesture,
                                          error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->tear_dragging = !wheel; ui->tear_wheel_active = wheel;
        ui->tear_amount = 0.0f; ui->tear_last_audition_ms = 0;
    } else snprintf(ui->status, sizeof(ui->status), "TEAR BEGIN FAILED: %.131s", error);
    return ok;
}

static int preview_tear_gesture(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, TsInstrument *instrument,
                                float amount, int output_rate)
{
    char error[160]; uint32_t now; int ok;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    lock_edit(device, audio);
    ok = ts_instrument_tear_gesture_preview(instrument, &ui->tear_gesture,
                                            amount, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) { snprintf(ui->status, sizeof(ui->status), "TEAR PREVIEW FAILED: %.129s", error); return 0; }
    ui->tear_amount = amount; now = SDL_GetTicks();
    if (!ts_ui_transform_auto_audition_allowed(ui)) {
        snprintf(ui->status, sizeof(ui->status), "TEAR PREVIEW %.2F - LOOP OWNS AUDITION", amount);
    } else if (ui->tear_last_audition_ms == 0 || now - ui->tear_last_audition_ms >= 125u) {
        TsAuditionRange range = ui->tear_gesture.start.has_selection ? TS_AUDITION_SELECTION : TS_AUDITION_ALL;
        ui->audition_source = TS_AUDITION_CURRENT;
        begin_audition(device, audio, ui, instrument, range, 1.0, output_rate);
        ui->tear_last_audition_ms = now;
    } else snprintf(ui->status, sizeof(ui->status), "TEAR PREVIEW %.2F", amount);
    return 1;
}

static void end_tear_gesture(SDL_AudioDeviceID device, AudioState *audio,
                             TsUiState *ui, TsInstrument *instrument, int cancel)
{
    char error[160]; float amount = ui->tear_gesture.amount; int ok;
    lock_edit(device, audio);
    ok = cancel ? ts_instrument_tear_gesture_cancel(instrument, &ui->tear_gesture, error, sizeof(error)) :
                  ts_instrument_tear_gesture_commit(instrument, &ui->tear_gesture, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    ui->tear_dragging = ui->tear_wheel_active = 0; ui->tear_amount = 0.0f;
    ui->tear_last_audition_ms = 0;
    if (cancel && !ui->workbench_loop_active) stop_all(device, audio, ui);
    if (ok && cancel) snprintf(ui->status, sizeof(ui->status), "TEAR CANCELLED - ORIGINAL RESTORED");
    else if (ok && amount > 0.0f) snprintf(ui->status, sizeof(ui->status), "TEAR %.2F COMMITTED", amount);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "TEAR RETURNED TO ZERO - NO EDIT");
    else snprintf(ui->status, sizeof(ui->status), "TEAR END FAILED: %.133s", error);
}

static void rotate_waveform(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                            TsInstrument *instrument, int direction, int crossing_count)
{
    char error[160];
    int selected = instrument->has_selection;
    size_t detents = direction < 0 ? (size_t)(-(int64_t)direction) : (size_t)direction;
    size_t candidates = (size_t)crossing_count;
    int ok;
    if (detents > SIZE_MAX / candidates) candidates = SIZE_MAX;
    else candidates *= detents;
    lock_edit(device, audio);
    ok = ts_instrument_rotate_zero_crossing(instrument, direction > 0 ? 1 : -1,
                                             candidates,
                                             error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status),
                     "ROTATED %s TO %s ZERO CROSSING",
                     selected ? "SELECTION" : "WAVEFORM",
                     direction > 0 ? "NEXT" : "PREVIOUS");
    else snprintf(ui->status, sizeof(ui->status), "ROTATE FAILED: %.135s", error);
}

static int begin_stretch_gesture(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    size_t pivot;
    int ok;
    if (!instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        snprintf(ui->status, sizeof(ui->status),
                 "SELECT AUDIO BEFORE CHANGING ITS TAPE LENGTH");
        return 0;
    }
    pivot = instrument->has_playhead &&
            instrument->playhead_frame >= instrument->selection_first &&
            instrument->playhead_frame < instrument->selection_last ?
            instrument->playhead_frame :
            instrument->selection_first +
            (instrument->selection_last - instrument->selection_first) / 2u;
    lock_edit(device, audio);
    ok = ts_instrument_stretch_gesture_begin(
        instrument, &ui->stretch_gesture, pivot, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->stretch_wheel_active = 1;
        ui->stretch_wheel_steps = 0;
    } else snprintf(ui->status, sizeof(ui->status),
                    "TAPE LENGTH BEGIN FAILED: %.120s", error);
    return ok;
}

static void stretch_waveform(SDL_AudioDeviceID device, AudioState *audio,
                             TsUiState *ui, TsInstrument *instrument,
                             int wheel_y)
{
    char error[160];
    float pitch = 0.0f;
    float requested_ratio;
    size_t before_frames, after_frames;
    int requested_steps;
    int ok;
    if (wheel_y == 0) return;
    if (!ui->stretch_gesture.active &&
        !begin_stretch_gesture(device, audio, ui, instrument)) return;
    before_frames = ui->stretch_gesture.start.selection_last -
                    ui->stretch_gesture.start.selection_first;
    requested_steps = ui->stretch_wheel_steps + wheel_y;
    requested_ratio = powf(2.0f, (float)requested_steps / 12.0f);
    lock_edit(device, audio);
    ok = ts_instrument_stretch_gesture_preview(
        instrument, &ui->stretch_gesture, requested_ratio,
        &pitch, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->stretch_wheel_steps = requested_steps;
        after_frames = instrument->selection_last - instrument->selection_first;
        ui->has_stretch_readout = 1;
        ui->stretch_pitch_semitones = pitch;
        ui->stretch_duration_ratio = before_frames > 0 ?
            (float)after_frames / (float)before_frames : 1.0f;
        snprintf(ui->status, sizeof(ui->status),
                 "TAPE PREVIEW %zu -> %zu  PITCH %+.2F ST - RELEASE MODIFIER",
                 before_frames, after_frames, pitch);
    } else snprintf(ui->status, sizeof(ui->status),
                    "TAPE LENGTH PREVIEW LIMIT: %.119s", error);
}

static void end_stretch_gesture(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, TsInstrument *instrument,
                                int cancel)
{
    char error[160];
    float pitch = ui->stretch_gesture.pitch_semitones;
    float ratio = ui->stretch_gesture.actual_ratio;
    int changed = ui->stretch_wheel_steps != 0;
    int ok;
    lock_edit(device, audio);
    ok = cancel ? ts_instrument_stretch_gesture_cancel(
                      instrument, &ui->stretch_gesture, error, sizeof(error)) :
                  ts_instrument_stretch_gesture_commit(
                      instrument, &ui->stretch_gesture, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok && ui->stretch_gesture.active) {
        char restore_error[160];
        lock_edit(device, audio);
        (void)ts_instrument_stretch_gesture_cancel(
            instrument, &ui->stretch_gesture,
            restore_error, sizeof(restore_error));
        unlock_edit(device, audio, ui, instrument);
    }
    ui->stretch_wheel_active = 0;
    ui->stretch_wheel_steps = 0;
    if (cancel || !changed) ui->has_stretch_readout = 0;
    if (ok && cancel)
        snprintf(ui->status, sizeof(ui->status),
                 "TAPE LENGTH CANCELLED - ORIGINAL RESTORED");
    else if (ok && changed)
        snprintf(ui->status, sizeof(ui->status),
                 "TAPE LENGTH X%.3F  PITCH %+.2F ST - ONE UNDO",
                 ratio, pitch);
    else if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "TAPE LENGTH RETURNED TO START - NO EDIT");
    else snprintf(ui->status, sizeof(ui->status),
                  "TAPE LENGTH END FAILED: %.122s", error);
}

static void release_canvas_capture(TsUiState *ui)
{
    (void)SDL_CaptureMouse(SDL_FALSE);
    ui->canvas_capture_raw_x = 0;
    ui->canvas_capture_raw_y = 0;
    ui->canvas_drag_logical_x = 0;
    ui->canvas_drag_start_frames = 0;
}

static int begin_canvas_gesture(SDL_Window *window, SDL_AudioDeviceID device,
                                AudioState *audio, TsUiState *ui,
                                TsInstrument *instrument, int edge,
                                int raw_x, int raw_y)
{
    char error[160];
    int ok;
    cancel_pitch_preview(device, audio, ui, instrument);
    lock_edit(device, audio);
    ok = ts_instrument_canvas_gesture_begin(
        instrument, &ui->canvas_gesture, edge, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS RESIZE FAILED: %.132s", error);
        return 0;
    }
    if (SDL_CaptureMouse(SDL_TRUE) != 0) {
        char restore_error[160];
        lock_edit(device, audio);
        (void)ts_instrument_canvas_gesture_cancel(
            instrument, &ui->canvas_gesture,
            restore_error, sizeof(restore_error));
        unlock_edit(device, audio, ui, instrument);
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS MOUSE CAPTURE FAILED: %.113s", SDL_GetError());
        return 0;
    }
    ui->canvas_capture_raw_x = raw_x;
    ui->canvas_capture_raw_y = raw_y;
    ui->canvas_drag_logical_x = 0;
    ui->canvas_drag_start_frames = instrument->current.frames;
    SDL_WarpMouseInWindow(window, raw_x, raw_y);
    snprintf(ui->status, sizeof(ui->status),
             "DRAG %s CANVAS EDGE - RELEASE COMMITS ONCE  ESC CANCELS",
             edge == 1 ? "LEFT" : "RIGHT");
    return 1;
}

static int64_t canvas_delta_from_capture(SDL_Window *window,
                                         const TsUiState *ui)
{
    int window_width = 0;
    int window_height = 0;
    double frames_per_logical_pixel;
    double value;
    int64_t delta;
    SDL_GetWindowSize(window, &window_width, &window_height);
    (void)window_height;
    if (window_width <= 0 || ui->canvas_drag_start_frames == 0) return 0;
    frames_per_logical_pixel =
        (double)ui->canvas_drag_start_frames / (double)TS_WAVE_W;
    if (frames_per_logical_pixel < 1.0) frames_per_logical_pixel = 1.0;
    value = (double)ui->canvas_drag_logical_x * frames_per_logical_pixel;
    if (ui->canvas_gesture.edge == 1) value = -value;
    if (value > (double)INT64_MAX) return INT64_MAX;
    if (value < (double)INT64_MIN) return INT64_MIN;
    delta = value >= 0.0 ? (int64_t)(value + 0.5) : (int64_t)(value - 0.5);
    return delta;
}

static void preserve_canvas_audition_position(AudioState *audio,
                                              const TsUiState *ui,
                                              const TsInstrument *instrument,
                                              int edge,
                                              int64_t coordinate_delta)
{
    TsAuditionPlan plan;
    double position;
    if (!audio->playing || audio->bank_slot >= 0 ||
        audio->source != TS_AUDITION_CURRENT ||
        !audition_plan_ui(instrument, ui, audio->source, audio->range, &plan))
        return;
    position = audio->position;
    if (edge == 1) position += (double)coordinate_delta;
    if (position < (double)plan.first) position = (double)plan.first;
    if (position >= (double)plan.last) {
        position = audio->looping ? (double)plan.first :
                   plan.last > plan.first ? (double)(plan.last - 1u) :
                   (double)plan.first;
    }
    audio->position = position;
    audio->range_start = plan.first;
    audio->range_end = plan.last;
}

static void preview_canvas_capture(SDL_Window *window, SDL_AudioDeviceID device,
                                   AudioState *audio, TsUiState *ui,
                                   TsInstrument *instrument, int raw_x)
{
    char error[160];
    int raw_delta = raw_x - ui->canvas_capture_raw_x;
    int window_width = 0;
    int window_height = 0;
    int logical_delta;
    int64_t requested;
    int64_t previous_delta;
    int ok;
    if (!ui->canvas_gesture.active || raw_delta == 0) return;
    SDL_GetWindowSize(window, &window_width, &window_height);
    (void)window_height;
    if (window_width <= 0) return;
    logical_delta = (int)((int64_t)raw_delta * TS_UI_WIDTH / window_width);
    if (logical_delta == 0) logical_delta = raw_delta < 0 ? -1 : 1;
    if (logical_delta > 0 && ui->canvas_drag_logical_x > INT_MAX - logical_delta)
        ui->canvas_drag_logical_x = INT_MAX;
    else if (logical_delta < 0 &&
             ui->canvas_drag_logical_x < INT_MIN - logical_delta)
        ui->canvas_drag_logical_x = INT_MIN;
    else ui->canvas_drag_logical_x += logical_delta;
    requested = canvas_delta_from_capture(window, ui);
    previous_delta = ui->canvas_gesture.delta_frames;
    lock_edit(device, audio);
    ok = ts_instrument_canvas_gesture_preview(
        instrument, &ui->canvas_gesture, requested, error, sizeof(error));
    if (ok)
        preserve_canvas_audition_position(
            audio, ui, instrument, ui->canvas_gesture.edge,
            ui->canvas_gesture.delta_frames - previous_delta);
    unlock_edit(device, audio, ui, instrument);
    SDL_WarpMouseInWindow(window, ui->canvas_capture_raw_x,
                         ui->canvas_capture_raw_y);
    if (ok) {
        double seconds = instrument->current.sample_rate > 0 ?
            (double)instrument->current.frames /
            (double)instrument->current.sample_rate : 0.0;
        double delta_seconds = instrument->current.sample_rate > 0 ?
            (double)ui->canvas_gesture.delta_frames /
            (double)instrument->current.sample_rate : 0.0;
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS %.3F S (%+.3F S) - RELEASE COMMITS ONCE",
                 seconds, delta_seconds);
    } else {
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS RESIZE LIMIT: %.132s", error);
    }
}

static void end_canvas_gesture(SDL_Window *window, SDL_AudioDeviceID device,
                               AudioState *audio, TsUiState *ui,
                               TsInstrument *instrument, int cancel)
{
    char error[160];
    int64_t delta = ui->canvas_gesture.delta_frames;
    int edge = ui->canvas_gesture.edge;
    int ok;
    (void)window;
    lock_edit(device, audio);
    ok = cancel ? ts_instrument_canvas_gesture_cancel(
                      instrument, &ui->canvas_gesture, error, sizeof(error)) :
                  ts_instrument_canvas_gesture_commit(
                      instrument, &ui->canvas_gesture, error, sizeof(error));
    if (ok)
        preserve_canvas_audition_position(
            audio, ui, instrument, edge, cancel ? -delta : 0);
    unlock_edit(device, audio, ui, instrument);
    if (!ok && ui->canvas_gesture.active) {
        char restore_error[160];
        lock_edit(device, audio);
        (void)ts_instrument_canvas_gesture_cancel(
            instrument, &ui->canvas_gesture,
            restore_error, sizeof(restore_error));
        preserve_canvas_audition_position(audio, ui, instrument, edge, -delta);
        unlock_edit(device, audio, ui, instrument);
    }
    release_canvas_capture(ui);
    if (ok && cancel)
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS RESIZE CANCELLED - ORIGINAL RESTORED");
    else if (ok && delta != 0)
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS RESIZED %+.3F S - ONE UNDO",
                 instrument->current.sample_rate > 0 ?
                 (double)delta / (double)instrument->current.sample_rate : 0.0);
    else if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS RETURNED TO START - NO EDIT");
    else snprintf(ui->status, sizeof(ui->status),
                  "CANVAS RESIZE END FAILED: %.120s", error);
}

static void apply_canvas_action(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, TsInstrument *instrument,
                                TsUiCanvasAction action)
{
    char error[160];
    int ok = 0;
    if (action == TS_UI_CANVAS_ACTION_GRID_COARSER) {
        ok = ts_instrument_cycle_grid_divisions(instrument, -1);
        snprintf(ui->status, sizeof(ui->status), ok ?
                 "GRID DIV %u - FULL CANVAS ANCHOR" : "GRID ALREADY COARSEST",
                 instrument->grid_divisions);
        return;
    }
    if (action == TS_UI_CANVAS_ACTION_GRID_FINER) {
        ok = ts_instrument_cycle_grid_divisions(instrument, 1);
        snprintf(ui->status, sizeof(ui->status), ok ?
                 "GRID DIV %u - FULL CANVAS ANCHOR" : "GRID ALREADY FINEST",
                 instrument->grid_divisions);
        return;
    }
    if (action == TS_UI_CANVAS_ACTION_GRID_SNAP) {
        (void)ts_instrument_toggle_grid_snap(instrument);
        snprintf(ui->status, sizeof(ui->status),
                 "GRID %s - ZERO-CROSSING SAFETY ALWAYS ON",
                 instrument->grid_snap == TS_GRID_SNAP_ALL ?
                 "SNAP SELECTION + MOVEMENT" :
                 instrument->grid_snap == TS_GRID_SNAP_MOVE_ONLY ?
                 "SNAP MOVEMENT ONLY" : "SNAP OFF");
        return;
    }
    cancel_pitch_preview(device, audio, ui, instrument);
    lock_edit(device, audio);
    if (action == TS_UI_CANVAS_ACTION_HALF)
        ok = ts_instrument_half_canvas(instrument, error, sizeof(error));
    else if (action == TS_UI_CANVAS_ACTION_DOUBLE)
        ok = ts_instrument_double_canvas(instrument, error, sizeof(error));
    if (ok)
        preserve_canvas_audition_position(audio, ui, instrument, 2, 0);
    unlock_edit(device, audio, ui, instrument);
    if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "CANVAS %s - %zu FRAMES  DIV %u  ONE UNDO",
                 action == TS_UI_CANVAS_ACTION_HALF ? "HALVED" : "DOUBLED",
                 instrument->current.frames, instrument->grid_divisions);
    else snprintf(ui->status, sizeof(ui->status),
                  "CANVAS %s FAILED: %.116s",
                  action == TS_UI_CANVAS_ACTION_HALF ? "HALF" : "DOUBLE", error);
}

static void history_move(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                         TsInstrument *instrument, int redo)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = redo ? ts_instrument_redo(instrument, error, sizeof(error)) :
                ts_instrument_undo(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) ui->has_stretch_readout = 0;
    if (ok) snprintf(ui->status, sizeof(ui->status), "%s", redo ? "REDO" : "UNDO");
    else snprintf(ui->status, sizeof(ui->status), "%.150s", error);
}

static void sync_playing_loop(SDL_AudioDeviceID device, AudioState *audio,
                              const TsInstrument *instrument);

static void set_loop(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                     TsInstrument *instrument)
{
    char error[160];
    int bank_slot = ui->bank_view_slot;
    int selected_automatically = !instrument->has_selection;
    int ok;
    if (bank_slot >= 0 && bank_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[bank_slot].occupied) {
        if (device) SDL_LockAudioDevice(device);
        ok = ts_instrument_bank_set_loop_full(instrument, bank_slot,
                                              error, sizeof(error));
        if (device) SDL_UnlockAudioDevice(device);
        if (ok) {
            sync_playing_loop(device, audio, instrument);
            snprintf(ui->status, sizeof(ui->status),
                     "BANK %02d WHOLE SAMPLE LOOPED - DRAG FLAGS TO TRIM",
                     bank_slot + 1);
        } else snprintf(ui->status, sizeof(ui->status),
                        "BANK LOOP FAILED: %.135s", error);
        return;
    }
    lock_edit(device, audio);
    ok = ts_instrument_set_loop_from_selection(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok && selected_automatically)
        snprintf(ui->status, sizeof(ui->status),
                 "WHOLE TILE SELECTED AND LOOPED - %zu FRAMES",
                 instrument->loop_last - instrument->loop_first);
    else if (ok) snprintf(ui->status, sizeof(ui->status),
                          "LOOP SET %zu FRAMES - ZERO SNAPPED",
                          instrument->loop_last - instrument->loop_first);
    else snprintf(ui->status, sizeof(ui->status), "LOOP FAILED: %.140s", error);
}

static void clear_loop(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                       TsInstrument *instrument)
{
    char error[160];
    int bank_slot = ui->bank_view_slot;
    int ok;
    if (bank_slot >= 0 && bank_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[bank_slot].occupied) {
        if (device) SDL_LockAudioDevice(device);
        ok = ts_instrument_bank_clear_loop(instrument, bank_slot,
                                           error, sizeof(error));
        if (device) SDL_UnlockAudioDevice(device);
        if (ok) sync_playing_loop(device, audio, instrument);
        if (ok) snprintf(ui->status, sizeof(ui->status),
                         "BANK %02d LOOP CLEARED - ONE-SHOT AUDITION",
                         bank_slot + 1);
        else snprintf(ui->status, sizeof(ui->status), "%.150s", error);
        return;
    }
    lock_edit(device, audio);
    ok = ts_instrument_clear_loop(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    snprintf(ui->status, sizeof(ui->status), "%s", ok ? "LOOP CLEARED" : error);
}

static void set_loop_crossfade(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                               TsInstrument *instrument, float milliseconds)
{
    char error[160];
    int bank_slot = ui->bank_view_slot;
    int ok;
    if (bank_slot >= 0 && bank_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[bank_slot].occupied) {
        if (device) SDL_LockAudioDevice(device);
        ok = ts_instrument_bank_set_loop_crossfade(instrument, bank_slot,
                                                   milliseconds,
                                                   error, sizeof(error));
        if (device) SDL_UnlockAudioDevice(device);
        if (ok) sync_playing_loop(device, audio, instrument);
        if (ok) snprintf(ui->status, sizeof(ui->status),
                         "BANK %02d LOOP CROSSFADE %.1F MS",
                         bank_slot + 1,
                         instrument->bank[bank_slot].loop_crossfade_ms);
        else snprintf(ui->status, sizeof(ui->status), "%.150s", error);
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    ok = ts_instrument_set_loop_crossfade(instrument, milliseconds, error, sizeof(error));
    if (ok && audio->looping && audio->sample != NULL) {
        TsAuditionPlan plan = {audio->sample, audio->range_start, audio->range_end};
        audio->crossfade_frames = ts_audition_crossfade_frames(
            &plan, instrument->loop_crossfade_ms);
    }
    if (ok) ts_note_bank_sync(&audio->notes, instrument, audio->output_rate);
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) snprintf(ui->status, sizeof(ui->status), "LOOP CROSSFADE %.1F MS",
                     instrument->loop_crossfade_ms);
    else snprintf(ui->status, sizeof(ui->status), "%.150s", error);
}

static void cycle_loop_mode(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                            TsInstrument *instrument)
{
    char error[160];
    int bank_slot = ui->bank_view_slot;
    TsLoopMode current_mode = bank_slot >= 0 && bank_slot < TS_BANK_SLOT_COUNT &&
                              instrument->bank[bank_slot].occupied ?
                              instrument->bank[bank_slot].loop_mode :
                              instrument->loop_mode;
    TsLoopMode mode = (TsLoopMode)((current_mode + 1) % TS_LOOP_MODE_COUNT);
    int ok;
    if (bank_slot >= 0 && bank_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[bank_slot].occupied) {
        if (device) SDL_LockAudioDevice(device);
        ok = ts_instrument_bank_set_loop_mode(instrument, bank_slot, mode,
                                              error, sizeof(error));
        if (device) SDL_UnlockAudioDevice(device);
        if (ok) sync_playing_loop(device, audio, instrument);
        if (ok) snprintf(ui->status, sizeof(ui->status),
                         "BANK %02d LOOP MODE %s",
                         bank_slot + 1, ts_loop_mode_name(mode));
        else snprintf(ui->status, sizeof(ui->status),
                      "BANK LOOP MODE FAILED: %.118s", error);
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    ok = ts_instrument_set_loop_mode(instrument, mode, error, sizeof(error));
    if (ok && audio->playing && audio->bank_slot < 0 && audio->looping) {
        audio->loop_mode = mode;
        audio->loop_direction = mode == TS_LOOP_REVERSE ? -1 : 1;
        if (mode == TS_LOOP_REVERSE) audio->position = (double)(audio->range_end - 1u);
    }
    if (ok) ts_note_bank_sync(&audio->notes, instrument, audio->output_rate);
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), ok ? "LOOP MODE %s" : "LOOP MODE FAILED: %.128s",
             ok ? ts_loop_mode_name(mode) : error);
}

static void sync_playing_loop(SDL_AudioDeviceID device, AudioState *audio,
                              const TsInstrument *instrument)
{
    TsAuditionPlan plan;
    if (device) SDL_LockAudioDevice(device);
    if (audio->playing && audio->bank_slot >= 0 &&
        audio->bank_slot < TS_BANK_SLOT_COUNT &&
        instrument->bank[audio->bank_slot].occupied) {
        const TsBankSlot *slot = &instrument->bank[audio->bank_slot];
        if (!ts_bank_audition_plan(instrument, audio->bank_slot, &plan)) {
            audio->playing = 0;
            audio->bank_slot = -1;
            if (device) SDL_UnlockAudioDevice(device);
            return;
        }
        audio->sample = plan.sample;
        audio->range_start = plan.first;
        audio->range_end = plan.last;
        audio->range = slot->has_loop ? TS_AUDITION_LOOP : TS_AUDITION_ALL;
        audio->looping = slot->has_loop;
        audio->crossfade_frames = slot->has_loop ?
                                  ts_audition_crossfade_frames(
                                      &plan, slot->loop_crossfade_ms) : 0;
        audio->pitch = ts_tuning_pair_audition_pitch(&slot->tuning,
                                                     &slot->audible_tuning);
        if (audio->output_rate > 0)
            audio->step = (double)plan.sample->sample_rate / audio->output_rate *
                          audio->pitch;
        audio->loop_mode = slot->loop_mode;
        if (audio->loop_mode == TS_LOOP_REVERSE) audio->loop_direction = -1;
        else if (audio->loop_mode == TS_LOOP_FORWARD) audio->loop_direction = 1;
        else if (audio->loop_direction == 0) audio->loop_direction = 1;
        if (audio->position < (double)plan.first || audio->position >= (double)plan.last)
            audio->position = audio->loop_mode == TS_LOOP_REVERSE && slot->has_loop ?
                              (double)(plan.last - 1u) : (double)plan.first;
    } else if (audio->playing && audio->bank_slot < 0 && audio->looping &&
        ts_audition_plan(instrument, audio->source, TS_AUDITION_LOOP, &plan)) {
        audio->sample = plan.sample;
        audio->range_start = plan.first;
        audio->range_end = plan.last;
        audio->crossfade_frames = ts_audition_crossfade_frames(
            &plan, instrument->loop_crossfade_ms);
        audio->pitch = ts_instrument_audition_pitch(instrument);
        if (audio->output_rate > 0)
            audio->step = (double)plan.sample->sample_rate / audio->output_rate *
                          audio->pitch;
        audio->loop_mode = instrument->loop_mode;
        if (audio->loop_mode == TS_LOOP_REVERSE) audio->loop_direction = -1;
        else if (audio->loop_mode == TS_LOOP_FORWARD) audio->loop_direction = 1;
        else if (audio->loop_direction == 0) audio->loop_direction = 1;
        if (audio->position < (double)plan.first || audio->position >= (double)plan.last)
            audio->position = audio->loop_mode == TS_LOOP_REVERSE ?
                              (double)(plan.last - 1u) : (double)plan.first;
    }
    ts_note_bank_sync(&audio->notes, instrument, audio->output_rate);
    if (device) SDL_UnlockAudioDevice(device);
}

static void begin_bank_audition(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, const TsInstrument *instrument,
                                int slot_index, int output_rate)
{
    const TsBankSlot *slot;
    TsAuditionPlan plan;
    if (slot_index < 0 || slot_index >= TS_BANK_SLOT_COUNT) return;
    if (ui->workbench_loop_persistent) {
        snprintf(ui->status, sizeof(ui->status),
                 "LOOP LOCKED - SHIFT+LOOP TO RELEASE");
        return;
    }
    if (ui->workbench_loop_active) stop_all(device, audio, ui);
    slot = &instrument->bank[slot_index];
    ui->bank_view_slot = slot_index;
    if (device) SDL_LockAudioDevice(device);
    ts_note_bank_clear(&audio->notes);
    audio->playing = 0;
    audio->bank_slot = -1;
    if (!slot->occupied) {
        audio->sample = NULL;
        if (device) SDL_UnlockAudioDevice(device);
        snprintf(ui->status, sizeof(ui->status), "BANK %02d EMPTY - SILENCE",
                 slot_index + 1);
        return;
    }
    if (!device || output_rate <= 0) {
        audio->sample = &slot->sample;
        if (device) SDL_UnlockAudioDevice(device);
        snprintf(ui->status, sizeof(ui->status), "BANK %02d SHOWN - AUDIO UNAVAILABLE",
                 slot_index + 1);
        return;
    }
    if (!ts_bank_audition_plan(instrument, slot_index, &plan)) {
        audio->sample = NULL;
        if (device) SDL_UnlockAudioDevice(device);
        snprintf(ui->status, sizeof(ui->status), "BANK %02d CANNOT AUDITION",
                 slot_index + 1);
        return;
    }
    audio->sample = &slot->sample;
    audio->loop_mode = slot->loop_mode;
    audio->loop_direction = audio->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
    audio->position = slot->has_loop && audio->loop_direction < 0 ?
                      (double)(plan.last - 1u) : (double)plan.first;
    audio->pitch = ts_tuning_pair_audition_pitch(&slot->tuning,
                                                 &slot->audible_tuning);
    audio->step = (double)slot->sample.sample_rate / output_rate * audio->pitch;
    audio->range_start = plan.first;
    audio->range_end = plan.last;
    audio->source = TS_AUDITION_CURRENT;
    audio->range = slot->has_loop ? TS_AUDITION_LOOP : TS_AUDITION_ALL;
    audio->looping = slot->has_loop;
    audio->crossfade_frames = slot->has_loop ?
                              ts_audition_crossfade_frames(
                                  &plan, slot->loop_crossfade_ms) : 0;
    audio->bank_slot = slot_index;
    audio->playing = 1;
    SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "PLAYING BANK %02d %s %s",
             slot_index + 1, ts_bank_capture_name(slot->capture_kind),
             slot->has_loop ? ts_loop_mode_name(slot->loop_mode) : "ONE-SHOT");
}

static void capture_bank_slot(SDL_AudioDeviceID device, TsUiState *ui,
                              TsInstrument *instrument,
                              int slot, TsUiBankAction action)
{
    char error[160];
    int ok;
    TsBankCaptureKind kind = action == TS_UI_BANK_ACTION_CAPTURE_LOOP ?
                             TS_BANK_CAPTURE_LOOP :
                             action == TS_UI_BANK_ACTION_CAPTURE_SELECTION ?
                             TS_BANK_CAPTURE_SELECTION : TS_BANK_CAPTURE_CURRENT;
    if (device) SDL_LockAudioDevice(device);
    ok = ts_ui_execute_bank_action(instrument, slot, action,
                                   error, sizeof(error));
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CAPTURED %s TO BANK %02d",
                     ts_bank_capture_name(kind), slot + 1);
    else snprintf(ui->status, sizeof(ui->status), "BANK CAPTURE FAILED: %.130s", error);
}

static void clone_bank_slot(SDL_AudioDeviceID device, AudioState *audio,
                            TsUiState *ui, TsInstrument *instrument, int slot)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_ui_execute_bank_action(instrument, slot, TS_UI_BANK_ACTION_CLONE,
                                   error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "CLONED ACTIVE TILE TO BANK %02d - INDEPENDENT EDIT COPY", slot + 1);
    else
        snprintf(ui->status, sizeof(ui->status), "BANK CLONE FAILED: %.132s", error);
}

static void clear_bank_slot(SDL_AudioDeviceID device, AudioState *audio,
                            TsUiState *ui, TsInstrument *instrument, int slot)
{
    char error[160];
    int clearing_active = instrument->selected_slot == slot;
    int ok;
    if (audio->capture.state != TS_CAPTURE_IDLE &&
        slot == audio->capture.destination_slot) {
        snprintf(ui->status, sizeof(ui->status),
                 "TILE %02d IS THE LOCKED CAPTURE DESTINATION - ESC CANCELS",
                 slot + 1);
        return;
    }
    if (audio->capture.state == TS_CAPTURE_RECORDING &&
        slot == audio->capture.source_slot) {
        snprintf(ui->status, sizeof(ui->status),
                 "TILE %02d IS THE RECORDING SOURCE", slot + 1);
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    if (audio->bank_slot == slot || clearing_active) {
        audio->playing = 0;
        audio->bank_slot = -1;
        ts_note_bank_clear(&audio->notes);
        if (clearing_active && !ui->workbench_loop_persistent) {
            ui->workbench_loop_active = 0;
        }
    }
    ok = ts_ui_execute_bank_action(instrument, slot, TS_UI_BANK_ACTION_CLEAR,
                                   error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok && clearing_active)
        snprintf(ui->status, sizeof(ui->status),
                 "CLEARED BANK %02d - EMPTY DESTINATION SELECTED", slot + 1);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "CLEARED BANK %02d", slot + 1);
    else snprintf(ui->status, sizeof(ui->status), "BANK CLEAR FAILED: %.132s", error);
}

static void clear_all_bank_slots(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    int ok;
    if (audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL CAPTURE BEFORE CLEAR ALL");
        return;
    }
    if (!ui->bank_clear_armed) {
        ui->bank_clear_armed = 1;
        snprintf(ui->status, sizeof(ui->status),
                 "CLICK CLEAR ALL AGAIN - ALL 16 BANK SLOTS WILL BE EMPTIED");
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    audio->bank_slot = -1;
    ts_note_bank_clear(&audio->notes);
    ok = ts_instrument_bank_clear_all(instrument, error, sizeof(error));
    if (device) SDL_UnlockAudioDevice(device);
    ui->bank_clear_armed = 0;
    ui->bank_view_slot = -1;
    if (ok)
        snprintf(ui->status, sizeof(ui->status),
                 "CLEARED ALL 16 BANK SLOTS");
    else
        snprintf(ui->status, sizeof(ui->status),
                 "CLEAR ALL FAILED: %.137s", error);
}

static void begin_bank_rename(TsUiState *ui, const TsInstrument *instrument, int slot)
{
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        snprintf(ui->status, sizeof(ui->status), "INVALID BANK SLOT");
        return;
    }
    if (!instrument->bank[slot].occupied) {
        snprintf(ui->status, sizeof(ui->status), "BANK %02d EMPTY - CAPTURE BEFORE RENAMING",
                 slot + 1);
        return;
    }
    ui->renaming_bank_slot = slot;
    snprintf(ui->bank_rename, sizeof(ui->bank_rename), "%s",
             instrument->bank[slot].sample.name);
    ui->bank_rename_cursor = strlen(ui->bank_rename);
    SDL_StartTextInput();
    snprintf(ui->status, sizeof(ui->status), "RENAMING BANK %02d", slot + 1);
}

static void text_insert_ascii(char *buffer, size_t capacity, size_t *cursor,
                              const char *text)
{
    size_t length = strlen(buffer);
    if (*cursor > length) *cursor = length;
    while (text != NULL && *text != '\0' && length + 1u < capacity) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32u && c <= 126u) {
            memmove(buffer + *cursor + 1u, buffer + *cursor,
                    length - *cursor + 1u);
            buffer[(*cursor)++] = (char)c;
            ++length;
        }
    }
}

static void text_backspace(char *buffer, size_t *cursor)
{
    size_t length = strlen(buffer);
    if (*cursor > length) *cursor = length;
    if (*cursor > 0) {
        memmove(buffer + *cursor - 1u, buffer + *cursor,
                length - *cursor + 1u);
        --*cursor;
    }
}

static void text_delete(char *buffer, size_t *cursor)
{
    size_t length = strlen(buffer);
    if (*cursor > length) *cursor = length;
    if (*cursor < length)
        memmove(buffer + *cursor, buffer + *cursor + 1u, length - *cursor);
}

static void text_move_cursor(const char *buffer, size_t *cursor, int amount)
{
    ptrdiff_t position = (ptrdiff_t)*cursor + amount;
    size_t length = strlen(buffer);
    if (position < 0) position = 0;
    if ((size_t)position > length) position = (ptrdiff_t)length;
    *cursor = (size_t)position;
}

static void select_config_field(TsUiState *ui, TsConfigField field)
{
    const char *value;
    if (field < 0) field = (TsConfigField)(TS_CONFIG_FIELD_COUNT - 1);
    if ((int)field >= TS_CONFIG_FIELD_COUNT) field = TS_CONFIG_SAMPLE_PATH;
    ui->config_field = field;
    value = ts_config_field_const(&ui->config, field);
    ui->config_cursor = value != NULL ? strlen(value) : 0;
}

static void begin_config(TsUiState *ui)
{
    ui->config_before_edit = ui->config;
    ui->config_open = 1;
    select_config_field(ui, TS_CONFIG_SAMPLE_PATH);
    SDL_StartTextInput();
    snprintf(ui->status, sizeof(ui->status), "EDITING TAPESISTER PATHS");
}

static void begin_palette(TsUiState *ui)
{
    ts_ui_begin_palette_edit(ui);
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "EDITING LIVE PALETTE");
}

static void finish_palette(TsUiState *ui, int cancel)
{
    ts_ui_finish_palette_edit(ui, cancel);
    select_config_field(ui, ui->config_field);
    SDL_StartTextInput();
    snprintf(ui->status, sizeof(ui->status), cancel ?
             "PALETTE CHANGES CANCELLED" : "PALETTE APPLIED - SAVE TS TO KEEP IT");
}

static void palette_import(TsUiState *ui)
{
    char error[160];
    TsPalette loaded = ui->palette;
    if (ts_palette_load(&loaded, tapehead_palette_path(), error, sizeof(error))) {
        ui->palette = loaded;
        snprintf(ui->status, sizeof(ui->status), "IMPORTED TAPEHEAD.PAL");
    } else snprintf(ui->status, sizeof(ui->status), "PALETTE IMPORT FAILED: %.130s", error);
}

static void palette_save(TsUiState *ui, int tapehead)
{
    char error[160];
    const char *path = tapehead ? tapehead_palette_path() : tapesister_palette_path();
    if ((tapehead ? ts_palette_save_tapehead(&ui->palette, path,
                                             error, sizeof(error)) :
                    ts_palette_save(&ui->palette, path,
                                    error, sizeof(error))))
        snprintf(ui->status, sizeof(ui->status), "%s %.112s",
                 tapehead ? "EXPORTED" : "SAVED", path);
    else snprintf(ui->status, sizeof(ui->status), "PALETTE SAVE FAILED: %.132s", error);
}

static void palette_adjust(TsUiState *ui, int amount)
{
    int value;
    if (ui->palette_channel == 3 || ui->palette_channel == 4) {
        int *contrast = ui->palette_channel == 3 ?
                        &ui->palette.desktop_contrast :
                        &ui->palette.buttons_contrast;
        value = *contrast + amount;
        if (value < 1) value = 1;
        if (value > 100) value = 100;
        *contrast = value;
    } else {
        value = ts_palette_component(&ui->palette,
                    (TsPaletteColor)ui->palette_entry, ui->palette_channel) + amount;
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        ts_palette_set_component(&ui->palette, (TsPaletteColor)ui->palette_entry,
                                 ui->palette_channel, (uint8_t)value);
    }
}

static void cancel_config(TsUiState *ui)
{
    ui->config = ui->config_before_edit;
    ui->config_open = 0;
    ui->config_cursor = 0;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "CONFIG CHANGES CANCELLED");
}

static void save_config(TsUiState *ui)
{
    char error[160];
    if (!ts_config_save(&ui->config, config_file_path(), error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "CONFIG SAVE FAILED: %.130s", error);
        return;
    }
    ui->config_before_edit = ui->config;
    if (path_is_directory(ui->config.sample_path))
        snprintf(ui->browser.directory, sizeof(ui->browser.directory), "%s",
                 ui->config.sample_path);
    ui->config_open = 0;
    ui->config_cursor = 0;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "SAVED CONFIG %.112s", config_file_path());
}

static void config_use_cwd(TsUiState *ui)
{
    char *field = ts_config_field(&ui->config, ui->config_field);
    if (field == NULL) return;
#ifdef _WIN32
    if (_getcwd(field, TS_CONFIG_PATH_MAX) == NULL)
#else
    if (getcwd(field, TS_CONFIG_PATH_MAX) == NULL)
#endif
        snprintf(ui->status, sizeof(ui->status), "COULD NOT READ CURRENT DIRECTORY");
    else {
        ui->config_cursor = strlen(field);
        snprintf(ui->status, sizeof(ui->status), "CURRENT DIRECTORY COPIED TO %s",
                 ts_config_field_name(ui->config_field));
    }
}

static void cancel_bank_rename(TsUiState *ui)
{
    int slot = ui->renaming_bank_slot;
    ui->renaming_bank_slot = -1;
    ui->bank_rename[0] = '\0';
    ui->bank_rename_cursor = 0;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "BANK %02d RENAME CANCELLED", slot + 1);
}

static void finish_bank_rename(TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    int slot = ui->renaming_bank_slot;
    if (!ts_instrument_bank_rename(instrument, slot, ui->bank_rename,
                                   error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "BANK RENAME FAILED: %.132s", error);
        return;
    }
    ui->renaming_bank_slot = -1;
    ui->bank_rename[0] = '\0';
    ui->bank_rename_cursor = 0;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "RENAMED BANK %02d", slot + 1);
}

static void cancel_recipe_rename(TsUiState *ui)
{
    int slot = ui->renaming_recipe_slot;
    ui->renaming_recipe_slot = -1;
    ui->recipe_rename[0] = '\0';
    ui->recipe_rename_cursor = 0;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "RECIPE %02d RENAME CANCELLED", slot + 1);
}

static void finish_recipe_rename(TsUiState *ui)
{
    char error[160];
    int slot = ui->renaming_recipe_slot;
    if (!ts_recipe_bank_rename(&ui->recipes, slot, ui->recipe_rename,
                               error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "RECIPE RENAME FAILED: %.130s", error);
        return;
    }
    ui->renaming_recipe_slot = -1;
    ui->recipe_rename[0] = '\0';
    ui->recipe_rename_cursor = 0;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "RENAMED USER RECIPE %02d", slot + 1);
}

static void begin_export_choice(TsUiState *ui)
{
    ui->export_choice_open = 1;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "EXPORT SELECTED TILE OR COMPLETE COLLECTION");
}

static unsigned bank_modifiers(SDL_Keymod mod)
{
    unsigned result = 0;
    if (mod & KMOD_SHIFT) result |= TS_UI_BANK_MOD_SHIFT;
    if (mod & KMOD_CTRL) result |= TS_UI_BANK_MOD_CTRL;
    if (mod & KMOD_ALT) result |= TS_UI_BANK_MOD_ALT;
    return result;
}

static void browser_open(TsUiState *ui, TsBrowserMode mode)
{
    const char *filename = mode == TS_BROWSER_SAVE_RECIPE ? "tapesister-recipe.tsr" :
                           mode == TS_BROWSER_SAVE_PRESET ? "my-process.tsp" :
                           mode == TS_BROWSER_EXPORT_WAV ? "tapesister-export.wav" : "";
    SDL_StopTextInput();
    if (ts_browser_open(&ui->browser, mode, filename)) {
        if (ts_browser_mode_edits_filename(mode)) SDL_StartTextInput();
        snprintf(ui->status, sizeof(ui->status), "%s", ts_browser_mode_title(mode));
    } else {
        snprintf(ui->status, sizeof(ui->status), "BROWSER FAILED: %.142s", ui->browser.message);
        ts_browser_close(&ui->browser);
    }
}

static TsBrowserMode config_browser_mode(TsConfigField field)
{
    if (field == TS_CONFIG_SAMPLE_PATH) return TS_BROWSER_SELECT_SAMPLE_DIRECTORY;
    if (field == TS_CONFIG_FASTTRACKER_PATH)
        return TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE;
    if (field == TS_CONFIG_EXCHANGE_PATH)
        return TS_BROWSER_SELECT_EXCHANGE_DIRECTORY;
    return TS_BROWSER_SELECT_CDP_BIN_DIRECTORY;
}

static TsConfigField browser_config_field(TsBrowserMode mode)
{
    if (mode == TS_BROWSER_SELECT_SAMPLE_DIRECTORY) return TS_CONFIG_SAMPLE_PATH;
    if (mode == TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE)
        return TS_CONFIG_FASTTRACKER_PATH;
    if (mode == TS_BROWSER_SELECT_EXCHANGE_DIRECTORY)
        return TS_CONFIG_EXCHANGE_PATH;
    return TS_CONFIG_CDP_BIN_PATH;
}

static void browser_open_config_path(TsUiState *ui, TsConfigField field)
{
    TsBrowserMode mode = config_browser_mode(field);
    const char *configured = ts_config_field_const(&ui->config, field);
    char initial[TS_BROWSER_PATH_MAX];
    if (path_is_directory(configured))
        snprintf(ui->browser.directory, sizeof(ui->browser.directory), "%s", configured);
    else if (field == TS_CONFIG_FASTTRACKER_PATH &&
             parent_directory_of(configured, initial, sizeof(initial)))
        snprintf(ui->browser.directory, sizeof(ui->browser.directory), "%s", initial);
    SDL_StopTextInput();
    ui->config_open = 0;
    if (ts_browser_open(&ui->browser, mode, NULL)) {
        snprintf(ui->status, sizeof(ui->status), "%s", ts_browser_mode_title(mode));
    } else {
        snprintf(ui->status, sizeof(ui->status), "BROWSER FAILED: %.142s",
                 ui->browser.message);
        ts_browser_close(&ui->browser);
        ui->config_open = 1;
        SDL_StartTextInput();
    }
}

static void browser_open_bank(TsUiState *ui, const TsInstrument *instrument)
{
    char folder[TS_BROWSER_NAME_MAX + 1];
    if (!ts_instrument_family_folder_name(instrument, folder, sizeof(folder)))
        snprintf(folder, sizeof(folder), "TapeSister_set");
    SDL_StopTextInput();
    if (ts_browser_open(&ui->browser, TS_BROWSER_EXPORT_BANK, folder)) {
        SDL_StartTextInput();
        snprintf(ui->status, sizeof(ui->status), "EXPORT SOUND COLLECTION");
    } else {
        snprintf(ui->status, sizeof(ui->status), "BROWSER FAILED: %.142s", ui->browser.message);
        ts_browser_close(&ui->browser);
    }
}

static const char *exchange_directory(const TsUiState *ui)
{
    return ui->config.exchange_path[0] != '\0' ?
           ui->config.exchange_path : ui->config.sample_path;
}

enum { EXCHANGE_PRESENCE_MAX_AGE_SECONDS = 5 };

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    return slash != NULL ? slash + 1 : path;
}

static int ui_dialog_open(const TsUiState *ui)
{
    return ui->exit_confirm_open || ui->transform_open || ui->drone_open ||
           ui->load_selection_choice_open || ui->palette_open || ui->config_open ||
           ui->renaming_bank_slot >= 0 || ui->renaming_recipe_slot >= 0 ||
           ui->export_choice_open ||
           ui->exchange_dialog != TS_UI_EXCHANGE_NONE ||
           ui->browser.mode != TS_BROWSER_CLOSED;
}

static int stage_incoming_exchange(TsUiState *ui, TsExchangeOffer *offer,
                                   const char *ignored_folder, int force)
{
    char error[160];
    if (!ts_exchange_find_pending(exchange_directory(ui), offer,
                                  error, sizeof(error))) {
        if (force) snprintf(ui->status, sizeof(ui->status), "FT2 INBOX: %.138s", error);
        return 0;
    }
    if (!force && ignored_folder != NULL && ignored_folder[0] != '\0' &&
        strcmp(offer->folder, ignored_folder) == 0) return 0;
    ui->exchange_dialog = TS_UI_EXCHANGE_RECEIVE;
    ui->exchange_layout = offer->layout;
    ui->exchange_item_count = offer->item_count;
    snprintf(ui->exchange_name, sizeof(ui->exchange_name), "%.90s",
             path_basename(offer->folder));
    snprintf(ui->status, sizeof(ui->status),
             "TAPEHEAD TRANSFER STAGED - REVIEW BEFORE REPLACING BANK");
    return 1;
}

static void begin_exchange_send(TsUiState *ui, const TsInstrument *instrument)
{
    int count = ts_instrument_bank_count(instrument);
    if (count <= 0) {
        snprintf(ui->status, sizeof(ui->status), "NO OCCUPIED TILES TO SEND");
        return;
    }
    if (!path_is_directory(exchange_directory(ui))) {
        snprintf(ui->status, sizeof(ui->status),
                 "CONFIGURE AN EXISTING FT2 EXCHANGE OR SAMPLE PATH FIRST");
        return;
    }
    ui->exchange_dialog = TS_UI_EXCHANGE_SEND;
    ui->exchange_item_count = count;
    ui->exchange_force_new_instance = 0;
    ui->exchange_name[0] = '\0';
    snprintf(ui->status, sizeof(ui->status),
             "CHOOSE HOW TAPESISTER TILES SHOULD ARRIVE IN TAPEHEAD");
}

static void send_to_fasttracker(TsUiState *ui, const TsInstrument *instrument,
                                TsExchangeLayout layout)
{
    const char *directory = exchange_directory(ui);
    char destination[TS_BROWSER_PATH_MAX];
    char error[160];
    if (!path_is_directory(directory)) {
        snprintf(ui->status, sizeof(ui->status),
                 "CONFIGURE AN EXISTING FT2 EXCHANGE OR SAMPLE PATH FIRST");
        return;
    }
    if (!ts_exchange_publish_bank(instrument, directory, layout,
                                  destination, sizeof(destination),
                                  error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "FT2 EXPORT FAILED: %.132s", error);
        return;
    }
    if (!ui->exchange_force_new_instance &&
        ts_exchange_presence_active(directory, "tapehead",
                                    EXCHANGE_PRESENCE_MAX_AGE_SECONDS)) {
        snprintf(ui->status, sizeof(ui->status),
                 "SENT %d SAMPLES AS %s - OPEN TAPEHEAD WILL RECEIVE",
                 ts_instrument_bank_count(instrument),
                 layout == TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS ?
                 "SEPARATE INSTRUMENTS" : "ONE INSTRUMENT");
        ui->exchange_force_new_instance = 0;
        return;
    }
    if (ui->config.fasttracker_path[0] == '\0') {
        snprintf(ui->status, sizeof(ui->status),
                 "FT2 COLLECTION READY %.100s - SET EXECUTABLE TO AUTO LAUNCH",
                 destination);
        ui->exchange_force_new_instance = 0;
        return;
    }
    if (!launch_program(ui->config.fasttracker_path, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status),
                 "COLLECTION READY %.60s  LAUNCH FAILED %.48s", destination, error);
        ui->exchange_force_new_instance = 0;
        return;
    }
    snprintf(ui->status, sizeof(ui->status),
             ui->exchange_force_new_instance ?
             "SENT %d SAMPLES AS %s - NEW FASTTRACKER INSTANCE LAUNCHED" :
             "SENT %d SAMPLES AS %s - FASTTRACKER LAUNCHED",
             ts_instrument_bank_count(instrument),
             layout == TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS ?
             "SEPARATE INSTRUMENTS" : "ONE INSTRUMENT");
    ui->exchange_force_new_instance = 0;
}

static int import_incoming_exchange(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, TsInstrument *instrument,
                                    TsExchangeOffer *offer)
{
    char error[160];
    int count = offer->item_count;
    stop_all(device, audio, ui);
    if (!ts_exchange_import_offer(instrument, offer, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "FT2 IMPORT FAILED: %.132s", error);
        return 0;
    }
    ui->exchange_dialog = TS_UI_EXCHANGE_NONE;
    ui->exchange_item_count = 0;
    ui->exchange_name[0] = '\0';
    ui->bank_view_slot = -1;
    ui->load_bank_slot = instrument->selected_slot;
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->has_pitch_suggestion = 0;
    snprintf(ui->status, sizeof(ui->status),
             "IMPORTED %d TAPEHEAD SAMPLES INTO TILES - BANK REPLACED", count);
    ts_exchange_offer_init(offer);
    return 1;
}

static void browser_cancel(TsUiState *ui)
{
    int returning_to_config = ts_browser_mode_selects_config(ui->browser.mode);
    SDL_StopTextInput();
    ts_browser_close(&ui->browser);
    if (returning_to_config) {
        ui->config_open = 1;
        select_config_field(ui, ui->config_field);
        SDL_StartTextInput();
        snprintf(ui->status, sizeof(ui->status), "CONFIG PATH BROWSE CANCELLED");
    } else snprintf(ui->status, sizeof(ui->status), "FILE OPERATION CANCELLED");
}

static int finish_atomic_file(const char *temporary, const char *destination,
                              char *error, size_t error_size)
{
#ifdef _WIN32
    if (MoveFileExA(temporary, destination,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
    snprintf(error, error_size, "Could not replace destination (Windows error %lu)",
             (unsigned long)GetLastError());
#else
    if (rename(temporary, destination) == 0) return 1;
    snprintf(error, error_size, "Could not replace destination: %s", strerror(errno));
#endif
    remove(temporary);
    return 0;
}

static int save_preset_atomic(const TsProcessRecipe *process, const TsTuning *tuning,
                              const TsTuning *audible_tuning,
                              const char *name,
                              const char *destination, char *error, size_t error_size)
{
    char temporary[TS_BROWSER_PATH_MAX + 32];
    TsPortableRecipe recipe;
    int written = snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", destination);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        snprintf(error, error_size, "Destination path is too long");
        return 0;
    }
    if (!ts_recipe_from_process_and_tunings(&recipe, process, tuning,
                                            audible_tuning, name) ||
        !ts_recipe_save(&recipe, temporary, error, error_size)) {
        remove(temporary);
        return 0;
    }
    return finish_atomic_file(temporary, destination, error, error_size);
}

static int export_wav_atomic(const TsSample *sample, const TsTuning *tuning,
                             int has_loop, size_t loop_first, size_t loop_last,
                             TsLoopMode loop_mode,
                             const char *destination,
                             char *error, size_t error_size)
{
    char temporary[TS_BROWSER_PATH_MAX + 32];
    int written = snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", destination);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        snprintf(error, error_size, "Destination path is too long");
        return 0;
    }
    if (!ts_sample_save_wav16_tuned_looped(sample, tuning, has_loop,
                                           loop_first, loop_last, loop_mode,
                                           temporary, error, error_size)) {
        remove(temporary);
        return 0;
    }
    return finish_atomic_file(temporary, destination, error, error_size);
}

static void cancel_selection_load(TsUiState *ui, TsSample *pending)
{
    ts_sample_free(pending);
    ui->load_selection_choice_open = 0;
    ui->load_selection_name[0] = '\0';
    snprintf(ui->status, sizeof(ui->status), "SELECTION LOAD CANCELLED");
}

static void apply_selection_load(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument,
                                 TsSample *pending, int fit)
{
    char error[160];
    size_t source_frames = pending->frames;
    size_t target_frames = instrument->has_selection ?
                           instrument->selection_last - instrument->selection_first : 0;
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_paste(instrument, pending, instrument->selection_first,
                             fit, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "%s LOAD FAILED: %.128s",
                 fit ? "FIT" : "PASTE", error);
        return;
    }
    ts_sample_free(pending);
    ui->load_selection_choice_open = 0;
    ui->load_selection_name[0] = '\0';
    if (fit)
        snprintf(ui->status, sizeof(ui->status),
                 "FIT %zu WAV FRAMES INTO %zu - UNDO AVAILABLE",
                 source_frames, target_frames);
    else
        snprintf(ui->status, sizeof(ui->status),
                 "PASTED %zu WAV FRAMES INTO SELECTION - UNDO AVAILABLE",
                 source_frames);
}

static void browser_action(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument, TsSample *pending_selection_load,
                           TsSamplePages *sample_pages,
                           TsInstrument *parked_record,
                           int record_bank_active)
{
    TsBrowser *browser = &ui->browser;
    char path[TS_BROWSER_PATH_MAX];
    char error[160];
    int ok = 0;
    if (ts_browser_mode_selects_config(browser->mode)) {
        TsBrowserMode mode = browser->mode;
        TsConfigField field = browser_config_field(mode);
        char *destination = ts_config_field(&ui->config, field);
        if (mode == TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE) {
            if (browser->selected >= 0 && browser->selected < browser->entry_count &&
                browser->entries[browser->selected].is_directory) {
                if (!ts_browser_enter_selected_directory(browser))
                    snprintf(browser->message, sizeof(browser->message),
                             "COULD NOT ENTER DIRECTORY");
                return;
            }
            if (!ts_browser_selected_path(browser, path, sizeof(path))) {
                snprintf(browser->message, sizeof(browser->message),
                         "SELECT AN EXECUTABLE FILE");
                return;
            }
        } else snprintf(path, sizeof(path), "%s", browser->directory);
        if (destination == NULL || strlen(path) >= TS_CONFIG_PATH_MAX) {
            snprintf(browser->message, sizeof(browser->message), "SELECTED PATH IS TOO LONG");
            return;
        }
        snprintf(destination, TS_CONFIG_PATH_MAX, "%s", path);
        ui->config_field = field;
        ui->config_cursor = strlen(destination);
        SDL_StopTextInput();
        ts_browser_close(browser);
        ui->config_open = 1;
        SDL_StartTextInput();
        snprintf(ui->status, sizeof(ui->status), "%s SET - SAVE CONFIG TO KEEP IT",
                 ts_config_field_name(field));
        return;
    }
    if (browser->mode == TS_BROWSER_LOAD_WAV && browser->selected >= 0 &&
        browser->selected < browser->entry_count &&
        browser->entries[browser->selected].is_directory) {
        if (!ts_browser_enter_selected_directory(browser))
            snprintf(browser->message, sizeof(browser->message), "COULD NOT ENTER DIRECTORY");
        return;
    }
    if (browser->mode == TS_BROWSER_LOAD_WAV) {
        if (!ts_browser_selected_path(browser, path, sizeof(path))) {
            snprintf(browser->message, sizeof(browser->message), "SELECT A WAV OR TSR FILE");
            return;
        }
        if (!path_is_tsr(path) && !path_is_tsp(path) &&
            instrument->current.data != NULL && instrument->has_selection &&
            instrument->selection_last > instrument->selection_first) {
            ts_sample_free(pending_selection_load);
            if (!ts_sample_load_wav(pending_selection_load, path,
                                    error, sizeof(error))) {
                snprintf(browser->message, sizeof(browser->message),
                         "WAV LOAD FAILED: %.132s", error);
                return;
            }
            snprintf(ui->load_selection_name, sizeof(ui->load_selection_name),
                     "%s", pending_selection_load->name);
            ui->load_selection_choice_open = 1;
            ui->load_bank_slot = -1;
            SDL_StopTextInput();
            ts_browser_close(browser);
            snprintf(ui->status, sizeof(ui->status),
                     "CHOOSE PASTE, FIT, OR CANCEL FOR THE SELECTED RANGE");
            return;
        }
        ok = load_instrument(device, audio, ui, instrument,
                             sample_pages, parked_record,
                             record_bank_active, path);
    } else {
        if (!ts_browser_destination_path(browser, path, sizeof(path))) {
            snprintf(browser->message, sizeof(browser->message), "ENTER A VALID FILENAME");
            return;
        }
        if (browser->mode == TS_BROWSER_EXPORT_BANK && ts_browser_path_exists(path)) {
            snprintf(browser->message, sizeof(browser->message),
                     "FOLDER EXISTS - CHOOSE A NEW NAME");
            return;
        }
        if (ts_browser_path_exists(path) && !browser->overwrite_armed) {
            browser->overwrite_armed = 1;
            snprintf(browser->message, sizeof(browser->message), "PRESS AGAIN TO OVERWRITE");
            return;
        }
        if (browser->mode == TS_BROWSER_SAVE_RECIPE) {
            const TsInstrument *active_sample = record_bank_active ? NULL : instrument;
            const TsInstrument *record_bank = record_bank_active ?
                                              instrument : parked_record;
            ok = ts_sample_pages_save_project(
                sample_pages, active_sample, record_bank,
                path, error, sizeof(error));
            if (ok)
                ui->saved_state_hash = paged_project_state_hash(
                    sample_pages, active_sample, record_bank);
            snprintf(ui->status, sizeof(ui->status), ok ? "SAVED TSR PROJECT %.104s" :
                     "SAVE FAILED: %.135s", ok ? path : error);
        } else if (browser->mode == TS_BROWSER_SAVE_PRESET) {
            char name[TS_RECIPE_NAME_MAX + 1];
            size_t length;
            snprintf(name, sizeof(name), "%.31s", browser->filename);
            length = strlen(name);
            if (length > 4u && name[length - 4u] == '.') name[length - 4u] = '\0';
            ok = save_preset_atomic(&instrument->process, &instrument->tuning,
                                    &instrument->audible_tuning, name, path,
                                    error, sizeof(error));
            snprintf(ui->status, sizeof(ui->status), ok ? "SAVED PROCESS RECIPE %.99s" :
                     "TSP SAVE FAILED: %.131s", ok ? path : error);
        } else if (browser->mode == TS_BROWSER_EXPORT_WAV) {
            ok = export_wav_atomic(&instrument->current, &instrument->tuning,
                                   instrument->has_loop,
                                   instrument->loop_first, instrument->loop_last,
                                   instrument->loop_mode,
                                   path, error, sizeof(error));
            snprintf(ui->status, sizeof(ui->status), ok ? "EXPORTED SELECTED TILE %.101s" :
                     "EXPORT FAILED: %.133s", ok ? path : error);
        } else {
            ok = ts_instrument_export_bank(instrument, path, error, sizeof(error));
            if (ok) snprintf(ui->status, sizeof(ui->status),
                             "EXPORTED %d-SAMPLE COLLECTION %.84s",
                             ts_instrument_bank_count(instrument), path);
            else snprintf(ui->status, sizeof(ui->status),
                          "BANK EXPORT FAILED: %.128s", error);
        }
    }
    if (ok) {
        SDL_StopTextInput();
        ts_browser_close(browser);
    } else if (browser->mode != TS_BROWSER_CLOSED) {
        browser->overwrite_armed = 0;
        snprintf(browser->message, sizeof(browser->message), "%.150s", ui->status);
    }
}

static void browser_activate_selection(SDL_AudioDeviceID device, AudioState *audio,
                                       TsUiState *ui, TsInstrument *instrument,
                                       TsSample *pending_selection_load,
                                       TsSamplePages *sample_pages,
                                       TsInstrument *parked_record,
                                       int record_bank_active)
{
    TsBrowser *browser = &ui->browser;
    if (browser->selected >= 0 && browser->selected < browser->entry_count &&
        browser->entries[browser->selected].is_directory && !browser->filename_focus) {
        if (!ts_browser_enter_selected_directory(browser))
            snprintf(browser->message, sizeof(browser->message), "COULD NOT ENTER DIRECTORY");
        return;
    }
    browser_action(device, audio, ui, instrument, pending_selection_load,
                   sample_pages, parked_record, record_bank_active);
}

static void logical_mouse(SDL_Window *window, int raw_x, int raw_y, int *x, int *y)
{
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    *x = raw_x * TS_UI_WIDTH / ww;
    *y = raw_y * TS_UI_HEIGHT / wh;
}

static float clamp_unit(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static int adjust_hovered_slider(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument,
                                 TsUiSlider slider, int amount, int coarse)
{
    TsProcessRecipe process;
    float step;
    const char *label = NULL;
    if (slider == TS_UI_SLIDER_NONE || amount == 0) return 0;
    step = (coarse ? 0.05f : 0.01f) * (float)amount;
    process = instrument->process;
    switch (slider) {
    case TS_UI_SLIDER_BODY:
        process.body = clamp_unit(process.body + step); label = "BODY"; break;
    case TS_UI_SLIDER_EDGE:
        process.edge = clamp_unit(process.edge + step); label = "EDGE"; break;
    case TS_UI_SLIDER_DRIFT:
        process.drift = clamp_unit(process.drift + step); label = "DRIFT"; break;
    case TS_UI_SLIDER_TUNE_FINE:
        if (ui->has_pitch_suggestion) {
            snprintf(ui->status, sizeof(ui->status),
                     "ACCEPT OR CANCEL THE PITCH SUGGESTION FIRST");
            return 1;
        }
        apply_audible_tuning(device, audio, ui, instrument,
                             instrument->audible_tuning.root_note,
                             instrument->audible_tuning.fine_tune_cents +
                             (float)amount * (coarse ? 5.0f : 1.0f));
        return 1;
    case TS_UI_SLIDER_NOISE_AMOUNT:
        process.noise_amount = clamp_unit(process.noise_amount + step);
        label = "NOISE"; break;
    case TS_UI_SLIDER_FILTER_CUTOFF: {
        float normalized = logf(process.filter_cutoff_hz / 20.0f) / logf(1000.0f);
        normalized = clamp_unit(normalized + step);
        process.filter_cutoff_hz = 20.0f * powf(1000.0f, normalized);
        label = "SHAPE";
        break;
    }
    case TS_UI_SLIDER_FILTER_RESONANCE:
        process.filter_resonance = clamp_unit(process.filter_resonance + step);
        label = "SHAPE"; break;
    case TS_UI_SLIDER_SHAPER_DRIVE: {
        float normalized = (process.shaper_drive - 1.0f) / 15.0f;
        process.shaper_drive = 1.0f + clamp_unit(normalized + step) * 15.0f;
        label = "SHAPE";
        break;
    }
    case TS_UI_SLIDER_SHAPER_MIX:
        process.shaper_mix = clamp_unit(process.shaper_mix + step);
        label = "SHAPE"; break;
    case TS_UI_SLIDER_VARIATION_RANGE:
        instrument->family_mutation = clamp_unit(instrument->family_mutation + step);
        snprintf(ui->status, sizeof(ui->status), "RANGE %d",
                 (int)lrintf(instrument->family_mutation * 100.0f));
        return 1;
    case TS_UI_SLIDER_DELAY_TIME: {
        float normalized = (process.delay_seconds - 0.005f) / 0.995f;
        process.delay_seconds = 0.005f + clamp_unit(normalized + step) * 0.995f;
        label = "DELAY";
        break;
    }
    case TS_UI_SLIDER_DELAY_FEEDBACK:
        process.delay_feedback = clamp_unit(process.delay_feedback / 0.85f + step) * 0.85f;
        label = "DELAY"; break;
    case TS_UI_SLIDER_DELAY_DAMPING:
        process.delay_damping = clamp_unit(process.delay_damping + step);
        label = "DELAY"; break;
    case TS_UI_SLIDER_DELAY_MIX:
        process.delay_mix = clamp_unit(process.delay_mix + step);
        label = "DELAY"; break;
    case TS_UI_SLIDER_REVERB_DECAY:
        process.reverb_decay = clamp_unit(process.reverb_decay / 0.9f + step) * 0.9f;
        label = "SPACE"; break;
    case TS_UI_SLIDER_REVERB_DAMPING:
        process.reverb_damping = clamp_unit(process.reverb_damping + step);
        label = "SPACE"; break;
    case TS_UI_SLIDER_REVERB_MIX:
        process.reverb_mix = clamp_unit(process.reverb_mix + step);
        label = "SPACE"; break;
    case TS_UI_SLIDER_LOOP_CROSSFADE: {
        int bank_slot = ui->bank_view_slot;
        float current = bank_slot >= 0 && bank_slot < TS_BANK_SLOT_COUNT &&
                        instrument->bank[bank_slot].occupied ?
                        instrument->bank[bank_slot].loop_crossfade_ms :
                        instrument->loop_crossfade_ms;
        set_loop_crossfade(device, audio, ui, instrument,
                           clamp_unit(current / 50.0f + step) * 50.0f);
        return 1;
    }
    default:
        return 0;
    }
    apply_process(device, audio, ui, instrument, process, label);
    return 1;
}

static size_t selection_frame_from_x(const TsInstrument *instrument, const TsUiState *ui,
                                     int x)
{
    if (ui->audition_source == TS_AUDITION_PARENT && instrument->parent.frames > 0) {
        size_t parent_frame = ts_ui_parent_frame_from_x(
            ui, instrument->parent.frames, x, TS_WAVE_W);
        if (parent_frame <= instrument->crop_first) return 0;
        if (parent_frame >= instrument->crop_last) return instrument->current.frames;
        return parent_frame - instrument->crop_first;
    }
    return ts_instrument_frame_from_view_x(instrument, x, TS_WAVE_W);
}

static int64_t tape_frame_from_x(const TsInstrument *instrument, int x)
{
    int64_t first = (int64_t)instrument->view_first;
    int64_t span = (int64_t)(instrument->view_last - instrument->view_first);
    if (span <= 0) span = (int64_t)instrument->current.frames;
    return first + (int64_t)x * span / TS_WAVE_W;
}

static const char *tape_gesture_name(TsPostEditKind kind)
{
    if (kind == TS_POST_COPY_OVERWRITE) return "COPY OVERWRITE";
    if (kind == TS_POST_MOVE_MIX) return "MOVE MIX";
    if (kind == TS_POST_MOVE_OVERWRITE) return "MOVE OVERWRITE";
    return "COPY MIX";
}

static int begin_tape_drag(TsUiState *ui, TsInstrument *instrument,
                           int button, SDL_Keymod mod, int x)
{
    TsPostEditKind kind;
    size_t clicked;
    if (!ts_ui_tape_action(button == SDL_BUTTON_RIGHT,
                           bank_modifiers(mod), &kind)) return 0;
    if (!instrument->has_selection ||
        instrument->selection_last <= instrument->selection_first) {
        snprintf(ui->status, sizeof(ui->status), "SELECT TAPE BEFORE DRAGGING IT");
        return 1;
    }
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    clicked = selection_frame_from_x(instrument, ui, x);
    if (clicked < instrument->selection_first || clicked >= instrument->selection_last) {
        snprintf(ui->status, sizeof(ui->status), "START TAPE DRAG INSIDE THE SELECTION");
        return 1;
    }
    ui->tape_dragging = 1;
    ui->tape_drag_button = button;
    ui->tape_source_first = instrument->selection_first;
    ui->tape_source_last = instrument->selection_last;
    ui->tape_grab_offset = clicked - instrument->selection_first;
    ui->tape_destination = (int64_t)instrument->selection_first;
    ui->tape_drag_kind = kind;
    snprintf(ui->status, sizeof(ui->status),
             "%s - DRAG GHOST TO %sZERO-SNAPPED DESTINATION",
             tape_gesture_name(ui->tape_drag_kind),
             ts_instrument_grid_moves_snap(instrument) ? "GRID + " : "");
    return 1;
}

static void update_tape_drag(TsUiState *ui, const TsInstrument *instrument, int x)
{
    int64_t pointer = tape_frame_from_x(instrument, x);
    size_t length = ui->tape_source_last - ui->tape_source_first;
    int64_t destination = pointer - (int64_t)ui->tape_grab_offset;
    if (ts_instrument_grid_moves_snap(instrument) && destination >= 0 &&
        (uint64_t)destination <= instrument->current.frames)
        destination = (int64_t)ts_instrument_grid_target(
            instrument, (size_t)destination);
    ui->tape_destination = ts_sample_snap_tape_destination(
        &instrument->current, destination, length);
    snprintf(ui->status, sizeof(ui->status), "%s GHOST AT %lld - RELEASE TO APPLY",
             tape_gesture_name(ui->tape_drag_kind),
             (long long)ui->tape_destination);
}

static void finish_tape_drag(SDL_AudioDeviceID device, AudioState *audio,
                             TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    TsPostEditKind kind = ui->tape_drag_kind;
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_apply_tape_drag(instrument, kind,
                                       ui->tape_source_first, ui->tape_source_last,
                                       ui->tape_destination, error, sizeof(error));
    ui->tape_dragging = 0;
    ui->tape_drag_button = 0;
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "%s APPLIED - ONE STEP UNDO",
                     tape_gesture_name(kind));
    else snprintf(ui->status, sizeof(ui->status), "TAPE DRAG FAILED: %.132s", error);
}

static int loop_marker_x(const TsInstrument *instrument, const TsUiState *ui, int endpoint)
{
    const TsBankSlot *bank_slot = ui->bank_view_slot >= 0 &&
                                  ui->bank_view_slot < TS_BANK_SLOT_COUNT &&
                                  instrument->bank[ui->bank_view_slot].occupied ?
                                  &instrument->bank[ui->bank_view_slot] : NULL;
    size_t frame = bank_slot != NULL ?
                   (endpoint == 1 ? bank_slot->loop_first : bank_slot->loop_last) :
                   (endpoint == 1 ? instrument->loop_first : instrument->loop_last);
    size_t first;
    size_t last;
    if (bank_slot != NULL) {
        first = 0;
        last = bank_slot->sample.frames;
    } else if (ui->audition_source == TS_AUDITION_PARENT) {
        frame += instrument->crop_first;
        first = ui->parent_view_first;
        last = ui->parent_view_last;
        if (last <= first || last > instrument->parent.frames) {
            first = 0;
            last = instrument->parent.frames;
        }
    } else {
        first = instrument->view_first;
        last = instrument->view_last;
    }
    if (last <= first || frame < first || frame > last) return -1000;
    {
        int x = TS_WAVE_X + (int)((frame - first) * TS_WAVE_W / (last - first));
        return x >= TS_WAVE_X + TS_WAVE_W ? TS_WAVE_X + TS_WAVE_W - 1 : x;
    }
}

static size_t bank_frame_from_x(const TsBankSlot *slot, int x)
{
    if (slot == NULL || slot->sample.frames == 0) return 0;
    if (x < 0) x = 0;
    if (x >= TS_WAVE_W) x = TS_WAVE_W - 1;
    return (size_t)x * slot->sample.frames / (size_t)TS_WAVE_W;
}


typedef struct {
    TsExternalRecorder recorder;
    TsInputMonitor monitor;
    TsLiveWaveform live_waveform;
    size_t waveform_consumed_frames;
    uint32_t peak_hold_until_ms;
    uint32_t clip_hold_until_ms;
    int channels;
    int input_channel;
    uint32_t sample_rate;
    char device_label[128];
} ExternalInputState;

static int external_capture_busy(const ExternalInputState *input)
{
    return input != NULL &&
           (input->recorder.state == TS_EXTERNAL_CAPTURE_ARMED ||
            input->recorder.state == TS_EXTERNAL_CAPTURE_RECORDING ||
            input->recorder.state == TS_EXTERNAL_CAPTURE_COMPLETED);
}

static void external_input_callback(void *userdata, Uint8 *stream, int bytes)
{
    ExternalInputState *input = (ExternalInputState *)userdata;
    const float *samples = (const float *)stream;
    int values = bytes / (int)sizeof(float);
    int channels = input != NULL && input->channels > 0 ? input->channels : 1;
    float block_peak = 0.0f;
    if (input == NULL) return;
    for (int frame = 0; frame + channels <= values; frame += channels) {
        float value = 0.0f;
        if (input->input_channel == 0) {
            for (int channel = 0; channel < channels; ++channel)
                value += samples[frame + channel];
            value /= (float)channels;
        } else {
            int channel = input->input_channel - 1;
            if (channel >= channels) channel = 0;
            value = samples[frame + channel];
        }
        if (fabsf(value) > block_peak) block_peak = fabsf(value);
        ts_input_monitor_push(&input->monitor, value);
        (void)ts_external_recorder_write_sample(&input->recorder, value);
    }
    ts_input_monitor_publish_level(&input->monitor, block_peak);
}

static int ensure_external_input_open(SDL_AudioDeviceID *input_device,
                                      ExternalInputState *input,
                                      const TsConfig *config,
                                      char *error, size_t error_size)
{
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    const char *device_name;
    if (input_device == NULL || input == NULL || config == NULL) return 0;
    if (*input_device != 0) return 1;
    SDL_zero(desired);
    SDL_zero(obtained);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 256;
    desired.callback = external_input_callback;
    desired.userdata = input;
    device_name = config->record_input_device[0] != '\0' ?
                  config->record_input_device : NULL;
    *input_device = SDL_OpenAudioDevice(
        device_name, 1, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
        SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (*input_device == 0) {
        snprintf(error, error_size, "Could not open recording input: %.110s", SDL_GetError());
        diagnostic_log("capture device open failed (nonfatal): %s", error);
        return 0;
    }
    if (obtained.format != AUDIO_F32SYS || obtained.freq <= 0 || obtained.channels == 0) {
        snprintf(error, error_size, "Recording input returned an unsupported audio format");
        SDL_CloseAudioDevice(*input_device);
        *input_device = 0;
        return 0;
    }
    if (config->record_input_channel > (int)obtained.channels) {
        snprintf(error, error_size, "Input channel %d is unavailable; device has %d channel%s",
                 config->record_input_channel, (int)obtained.channels,
                 obtained.channels == 1 ? "" : "s");
        SDL_CloseAudioDevice(*input_device);
        *input_device = 0;
        return 0;
    }
    input->channels = obtained.channels;
    input->input_channel = config->record_input_channel;
    input->sample_rate = (uint32_t)obtained.freq;
    snprintf(input->device_label, sizeof(input->device_label), "%.127s",
             device_name != NULL ? device_name : "SYSTEM DEFAULT");
    diagnostic_log("capture device opened paused: %s rate=%u channels=%d",
                   input->device_label, input->sample_rate, input->channels);
    error[0] = '\0';
    return 1;
}

static TsCaptureState external_ui_state(TsExternalCaptureState state)
{
    if (state == TS_EXTERNAL_CAPTURE_ARMED)
        return TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    if (state == TS_EXTERNAL_CAPTURE_RECORDING)
        return TS_CAPTURE_RECORDING;
    return TS_CAPTURE_IDLE;
}

static void sync_external_capture_ui(SDL_AudioDeviceID input_device,
                                     ExternalInputState *input,
                                     TsUiState *ui)
{
    float callback_peak;
    uint32_t now = SDL_GetTicks();
    if (input_device) SDL_LockAudioDevice(input_device);
    ui->capture_state = external_ui_state(input->recorder.state);
    ui->capture_destination_slot = input->recorder.destination_slot;
    ui->capture_source_slot = -1;
    ui->capture_recorded_frames = input->recorder.recorded_frames;
    ui->capture_capacity_frames = input->recorder.capacity_frames;
    ui->input_sample_rate = input->recorder.sample_rate;
    ui->input_threshold = input->recorder.threshold_amplitude;
    ui->input_meter_active = input->recorder.state == TS_EXTERNAL_CAPTURE_ARMED ||
                             input->recorder.state == TS_EXTERNAL_CAPTURE_RECORDING;
    if (input->recorder.recorded_frames > input->waveform_consumed_frames &&
        input->recorder.buffer != NULL) {
        size_t available = input->recorder.recorded_frames -
                           input->waveform_consumed_frames;
        if (available > 65536u) available = 65536u;
        ts_live_waveform_push(
            &input->live_waveform,
            input->recorder.buffer + input->waveform_consumed_frames,
            available);
        input->waveform_consumed_frames += available;
    }
    ui->input_wave_columns = ts_live_waveform_snapshot(
        &input->live_waveform, ui->input_wave_minimum,
        ui->input_wave_maximum, TS_WAVE_W);
    ui->staged_notes = 0u;
    if (input_device) SDL_UnlockAudioDevice(input_device);
    ui->input_level = ts_input_monitor_level(&input->monitor);
    callback_peak = ts_input_monitor_take_peak(&input->monitor);
    if (callback_peak >= ui->input_peak) {
        ui->input_peak = callback_peak;
        input->peak_hold_until_ms = now + 1200u;
    } else if ((Sint32)(now - input->peak_hold_until_ms) >= 0) {
        ui->input_peak *= 0.92f;
        if (ui->input_peak < ui->input_level) ui->input_peak = ui->input_level;
    }
    if (ts_input_monitor_take_clip(&input->monitor)) {
        ui->input_clipping = 1;
        input->clip_hold_until_ms = now + 1200u;
    } else if ((Sint32)(now - input->clip_hold_until_ms) >= 0) {
        ui->input_clipping = 0;
    }
    if (!ui->input_meter_active) {
        ui->input_level = 0.0f;
        ui->input_peak = 0.0f;
        ui->input_clipping = 0;
        ui->input_wave_columns = 0u;
    }
}

static int arm_external_capture(SDL_AudioDeviceID output_device,
                                SDL_AudioDeviceID *input_device,
                                AudioState *audio, ExternalInputState *input,
                                TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    int slot = instrument->selected_slot;
    int ok;
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT) {
        snprintf(ui->status, sizeof(ui->status), "SELECT AN EMPTY REC TILE FIRST");
        return 0;
    }
    if (instrument->bank[slot].occupied) {
        snprintf(ui->status, sizeof(ui->status),
                 "REC TILE %02d IS OCCUPIED - SELECT AN EMPTY TILE", slot + 1);
        return 0;
    }
    if (!ensure_external_input_open(input_device, input, &ui->config,
                                    error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "REC INPUT FAILED: %.140s", error);
        return 0;
    }
    stop_all_force(output_device, audio, ui);
    /* SDL starts capture devices paused. Re-pause explicitly before replacing
       recorder buffers so no callback can observe freed or half-initialized tape. */
    SDL_PauseAudioDevice(*input_device, 1);
    SDL_LockAudioDevice(*input_device);
    ok = ts_external_recorder_arm(
        &input->recorder, slot, input->sample_rate,
        ui->config.record_threshold_db,
        ui->config.record_preroll_ms,
        ui->config.record_silence_ms,
        ui->config.record_tail_ms,
        ui->config.record_max_seconds,
        error, sizeof(error));
    SDL_UnlockAudioDevice(*input_device);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "REC ARM FAILED: %.142s", error);
        diagnostic_log("REC arm failed (nonfatal): %s", error);
        return 0;
    }
    ts_live_waveform_init(&input->live_waveform, input->sample_rate);
    input->waveform_consumed_frames = 0u;
    input->peak_hold_until_ms = 0u;
    input->clip_hold_until_ms = 0u;
    ts_input_monitor_reset_meter(&input->monitor);
    ui->input_peak = 0.0f;
    ui->input_clipping = 0;
    SDL_PauseAudioDevice(*input_device, 0);
    diagnostic_log("REC armed: slot=%d threshold=%d dB", slot + 1,
                   ui->config.record_threshold_db);
    sync_external_capture_ui(*input_device, input, ui);
    show_overlay(ui, "REC ARMED", 850u);
    snprintf(ui->status, sizeof(ui->status),
             "REC %02d ARMED  %.96s  CH %d  THRESH %d DB",
             slot + 1, input->device_label, ui->config.record_input_channel,
             ui->config.record_threshold_db);
    return 1;
}

static void cancel_external_capture(SDL_AudioDeviceID input_device,
                                    ExternalInputState *input, TsUiState *ui)
{
    if (input_device) SDL_PauseAudioDevice(input_device, 1);
    if (input_device) SDL_LockAudioDevice(input_device);
    (void)ts_external_recorder_cancel(&input->recorder);
    ts_external_recorder_free(&input->recorder);
    if (input_device) SDL_UnlockAudioDevice(input_device);
    if (input_device && ui->monitor_enabled) SDL_PauseAudioDevice(input_device, 0);
    sync_external_capture_ui(input_device, input, ui);
    show_overlay(ui, "REC CANCELLED", 700u);
    snprintf(ui->status, sizeof(ui->status), "EXTERNAL RECORDING CANCELLED - TILE UNCHANGED");
}

static void stop_external_capture_early(SDL_AudioDeviceID input_device,
                                        ExternalInputState *input,
                                        TsUiState *ui)
{
    char error[160];
    int ok;
    if (input_device) SDL_PauseAudioDevice(input_device, 1);
    if (input_device) SDL_LockAudioDevice(input_device);
    ok = ts_external_recorder_stop(&input->recorder, error, sizeof(error));
    if (input_device) SDL_UnlockAudioDevice(input_device);
    if (!ok)
        snprintf(ui->status, sizeof(ui->status), "REC STOP FAILED: %.140s", error);
    else
        snprintf(ui->status, sizeof(ui->status), "STOPPING REC - KEEPING SHORT TAKE");
}

static void external_capture_button(SDL_AudioDeviceID output_device,
                                    SDL_AudioDeviceID *input_device,
                                    AudioState *audio, ExternalInputState *input,
                                    TsUiState *ui, TsInstrument *instrument)
{
    if (input->recorder.state == TS_EXTERNAL_CAPTURE_COMPLETED) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISHING COMPLETED REC TAKE");
        return;
    }
    if (input->recorder.state == TS_EXTERNAL_CAPTURE_RECORDING) {
        stop_external_capture_early(*input_device, input, ui);
        return;
    }
    if (input->recorder.state == TS_EXTERNAL_CAPTURE_ARMED) {
        cancel_external_capture(*input_device, input, ui);
        return;
    }
    (void)arm_external_capture(output_device, input_device, audio, input, ui, instrument);
}

static int install_external_take(SDL_AudioDeviceID output_device,
                                 AudioState *audio, TsUiState *ui,
                                 TsInstrument *instrument, int slot,
                                 const float *captured, size_t frames,
                                 uint32_t sample_rate,
                                 char *error, size_t error_size)
{
    int ok;
    char name[32];
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT || captured == NULL ||
        frames == 0u || sample_rate == 0u) {
        snprintf(error, error_size, "Invalid external take");
        return 0;
    }
    lock_edit(output_device, audio);
    ok = ts_instrument_select_bank(instrument, slot, error, error_size);
    if (ok && instrument->bank[slot].occupied) {
        snprintf(error, error_size, "REC destination is no longer empty");
        ok = 0;
    }
    if (ok)
        ok = ts_instrument_activate_silence(instrument, frames, sample_rate,
                                            error, error_size);
    if (ok) {
        TsBankSlot *bank = &instrument->bank[slot];
        if (instrument->current.frames != frames || instrument->parent.frames != frames ||
            bank->sample.frames != frames || bank->edit_parent.frames != frames ||
            instrument->current.data == NULL || instrument->parent.data == NULL ||
            bank->sample.data == NULL || bank->edit_parent.data == NULL) {
            snprintf(error, error_size, "REC tile buffers did not match the captured take");
            ok = 0;
        } else {
            size_t bytes = frames * sizeof(*captured);
            memcpy(instrument->current.data, captured, bytes);
            memcpy(instrument->parent.data, captured, bytes);
            memcpy(bank->sample.data, captured, bytes);
            memcpy(bank->edit_parent.data, captured, bytes);
            snprintf(name, sizeof(name), "REC %02d", slot + 1);
            snprintf(instrument->current.name, sizeof(instrument->current.name), "%s", name);
            snprintf(instrument->parent.name, sizeof(instrument->parent.name), "%s", name);
            snprintf(bank->sample.name, sizeof(bank->sample.name), "%s", name);
            snprintf(bank->edit_parent.name, sizeof(bank->edit_parent.name), "%s", name);
            bank->capture_kind = TS_BANK_CAPTURE_CURRENT;
            bank->relation = TS_FAMILY_CAPTURED;
            bank->parent_slot = -1;
            instrument->source_kind = TS_SOURCE_IMPORTED;
            instrument->has_selection = 1;
            instrument->selection_first = 0u;
            instrument->selection_last = frames;
            instrument->has_playhead = 1;
            instrument->playhead_frame = 0u;
            ts_instrument_show_all(instrument);
        }
    }
    unlock_edit(output_device, audio, ui, instrument);
    return ok;
}

static void finalize_external_recording(SDL_AudioDeviceID output_device,
                                        SDL_AudioDeviceID *input_device,
                                        AudioState *audio,
                                        ExternalInputState *input,
                                        TsUiState *ui,
                                        TsInstrument *instrument)
{
    char error[160];
    char archive_error[160];
    char archive_path[1200];
    float *captured = NULL;
    size_t frames;
    uint32_t sample_rate;
    int slot;
    int chain;
    int ok;
    int archived;
    if (input->recorder.state != TS_EXTERNAL_CAPTURE_COMPLETED) return;
    if (*input_device) SDL_PauseAudioDevice(*input_device, 1);
    if (*input_device) SDL_LockAudioDevice(*input_device);
    frames = input->recorder.recorded_frames;
    sample_rate = input->recorder.sample_rate;
    slot = input->recorder.destination_slot;
    if (frames > 0u && frames <= SIZE_MAX / sizeof(*captured)) {
        captured = (float *)malloc(frames * sizeof(*captured));
        if (captured != NULL)
            memcpy(captured, input->recorder.buffer, frames * sizeof(*captured));
    }
    if (*input_device) SDL_UnlockAudioDevice(*input_device);
    if (captured == NULL) {
        if (*input_device) SDL_LockAudioDevice(*input_device);
        ts_external_recorder_free(&input->recorder);
        if (*input_device) SDL_UnlockAudioDevice(*input_device);
        sync_external_capture_ui(*input_device, input, ui);
        if (*input_device && ui->monitor_enabled) SDL_PauseAudioDevice(*input_device, 0);
        snprintf(ui->status, sizeof(ui->status), "REC TAKE FAILED - OUT OF MEMORY");
        return;
    }
    archived = ts_capture_archive_write(
        capture_archive_directory(), TS_CAPTURE_ARCHIVE_INPUT,
        captured, frames, sample_rate,
        archive_path, sizeof(archive_path),
        archive_error, sizeof(archive_error));
    chain = instrument->family_trajectory;
    ok = install_external_take(output_device, audio, ui, instrument, slot,
                               captured, frames, sample_rate,
                               error, sizeof(error));
    free(captured);
    if (*input_device) SDL_LockAudioDevice(*input_device);
    ts_external_recorder_free(&input->recorder);
    if (*input_device) SDL_UnlockAudioDevice(*input_device);
    if (*input_device && ui->monitor_enabled) SDL_PauseAudioDevice(*input_device, 0);
    if (!ok) {
        sync_external_capture_ui(*input_device, input, ui);
        snprintf(ui->status, sizeof(ui->status),
                 archived ? "INPUT ARCHIVED - REC TILE COMMIT FAILED: %.104s" :
                            "REC COMMIT AND ARCHIVE FAILED: %.102s",
                 archived ? error : archive_error);
        return;
    }
    show_overlay(ui, "REC TAKE KEPT", 850u);
    if (chain) {
        int next = ts_external_next_chain_slot(slot);
        if (next >= 0 && !instrument->bank[next].occupied) {
            lock_edit(output_device, audio);
            ok = ts_instrument_select_bank(instrument, next, error, sizeof(error));
            unlock_edit(output_device, audio, ui, instrument);
            if (ok && arm_external_capture(output_device, input_device, audio, input,
                                           ui, instrument)) {
                snprintf(ui->status, sizeof(ui->status), archived ?
                         "REC %02d ARCHIVED - CHAIN ARMED %02d" :
                         "REC %02d KEPT, ARCHIVE FAILED - CHAIN ARMED %02d",
                         slot + 1, next + 1);
                return;
            }
        }
        snprintf(ui->status, sizeof(ui->status),
                 "REC %02d KEPT - CHAIN STOPPED AT NEXT OCCUPIED/END TILE", slot + 1);
    } else {
        sync_external_capture_ui(*input_device, input, ui);
        if (archived)
            snprintf(ui->status, sizeof(ui->status),
                     "REC %02d ARCHIVED - %zu FRAMES AT %u HZ",
                     slot + 1, frames, sample_rate);
        else
            snprintf(ui->status, sizeof(ui->status),
                     "REC %02d KEPT - ARCHIVE FAILED: %.82s",
                     slot + 1, archive_error);
    }
}

static void toggle_external_monitor(SDL_AudioDeviceID output_device,
                                    SDL_AudioDeviceID *input_device,
                                    AudioState *audio,
                                    ExternalInputState *input,
                                    TsUiState *ui)
{
    char error[160];
    if (ui->monitor_enabled) {
        ts_input_monitor_set_enabled(&input->monitor, 0, input->sample_rate);
        ui->monitor_enabled = 0;
        if (*input_device && !external_capture_busy(input))
            SDL_PauseAudioDevice(*input_device, 1);
        show_overlay(ui, "MONITOR OFF", 650u);
        snprintf(ui->status, sizeof(ui->status),
                 "DRY INPUT MONITOR OFF - RECORDING AND METERING ARE UNAFFECTED");
        return;
    }
    if (output_device == 0 || audio->output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status),
                 "MONITOR NEEDS AN AVAILABLE AUDIO OUTPUT");
        return;
    }
    if (!ensure_external_input_open(input_device, input, &ui->config,
                                    error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status),
                 "MONITOR INPUT FAILED: %.138s", error);
        return;
    }
    ts_input_monitor_set_enabled(&input->monitor, 1, input->sample_rate);
    ui->monitor_enabled = 1;
    SDL_PauseAudioDevice(*input_device, 0);
    show_overlay(ui, "MONITOR ON", 750u);
    snprintf(ui->status, sizeof(ui->status),
             "DRY MONITOR ON - USE HEADPHONES OR KEEP MICROPHONES AWAY FROM SPEAKERS");
}

static void swap_instrument_storage(TsInstrument *first, TsInstrument *second)
{
    unsigned char scratch[4096];
    unsigned char *left = (unsigned char *)first;
    unsigned char *right = (unsigned char *)second;
    size_t remaining = sizeof(*first);
    while (remaining > 0u) {
        size_t amount = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        memcpy(scratch, left, amount);
        memcpy(left, right, amount);
        memcpy(right, scratch, amount);
        left += amount;
        right += amount;
        remaining -= amount;
    }
}

static int set_record_bank(SDL_Window *window,
                           SDL_AudioDeviceID output_device,
                           SDL_AudioDeviceID input_device,
                           ExternalInputState *input,
                           AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument,
                           TsSamplePages *sample_pages,
                           TsInstrument **parked,
                           int *record_bank_active,
                           int activate,
                           TransformController *controller)
{
    char error[160];
    diagnostic_log("bank switch requested active=%d target=%d parked=%p",
                   *record_bank_active, activate,
                   (void *)(parked != NULL ? *parked : NULL));
    if (*record_bank_active == activate) return 1;
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
        audio->capture.state == TS_CAPTURE_RECORDING || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL RECORDING BEFORE SWITCHING BANKS");
        diagnostic_log("bank toggle deferred: recorder busy");
        return 0;
    }
    if (controller != NULL && controller->worker != NULL) {
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->status, sizeof(ui->status),
                 "CANCELING TRANSFORM - PRESS 1 AGAIN WHEN IT STOPS");
        diagnostic_log("bank toggle deferred: canceling transform worker");
        return 0;
    }
    if (parked == NULL || sample_pages == NULL) {
        snprintf(ui->status, sizeof(ui->status), "REC BANK STORAGE ERROR");
        return 0;
    }
    if (*parked == NULL) {
        *parked = (TsInstrument *)malloc(sizeof(**parked));
        if (*parked == NULL) {
            snprintf(ui->status, sizeof(ui->status),
                     "REC BANK UNAVAILABLE - OUT OF MEMORY");
            diagnostic_log("bank toggle failed: heap allocation of %zu bytes", sizeof(**parked));
            return 0;
        }
        ts_instrument_init(*parked);
        diagnostic_log("allocated parked REC collection on heap: %zu bytes", sizeof(**parked));
    }

    /* A transform preview may retain pointers into the active collection. Clear all
       preview/controller identity before the collection storage changes. */
    if (controller != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 0;
        controller->quick_apply = 0;
        discard_transform_preview(output_device, audio, ui, controller);
        ui->transform_open = 0;
        ui->transform_rendering = 0;
    }
    stop_all_force(output_device, audio, ui);
    if (audio->capture.state != TS_CAPTURE_IDLE || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISHING CAPTURE - SWITCH BANKS AGAIN IN A MOMENT");
        return 0;
    }
    diagnostic_log("bank swap begin");
    if (activate) {
        if (!ts_sample_pages_park(sample_pages, instrument,
                                  error, sizeof(error))) {
            snprintf(ui->status, sizeof(ui->status),
                     "SAMPLE PAGE PARK FAILED: %.130s", error);
            return 0;
        }
        swap_instrument_storage(instrument, *parked);
    } else {
        if (ui->monitor_enabled) {
            ts_input_monitor_set_enabled(&input->monitor, 0, input->sample_rate);
            ui->monitor_enabled = 0;
            if (input_device) SDL_PauseAudioDevice(input_device, 1);
        }
        swap_instrument_storage(instrument, *parked);
        if (!ts_sample_pages_unpark(sample_pages, instrument,
                                    error, sizeof(error))) {
            swap_instrument_storage(instrument, *parked);
            snprintf(ui->status, sizeof(ui->status),
                     "SAMPLE PAGE RESTORE FAILED: %.126s", error);
            return 0;
        }
    }
    diagnostic_log("bank swap complete");
    *record_bank_active = activate;
    ui->external_record_bank = *record_bank_active;
    ui->sample_page = (int)ts_sample_pages_active(sample_pages);
    ui->sample_page_count = (int)ts_sample_pages_count(sample_pages);
    ui->capture_state = TS_CAPTURE_IDLE;
    ui->capture_destination_slot = -1;
    ui->capture_source_slot = -1;
    ui->capture_recorded_frames = 0u;
    ui->capture_capacity_frames = 0u;
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ts_ui_select_panel(ui, TS_UI_PANEL_SAMPLE_TILES);
    if (window != NULL)
        SDL_SetWindowTitle(window, *record_bank_active ?
                          "TapeSister - REC BANK" : "TapeSister");
    show_overlay(ui, *record_bank_active ? "REC BANK" : "SAMPLE BANK", 800u);
    if (*record_bank_active)
        snprintf(ui->status, sizeof(ui->status),
                 "REC BANK - SELECT EMPTY TILE AND REC ARM  1 RETURNS TO SAMPLES");
    else
        snprintf(ui->status, sizeof(ui->status),
                 "SAMPLE %d/%d - PRESS 1 TO CYCLE PAGES  SHIFT+1 FOR REC BANK",
                 ui->sample_page + 1, ui->sample_page_count);
    diagnostic_log("bank toggle complete active=%d", *record_bank_active);
    return 1;
}

static int switch_sample_page(SDL_AudioDeviceID output_device,
                              ExternalInputState *input,
                              AudioState *audio, TsUiState *ui,
                              TsInstrument *instrument,
                              TsSamplePages *pages, size_t target,
                              TransformController *controller)
{
    char error[160];
    if (pages != NULL && target == ts_sample_pages_active(pages)) {
        ui->sample_page = (int)target;
        ui->sample_page_count = (int)ts_sample_pages_count(pages);
        snprintf(ui->status, sizeof(ui->status),
                 "SAMPLE %d/%d - SHIFT+1 FOR REC BANK",
                 ui->sample_page + 1, ui->sample_page_count);
        return 1;
    }
    if (audio->capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
        audio->capture.state == TS_CAPTURE_RECORDING || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL CAPTURE BEFORE SWITCHING SAMPLE PAGES");
        return 0;
    }
    if (controller != NULL && controller->worker != NULL) {
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->status, sizeof(ui->status),
                 "CANCELING TRANSFORM - PRESS 1 AGAIN WHEN IT STOPS");
        return 0;
    }
    if (controller != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 0;
        controller->quick_apply = 0;
        discard_transform_preview(output_device, audio, ui, controller);
        ui->transform_open = 0;
        ui->transform_rendering = 0;
    }
    stop_all_force(output_device, audio, ui);
    if (audio->capture.state != TS_CAPTURE_IDLE || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISHING CAPTURE - SWITCH PAGES AGAIN IN A MOMENT");
        return 0;
    }
    if (!ts_sample_pages_switch(pages, instrument, target,
                                error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status),
                 "SAMPLE PAGE SWITCH FAILED: %.126s", error);
        return 0;
    }
    ui->sample_page = (int)ts_sample_pages_active(pages);
    ui->sample_page_count = (int)ts_sample_pages_count(pages);
    ui->bank_view_slot = -1;
    ui->audition_source = TS_AUDITION_CURRENT;
    ui->capture_destination_slot = -1;
    ui->capture_source_slot = -1;
    show_overlay(ui, "SAMPLE PAGE", 650u);
    snprintf(ui->status, sizeof(ui->status),
             "SAMPLE %d/%d - PRESS 1 TO CYCLE  SHIFT+1 FOR REC BANK",
             ui->sample_page + 1, ui->sample_page_count);
    return 1;
}

static void keep_record_bank(SDL_AudioDeviceID output_device,
                             ExternalInputState *input,
                             AudioState *audio,
                             TsInstrument *record_bank,
                             TsSamplePages *sample_pages,
                             TsUiState *ui,
                             TransformController *controller)
{
    char error[160];
    size_t copied = 0u;
    size_t first_page = 0u;
    size_t last_page = 0u;
    if (ui->capture_state != TS_CAPTURE_IDLE || external_capture_busy(input)) {
        snprintf(ui->status, sizeof(ui->status),
                 "FINISH OR CANCEL RECORDING BEFORE KEEP");
        return;
    }
    if (controller != NULL && controller->worker != NULL) {
        SDL_AtomicSet(&controller->worker->cancel, 1);
        snprintf(ui->status, sizeof(ui->status),
                 "CANCELING TRANSFORM - PRESS KEEP AGAIN WHEN IT STOPS");
        return;
    }
    if (controller != NULL) {
        ++controller->render_generation;
        controller->rerender_requested = 0;
        controller->quick_apply = 0;
        discard_transform_preview(output_device, audio, ui, controller);
        ui->transform_open = 0;
        ui->transform_rendering = 0;
    }
    /* KEEP frees REC-tile buffers after copying them. Stop every audition
       owner first so the output callback cannot retain one of those buffers. */
    stop_all_force(output_device, audio, ui);
    if (!ts_sample_pages_keep_record_bank(
            sample_pages, NULL, record_bank,
            &copied, &first_page, &last_page,
            error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "KEEP FAILED: %.145s", error);
        return;
    }
    ui->sample_page_count = (int)ts_sample_pages_count(sample_pages);
    ui->bank_view_slot = -1;
    ui->capture_destination_slot = -1;
    if (copied == 0u) {
        snprintf(ui->status, sizeof(ui->status),
                 "REC BANK IS EMPTY - NOTHING TO KEEP");
        return;
    }
    show_overlay(ui, "RECORDINGS KEPT", 900u);
    if (first_page == last_page)
        snprintf(ui->status, sizeof(ui->status),
                 "KEPT %zu RECORDING%s IN SAMPLE %zu - REC BANK CLEARED",
                 copied, copied == 1u ? "" : "S", first_page);
    else
        snprintf(ui->status, sizeof(ui->status),
                 "KEPT %zu RECORDINGS IN SAMPLE %zu-%zu - REC BANK CLEARED",
                 copied, first_page, last_page);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioDeviceID device = 0;
    SDL_AudioDeviceID input_device = 0;
    SDL_AudioSpec desired, obtained;
    TsInstrument instrument;
    TsSamplePages sample_pages;
    TsInstrument *parked_instrument = NULL;
    TsUiState ui;
    TsFramebuffer framebuffer;
    TsSample clipboard;
    TsSample pending_selection_load;
    TsSample drone_preview;
    TsExchangeOffer exchange_offer;
    TransformController transform;
    size_t clipboard_origin_first = 0;
    size_t clipboard_source_frames = 0;
    uint32_t clipboard_source_rate = 0;
    AudioState audio = {0};
    ExternalInputState external_input = {0};
    char ignored_exchange[TS_EXCHANGE_PATH_MAX] = {0};
    uint32_t last_exchange_poll = 0;
    int record_bank_active = 0;
    int diagnostic_bank_stress = argc > 1 &&
                                 strcmp(argv[1], "--diagnostic-bank-toggle-stress") == 0;
    int diagnostic_failed = 0;
    int running = 1;

    diagnostic_log("entered main: TsInstrument=%zu framebuffer=%zu UI=%zu stress=%d",
                   sizeof(TsInstrument), sizeof(TsFramebuffer), sizeof(TsUiState),
                   diagnostic_bank_stress);
    ts_instrument_init(&instrument);
    {
        char page_error[160];
        if (!ts_sample_pages_init(&sample_pages, page_error, sizeof(page_error))) {
            fprintf(stderr, "TapeSister Sample pages: %s\n", page_error);
            ts_instrument_free(&instrument);
            return 1;
        }
        parked_instrument = (TsInstrument *)malloc(sizeof(*parked_instrument));
        if (parked_instrument == NULL) {
            fprintf(stderr, "TapeSister Record Bank: out of memory\n");
            ts_sample_pages_free(&sample_pages);
            ts_instrument_free(&instrument);
            return 1;
        }
        ts_instrument_init(parked_instrument);
    }
    ts_external_recorder_init(&external_input.recorder);
    ts_input_monitor_init(&external_input.monitor);
    ts_live_waveform_init(&external_input.live_waveform, 48000u);
    ts_sample_init(&clipboard);
    ts_sample_init(&pending_selection_load);
    ts_sample_init(&drone_preview);
    ts_exchange_offer_init(&exchange_offer);
    transform_controller_init(&transform);
    ts_ui_init(&ui);
    ui.sample_page = 0;
    ui.sample_page_count = 1;
    {
        char config_error[160];
        if (!ts_config_load(&ui.config, config_file_path(),
                            config_error, sizeof(config_error)))
            fprintf(stderr, "TapeSister config: %s\n", config_error);
        else if (path_is_directory(ui.config.sample_path))
            snprintf(ui.browser.directory, sizeof(ui.browser.directory), "%s",
                     ui.config.sample_path);
        for (int slot = 0; slot < TS_DSP_FACTORY_RECIPE_COUNT; ++slot) {
            const TsDspRecipe *recipe = ts_dsp_factory_recipe_at((size_t)slot);
            if (!ui.config.dsp_factory_overridden[slot] || recipe == NULL) continue;
            for (size_t control = 0; control < recipe->control_count; ++control) {
                float value = ui.config.dsp_factory_controls[slot][control];
                if (!ts_dsp_recipe_set_control(recipe, &ui.dsp_presets[slot],
                                               control, value))
                    fprintf(stderr, "TapeSister config: invalid DSP preset %02d\n",
                            slot + 1);
            }
        }
        for (int slot = 0; slot < TS_CDP_FACTORY_RECIPE_COUNT; ++slot) {
            const TsCdpRecipe *recipe = ts_cdp_factory_recipe_at((size_t)slot);
            if (!ui.config.cdp_factory_overridden[slot] || recipe == NULL) continue;
            for (size_t control = 0; control < recipe->control_count; ++control)
                ui.cdp_presets[slot].controls[control] = ts_cdp_control_quantize(
                    &recipe->controls[control],
                    ui.config.cdp_factory_controls[slot][control]);
            ui.cdp_presets[slot].mix = recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED ?
                                       1.0f : ui.config.cdp_factory_mix[slot];
            ui.cdp_presets[slot].seed = ui.config.cdp_factory_seed[slot];
        }
    }
    ts_note_bank_init(&audio.notes);
    ts_capture_init(&audio.capture);
    audio.input_monitor = &external_input.monitor;
    audio.bank_slot = -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        ts_sample_free(&drone_preview);
        ts_sample_free(&pending_selection_load);
        ts_sample_free(&clipboard);
        ts_instrument_free(parked_instrument);
        free(parked_instrument);
        ts_sample_pages_free(&sample_pages);
        ts_instrument_free(&instrument);
        return 1;
    }
    {
        char palette_error[160];
        char bundled_palette[1024];
        if (!ts_palette_load(&ui.palette, tapesister_palette_path(),
                             palette_error, sizeof(palette_error)) &&
            runtime_asset_path("assets/tapehead.pal", bundled_palette,
                               sizeof(bundled_palette)) &&
            !ts_palette_load(&ui.palette, bundled_palette,
                             palette_error, sizeof(palette_error)))
            fprintf(stderr, "TapeSister palette: %s\n", palette_error);
    }
    window = SDL_CreateWindow("TapeSister", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              TS_UI_WIDTH * 2, TS_UI_HEIGHT * 2,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                            TS_UI_WIDTH, TS_UI_HEIGHT) : NULL;
    if (!window || !renderer || !texture) {
        fprintf(stderr, "Video setup failed: %s\n", SDL_GetError());
        running = 0;
    }
    if (running && !diagnostic_bank_stress && !show_splash(renderer)) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        ts_sample_free(&drone_preview);
        ts_sample_free(&pending_selection_load);
        ts_sample_free(&clipboard);
        ts_instrument_free(parked_instrument);
        free(parked_instrument);
        ts_sample_pages_free(&sample_pages);
        ts_instrument_free(&instrument);
        return 0;
    }

    SDL_zero(desired);
    SDL_zero(obtained);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 256;
    desired.callback = audio_callback;
    desired.userdata = &audio;
    device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    audio.output_rate = obtained.freq;
    if (!device) snprintf(ui.status, sizeof(ui.status), "AUDIO UNAVAILABLE: %.130s", SDL_GetError());
    else SDL_PauseAudioDevice(device, 0);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    if (argc > 1 && !diagnostic_bank_stress) {
        load_instrument(device, &audio, &ui, &instrument,
                        &sample_pages, parked_instrument,
                        record_bank_active, argv[1]);
    } else if (!diagnostic_bank_stress && ui.config.startup_welcome_sample) {
        char welcome_path[1024];
        if (runtime_asset_path("assets/tapesister_welcome.wav",
                               welcome_path, sizeof(welcome_path))) {
            ui.load_bank_slot = 0;
            if (load_instrument(device, &audio, &ui, &instrument,
                                &sample_pages, parked_instrument,
                                record_bank_active, welcome_path)) {
                ui.startup_welcome_installed = 1;
                ui.startup_welcome_autoplay = ui.config.startup_welcome_autoplay;
            }
        } else {
            fprintf(stderr, "TapeSister welcome: assets/tapesister_welcome.wav not found\n");
            snprintf(ui.status, sizeof(ui.status), "WELCOME SAMPLE MISSING - BANK 01 EMPTY");
        }
    }
    if (ts_ui_request_startup_welcome(&ui, 1, device != 0)) {
        ui.audition_source = TS_AUDITION_CURRENT;
        begin_audition(device, &audio, &ui, &instrument,
                       TS_AUDITION_ALL, 1.0, obtained.freq);
    }
    ui.saved_state_hash = runtime_project_state_hash(
        &sample_pages, &instrument, parked_instrument, record_bank_active);
    last_exchange_poll = SDL_GetTicks();
    (void)ts_exchange_presence_touch(exchange_directory(&ui), "tapesister");
    (void)stage_incoming_exchange(&ui, &exchange_offer, ignored_exchange, 0);

    if (diagnostic_bank_stress && running) {
        char original_device[TS_CONFIG_PATH_MAX];
        snprintf(original_device, sizeof(original_device), "%s",
                 ui.config.record_input_device);
        diagnostic_log("starting 2000 bank-toggle stress passes");
        for (int pass = 0; pass < 2000; ++pass) {
            if (!set_record_bank(window, device, input_device,
                                 &external_input, &audio, &ui,
                                 &instrument, &sample_pages, &parked_instrument,
                                 &record_bank_active, !record_bank_active,
                                 &transform)) {
                diagnostic_log("stress toggle failed at pass %d: %s", pass, ui.status);
                diagnostic_failed = 1;
                break;
            }
        }
        if (!diagnostic_failed && !record_bank_active &&
            !set_record_bank(window, device, input_device,
                             &external_input, &audio, &ui,
                             &instrument, &sample_pages, &parked_instrument,
                             &record_bank_active, 1, &transform))
            diagnostic_failed = 1;
        if (!diagnostic_failed) {
            snprintf(ui.config.record_input_device, sizeof(ui.config.record_input_device),
                     "__TAPESISTER_INTENTIONALLY_MISSING_CAPTURE_DEVICE__");
            if (arm_external_capture(device, &input_device, &audio, &external_input,
                                     &ui, &instrument)) {
                diagnostic_log("ERROR: intentionally missing capture device opened unexpectedly");
                cancel_external_capture(input_device, &external_input, &ui);
                diagnostic_failed = 1;
            } else {
                diagnostic_log("missing capture device remained nonfatal: %s", ui.status);
            }
            snprintf(ui.config.record_input_device, sizeof(ui.config.record_input_device),
                     "%s", original_device);
        }
        if (!diagnostic_failed && record_bank_active &&
            !set_record_bank(window, device, input_device,
                             &external_input, &audio, &ui,
                             &instrument, &sample_pages, &parked_instrument,
                             &record_bank_active, 0, &transform))
            diagnostic_failed = 1;
        diagnostic_log("bank-toggle stress complete result=%s",
                       diagnostic_failed ? "FAIL" : "PASS");
        running = 0;
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_DROPFILE && ui.canvas_gesture.active)
                end_canvas_gesture(window, device, &audio, &ui, &instrument, 1);
            if (event.type == SDL_DROPFILE && ui.stretch_gesture.active)
                end_stretch_gesture(device, &audio, &ui, &instrument, 1);
            if (event.type == SDL_DROPFILE && ui.tear_gesture.active)
                end_tear_gesture(device, &audio, &ui, &instrument, 1);
            if (event.type == SDL_DROPFILE && ui.smear_gesture.active) {
                end_smear_gesture(device, &audio, &ui, &instrument, 1);
            }
            if (event.type == SDL_DROPFILE && ui.warp_gesture.active) {
                end_warp_gesture(device, &audio, &ui, &instrument, 1);
                SDL_free(event.drop.file);
                continue;
            } else if (event.type == SDL_QUIT) {
                if (ui.smear_gesture.active)
                    end_smear_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.warp_gesture.active)
                    end_warp_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.tear_gesture.active)
                    end_tear_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.stretch_gesture.active)
                    end_stretch_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.canvas_gesture.active)
                    end_canvas_gesture(window, device, &audio, &ui, &instrument, 1);
                if (!ui.exit_confirm_open)
                    begin_exit_confirmation(device, &audio, &ui, &instrument,
                                            &sample_pages, parked_instrument,
                                            record_bank_active);
            }
            else if (event.type == SDL_DROPFILE &&
                     (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||
                      ui.config_open || ui.palette_open || ui.export_choice_open ||
                      ui.exchange_dialog != TS_UI_EXCHANGE_NONE ||
                      ui.load_selection_choice_open ||
                      ui.transform_open ||
                      ui.drone_open ||
                      ui.exit_confirm_open ||
                      ui.browser.mode != TS_BROWSER_CLOSED)) {
                snprintf(ui.status, sizeof(ui.status),
                         "FINISH OR CANCEL THE OPEN DIALOG FIRST");
                SDL_free(event.drop.file);
            }
            else if (event.type == SDL_DROPFILE) {
                load_instrument(device, &audio, &ui, &instrument,
                                &sample_pages, parked_instrument,
                                record_bank_active, event.drop.file);
                SDL_free(event.drop.file);
            } else if (event.type == SDL_TEXTINPUT && ui.config_open &&
                       !ui.exit_confirm_open) {
                char *field = ts_config_field(&ui.config, ui.config_field);
                if (field != NULL)
                    text_insert_ascii(field, TS_CONFIG_PATH_MAX,
                                      &ui.config_cursor, event.text.text);
            } else if (event.type == SDL_TEXTINPUT && ui.renaming_bank_slot >= 0 &&
                       !ui.exit_confirm_open) {
                text_insert_ascii(ui.bank_rename, sizeof(ui.bank_rename),
                                  &ui.bank_rename_cursor, event.text.text);
            } else if (event.type == SDL_TEXTINPUT && ui.renaming_recipe_slot >= 0 &&
                       !ui.exit_confirm_open) {
                text_insert_ascii(ui.recipe_rename, sizeof(ui.recipe_rename),
                                  &ui.recipe_rename_cursor, event.text.text);
            } else if (event.type == SDL_TEXTINPUT &&
                       ui.browser.mode != TS_BROWSER_CLOSED && !ui.exit_confirm_open) {
                if (ui.browser.filename_focus &&
                    ts_browser_mode_edits_filename(ui.browser.mode))
                    ts_browser_append_filename(&ui.browser, event.text.text);
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
                       ui.transform_open) {
                stop_transform_preview(device, &audio, &ui, &transform);
                ui.transform_selection_dragging = 0;
                snprintf(ui.transform_message, sizeof(ui.transform_message),
                         "PREVIEW STOPPED - WINDOW LOST FOCUS");
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
                       ui.drone_open) {
                stop_drone_preview(device, &audio, &ui, &drone_preview);
                ui.drone_crossfade_dragging = 0;
                ui.drone_crossfade_drag_start_x = 0;
                ui.drone_crossfade_drag_start_frames = 0;
                snprintf(ui.status, sizeof(ui.status),
                         "DRONE PREVIEW STOPPED - WINDOW LOST FOCUS");
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
                       (ui.warp_gesture.active || ui.smear_gesture.active ||
                        ui.tear_gesture.active || ui.stretch_gesture.active ||
                        ui.canvas_gesture.active)) {
                if (ui.warp_gesture.active) end_warp_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.smear_gesture.active) end_smear_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.tear_gesture.active) end_tear_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.stretch_gesture.active)
                    end_stretch_gesture(device, &audio, &ui, &instrument, 1);
                if (ui.canvas_gesture.active)
                    end_canvas_gesture(window, device, &audio, &ui, &instrument, 1);
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod mod = SDL_GetModState();
                TsUiSlider hovered_slider = TS_UI_SLIDER_NONE;
                if ((key == SDLK_LEFT || key == SDLK_RIGHT) &&
                    (mod & (KMOD_CTRL | KMOD_ALT)) == 0) {
                    int raw_x, raw_y, mouse_x, mouse_y;
                    SDL_GetMouseState(&raw_x, &raw_y);
                    logical_mouse(window, raw_x, raw_y, &mouse_x, &mouse_y);
                    hovered_slider = ts_ui_slider_from_point(&ui, mouse_x, mouse_y);
                }
                ui.bank_clear_armed = 0;
                if (ui.canvas_gesture.active && key == SDLK_ESCAPE) {
                    end_canvas_gesture(window, device, &audio, &ui, &instrument, 1);
                } else if (ui.canvas_gesture.active) {
                    snprintf(ui.status, sizeof(ui.status),
                             "RELEASE MOUSE TO COMMIT CANVAS - ESC CANCELS");
                } else if ((mod & KMOD_ALT) &&
                    (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    int fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
                    if (SDL_SetWindowFullscreen(
                            window, fullscreen ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP) == 0)
                        snprintf(ui.status, sizeof(ui.status), "%s - ALT+ENTER TO TOGGLE",
                                 fullscreen ? "WINDOWED" : "FULLSCREEN");
                    else
                        snprintf(ui.status, sizeof(ui.status),
                                 "FULLSCREEN TOGGLE FAILED: %.119s", SDL_GetError());
                } else if (ui.stretch_gesture.active && key == SDLK_ESCAPE) {
                    end_stretch_gesture(device, &audio, &ui, &instrument, 1);
                } else if (ui.stretch_gesture.active) {
                    snprintf(ui.status, sizeof(ui.status),
                             "RELEASE SHIFT OR ALT TO COMMIT TAPE LENGTH - ESC CANCELS");
                } else if (ui.tear_gesture.active && key == SDLK_ESCAPE) {
                    end_tear_gesture(device, &audio, &ui, &instrument, 1);
                } else if (ui.tear_gesture.active) {
                    snprintf(ui.status, sizeof(ui.status), "FINISH OR CANCEL THE TEAR GESTURE FIRST");
                } else if (ui.smear_gesture.active && key == SDLK_ESCAPE) {
                    end_smear_gesture(device, &audio, &ui, &instrument, 1);
                } else if (ui.smear_gesture.active) {
                    snprintf(ui.status, sizeof(ui.status), "FINISH OR CANCEL THE SMEAR GESTURE FIRST");
                } else if (ui.warp_gesture.active && key == SDLK_ESCAPE) {
                    end_warp_gesture(device, &audio, &ui, &instrument, 1);
                } else if (ui.warp_gesture.active) {
                    snprintf(ui.status, sizeof(ui.status),
                             "FINISH OR CANCEL THE WARP GESTURE FIRST");
                } else if (ui.exit_confirm_open) {
                    if (key == SDLK_ESCAPE || key == SDLK_n) {
                        ui.exit_confirm_open = 0;
                        snprintf(ui.status, sizeof(ui.status), "EXIT CANCELLED");
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                               key == SDLK_y) {
                        running = 0;
                    }
                } else if (ui.transform_open) {
                    if ((mod & KMOD_CTRL) && key == SDLK_z) {
                        history_move(device, &audio, &ui, &instrument, 0);
                        mark_transform_stale(device, &audio, &ui, &transform,
                                             "UNDO CHANGED TILE - RENDER AGAIN");
                    } else if ((mod & KMOD_CTRL) && key == SDLK_y) {
                        history_move(device, &audio, &ui, &instrument, 1);
                        mark_transform_stale(device, &audio, &ui, &transform,
                                             "REDO CHANGED TILE - RENDER AGAIN");
                    } else if (key == SDLK_ESCAPE) {
                        handle_transform_action(device, &audio, &ui, &instrument,
                                                &transform,
                                                TS_UI_TRANSFORM_ACTION_BACK,
                                                obtained.freq);
                    } else if (key == SDLK_SPACE) {
                        audition_transform_preview(device, &audio, &ui,
                                                   &instrument, &transform,
                                                   obtained.freq);
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                               key == SDLK_r) {
                        request_transform_render(device, &audio, &ui, &instrument,
                                                 &transform);
                    } else if (key == SDLK_a) {
                        apply_transform_preview(device, &audio, &ui, &instrument,
                                                &transform);
                    } else if (key == SDLK_u) {
                        handle_transform_action(device, &audio, &ui, &instrument,
                                                &transform,
                                                TS_UI_TRANSFORM_ACTION_SAVE,
                                                obtained.freq);
                    } else if (key == SDLK_s && instrument.has_selection) {
                        handle_transform_action(device, &audio, &ui, &instrument,
                                                &transform,
                                                TS_UI_TRANSFORM_ACTION_SELECTION,
                                                obtained.freq);
                    } else if (key == SDLK_w) {
                        handle_transform_action(device, &audio, &ui, &instrument,
                                                &transform,
                                                TS_UI_TRANSFORM_ACTION_WHOLE,
                                                obtained.freq);
                    }
                } else if (ui.drone_open) {
                    if (key == SDLK_ESCAPE) {
                        close_drone_dialog(device, &audio, &ui, &drone_preview);
                        snprintf(ui.status, sizeof(ui.status),
                                 "DRONE CANCELLED - SOURCE UNCHANGED");
                    } else if (key == SDLK_SPACE || key == SDLK_s) {
                        stop_drone_preview(device, &audio, &ui, &drone_preview);
                        snprintf(ui.status, sizeof(ui.status), "DRONE PREVIEW STOPPED");
                    } else if (key == SDLK_p || key == SDLK_RETURN ||
                               key == SDLK_KP_ENTER) {
                        preview_drone(device, &audio, &ui, &instrument,
                                      &drone_preview, obtained.freq);
                    } else if (key == SDLK_c) {
                        commit_drone(device, &audio, &ui, &instrument,
                                     &drone_preview, 1);
                    } else if (key == SDLK_r) {
                        commit_drone(device, &audio, &ui, &instrument,
                                     &drone_preview, 0);
                    }
                } else if (ui.exchange_dialog != TS_UI_EXCHANGE_NONE) {
                    if (ui.exchange_dialog == TS_UI_EXCHANGE_SEND) {
                        if (key == SDLK_ESCAPE || key == SDLK_l) {
                            ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                            snprintf(ui.status, sizeof(ui.status), "FT2 LINK CANCELLED");
                        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                                   key == SDLK_o || key == SDLK_1) {
                            ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                            send_to_fasttracker(
                                &ui, &instrument,
                                TS_EXCHANGE_LAYOUT_INSTRUMENT_SAMPLES);
                        } else if (key == SDLK_s || key == SDLK_2) {
                            ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                            send_to_fasttracker(
                                &ui, &instrument,
                                TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS);
                        } else if (key == SDLK_i || key == SDLK_c) {
                            ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                            (void)stage_incoming_exchange(
                                &ui, &exchange_offer, ignored_exchange, 1);
                        } else if (key == SDLK_n) {
                            ui.exchange_force_new_instance =
                                !ui.exchange_force_new_instance;
                            snprintf(ui.status, sizeof(ui.status),
                                     ui.exchange_force_new_instance ?
                                     "NEXT SEND WILL LAUNCH A NEW TAPEHEAD INSTANCE" :
                                     "NEXT SEND WILL REUSE AN OPEN TAPEHEAD");
                        }
                    } else if (key == SDLK_ESCAPE || key == SDLK_l) {
                        snprintf(ignored_exchange, sizeof(ignored_exchange), "%s",
                                 exchange_offer.folder);
                        ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
                        snprintf(ui.status, sizeof(ui.status),
                                 "TAPEHEAD TRANSFER LEFT IN INBOX FOR LATER");
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                               key == SDLK_i) {
                        ignored_exchange[0] = '\0';
                        (void)import_incoming_exchange(
                            device, &audio, &ui, &instrument, &exchange_offer);
                    }
                } else if (ui.load_selection_choice_open) {
                    if (key == SDLK_ESCAPE || key == SDLK_c)
                        cancel_selection_load(&ui, &pending_selection_load);
                    else if (key == SDLK_f)
                        apply_selection_load(device, &audio, &ui, &instrument,
                                             &pending_selection_load, 1);
                    else if (key == SDLK_p || key == SDLK_RETURN ||
                             key == SDLK_KP_ENTER)
                        apply_selection_load(device, &audio, &ui, &instrument,
                                             &pending_selection_load, 0);
                } else if (ui.palette_open) {
                    int step = (mod & KMOD_SHIFT) ? 8 : 1;
                    if (key == SDLK_ESCAPE) finish_palette(&ui, 1);
                    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_d)
                        finish_palette(&ui, 0);
                    else if (key == SDLK_TAB || key == SDLK_DOWN)
                        ui.palette_channel = ts_ui_palette_cycle_channel(
                                                 ui.palette_channel, 1);
                    else if (key == SDLK_UP)
                        ui.palette_channel = ts_ui_palette_cycle_channel(
                                                 ui.palette_channel, -1);
                    else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                        int raw_x, raw_y, mouse_x, mouse_y, ignored;
                        int channel;
                        SDL_GetMouseState(&raw_x, &raw_y);
                        logical_mouse(window, raw_x, raw_y, &mouse_x, &mouse_y);
                        channel = ts_ui_palette_channel_from_point(
                                      mouse_x, mouse_y, &ignored);
                        if (channel >= 0) ui.palette_channel = channel;
                        palette_adjust(&ui, key == SDLK_LEFT ? -step : step);
                    }
                    else if (key == SDLK_PAGEUP)
                        ui.palette_entry = ts_ui_palette_cycle_entry(
                                               ui.palette_entry, -1);
                    else if (key == SDLK_PAGEDOWN)
                        ui.palette_entry = ts_ui_palette_cycle_entry(
                                               ui.palette_entry, 1);
                    else if (key == SDLK_i) palette_import(&ui);
                    else if (key == SDLK_s) palette_save(&ui, 0);
                    else if (key == SDLK_e) palette_save(&ui, 1);
                    else if (key == SDLK_r) {
                        ts_palette_default(&ui.palette);
                        snprintf(ui.status, sizeof(ui.status),
                                 "RESTORED FACTORY TAPESISTER PALETTE");
                    }
                } else if (ui.config_open) {
                    char *field = ts_config_field(&ui.config, ui.config_field);
                    if (key == SDLK_ESCAPE) cancel_config(&ui);
                    else if ((mod & KMOD_CTRL) && key == SDLK_BACKSPACE && field != NULL) {
                        field[0] = '\0';
                        ui.config_cursor = 0;
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
                        save_config(&ui);
                    else if (key == SDLK_TAB || key == SDLK_DOWN)
                        select_config_field(&ui,
                            ts_ui_config_cycle_field(ui.config_field, 1));
                    else if (key == SDLK_UP)
                        select_config_field(&ui,
                            ts_ui_config_cycle_field(ui.config_field, -1));
                    else if (key == SDLK_BACKSPACE && field != NULL)
                        text_backspace(field, &ui.config_cursor);
                    else if (key == SDLK_DELETE && field != NULL)
                        text_delete(field, &ui.config_cursor);
                    else if (key == SDLK_LEFT && field != NULL)
                        text_move_cursor(field, &ui.config_cursor, -1);
                    else if (key == SDLK_RIGHT && field != NULL)
                        text_move_cursor(field, &ui.config_cursor, 1);
                    else if (key == SDLK_HOME) ui.config_cursor = 0;
                    else if (key == SDLK_END && field != NULL)
                        ui.config_cursor = strlen(field);
                } else if (ui.renaming_bank_slot >= 0) {
                    if (key == SDLK_ESCAPE) cancel_bank_rename(&ui);
                    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
                        finish_bank_rename(&ui, &instrument);
                    else if (key == SDLK_BACKSPACE)
                        text_backspace(ui.bank_rename, &ui.bank_rename_cursor);
                    else if (key == SDLK_DELETE)
                        text_delete(ui.bank_rename, &ui.bank_rename_cursor);
                    else if (key == SDLK_LEFT)
                        text_move_cursor(ui.bank_rename, &ui.bank_rename_cursor, -1);
                    else if (key == SDLK_RIGHT)
                        text_move_cursor(ui.bank_rename, &ui.bank_rename_cursor, 1);
                    else if (key == SDLK_HOME) ui.bank_rename_cursor = 0;
                    else if (key == SDLK_END) ui.bank_rename_cursor = strlen(ui.bank_rename);
                } else if (ui.renaming_recipe_slot >= 0) {
                    if (key == SDLK_ESCAPE) cancel_recipe_rename(&ui);
                    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
                        finish_recipe_rename(&ui);
                    else if (key == SDLK_BACKSPACE)
                        text_backspace(ui.recipe_rename, &ui.recipe_rename_cursor);
                    else if (key == SDLK_DELETE)
                        text_delete(ui.recipe_rename, &ui.recipe_rename_cursor);
                    else if (key == SDLK_LEFT)
                        text_move_cursor(ui.recipe_rename, &ui.recipe_rename_cursor, -1);
                    else if (key == SDLK_RIGHT)
                        text_move_cursor(ui.recipe_rename, &ui.recipe_rename_cursor, 1);
                    else if (key == SDLK_HOME) ui.recipe_rename_cursor = 0;
                    else if (key == SDLK_END)
                        ui.recipe_rename_cursor = strlen(ui.recipe_rename);
                } else if (ui.export_choice_open) {
                    if (key == SDLK_ESCAPE) {
                        ui.export_choice_open = 0;
                        snprintf(ui.status, sizeof(ui.status), "EXPORT CANCELLED");
                    } else if (key == SDLK_c || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        ui.export_choice_open = 0;
                        browser_open(&ui, TS_BROWSER_EXPORT_WAV);
                    } else if (key == SDLK_f) {
                        ui.export_choice_open = 0;
                        browser_open_bank(&ui, &instrument);
                    }
                } else if (ui.browser.mode != TS_BROWSER_CLOSED) {
                    if (key == SDLK_ESCAPE) browser_cancel(&ui);
                    else if (ui.browser.filename_focus && key == SDLK_LEFT)
                        ts_browser_move_filename_cursor(&ui.browser, -1);
                    else if (ui.browser.filename_focus && key == SDLK_RIGHT)
                        ts_browser_move_filename_cursor(&ui.browser, 1);
                    else if (ui.browser.filename_focus && key == SDLK_HOME)
                        ts_browser_set_filename_cursor(&ui.browser, 0);
                    else if (ui.browser.filename_focus && key == SDLK_END)
                        ts_browser_set_filename_cursor(&ui.browser,
                                                       strlen(ui.browser.filename));
                    else if (ui.browser.filename_focus && key == SDLK_DELETE)
                        ts_browser_delete_filename(&ui.browser);
                    else if (key == SDLK_UP) {
                        ui.browser.filename_focus = 0;
                        ts_browser_move_selection(&ui.browser, -1);
                    } else if (key == SDLK_DOWN) {
                        ui.browser.filename_focus = 0;
                        ts_browser_move_selection(&ui.browser, 1);
                    } else if (key == SDLK_PAGEUP) {
                        ui.browser.filename_focus = 0;
                        ts_browser_move_selection(&ui.browser, -TS_BROWSER_VISIBLE_ROWS);
                    } else if (key == SDLK_PAGEDOWN) {
                        ui.browser.filename_focus = 0;
                        ts_browser_move_selection(&ui.browser, TS_BROWSER_VISIBLE_ROWS);
                    }
                    else if (key == SDLK_HOME) {
                        ui.browser.filename_focus = 0;
                        ui.browser.selected = ui.browser.entry_count > 0 ? 0 : -1;
                        ts_browser_set_scroll(&ui.browser, 0);
                        ui.browser.overwrite_armed = 0;
                    } else if (key == SDLK_END) {
                        ui.browser.filename_focus = 0;
                        ui.browser.selected = ui.browser.entry_count - 1;
                        ts_browser_set_scroll(&ui.browser, ui.browser.entry_count);
                        ui.browser.overwrite_armed = 0;
                    } else if (key == SDLK_TAB &&
                               ts_browser_mode_edits_filename(ui.browser.mode)) {
                        ui.browser.filename_focus = !ui.browser.filename_focus;
                        ui.browser.overwrite_armed = 0;
                    } else if (key == SDLK_BACKSPACE) {
                        if (ui.browser.filename_focus &&
                            ts_browser_mode_edits_filename(ui.browser.mode))
                            ts_browser_backspace_filename(&ui.browser);
                        else ts_browser_parent(&ui.browser);
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        browser_activate_selection(device, &audio, &ui, &instrument,
                                                   &pending_selection_load,
                                                   &sample_pages, parked_instrument,
                                                   record_bank_active);
                    }
                } else if (key >= SDLK_F1 && key <= SDLK_F8) {
                    int octave = ts_ui_keyboard_set_octave(&ui, (int)(key - SDLK_F1));
                    snprintf(ui.status, sizeof(ui.status),
                             "KEYBOARD OCTAVE %d - HELD CHORD PRESERVED", octave);
                } else if (hovered_slider != TS_UI_SLIDER_NONE) {
                    adjust_hovered_slider(device, &audio, &ui, &instrument,
                                          hovered_slider,
                                          key == SDLK_RIGHT ? 1 : -1,
                                          (mod & KMOD_SHIFT) != 0);
                } else if ((mod & KMOD_CTRL) && key == SDLK_c) {
                    copy_selection_to_clipboard(device, &audio, &ui, &instrument,
                                                &clipboard, &clipboard_origin_first,
                                                &clipboard_source_frames,
                                                &clipboard_source_rate);
                } else if ((mod & KMOD_CTRL) && key == SDLK_x) {
                    cut_selection_to_clipboard(device, &audio, &ui, &instrument,
                                               &clipboard, &clipboard_origin_first,
                                               &clipboard_source_frames,
                                               &clipboard_source_rate);
                } else if ((mod & KMOD_CTRL) && key == SDLK_v) {
                    paste_from_clipboard(device, &audio, &ui, &instrument,
                                         &clipboard, clipboard_origin_first,
                                         (mod & KMOD_SHIFT) != 0);
                } else if ((mod & KMOD_CTRL) && key == SDLK_o) {
                    ui.load_bank_slot = instrument.selected_slot;
                    browser_open(&ui, TS_BROWSER_LOAD_WAV);
                } else if ((mod & KMOD_CTRL) && key == SDLK_s) {
                    browser_open(&ui, ui.show_ingredients ?
                                 TS_BROWSER_SAVE_PRESET : TS_BROWSER_SAVE_RECIPE);
                } else if ((mod & KMOD_CTRL) && key == SDLK_e) {
                    begin_export_choice(&ui);
                } else if ((mod & KMOD_CTRL) && key == SDLK_z) {
                    history_move(device, &audio, &ui, &instrument, 0);
                } else if ((mod & KMOD_CTRL) && key == SDLK_y) {
                    history_move(device, &audio, &ui, &instrument, 1);
                } else if ((mod & KMOD_CTRL) && key == SDLK_a) {
                    select_current_tile(device, &audio, &ui, &instrument, 0);
                } else if ((mod & KMOD_CTRL) && key == SDLK_r) {
                    apply_sample_edit(device, &audio, &ui, &instrument,
                                      TS_SAMPLE_EDIT_REVERSE, 1.0f);
                } else if ((mod & KMOD_CTRL) && key == SDLK_n) {
                    apply_sample_edit(device, &audio, &ui, &instrument,
                                      TS_SAMPLE_EDIT_NORMALIZE, 0.98f);
                } else if ((mod & KMOD_CTRL) && key == SDLK_i) {
                    apply_sample_edit(device, &audio, &ui, &instrument,
                                      TS_SAMPLE_EDIT_FADE_IN, 1.0f);
                } else if ((mod & KMOD_CTRL) && key == SDLK_u) {
                    apply_sample_edit(device, &audio, &ui, &instrument,
                                      TS_SAMPLE_EDIT_FADE_OUT, 1.0f);
                } else if ((mod & KMOD_CTRL) && key == SDLK_UP) {
                    apply_sample_edit(device, &audio, &ui, &instrument,
                                      TS_SAMPLE_EDIT_GAIN, 1.4125376f);
                } else if ((mod & KMOD_CTRL) && key == SDLK_DOWN) {
                    apply_sample_edit(device, &audio, &ui, &instrument,
                                      TS_SAMPLE_EDIT_GAIN, 0.7079458f);
                } else if (key == SDLK_1 && (mod & KMOD_SHIFT) != 0 &&
                           (mod & (KMOD_CTRL | KMOD_ALT)) == 0) {
                    (void)set_record_bank(window, device, input_device,
                                          &external_input, &audio, &ui,
                                          &instrument, &sample_pages,
                                          &parked_instrument,
                                          &record_bank_active, 1, &transform);
                    ts_ui_select_panel(&ui, TS_UI_PANEL_SAMPLE_TILES);
                } else if ((mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) == 0 &&
                           key >= SDLK_1 && key <= SDLK_4) {
                    TsUiPanel panel = (TsUiPanel)(key - SDLK_1);
                    TsUiPanel before = ts_ui_panel(&ui);
                    static const char *const names[] = {
                        "SAMPLE TILES", "KEYBOARD", "CDP", "DSP"
                    };
                    if (panel == TS_UI_PANEL_SAMPLE_TILES && record_bank_active) {
                        (void)set_record_bank(window, device, input_device,
                                              &external_input, &audio, &ui,
                                              &instrument, &sample_pages,
                                              &parked_instrument,
                                              &record_bank_active, 0, &transform);
                    } else if (panel == TS_UI_PANEL_SAMPLE_TILES &&
                               before == TS_UI_PANEL_SAMPLE_TILES) {
                        size_t count = ts_sample_pages_count(&sample_pages);
                        size_t target = count > 0u ?
                            (ts_sample_pages_active(&sample_pages) + 1u) % count : 0u;
                        (void)switch_sample_page(device, &external_input,
                                                 &audio, &ui, &instrument,
                                                 &sample_pages, target, &transform);
                    } else {
                        ts_ui_select_panel(&ui, panel);
                        ui.bank_view_slot = -1;
                        if (panel == TS_UI_PANEL_CDP)
                            snprintf(ui.status, sizeof(ui.status),
                                     "CDP %d PAGE%s - KEYS 1 2 3 4",
                                     ui.cdp_page + 1,
                                     before == TS_UI_PANEL_CDP ? " TOGGLED" : " RESTORED");
                        else if (panel == TS_UI_PANEL_DSP)
                            snprintf(ui.status, sizeof(ui.status),
                                     "DSP %d %s%s - KEYS 1 2 3 4",
                                     ui.dsp_page + 1,
                                     ui.dsp_page == 0 ? "PROCESS" : "PRIMITIVES",
                                     before == TS_UI_PANEL_DSP ? " TOGGLED" : " RESTORED");
                        else
                            snprintf(ui.status, sizeof(ui.status),
                                     "%s PANEL - KEYS 1 2 3 4", names[panel]);
                    }
                } else if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT) {
                        size_t anchor = instrument.has_selection ?
                                        instrument.crop_first +
                                        (instrument.selection_first +
                                         instrument.selection_last) / 2u :
                                        (ui.parent_view_first + ui.parent_view_last) / 2u;
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_ui_zoom_parent_view(&ui, instrument.parent.frames,
                                                        anchor, 0.5f, 0.5f) ?
                                 "ZOOMED SOURCE IN" : "ZOOM LIMIT");
                    } else {
                        size_t anchor = instrument.has_selection ?
                                        (instrument.selection_first +
                                         instrument.selection_last) / 2u :
                                        (instrument.view_first + instrument.view_last) / 2u;
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_instrument_zoom_view(&instrument, anchor, 0.5f, 0.5f) ?
                                 "ZOOMED IN" : "ZOOM LIMIT");
                    }
                } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT) {
                        size_t anchor = instrument.has_selection ?
                                        instrument.crop_first +
                                        (instrument.selection_first +
                                         instrument.selection_last) / 2u :
                                        (ui.parent_view_first + ui.parent_view_last) / 2u;
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_ui_zoom_parent_view(&ui, instrument.parent.frames,
                                                        anchor, 0.5f, 2.0f) ?
                                 "ZOOMED SOURCE OUT" : "SHOWING ALL SOURCE");
                    } else {
                        size_t anchor = instrument.has_selection ?
                                        (instrument.selection_first +
                                         instrument.selection_last) / 2u :
                                        (instrument.view_first + instrument.view_last) / 2u;
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_instrument_zoom_view(&instrument, anchor, 0.5f, 2.0f) ?
                                 "ZOOMED OUT" : "SHOWING ALL TILE");
                    }
                } else if (key == SDLK_0) {
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT) {
                        ts_ui_reset_parent_view(&ui, instrument.parent.frames);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL SOURCE");
                    } else {
                        ts_instrument_show_all(&instrument);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL TILE");
                    }
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    ui.bank_view_slot = -1;
                    size_t span = ui.audition_source == TS_AUDITION_PARENT ?
                                  ui.parent_view_last - ui.parent_view_first :
                                  instrument.view_last - instrument.view_first;
                    ptrdiff_t amount = (ptrdiff_t)(span / 8u);
                    if (amount < 1) amount = 1;
                    if (key == SDLK_LEFT) amount = -amount;
                    snprintf(ui.status, sizeof(ui.status),
                             (ui.audition_source == TS_AUDITION_PARENT ?
                              ts_ui_pan_parent_view(&ui, instrument.parent.frames, amount) :
                              ts_instrument_pan_view(&instrument, amount)) ?
                             "PANNED WAVEFORM VIEW" : "PAN LIMIT");
                } else if (key == SDLK_ESCAPE) {
                    ui.bank_clear_armed = 0;
                    if (record_bank_active && external_capture_busy(&external_input)) {
                        cancel_external_capture(input_device, &external_input, &ui);
                    } else if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
                        audio.capture.state == TS_CAPTURE_RECORDING) {
                        cancel_capture(device, &audio, &ui);
                    } else if (ui.has_pitch_suggestion) {
                        cancel_pitch_preview(device, &audio, &ui, &instrument);
                        stop_all(device, &audio, &ui);
                        snprintf(ui.status, sizeof(ui.status),
                                 "PITCH PREVIEW CANCELLED - STOPPED");
                    } else if (ui.tape_dragging) {
                        ui.tape_dragging = 0;
                        ui.tape_drag_button = 0;
                        snprintf(ui.status, sizeof(ui.status), "TAPE DRAG CANCELLED");
                    } else if (ui.selecting) {
                        ui.selecting = 0;
                        ui.selecting_button = 0;
                        (void)ts_instrument_reset_selection_playhead(&instrument);
                        ui.has_stretch_readout = 0;
                        snprintf(ui.status, sizeof(ui.status),
                                 "SELECTION CLEARED - PLAYHEAD AT START");
                    } else if (ui.wave_pointer_pending) {
                        ui.wave_pointer_pending = 0;
                        ui.wave_pointer_button = 0;
                        (void)ts_instrument_reset_selection_playhead(&instrument);
                        ui.has_stretch_readout = 0;
                        snprintf(ui.status, sizeof(ui.status),
                                 "SELECTION CLEARED - PLAYHEAD AT START");
                    } else if (ui.dragging_loop_endpoint) {
                        ui.dragging_loop_endpoint = 0;
                        ui.loop_drag_started = 0;
                        snprintf(ui.status, sizeof(ui.status), "LOOP DRAG ENDED");
                    } else if (ts_instrument_reset_selection_playhead(&instrument)) {
                        ui.bank_view_slot = -1;
                        ui.has_stretch_readout = 0;
                        snprintf(ui.status, sizeof(ui.status),
                                 "SELECTION CLEARED - PLAYHEAD AT START");
                    } else {
                        begin_exit_confirmation(device, &audio, &ui, &instrument,
                                                &sample_pages, parked_instrument,
                                                record_bank_active);
                    }
                } else if (key == SDLK_SPACE) {
                    ui.bank_clear_armed = 0;
                    if (record_bank_active &&
                        external_input.recorder.state == TS_EXTERNAL_CAPTURE_RECORDING)
                        stop_external_capture_early(input_device, &external_input, &ui);
                    else if (record_bank_active &&
                             external_input.recorder.state == TS_EXTERNAL_CAPTURE_ARMED)
                        snprintf(ui.status, sizeof(ui.status),
                                 "REC ARMED - MAKE SOUND OR ESC/CAPTURE TO CANCEL");
                    else if (audio.capture.state == TS_CAPTURE_RECORDING)
                        stop_capture_early(device, &audio, &ui);
                    else if (audio.playing || ts_note_bank_count(&audio.notes) > 0 ||
                        ui.workbench_loop_active)
                        stop_all(device, &audio, &ui);
                    else {
                        ui.audition_source = TS_AUDITION_CURRENT;
                        if (ts_ui_space_plays_selection(&instrument))
                            begin_audition(device, &audio, &ui, &instrument,
                                           TS_AUDITION_SELECTION, 1.0,
                                           obtained.freq);
                        else {
                            if (!instrument.has_playhead)
                                (void)ts_instrument_reset_selection_playhead(&instrument);
                            begin_playhead_audition(device, &audio, &ui, &instrument,
                                                    obtained.freq);
                        }
                    }
                } else {
                    int note = note_for_key(key);
                    if (note >= 0 && device) {
                        if (audio.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
                            audio.capture.staged_notes != 0u) {
                            launch_staged_capture(device, &audio, &ui, &instrument,
                                                  note, obtained.freq);
                        } else {
                            if ((mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) == 0) {
                                SDL_LockAudioDevice(device);
                                ts_note_bank_clear(&audio.notes);
                                SDL_UnlockAudioDevice(device);
                            }
                            begin_note(device, &audio, &ui, &instrument,
                                       note, obtained.freq, 0);
                        }
                    }
                }
            } else if (event.type == SDL_KEYUP && ui.stretch_wheel_active &&
                       (event.key.keysym.sym == SDLK_LSHIFT ||
                        event.key.keysym.sym == SDLK_RSHIFT ||
                        event.key.keysym.sym == SDLK_LALT ||
                        event.key.keysym.sym == SDLK_RALT)) {
                end_stretch_gesture(device, &audio, &ui, &instrument, 0);
            } else if (event.type == SDL_KEYUP && ui.warp_wheel_active &&
                       (event.key.keysym.sym == SDLK_LCTRL ||
                        event.key.keysym.sym == SDLK_RCTRL)) {
                end_warp_gesture(device, &audio, &ui, &instrument, 0);
            } else if (event.type == SDL_KEYUP && ui.smear_wheel_active &&
                       (event.key.keysym.sym == SDLK_LCTRL || event.key.keysym.sym == SDLK_RCTRL)) {
                end_smear_gesture(device, &audio, &ui, &instrument, 0);
            } else if (event.type == SDL_KEYUP && ui.tear_wheel_active &&
                       (event.key.keysym.sym == SDLK_LCTRL || event.key.keysym.sym == SDLK_RCTRL)) {
                end_tear_gesture(device, &audio, &ui, &instrument, 0);
            } else if (event.type == SDL_KEYUP && !ui.config_open && !ui.palette_open &&
                       ui.renaming_bank_slot < 0 &&
                       ui.renaming_recipe_slot < 0 && !ui.export_choice_open &&
                       ui.exchange_dialog == TS_UI_EXCHANGE_NONE &&
                       !ui.load_selection_choice_open &&
                       !ui.transform_open &&
                       !ui.drone_open &&
                       !ui.exit_confirm_open &&
                       ui.browser.mode == TS_BROWSER_CLOSED &&
                       note_for_key(event.key.keysym.sym) >= 0) {
                release_note(device, &audio, &ui, note_for_key(event.key.keysym.sym));
            } else if (event.type == SDL_MOUSEWHEEL && ui.canvas_gesture.active) {
                snprintf(ui.status, sizeof(ui.status),
                         "RELEASE MOUSE TO COMMIT CANVAS - ESC CANCELS");
            } else if (event.type == SDL_MOUSEWHEEL && ui.palette_open) {
                int raw_x, raw_y, x, y, ignored;
                int wheel_y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ?
                              -event.wheel.y : event.wheel.y;
                int channel;
                SDL_GetMouseState(&raw_x, &raw_y);
                logical_mouse(window, raw_x, raw_y, &x, &y);
                channel = ts_ui_palette_channel_from_point(x, y, &ignored);
                if (channel >= 0 && wheel_y != 0) {
                    ui.palette_channel = channel;
                    palette_adjust(&ui, wheel_y *
                                   ((SDL_GetModState() & KMOD_SHIFT) ? 8 : 1));
                } else snprintf(ui.status, sizeof(ui.status),
                                "HOVER A PALETTE SLIDER TO USE THE WHEEL");
            } else if (event.type == SDL_MOUSEWHEEL && ui.transform_open) {
                int raw_x, raw_y, x, y;
                int wheel_y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ?
                              -event.wheel.y : event.wheel.y;
                int control;
                SDL_GetMouseState(&raw_x, &raw_y);
                logical_mouse(window, raw_x, raw_y, &x, &y);
                control = ts_ui_transform_control_from_point(x, y);
                if (wheel_y != 0 && control >= 0) {
                    int amount = wheel_y < 0 ? -wheel_y : wheel_y;
                    for (int step = 0; step < amount; ++step)
                        adjust_transform_control(
                            device, &audio, &ui, &instrument, &transform, control,
                            wheel_y > 0 ? 1 : -1,
                            (SDL_GetModState() & KMOD_SHIFT) == 0);
                } else if (wheel_y != 0 && ts_ui_transform_mix_contains(x, y)) {
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
                        float step = (SDL_GetModState() & KMOD_SHIFT) ? 0.01f : 0.05f;
                        ui.transform_values.mix += wheel_y > 0 ? step : -step;
                        if (ui.transform_values.mix < 0.0f) ui.transform_values.mix = 0.0f;
                        if (ui.transform_values.mix > 1.0f) ui.transform_values.mix = 1.0f;
                        mark_transform_stale(device, &audio, &ui, &transform,
                                             "MIX CHANGED - RENDER AGAIN");
                    }
                } else snprintf(ui.transform_message,
                                sizeof(ui.transform_message),
                                ui.transform_backend == TS_TRANSFORM_BACKEND_DSP ?
                                "HOVER A DSP MACRO TO USE THE WHEEL" :
                                "HOVER A CONTROL OR MIX TO USE THE WHEEL");
            } else if (event.type == SDL_MOUSEWHEEL && ui.drone_open) {
                int raw_x, raw_y, x, y;
                int wheel_y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ?
                              -event.wheel.y : event.wheel.y;
                SDL_GetMouseState(&raw_x, &raw_y);
                logical_mouse(window, raw_x, raw_y, &x, &y);
                if (wheel_y != 0 && ts_ui_drone_waveform_contains(x, y)) {
                    int handle = ts_ui_drone_crossfade_handle_from_point(&ui, x, y);
                    int fine = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    size_t frames_per_step =
                        (size_t)instrument.current.sample_rate * (fine ? 1u : 10u) /
                        1000u;
                    size_t amount;
                    size_t requested;
                    if (frames_per_step < 1u) frames_per_step = 1u;
                    amount = frames_per_step *
                             (size_t)(wheel_y < 0 ? -(int64_t)wheel_y : wheel_y);
                    if (handle == 0)
                        handle = x < TS_DRONE_WAVE_X + TS_DRONE_WAVE_W / 2 ? 1 : 2;
                    if (wheel_y > 0) {
                        requested = ui.drone_overlap_frames > SIZE_MAX - amount ?
                                    SIZE_MAX : ui.drone_overlap_frames + amount;
                    } else {
                        requested = amount >= ui.drone_overlap_frames ?
                                    1u : ui.drone_overlap_frames - amount;
                    }
                    (void)adjust_drone_crossfade(
                        device, &audio, &ui, &instrument, &drone_preview,
                        requested, handle, wheel_y > 0 ? 1 : -1, obtained.freq);
                } else snprintf(ui.status, sizeof(ui.status),
                                "HOVER THE DRONE WAVEFORM TO ADJUST CROSSFADE");
            } else if (event.type == SDL_MOUSEWHEEL &&
                       (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||
                        ui.config_open || ui.export_choice_open ||
                        ui.exchange_dialog != TS_UI_EXCHANGE_NONE ||
                        ui.load_selection_choice_open ||
                        ui.transform_open ||
                        ui.exit_confirm_open)) {
                snprintf(ui.status, sizeof(ui.status),
                         "FINISH OR CANCEL THE OPEN DIALOG FIRST");
            } else if (event.type == SDL_MOUSEWHEEL && ui.browser.mode != TS_BROWSER_CLOSED) {
                int wheel_y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ?
                              -event.wheel.y : event.wheel.y;
                ts_browser_scroll(&ui.browser, -wheel_y * 3);
            } else if (event.type == SDL_MOUSEWHEEL) {
                int raw_x, raw_y, x, y;
                int wheel_y = event.wheel.y;
                int wheel_x = event.wheel.x;
                SDL_Keymod mod = SDL_GetModState();
                SDL_GetMouseState(&raw_x, &raw_y);
                logical_mouse(window, raw_x, raw_y, &x, &y);
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_y = -wheel_y;
                    wheel_x = -wheel_x;
                }
                if (ui.stretch_wheel_active &&
                    ((mod & KMOD_SHIFT) == 0 || (mod & KMOD_ALT) == 0)) {
                    end_stretch_gesture(device, &audio, &ui, &instrument, 0);
                    continue;
                }
                if (ui.show_keyboard && (mod & KMOD_SHIFT) && wheel_y != 0 &&
                    x >= 10 && x < 622 && y >= 318 && y < 379) {
                    char note_name[8];
                    int base_note = ts_ui_keyboard_shift_semitone(&ui, wheel_y);
                    snprintf(ui.status, sizeof(ui.status),
                             "KEYBOARD START %s - HELD CHORD PRESERVED",
                             ts_midi_note_name(base_note, note_name, sizeof(note_name)));
                } else if ((mod & KMOD_CTRL) && wheel_y != 0 &&
                    x >= 407 && x < 500 && y >= 205 && y < 229) {
                    if ((!ui.tear_gesture.active && begin_tear_gesture(device, &audio, &ui, &instrument, 1)) ||
                        ui.tear_wheel_active) {
                        float amount = ui.tear_amount + (float)wheel_y * 0.015f;
                        preview_tear_gesture(device, &audio, &ui, &instrument, amount, obtained.freq);
                    }
                } else if ((mod & KMOD_CTRL) && wheel_y != 0 &&
                    x >= 505 && x < 630 && y >= 205 && y < 229) {
                    if ((!ui.smear_gesture.active && begin_smear_gesture(device, &audio, &ui, &instrument, 1)) ||
                        ui.smear_wheel_active) {
                        float amount = ui.smear_amount + (float)wheel_y * 0.015f;
                        preview_smear_gesture(device, &audio, &ui, &instrument, amount, obtained.freq);
                    }
                } else if ((mod & KMOD_CTRL) && wheel_y != 0 &&
                    x >= 244 && x < 330 && y >= 233 && y < 257) {
                    if ((!ui.warp_gesture.active &&
                         begin_warp_gesture(device, &audio, &ui, &instrument, 1)) ||
                        ui.warp_wheel_active) {
                        float amount = ui.warp_amount + (float)wheel_y * 0.015f;
                        preview_warp_gesture(device, &audio, &ui, &instrument,
                                             amount, obtained.freq);
                    }
                } else if (wheel_y != 0 &&
                           ts_ui_slider_from_point(&ui, x, y) != TS_UI_SLIDER_NONE) {
                    adjust_hovered_slider(device, &audio, &ui, &instrument,
                                          ts_ui_slider_from_point(&ui, x, y),
                                          wheel_y,
                                          (mod & KMOD_SHIFT) != 0);
                } else if (ui.input_meter_active &&
                           x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    snprintf(ui.status, sizeof(ui.status),
                             "LIVE INPUT DISPLAY IS READ-ONLY WHILE REC IS ARMED");
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                    y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    ui.bank_view_slot = -1;
                    if ((mod & KMOD_ALT) && (mod & KMOD_SHIFT) && wheel_y != 0) {
                        size_t at = selection_frame_from_x(
                            &instrument, &ui, x - TS_WAVE_X);
                        if (!instrument.has_selection ||
                            at < instrument.selection_first ||
                            at > instrument.selection_last)
                            snprintf(ui.status, sizeof(ui.status),
                                     "HOVER THE SELECTED AUDIO TO CHANGE TAPE LENGTH");
                        else stretch_waveform(device, &audio, &ui, &instrument, wheel_y);
                    } else if ((mod & KMOD_ALT) && wheel_y != 0) {
                        size_t at = selection_frame_from_x(
                            &instrument, &ui, x - TS_WAVE_X);
                        size_t center;
                        int endpoint;
                        size_t detents = wheel_y < 0 ?
                            (size_t)(-(int64_t)wheel_y) : (size_t)wheel_y;
                        size_t crossings = (size_t)ui.config.rotate_wheel_coarse;
                        if (detents > SIZE_MAX / crossings) crossings = SIZE_MAX;
                        else crossings *= detents;
                        if (!instrument.has_selection ||
                            at < instrument.selection_first ||
                            at > instrument.selection_last) {
                            snprintf(ui.status, sizeof(ui.status),
                                     "HOVER THE SELECTION BEFORE ALT+WHEEL RESIZE");
                        } else {
                            center = instrument.selection_first +
                                (instrument.selection_last - instrument.selection_first) / 2u;
                            endpoint = at < center ? 1 : 2;
                            if (ts_instrument_resize_selection(
                                    &instrument, endpoint, wheel_y > 0, crossings))
                                snprintf(ui.status, sizeof(ui.status),
                                         "SELECTION %s %s TO ZERO CROSSING",
                                         wheel_y > 0 ? "EXPANDED" : "CONTRACTED",
                                         endpoint == 1 ? "LEFT" : "RIGHT");
                            else snprintf(ui.status, sizeof(ui.status),
                                          "SELECTION RESIZE LIMIT");
                        }
                    } else if ((mod & KMOD_CTRL) && wheel_y != 0) {
                        int crossings = (mod & KMOD_SHIFT) ? ui.config.rotate_wheel_fine :
                                                            ui.config.rotate_wheel_coarse;
                        rotate_waveform(device, &audio, &ui, &instrument,
                                        wheel_y, crossings);
                    } else if ((mod & KMOD_SHIFT) || wheel_x != 0) {
                        size_t span = ui.audition_source == TS_AUDITION_PARENT ?
                                      ui.parent_view_last - ui.parent_view_first :
                                      instrument.view_last - instrument.view_first;
                        ptrdiff_t step = (ptrdiff_t)(span / 8u);
                        ptrdiff_t amount;
                        if (step < 1) step = 1;
                        amount = -(ptrdiff_t)wheel_y * step + (ptrdiff_t)wheel_x * step;
                        snprintf(ui.status, sizeof(ui.status),
                                 (ui.audition_source == TS_AUDITION_PARENT ?
                                  ts_ui_pan_parent_view(&ui, instrument.parent.frames, amount) :
                                  ts_instrument_pan_view(&instrument, amount)) ?
                                 "MOUSE PANNED WAVEFORM VIEW" : "PAN LIMIT");
                    } else if (wheel_y != 0) {
                        size_t anchor = ui.audition_source == TS_AUDITION_PARENT ?
                                        ts_ui_parent_frame_from_x(
                                            &ui, instrument.parent.frames,
                                            x - TS_WAVE_X, TS_WAVE_W) :
                                        ts_instrument_frame_from_view_x(
                                            &instrument, x - TS_WAVE_X, TS_WAVE_W);
                        float ratio = (float)(x - TS_WAVE_X) / (float)TS_WAVE_W;
                        float scale = powf(0.75f, (float)wheel_y);
                        snprintf(ui.status, sizeof(ui.status),
                                 (ui.audition_source == TS_AUDITION_PARENT ?
                                  ts_ui_zoom_parent_view(&ui, instrument.parent.frames,
                                                         anchor, ratio, scale) :
                                  ts_instrument_zoom_view(
                                      &instrument, anchor, ratio, scale)) ?
                                 "MOUSE ZOOM - POINTER ANCHORED" : "ZOOM LIMIT");
                    }
                }
            } else if (event.type == SDL_MOUSEMOTION && ui.canvas_gesture.active) {
                preview_canvas_capture(window, device, &audio, &ui, &instrument,
                                       event.motion.x);
            } else if (event.type == SDL_MOUSEMOTION && ui.warp_dragging) {
                int x, y;
                float amount;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                amount = (float)(x - 244) / 86.0f;
                preview_warp_gesture(device, &audio, &ui, &instrument,
                                     amount, obtained.freq);
            } else if (event.type == SDL_MOUSEMOTION && ui.smear_dragging) {
                int x, y; float amount;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y); (void)y;
                amount = (float)(x - 505) / 125.0f;
                preview_smear_gesture(device, &audio, &ui, &instrument, amount, obtained.freq);
            } else if (event.type == SDL_MOUSEMOTION && ui.tear_dragging) {
                int x, y; float amount;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y); (void)y;
                amount = (float)(x - 407) / 93.0f;
                preview_tear_gesture(device, &audio, &ui, &instrument, amount, obtained.freq);
            } else if (event.type == SDL_MOUSEMOTION &&
                       ui.transform_open && ui.transform_selection_dragging) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                update_transform_selection_drag(&ui, &instrument, x);
            } else if (event.type == SDL_MOUSEMOTION &&
                       ui.drone_open && ui.drone_crossfade_dragging) {
                int x, y;
                int delta_x;
                int64_t delta_frames;
                int64_t requested;
                size_t selection_frames = ui.drone_source_last - ui.drone_source_first;
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
                    } else if (x >= 135 && x < 255 && y >= 326 && y < 349) {
                        browser_action(device, &audio, &ui, &instrument,
                                       &pending_selection_load,
                                       &sample_pages, parked_instrument,
                                       record_bank_active);
                    } else if (x >= 260 && x < 344 && y >= 326 && y < 349) {
                        browser_cancel(&ui);
                    }
                } else if (y >= 4 && y < 28 && x >= 350 && x < 426) {
                    begin_config(&ui);
                } else if (y >= 4 && y < 28 && x >= 431 && x < 511) {
                    begin_exchange_send(&ui, &instrument);
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
                        if (ui.has_pitch_suggestion && x < 470)
                            snprintf(ui.status, sizeof(ui.status),
                                     "PITCH PREVIEW ACTIVE - ACCEPT OR ESC CANCEL");
                        else if (x >= 10 && x < 58 &&
                                 instrument.audible_tuning.root_note > 0)
                            apply_audible_tuning(device, &audio, &ui, &instrument,
                                                 instrument.audible_tuning.root_note - 1,
                                                 instrument.audible_tuning.fine_tune_cents);
                        else if (x >= 156 && x < 204 &&
                                 instrument.audible_tuning.root_note < 127)
                            apply_audible_tuning(device, &audio, &ui, &instrument,
                                                 instrument.audible_tuning.root_note + 1,
                                                 instrument.audible_tuning.fine_tune_cents);
                        else if (x >= 214 && x < 360)
                            apply_audible_tuning(device, &audio, &ui, &instrument,
                                                 instrument.audible_tuning.root_note,
                                                 (float)(x - 214) / 146.0f * 200.0f - 100.0f);
                        else if (x >= 470 && x < 630)
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
                        if (x >= 10 && x < 530) {
                            instrument.family_mutation = (float)(x - 10) / 520.0f;
                            snprintf(ui.status, sizeof(ui.status), "RANGE %d",
                                     (int)lrintf(instrument.family_mutation * 100.0f));
                        } else if (x >= 538 && x < 630) {
                            instrument.family_trajectory = !instrument.family_trajectory;
                            snprintf(ui.status, sizeof(ui.status), instrument.family_trajectory ?
                                     "CHAIN ON" : "CHAIN OFF");
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
                    int keep_control = record_bank_active &&
                                       !ui.show_keyboard && !ui.show_recipes &&
                                       !ui.show_ingredients &&
                                       ts_ui_record_keep_button_from_point(x, y);
                    int monitor_control = record_bank_active &&
                                          !ui.show_keyboard && !ui.show_recipes &&
                                          !ui.show_ingredients &&
                                          ts_ui_monitor_button_from_point(x, y);
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
                    } else if (keep_control) {
                        keep_record_bank(device, &external_input, &audio,
                                         &instrument, &sample_pages, &ui,
                                         &transform);
                    } else if (monitor_control) {
                        toggle_external_monitor(device, &input_device, &audio,
                                                &external_input, &ui);
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
                    apply_tuning(device, &audio, &ui, &instrument,
                                 ts_ui_keyboard_base_note(&ui) + note,
                                 instrument.audible_tuning.fine_tune_cents);
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
            sync_external_capture_ui(input_device, &external_input, &ui);
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
            ui.active_notes = ts_note_bank_mask(&audio.notes);
            ui.playback_active = audio.playing || voice != NULL;
            if (audio.playing) {
                ui.playhead_source = audio.source;
                ui.playhead_bank_slot = audio.bank_slot;
                ui.playhead_frame = audio.position > 0.0 ? (size_t)audio.position : 0;
                ui.playhead_frames = audio.sample ? audio.sample->frames : 0;
            } else if (voice != NULL) {
                ui.playhead_source = voice->source;
                ui.playhead_bank_slot = -1;
                ui.playhead_frame = voice->position > 0.0 ? (size_t)voice->position : 0;
                ui.playhead_frames = voice->sample ? voice->sample->frames : 0;
            } else {
                ui.playhead_bank_slot = -1;
                ui.playhead_frame = 0;
                ui.playhead_frames = 0;
            }
        }
        if (device) SDL_UnlockAudioDevice(device);
        ui.text_cursor_visible = ((SDL_GetTicks() / 500u) & 1u) == 0u;
        ts_ui_render(&framebuffer, &ui, &instrument);
        SDL_UpdateTexture(texture, NULL, framebuffer.pixels, TS_UI_WIDTH * (int)sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
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
    ts_sample_free(&drone_preview);
    ts_sample_free(&pending_selection_load);
    ts_sample_free(&clipboard);
    ts_sample_pages_free(&sample_pages);
    if (parked_instrument != NULL) {
        ts_instrument_free(parked_instrument);
        free(parked_instrument);
        parked_instrument = NULL;
    }
    ts_instrument_free(&instrument);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    diagnostic_log("shutdown complete diagnostic_failed=%d", diagnostic_failed);
    return diagnostic_failed ? 2 : 0;
}
