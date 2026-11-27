# Adaptive Gallery stress diagnostic and DSP efficiency

The diagnostic is intended to find the loaded workload's timing limits and
verify playback/transition recovery on the current host. It does not select a
particular composition, effect ID, CPU model or operating system.

Run instructions and measurement limits are in
[the diagnostic guide](../audits/live-test.md).

## Output-preserving changes

Meta FX can now process bounded spans of up to 64 frames. Spans split at the
existing macro-refresh boundary, retaining per-sample smoothing, gate ramps,
transport synchronization, random state, and the scalar path where graph
chronology requires it. Scratch storage is bounded and local to the audio call.

Pedalboards track active children, replacement fades and retiring tails with
private masks. Stable boards avoid repeatedly scanning all 20 slots' atomic
transition counters. UI retirement notifications still use the existing
atomic counters, and topology publication keeps the existing audio exclusion.

The Audio Health setup snapshot retains only Matrix routing, names, effect
identities, and other values actually reported. It no longer copies every
embedded pedalboard and Meta control while holding the UI/audio lock. Name
lookup and report formatting remain outside the lock; the audio renderer and
its synchronization are unchanged. Regression checks cover report equivalence,
relevant-field invalidation, legacy clipping/insert flags, SunVox display data,
and exclusion of unreported processor settings. This removes known excess
copying; it is not a measured reduction in physical-device deadline misses.

Recurring main-loop locks have distinct site names for memory clearing,
Gallery clock metadata, and the playback UI snapshot. Subsequent logs can
identify which scope was held, rather than labeling all three as `main`.

The Meta FX regressions compare scalar, span and in-place PCM bit for bit across
all ten presets and additional random/tempo-synchronized AutoPan stages at
44.1, 48 and 96 kHz. Uneven spans exercise macro changes, morphing, stage ramps,
tails and transport resets. Matrix tests include the outer mix/gain envelope.
Pedalboard regressions cover sparse layouts, moves, replacement/removal tails,
overlapping fresh insertions and eventual empty-board passthrough.

An independent lifecycle driver linked against the original and updated core
also produced identical combined wet/dry PCM: `d43b295f881fb6f9`.

## Isolated CPU measurements

Baseline: `67003119800793852721fc07e9d6143af74b25da` (PR #240).
Measured on the Linux development executor on November 24, 2026, using Release
builds and the same public-API driver against each core library. These are not
measurements from the Dell, and they are not whole-application speedups.

Each case rendered 960,000 stereo frames at 48 kHz. Five baseline and five
updated runs alternated order, pinned to one CPU. The table uses median process
CPU time. Input generation, PCM hashing and effect construction were outside
the timed region. Detailed profiling was off and project compilation was
stopped during measurement. Every before/after PCM hash matched.

| Workload | Baseline CPU | Updated CPU | Change |
| --- | ---: | ---: | ---: |
| Meta FX ARC | 1064.13 ms | 1013.89 ms | 4.72% lower |
| Meta FX SPRING DUST | 659.53 ms | 646.12 ms | 2.03% lower |
| Meta FX GLASS THREAD | 293.95 ms | 262.99 ms | 10.53% lower |
| Meta FX RESONANT ROOM | 608.54 ms | 564.61 ms | 7.22% lower |
| One-effect board, single-sample calls | 112.66 ms | 80.42 ms | 28.62% lower |
| Three-effect board, single-sample calls | 288.71 ms | 272.50 ms | 5.61% lower |
| Three-effect board, 64-frame calls | 235.70 ms | 236.65 ms | 0.40% higher; essentially unchanged |

The single-sample board cases retain 20 slots with holes. They expose lifecycle
bookkeeping costs; already-blocked effect processing benefits much less.
A shorter preliminary comparison covered all ten Meta presets and six board
cases with matching PCM, but its noisy timings are not used for these claims.

## Diagnostic acceptance

Successful execution and deadline performance are distinct results. Mode 4
requires real audio-clock progress, completed native-speed movement endpoints,
and an incoming/outgoing trace for each planned fade. Sub-window fades are
checked with renderer frame counts rather than inferred from coarse UI polls.
Interrupted fades are labeled explicitly and followed by recovery checks.
Lost telemetry, missing source references, unsupported timeline ranges and
incomplete coverage prevent a completed diagnostic result. An empty Gallery
reports that no playback workload was available.

The report validates unique paired stage boundaries, positive callback/frame
progress, stable movement identities, final source positions and retirement
counts. Callback durations that cross measurement boundaries are excluded from
event peaks; asynchronous log drains are ordered by event time. Arrival gaps
remain separate from processing overruns. Neither is presented as a count of
audible clicks.

The dedicated aligned-overlap stage gives each movement independent section
endpoints so both selected density peaks occur at a common fade phase. Tests
cover brief interior bursts, differing saved speeds and boundary-limited
sections. The reporter rejects a trace whose outgoing peak has already passed,
whose incoming peak occurs at a different phase, or whose stable IDs/speeds
disagree. A discrete frame witness must lie inside both recorded density
intervals, including exact-start and sub-sample boundary cases. Native section
limits that prevent alignment are reported explicitly.

Closed/open Canvas samples use the same source positions with effects history
carried between stages. They do not isolate drawing cost from evolving DSP state.
The full-play stages run with detailed profiling off; the stress stages turn
it on to identify costs. Physical-interface listening and timing on the intended
host remain necessary to evaluate a performance setup.
