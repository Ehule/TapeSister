# Headless pedalboard and visualizer interaction probe

The user's requested pathological sequence was run: dense audio continues while
pedalboard controls change and the real visualizer process repeatedly opens and
closes. The audio-driven graphics guard has been reverted. Application source is
`2dd2847`, identical to the previously tested `71b2c7b` tree; this diagnostic adds
only an opt-in test mode and an analysis script.

## Result

Deadline overruns were reproduced, but there was no repeatable increase linked
to visualizer open/close requests in these runs. Across 31,249 measured callbacks,
4,673 real controller edits and 39 measured open / 27 close requests:

| Callback timing | Overruns / callbacks | Rate |
| --- | ---: | ---: |
| Overlapping the 250 ms after an open/close request | 3 / 1,092 | 0.275% |
| Outside those windows | 80 / 30,157 | 0.265% |
| Overlapping a wider 1 s window | 9 / 4,316 | 0.209% |
| Outside the wider windows | 74 / 26,933 | 0.275% |

These are descriptive results with few overruns, not evidence that the Windows
report is impossible. Occasional long callbacks also occurred with graphics
closed or frozen. The largest measured control hold across all runs was 1.689 ms,
below the 10.667 ms buffer duration. No control hold exceeded the buffer budget.

Completed-callback overruns in each 20-second phase:

| Phase | 10 FX, 16 Mosaic notes | 20 FX, 16 notes | 20 FX, 48 notes |
| --- | ---: | ---: | ---: |
| Playback only, visualizer closed | 2 | 1 | 8 |
| Pedalboard edits only | 1 | 2 | 11 |
| Visualizer continuously open | 2 | 3 | 7 |
| Visualizer open/close cycles only | 0 | 2 | 3 |
| Pedalboard edits plus open/close cycles | 0 | 3 | 1 |
| Pedalboard edits, visualizer frozen | 0 | 9 | 8 |
| Edits plus open/close, repeated | 3 | 1 | 3 |
| Playback-only baseline, repeated | 1 | 10 | 2 |

Mean callback times ranged from 4.22–4.58 ms, 5.18–5.63 ms and 5.87–6.29 ms
respectively. The heavier pass approached the deadline in its upper tail (the
pedal-only 99th percentile was 10.58 ms) but its mean remained below the roughly
70% load in the user's Windows report. Do not interpret lower counts during
some visual phases as a performance benefit; the baseline repeats show jitter.

Full run summaries are in [mandala-interaction-stress.txt](mandala-interaction-stress.txt).

## Workload and measurements

- Linux Release build, SDL 2.30 dummy audio/video, actual software-rendered
  visualizer child, 48 kHz / 512 frames / four output channels. Nine CPUs were
  available to scheduling, with an eight-core cgroup CPU quota. No extra browser
  or screen encoder was added during this component-isolation test.
- Sixteen simultaneous looping Mosaic cards; one or three notes per card.
  ARP sequences three notes, embedded TrackSister plays four pattern channels,
  and Prism uses eight lenses. Sister, Fallout and the unified routing matrix
  remain active. All 10 or 20 pedalboard slots process POST effects, cycling
  reverb, delay, distortion and grain.
- Real `sister_apply_action` controller calls adjust slot mixes and transition
  times approximately 20 times per second. Effect types stay fixed so topology
  replacement does not change the DSP workload between comparisons.
- Open/close requests alternate every three seconds. The continuously open
  phase also exercises mature animation. Each phase has a two-second settling
  interval beforehand; the music transports continue throughout the sequence.
- Per-callback and action traces use the same performance-counter clock.
  Callback durations and the actual Audio Health telemetry are both collected.
  Trace files are written after audio stops. The final heavier run verifies that
  embedded TrackSister, Mosaic and ARP are still running.
- Health snapshots are read just after each completed-callback measurement
  window, so they can include an additional in-flight callback. The table uses
  the precisely delimited callback traces. Health's tracker state currently
  describes the legacy tracker; the fixture checks embedded TrackSister directly.

## Limits

This reproduces callback deadline misses, not a confirmed audible hardware
dropout. SDL dummy output does not provide hardware underrun counters, and its
callback cadence differs from a device driver. Start gaps are therefore not
counted as dropouts. The fixture exercises controller edits and the visualizer,
not the complete main-window desktop painting/compositing workload.

The software renderer used its **pre-existing render-time** reduction in this
environment. The rejected audio-driven guard is absent. Windows GPU initialization,
WASAPI/MOTU scheduling, Firefox and screen recording are not reproduced. The
correct next comparison remains the same Windows project with those background
applications closed, then reintroduced individually, without changing the art.

## Reproduce

Build the application and `tapesister_mandala_controller_tests`. The default
controller test still runs its normal assertions and passed after the harness
was added. Stress timing counts are reported, not CI pass/fail thresholds.

```sh
build/tapesister_mandala_controller_tests --interact /tmp/mandala-10 20 10 1
build/tapesister_mandala_controller_tests --interact /tmp/mandala-20 20 20 1
build/tapesister_mandala_controller_tests --interact /tmp/mandala-48 20 20 3
python3 tools/analyze_mandala_interaction.py /tmp/mandala-48
python3 tools/analyze_mandala_interaction.py /tmp/mandala-48 --window-ms 1000
```

Arguments after the prefix are seconds per phase (4–60), active slots (5–20)
and notes per Mosaic card (1–5). The prefix's directory must already exist.
Each run writes `-callbacks.csv`, `-events.csv` and `-health.txt`. Run comparisons
serially; compiling or running a second stress process concurrently would add
an uncontrolled workload.
