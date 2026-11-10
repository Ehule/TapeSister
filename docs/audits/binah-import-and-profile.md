# Binah import and callback profiling audit

## Reproduction

The November 10, 2026 Tree of Life FX04 -> FX03 canvas-3 import loads all
43 Binah cards and all eight PCM sources. In the original importer, 37 cards
retain destinations in instance slots 11–14; FX03 has only six instances.
The sources are present, but those routes have no processors to receive them.
This is distinct from missing WAV assets.

The routed importer stages the selected scene, follows its downstream instance
graph, copies the four required effects into free destination slots, and remaps
card routes, processor routes, detector destinations and saved FX positions.
The destination's original six processors, fixed buses and finishing controls
are retained. SunVox definitions are embedded in the saved state, not dependent
on the original `.sunsynth` path. The user-provided archives remain unchanged.

A native API reproduction using the two supplied archives verifies 43 cards,
eight valid PCM sources and zero missing effect destinations after import.
Rendering 96,256 stereo frames from position 220 s through FX03's destination
master produces finite, nonzero output (peak 0.201010, RMS 0.062270). This is an
offline routing check, not an ASIO underrun or listening-quality claim.

## Reorder and replacement

Reordering moves complete canvas records and updates active/outgoing indices by
scene identity, preserving transport position and an in-progress fade. Replacement
keeps the playlist position but assigns a fresh canvas ID, stages everything first,
and frees retired scenes outside audio-device exclusion. The caller-owned root
has the same lifetime as when it is detached by deletion. The active canvas must
be stopped, with audition and live loops stopped/paused. A nonempty destination
requires a second confirmation in the chooser; a full 16-canvas Gallery can replace.

Before staging replacement effects, the importer identifies processors reachable
only from that canvas. Routes from other Gallery cards, bank tiles, tracker lanes
and fixed/external matrix rows protect shared chains transitively. Exclusive slots
are recycled, avoiding abandoned chains and unnecessary DSP on repeated imports.
The complete scene/graph update is published together; validation, capacity or
engine-preparation failure leaves the destination intact. Matrix undo/A-B history
is cleared after publication because its old snapshots predate the new graph.

## Clicking and measurement overhead

The supplied 48 kHz / 512-frame report has a 10.667 ms deadline, approximately
9.8 ms average work, a retained 19.641 ms maximum and 1,675 over-budget callbacks.
Binah's Taneb Spring accounts for about 2.47 ms per sampled block (25.3% of the
reported callback total). The later session records overruns before profiling is
turned on as well as during profiling. Only one retained control hold exceeds a
buffer; the FX Canvas was already closed for part of the clicking interval.
Zero driver-reported errors do not negate the observed processing overruns.

Inspection found that PROFILE concentrated every per-sample stage and all Matrix
effect timers into the same sampled callback. The fix assigns separate rotating
phases to stages and effect slots. Each row still measures a complete callback
at a 1/32 sampling rate, including buffered SunVox render bursts. Rates/totals
remain estimates and peaks remain sampled observations. The Matrix aggregate is
the sum of estimated slot work; nested subsystem rows must not be added together.

A deterministic 512-frame regression exercises every stage and all 16 slots over
1,024 callbacks. No callback performs more than 1,026 timer reads in that harness,
while each effect receives 32 complete-block samples. PROFILE on/off produces
bit-identical audio across 65,536 stereo frames, including effect replacement.
This reduces profiling concentration; it does not eliminate the substantial
baseline DSP load. Experimental scalar/SSE spring-filter kernels did not show a
convincing repeatable full-patch improvement and are not included.

For live verification, first test the same passage with PROFILE off, then on,
resetting Audio Health between runs. Compare over-budget callback counts and
listen through the same interface settings. A 1,024-frame ASIO buffer offers more
scheduling margin as an interim measure; it does not reduce per-second DSP work.
No physical MOTU/ASIO verification was available in this environment.

## Regression coverage

- Gallery core: moving either side of a fade, stable identity/view metadata,
  active replacement refusal, root/non-root ownership, order after save/reopen.
- Portable canvas transfer: PCM and immutable pattern assets, embedded SunVox
  definitions, slot/route remapping, destination graph preservation, moving the
  source bundle away, failure without mutation, shared-chain protection and
  recycling exclusive slots in a full effect graph.
- Native Gallery controller: actual browser/import controls, Add to end, a full
  Gallery replacement, populated confirmation, active playback refusal, source
  cache cleanup/rebinding, reorder buttons and Alt+Up/Down.
- Profiler: staggered timer bound, phase coverage, estimates, effect generations,
  crossfade accounting, report bounds and exact audio comparison.
