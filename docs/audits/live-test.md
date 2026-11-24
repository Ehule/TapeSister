# Loaded-project Gallery diagnostic

## Adaptive full playback and stress test

Use the new mode for a comprehensive diagnostic of the loaded Gallery:

```ini
[Audio]
diagnostic_live_test=4
```

Restart TapeSister, open a `.tsr` project, and press Mosaic Play after the test
is armed. The generated plan and its estimated duration appear before playback.
This mode is `gallery-stress-v1`; mode 1 below retains the shorter screening
schedule for comparisons with existing logs.

Mode 4 discovers the actual Gallery contents. It exercises full playback at
the saved movement speeds, real incoming/outgoing crossfades, dense passages
with FX Canvas closed and open, overlapping scenes, and transport recovery.
Full playback preserves the buildup of stateful effects. Its duration therefore
depends on the music: a long album is a long test, and slow playback extends it.
The full-play stages disable detailed profiling; the stress stages enable it
to identify processing costs separately from the less-instrumented playback.

The test generates its own arrangement to cover the loaded movements rather
than replaying the saved Gallery sequence order. It retains sound parameters,
source routes, card mute/solo choices and the loaded assets. Diagnostic transport
changes and generated sequences cannot be saved. Reload the project afterward;
set `diagnostic_live_test=0` and restart to return to ordinary editing.

Crossfade coverage is measured in the audio renderer. The log records the two
scene identities, the requested/effective fade, frames rendered with both scenes
active, progress, and normal or intentional interrupted retirement. This covers
fades shorter than a UI update or a one-second logging window. Missing sources,
lost trace records, incomplete playback and failed state checks are reported;
an action returning success is not sufficient evidence that a fade was tested.
Escape aborts and exports the partial result.

`live_test_stage` begin/end records provide counter differences for each job,
including callback deadline overruns, near-budget callbacks and arrival gaps.
`live_test_transition` records prove overlap progress; their counts do not
pretend to assign every callback deadline to an individual sub-buffer fade.
`live_test_coverage` records whether the full-play and transition requirements
were met. The comparison tool exposes these as `stages` and `stress_coverage`
and reports `deadline_status` separately from successful scenario completion.

```sh
python tools/compare_live_test_logs.py path/to/session.saved.jsonl
```

The report includes the actual buffer deadline, peak callback time as a fraction
of that budget, timing events by movement/state, system CPU and memory, and any
explicit schedule adjustments required to wait for audio-clock progress. It
orders asynchronous records by their timestamps and excludes callbacks that
cross the measurement boundaries. One-second window averages are never reported
as individual-callback percentiles. Profiling, OS scheduling and external input
can affect a run; silence or lack of an external signal is not evidence of
successful audible playback. A completed test with deadline misses has reached
an unstable operating point at those settings, even if most callbacks fit.

No CPU model, project filename, movement title or particular effect identity
is privileged by this mode. Its purpose is to expose the loaded workload's
limits and recovery behavior on the current hardware, while retaining the
musical processing that creates that workload.

Dense regions are selected by overlapping audible note slots, a structural
heuristic rather than a claim to have measured the CPU-heaviest instant. The
closed/open samples use the same source positions, but effects history carries
between stages: hard stop freezes processing rather than recreating each effect.
These samples describe both viewing conditions; they are not a strict isolated
test of drawing cost. Dense overlap also exercises a sustained power fade and a
separate one-millisecond smooth fade. Where the sections are long enough, the
test pauses/resumes an active fade, rejects a competing switch, interrupts a
fade with hard stop, then verifies playback recovery. Short sections explicitly
report unavailable mid-fade interactions. An all-empty Gallery reports
`no_playback_workload`, and nonempty sections below the native sequencer's
one-millisecond minimum are reported as unsupported rather than silently passed.

## Quick Gallery screening

Set this in the active `tapesister.ini`, under `[Audio]`, then restart TapeSister:

```ini
[Audio]
diagnostic_live_test=1
```

Open any TapeSister project (`.tsr`). The diagnostic scans its Gallery and shows
**GALLERY TEST ARMED**, the movement count, and estimated duration. Press Mosaic
Play or Space to start. Loading arms the test; it does not start playback.
Names, filenames, effect IDs, movement lengths and card counts are unrestricted.
A project with one canvas is a one-movement test. This is a project diagnostic,
not analysis of an arbitrary WAV opened in the sample browser.

The plan visits every Gallery canvas in order. A nonempty movement takes about
67 seconds, plus 11 seconds if it has usable markers. Empty canvases take
12 seconds each. There are about eight seconds of initial idle measurement.
For ten nonempty movements with markers, the estimate is **13:08**.

Each populated movement samples the beginning, midpoint and late timeline,
opens/closes FX Canvas, and jumps to an existing marker when available. It
measures playback, Space-paused tails, and Backspace-stopped engine load, and
verifies the double-Backspace return to zero. Empty movements are identified in
the log and get a stopped-engine check without playback assertions. The report
inventories sample, pattern, live and video cards, missing source references,
markers, duration, saved speed, and the project's Matrix effect count.

The existing sound parameters, card mute/solo states and routing are retained.
For repeatable screening, playback temporarily loops each complete movement at
its saved speed; marker loops and grid snapping are disabled, and Gallery entry
is Resume with no crossfade. The Gallery sequencer is stopped. These temporary
transport/view changes cannot be saved: reload the project after the test. This
screening does not assess live crossfades, sequence order, every point on the
timeline, or the full uninterrupted buildup of stateful effects.

Audio Health recording and interval profiling start automatically. Logs export
to the loaded project's `logs` folder after completion or abort. Escape aborts;
other manual input aborts before allowing edits. Lost telemetry or a changed
audio device makes the test incomplete. The previous profiler setting is
restored. Set `diagnostic_live_test=0` and restart for ordinary editing.

`tools/compare_live_test_logs.py path/to/session.saved.jsonl` summarizes the
export, including separate named movement results and callback timing grouped
by playback state and FX Canvas visibility. Only complete measurement windows
after two seconds of settling are attributed to a phase. No hardware-specific
CPU threshold is imposed. Profiling overhead is included; timings alone do not
establish that the physical audio output was click-free.

The active INI is the `TAPESISTER_CONFIG` override when set, otherwise the first
`tapesister.ini` found in the working directory, beside the executable, or in
its parent directory. The shipped example now places the flag under `[Audio]`.

Options 2 and 3 retain the original full Keter tests below. Earlier versions of
option 1 used fixed Tree04/Angelic Noise schedules; current option 1 always uses
`gallery-live-v1`, including for those same projects.

## Uninterrupted Keter test

Use `diagnostic_live_test=2` to test the reported clicking later in Keter.
Open the same original Tree-of-Life-Live-FX-04 project. The diagnostic selects
Keter through the normal Gallery controller while stopped, then announces
**KETER FULL TEST ARMED**. Press Mosaic Play once.

This mode plays Keter from **0:00 through its saved end**, followed by eight
seconds for the tails (10:08 for a ten-minute movement). It does not seek into
the ending: the earlier playback builds the real effect history. There are no
scripted parameter or window edits during this run; checkpoints every 30 seconds
let the log show how the load changes, including the final three minutes.
Automatic health recording/export, profile-off measurement, Escape abort and
save protection work as in mode 1.

The log identifies this as `tree04-keter-full-v1`. Compare it with another
run of that same mode and project. Current option `1` uses the discovered
Gallery schedule and is not a matched control for old fixed-scenario logs. Restart after switching modes; use `0` for normal editing.

## Evidence and comparisons

The JSONL log records:
- scenario version, intended action count, loaded-project fingerprint;
- audio backend, actual callback format, worker setting, Git revision and build timestamp;
- each action's planned time, actual time, lateness, audio frame counter,
  execution duration and success/failure;
- counter checkpoints and the usual callback windows, control-lock timings,
  device changes, workload snapshots and system measurements;
- completion or explicit abort, including lost telemetry or failed actions.

In screening and legacy modes, deadlines are absolute relative to Play. Late actions are not silently
rescheduled. At most one action is dispatched per UI iteration; backlogged
actions retain their original planned times. The script is repeatable; OS
scheduling and modulation/random state are not promised to be sample-identical.
Reloading restores project/DSP state, but waiting before Play and host scheduling
can still change evolving effects. Intentional mute/bypass edits are not
automatically classified as glitches.

Mode 4 waits for actual audio-clock progress before ending a playback stage.
It logs every resulting schedule shift and retains original planned times and
dispatch lateness, so an overloaded host cannot earn full-play coverage by
simply reaching a wall-clock timeout. A stalled audio clock aborts the test.

```sh
python tools/compare_live_test_logs.py before.saved.jsonl after.saved.jsonl
```

The comparison refuses to call runs matched if the scenario, project
fingerprint, audio configuration/output, reported platform/CPU count/RAM,
planned actions or successful completion
differ. Matching these fields does not prove identical hardware or background load.
It uses in-run counter differences, not session-wide maxima or startup
overruns. It reports lateness separately from callback overruns. Its reported
peak comes only from complete audio windows inside the run.

A completed replay means the scenario executed, **not that audio quality passed**.
Require fewer deadline misses, acceptable worst spikes, preserved musical
behavior, and physical-interface listening before claiming a performance fix.

## Developer integration

`tapesister_live_test_tests` supplies fast controller/gating regressions.
Its optional `--run PROJECT.tsr` mode enters the full, canonical application
main loop, presses Play through the normal SDL event route, and exits when the
scenario completes or aborts. Supply an isolated `TAPESISTER_CONFIG` with the
diagnostic enabled. It does not alter the normal executable's startup behavior.

SDL dummy video/audio can verify dispatch, painting, concurrent callbacks,
logging and completion where no display/audio interface is available. Label
such runs accordingly; they cannot certify ASIO/JACK or physical GPU/USB timing.
Run timed comparisons without builds or other controlled load changing between
the two measurements.

## Profile the full Keter buildup

Set `diagnostic_live_test=3`, restart, open the same original project, and press
Mosaic Play after **KETER PROFILE TEST ARMED**. This uses exactly mode 2's
uninterrupted playback and 30-second checkpoints, including the full buildup.
The JSONL identifies `tree04-keter-profile-v1` and `profile=1` and contains a
`live_test_profile` report for every interval and the final tail (or partial
interval on abort). Each report includes elapsed run boundaries, the callback,
Mosaic/voices, router, native effects and individually named SunVox instances.
Nested rows overlap; do not add them together. Only profiling counters reset at
checkpoints; audio state, source positions, effect histories and transport do not.
Profiler epoch publication can straddle one callback at an interval boundary.
The previous profiler on/off state is restored when the run ends or aborts.

Profiling adds timer/publication overhead. Use mode 3 to locate costs, then mode 2
with profiling off to compare deadline performance. The full Keter schedules remain unchanged. All modes automatically record/export the health log and
protect the loaded project from saving the temporary diagnostic state.

The offline callback probe also accepts `TS_BENCH_INTERVAL=30`, emitting per-
interval callback CPU and wall time, plus interval profiles when
`TS_BENCH_PROFILE=1`. Set `TS_BENCH_GALLERY=0` to select Keter and render from zero
through 608 seconds. To match mode 2, use a disposable project copy with the
same preparation: repeat and marker loops off, speed 1, and Gallery fade 0.
The original saved project has repeat on; leaving it on restarts Keter during
what should be the final eight-second tail. This executes the canonical callback with deterministic
SunVox benchmark seeds, without the physical driver or UI; it cannot certify
MOTU/ASIO deadlines. PCM hashing covers the complete render including warmup.
