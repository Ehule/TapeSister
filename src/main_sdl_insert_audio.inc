/* Uses the existing output endpoint and lifecycle. Stereo DSP/master stays on
   1/2; spare native channels are written by the same callback and clock. */

static unsigned ts_audio_output_channels_for(const char *device)
{
    SDL_AudioSpec spec;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    if (!device) {
        SDL_zero(spec);
        if (SDL_GetDefaultAudioInfo(NULL, &spec, 0) == 0) return spec.channels;
    }
#endif
#if SDL_VERSION_ATLEAST(2, 0, 16)
    int count = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < count; ++i) {
        const char *name = SDL_GetAudioDeviceName(i, 0);
        if (device && (!name || strcmp(name, device))) continue;
        SDL_zero(spec);
        if (SDL_GetAudioDeviceSpec(i, 0, &spec) == 0) return spec.channels;
        if (!device) break;
    }
#else
    (void)device; (void)spec;
#endif
    return 0;
}

static unsigned ts_audio_insert_offered_channels(void)
{
    if (ts_real_output && ts_output_endpoint.channels)
        return ts_output_endpoint.channels > TS_INSERT_MAX_CHANNELS ?
               TS_INSERT_MAX_CHANNELS : ts_output_endpoint.channels;
    unsigned n=ts_audio_output_channels_for(ts_configured_output_name());
    return n>TS_INSERT_MAX_CHANNELS?TS_INSERT_MAX_CHANNELS:n;
}

static SDL_AudioDeviceID ts_audio_open_output_native(const char *device,
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed)
{
    SDL_AudioSpec request = *desired;
    {
        unsigned channels = ts_audio_output_channels_for(device);
        if (channels >= 2 && channels <= 255) {
            request.channels = (Uint8)channels;
            /* Without this flag SDL may silently downmix SEND into Master. */
            allowed |= SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
        }
    }
    allowed |= SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
    SDL_AudioDeviceID result = SDL_OpenAudioDevice(device, 0, &request, obtained, allowed);
    if (result && (obtained->channels < 1 || obtained->freq <= 0 || obtained->samples == 0 ||
                   obtained->format != AUDIO_F32SYS)) {
        SDL_CloseAudioDevice(result);
        SDL_SetError("Output must expose float audio at a valid rate and buffer size");
        return 0;
    }
    return result;
}

static void ts_audio_commit_output_layout(AudioState *audio, const SDL_AudioSpec *spec)
{
    if (!audio) return;
    audio->output_device_channels = spec->channels;
    audio->sister.insert.output_channels = spec->channels;
    ts_input_monitor_discard(&audio->sister.insert.return_monitor);
}

#include "main_sdl_insert_devices.inc"
