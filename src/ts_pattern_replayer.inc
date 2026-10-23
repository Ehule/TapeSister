#ifdef TAPEHEAD_EMBEDDED
#include "ts_pattern_instance.h"
bool tapeheadPatternContext;
static void patternClockCommand(thPatternState *s,const TsPatternCard *p,int lane,int value,int length)
{
    TsTrackerLane *l=&s->lanes[lane];
    if(value<16)l->ratio=value>=7?value+1:value;
    else if(value==0x10 || value==0x11)s->clutch[lane]=value==0x11;
    else if(value==0x12)l->mode=TS_TRACKER_STANDARD;
    else if(value==0x13 || value==0x15 || value==0x16)l->mode=TS_TRACKER_PATTERN;
    else if(value==0x17 || value==0x18)l->ratio=7;
    else if(value==0x20 || value==0x21)s->enabled=value==0x21;
    else if(value==0x25 || value==0x26)s->transmission=value==0x26;
    if(value==0x14 || value==0x18){s->rows[lane]=(s->tick/p->score.ticks_per_line)%length;s->accumulator[lane]=0;}
    if(value>=0x22)for(int i=0;i<p->score.channel_count;++i){
        bool selected=(p->selected>>i)&1;
        if(value==0x24 || value==0x27 || value==0x2a || value==0x2b)s->lanes[i].ratio=7;
        if(!selected)continue;
        if(value==0x22){s->random=s->random*1664525u+1013904223u;s->lanes[i].ratio=(s->random>>16)%17;}
        if(value==0x23){int len=s->lanes[i].length?s->lanes[i].length:p->score.patterns[0]->rows;s->rows[i]=(s->tick/p->score.ticks_per_line)%len;s->accumulator[i]=0;}
        if(value==0x28 || value==0x29)s->lanes[i].mode=TS_TRACKER_PATTERN;
    }
}
/* Deliberately bounded to one pattern. Fxx/Bxx/Dxx/E6x/EEx cannot change a
   different card, the global song, or the card's declared timeline length. */
void tapeheadPatternStep(thPatternState *s,const thPatternSource *source)
{
    static const unsigned char num[17]={1,2,3,4,5,7,15,1,17,8,6,5,4,3,2,3,5};
    static const unsigned char den[17]={2,3,4,5,6,8,16,1,16,7,5,4,3,2,1,1,1};
    const TsPatternCard *p=source->definition;const TsSisterTracker *score=&p->score;
    const TsTrackerPattern *patternDef=score->patterns[0];
    bool saved_linear=audio.linearPeriodsFlag;audio.linearPeriodsFlag=p->linear;
    uint32_t saved_samples=audio.samplesPerTickInt;float saved_mul=audio.fSamplesPerTickIntMul;
    audio.samplesPerTickInt=(uint32_t)(audio.freq*2.5/score->bpm);audio.fSamplesPerTickIntMul=1.0f/audio.samplesPerTickInt;
    song_t saved_song=song;channel_t *saved_channel=channel;
    const uint16_t *saved_period=note2PeriodLUT;instr_t *saved_instr[132];uint16_t saved_trim[32];
    memcpy(saved_instr,instr,sizeof(saved_instr));memcpy(instr,source->instruments,sizeof(saved_instr));
    memcpy(saved_trim,channelVolumeTrim,sizeof(saved_trim));
    memset(&song,0,sizeof(song));song.numChannels=score->channel_count;song.speed=score->ticks_per_line;song.BPM=score->bpm;
    song.tick=score->ticks_per_line-(s->tick%score->ticks_per_line);song.globalVolume=s->global_volume;
    channel=s->channels;note2PeriodLUT=p->linear?linearPeriodLUT:amigaPeriodLUT;tapeheadPatternContext=true;
    for(int l=0;l<score->channel_count;++l)channelVolumeTrim[l]=(uint16_t)(fmaxf(0,fminf(2,s->lanes[l].trim))*256);
    for(int l=0;l<score->channel_count;++l){
        TsTrackerLane *lane=&s->lanes[l];channel_t *ch=&channel[l];
        if(s->released){if(s->released==1)keyOff(ch);updateVolPanAutoVib(ch);continue;}
        int length=patternDef->rows;
        if(!score->length_bypass && lane->length && (lane->mode==TS_TRACKER_STANDARD || score->fasttracks_uses_length))length=lane->length;
        int ratio=lane->ratio<17?lane->ratio:7;bool private=s->enabled && lane->mode!=TS_TRACKER_STANDARD;
        int count=private && (!s->clutch[l] || !s->transmission)?0:
            fastTracksClockTick(&s->started[l],&s->accumulator[l],&s->tpl[l],private?num[ratio]:1,private?den[ratio]:1,score->ticks_per_line);
        if(!s->tick)count=1; /* First row sounds at the card boundary. */
        for(int j=0;j<count;++j){
            int row=s->rows[l];note_t event={0};
            if(row>=0 && row<patternDef->rows)memcpy(&event,p->cells[row][l],7);
            if(event.efx==0x23){ /* Z clock operations belong only to this instance. */
                patternClockCommand(s,p,l,event.efxData,length);
                event.efx=event.efxData=0;
            }
            if(event.efx==0x0F || event.efx==0x0B || event.efx==0x0D || (event.efx==0x0E && ((event.efxData&0xF0)==0x60 || (event.efxData&0xF0)==0xE0)))event.efx=event.efxData=0;
            getNewNote(ch,&event);
            int direction=private?s->direction[l]:1;s->rows[l]+=direction;
            if(s->rows[l]>=length || s->rows[l]<0){
                if(private && lane->direction==TS_TRACKER_PING_PONG && length>1){s->direction[l]=-direction;s->rows[l]=direction>0?length-2:1;}
                else s->rows[l]=(s->rows[l]+length)%length;
            }
        }
        /* Match foreground FastTracks: a held private row also holds its
           effects at master tick zero. */
        if(!count && s->tick%score->ticks_per_line)handleEffects_TickNonZero(ch);
        updateVolPanAutoVib(ch);
    }
    if(s->released)s->released=2;
    s->global_volume=song.globalVolume;tapeheadPatternVoices(s);
    audio.samplesPerTickInt=saved_samples;audio.fSamplesPerTickIntMul=saved_mul;
    tapeheadPatternContext=false;audio.linearPeriodsFlag=saved_linear;channel=saved_channel;song=saved_song;note2PeriodLUT=saved_period;
    memcpy(instr,saved_instr,sizeof(saved_instr));memcpy(channelVolumeTrim,saved_trim,sizeof(saved_trim));++s->tick;
}
#endif
