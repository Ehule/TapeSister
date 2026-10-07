# Audio engine audit after PR 119

Baseline: `62e9933f2abb2e844a5554172cbf39973f7eb916` (merged PR 119).

The reported case was two held notes with ARP and slot sequencing, the pedalboard,
then Sister Machine, without an external Insert. Mouse keyboard clicks and drags
were subsequently included. The audit traced the shared callback, note/performance
banks, ARP, parameter publication, pedalboard, Sister, Router and output path.

## Confirmed problems and changes

| Finding on main | Change |
| --- | --- |
| Ordinary keyboard key-up removes a voice instantly. Adding/removing voices also changes shared note normalization in one sample. | A bounded 5 ms residual handoff smooths the mixed discontinuity. Held voices retain their positions; released voices relinquish ownership immediately. Sample, FM, dry recording and group/Sister buses use the same policy. |
| Moving ARP Gate behind elapsed note time immediately cuts its signal; reopening the gate can jump upward. | Gate edits slew over at most 5 ms while retaining note identity and sequencer clocks. Ordinary note boundaries retain their existing fade. |
| FM Output trim changes gain in one sample. | One audio-clock gain slew serves the monitor and dry/capture taps. |
| Publishing an unrelated Sister parameter restarts unfinished level, source-gain and filter ramps. Fast edits can prevent them from finishing. | Unchanged ramp targets retain their progress; filter coefficients are rebuilt only when filter parameters change. |
| Powered-off Sister publishes its full atomic UI snapshot for every sample. | Reuse block-level publication, including powered-off and failed-runtime paths. Single-frame API users still receive a fresh snapshot. |
| Audio readers scan the full 69-note and 384-performance-voice pools even when empty or nearly empty. | Track the highest occupied slot, observe releases once, then shrink the render range. Voice limits and stealing rules are unchanged. |
| Repeated exponentials, gain conversions and normalization in the sample loop. | Cache sample-rate smoothing coefficients, unchanged makeup/density targets, settled reverb/distortion coefficients, transition frame counts and note normalization. Recalculate at the same audio rate whenever an input actually changes. |

The short handoff stores stereo values, not pointers into a released sample. It
requires no allocation, locks, queues or added audio buffer. A continuing waveform
is not low-pass filtered. Stop/Clear remain immediate; ordinary key-up receives the
short de-click. Cold note onset retains the configured attack, including zero ms.
Stereo relationships, effect tails, modulation clocks, nonlinear processing,
interpolation, sample rate, polyphony and device buffer settings are retained.

This reduces callback work and known signal discontinuities. It does not measure
or change Windows driver latency, scheduling jitter, or the interface's minimum
stable buffer size. The existing preallocated DSP histories remain available for
immediate effect changes. The small new handoff/cache fields trade a few kilobytes
of state for fewer calculations and allocation-free transitions.

## Regressions and sound checks

`test_realtime_edits` tests 44.1, 48 and 96 kHz, fast 33-note press/release sweeps
with another note held, sample and FM routes, group/Sister release, repeated ARP
gate edits, ramp completion during unrelated edits, and powered-off snapshot
batching. Each of its five regression groups fails against the baseline.

At 48 kHz, the constant-signal gate test reduces the maximum edit step from 0.30
to 0.00125003. The 44.1 kHz keyboard sweep reduces its maximum step from 0.12426406
to 0.00325787. Group release becomes continuous at the event boundary and settles
to exact silence. These controlled signals expose discontinuities; they are not
claims about peak differences between arbitrary musical samples.

`tapesister_realtime_controller_tests` invokes the actual callback with two held
FM notes, an ARP and its outer slot sequencer, without Insert. The trim regression
reduces the maximum dry-tap edit step from 0.21213204 to 0.00088389 at 48 kHz.

A 192,000-frame stereo stress render using the existing Sister benchmark (three
grain slots on all heads, reverb, feedback and Fallout) was compared sample for
sample against main. All 1,536,000 output bytes matched. SHA-256 for both renders:
`f4ee5667a6c28559f90f70654b37830b79c28293dc12f90b70effcca5b660453`.
The rendered comparison isolates the calculation caches; audible note/gate/trim
transitions intentionally change.

Local Release builds use GCC 13.3, `-O3 -UNDEBUG`, with strict floating-point
semantics and assertions active. The application builds. The full suite passes
87/90 tests. These three failures also reproduce on unmodified main:

- `test_sister_source_mask`: old expectation that repeating one trigger leaves four voices.
- `test_sister_recursion`: old overlap/voice-count expectation.
- `tapesister_canvas_tests`: boundary-resolution assertion at line 180.

Capture and routing fixtures now assert the new short transitions and exact
settled values. The low-level stereo fixture explicitly registers its manually
installed voice. Core edit regressions and the Mosaic controller pass with
AddressSanitizer/UndefinedBehaviorSanitizer (leak checking disabled because this
execution environment cannot run LeakSanitizer). Native Windows/Linux release CI
includes the new regressions and relevant DSP/keyboard/capture tests.
The modulation and post-FX regression files explicitly retain assertions in
Release builds: their existing fixtures perform initialization inside assertions,
so `NDEBUG` previously removed both setup and validation.
The post-FX fixture also needs the same Windows stack reserve as the application:
one existing three-runtime test uses over 2.5 MB, beyond MinGW's default 2 MB.
Its crash reproduces locally with a 2 MB stack and passes with the larger reserve.

## Repeatable callback benchmark

Build `tapesister_realtime_controller_tests`, then run:

```sh
build/tapesister_realtime_controller_tests --benchmark 0 2000
build/tapesister_realtime_controller_tests --benchmark 1 2000
build/tapesister_realtime_controller_tests --benchmark 2 2000
```

All scenarios run the actual 48 kHz stereo callback in 256-frame blocks after
100 warm-up blocks. They retain two held notes and the inner/outer ARP sequencers.
Scenario 0 is dry; 1 adds distortion, delay, reverb and grain; 2 also routes the
notes into powered, monitored Sister Machine. No Insert or physical device is
used. Compare median callback time across alternating baseline/current runs on
the same machine. Timing is diagnostic rather than a flaky CI pass/fail gate.

Local Linux measurements (microseconds per 256-frame callback), using the median
of three alternating runs of 2,000 callbacks per build:

| Scenario | Merged main | This change | Less callback time |
| --- | ---: | ---: | ---: |
| Two notes + inner/outer ARP | 1,191.436 | 332.508 | 72.1% |
| Plus four-effect pedalboard | 1,265.899 | 550.682 | 56.5% |
| Plus powered, monitored Sister | 1,143.138 | 793.373 | 30.6% |

Each scenario produced the same nonzero output checksum in both builds. This is
a shared virtual Linux host with occasional scheduling outliers; these numbers
are comparative CPU evidence, not a Windows latency guarantee. Do not compare
absolute costs between scenarios: powering Sister changes which runtime path
executes. No builds or other test jobs ran during these timing measurements.

## Windows listening check

Use the same audio backend, sample rate, buffer and project as the report. Hold
two notes and start the ARP/slot sequencer, with Insert off. Sweep Gate and FM
Output; click and drag across both note keyboards, including rapid reversals and
release outside the piano. Repeat with the pedalboard and then Sister Machine
powered on. Move Sister and FX sliders while a long ramp is in progress. Held
notes, arp clocks and existing tails should continue. Check ordinary Sustain and
HOLD behavior, dry recording, and Stop. Hardware listening remains necessary to
confirm the user's crackle is resolved on their Windows machine.

## ASIO edit contention, October 2026

The MOTU report at 44.1 kHz / 1,024 frames / four outputs had no DSP deadline
overruns, but 21 ASIO control-lock skips and a 65.237 ms `lock_edit` hold. The
native adapter uses a nonblocking mutex attempt and emits a silent period on
contention. Even a short UI lock can therefore cause a discontinuity; a fast
callback average does not rule this out. The report cannot attribute every
audible click to a particular operation.

Warp, Smear and Tear now prepare stable Current/Parent copies for ordinary
keyboard/MIDI voices on the UI thread. A short locked pointer swap lets those
voices continue advancing during gesture setup, rendering, commit and cancel.
The final locked publication restores editor references and uses the existing
note-bank residual fade when the sound changes. Copies are shared by notes
using the same source, and freed on the UI thread after publication. No output
queue, extra device latency, waiting or allocation was added to the callback.

Unchanged HOLD/PLAY VIEW refreshes now skip synchronization, while changes to
sample, tuning, range or loop metadata still update the voices. Snapshot
retirement also avoids the device lock when there are no snapshots to retire.
Other UI polling and edit paths still take locks. Standalone audition, grouped
keyboard playback and allocation failure retain the previous safe exclusion
path; this is a focused reduction in contention, not a claim of zero dropouts.

`test_edit_audio_continuity.inc` runs the actual callback on another thread
after a render replaces Current, before publishing the edit. It checks lock
availability, non-silent output, advancing positions, pointer lifetime and the
handoff ramp, plus failed rendering and native gesture commit/cancel. The idle
test checks 120 unchanged HOLD refreshes with no lock acquisitions and verifies
that view and tuning changes still synchronize. The ordinary controller suite
and an AddressSanitizer/UndefinedBehaviorSanitizer build cover this path.

For the Windows listening check, retain MOTU M Series, 44.1 kHz, 1,024 frames
and four channels. Reset Audio Health, hold two ordinary sample notes, leave
the UI untouched, then exercise Warp, Smear and Tear. Compare control-lock
skips and long holds with a separate idle-only run. This environment cannot
verify the physical M6 driver or speakers.
