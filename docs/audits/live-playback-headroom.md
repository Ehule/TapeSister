# Live playback headroom after the Tree04 replay

This change is stacked on PR #207 (63ea32c84047cb40c03f8839eb02373c25424b91).
Keep the diagnostic scenario, project, ASIO format and worker setting unchanged
for the physical comparison. Do not merge the stack based only on CI timings.

## Evidence and scope

The completed MOTU M Series / 48 kHz / 512-frame / four-channel replay had
243 deadline overruns across 20,248 callbacks. Of those, 166 fell in the
nominal 200–208 second return-to-Binah crossfade window. Another 29 occurred
during steady Chokhmah playback at 190–200 seconds. These are deadline
observations, not a count of detected audible clicks. The profiler was off;
the log cannot attribute that cluster to an individual effect.

The patch removes avoidable work without changing the scenario:
- Mosaic reuses one audio-owned route workspace. It clears previously owned
  graph destinations and the small fixed bus section rather than clearing
  all 186 destinations on every sample.
- Gallery applies its crossfade gain at the final route accumulation after
  each scene's handoff. Both scenes still render at the zero-gain endpoints;
  the main-bus fade arithmetic and eight-second diagnostic fade are unchanged.
  Two intermediate route buffers and unity-gain accumulation passes disappear.
- Native Grain, Delay and Reverb ring positions advance with a boundary check
  rather than integer remainder. Delay interpolation casts its already
  wrapped nonnegative position rather than calling floor. Interpolation,
  saturation, feedback, modulation and effect tails remain unchanged.

The route workspace is allocated with the existing Mosaic object. No new
audio-thread allocation, locks, worker threads, latency or lower-quality mode
is introduced. Global matrix effects are still processed by the existing
graph; this change does not claim that Gallery was duplicating the FX graph.

## Verification

Two regression executables carry frozen reference implementations from #207,
separate from the optimized paths. They compare PCM and routed buses exactly,
including recording taps, handoff state, scene clocks, output destinations in
all three mask words, automation, mute/solo, seek, live-loop transport, audition,
sample-rate changes, ring wrapping, bypass and tails.

Each executable also prints median before/after kernel timings from five
alternating trials on the same CI runner. Timings are observations, not pass/fail
thresholds. They exclude the full application, UI, SunVox graph and physical
audio driver; they must not be presented as ASIO improvements.

Linux and Windows native bundle workflows run these comparisons explicitly,
alongside the existing regressions. The opt-in `tree04-live-v1` test and its
automatic log export remain unchanged. A fresh saved JSONL from the same
physical setup is required to determine whether deadline headroom improved.
