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
    int playing;
} AudioState;

static void audio_callback(void *userdata, Uint8 *stream, int bytes)
{
    AudioState *audio = (AudioState *)userdata;
    float *out = (float *)stream;
    int values = bytes / (int)sizeof(float);
    for (int i = 0; i < values; i += 2) {
        float value = 0.0f;
        if (audio->playing && audio->sample && audio->sample->data && audio->sample->frames > 1) {
            size_t at = (size_t)audio->position;
            if (at + 1 < audio->sample->frames) {
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

static void begin_note(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, int note, int output_rate)
{
    if (!audio->sample || !audio->sample->data) return;
    if (device) SDL_LockAudioDevice(device);
    audio->position = 0.0;
    audio->step = ((double)audio->sample->sample_rate / output_rate) * pow(2.0, note / 12.0);
    audio->playing = 1;
    if (device) SDL_UnlockAudioDevice(device);
    ui->active_key = note;
    snprintf(ui->status, sizeof(ui->status), "PLAYING NOTE %d", note);
}

static void stop_all(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui)
{
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    if (device) SDL_UnlockAudioDevice(device);
    ui->active_key = -1;
    snprintf(ui->status, sizeof(ui->status), "STOPPED");
}

static int load_sample(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, TsSample *sample, const char *path)
{
    TsSample incoming;
    char error[160];
    ts_sample_init(&incoming);
    if (!ts_sample_load_wav(&incoming, path, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "LOAD FAILED: %.135s", error);
        return 0;
    }
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    ts_sample_free(sample);
    *sample = incoming;
    audio->sample = sample;
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "LOADED %.120s", sample->name);
    return 1;
}

static void generate_sample(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui, TsSample *sample)
{
    TsSample incoming;
    char error[160];
    ts_sample_init(&incoming);
    if (!ts_sample_generate(&incoming, &ui->recipe, error, sizeof(error))) {
        snprintf(ui->status, sizeof(ui->status), "GENERATE FAILED: %.130s", error);
        return;
    }
    if (device) SDL_LockAudioDevice(device);
    audio->playing = 0;
    ts_sample_free(sample);
    *sample = incoming;
    audio->sample = sample;
    if (device) SDL_UnlockAudioDevice(device);
    snprintf(ui->status, sizeof(ui->status), "GENERATED %08X", ui->recipe.seed);
}

static int save_recipe(const TsUiState *ui, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "TAPESISTER_RECIPE 1\nseed=%u\nbody=%.9g\nedge=%.9g\ndrift=%.9g\nseconds=%.9g\nfrequency=%.9g\n",
            ui->recipe.seed, ui->recipe.body, ui->recipe.edge, ui->recipe.drift,
            ui->recipe.seconds, ui->recipe.frequency);
    return fclose(f) == 0;
}

static void path_text(TsUiState *ui, const char *input)
{
    size_t used = strlen(ui->path);
    size_t available = sizeof(ui->path) - used - 1;
    strncat(ui->path, input, available);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec desired, obtained;
    TsSample sample;
    TsUiState ui;
    TsFramebuffer framebuffer;
    AudioState audio = {0};
    int running = 1;

    ts_sample_init(&sample);
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
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = audio_callback;
    desired.userdata = &audio;
    device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (!device) {
        snprintf(ui.status, sizeof(ui.status), "AUDIO UNAVAILABLE: %.130s", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(device, 0);
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    generate_sample(device, &audio, &ui, &sample);
    if (argc > 1) load_sample(device, &audio, &ui, &sample, argv[1]);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            else if (event.type == SDL_DROPFILE) {
                load_sample(device, &audio, &ui, &sample, event.drop.file);
                SDL_free(event.drop.file);
            } else if (event.type == SDL_TEXTINPUT && ui.path_entry) {
                path_text(&ui, event.text.text);
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod mod = SDL_GetModState();
                if (ui.path_entry) {
                    if (key == SDLK_ESCAPE) {
                        ui.path_entry = 0; SDL_StopTextInput();
                        snprintf(ui.status, sizeof(ui.status), "LOAD CANCELLED");
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        if (ui.path[0] && load_sample(device, &audio, &ui, &sample, ui.path)) {
                            ui.path_entry = 0; SDL_StopTextInput();
                        }
                    } else if (key == SDLK_BACKSPACE && ui.path[0]) {
                        ui.path[strlen(ui.path) - 1] = '\0';
                    }
                } else if ((mod & KMOD_CTRL) && key == SDLK_o) {
                    ui.path_entry = 1; ui.path[0] = '\0'; SDL_StartTextInput();
                    snprintf(ui.status, sizeof(ui.status), "ENTER A WAV PATH");
                } else if ((mod & KMOD_CTRL) && key == SDLK_s) {
                    snprintf(ui.status, sizeof(ui.status), save_recipe(&ui, "tapesister-recipe.tsr") ?
                             "SAVED TAPESISTER-RECIPE.TSR" : "RECIPE SAVE FAILED");
                } else if ((mod & KMOD_CTRL) && key == SDLK_e) {
                    char error[120];
                    snprintf(ui.status, sizeof(ui.status), ts_sample_save_wav16(&sample, "tapesister-export.wav", error, sizeof(error)) ?
                             "EXPORTED TAPESISTER-EXPORT.WAV" : "EXPORT FAILED: %.100s", error);
                } else if (key == SDLK_ESCAPE || key == SDLK_SPACE) {
                    stop_all(device, &audio, &ui);
                } else {
                    int note = note_for_key(key);
                    if (note >= 0 && device) begin_note(device, &audio, &ui, note, obtained.freq);
                }
            } else if (event.type == SDL_KEYUP && ui.active_key == note_for_key(event.key.keysym.sym)) {
                ui.active_key = -1;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int ww, wh;
                SDL_GetWindowSize(window, &ww, &wh);
                int x = event.button.x * TS_UI_WIDTH / ww;
                int y = event.button.y * TS_UI_HEIGHT / wh;
                if (ui.path_entry) {
                    /* The path editor owns the modal surface. */
                } else if (y >= 4 && y < 28 && x >= 447 && x < 529) {
                    snprintf(ui.status, sizeof(ui.status), save_recipe(&ui, "tapesister-recipe.tsr") ?
                             "SAVED TAPESISTER-RECIPE.TSR" : "RECIPE SAVE FAILED");
                } else if (y >= 4 && y < 28 && x >= 535 && x < 630) {
                    char error[120];
                    int ok = ts_sample_save_wav16(&sample, "tapesister-export.wav", error, sizeof(error));
                    snprintf(ui.status, sizeof(ui.status), ok ? "EXPORTED TAPESISTER-EXPORT.WAV" : "EXPORT FAILED: %.100s", error);
                } else if (y >= 254 && y < 278 && x >= 10 && x < 96) {
                    ui.path_entry = 1; ui.path[0] = '\0'; SDL_StartTextInput();
                    snprintf(ui.status, sizeof(ui.status), "ENTER A WAV PATH");
                } else if (y >= 254 && y < 278 && x >= 102 && x < 198) {
                    generate_sample(device, &audio, &ui, &sample);
                } else if (y >= 254 && y < 278 && x >= 204 && x < 286) {
                    ui.recipe.seed = ui.recipe.seed * 1664525u + 1013904223u;
                    generate_sample(device, &audio, &ui, &sample);
                } else if (y >= 254 && y < 278 && x >= 544 && x < 630) {
                    stop_all(device, &audio, &ui);
                } else if (y >= 265 && y < 278 && x >= 304 && x < 524) {
                    float *control = x < 376 ? &ui.recipe.body : x < 455 ? &ui.recipe.edge : &ui.recipe.drift;
                    int start = x < 376 ? 304 : x < 455 ? 383 : 462;
                    *control = (float)(x - start) / 62.0f;
                    if (*control < 0) *control = 0;
                    if (*control > 1) *control = 1;
                    generate_sample(device, &audio, &ui, &sample);
                } else {
                    int note = ts_ui_key_from_point(x, y);
                    if (note >= 0 && device) begin_note(device, &audio, &ui, note, obtained.freq);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                ui.active_key = -1;
            }
        }

        ts_ui_render(&framebuffer, &ui, &sample);
        SDL_UpdateTexture(texture, NULL, framebuffer.pixels, TS_UI_WIDTH * (int)sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    if (device) SDL_CloseAudioDevice(device);
    ts_sample_free(&sample);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
