#ifndef TAPESISTER_PERFORMANCE_TRANSPORT_H
#define TAPESISTER_PERFORMANCE_TRANSPORT_H
/* Project controls. Selection uses ARP=1, TrackSister=2, Mosaic=4 separately. */
typedef struct {
    float gain[3]; /* Pre-routing trim, 0 (silent) .. 2 (+6 dB). */
    unsigned restart, arp_sync, always_on_top; /* arp_sync: free, 1/4, 1/8, 1/16. */
} TsPerformanceMixer;
static inline void ts_performance_mixer_default(TsPerformanceMixer *m)
{ *m=(TsPerformanceMixer){{1,1,1},0,0,0}; }
#endif
