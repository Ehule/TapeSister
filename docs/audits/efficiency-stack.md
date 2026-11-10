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
