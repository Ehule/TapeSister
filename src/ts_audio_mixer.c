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

void ts_audio_buses_apply_source_dry(TsAudioBuses *buses, float gain,
                                     int preview_routed, int tiles_routed,
                                     int fm_routed, int external_routed,
                                     int tapehead_routed)
{
    if (buses == NULL) return;
    if (!isfinite(gain)) gain = 0.0f;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    if (preview_routed) buses->legacy_preview = scale_frame(buses->legacy_preview, gain);
    if (tiles_routed) buses->tile_performance = scale_frame(buses->tile_performance, gain);
    if (fm_routed) buses->fm = scale_frame(buses->fm, gain);
    if (external_routed) buses->monitor = scale_frame(buses->monitor, gain);
    if (tapehead_routed) buses->tapehead = scale_frame(buses->tapehead, gain);
}

static float direct_gain_for_insert(float insert)
{
    if (!isfinite(insert)) insert = 0.0f;
    if (insert < 0.0f) insert = 0.0f;
    if (insert > 1.0f) insert = 1.0f;
    return 1.0f - insert;
}

void ts_audio_buses_apply_source_insert(TsAudioBuses *buses,
                                        float preview_insert,
                                        float tiles_insert,
                                        float fm_insert,
                                        float external_insert,
                                        float tapehead_insert)
{
    if (buses == NULL) return;
    buses->legacy_preview = scale_frame(
        buses->legacy_preview, direct_gain_for_insert(preview_insert));
    buses->tile_performance = scale_frame(
        buses->tile_performance, direct_gain_for_insert(tiles_insert));
    buses->fm = scale_frame(buses->fm, direct_gain_for_insert(fm_insert));
    buses->monitor = scale_frame(
        buses->monitor, direct_gain_for_insert(external_insert));
    buses->tapehead = scale_frame(
        buses->tapehead, direct_gain_for_insert(tapehead_insert));
}

void ts_audio_buses_apply_sister_ownership(TsAudioBuses *buses,
                                           int sister_active)
{
    if (!sister_active) return;
    ts_audio_buses_apply_source_insert(buses, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}

TsStereoFrame ts_audio_mixer_render_unclamped(TsAudioMixer *mixer,
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
    program = add_frame(program, buses.tapehead);
    program = clamp_frame(program);
    output = scale_frame(program, mixer->program_gain);
    output = add_frame(output, buses.sister);
    output = add_frame(output, buses.post_fx);
    if (mixer->monitor_enabled)
        output = add_frame(output, buses.monitor);
    output = add_frame(output, buses.reference);
    output = scale_frame(output, mixer->master_gain);

    buses.external = ts_stereo_frame_sanitize(buses.external);
    buses.tapehead = ts_stereo_frame_sanitize(buses.tapehead);
    buses.monitor = ts_stereo_frame_sanitize(buses.monitor);
    buses.sister = ts_stereo_frame_sanitize(buses.sister);
    buses.post_fx = ts_stereo_frame_sanitize(buses.post_fx);
    buses.capture = ts_stereo_frame_sanitize(buses.capture);
    buses.program = program;
    buses.output = output;
    mixer->buses = buses;
    return output;
}

TsStereoFrame ts_audio_mixer_render(TsAudioMixer *mixer,
                                    const TsAudioBuses *sources)
{
    TsStereoFrame output = ts_audio_mixer_render_unclamped(mixer, sources);
    output = clamp_frame(output);
    if (mixer != NULL) mixer->buses.output = output;
    return output;
}
