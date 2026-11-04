# SunVox-informed resource efficiency

This is a staged audio-engine optimization effort. Preserve TapeSister's live
behavior and timbres while removing repeated work from its callback. The first
change addresses routing bookkeeping; it does not replace any DSP algorithm.

## Evidence and scope

The reference is the official SunVox 2.1.4d developer package, especially:

- `lib_sunvox/psynth/psynth_net.cpp`: block rendering, event offsets, render-once
  flags, input/output content tracking and per-module CPU accounting.
- `psynths_sampler.cpp`: voice activity and playback processing.
- `psynths_filter2.cpp` and `psynths_reverb.cpp`: module-specific silence/tail
  handling. Reverb checks its stored signal before declaring itself empty.
- `psynths_waveshaper.cpp`: lookup/interpolation for the shaping curve.
- The package's build configuration: its supplied Linux x86_64 library uses the
  floating-point engine. Fixed-point arithmetic does not explain that build's
  efficiency advantage.

Primary references: [developer library](https://warmplace.ru/soft/sunvox/sunvox_lib.php),
[2.1.4d package](https://warmplace.ru/soft/sunvox/sunvox_lib-2.1.4d.zip),
and [manual](https://warmplace.ru/soft/sunvox/manual.php).

The supplied `lib_sunvox/LICENSE` licenses that engine source under MIT,
Copyright (c) 2007–2025 Alexander Zolotov. These changes independently apply
architectural ideas; they copy no SunVox source and introduce no SunVox runtime
dependency. Any later source port must carry the applicable copyright/license
and check its dependencies separately.

The starting TapeSister revision is
`e0f9203497d3993c1086c743268400093e13e40d`. Existing strengths include an active
Mosaic scheduler, cached playback/filter/gain coefficients, sparse route
coefficients, graph ordering compiled on edits, block-batched snapshots and x86
denormal protection. Extend these mechanisms instead of duplicating them.

The earlier server comparison found approximately 35.8% of one real-time core
budget for the saved TapeSister callback and 5.2% for its SunVox reconstruction.
Those are different DSP workloads: the reconstruction omits an enabled
eight-lens Prism stage and substitutes other effects. The user's 7% SunVox
reading was on an X230. Neither comparison establishes an attainable fixed
speed ratio or predicts this change's laptop CPU use.

## Transferable mechanisms and implementation order

| Order | SunVox mechanism | TapeSister application | Acceptance requirement |
| --- | --- | --- | --- |
| 1 | Avoid work on unused signal paths | Iterate route destination masks; eliminate temporary audio-buffer copies; snapshot block-stable output mappings | Identical output with existing routing, handoff, capture and feedback semantics |
| 2 | Render arrays rather than rebuilding module context for every sample | Introduce preallocated block/span interfaces for sources, graph mixing and effects; retain a scalar reference | Same sample times, accumulation order, gain ramps, taps and output layout |
| 3 | Track known-empty input and output ranges | Carry silence metadata through source buffers and graph connections; clear only ranges previously written | Disconnects never replay stale buffers; zero input does not prematurely discard tails |
| 4 | Each module renders once per pass | Extend the existing compiled topology with explicit audio/control/tail requirements and source reachability | Fan-outs share one processor history; sidechain-only and feedback paths stay active |
| 5 | Module-specific idle handling | Add a lightweight state-advance path and a proven tail-empty state where appropriate | Reconnection, LFO phase, random state, grain scheduling, bypass ramps and tape state retain their intended behavior |
| 6 | Separate controls/events from sample loops | Prepare coefficients at control boundaries; publish local meter accumulators at block end; keep required per-sample recurrence | Parameter/MIDI changes still land on the correct sample and no display or capture feature loses its signal |
| 7 | Tune expensive algorithms deliberately | Profile nonlinear reverb/distortion, interpolation and transcendental functions after structural work | Spectrum, aliasing, dynamics, feedback stability and listening checks before any approximate replacement |
| 8 | Match quality to the target device | Add only measured, explicit optional quality choices; consider portable SIMD for contiguous buffers | Default sound stays compatible; an X230 build must not require AVX2 or FMA |

Block processing can preserve sample-accurate behavior: split blocks into spans
at events, ramp boundaries and transport changes. A block API alone is not a
speedup if it merely wraps the old callback in another sample loop. The useful
change is reusing prepared state and operating on contiguous buffers.

The matrix's feedback delay is 64 frames. A processor-at-a-time block conversion
must respect causal dependencies across that delay, by bounded spans or an
explicit feedback schedule. External Insert latency, hardware output ranges,
spatial decode, FILE OUT and dry capture taps also need their own contracts.
Do not silently add a full buffer of latency or route a signal through an
instance twice to simplify batching.

A graph node with no current input may still have an audible tail, a detector
signal, modulation or future reconnection state. Existing tests require Prism's
clock to advance once per sample even in bypass. Reachability, input silence,
audio-history silence and control-state activity are separate facts. Introduce
processor-specific contracts before skipping DSP.

Meter publication can move to block boundaries while the existing peak/decay
recurrence runs in local state. Replacing it with a simple block maximum times
one decay factor changes the timing of short transients; preserve the actual
recurrence or document/test a deliberate display-law change. Audio peak guards
and limiters remain in the signal path.

## First implementation

- `ts_source_route_add` visits set destination bits in increasing order instead
  of checking all 66 destinations for every mix addition. Both mask words are
  supported, including destinations 64 and 65. Unused high bits are ignored
  when indexing, as in the dense implementation.
- A mix can reset itself while retaining output availability. The runtime no
  longer copies an entire `TsSourceRouteMix` just to preserve those two fields.
- Matrix accumulation uses that same sparse addition for runtime-owned voices,
  avoiding a separate dense pass over their graph destinations.
- Settled routes with exactly zero input retain their compiled graph/send/output
  ownership without evaluating coefficient products. Silent input still runs
  an active route slew; no source engine or DSP module is put to sleep.
- Speaker channel lookup and fixed slew rates are prepared once per callback.
  Per-sample gains, routing ramps, processors, meter laws and histories retain
  their existing update cadence.

The source-route test adds a dense arithmetic oracle for sparse/dense masks,
mask boundaries, self-addition and negative handoff gains, plus an aliased
reset check. Silent-route tests compare against the ordinary arithmetic path
and check that 5 ms slews finish even without input. Existing matrix tests cover graph order, route slews, sidechains,
feedback, shared processor history, imported-chain equivalence and persistence.

## Reproducible callback measurements

Build the explicit utility with the normal development dependencies:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTAPESISTER_ENABLE_MIDI=OFF -DTAPESISTER_BUNDLE_CDP8=OFF \
  -DTAPESISTER_BUNDLE_VIDEO=OFF
cmake --build build --target tapesister_audio_benchmark
build/tapesister_audio_benchmark /path/to/song.tsr 120 30 48000 512 2
```

Arguments are project, total seconds, warmup seconds, sample rate, buffer frames,
output channels, then an optional raw interleaved float32 hardware-output file.
The utility starts Mosaic at zero and loads the saved processing state. It is a
Mosaic callback fixture: it does not start a standalone tracker song, send live
MIDI, run the GUI/video, or connect an audio device or external processor. As in
the original comparison fixture, OUT is unity and the output limiter is set to
-0.2 dB, 3 ms attack, 120 ms release. It is not a complete application startup
or an external hardware loopback test.

JSON reports callback thread CPU time as a percentage of one core's real-time
audio budget, mean callback cost, wall p95, finite stereo levels, and hashes of
both the stereo capture tap and every hardware output channel. Loading,
validation, hashing and optional file writing are outside the callback timer;
file writing can still perturb system conditions, so omit it for timing runs.
Hashes cover startup/warmup too. A matching hash is useful regression evidence;
a direct byte comparison is stronger when feasible.

For a baseline before this utility existed, compile this same fixture against
the old source using `TS_BENCH_APP_SOURCE` and the old core/support libraries,
with identical optimization flags. Do not compare an old binary compiled with
profiling against a new unprofiled one. The SDK and source-song assets are not
required in the repository; supply your own authorized test projects.

Run binaries serially, reversing their order on alternate trials:

```sh
python3 tools/compare_audio_benchmarks.py /path/to/baseline /path/to/candidate \
  /path/to/song.tsr --seconds 120 --warmup 30 --trials 3 \
  --rate 48000 --block 512 --channels 2 --output paired-results.json
```

The runner retains every trial and exits with failure if stereo/hardware
hashes differ. Repeat with representative direct/matrix routing, user effect
instances, sidechains, feedback, tail-only passages, live input, multiple output
channels, clock changes and small buffers before accepting broader changes.
A headless offline fixture does not establish physical audio-device stability.

## Hardware acceptance

Repeat matching settings on the X230: backend, 48 kHz, 512 frames, output channel
count, power mode and CPU-meter definition. Record callback CPU and peaks,
underruns, audio hashes where deterministic, and visible/minimized UI use. Test
at least one dense Mosaic arrangement, a pattern arrangement, live voices,
tail-only material and a project with feedback/Insert. Keep render time,
resident memory and load time separate from steady-state audio CPU.

A change is accepted for its measured behavior on the relevant projects, not
because its structure resembles SunVox. Set performance targets after these
baselines; the earlier reconstruction does not justify promising SunVox's CPU
percentage for TapeSister's larger DSP workload.
