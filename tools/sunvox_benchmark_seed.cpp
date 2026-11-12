// Linked ONLY into the two offline probes. Shimmer's random LFO seeds include
// stime_ns(); fixing both seed sources lets the probes compare exact PCM.
// GNU ld wraps the SDK's C++ symbol; the application's library stays unchanged.
#include <cstdint>
// Pinned time/misc ABI, without pulling GUI/platform compile flags into the
// application callback probe. stime_ns_t is uint64_t in SunVox 2.1.4d.
void set_pseudo_random_seed(uint32_t seed);
extern "C" uint64_t __wrap__Z8stime_nsv() { return 0x12345678u; }
extern "C" void ts_sunvox_benchmark_seed() { set_pseudo_random_seed(1); }
