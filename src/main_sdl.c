#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

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
    int looping;
    int note_gate;
    int playing;
} AudioState;

static uint32_t advance_seed(uint32_t seed)
{
    return seed * 1664525u + 1013904223u;
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
                audio->position = ts_audition_wrap_position(
                    audio->position, audio->range_start, audio->range_end,
                    audio->crossfade_frames);
                value = ts_audition_read_looped(
                    audio->sample, audio->position, audio->range_start,
                    audio->range_end, audio->crossfade_frames);
                audio->position += audio->step;
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
        out[i] = value * 0.8f;
        if (i + 1 < values) out[i + 1] = value * 0.8f;
    }
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
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "AUDIO UNAVAILABLE");
        return;
    }
    if (!ts_audition_plan(instrument, ui->audition_source, range, &plan)) {
        snprintf(ui->status, sizeof(ui->status),
                 range == TS_AUDITION_SELECTION ? "SELECT A RANGE FIRST" :
                 range == TS_AUDITION_LOOP ? "SET A LOOP FIRST" :
                 "NOTHING TO AUDITION");
        return;
    }
    SDL_LockAudioDevice(device);
    audio->sample = plan.sample;
    audio->position = (double)plan.first;
    audio->pitch = pitch;
    audio->range_start = plan.first;
    audio->range_end = plan.last;
    audio->source = ui->audition_source;
    audio->range = range;
    audio->looping = range == TS_AUDITION_LOOP;
    audio->crossfade_frames = audio->looping ?
                              ts_audition_crossfade_frames(&plan,
                                  instrument->loop_crossfade_ms) : 0;
    audio->note_gate = 0;
    audio->step = ((double)plan.sample->sample_rate / output_rate) * pitch;
    audio->playing = 1;
    SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "PLAYING %s %s",
             ts_audition_source_name(ui->audition_source),
             ts_audition_range_name(range));
}

static void begin_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                       const TsInstrument *instrument, int note, int output_rate)
{
    begin_audition(device, audio, ui, instrument,
                   instrument->has_loop ? TS_AUDITION_LOOP : TS_AUDITION_NOTE,
                   pow(2.0, note / 12.0), output_rate);
    if (instrument->has_loop && audio->playing) audio->note_gate = 1;
    ui->active_key = note;
}

static void release_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
{
    int stopped = 0;
    if (device) SDL_LockAudioDevice(device);
    if (audio->note_gate) {
        audio->playing = 0;
        stopped = 1;
    }
    audio->note_gate = 0;
    if (device) SDL_UnlockAudioDevice(device);
    ui->active_key = -1;
    if (stopped) snprintf(ui->status, sizeof(ui->status), "LOOP NOTE RELEASED");
}

static void switch_audition_source(SDL_AudioDeviceID device, AudioState *audio,
                                   TsUiState *ui, const TsInstrument *instrument,
                                   TsAuditionSource source, int output_rate)
{
    TsAuditionPlan plan;
    if (source == ui->audition_source) return;
    if (!ts_audition_plan(instrument, source,
                          audio->playing ? audio->range : TS_AUDITION_ALL, &plan)) {
        snprintf(ui->status, sizeof(ui->status), "NOTHING TO AUDITION");
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    if (audio->playing) {
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
    if (device) SDL_UnlockAudioDevice(device);
    ui->audition_source = source;
    snprintf(ui->status, sizeof(ui->status), "AUDITIONING %s%s",
             ts_audition_source_name(source), audio->playing ? " - PLAYING" : "");
}

static void stop_all(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
{
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    audio->note_gate = 0;
    if (device) SDL_UnlockAudioDevice(device);
    ui->active_key = -1;
    snprintf(ui->status, sizeof(ui->status), "STOPPED");
}

static void lock_edit(SDL_AudioDeviceID device, AudioState *audio)
{
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
}

static void unlock_edit(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                        TsInstrument *instrument)
{
    audio->sample = ui->audition_source == TS_AUDITION_PARENT ?
                    &instrument->parent : &instrument->current;
    audio->source = ui->audition_source;
    if (device) SDL_UnlockAudioDevice(device);
}

static int load_instrument(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument, const char *path)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_load_wav(instrument, path, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "IMPORTED PARENT %.112s", instrument->parent.name);
    else snprintf(ui->status, sizeof(ui->status), "LOAD FAILED: %.135s", error);
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
    ok = ts_instrument_generate(instrument, kind, seed, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "NEW %s PARENT %08X",
                     ts_generator_name(kind), seed);
    else snprintf(ui->status, sizeof(ui->status), "GENERATE FAILED: %.130s", error);
    return ok;
}

static void reseed_parent(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                          TsInstrument *instrument)
{
    char error[160];
    uint64_t old_parent = ts_sample_hash(&instrument->parent);
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_reseed(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (!ok) snprintf(ui->status, sizeof(ui->status), "RESEED FAILED: %.132s", error);
    else if (ts_sample_hash(&instrument->parent) == old_parent)
        snprintf(ui->status, sizeof(ui->status), "RESEEDED PROCESSING - PARENT PRESERVED");
    else
        snprintf(ui->status, sizeof(ui->status), "RESEEDED %s PARENT %08X",
                 ts_generator_name(instrument->generator.kind), instrument->generator.seed);
}

static void apply_process(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                          TsInstrument *instrument, TsProcessRecipe process, const char *label)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_set_process(instrument, &process, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok && strcmp(label, "BODY") == 0)
        snprintf(ui->status, sizeof(ui->status), "BODY %.2F - PARENT PRESERVED", process.body);
    else if (ok && strcmp(label, "EDGE") == 0)
        snprintf(ui->status, sizeof(ui->status), "EDGE %.2F - PARENT PRESERVED", process.edge);
    else if (ok && strcmp(label, "DRIFT") == 0)
        snprintf(ui->status, sizeof(ui->status), "DRIFT %.2F - PARENT PRESERVED", process.drift);
    else if (ok) snprintf(ui->status, sizeof(ui->status), "%s UPDATED - PARENT PRESERVED", label);
    else snprintf(ui->status, sizeof(ui->status), "PROCESS FAILED: %.130s", error);
}

static void reset_current(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                          TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_reset_current(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "CURRENT RESET EXACTLY TO PARENT");
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
    if (ok) snprintf(ui->status, sizeof(ui->status), "COMMITTED CURRENT AS GENERATION %u PARENT",
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
    if (ok) snprintf(ui->status, sizeof(ui->status), "CROPPED CURRENT - PARENT PRESERVED");
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
    if (ok) snprintf(ui->status, sizeof(ui->status), "%s %s - PARENT PRESERVED",
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

static void set_loop(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                     TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_set_loop_from_selection(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "LOOP SET %zu FRAMES - ZERO SNAPPED",
                     instrument->loop_last - instrument->loop_first);
    else snprintf(ui->status, sizeof(ui->status), "LOOP FAILED: %.140s", error);
}

static void clear_loop(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                       TsInstrument *instrument)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_clear_loop(instrument, error, sizeof(error));
    unlock_edit(device, audio, ui, instrument);
    snprintf(ui->status, sizeof(ui->status), "%s", ok ? "LOOP CLEARED" : error);
}

static void set_loop_crossfade(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                               TsInstrument *instrument, float milliseconds)
{
    char error[160];
    int ok;
    if (device) SDL_LockAudioDevice(device);
    ok = ts_instrument_set_loop_crossfade(instrument, milliseconds, error, sizeof(error));
    if (ok && audio->looping && audio->sample != NULL) {
        TsAuditionPlan plan = {audio->sample, audio->range_start, audio->range_end};
        audio->crossfade_frames = ts_audition_crossfade_frames(
            &plan, instrument->loop_crossfade_ms);
    }
    if (device) SDL_UnlockAudioDevice(device);
    if (ok) snprintf(ui->status, sizeof(ui->status), "LOOP CROSSFADE %.1F MS",
                     instrument->loop_crossfade_ms);
    else snprintf(ui->status, sizeof(ui->status), "%.150s", error);
}

static void browser_open(TsUiState *ui, TsBrowserMode mode)
{
    const char *filename = mode == TS_BROWSER_SAVE_RECIPE ? "tapesister-recipe.tsr" :
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

static int export_wav_atomic(const TsSample *sample, const char *destination,
                             char *error, size_t error_size)
{
    char temporary[TS_BROWSER_PATH_MAX + 32];
    int written = snprintf(temporary, sizeof(temporary), "%s.tapesister-tmp", destination);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        snprintf(error, error_size, "Destination path is too long");
        return 0;
    }
    if (!ts_sample_save_wav16(sample, temporary, error, error_size)) {
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
            snprintf(browser->message, sizeof(browser->message), "SELECT A WAV FILE");
            return;
        }
        ok = load_instrument(device, audio, ui, instrument, path);
    } else {
        if (!ts_browser_destination_path(browser, path, sizeof(path))) {
            snprintf(browser->message, sizeof(browser->message), "ENTER A VALID FILENAME");
            return;
        }
        if (ts_browser_path_exists(path) && !browser->overwrite_armed) {
            browser->overwrite_armed = 1;
            snprintf(browser->message, sizeof(browser->message), "PRESS AGAIN TO OVERWRITE");
            return;
        }
        if (browser->mode == TS_BROWSER_SAVE_RECIPE) {
            ok = save_recipe_atomic(instrument, path, error, sizeof(error));
            snprintf(ui->status, sizeof(ui->status), ok ? "SAVED RECIPE %.108s" :
                     "SAVE FAILED: %.135s", ok ? path : error);
        } else {
            ok = export_wav_atomic(&instrument->current, path, error, sizeof(error));
            snprintf(ui->status, sizeof(ui->status), ok ? "EXPORTED CURRENT %.101s" :
                     "EXPORT FAILED: %.133s", ok ? path : error);
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
        size_t parent_frame;
        if (x < 0) x = 0;
        if (x >= TS_WAVE_W) x = TS_WAVE_W - 1;
        parent_frame = (size_t)x * instrument->parent.frames / TS_WAVE_W;
        if (parent_frame <= instrument->crop_first) return 0;
        if (parent_frame >= instrument->crop_last) return instrument->current.frames;
        return parent_frame - instrument->crop_first;
    }
    return ts_instrument_frame_from_view_x(instrument, x, TS_WAVE_W);
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("TapeSister", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              TS_UI_WIDTH * 2, TS_UI_HEIGHT * 2, SDL_WINDOW_RESIZABLE);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                            TS_UI_WIDTH, TS_UI_HEIGHT) : NULL;
    if (!window || !renderer || !texture) {
        fprintf(stderr, "Video setup failed: %s\n", SDL_GetError());
        running = 0;
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
    if (!device) snprintf(ui.status, sizeof(ui.status), "AUDIO UNAVAILABLE: %.130s", SDL_GetError());
    else SDL_PauseAudioDevice(device, 0);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    generate_parent(device, &audio, &ui, &instrument, 0);
    if (argc > 1) load_instrument(device, &audio, &ui, &instrument, argv[1]);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            else if (event.type == SDL_DROPFILE) {
                load_instrument(device, &audio, &ui, &instrument, event.drop.file);
                SDL_free(event.drop.file);
            } else if (event.type == SDL_TEXTINPUT && ui.browser.mode != TS_BROWSER_CLOSED) {
                if (ui.browser.filename_focus && ui.browser.mode != TS_BROWSER_LOAD_WAV)
                    ts_browser_append_filename(&ui.browser, event.text.text);
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod mod = SDL_GetModState();
                if (!((mod & KMOD_CTRL) && key == SDLK_p)) ui.commit_armed = 0;
                if (ui.browser.mode != TS_BROWSER_CLOSED) {
                    if (key == SDLK_ESCAPE) browser_cancel(&ui);
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
                        snprintf(ui.status, sizeof(ui.status), "CTRL+P AGAIN TO COMMIT CURRENT AS PARENT");
                    }
                } else if ((mod & KMOD_CTRL) && key == SDLK_s) {
                    browser_open(&ui, TS_BROWSER_SAVE_RECIPE);
                } else if ((mod & KMOD_CTRL) && key == SDLK_e) {
                    browser_open(&ui, TS_BROWSER_EXPORT_WAV);
                } else if ((mod & KMOD_CTRL) && key == SDLK_b) {
                    switch_audition_source(device, &audio, &ui, &instrument,
                        ui.audition_source == TS_AUDITION_CURRENT ?
                        TS_AUDITION_PARENT : TS_AUDITION_CURRENT, obtained.freq);
                } else if ((mod & KMOD_CTRL) && key == SDLK_z) {
                    history_move(device, &audio, &ui, &instrument, 0);
                } else if ((mod & KMOD_CTRL) && key == SDLK_y) {
                    history_move(device, &audio, &ui, &instrument, 1);
                } else if ((mod & KMOD_CTRL) && key == SDLK_a) {
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
                    size_t anchor = instrument.has_selection ?
                                    (instrument.selection_first + instrument.selection_last) / 2u :
                                    (instrument.view_first + instrument.view_last) / 2u;
                    snprintf(ui.status, sizeof(ui.status),
                             ts_instrument_zoom_view(&instrument, anchor, 0.5f, 0.5f) ?
                             "ZOOMED IN" : "ZOOM LIMIT");
                } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
                    size_t anchor = instrument.has_selection ?
                                    (instrument.selection_first + instrument.selection_last) / 2u :
                                    (instrument.view_first + instrument.view_last) / 2u;
                    snprintf(ui.status, sizeof(ui.status),
                             ts_instrument_zoom_view(&instrument, anchor, 0.5f, 2.0f) ?
                             "ZOOMED OUT" : "SHOWING ALL CURRENT");
                } else if (key == SDLK_0) {
                    ts_instrument_show_all(&instrument);
                    snprintf(ui.status, sizeof(ui.status), "SHOWING ALL CURRENT");
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    ptrdiff_t amount = (ptrdiff_t)((instrument.view_last - instrument.view_first) / 8u);
                    if (amount < 1) amount = 1;
                    if (key == SDLK_LEFT) amount = -amount;
                    snprintf(ui.status, sizeof(ui.status),
                             ts_instrument_pan_view(&instrument, amount) ?
                             "PANNED WAVEFORM VIEW" : "PAN LIMIT");
                } else if (key == SDLK_ESCAPE || key == SDLK_SPACE) {
                    ui.commit_armed = 0;
                    stop_all(device, &audio, &ui);
                } else {
                    int note = note_for_key(key);
                    if (note >= 0 && device)
                        begin_note(device, &audio, &ui, &instrument, note, obtained.freq);
                }
            } else if (event.type == SDL_KEYUP && ui.active_key >= 0 &&
                       ui.active_key == note_for_key(event.key.keysym.sym)) {
                release_note(device, &audio, &ui);
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
                    if ((mod & KMOD_SHIFT) || wheel_x != 0) {
                        size_t span = instrument.view_last - instrument.view_first;
                        ptrdiff_t step = (ptrdiff_t)(span / 8u);
                        ptrdiff_t amount;
                        if (step < 1) step = 1;
                        amount = -(ptrdiff_t)wheel_y * step + (ptrdiff_t)wheel_x * step;
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_instrument_pan_view(&instrument, amount) ?
                                 "MOUSE PANNED WAVEFORM VIEW" : "PAN LIMIT");
                    } else if (wheel_y != 0) {
                        size_t anchor = ts_instrument_frame_from_view_x(
                            &instrument, x - TS_WAVE_X, TS_WAVE_W);
                        float ratio = (float)(x - TS_WAVE_X) / (float)TS_WAVE_W;
                        float scale = powf(0.75f, (float)wheel_y);
                        snprintf(ui.status, sizeof(ui.status),
                                 ts_instrument_zoom_view(&instrument, anchor, ratio, scale) ?
                                 "MOUSE ZOOM - POINTER ANCHORED" : "ZOOM LIMIT");
                    }
                }
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
            } else if (event.type == SDL_MOUSEMOTION && ui.selecting) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                size_t at = selection_frame_from_x(&instrument, &ui, x - TS_WAVE_X);
                ts_instrument_set_selection_snapped(&instrument, ui.selection_anchor, at);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                logical_mouse(window, event.button.x, event.button.y, &x, &y);
                if (!(y >= 205 && y < 228 && x >= 247 && x < 325)) ui.commit_armed = 0;
                if (ui.browser.mode != TS_BROWSER_CLOSED) {
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
                } else if (y >= 4 && y < 28 && x >= 447 && x < 529) {
                    browser_open(&ui, TS_BROWSER_SAVE_RECIPE);
                } else if (y >= 4 && y < 28 && x >= 535 && x < 630) {
                    browser_open(&ui, TS_BROWSER_EXPORT_WAV);
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    ui.selection_anchor = ts_sample_nearest_zero_crossing(
                        &instrument.current, selection_frame_from_x(
                            &instrument, &ui, x - TS_WAVE_X));
                    ts_instrument_set_selection_snapped(
                        &instrument, ui.selection_anchor, ui.selection_anchor);
                    ui.selecting = 1;
                    snprintf(ui.status, sizeof(ui.status), "SELECTING CURRENT");
                } else if (y >= 205 && y < 228 && x >= 10 && x < 80) {
                    ui.commit_armed = 0;
                    browser_open(&ui, TS_BROWSER_LOAD_WAV);
                } else if (y >= 205 && y < 228 && x >= 85 && x < 167) {
                    ui.commit_armed = 0;
                    generate_parent(device, &audio, &ui, &instrument, 1);
                } else if (y >= 205 && y < 228 && x >= 172 && x < 242) {
                    ui.commit_armed = 0;
                    reseed_parent(device, &audio, &ui, &instrument);
                } else if (y >= 205 && y < 228 && x >= 247 && x < 325) {
                    if (ui.commit_armed) commit_current(device, &audio, &ui, &instrument);
                    else {
                        ui.commit_armed = 1;
                        snprintf(ui.status, sizeof(ui.status), "CLICK COMMIT AGAIN TO MAKE CURRENT THE PARENT");
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
                    stop_all(device, &audio, &ui);
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
                } else if (y >= 233 && y < 256 && x >= 345 && x < 398) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_EDIT;
                    snprintf(ui.status, sizeof(ui.status), "SAMPLE EDITING PAGE");
                } else if (y >= 233 && y < 256 && x >= 402 && x < 455) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_NOISE;
                    snprintf(ui.status, sizeof(ui.status), "NOISE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 459 && x < 512) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_DELAY;
                    snprintf(ui.status, sizeof(ui.status), "DELAY PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 516 && x < 569) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_SPACE;
                    snprintf(ui.status, sizeof(ui.status), "SPACE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 573 && x < 630) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_LOOP;
                    snprintf(ui.status, sizeof(ui.status), "FORWARD LOOP EDITING PAGE");
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
                    } else if (ui.fx_page == TS_FX_NOISE) {
                        label = "NOISE";
                        if (x >= 10 && x < 104) { process.noise_enabled = !process.noise_enabled; changed = 1; }
                        else if (x >= 118 && x < 298) {
                            process.noise_amount = (float)(x - 118) / 180.0f; changed = 1;
                        } else if (x >= 312 && x < 462) {
                            process.noise_color = (TsNoiseColor)((process.noise_color + 1) % TS_NOISE_COLOR_COUNT);
                            changed = 1;
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
                        if (x >= 10 && x < 94)
                            set_loop(device, &audio, &ui, &instrument);
                        else if (x >= 99 && x < 183)
                            clear_loop(device, &audio, &ui, &instrument);
                        else if (x >= 188 && x < 282)
                            begin_audition(device, &audio, &ui, &instrument,
                                           TS_AUDITION_LOOP, 1.0, obtained.freq);
                        else if (x >= 297 && x < 577)
                            set_loop_crossfade(device, &audio, &ui, &instrument,
                                               (float)(x - 297) / 280.0f * 50.0f);
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
                } else if (y >= 289 && y < 312 && x >= 245 && x < 297) {
                    crop_selection(device, &audio, &ui, &instrument);
                } else if (y >= 289 && y < 312 && x >= 302 && x < 376) {
                    snprintf(ui.status, sizeof(ui.status), ts_instrument_zoom_selection(&instrument) ?
                             "ZOOMED TO SELECTION" : "SELECT A RANGE FIRST");
                } else if (y >= 289 && y < 312 && x >= 381 && x < 455) {
                    ts_instrument_show_all(&instrument);
                    snprintf(ui.status, sizeof(ui.status), "SHOWING ALL CURRENT");
                } else if (y >= 289 && y < 312 && x >= 460 && x < 516) {
                    history_move(device, &audio, &ui, &instrument, 0);
                } else if (y >= 289 && y < 312 && x >= 521 && x < 583) {
                    history_move(device, &audio, &ui, &instrument, 1);
                } else {
                    int note = ts_ui_key_from_point(x, y);
                    if (note >= 0 && device)
                        begin_note(device, &audio, &ui, &instrument, note, obtained.freq);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                ui.browser.dragging_scrollbar = 0;
                if (ui.selecting) {
                    ui.selecting = 0;
                    snprintf(ui.status, sizeof(ui.status), "SELECTED %zu FRAMES",
                             instrument.selection_last - instrument.selection_first);
                }
                if (ui.active_key >= 0) release_note(device, &audio, &ui);
            }
        }

        if (device) SDL_LockAudioDevice(device);
        ui.playback_active = audio.playing;
        ui.playhead_source = audio.source;
        ui.playhead_frame = audio.position > 0.0 ? (size_t)audio.position : 0;
        ui.playhead_frames = audio.sample ? audio.sample->frames : 0;
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
