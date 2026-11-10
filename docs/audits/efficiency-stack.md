# Effects efficiency stack

The five phases preserve the saved instrument's sound, routing and timing.
Each phase is based on the preceding branch and must pass its own regression
checks before the stack is offered for merging. No approximate DSP, automatic
quality reduction, rendered replacement, or extra audio latency is implied.

1. **Block work and graph preparation.** Compile the graph's repeated routing
   bookkeeping outside rendering and expose buffered SunVox render jobs at their
   existing quantum boundary. Keep the sample-based path as a comparison oracle.
2. **Idle processing.** Extend exact zero-history handling to suitable native
   processors while advancing controls, clocks, random state and grain schedules.
   Never suspend an arbitrary SunVox MetaModule based on input amplitude alone.
3. **Expensive kernels.** Profile actual patches and retain only independently
   validated, sound-preserving improvements with repeatable measurements.
4. **Interface/audio separation.** Remove avoidable audio-device exclusion from
   passive FX Canvas refresh; keep edits transactional and meters coherent.
5. **Independent block scheduling.** Use the buffered jobs from phase 1 for
   bounded parallel rendering, with a serial fallback. Preserve serial graph
   dependencies, latency, worker lifetime and floating-point environment.

The baseline includes the reviewed changes of #200 and #201: those PRs were
merged into their feature-branch bases after #199 reached main. The first phase
carries those commits forward so they cannot be lost during this merge sequence.

## Acceptance

- Compare PCM with the reference on deterministic graphs, including routing
  edits, sidechains, 64-frame feedback, engine replacement and bypass transitions.
- Exercise silence, delayed impulses, tails, automation and re-entry.
- Keep allocations and file I/O out of the audio callback.
- Build and test native Linux and Windows configurations in CI.
- Measure Tree of Life 04 playback and Gallery transitions separately from
  kernel microbenchmarks. Record CPU time, wall-time distribution and memory;
  an offline run is not a physical ASIO/JACK underrun test.
- Include actual interface/controller tests for presentation changes.

Merge from phase 1 through phase 5. Before merging a later PR, retarget its base
to main after its parent reaches main; do not merge it into an already-merged
feature branch. Keep the full chain until the final merge is verified.

## Phase 1: compiled routing and buffered jobs

- The independent pre-change graph path and inline SunVox rendering match the
  compiled/batched path bit for bit across 98,304 stereo frames at 64/256/1024
  frames, including a live quantum replacement, bypass, deletion, parallel
  inputs, sidechains and feedback. Histories, route slews and meters also match.
- Routing, 16-instance stress/lifecycle, profiling and allocation-wrapped SunVox
  tests pass locally (5 targeted tests). Linux/Windows PR regressions include
  the new oracle test.
- Three paired 16-instance routing runs retain both output hashes. Host timing
  is noisy (0.58–1.60 ms/512 reference; 0.58–1.37 ms candidate); the useful
  stable samples suggest a small feedback improvement, not a total DSP claim.
- The main graph still advances sample by sample. This phase moves immutable
  bookkeeping to edits and collects already-buffered work; it does not claim
  a complete block rewrite of the instrument.

## Phase 2: exact empty granular history

- An exact ring occupancy counter allows silent granular interpolation/window
  work to be skipped. Grain births, PRNG, positions, ages, history cursor,
  control smoothing and gain/mix advance normally, including during bypass.
- Forty-second comparisons at 8 and 48 kHz pass against the full kernel, with
  maximum density, pitch/size changes, bypass, tiny impulses, tails and re-entry.
  Every grain state and periodic full ring contents match; output is exact.
  Existing Delay/Reverb parity and the phase-1 graph oracle also pass.
- The optional `tapesister_fx_silence_tests --benchmark` reports hashes and
  silent/active timings. Silent grain work is substantially cheaper; active
  timings must be assessed separately on the target hardware.
- A distortion shortcut was tested and rejected because the active-path cost
  was less convincing. No arbitrary SunVox engine is suspended.

## Phase 3: exact Echo ring spans

- Split the unfiltered SunVox Echo loop at logical ring boundaries, retaining
  read-before-write order and independent dry/wet/feedback arithmetic. Filtered
  paths are unchanged. Both shifted and unshifted right-channel inputs work.
- A separately compiled pristine SDK handler matches output and internal state
  in 117 cases over 8/48/192 kHz and 64/256/1024-frame blocks: one-sample rings,
  wrap boundaries, split events, automation, filters and tails.
- Six paired kernel runs: reference 95–268 ms versus candidate 34–98 ms for
  the same 100,000 blocks. This is a kernel result, not whole-project capacity.
- Velvet, Ensemble and Shimmer hashes match at all three quanta with fixed
  probe seeds. Full-patch timing is noisy; no whole-patch percentage is claimed.
  Linux/Windows DSP CI now also compares the full patches against pristine Echo.
- Preparation remains byte-based and fails closed on changed pinned input.
  Native/default CP1252 encoding and pristine-oracle checks cover Echo too.

## Phase 4: callback-owned Matrix/FX Canvas snapshots

- Passive refresh now requests a bounded snapshot. Audio publishes at the end
  of a callback and never waits for the UI; a READY slot remains immutable until
  the UI releases it. No allocation, painting or row matching occurs in audio.
- A 2,000-publication concurrency test verifies coherent meters and zero UI
  output-lock acquisitions. The existing full FX Canvas controller suite passes.
  Pending controls are invalidated by edits; card IDs prevent reused slots from
  inheriting stale activity. Paused/offline output retains synchronous refresh.
- Actual edits, opening a window and retiring DSP still use their required
  exclusion. This does not claim all UI/audio locking has been removed.
- The callback benchmark supports `TS_BENCH_METERS=1` and fixed SunVox seeds
  (benchmark only) to compare publisher cost and PCM with the window closed/open.
- CI's existing transient controller test caught an undo-snapshot read replacing
  a pending UI edit. Snapshot capture now leaves those controls alone; transient
  state editing and the complete FX Canvas controller test both pass locally.

## Phase 5: optional independent SunVox worker

- `[Audio] audio_fx_workers=0` remains the default. Set it to `1` and restart to
  opt in. A worker is prepared off the audio thread only when the bank contains
  at least two SunVox engines and one sufficiently expensive eligible graph.
  Creation failure, single-core hardware, 64-frame quanta and small jobs retain
  serial rendering. No quantum or saved sound-quality setting is changed.
- Admission recursively inspects the pinned MetaModule engine. Reverb, Delay,
  Echo, Amplifier, Input, Pitch2Ctl, MultiSynth and MultiCtl are audited for
  instance-local rendering state. Unknown modules and filters stay serial;
  filter LFOs can consume process-global random state. SDK builds using denormal
  noise also stay serial. All serial-only engines keep their relative order.
- At an existing quantum boundary, the worker takes one eligible job while the
  audio thread renders the remaining jobs. The audio thread reclaims a job the
  worker has not started. Completion is joined before consuming another frame.
  The floating-point environment follows the caller, including FTZ/DAZ and
  rounding. Profiling results are merged by audio, never written to its counters
  from the worker. Construction, retirement and shutdown remain control work.
- Exact comparisons cover 64/256/1024-frame buffering, independent and serial
  paths, SYBERIA's nested graph, a serial-only Velvet patch, controls, tails,
  nondefault rounding, allocation-wrapped rendering and repeated shutdown.
  The complete routing oracle also runs with workers and Reverb engines,
  including feedback, sidechains, live replacement, bypass and deletion.
  Worker profiling preserves PCM and retains sampled effect identities.
- Three alternating four-Reverb microbenchmarks retained both PCM hashes.
  At 1024 frames, serial render totals were 127–163 ms and worker totals
  116–149 ms; results overlap and are not a guaranteed gain. At 256 frames the
  wake-up cost was less convincing. Admission now scales with quantum size,
  so single-Reverb jobs at 256 frames stay serial. The optional benchmark is
  `tapesister_sunvox_jobs_tests --benchmark`.
- `TS_BENCH_WORKERS=1` opts the callback benchmark in. It reports process CPU
  as well as audio-thread CPU, p95/p99/max callback wall time and dispatch/steal
  counts. Lower audio-thread CPU alone must never be reported as a total saving.
- A worker already rendering must finish; this is not a hard deadline guarantee.
  Scheduling and real-time priority behavior still need target ASIO/JACK hardware
  measurements. Keep the default serial mode on unmeasured low-powered systems.

## Review and merge order

The review chain is #202 → #203 → #204 → #205 → the phase-5 PR. Each child targets
its preceding branch. Merge #202 into main first, then retarget the next PR to
main and merge it after its checks pass. Repeat in order. Do not merge a child
into a feature branch whose earlier version has already reached main.
