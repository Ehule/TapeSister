#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/master_clock.h"
#include <assert.h>
#include <math.h>
#include <string.h>

int main(void)
{
    TsMasterClockControls p;ts_master_clock_default(&p);assert(ts_master_clock_valid(&p) && !p.enabled);
    TsMasterClock c={0},before=c;
    ts_master_clock_advance(&c,&p,48000,48000,.5);assert(!memcmp(&before,&c,sizeof(c)));
    assert(ts_master_clock_home_mask(&p,6)==6);
    p.position_custom=1;p.home_mask=2;assert(ts_master_clock_home_mask(&p,4)==2);
    p.enabled=1;ts_master_clock_transport(&c,1);
    for(int i=0;i<100;++i)ts_master_clock_advance(&c,&p,480,48000,0);
    assert(fabs(c.seconds-1)<1e-12 && fabs(c.beats-2)<1e-12);
    double seconds=c.seconds,beats=c.beats;p.bpm=60;
    ts_master_clock_advance(&c,&p,12000,48000,0);
    assert(fabs(c.seconds-seconds-.25)<1e-12 && fabs(c.beats-beats-.25)<1e-12);
    ts_master_clock_transport(&c,0);before=c;
    ts_master_clock_advance(&c,&p,12000,48000,0);
    assert(c.seconds==before.seconds && c.beats==before.beats && fabs(c.phase-before.phase-.25)<1e-12);
    p.source=TS_CLOCK_TRACKER_SOURCE;
    ts_master_clock_advance(&c,&p,12000,48000,.5);assert(c.quarter_seconds==.5);
    p.tempo_mask=TS_CLOCK_TRACKER;assert(!ts_master_clock_valid(&p));p.tempo_mask=TS_CLOCK_ARP;
    ts_master_clock_transport(&c,2);assert(!c.running && !c.seconds && !c.beats && !c.phase);
    assert(ts_master_clock_read(&p,"Clock.BPM","nan")==-1);
    assert(ts_master_clock_read(&p,"Clock.BPM","31")==-1);
    assert(ts_master_clock_read(&p,"Clock.BPM","256")==-1);
    assert(ts_master_clock_read(&p,"Clock.HomeMask","-1")==-1);
    assert(ts_master_clock_read(&p,"Clock.TempoMask","4")==-1);
    assert(ts_master_clock_read(&p,"Clock.BPM","125")==1 && p.bpm==125);
    assert(ts_master_clock_read(&p,"Clock.Unknown","1")==0);
    FILE *f=tmpfile();assert(f && ts_master_clock_write(f,&p));rewind(f);
    TsMasterClockControls loaded;ts_master_clock_default(&loaded);char line[128];
    while(fgets(line,sizeof(line),f)) {
        char *eq=strchr(line,'=');assert(eq);*eq++=0;eq[strcspn(eq,"\r\n")]=0;
        assert(ts_master_clock_read(&loaded,line,eq)==1);
    }
    fclose(f);assert(ts_master_clock_valid(&loaded));assert(!memcmp(&p,&loaded,sizeof(p)));
    puts("Master tempo, paused independent clock, position routing and persistence tests passed.");
    return 0;
}
