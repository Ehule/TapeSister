#ifdef TAPEHEAD_EMBEDDED
#include "ts_pattern_instance.h"
void tapeheadPatternVoices(thPatternState *state)
{
    voice_t *old=voice;voice=state->voices;
    resetRampVolumes();updateVoices();voice=old;
}
/* Scratch output belongs to the caller. The host tile mixer skips all ordinary
   track routes in this context; routing happens once on the whole Mosaic card. */
void tapeheadPatternMix(thPatternState *state,float *left,float *right,unsigned frames,int channels)
{
    float *old_left=audio.fMixBufferL,*old_right=audio.fMixBufferR;
    audio.fMixBufferL=left;audio.fMixBufferR=right;tapeheadPatternContext=true;
    for(int i=0;i<channels;++i)mixVoicePair(&state->voices[i],&state->voices[MAX_CHANNELS+i],0,frames,3*NUM_INTERPOLATORS*2);
    tapeheadPatternContext=false;audio.fMixBufferL=old_left;audio.fMixBufferR=old_right;
}
#endif
