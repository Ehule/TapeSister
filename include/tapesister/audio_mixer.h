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
    TsStereoFrame reference;
    TsStereoFrame monitor;
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
TsStereoFrame ts_audio_mixer_render(TsAudioMixer *mixer,
                                    const TsAudioBuses *sources);

#endif
