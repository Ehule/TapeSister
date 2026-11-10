# Gallery transition and FX Canvas audit

Baseline: main after PR #198 (`16a1070`), Tree-of-Life-Live-FX-04 revision 3
(2026-11-10). The composition/audio assets are not changed or checked into Git.

## Native project inventory

The Gallery contains ten movements; three are composed: Keter (53 cards),
Chokhmah (48), Binah (43). The other seven are empty. There are fourteen Matrix
effects, including five SunVox instances, with an eight-second Gallery fade.
Every effect has an upstream card path. The apparently floating chains resulted
from showing only the active movement's cards alongside the global processors.
The saved project already has three spatial clusters and shared finishing nodes.

The FX Canvas now shows all Gallery cards by default, preserves their saved
positions, labels the movement clusters, and offers an optional VIEW filter.
A filtered view follows the selected movement's actual routes through effects,
sidechains/feedback and shared downstream processors to the output. It never
switches playback, pauses an engine, or alters routing. Empty movements show an
empty view. ALL MOVEMENTS restores the complete graph, including truly unpatched
effects, which must remain available for editing.

Cards are addressed by stable canvas ID plus local card ID. Parked-card routing,
selection, transforms, undo, effect removal and Clear Cables use that identity.
Legacy unqualified positions are adopted once, in Gallery order; an identically
numbered card in another movement receives an independent position. New saves
record the canvas ID as FxCanvas.Scene metadata. The table Matrix continues to
show the active canvas's card rows. Spatial positions are independent of that
row order. Opening/filtering a graph does not instantiate more audio processors.

## Callback findings and change

`ts_gallery_read` renders at most two source canvases during a transition and
parks the outgoing source at its end. The global effect graph continues to run.
The fade is still applied **before** effects: a silent source does not establish
that its reverb, delay, feedback loop or synthesizer is finished.

Native Delay and Reverb now count exact nonzero samples in their existing rings.
Only when those rings, filter memories and incoming samples are zero can they
skip the expensive reads/filtering/feedback calculations. Parameter smoothing,
modulation oscillators, ring cursors, room-size transitions and output handoff
continue to advance. Delayed impulses across quiet gaps are retained. No timeout,
amplitude gate, tail truncation, buffer clearing or callback allocation is added.
This optimization applies equally to Matrix instances and pedalboard/meta slots.
It saves work in unused chains and again once native tails actually reach zero;
it does not promise that long or sustained feedback will sleep.

SunVox's Input marks even an all-zero supplied buffer as nonempty. An experiment
using the SDK's empty-input protocol reduced CPU further, but tail/re-entry
comparisons revealed changed state in Pitch shifter and some modulated patches,
including Taneb Spring. That experiment was rejected. SunVox processing and its
saved patches are unchanged in this PR. Their silent processing remains a
significant cost requiring a separate module-specific audit.

UI snapshots allocate routes for actual cards rather than the maximum number
of cards in every Gallery slot. UI-only layout copies are outside audio-device
exclusion, and graph-cache comparisons omit unused position capacity.

## Validation and reproduction

`fx_silence_exact` compares the fast path with the complete kernels at 8 and
48 kHz, checking output, internal clocks, ring contents/occupancy, tiny inputs,
silent startup, active audio, full tail drain, automation and re-entry.
The SDL controller tests exercise two movements with identical local card IDs,
real filter clicks, parked routing, cross-movement undo, shared finishing paths,
instance removal, global cable clearing, implicit Mosaic-bus cards and deletion
of the filtered movement. Project-state tests round-trip qualified positions.

The headless callback benchmark supports reproducible manual or future-sequencer
turns using the same Gallery core switch:

```sh
TS_BENCH_GALLERY=2,0,1,2 TS_BENCH_TURN_SECONDS=32 TS_BENCH_POSITION=120 \
  build/tapesister_audio_benchmark PROJECT.tsr 128 0 48000 512 4 -
```

The initial seek is 120 seconds into Binah; subsequent entries use the project's
saved START/RESUME/MARKER behavior. `TS_BENCH_PROFILE=1` enables stage attribution;
profile timings must not be compared with unprofiled measurements.
`TS_BENCH_CANVAS=EXISTING_DIRECTORY` additionally exports native SDL screenshots
and card/effect counts for ALL and each movement, verifying that filtering did
not mutate Gallery content.

Two consecutive paired runs (baseline, candidate, candidate, baseline) of the
128-second sequence after local builds completed gave:

| Callback measurement | Main after #198 | This change |
| --- | ---: | ---: |
| Mean thread CPU per callback, run 1 | 5.038 ms | 4.399 ms |
| Mean thread CPU per callback, run 2 | 4.674 ms | 4.467 ms |
| Average of those means | 4.856 ms | 4.433 ms |
| Thread CPU / audio duration, average | 45.52% | 41.56% |
| 95th-percentile wall time, range | 6.667–8.337 ms | 6.112–6.285 ms |

That is about **8.7% less mean callback CPU time** in this specific sequence.
Raw runs are in `gallery-transition-timings.json`. The repeated runs show host variation; this is not a universal savings claim.
Both versions returned finite output with the same reported RMS (0.0716162499).

These are offline host measurements, not physical-device underrun measurements.
Driver, GUI activity, CPU frequency and scheduling remain factors. Output hashes
between independent project runs can differ because SunVox seeds modulation at
load; the native-kernel comparison is the deterministic sound-preservation test.

## Sequencer boundary

A future sequencer should call the same allocation-free Gallery switch and respect
its busy state while a fade is in progress. For output gain to govern the complete
movement, including its reverb tail, the graph first needs explicit movement
ownership and post-effect boundary gains. Shared processors and sidechain/feedback
paths cannot be assigned to a single movement merely from canvas layout or a
view filter. Muted tail drain, exact idle detection and wake behavior can then be
implemented at those boundaries without changing today's shared routing.
