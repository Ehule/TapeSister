#include "tapesister/master_clock.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void ts_master_clock_default(TsMasterClockControls *c)
{*c=(TsMasterClockControls){.bpm=120,.arp_division=1};}
int ts_master_clock_valid(const TsMasterClockControls *c)
{
    return c && c->enabled<=1 && c->source<=TS_CLOCK_TRACKER_SOURCE && c->bpm>=32 && c->bpm<=255 &&
        c->tempo_mask<=3 && c->arp_division<=2 && c->position_custom<=1 && c->home_mask<=7 &&
        !(c->source==TS_CLOCK_TRACKER_SOURCE && (c->tempo_mask&TS_CLOCK_TRACKER));
}
void ts_master_clock_transport(TsMasterClock *c,int command)
{
    c->running=command==1;
    if(command==2)c->seconds=c->beats=c->phase=0;
}
unsigned ts_master_clock_home_mask(const TsMasterClockControls *c,unsigned group)
{return (c->position_custom?c->home_mask:group)&7u;}
void ts_master_clock_advance(TsMasterClock *c,const TsMasterClockControls *p,
    unsigned frames,unsigned rate,double tracker_quarter)
{
    if(!p->enabled || !frames || !rate)return;
    double quarter=p->source==TS_CLOCK_TRACKER_SOURCE?tracker_quarter:0;
    if(!isfinite(quarter) || quarter<=0)quarter=60.0/p->bpm;
    double seconds=(double)frames/rate,beats=seconds/quarter;
    c->quarter_seconds=quarter;c->phase+=beats;c->phase-=floor(c->phase);
    if(c->running){c->seconds+=seconds;c->beats+=beats;}
}
int ts_master_clock_write(FILE *f,const TsMasterClockControls *c)
{
    return ts_master_clock_valid(c) && fprintf(f,
        "Clock.Enabled=%u\nClock.Source=%u\nClock.BPM=%u\nClock.TempoMask=%u\n"
        "Clock.ArpDivision=%u\nClock.PositionCustom=%u\nClock.HomeMask=%u\n",
        c->enabled,c->source,c->bpm,c->tempo_mask,c->arp_division,c->position_custom,c->home_mask)>=0;
}
int ts_master_clock_read(TsMasterClockControls *c,const char *key,const char *value)
{
    struct Field {const char *key;unsigned *at,min,max;} fields[]={
        {"Clock.Enabled",&c->enabled,0,1},{"Clock.Source",&c->source,0,TS_CLOCK_TRACKER_SOURCE},
        {"Clock.BPM",&c->bpm,32,255},{"Clock.TempoMask",&c->tempo_mask,0,3},
        {"Clock.ArpDivision",&c->arp_division,0,2},{"Clock.PositionCustom",&c->position_custom,0,1},
        {"Clock.HomeMask",&c->home_mask,0,7}};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i)if(!strcmp(fields[i].key,key)) {
        errno=0;char *end;unsigned long n=strtoul(value,&end,10);
        if(errno || end==value || *end || n>UINT_MAX || n<fields[i].min || n>fields[i].max)return -1;
        *fields[i].at=(unsigned)n;return 1;
    }
    return 0;
}
