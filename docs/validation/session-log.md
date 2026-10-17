# Session recorder validation — 2026-10-17

Built the Linux Release SDL application and exercised the production background
recorder. SDL dummy audio and software rendering are available here; Windows,
WASAPI/MOTU and GPU-driver behavior still require the native build and hardware.

## Automated coverage

Seven targeted CTest suites passed: session log, Audio Health, realtime
diagnostics, audio configuration, audio reconfiguration, New project and visualizer
controller. The real Linux executable also initialized, rendered, ran audio and
shut down with a normal 8 MiB desktop stack limit. Its automatically produced log
parsed as JSONL and included setup/device/workload/audio/system records with no
lost records and a normal shutdown record, without opening the Health window.

Recorder tests cover JSON escaping; precise over-budget callback records; device
and buffer epochs; control-hold/wait separation; markers; retained history across
Health resets; asynchronous export while recording continues; post-export events;
concurrent control producers; deliberate audio-queue overflow; explicit loss
counters; storage cap and export after reaching it; an invalid output location;
INI default/disable round trip; retention of eight previous logs; and preservation
of unrelated files. Health integration tests exercise the marker shortcut while
hidden, background setup capture, Save Session and clipboard path completion.

The recorder source and its unit test also passed AddressSanitizer and
UndefinedBehaviorSanitizer. The existing core library/SDL were not sanitizer
rebuilt; this is a targeted recorder check, not a whole-application sanitizer run.
The Health window was rendered and visually inspected with the new controls.

## Playback with edits and visualizer lifecycle

Run serially, with no concurrent builds or synthetic CPU load:

```
build/tapesister_mandala_controller_tests --interact /tmp/session-disabled 4 10 1
TAPESISTER_SESSION_TEST=/tmp/tapesister-session-validation \
  build/tapesister_mandala_controller_tests --interact /tmp/session-enabled 4 10 1
```

The fixture uses 10 active effects, 16 Mosaic notes, ARP, embedded TrackSister and
Prism. Eight four-second measured phases include baseline, pedalboard edits,
steady visuals, visualizer opening/closing, combined edits/cycles, frozen visuals,
and repeated combined/baseline phases. Identical controls settle between phases.

| Measurement | Recorder off | Recorder on |
| --- | ---: | ---: |
| Callbacks in measured phases | 2,189 | 2,207 |
| Mean callback duration | 4.504 ms | 4.381 ms |
| Callback deadline overruns | 7 | 6 |

These short runs do not establish a precise whole-application overhead or show
that logging improves performance. They show no observed slowdown in this pair;
ordinary host scheduling remains a confounder.

The continuous enabled log covered 3,326 callbacks including settling periods,
313 Sister/pedalboard action requests, five visualizer launches/readies/closes/exits,
eight Health resets, 67 setup snapshots and 32 workload samples. All 3,917 JSONL
records parsed successfully. Both loss counters stayed zero. The file was 902,571
bytes. Dummy audio produces processing-plus-wait cadence, so most callbacks have
start-gap observations; these are explicitly not identified as hardware dropouts.
This file size is not a prediction for normal hardware sessions.

A separate isolated Release producer measurement reported 14.8 ns per accepted
packet over 20,000 enqueues (about 0.015 microseconds); the disabled check measured
1.6 ns per call. A one-million-call overflow stress returned without waiting and
reported lost diagnostic packets. Producer timings exclude background writing,
system sampling and UI setup formatting and are not Windows guarantees.

The recorded run used the final recording/event algorithm. Subsequent changes
made the manual-marker acknowledgement reflect queue acceptance and moved its
shortcut ahead of auxiliary-window input handlers; the targeted window/recorder
checks were repeated after those changes.

## Project log location and Windows build follow-up

The native workflow for `5767ae5` passed on Linux but failed to compile on Windows:
the Windows SDK defines `near` as a macro, conflicting with the recorder's window
counter. Renamed that member to `near_budget`; the JSON field stays unchanged.
The user's October 16 21:13:01 build/report predates the recorder (no session
recorder line or new controls). Its `tapesister-diagnostic.log` contains startup,
backend and renderer messages, not the new session history.

Successful project saves now queue a snapshot in `logs/` beside the bundled TSR.
Save As snapshots the new location; manual Save Session follows the current
project. The recovery timeline continues independently. Copies use a temporary
file and replacement, and project-side logs are excluded from auto-log pruning.

The Linux Release application and the same seven targeted suites passed again.
Additional checks exercise:

- UTF-8/space-containing directories, Windows drive/UNC path construction,
  relative paths and escaped folder URLs.
- Project save success/failure, New while a copy is queued, and immediate shutdown
  with two accepted project destinations (Save As).
- Copy failure preserving the prior snapshot and leaving live recording enabled.
- Project exports surviving recovery-directory pruning; unsaved-project fallback.
- Earlier session files surviving complete project-bundle replacement, including
  consecutive saves while an earlier export can still be pending. The writer
  continues draining events while a project save defers export copying.
- Ordinary saves preserving clipboard content, explicit Save Session copying its
  path, and completion arriving before the next UI poll.

The targeted recorder AddressSanitizer/UndefinedBehaviorSanitizer check passed
again. A syntax check with the SDK's empty `near`/`far` macros defined also passed;
this checks the specific collision, not Windows platform execution. Native CI
must validate the final Windows binary and filesystem branches.

No graphics-quality or DSP changes are included in this follow-up. The new user
reports retain historical timing maxima and do not timestamp the audible clicks;
the 2.6-second control hold is attributed to `load_instrument`. The short idle
profile peaks below its buffer deadline while the routed effects continue running.
These observations do not establish or exclude a visualizer/driver scheduling
cause for the remaining clicks.

The bundle-preservation follow-up also passed the dedicated sample-pages,
recorder, New/project-save integration and Audio Health suites, a rebuilt Release
application, and the targeted recorder sanitizer check. Save transactions move
the old `logs` entry into the staged bundle and restore it on publication failure;
if rollback itself fails, staging/backup are retained instead of deleting history.

## Export/shutdown ordering regression

Windows run 197 built successfully and passed the F11 transport integration
checks, but failed the recorder assertion requiring earlier markers in the saved
snapshot. The writer could drain its queues, then observe a newly queued export
request and copy before draining the records preceding that request. A similar
late-observed stop could omit the last queued records on shutdown.

The writer now selects export/stop work before taking its finite queue batch.
A busy event gate defers export/shutdown to the next iteration. Audio and control
producers retain their existing bounded, nonblocking paths; there is no added
audio lock or disk work on those paths.

The recorder test includes the production writer with test-only semaphore hooks
to force these interleavings. Queuing markers/audio and Save Session immediately
after a drain reproduced the exact Windows assertion on the previous algorithm;
it passes with the corrected ordering. Additional deterministic cases cover a
busy event gate after export selection and Stop arriving after the last drain.
Fixture events whose contents are required now check enqueue acceptance, since
production intentionally rejects a contended nonblocking attempt.

Local validation: the Release application built; recorder, Audio Health and
New/project-save integration passed (3/3). The recorder also passed ten consecutive
runs. Real startup/audio/shutdown passed with the 8 MiB stack. Targeted recorder
AddressSanitizer/UndefinedBehaviorSanitizer passed; leak scanning was disabled
because LeakSanitizer could not inspect this environment's process/thread paths.
Native Windows/Linux validation is tracked in PR #147.
