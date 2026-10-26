#pragma once
/* Host-owned pattern instances reuse FT2's channel effects and mixer, under
   the existing audio callback exclusion. No UI/transport work is dispatched. */
#include "tapesister/pattern_card.h"
#include "ft2_replayer.h"
#include "ft2_audio.h"
#include "ft2_fasttracks_core.h"
typedef struct {
    const TsPatternCard *definition;
    instr_t *instruments[132];
} thPatternSource;
typedef struct {
    channel_t channels[MAX_CHANNELS];
    voice_t voices[MAX_CHANNELS*2];
    TsTrackerLane lanes[MAX_CHANNELS];
    int rows[MAX_CHANNELS],accumulator[MAX_CHANNELS],direction[MAX_CHANNELS];
    uint16_t tpl[MAX_CHANNELS];
    bool started[MAX_CHANNELS],enabled,clutch[MAX_CHANNELS],transmission;
    uint32_t random;
    unsigned tick,total_ticks;
    int global_volume,released;
} thPatternState;
extern bool tapeheadPatternContext;
void tapeheadPatternStep(thPatternState *,const thPatternSource *);
void tapeheadPatternVoices(thPatternState *);
void tapeheadPatternMix(thPatternState *,float *,float *,unsigned,int);
void ts_tapehead_pattern_install(void);
