#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const TsSample *sample;
    double position;
    double step;
    size_t range_end;
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

static void begin_range(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                        size_t first, size_t last, double pitch, int output_rate,
                        const char *label)
{
    if (!device || output_rate <= 0) {
        snprintf(ui->status, sizeof(ui->status), "AUDIO UNAVAILABLE");
        return;
    }
    if (!audio->sample || !audio->sample->data || first >= last || last > audio->sample->frames)
        return;
    if (device) SDL_LockAudioDevice(device);
    audio->position = (double)first;
    audio->range_end = last;
    audio->step = ((double)audio->sample->sample_rate / output_rate) * pitch;
    audio->playing = 1;
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "PLAYING %s", label);
}

static void begin_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                       int note, int output_rate)
{
    if (!audio->sample) return;
    begin_range(device, audio, ui, 0, audio->sample->frames, pow(2.0, note / 12.0),
                output_rate, "CURRENT");
    ui->active_key = note;
}

static void stop_all(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
{
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    if (device) SDL_UnlockAudioDevice(device);
    ui->active_key = -1;
    snprintf(ui->status, sizeof(ui->status), "STOPPED");
}

static void lock_edit(SDL_AudioDeviceID device, AudioState *audio)
{
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
}

static void unlock_edit(SDL_AudioDeviceID device, AudioState *audio, TsInstrument *instrument)
{
    audio->sample = &instrument->current;
    if (device) SDL_UnlockAudioDevice(device);
}

static int load_instrument(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                           TsInstrument *instrument, const char *path)
{
    char error[160];
    int ok;
    lock_edit(device, audio);
    ok = ts_instrument_load_wav(instrument, path, error, sizeof(error));
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
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
    unlock_edit(device, audio, instrument);
    if (ok) snprintf(ui->status, sizeof(ui->status), "%s", redo ? "REDO" : "UNDO");
    else snprintf(ui->status, sizeof(ui->status), "%.150s", error);
}

static int save_recipe(const TsInstrument *instrument, const char *path)
{
    char error[160];
    return ts_instrument_save_recipe(instrument, path, error, sizeof(error));
}

static void path_text(TsUiState *ui, const char *input)
{
    size_t used = strlen(ui->path);
    size_t available = sizeof(ui->path) - used - 1;
    strncat(ui->path, input, available);
}

static void logical_mouse(SDL_Window *window, int raw_x, int raw_y, int *x, int *y)
{
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    *x = raw_x * TS_UI_WIDTH / ww;
    *y = raw_y * TS_UI_HEIGHT / wh;
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
            } else if (event.type == SDL_TEXTINPUT && ui.path_entry) {
                path_text(&ui, event.text.text);
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod mod = SDL_GetModState();
                if (!((mod & KMOD_CTRL) && key == SDLK_p)) ui.commit_armed = 0;
                if (ui.path_entry) {
                    if (key == SDLK_ESCAPE) {
                        ui.path_entry = 0; SDL_StopTextInput();
                        snprintf(ui.status, sizeof(ui.status), "LOAD CANCELLED");
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        if (ui.path[0] && load_instrument(device, &audio, &ui, &instrument, ui.path)) {
                            ui.path_entry = 0; SDL_StopTextInput();
                        }
                    } else if (key == SDLK_BACKSPACE && ui.path[0]) {
                        ui.path[strlen(ui.path) - 1] = '\0';
                    }
                } else if ((mod & KMOD_CTRL) && key == SDLK_o) {
                    ui.path_entry = 1; ui.path[0] = '\0'; SDL_StartTextInput();
                    snprintf(ui.status, sizeof(ui.status), "ENTER A WAV PATH");
                } else if ((mod & KMOD_CTRL) && key == SDLK_p) {
                    if (ui.commit_armed) commit_current(device, &audio, &ui, &instrument);
                    else {
                        ui.commit_armed = 1;
                        snprintf(ui.status, sizeof(ui.status), "CTRL+P AGAIN TO COMMIT CURRENT AS PARENT");
                    }
                } else if ((mod & KMOD_CTRL) && key == SDLK_s) {
                    snprintf(ui.status, sizeof(ui.status), save_recipe(&instrument, "tapesister-recipe.tsr") ?
                             "SAVED TAPESISTER-RECIPE.TSR" : "RECIPE SAVE FAILED");
                } else if ((mod & KMOD_CTRL) && key == SDLK_e) {
                    char error[120];
                    snprintf(ui.status, sizeof(ui.status),
                             ts_sample_save_wav16(&instrument.current, "tapesister-export.wav", error, sizeof(error)) ?
                             "EXPORTED CURRENT TO TAPESISTER-EXPORT.WAV" : "EXPORT FAILED: %.100s", error);
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
                    if (note >= 0 && device) begin_note(device, &audio, &ui, note, obtained.freq);
                }
            } else if (event.type == SDL_KEYUP && ui.active_key == note_for_key(event.key.keysym.sym)) {
                ui.active_key = -1;
            } else if (event.type == SDL_MOUSEWHEEL && !ui.path_entry) {
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
            } else if (event.type == SDL_MOUSEMOTION && ui.selecting) {
                int x, y;
                logical_mouse(window, event.motion.x, event.motion.y, &x, &y);
                (void)y;
                size_t at = ts_instrument_frame_from_view_x(&instrument, x - TS_WAVE_X, TS_WAVE_W);
                ts_instrument_set_selection(&instrument, ui.selection_anchor, at);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                logical_mouse(window, event.button.x, event.button.y, &x, &y);
                if (!(y >= 205 && y < 228 && x >= 247 && x < 325)) ui.commit_armed = 0;
                if (ui.path_entry) {
                    /* The path editor owns the modal surface. */
                } else if (y >= 4 && y < 28 && x >= 447 && x < 529) {
                    snprintf(ui.status, sizeof(ui.status), save_recipe(&instrument, "tapesister-recipe.tsr") ?
                             "SAVED TAPESISTER-RECIPE.TSR" : "RECIPE SAVE FAILED");
                } else if (y >= 4 && y < 28 && x >= 535 && x < 630) {
                    char error[120];
                    int ok = ts_sample_save_wav16(&instrument.current, "tapesister-export.wav", error, sizeof(error));
                    snprintf(ui.status, sizeof(ui.status), ok ? "EXPORTED CURRENT TO TAPESISTER-EXPORT.WAV" :
                             "EXPORT FAILED: %.100s", error);
                } else if (x >= TS_WAVE_X && x < TS_WAVE_X + TS_WAVE_W &&
                           y >= TS_WAVE_Y && y < TS_WAVE_Y + TS_WAVE_H) {
                    ui.selection_anchor = ts_instrument_frame_from_view_x(&instrument, x - TS_WAVE_X, TS_WAVE_W);
                    ts_instrument_set_selection(&instrument, ui.selection_anchor, ui.selection_anchor);
                    ui.selecting = 1;
                    snprintf(ui.status, sizeof(ui.status), "SELECTING CURRENT");
                } else if (y >= 205 && y < 228 && x >= 10 && x < 80) {
                    ui.commit_armed = 0;
                    ui.path_entry = 1; ui.path[0] = '\0'; SDL_StartTextInput();
                    snprintf(ui.status, sizeof(ui.status), "ENTER A WAV PATH");
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
                } else if (y >= 233 && y < 256 && x >= 345 && x < 410) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_EDIT;
                    snprintf(ui.status, sizeof(ui.status), "SAMPLE EDITING PAGE");
                } else if (y >= 233 && y < 256 && x >= 415 && x < 480) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_NOISE;
                    snprintf(ui.status, sizeof(ui.status), "NOISE PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 485 && x < 550) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_DELAY;
                    snprintf(ui.status, sizeof(ui.status), "DELAY PROCESSING PAGE");
                } else if (y >= 233 && y < 256 && x >= 555 && x < 630) {
                    ui.commit_armed = 0; ui.fx_page = TS_FX_SPACE;
                    snprintf(ui.status, sizeof(ui.status), "SPACE PROCESSING PAGE");
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
                    } else {
                        label = "SPACE";
                        if (x >= 10 && x < 104) { process.reverb_enabled = !process.reverb_enabled; changed = 1; }
                        else if (x >= 118 && x < 238) {
                            process.reverb_decay = (float)(x - 118) / 120.0f * 0.9f; changed = 1;
                        } else if (x >= 250 && x < 370) {
                            process.reverb_damping = (float)(x - 250) / 120.0f; changed = 1;
                        } else if (x >= 382 && x < 502) {
                            process.reverb_mix = (float)(x - 382) / 120.0f; changed = 1;
                        }
                    }
                    if (changed) apply_process(device, &audio, &ui, &instrument, process, label);
                } else if (y >= 289 && y < 312 && x >= 10 && x < 80) {
                    begin_range(device, &audio, &ui, 0, instrument.current.frames, 1.0,
                                obtained.freq, "ALL");
                } else if (y >= 289 && y < 312 && x >= 85 && x < 157) {
                    if (instrument.has_selection)
                        begin_range(device, &audio, &ui, instrument.selection_first,
                                    instrument.selection_last, 1.0, obtained.freq, "SELECTION");
                    else snprintf(ui.status, sizeof(ui.status), "SELECT A RANGE FIRST");
                } else if (y >= 289 && y < 312 && x >= 162 && x < 240) {
                    begin_range(device, &audio, &ui, instrument.view_first, instrument.view_last,
                                1.0, obtained.freq, "DISPLAYED RANGE");
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
                    if (note >= 0 && device) begin_note(device, &audio, &ui, note, obtained.freq);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                if (ui.selecting) {
                    ui.selecting = 0;
                    snprintf(ui.status, sizeof(ui.status), "SELECTED %zu FRAMES",
                             instrument.selection_last - instrument.selection_first);
                }
                ui.active_key = -1;
            }
        }

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
