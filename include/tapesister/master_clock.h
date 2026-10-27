#ifndef TAPESISTER_MASTER_CLOCK_H
#define TAPESISTER_MASTER_CLOCK_H
#include <stdio.h>

/* Native-player IDs match the existing group transport. Future persistent
   loop cards are independent destinations with their own route records; they
   must never inherit the main Mosaic playhead's run/position policy. */
enum { TS_CLOCK_ARP=1,TS_CLOCK_TRACKER=2,TS_CLOCK_MOSAIC=4 };
enum { TS_CLOCK_INTERNAL,TS_CLOCK_TRACKER_SOURCE };
typedef struct {
    unsigned enabled,source,bpm,tempo_mask,arp_division;
    unsigned position_custom,home_mask;
} TsMasterClockControls;
typedef struct {
    int running;
    double seconds,beats,phase,quarter_seconds;
} TsMasterClock;

void ts_master_clock_default(TsMasterClockControls *controls);
int ts_master_clock_valid(const TsMasterClockControls *controls);
/* Group transport is deliberately independent of master-tempo enablement. */
void ts_master_clock_transport(TsMasterClock *clock,int command);
unsigned ts_master_clock_home_mask(const TsMasterClockControls *controls,unsigned group);
/* Audio-span operation, not UI/wall-clock timing. Tempo remains available to
   independent followers while the master transport cursor is paused. */
void ts_master_clock_advance(TsMasterClock *clock,const TsMasterClockControls *controls,
    unsigned frames,unsigned sample_rate,double tracker_quarter_seconds);
int ts_master_clock_write(FILE *file,const TsMasterClockControls *controls);
/* 1 recognized, 0 unknown, -1 malformed. Validate the whole object after read. */
int ts_master_clock_read(TsMasterClockControls *controls,const char *key,const char *value);
#endif
