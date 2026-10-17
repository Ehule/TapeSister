#ifndef TAPESISTER_PERFORMANCE_TRANSPORT_H
#define TAPESISTER_PERFORMANCE_TRANSPORT_H
/* Project controls. Selection uses ARP=1, TrackSister=2, Mosaic=4 separately. */
typedef struct {
    float gain[4]; /* ARP, TrackSister, Mosaic, live tiles (no transport). */ /* Pre-routing trim, 0 (silent) .. 2 (+6 dB). */
    /* restart is retained for file compatibility; F11 always resumes. */
    unsigned restart, arp_sync, always_on_top; /* arp_sync: free, 1/4, 1/8, 1/16. */
} TsPerformanceMixer;
static inline void ts_performance_mixer_default(TsPerformanceMixer *m)
{ *m=(TsPerformanceMixer){{1,1,1,1},0,0,0}; }
#endif
