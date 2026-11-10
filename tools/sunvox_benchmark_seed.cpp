// Linked ONLY into the two offline probes. Shimmer's random LFO seeds include
// stime_ns(); fixing both seed sources lets the probes compare exact PCM.
// GNU ld wraps the SDK's C++ symbol; the application's library stays unchanged.
#include "sundog.h"
extern "C" stime_ns_t __wrap__Z8stime_nsv() { return 0x12345678u; }
extern "C" void ts_sunvox_benchmark_seed() { set_pseudo_random_seed(1); }
