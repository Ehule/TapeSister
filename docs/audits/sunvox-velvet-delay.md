# Velvet Reverb: exact Delay-loop optimization

Baseline: `a8a3c04817eb0043cd6250de164c54b62dcd74ed` (main after #196).
This change is independent of the profiling work in #197.

## What the user's profile establishes

At 48 kHz / 512 ASIO frames, with SunVox processing at 256 frames, Velvet
averaged 1.592583 ms per callback: 24.4% of the 6.532386 ms measured callback,
and about 52% of the six matrix effects' combined time. Its observed sampled
peak was 3.8443 ms. These are inclusive measurements, not additive to the parent.
The retained 16.174 ms callback maximum was not sampled for effect attribution;
this evidence does not identify Velvet as the cause of a particular click.

## Inside the factory patch

Recursive inspection of the pinned factory file finds 1,272 typed modules,
including the outer MetaModule. This is a count of stored modules, not a count
of audio processors doing work on every block.

| Module | Count |
| --- | ---: |
| MetaModule | 21 |
| Amplifier | 295 |
| Echo | 13 |
| Delay | 96 |
| Pitch2Ctl | 384 |
| MultiSynth | 384 |
| MultiCtl | 47 |
| Ctl2Note | 12 |
| Modulator | 16 |
| Feedback | 2 |
| Vibrato | 1 |
| Filter Pro | 1 |

There is no standard Reverb module. All 96 stored Delay modules initially have
zero internal feedback; the surrounding network builds the reverberation.

Linux `gprof` instruction-address sampling of a 180-second default-control
render at 48 kHz / 256 frames put approximately 31.0% of self time in
`psynth_render`, 30.7% in `psynth_delay`, 14.7% in `psynth_amplifier`, and 6.9%
in `psynth_echo`. The wrapper was about 0.7%. This is a hotspot guide, distinct
from the user's ASIO measurements and from the paired timing runs below.

## Change and invariants

For nonzero wet gain with zero internal feedback, Delay now processes spans
ending at the next read or write ring boundary. Each inner iteration still:

1. Reads the delayed value and multiplies it by the original wet gain.
2. Writes that value to the output.
3. Writes the current input to the delay history.

The span length is the minimum of remaining frames and the distances to each
ring boundary. Every span is nonempty for a valid SDK delay, and both cursors
wrap after the span. Short overlapping delays retain their loop-carried
dependency: there are no `restrict` declarations or bulk read/write reorderings.
GCC can vectorize only when its runtime alias checks allow it.

Feedback/allpass, zero wet, dry addition, control/event handling, buffer sizes
and tail/silence handling retain the upstream code. No fast-math or new CPU
instruction-set requirements are introduced. The source archive and factory
patches remain byte-for-byte unchanged. Preparation still patches ASCII markers
as bytes, preserving the earlier native-Windows encoding fix.

## Validation and reproduction

`sunvox_delay_exact` includes the pristine archived handler under another symbol
and compares it with the production handler. Its 270 combinations cover five
sample rates (8–192 kHz), three processing quanta, and 18 delay lengths, including
zero-valued controls, one-sample delays and ring boundaries. Within each case it
changes gains, inversion, mono/stereo, feedback and allpass, shortens/restores
delays without clearing retained history, splits rendering at nonzero offsets,
drains tails and clears the module. It checks exact output, ring history and
cursor state, finite values and output guard regions.

The paired probes use identical host and engine objects; the reference links
only the pristine Delay handler in place of the optimized one. They warm up for
five seconds, feed deterministic stereo input for two thirds of the measured
duration and then silence for the remainder. Thread CPU time includes rendering
and output stores; hashing, finite checks and PCM file writes occur afterward.
Shimmer has random LFOs seeded by the nanosecond clock: unchanged builds differ
across runs without seed control. Both **test executables only** fix the SDK
random seed and wrap `stime_ns()` with the same constant using GNU-compatible
linker wrapping. The production library/application retains normal randomness
and clocks. Complete PCM is compared directly, not only by RMS or hash. The two control modes
are factory defaults and the earlier native audit's settings; neither claims to
reconstruct the user's complete project.

```sh
cmake -S . -B build-delay -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTAPESISTER_BUILD_SDL=OFF -DTAPESISTER_BUNDLE_CDP8=OFF \
  -DTAPESISTER_BUNDLE_VIDEO=OFF -DTAPESISTER_ENABLE_MIDI=OFF
cmake --build build-delay --parallel 2 --target \
  tapesister_sunvox_delay_tests tapesister_sunvox_patch_reference \
  tapesister_sunvox_patch_benchmark
ctest --test-dir build-delay --output-on-failure -R '^sunvox_delay_exact$'
python scripts/compare_sunvox_delay.py --build build-delay --output delay-results.json
```

Run the same commands in Windows UCRT64. The native SunVox comparison workflow
also runs this suite on Linux and Windows, before its optional math-flags
experiment, and uploads `delay-results.json`. Timings are reported rather than
used as noisy CI pass/fail thresholds; exact PCM equality is required.

## Linux observations

GCC 14.2.0, `-O3 -g -DNDEBUG`, no fast-math or CPU extensions, on a shared
virtualized AMD EPYC 7763 host. At 48 kHz / 256 frames, medians of three paired
60-second Velvet renders, alternating execution order:

| Controls | Original CPU | Optimized CPU | Relative reduction |
| --- | ---: | ---: | ---: |
| Factory defaults | 6.9504% | 5.8548% | 15.76% |
| Earlier audit settings | 7.0477% | 6.1671% | 12.49% |

CPU here means thread CPU seconds divided by rendered audio seconds, not total
machine utilization. All 26 paired comparisons passed direct PCM equality:
Velvet, Ensemble and Shimmer at 64/256/1024 frames in both control modes, plus
Velvet at 8/44.1/96/192 kHz. The other cases are shorter parity checks; their
single-run timings fluctuate and are not evidence of a repeatable speed change.
Full observations: [sunvox-velvet-delay-linux.json](sunvox-velvet-delay-linux.json).
The 270-case Delay oracle, five preparation/encoding checks, and existing SunVox
real-time, load/save and matrix-instance regressions also passed locally.

## Limits

This reduces one effect's processing cost; it does not promise a matching
percentage reduction in the full audio callback or eliminate every click.
Scheduling, control-lock holds, UI work, graph traversal and other effects remain
separate costs. A same-device, same-project ASIO comparison after resetting
telemetry is needed to quantify the improvement on the user's computer.
