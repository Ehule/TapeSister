#ifndef TAPESISTER_AUDIO_MIXER_H
#define TAPESISTER_AUDIO_MIXER_H

#include "tapesister/sample.h"

/* Every callback contribution has an explicit stereo route. `external` is the
   selected input frame before monitor policy; `monitor` is the audible input
   route. `capture` is a tap and is never added to final output implicitly. */
typedef struct {
    TsStereoFrame legacy_preview;
    TsStereoFrame tile_performance;
    TsStereoFrame fm;
    TsStereoFrame external;
    TsStereoFrame tapehead;
    TsStereoFrame reference;
    TsStereoFrame monitor;
    TsStereoFrame sister;
    /* Explicit completed Sister/ordinary post-effects contribution. */
    TsStereoFrame post_fx;
    TsStereoFrame capture;
    TsStereoFrame program;
    TsStereoFrame output;
} TsAudioBuses;

typedef struct {
    float program_gain;
    float master_gain;
    int monitor_enabled;
    TsAudioBuses buses;
} TsAudioMixer;

void ts_audio_buses_clear(TsAudioBuses *buses);
void ts_audio_mixer_init(TsAudioMixer *mixer);
TsStereoFrame ts_audio_normalize_linked(TsStereoFrame sum, int active_voices);
void ts_audio_buses_apply_source_dry(TsAudioBuses *buses, float gain,
                                     int preview_routed, int tiles_routed,
                                     int fm_routed, int external_routed,
                                     int tapehead_routed);
void ts_audio_buses_apply_source_insert(TsAudioBuses *buses,
                                        float preview_insert,
                                        float tiles_insert,
                                        float fm_insert,
                                        float external_insert,
                                        float tapehead_insert);
/* An active Sister Machine owns the complete musical input path. Sources not
   selected for Sister are silent rather than leaking around the insert. */
void ts_audio_buses_apply_sister_ownership(TsAudioBuses *buses,
                                           int sister_active);
TsStereoFrame ts_audio_mixer_render(TsAudioMixer *mixer,
                                    const TsAudioBuses *sources);
TsStereoFrame ts_audio_mixer_render_unclamped(TsAudioMixer *mixer,
                                              const TsAudioBuses *sources);

#endif
