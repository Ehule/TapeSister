/* Scatter plans notes once on the UI thread; playback uses ordinary card notes. */
const char *ts_mosaic_pitch_mode_name(int mode)
{
    static const char *names[]={"KEEP NOTES","CHORD PER CARD","SPREAD CHORD","SPREAD SCALE"};
    return mode>=0 && mode<TS_MOSAIC_PITCH_MODE_COUNT?names[mode]:names[0];
}
const char *ts_mosaic_chord_name(int chord)
{
    static const char *names[]={"PRIMARY CHORD","MAJOR","MINOR","MAJOR 7","MINOR 7","DOMINANT 7","SUS2","SUS4"};
    return chord>=0 && chord<TS_MOSAIC_CHORD_COUNT?names[chord]:names[0];
}
const char *ts_mosaic_pitch_order_name(int order)
{
    static const char *names[]={"RANDOM","ASCENDING","DESCENDING","SHUFFLED"};
    return order>=0 && order<TS_MOSAIC_PITCH_ORDER_COUNT?names[order]:names[0];
}
int ts_mosaic_pitch_choices(const TsMosaicPitchSettings *p,
    const int primary[TS_MOSAIC_NOTES],int primary_count,
    int choices[128],int intervals[TS_MOSAIC_NOTES],int *voices)
{
    /* Same tonal sets as FM's Pitch page: chromatic, major, natural minor,
       major pentatonic and whole tone. */
    static const unsigned scales[]={0xfff,0xab5,0x5ad,0x295,0x555};
    static const int chords[TS_MOSAIC_CHORD_COUNT][4]={
        {0},{0,4,7},{0,3,7},{0,4,7,11},{0,3,7,10},{0,4,7,10},{0,2,7},{0,5,7}};
    *voices=0;
    if(p->mode<=TS_MOSAIC_PITCH_KEEP || p->mode>=TS_MOSAIC_PITCH_MODE_COUNT ||
       p->root<0 || p->root>11 || p->scale<0 || p->scale>=5 ||
       p->chord<0 || p->chord>=TS_MOSAIC_CHORD_COUNT ||
       p->low_octave<-1 || p->high_octave>9 || p->low_octave>p->high_octave)return 0;
    int count=0,low=12*(p->low_octave+1),high=12*(p->high_octave+2)-1;
    if(high>127)high=127;
    unsigned mask=0;
    if(p->mode==TS_MOSAIC_PITCH_SPREAD_SCALE)mask=scales[p->scale];
    else {
        if(p->chord==TS_MOSAIC_CHORD_PRIMARY) {
            if(primary_count<1 || primary_count>TS_MOSAIC_NOTES)return 0;
            for(int i=0;i<primary_count;++i) {
                if(primary[i]<0 || primary[i]>127)return 0;
                int at=i;while(at>0 && intervals[at-1]>primary[i]){intervals[at]=intervals[at-1];--at;}
                intervals[at]=primary[i];
            }
            int base=intervals[0];*voices=primary_count;
            for(int i=0;i<*voices;++i)intervals[i]-=base;
        } else {
            *voices=p->chord>=TS_MOSAIC_CHORD_MAJ7 && p->chord<=TS_MOSAIC_CHORD_DOM7?4:3;
            for(int i=0;i<*voices;++i)intervals[i]=chords[p->chord][i];
        }
        for(int i=0;i<*voices;++i)mask|=1u<<(intervals[i]%12);
    }
    for(int note=low;note<=high;++note) {
        int relative=(note%12-p->root+12)%12;
        if(p->mode==TS_MOSAIC_PITCH_CHORD) {
            if(!relative && note+intervals[*voices-1]<=high)choices[count++]=note;
        } else if(mask&(1u<<relative))choices[count++]=note;
    }
    if(p->mode!=TS_MOSAIC_PITCH_CHORD){*voices=1;intervals[0]=0;}
    return count;
}
