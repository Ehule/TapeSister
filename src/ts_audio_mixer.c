#include "tapesister/audio_mixer.h"

#include <math.h>
#include <string.h>

static TsStereoFrame add_frame(TsStereoFrame a, TsStereoFrame b)
{
    TsStereoFrame result = {a.l + b.l, a.r + b.r};
    return ts_stereo_frame_sanitize(result);
}

static TsStereoFrame scale_frame(TsStereoFrame value, float gain)
{
    TsStereoFrame result;
    if (!isfinite(gain)) gain = 0.0f;
    result.l = value.l * gain;
    result.r = value.r * gain;
    return ts_stereo_frame_sanitize(result);
}

static TsStereoFrame clamp_frame(TsStereoFrame value)
{
    value = ts_stereo_frame_sanitize(value);
    if (value.l > 1.0f) value.l = 1.0f;
    if (value.l < -1.0f) value.l = -1.0f;
    if (value.r > 1.0f) value.r = 1.0f;
    if (value.r < -1.0f) value.r = -1.0f;
    return value;
}

void ts_audio_buses_clear(TsAudioBuses *buses)
{
    if (buses != NULL) memset(buses, 0, sizeof(*buses));
}

void ts_audio_mixer_init(TsAudioMixer *mixer)
{
    if (mixer == NULL) return;
    memset(mixer, 0, sizeof(*mixer));
    mixer->program_gain = 0.8f;
    mixer->master_gain = 1.0f;
    mixer->monitor_enabled = 1;
}

TsStereoFrame ts_audio_normalize_linked(TsStereoFrame sum, int active_voices)
{
    if (active_voices <= 0) return (TsStereoFrame){0.0f, 0.0f};
    return scale_frame(sum, 1.0f / sqrtf((float)active_voices));
}

TsStereoFrame ts_audio_mixer_render(TsAudioMixer *mixer,
                                    const TsAudioBuses *sources)
{
    TsAudioBuses buses;
    TsStereoFrame program;
    TsStereoFrame output;
    if (mixer == NULL) return (TsStereoFrame){0.0f, 0.0f};
    if (sources == NULL) ts_audio_buses_clear(&buses);
    else buses = *sources;

    /* Preserve the legacy order: preview + note/performance + FM, clamp that
       program path, apply its 0.8 gain, then add monitor and reference. */
    program = add_frame(buses.legacy_preview, buses.tile_performance);
    program = add_frame(program, buses.fm);
    program = clamp_frame(program);
    output = scale_frame(program, mixer->program_gain);
    if (mixer->monitor_enabled)
        output = add_frame(output, buses.monitor);
    output = add_frame(output, buses.reference);
    output = clamp_frame(scale_frame(output, mixer->master_gain));

    buses.external = ts_stereo_frame_sanitize(buses.external);
    buses.monitor = ts_stereo_frame_sanitize(buses.monitor);
    buses.capture = ts_stereo_frame_sanitize(buses.capture);
    buses.program = program;
    buses.output = output;
    mixer->buses = buses;
    return output;
}
