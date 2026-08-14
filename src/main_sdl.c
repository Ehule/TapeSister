#include "tapesister/sample.h"
#include "tapesister/note_bank.h"
#include "tapesister/ui.h"

#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
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

static const char *config_file_path(void)
{
    const char *override = getenv("TAPESISTER_CONFIG");
    return override != NULL && override[0] != '\0' ? override : "tapesister.ini";
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
    }
    return hash;
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
} AudioState;

static uint32_t advance_seed(uint32_t seed)
{
    return seed * 1664525u + 1013904223u;
}

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
        out[i] = value * 0.8f;
        if (i + 1 < values) out[i + 1] = value * 0.8f;
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

static void begin_audition(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           const TsInstrument *instrument, TsAuditionRange range,
                           double pitch, int output_rate)
{
    TsAuditionPlan plan;
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
    audio->looping = range == TS_AUDITION_LOOP;
    audio->crossfade_frames = audio->looping ?
                              ts_audition_crossfade_frames(&plan,
                                  instrument->loop_crossfade_ms) : 0;
    audio->step = ((double)plan.sample->sample_rate / output_rate) * pitch;
    audio->playing = 1;
    SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "PLAYING %s %s",
             ts_audition_source_name(ui->audition_source),
             ts_audition_range_name(range));
}

static void begin_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                       const TsInstrument *instrument, int note, int output_rate, int latched)
{
    TsNoteStartResult result;
    int voice_count;
    ui->bank_view_slot = -1;
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "AUDIO UNAVAILABLE");
        return;
    }
    SDL_LockAudioDevice(device);
    audio->playing = 0;
    audio->bank_slot = -1;
    result = ts_note_bank_start_tuned(&audio->notes, instrument,
                                      ts_ui_audition_tuning(ui, instrument),
                                      ui->audition_source, note, latched, output_rate);
    voice_count = ts_note_bank_count(&audio->notes);
    SDL_UnlockAudioDevice(device);
    if (result == TS_NOTE_LIMIT_REACHED)
        snprintf(ui->status, sizeof(ui->status), "CHORD LIMIT %d NOTES", TS_NOTE_VOICE_LIMIT);
    else if (result == TS_NOTE_TOGGLED_OFF)
        snprintf(ui->status, sizeof(ui->status), "CHORD NOTE REMOVED");
    else if (result == TS_NOTE_STARTED && latched)
        snprintf(ui->status, sizeof(ui->status), "CHORD %d/%d - SHIFT+CLICK TO TOGGLE",
                 voice_count, TS_NOTE_VOICE_LIMIT);
    else if (result == TS_NOTE_STARTED)
        snprintf(ui->status, sizeof(ui->status), "PLAYING %s NOTE",
                 ts_audition_source_name(ui->audition_source));
}

static void release_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, int note)
{
    if (device) SDL_LockAudioDevice(device);
    ts_note_bank_release(&audio->notes, note);
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "NOTE RELEASED");
}

static void switch_audition_source(SDL_AudioDeviceID device, AudioState *audio,
                                   TsUiState *ui, const TsInstrument *instrument,
                                   TsAuditionSource source, int output_rate)
{
    TsAuditionPlan plan;
    int was_bank_view = ui->bank_view_slot >= 0;
    ui->bank_view_slot = -1;
    if (source == ui->audition_source) {
        if (was_bank_view && audio->bank_slot >= 0) {
            if (device) SDL_LockAudioDevice(device);
            audio->playing = 0;
            audio->bank_slot = -1;
            audio->sample = source == TS_AUDITION_PARENT ?
                            &instrument->parent : &instrument->current;
            if (device) SDL_UnlockAudioDevice(device);
        }
        if (was_bank_view)
            snprintf(ui->status, sizeof(ui->status), "AUDITIONING %s",
                     ts_audition_source_name(source));
        return;
    }
    if (!audition_plan_ui(instrument, ui, source,
                          audio->playing && audio->bank_slot < 0 ?
                          audio->range : TS_AUDITION_ALL, &plan)) {
        snprintf(ui->status, sizeof(ui->status), "NOTHING TO AUDITION");
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    if (audio->playing && audio->bank_slot >= 0) {
        audio->playing = 0;
        audio->bank_slot = -1;
    } else if (audio->playing) {
        audio->position = ts_audition_map_progress(
            audio->position, audio->range_start, audio->range_end, plan.first, plan.last);
        audio->range_start = plan.first;
        audio->range_end = plan.last;
        audio->crossfade_frames = audio->looping ?
                                  ts_audition_crossfade_frames(&plan,
                                      instrument->loop_crossfade_ms) : 0;
        if (output_rate > 0)
            audio->step = ((double)plan.sample->sample_rate / output_rate) * audio->pitch;
    }
    audio->sample = plan.sample;
    audio->source = source;
    ts_note_bank_set_source_tuned(&audio->notes, instrument,
                                  ts_ui_audition_tuning(ui, instrument),
                                  source, output_rate);
    if (device) SDL_UnlockAudioDevice(device);
    ui->audition_source = source;
    snprintf(ui->status, sizeof(ui->status), "AUDITIONING %s%s",
             ts_audition_source_name(source), audio->playing ? " - PLAYING" : "");
}

static void stop_all(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
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
    ui->dragging_loop_endpoint = 0;
    ui->loop_drag_started = 0;
    snprintf(ui->status, sizeof(ui->status), "STOPPED");
}

static void begin_exit_confirmation(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, const TsInstrument *instrument)
{
    stop_all(device, audio, ui);
    ui->exit_has_unsaved = instrument_state_hash(instrument) != ui->saved_state_hash;
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
        /* Bank slots own stable buffers and do not remap through Current edits. */
    } else if (audio->playing && audition_plan_ui(instrument, ui, audio->source,
                                           audio->range, &plan)) {
        audio->position = ts_audition_map_progress(
            audio->position, audio->range_start, audio->range_end,
            plan.first, plan.last);
        if (audio->position >= (double)plan.last) audio->position = (double)plan.first;
        audio->sample = plan.sample;
        audio->range_start = plan.first;
        audio->range_end = plan.last;
        audio->looping = audio->range == TS_AUDITION_LOOP && instrument->has_loop;
        audio->loop_mode = instrument->loop_mode;
        if (audio->loop_mode == TS_LOOP_REVERSE) audio->loop_direction = -1;
        else if (audio->loop_mode == TS_LOOP_FORWARD) audio->loop_direction = 1;
        else if (audio->loop_direction == 0) audio->loop_direction = 1;
        audio->crossfade_frames = audio->looping ?
                                  ts_audition_crossfade_frames(
                                      &plan, instrument->loop_crossfade_ms) : 0;
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

static int load_instrument(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument, const char *path)
{
    char error[160];
    int recipe = path_is_tsr(path);
    int preset = path_is_tsp(path);
    int ok;
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
            ui->show_recipes = 1;
            ui->recipes.active_slot = slot - 1;
            ui->has_pitch_suggestion = 0;
            snprintf(ui->status, sizeof(ui->status), "LOADED RECIPE %.31s%s - UNDO RESTORES",
                     loaded.name, loaded.has_tuning ? " + TUNING" : "");
        } else snprintf(ui->status, sizeof(ui->status), "TSP LOAD FAILED: %.131s", error);
        return ok;
    }
    lock_edit(device, audio);
    ui->bank_view_slot = -1;
    if (audio->bank_slot >= 0) {
        audio->playing = 0;
        audio->bank_slot = -1;
    }
    if (recipe)
        ok = ts_instrument_load_recipe(instrument, path, error, sizeof(error));
    else
        ok = ts_instrument_load_wav(instrument, path, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) ts_ui_reset_parent_view(ui, instrument->parent.frames);
    if (ok && recipe)
        snprintf(ui->status, sizeof(ui->status), "OPENED TSR PROJECT %.112s",
                 instrument->parent.name);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "IMPORTED NEUTRAL SOURCE %.104s",
                          instrument->parent.name);
    else snprintf(ui->status, sizeof(ui->status), "LOAD FAILED: %.135s", error);
    if (ok && recipe) ui->saved_state_hash = instrument_state_hash(instrument);
    return ok;
}

static int generate_parent(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument, int next_family)
{
    char error[160];
    TsGeneratorKind kind = instrument->generator.kind;
    uint32_t seed = instrument->generator.seed;
    int ok;
    if (next_family) {
        kind = (TsGeneratorKind)((kind + 1) % TS_GENERATOR_COUNT);
        seed = advance_seed(seed);
    }
    lock_edit(device, audio);
    ui->bank_view_slot = -1;
    if (audio->bank_slot >= 0) {
        audio->playing = 0;
        audio->bank_slot = -1;
    }
    ok = ts_instrument_generate(instrument, kind, seed, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) ts_ui_reset_parent_view(ui, instrument->parent.frames);
    if (ok) snprintf(ui->status, sizeof(ui->status), "NEW %s SOURCE %08X",
                     ts_generator_name(kind), seed);
    else snprintf(ui->status, sizeof(ui->status), "CREATE FAILED: %.130s", error);
    return ok;
}

static void begin_bank_audition(SDL_AudioDeviceID device, AudioState *audio,
                                TsUiState *ui, const TsInstrument *instrument,
                                int slot, int output_rate);

static void generate_family_candidate(SDL_AudioDeviceID device, AudioState *audio,
                                      TsUiState *ui, TsInstrument *instrument,
                                      int reseed, int promote, int force_stranger)
{
    char error[160];
    TsFamilyRelation selected_relation = instrument->family_relation;
    int anchor = ui->bank_view_slot;
    int slot = -1;
    int ok;
    if (force_stranger) instrument->family_relation = TS_FAMILY_STRANGER;
    lock_edit(device, audio);
    if (audio->bank_slot >= 0) {
        audio->playing = 0;
        audio->bank_slot = -1;
    }
    ok = ts_instrument_generate_family_candidate(instrument, anchor, reseed,
                                                  &slot, error, sizeof(error));
    instrument->family_relation = selected_relation;
    if (ok && promote)
        ok = ts_instrument_set_bank_as_current(instrument, slot,
                                               error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) {
        snprintf(ui->status, sizeof(ui->status), "VARIATION FAILED: %.132s", error);
        return;
    }
    if (promote) {
        ui->bank_view_slot = -1;
        ts_ui_reset_parent_view(ui, instrument->parent.frames);
        snprintf(ui->status, sizeof(ui->status),
                 "BANK %02d %s CREATED AND SET CURRENT",
                 slot + 1, ts_family_relation_name(instrument->bank[slot].relation));
        return;
    }
    ui->show_keyboard = 0;
    ui->show_recipes = 0;
    begin_bank_audition(device, audio, ui, instrument, slot, audio->output_rate);
    if (instrument->bank[slot].has_generator)
        snprintf(ui->status, sizeof(ui->status),
                 "BANK %02d %s %s OF %02d  SEED %08X%s",
                 slot + 1, ts_family_relation_name(instrument->bank[slot].relation),
                 ts_generator_name(instrument->bank[slot].generator.kind),
                 instrument->bank[slot].parent_slot + 1,
                 instrument->bank[slot].lineage_seed,
                 instrument->family_trajectory ? "  CHAIN" : "");
    else
        snprintf(ui->status, sizeof(ui->status),
                 "BANK %02d %s OF %02d  SEED %08X%s",
                 slot + 1, ts_family_relation_name(instrument->bank[slot].relation),
                 instrument->bank[slot].parent_slot + 1,
                 instrument->bank[slot].lineage_seed,
                 instrument->family_trajectory ? "  CHAIN" : "");
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
        snprintf(ui->status, sizeof(ui->status), "BODY %.2F - SOURCE PRESERVED", process.body);
    else if (ok && strcmp(label, "EDGE") == 0)
        snprintf(ui->status, sizeof(ui->status), "EDGE %.2F - SOURCE PRESERVED", process.edge);
    else if (ok && strcmp(label, "DRIFT") == 0)
        snprintf(ui->status, sizeof(ui->status), "DRIFT %.2F - SOURCE PRESERVED", process.drift);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "%s UPDATED - SOURCE PRESERVED", label);
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

static void apply_recipe_slot(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                              TsInstrument *instrument, int slot)
{
    char error[160];
    int ok;
    const TsPortableRecipe *recipe;
    if (slot < 0 || slot >= TS_RECIPE_SLOT_COUNT ||
        !ui->recipes.slots[slot].occupied) {
        snprintf(ui->status, sizeof(ui->status), "EMPTY USER RECIPE - SHIFT+CLICK CAPTURES");
        return;
    }
    recipe = &ui->recipes.slots[slot];
    lock_edit(device, audio);
    ok = recipe->has_tuning ?
         ts_instrument_set_process_and_tunings(instrument, &recipe->process,
                                               &recipe->tuning,
                                               &recipe->audible_tuning,
                                               error, sizeof(error)) :
         ts_instrument_set_process(instrument, &recipe->process, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) {
        ui->recipes.active_slot = slot;
        ui->has_pitch_suggestion = 0;
        snprintf(ui->status, sizeof(ui->status), "APPLIED %.31s%s",
                 recipe->name, recipe->has_tuning ? " + TUNING" : "");
    } else snprintf(ui->status, sizeof(ui->status), "RECIPE FAILED: %.135s", error);
}

static void capture_recipe_slot(TsUiState *ui, const TsInstrument *instrument, int slot)
{
    char error[160];
    char name[32];
    snprintf(name, sizeof(name), "USER %02d", slot - TS_FACTORY_RECIPE_COUNT + 1);
    if (ts_recipe_bank_capture(&ui->recipes, slot, &instrument->process,
                               &instrument->tuning, &instrument->audible_tuning,
                               name, error, sizeof(error)))
        snprintf(ui->status, sizeof(ui->status), "CAPTURED %.31s - TOP SAVE WRITES TSP", name);
    else snprintf(ui->status, sizeof(ui->status), "RECIPE CAPTURE FAILED: %.126s", error);
}

static void reset_current(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                          TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_reset_current(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CURRENT RESET EXACTLY TO SOURCE");
    else snprintf(ui->status, sizeof(ui->status), "RESET FAILED: %.137s", error);
}

static void commit_current(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_commit_current(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    ui->commit_armed = 0;
    if (ok) ts_ui_reset_parent_view(ui, instrument->parent.frames);
    if (ok) snprintf(ui->status, sizeof(ui->status), "COMMITTED CURRENT AS GENERATION %u SOURCE",
                     instrument->generation);
    else snprintf(ui->status, sizeof(ui->status), "COMMIT FAILED: %.136s", error);
}

static void crop_selection(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_crop_selection(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CROPPED CURRENT - SOURCE PRESERVED");
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
    if (ok) snprintf(ui->status, sizeof(ui->status), "%s %s - SOURCE PRESERVED",
                     ts_sample_edit_name(kind), selected ? "SELECTION" : "ALL");
    else snprintf(ui->status, sizeof(ui->status), "EDIT FAILED: %.137s", error);
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
                 "WHOLE CURRENT SELECTED AND LOOPED - %zu FRAMES",
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
    audio->pitch = 1.0;
    audio->step = (double)slot->sample.sample_rate / output_rate;
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
                              int slot, TsBankCaptureKind kind)
{
    char error[160];
    int ok;
    if (device) SDL_LockAudioDevice(device);
    ok = ts_instrument_bank_capture(instrument, slot, kind, error, sizeof(error));
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CAPTURED %s TO BANK %02d",
                     ts_bank_capture_name(kind), slot + 1);
    else snprintf(ui->status, sizeof(ui->status), "BANK CAPTURE FAILED: %.130s", error);
}

static void clear_bank_slot(SDL_AudioDeviceID device, AudioState *audio,
                            TsUiState *ui, TsInstrument *instrument, int slot)
{
    char error[160];
    int ok;
    if (device) SDL_LockAudioDevice(device);
    if (audio->bank_slot == slot) {
        audio->playing = 0;
        audio->bank_slot = -1;
    }
    ok = ts_instrument_bank_clear(instrument, slot, error, sizeof(error));
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CLEARED BANK %02d", slot + 1);
    else snprintf(ui->status, sizeof(ui->status), "BANK CLEAR FAILED: %.132s", error);
}

static void clear_all_bank_slots(SDL_AudioDeviceID device, AudioState *audio,
                                 TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    int ok;
    if (!ui->bank_clear_armed) {
        ui->bank_clear_armed = 1;
        snprintf(ui->status, sizeof(ui->status),
                 "CLICK CLEAR ALL AGAIN - SOURCE SLOT 01 WILL BE KEPT");
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
                 "CLEARED COLLECTION SLOTS 02-16 - SOURCE KEPT");
    else
        snprintf(ui->status, sizeof(ui->status),
                 "CLEAR ALL FAILED: %.137s", error);
}

static void set_auditioned_bank_current(SDL_AudioDeviceID device, AudioState *audio,
                                        TsUiState *ui, TsInstrument *instrument)
{
    char error[160];
    int slot = ui->bank_view_slot;
    int ok;
    if (slot < 0 || slot >= TS_BANK_SLOT_COUNT || !instrument->bank[slot].occupied) {
        snprintf(ui->status, sizeof(ui->status), "AUDITION A FILLED BANK SLOT FIRST");
        return;
    }
    lock_edit(device, audio);
    audio->playing = 0;
    audio->bank_slot = -1;
    ts_note_bank_clear(&audio->notes);
    ui->audition_source = TS_AUDITION_CURRENT;
    ok = ts_instrument_set_bank_as_current(instrument, slot, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    ui->active_notes = 0;
    ui->mouse_note = -1;
    ui->commit_armed = 0;
    if (ok) {
        ts_ui_reset_parent_view(ui, instrument->parent.frames);
        snprintf(ui->status, sizeof(ui->status),
                 "BANK %02d SET AS CURRENT - CLEAN EDIT BASE G%u",
                 slot + 1, instrument->generation);
    } else {
        snprintf(ui->status, sizeof(ui->status), "SET CURRENT FAILED: %.133s", error);
    }
}

static void begin_bank_rename(TsUiState *ui, const TsInstrument *instrument, int slot)
{
    if (slot <= 0 || slot >= TS_BANK_SLOT_COUNT) {
        snprintf(ui->status, sizeof(ui->status), "BANK 01 ROOT NAME IS FIXED");
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
    ui->commit_armed = 0;
    ui->config_before_edit = ui->config;
    ui->config_open = 1;
    select_config_field(ui, TS_CONFIG_SAMPLE_PATH);
    SDL_StartTextInput();
    snprintf(ui->status, sizeof(ui->status), "EDITING TAPESISTER PATHS");
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

static void begin_recipe_rename(TsUiState *ui, int slot)
{
    if (slot < TS_FACTORY_RECIPE_COUNT) {
        snprintf(ui->status, sizeof(ui->status), "FACTORY RECIPE NAMES ARE FIXED");
        return;
    }
    if (slot >= TS_RECIPE_SLOT_COUNT || !ui->recipes.slots[slot].occupied) {
        snprintf(ui->status, sizeof(ui->status), "CAPTURE OR LOAD A USER RECIPE FIRST");
        return;
    }
    ui->renaming_recipe_slot = slot;
    snprintf(ui->recipe_rename, sizeof(ui->recipe_rename), "%s",
             ui->recipes.slots[slot].name);
    ui->recipe_rename_cursor = strlen(ui->recipe_rename);
    SDL_StartTextInput();
    snprintf(ui->status, sizeof(ui->status), "RENAMING USER RECIPE %02d", slot + 1);
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
    ui->commit_armed = 0;
    ui->export_choice_open = 1;
    SDL_StopTextInput();
    snprintf(ui->status, sizeof(ui->status), "EXPORT CURRENT WAV OR COMPLETE COLLECTION");
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
    ui->commit_armed = 0;
    SDL_StopTextInput();
    if (ts_browser_open(&ui->browser, mode, filename)) {
        if (mode != TS_BROWSER_LOAD_WAV) SDL_StartTextInput();
        snprintf(ui->status, sizeof(ui->status), "%s", ts_browser_mode_title(mode));
    } else {
        snprintf(ui->status, sizeof(ui->status), "BROWSER FAILED: %.142s", ui->browser.message);
        ts_browser_close(&ui->browser);
    }
}

static void browser_open_bank(TsUiState *ui, const TsInstrument *instrument)
{
    char folder[TS_BROWSER_NAME_MAX + 1];
    if (!ts_instrument_family_folder_name(instrument, folder, sizeof(folder)))
        snprintf(folder, sizeof(folder), "TapeSister_set");
    ui->commit_armed = 0;
    SDL_StopTextInput();
    if (ts_browser_open(&ui->browser, TS_BROWSER_EXPORT_BANK, folder)) {
        SDL_StartTextInput();
        snprintf(ui->status, sizeof(ui->status), "EXPORT SOUND COLLECTION");
    } else {
        snprintf(ui->status, sizeof(ui->status), "BROWSER FAILED: %.142s", ui->browser.message);
        ts_browser_close(&ui->browser);
    }
}

static void send_to_fasttracker(TsUiState *ui, const TsInstrument *instrument)
{
    const char *directory = ui->config.exchange_path[0] != '\0' ?
                            ui->config.exchange_path : ui->config.sample_path;
    char destination[TS_BROWSER_PATH_MAX];
    char error[160];
    if (!path_is_directory(directory)) {
        snprintf(ui->status, sizeof(ui->status),
                 "CONFIGURE AN EXISTING FT2 EXCHANGE OR SAMPLE PATH FIRST");
        return;
    }
    if (!ts_instrument_next_family_path(instrument, directory,
                                        destination, sizeof(destination),
                                        error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "FT2 PATH FAILED: %.134s", error);
        return;
    }
    if (!ts_instrument_export_bank(instrument, destination, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "FT2 EXPORT FAILED: %.132s", error);
        return;
    }
    if (ui->config.fasttracker_path[0] == '\0') {
        snprintf(ui->status, sizeof(ui->status),
                 "FT2 COLLECTION READY %.100s - SET EXECUTABLE TO AUTO LAUNCH",
                 destination);
        return;
    }
    if (!launch_program(ui->config.fasttracker_path, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status),
                 "COLLECTION READY %.60s  LAUNCH FAILED %.48s", destination, error);
        return;
    }
    snprintf(ui->status, sizeof(ui->status),
             "SENT %d LOOP AWARE SAMPLES TO %.83s - FASTTRACKER LAUNCHED",
             ts_instrument_bank_count(instrument), destination);
}

static void browser_cancel(TsUiState *ui)
{
    SDL_StopTextInput();
    ts_browser_close(&ui->browser);
    snprintf(ui->status, sizeof(ui->status), "FILE OPERATION CANCELLED");
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

static int save_recipe_atomic(const TsInstrument *instrument, const char *destination,
                              char *error, size_t error_size)
{
    char temporary[TS_BROWSER_PATH_MAX + 32];
    int written = snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", destination);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        snprintf(error, error_size, "Destination path is too long");
        return 0;
    }
    if (!ts_instrument_save_recipe(instrument, temporary, error, error_size)) {
        remove(temporary);
        return 0;
    }
    return finish_atomic_file(temporary, destination, error, error_size);
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

static void browser_action(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument)
{
    TsBrowser *browser = &ui->browser;
    char path[TS_BROWSER_PATH_MAX];
    char error[160];
    int ok = 0;
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
        ok = load_instrument(device, audio, ui, instrument, path);
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
            ok = save_recipe_atomic(instrument, path, error, sizeof(error));
            if (ok) ui->saved_state_hash = instrument_state_hash(instrument);
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
            snprintf(ui->status, sizeof(ui->status), ok ? "EXPORTED CURRENT %.101s" :
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
                                       TsUiState *ui, TsInstrument *instrument)
{
    TsBrowser *browser = &ui->browser;
    if (browser->selected >= 0 && browser->selected < browser->entry_count &&
        browser->entries[browser->selected].is_directory && !browser->filename_focus) {
        if (!ts_browser_enter_selected_directory(browser))
            snprintf(browser->message, sizeof(browser->message), "COULD NOT ENTER DIRECTORY");
        return;
    }
    browser_action(device, audio, ui, instrument);
}

static void logical_mouse(SDL_Window *window, int raw_x, int raw_y, int *x, int *y)
{
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    *x = raw_x * TS_UI_WIDTH / ww;
    *y = raw_y * TS_UI_HEIGHT / wh;
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
    snprintf(ui->status, sizeof(ui->status), "%s - DRAG GHOST TO ZERO-SNAPPED DESTINATION",
             tape_gesture_name(ui->tape_drag_kind));
    return 1;
}

static void update_tape_drag(TsUiState *ui, const TsInstrument *instrument, int x)
{
    int64_t pointer = tape_frame_from_x(instrument, x);
    size_t length = ui->tape_source_last - ui->tape_source_first;
    int64_t destination = pointer - (int64_t)ui->tape_grab_offset;
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

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec desired, obtained;
    TsInstrument instrument;
    TsUiState ui;
    TsFramebuffer framebuffer;
    AudioState audio = {0};
    int running = 1;

    ts_instrument_init(&instrument);
    ts_ui_init(&ui);
    {
        char config_error[160];
        if (!ts_config_load(&ui.config, config_file_path(),
                            config_error, sizeof(config_error)))
            fprintf(stderr, "TapeSister config: %s\n", config_error);
        else if (path_is_directory(ui.config.sample_path))
            snprintf(ui.browser.directory, sizeof(ui.browser.directory), "%s",
                     ui.config.sample_path);
    }
    ts_note_bank_init(&audio.notes);
    audio.bank_slot = -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
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
    if (running && !show_splash(renderer)) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        ts_instrument_free(&instrument);
        return 0;
    }

    SDL_zero(desired);
    SDL_zero(obtained);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = audio_callback;
    desired.userdata = &audio;
    device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    audio.output_rate = obtained.freq;
    if (!device) snprintf(ui.status, sizeof(ui.status), "AUDIO UNAVAILABLE: %.130s", SDL_GetError());
    else SDL_PauseAudioDevice(device, 0);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    generate_parent(device, &audio, &ui, &instrument, 0);
    if (argc > 1) load_instrument(device, &audio, &ui, &instrument, argv[1]);
    ui.saved_state_hash = instrument_state_hash(&instrument);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                if (!ui.exit_confirm_open)
                    begin_exit_confirmation(device, &audio, &ui, &instrument);
            }
            else if (event.type == SDL_DROPFILE &&
                     (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||
                      ui.config_open || ui.export_choice_open || ui.exit_confirm_open ||
                      ui.browser.mode != TS_BROWSER_CLOSED)) {
                snprintf(ui.status, sizeof(ui.status),
                         "FINISH OR CANCEL THE OPEN DIALOG FIRST");
                SDL_free(event.drop.file);
            }
            else if (event.type == SDL_DROPFILE) {
                load_instrument(device, &audio, &ui, &instrument, event.drop.file);
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
                if (ui.browser.filename_focus && ui.browser.mode != TS_BROWSER_LOAD_WAV)
                    ts_browser_append_filename(&ui.browser, event.text.text);
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod mod = SDL_GetModState();
                if (!((mod & KMOD_CTRL) && key == SDLK_p)) ui.commit_armed = 0;
                ui.bank_clear_armed = 0;
                if (ui.exit_confirm_open) {
                    if (key == SDLK_ESCAPE || key == SDLK_n) {
                        ui.exit_confirm_open = 0;
                        snprintf(ui.status, sizeof(ui.status), "EXIT CANCELLED");
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                               key == SDLK_y) {
                        running = 0;
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
                            (TsConfigField)(ui.config_field + 1));
                    else if (key == SDLK_UP)
                        select_config_field(&ui,
                            (TsConfigField)(ui.config_field - 1));
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
                    } else if (key == SDLK_TAB && ui.browser.mode != TS_BROWSER_LOAD_WAV) {
                        ui.browser.filename_focus = !ui.browser.filename_focus;
                        ui.browser.overwrite_armed = 0;
                    } else if (key == SDLK_BACKSPACE) {
                        if (ui.browser.filename_focus && ui.browser.mode != TS_BROWSER_LOAD_WAV)
                            ts_browser_backspace_filename(&ui.browser);
                        else ts_browser_parent(&ui.browser);
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        browser_activate_selection(device, &audio, &ui, &instrument);
                    }
                } else if ((mod & KMOD_CTRL) && key == SDLK_o) {
                    browser_open(&ui, TS_BROWSER_LOAD_WAV);
                } else if ((mod & KMOD_CTRL) && key == SDLK_p) {
                    if (ui.commit_armed) commit_current(device, &audio, &ui, &instrument);
                    else {
                        ui.commit_armed = 1;
                        snprintf(ui.status, sizeof(ui.status), "CTRL+P AGAIN TO COMMIT CURRENT AS SOURCE");
                    }
                } else if ((mod & KMOD_CTRL) && key == SDLK_s) {
                    browser_open(&ui, ui.show_recipes ?
                                 TS_BROWSER_SAVE_PRESET : TS_BROWSER_SAVE_RECIPE);
                } else if ((mod & KMOD_CTRL) && key == SDLK_e) {
                    begin_export_choice(&ui);
                } else if ((mod & KMOD_CTRL) && key == SDLK_b) {
                    switch_audition_source(device, &audio, &ui, &instrument,
                        ui.audition_source == TS_AUDITION_CURRENT ?
                        TS_AUDITION_PARENT : TS_AUDITION_CURRENT, obtained.freq);
                } else if ((mod & KMOD_CTRL) && key == SDLK_z) {
                    history_move(device, &audio, &ui, &instrument, 0);
                } else if ((mod & KMOD_CTRL) && key == SDLK_y) {
                    history_move(device, &audio, &ui, &instrument, 1);
                } else if ((mod & KMOD_CTRL) && key == SDLK_a) {
                    cancel_pitch_preview(device, &audio, &ui, &instrument);
                    ui.bank_view_slot = -1;
                    ts_instrument_set_selection(&instrument, 0, instrument.current.frames);
                    snprintf(ui.status, sizeof(ui.status), "SELECTED ALL CURRENT");
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
                                 "ZOOMED OUT" : "SHOWING ALL CURRENT");
                    }
                } else if (key == SDLK_0) {
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT) {
                        ts_ui_reset_parent_view(&ui, instrument.parent.frames);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL SOURCE");
                    } else {
                        ts_instrument_show_all(&instrument);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL CURRENT");
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
                    ui.commit_armed = 0;
                    ui.bank_clear_armed = 0;
                    if (ui.has_pitch_suggestion) {
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
                        snprintf(ui.status, sizeof(ui.status), "SELECTION DRAG CANCELLED");
                    } else if (ui.dragging_loop_endpoint) {
                        ui.dragging_loop_endpoint = 0;
                        ui.loop_drag_started = 0;
                        snprintf(ui.status, sizeof(ui.status), "LOOP DRAG ENDED");
                    } else {
                        begin_exit_confirmation(device, &audio, &ui, &instrument);
                    }
                } else if (key == SDLK_SPACE) {
                    ui.commit_armed = 0;
                    ui.bank_clear_armed = 0;
                    stop_all(device, &audio, &ui);
                } else {
                    int note = note_for_key(key);
                    if (note >= 0 && device)
                        begin_note(device, &audio, &ui, &instrument, note, obtained.freq, 0);
                }
            } else if (event.type == SDL_KEYUP && !ui.config_open &&
                       ui.renaming_bank_slot < 0 &&
                       ui.renaming_recipe_slot < 0 && !ui.export_choice_open &&
                       !ui.exit_confirm_open &&
                       ui.browser.mode == TS_BROWSER_CLOSED &&
                       note_for_key(event.key.keysym.sym) >= 0) {
                release_note(device, &audio, &ui, note_for_key(event.key.keysym.sym));
            } else if (event.type == SDL_MOUSEWHEEL &&
                       (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||
                        ui.config_open || ui.export_choice_open || ui.exit_confirm_open)) {
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
                SDL_GetMouseState(&raw_x, &raw_y);
                logical_mouse(window, raw_x, raw_y, &x, &y);
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheel_y = -wheel_y;
                    wheel_x = -wheel_x;
                }
                if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                    y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    SDL_Keymod mod = SDL_GetModState();
                    ui.bank_view_slot = -1;
                    if ((mod & KMOD_CTRL) && (wheel_y != 0 || wheel_x != 0)) {
                        int detents = wheel_y != 0 ? wheel_y : -wheel_x;
                        int direction = detents < 0 ? 1 : -1;
                        int count = detents < 0 ? -detents : detents;
                        char error[160];
                        int moved = 0;
                        while (count-- > 0 &&
                               ts_instrument_rotate_zero_crossing(
                                   &instrument, direction, error, sizeof(error)))
                            moved = 1;
                        snprintf(ui.status, sizeof(ui.status), moved ?
                                 "CTRL+WHEEL ROTATED TO ZERO CROSSING" : error);
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
            } else if (event.type == SDL_MOUSEMOTION &&
                       (ui.renaming_bank_slot >= 0 || ui.renaming_recipe_slot >= 0 ||
                        ui.config_open || ui.export_choice_open || ui.exit_confirm_open)) {
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
            } else if (event.type == SDL_MOUSEMOTION && ui.selecting) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                size_t at = selection_frame_from_x(&instrument, &ui, x - TS_WAVE_X);
                ts_instrument_set_selection_snapped(&instrument, ui.selection_anchor, at);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                SDL_Keymod mod = SDL_GetModState();
                logical_mouse(window, event.button.x, event.button.y, &x, &y);
                if (!(y >= 205 && y < 228 && x >= 247 && x < 325)) ui.commit_armed = 0;
                if (!(y >= 289 && y < 312 && x >= 245 && x < 376 &&
                      !ui.show_keyboard && !ui.show_recipes))
                    ui.bank_clear_armed = 0;
                if (ui.exit_confirm_open) {
                    if (x >= 172 && x < 308 && y >= 188 && y < 211) {
                        running = 0;
                    } else if (x >= 324 && x < 468 && y >= 188 && y < 211) {
                        ui.exit_confirm_open = 0;
                        snprintf(ui.status, sizeof(ui.status), "EXIT CANCELLED");
                    }
                } else if (ui.config_open) {
                    static const int field_y[TS_CONFIG_FIELD_COUNT] = {103, 158, 213};
                    int selected = -1;
                    for (int i = 0; i < TS_CONFIG_FIELD_COUNT; ++i)
                        if (x >= 50 && x < 590 && y >= field_y[i] && y < field_y[i] + 24)
                            selected = i;
                    if (selected >= 0) {
                        const char *field;
                        size_t length;
                        size_t cursor;
                        size_t first;
                        size_t clicked;
                        if (selected != (int)ui.config_field)
                            select_config_field(&ui, (TsConfigField)selected);
                        field = ts_config_field_const(&ui.config, ui.config_field);
                        length = strlen(field);
                        cursor = ui.config_cursor > length ? length : ui.config_cursor;
                        first = length > 82u ? length - 82u : 0u;
                        if (cursor < first) first = cursor;
                        if (cursor > first + 82u) first = cursor - 82u;
                        clicked = first + (size_t)((x - 56 + 3) / 6);
                        if (clicked > length) clicked = length;
                        ui.config_cursor = clicked;
                    } else if (x >= 50 && x < 160 && y >= 274 && y < 297)
                        save_config(&ui);
                    else if (x >= 170 && x < 270 && y >= 274 && y < 297)
                        config_use_cwd(&ui);
                    else if (x >= 280 && x < 362 && y >= 274 && y < 297)
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
                            browser_activate_selection(device, &audio, &ui, &instrument);
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
                    } else if (ui.browser.mode != TS_BROWSER_LOAD_WAV &&
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
                        browser_action(device, &audio, &ui, &instrument);
                    } else if (x >= 260 && x < 344 && y >= 326 && y < 349) {
                        browser_cancel(&ui);
                    }
                } else if (y >= 4 && y < 28 && x >= 350 && x < 426) {
                    begin_config(&ui);
                } else if (y >= 4 && y < 28 && x >= 431 && x < 511) {
                    send_to_fasttracker(&ui, &instrument);
                } else if (y >= 4 && y < 28 && x >= 516 && x < 568) {
                    browser_open(&ui, ui.show_recipes ?
                                 TS_BROWSER_SAVE_PRESET : TS_BROWSER_SAVE_RECIPE);
                } else if (y >= 4 && y < 28 && x >= 573 && x < 630) {
                    begin_export_choice(&ui);
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
                    if (shown_bank != NULL && has_visible_loop &&
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
                            ui.selection_anchor = ts_sample_nearest_zero_crossing(
                                &instrument.current, selection_frame_from_x(
                                    &instrument, &ui, x - TS_WAVE_X));
                            ts_instrument_set_selection_snapped(
                                &instrument, ui.selection_anchor, ui.selection_anchor);
                            ui.selecting = 1;
                            snprintf(ui.status, sizeof(ui.status),
                                     "SELECTING CURRENT - ZERO SNAP");
                        }
                    }
                } else if (y >= 205 && y < 228 && x >= 10 && x < 80) {
                    ui.commit_armed = 0;
                    browser_open(&ui, TS_BROWSER_LOAD_WAV);
                } else if (y >= 205 && y < 228 && x >= 85 && x < 167) {
                    ui.commit_armed = 0;
                    generate_family_candidate(device, &audio, &ui, &instrument,
                                              0, (mod & KMOD_SHIFT) != 0,
                                              (mod & KMOD_CTRL) != 0);
                } else if (y >= 205 && y < 228 && x >= 172 && x < 242) {
                    ui.commit_armed = 0;
                    generate_family_candidate(device, &audio, &ui, &instrument,
                                              1, (mod & KMOD_SHIFT) != 0, 0);
                } else if (y >= 205 && y < 228 && x >= 247 && x < 325) {
                    if (ui.commit_armed) commit_current(device, &audio, &ui, &instrument);
                    else {
                        ui.commit_armed = 1;
                        snprintf(ui.status, sizeof(ui.status), "CLICK COMMIT AGAIN TO MAKE CURRENT THE SOURCE");
                    }
                } else if (y >= 205 && y < 228 && x >= 330 && x < 402) {
                    ui.commit_armed = 0;
                    reset_current(device, &audio, &ui, &instrument);
                } else if (y >= 205 && y < 228 && x >= 407 && x < 468) {
                    switch_audition_source(device, &audio, &ui, &instrument,
                                           TS_AUDITION_PARENT, obtained.freq);
                } else if (y >= 205 && y < 228 && x >= 472 && x < 535) {
                    switch_audition_source(device, &audio, &ui, &instrument,
                                           TS_AUDITION_CURRENT, obtained.freq);
                } else if (y >= 205 && y < 228 && x >= 540 && x < 630) {
                    ui.commit_armed = 0;
                    set_auditioned_bank_current(device, &audio, &ui, &instrument);
                } else if (y >= 233 && y < 257 && x >= 10 && x < 330) {
                    TsProcessRecipe process = instrument.process;
                    const char *label;
                    int start;
                    int width;
                    float *control;
                    ui.commit_armed = 0;
                    if (x < 110) { control = &process.body; start = 10; width = 100; label = "BODY"; }
                    else if (x < 220) { control = &process.edge; start = 120; width = 100; label = "EDGE"; }
                    else { control = &process.drift; start = 230; width = 100; label = "DRIFT"; }
                    *control = (float)(x - start) / (float)width;
                    if (*control < 0.0f) *control = 0.0f;
                    if (*control > 1.0f) *control = 1.0f;
                    apply_process(device, &audio, &ui, &instrument, process, label);
                } else if (y >= 233 && y < 256 && x >= 335 && x < 369) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_EDIT;
                    snprintf(ui.status, sizeof(ui.status), "SAMPLE EDITING PAGE");
                } else if (y >= 233 && y < 256 && x >= 372 && x < 406) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_TUNE;
                    snprintf(ui.status, sizeof(ui.status), "ROOT NOTE AND FINE TUNING");
                } else if (y >= 233 && y < 256 && x >= 409 && x < 445) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_NOISE;
                    snprintf(ui.status, sizeof(ui.status), "NOISE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 448 && x < 486) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_SHAPE;
                    snprintf(ui.status, sizeof(ui.status), "FILTER AND SHAPER PAGE");
                } else if (y >= 233 && y < 256 && x >= 489 && x < 523) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_FAMILY;
                    snprintf(ui.status, sizeof(ui.status),
                             "VARIATION RANGE AMOUNT LOCKS AND CHAIN");
                } else if (y >= 233 && y < 256 && x >= 526 && x < 562) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_DELAY;
                    snprintf(ui.status, sizeof(ui.status), "DELAY PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 565 && x < 594) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_SPACE;
                    snprintf(ui.status, sizeof(ui.status), "SPACE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 597 && x < 630) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_LOOP;
                    snprintf(ui.status, sizeof(ui.status), "LOOP EDITING PAGE");
                } else if (y >= 261 && y < 285) {
                    TsProcessRecipe process = instrument.process;
                    int changed = 0;
                    const char *label = "DSP";
                    ui.commit_armed = 0;
                    if (ui.fx_page == TS_FX_EDIT) {
                        if (x >= 10 && x < 104)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_REVERSE, 1.0f);
                        else if (x >= 109 && x < 203)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_NORMALIZE, 0.98f);
                        else if (x >= 208 && x < 292)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_GAIN, 0.7079458f);
                        else if (x >= 297 && x < 381)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_GAIN, 1.4125376f);
                        else if (x >= 386 && x < 496)
                            apply_sample_edit(device, &audio, &ui, &instrument,
                                              TS_SAMPLE_EDIT_FADE_IN, 1.0f);
                        else if (x >= 501 && x < 611)
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
                        if (x >= 10 && x < 110) {
                            if (instrument.family_relation == TS_FAMILY_CHILD)
                                instrument.family_relation = TS_FAMILY_COUSIN;
                            else if (instrument.family_relation == TS_FAMILY_COUSIN)
                                instrument.family_relation = TS_FAMILY_STRANGER;
                            else instrument.family_relation = TS_FAMILY_CHILD;
                            snprintf(ui.status, sizeof(ui.status), "NEXT CANDIDATE RELATION %s",
                                     ts_family_relation_name(instrument.family_relation));
                        } else if (x >= 115 && x < 225) {
                            instrument.family_mutation = (float)(x - 115) / 110.0f;
                            snprintf(ui.status, sizeof(ui.status), "VARIATION AMOUNT %d%%",
                                     (int)lrintf(instrument.family_mutation * 100.0f));
                        } else if (x >= 230 && x < 290)
                            instrument.family_locks ^= TS_FAMILY_LOCK_LOOP;
                        else if (x >= 294 && x < 346)
                            instrument.family_locks ^= TS_FAMILY_LOCK_DURATION;
                        else if (x >= 350 && x < 410)
                            instrument.family_locks ^= TS_FAMILY_LOCK_PITCH;
                        else if (x >= 414 && x < 468)
                            instrument.family_locks ^= TS_FAMILY_LOCK_ENVELOPE;
                        else if (x >= 472 && x < 534)
                            instrument.family_locks ^= TS_FAMILY_LOCK_SPECTRAL;
                        else if (x >= 538 && x < 630) {
                            instrument.family_trajectory = !instrument.family_trajectory;
                            if (!instrument.family_trajectory)
                                instrument.family_anchor_slot = ui.bank_view_slot >= 0 ?
                                                                ui.bank_view_slot : 0;
                            snprintf(ui.status, sizeof(ui.status),
                                     instrument.family_trajectory ?
                                     "CHAIN ON - EACH RESULT BECOMES THE NEXT SOURCE" :
                                     "CHAIN OFF - VARIATIONS SHARE A SOURCE");
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
                } else if (y >= 289 && y < 312 && x >= 10 && x < 80) {
                    begin_audition(device, &audio, &ui, &instrument,
                                   TS_AUDITION_ALL, 1.0, obtained.freq);
                } else if (y >= 289 && y < 312 && x >= 85 && x < 157) {
                    begin_audition(device, &audio, &ui, &instrument,
                                   TS_AUDITION_SELECTION, 1.0, obtained.freq);
                } else if (y >= 289 && y < 312 && x >= 162 && x < 240) {
                    begin_audition(device, &audio, &ui, &instrument,
                                   TS_AUDITION_DISPLAYED, 1.0, obtained.freq);
                } else if (y >= 289 && y < 312 && x >= 245 && x < 376 &&
                           !ui.show_keyboard && !ui.show_recipes) {
                    clear_all_bank_slots(device, &audio, &ui, &instrument);
                } else if (y >= 289 && y < 312 && x >= 245 && x < 297) {
                    ui.bank_clear_armed = 0;
                    crop_selection(device, &audio, &ui, &instrument);
                } else if (y >= 289 && y < 312 && x >= 302 && x < 376) {
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
                } else if (y >= 289 && y < 312 && x >= 381 && x < 455) {
                    ui.bank_view_slot = -1;
                    if (ui.audition_source == TS_AUDITION_PARENT) {
                        ts_ui_reset_parent_view(&ui, instrument.parent.frames);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL SOURCE");
                    } else {
                        ts_instrument_show_all(&instrument);
                        snprintf(ui.status, sizeof(ui.status), "SHOWING ALL CURRENT");
                    }
                } else if (y >= 289 && y < 312 && x >= 460 && x < 516) {
                    history_move(device, &audio, &ui, &instrument, 0);
                } else if (y >= 289 && y < 312 && x >= 521 && x < 583) {
                    history_move(device, &audio, &ui, &instrument, 1);
                } else if (y >= 289 && y < 312 && x >= 588 && x < 630) {
                    if (ui.show_keyboard) {
                        ui.show_keyboard = 0;
                        ui.show_recipes = 0;
                    } else if (!ui.show_recipes) {
                        ui.show_recipes = 1;
                        ui.bank_view_slot = -1;
                    } else {
                        ui.show_keyboard = 1;
                        ui.show_recipes = 0;
                    }
                    snprintf(ui.status, sizeof(ui.status), "%s PANEL",
                             ui.show_keyboard ? "KEYS" :
                             ui.show_recipes ? "PROCESS RECIPES" : "SAMPLE BANK");
                } else {
                    int note = ui.show_keyboard ? ts_ui_key_from_point(x, y) : -1;
                    int bank_slot = !ui.show_keyboard && !ui.show_recipes ?
                                    ts_ui_bank_slot_from_point(x, y) : -1;
                    int recipe_slot = ui.show_recipes ?
                                      ts_ui_recipe_slot_from_point(x, y) : -1;
                    if (recipe_slot >= 0) {
                        unsigned modifiers = bank_modifiers(mod);
                        if (modifiers == 0)
                            apply_recipe_slot(device, &audio, &ui, &instrument, recipe_slot);
                        else if (modifiers == TS_UI_BANK_MOD_SHIFT)
                            capture_recipe_slot(&ui, &instrument, recipe_slot);
                        else snprintf(ui.status, sizeof(ui.status),
                                      "CLICK APPLY  SHIFT+CLICK CAPTURE USER");
                    } else if (bank_slot >= 0) {
                        TsUiBankAction action = ts_ui_bank_action(
                            0, bank_modifiers(mod));
                        if (action == TS_UI_BANK_ACTION_AUDITION) {
                            begin_bank_audition(device, &audio, &ui, &instrument,
                                                bank_slot, obtained.freq);
                        } else if (action == TS_UI_BANK_ACTION_CAPTURE_CURRENT ||
                                   action == TS_UI_BANK_ACTION_CAPTURE_LOOP ||
                                   action == TS_UI_BANK_ACTION_CAPTURE_SELECTION) {
                            TsBankCaptureKind kind =
                                action == TS_UI_BANK_ACTION_CAPTURE_CURRENT ?
                                TS_BANK_CAPTURE_CURRENT :
                                action == TS_UI_BANK_ACTION_CAPTURE_LOOP ?
                                TS_BANK_CAPTURE_LOOP : TS_BANK_CAPTURE_SELECTION;
                            capture_bank_slot(device, &ui, &instrument,
                                              bank_slot, kind);
                        } else {
                            snprintf(ui.status, sizeof(ui.status),
                                     "USE ONE BANK CAPTURE MODIFIER AT A TIME");
                        }
                    } else if (ui.show_keyboard && note >= 0 && device) {
                        if (mod & KMOD_SHIFT) {
                            ui.mouse_note = -1;
                            begin_note(device, &audio, &ui, &instrument,
                                       note, obtained.freq, 1);
                        } else {
                            SDL_LockAudioDevice(device);
                            ts_note_bank_clear(&audio.notes);
                            SDL_UnlockAudioDevice(device);
                            ui.mouse_note = note;
                            begin_note(device, &audio, &ui, &instrument,
                                       note, obtained.freq, 0);
                        }
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                       event.button.button == SDL_BUTTON_RIGHT &&
                       !ui.config_open && ui.browser.mode == TS_BROWSER_CLOSED) {
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
                if (ui.exit_confirm_open) {
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
                             "CHOOSE CURRENT OR COLLECTION  ESC CANCELS");
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    if (!begin_tape_drag(&ui, &instrument, SDL_BUTTON_RIGHT,
                                         mod, x - TS_WAVE_X))
                        snprintf(ui.status, sizeof(ui.status),
                                 "SHIFT+RMB COPY OVERWRITE  CTRL+RMB MOVE OVERWRITE");
                } else if (note >= 0 && (mod & KMOD_SHIFT)) {
                    apply_tuning(device, &audio, &ui, &instrument,
                                 TS_KEYBOARD_BASE_NOTE + note,
                                 instrument.audible_tuning.fine_tune_cents);
                } else if (ui.show_recipes && recipe_slot >= 0 &&
                           (mod & KMOD_SHIFT)) {
                    char error[160];
                    if (ts_recipe_bank_clear(&ui.recipes, recipe_slot,
                                             error, sizeof(error)))
                        snprintf(ui.status, sizeof(ui.status), "CLEARED USER RECIPE %02d",
                                 recipe_slot + 1);
                    else snprintf(ui.status, sizeof(ui.status),
                                  "RECIPE CLEAR FAILED: %.126s", error);
                } else if (ui.show_recipes && recipe_slot >= 0 &&
                           bank_modifiers(mod) == 0) {
                    begin_recipe_rename(&ui, recipe_slot);
                } else if (ui.show_recipes && recipe_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "RMB RENAME  SHIFT+RMB CLEAR");
                } else if (!ui.show_keyboard && !ui.show_recipes && bank_slot >= 0 &&
                           action == TS_UI_BANK_ACTION_CLEAR) {
                    clear_bank_slot(device, &audio, &ui, &instrument, bank_slot);
                } else if (!ui.show_keyboard && !ui.show_recipes && bank_slot >= 0 &&
                           action == TS_UI_BANK_ACTION_RENAME) {
                    begin_bank_rename(&ui, &instrument, bank_slot);
                } else if (!ui.show_keyboard && !ui.show_recipes && bank_slot >= 0) {
                    snprintf(ui.status, sizeof(ui.status),
                             "RMB RENAME  SHIFT+RMB CLEAR");
                }
            } else if (event.type == SDL_MOUSEBUTTONUP &&
                       (event.button.button == SDL_BUTTON_LEFT ||
                        event.button.button == SDL_BUTTON_RIGHT)) {
                if (ui.tape_dragging && event.button.button == ui.tape_drag_button) {
                    finish_tape_drag(device, &audio, &ui, &instrument);
                    continue;
                }
                ui.browser.dragging_scrollbar = 0;
                ui.dragging_loop_endpoint = 0;
                ui.loop_drag_started = 0;
                if (event.button.button == SDL_BUTTON_LEFT && ui.selecting) {
                    ui.selecting = 0;
                    snprintf(ui.status, sizeof(ui.status), "SELECTED %zu FRAMES",
                             instrument.selection_last - instrument.selection_first);
                }
                if (event.button.button == SDL_BUTTON_LEFT && ui.mouse_note >= 0) {
                    release_note(device, &audio, &ui, ui.mouse_note);
                    ui.mouse_note = -1;
                }
            }
        }

        if (device) SDL_LockAudioDevice(device);
        {
            const TsNoteVoice *voice = ts_note_bank_display_voice(&audio.notes);
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

    if (device) SDL_CloseAudioDevice(device);
    ts_instrument_free(&instrument);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
