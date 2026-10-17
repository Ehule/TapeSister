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
