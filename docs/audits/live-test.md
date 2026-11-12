# Tree of Life 04 live diagnostic

This temporary opt-in diagnostic drives the actual application UI/controller
and audio-device loop. It is separate from the unpaced callback benchmark.
It does not freeze, render, replace, or simplify the project's effects.

## Run

1. Keep the complete original **Tree-of-Life-Live-FX-04** project folder,
   including its `samples`, `project-data` and `sister-state.ini`. The loose
   TSR alone is not sufficient.
2. Close TapeSister. In the active `tapesister.ini`, set:
   ```ini
   [Audio]
   diagnostic_live_test=1
   ```
   The default is `0`. Restart after changing it. Keep the same backend, device,
   sample rate, buffer and output channel configuration for comparison runs.
3. Open `Tree-of-Life-Live-FX-04.tsr`. The title says **LIVE TEST ARMED**.
   Opening the project never starts playback. The test requires its original
   first three movements (53/48/43 cards), 14 enabled effect instances, Binah
   selected, and the Taneb Spring / Grain instances used by this fixture.
   An incompatible project is rejected with a status message.
4. Health recording starts automatically on launch when this switch is enabled,
   even with `session_log=0`. You do not need to open F12 or press Record.
   This is the diagnostic JSONL recording, not an audio WAV recording.
   Press **Mosaic Play** (or Space in Mosaic). Leave the application alone for
   **3 minutes 36 seconds**. **Escape aborts from any application window.**
   Ctrl+Shift+F12 can still mark an audible click without invalidating the test.
   Other keyboard/mouse edits, MIDI input, minimizing/closing a window, or
   changing the audio device invalidate the controlled run.
5. On completion or abort, the session log is automatically exported to the
   project's `logs` folder. The status changes from **EXPORT QUEUED** to
   **LOG SAVED IN PROJECT logs** after the background save finishes. The Audio Health recorder continues to retain its
   recovery file. If export fails, use F12 / Save Session.
6. Reload the original project to repeat. Set `diagnostic_live_test=0` and
   restart to return to normal use.

The diagnostic's changes stay in memory. Project saves and configuration saves
are blocked for the rest of a process that has armed the test, so temporary
routing/parameter/view changes cannot overwrite the baseline or become startup
settings. Restart before doing ordinary editing/saving. This deliberately small
diagnostic is not a general-purpose project scripting system.

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
run of that same mode and project. Mode `1` retains the original 240-action,
3:36 `tree04-live-v1` schedule unchanged, for comparison with the existing
transition log. Restart after switching modes; use `0` for normal editing.

## Versioned scenario: tree04-live-v1

All three populated movements are prepared at 300 seconds, with repeat off,
Gallery entry set to Resume, and an eight-second Gallery crossfade. A fixed
card identity in each scene is selected before playback for mute/solo; the
selection does not depend on which action happens to be late.

| Time from Play | Work |
| --- | --- |
| 0–20 s | Binah warmup, profiler off |
| 20–80 s | Binah control/window exercise |
| 80–140 s | Gallery transition to Keter and the same exercise |
| 140–200 s | Gallery transition to Chokhmah and the same exercise |
| 200–216 s | Return to Binah, allow the crossfade/tails, stop |

Each exercise opens FX Canvas, sweeps the Taneb Spring mix and native Grain
size at 10 updates/second, restores those parameters, mutes/unmutes and
solos/unsolos a card, bypasses/re-enables Taneb Spring, visits the embedded
tracker, switches between Matrix and FX Canvas, opens/closes Audio Health,
and returns to a steady playback period. The core controllers used for manual
operation handle the edits and Gallery transitions.

The profiler is disabled during the run to avoid conflating its overhead with
the scenario. Its prior state is restored at the end. The session recorder and
callback telemetry are required and enabled independently of profiling.

## Evidence and comparisons

The JSONL log records:
- scenario version, intended action count, loaded-project fingerprint;
- audio backend, actual callback format, worker setting, Git revision and build timestamp;
- each action's planned time, actual time, lateness, audio frame counter,
  execution duration and success/failure;
- counter checkpoints and the usual callback windows, control-lock timings,
  device changes, workload snapshots and system measurements;
- completion or explicit abort, including lost telemetry or failed actions.

Deadlines are absolute relative to Play. Late actions are not silently
rescheduled. At most one action is dispatched per UI iteration; backlogged
actions retain their original planned times. The script is repeatable; OS
scheduling and modulation/random state are not promised to be sample-identical.
Reloading restores project/DSP state, but waiting before Play and host scheduling
can still change evolving effects. Intentional mute/bypass edits are not
automatically classified as glitches.

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
with profiling off to compare deadline performance. Modes 1 and 2 retain their
existing schedules. All modes automatically record/export the health log and
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
