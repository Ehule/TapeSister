# Idle CPU, rendering and DSP scheduling audit

Scope: follow-up in PR #129, based on the HP A8-6500 / Radeon / Mint XFCE /
OpenGL / PipeWire reports. This supplements the ASIO edit-lock work in
[REALTIME_AUDIO_AUDIT.md](REALTIME_AUDIO_AUDIT.md).

## Measurement rules

The supplied screenshots show a large visible/hidden difference. They do not
identify which code consumes that time, establish GPU or PipeWire CPU cost, or
measure callback deadlines. Confirm htop's thread-display mode before adding a
process row to its thread rows. Compare the same sample, configuration, driver,
buffer, window size and build. Release timings must not be inferred from Debug.

Targets remain 5–10% of one core while visibly idle, minimal hidden CPU, no
hidden drawing, and unchanged playback/edit response. The A8, Radeon driver and
PipeWire are not available in the development environment; local results below
are comparison evidence, not a claim that those hardware targets are met.

## Instrumentation

F12 → **START PROFILE** switches Audio Health to a subsystem table. RESET starts
new measurements. COPY REPORT includes both the normal audio-health report and
all profiler rows. STOP PROFILE freezes the profile and returns to normal audio
health; closing/minimizing the window leaves profiling running so hidden-window
measurements remain possible. Profiling is off by default. The optional
`TAPESISTER_PROFILE=1` environment variable enables it at startup.

Each row contains mean milliseconds per call, observed maximum milliseconds,
calls/second, accumulated milliseconds/second and percentage of its parent
(UI work or audio callback). These are **inclusive wall-clock timings**: child
rows overlap parents. Present/swap includes SDL/driver/VSync waiting and is not
CPU time. Event wait is shown separately and has no parent percentage. Compare
OS process CPU separately. Instrumentation itself adds cost.

UI scopes cover controller updates, event/MIDI handling, tracker UI, main paint,
main waveform drawing, waveform analysis/cache, Sister paint, Prism graphics,
EQ response drawing, tracker scope updates, auxiliary windows, texture damage /
upload / copying, presentation and waiting. Audio scopes cover the complete
callback, voices/ARP/Mosaic, tracker, Sister, router, individual router stages,
master EQ/limiter, spatial output and snapshot publication. Main paint includes
other panels and overlays; controller update includes its nested tracker row.
There is no independent main-window spectrum FFT: imported tracker scopes and
Prism graphics have their own rows.

Audio children sample one complete block in 32. Totals and call rates for those
rows are estimates; their peaks are observed samples, **not a deadline audit**.
The existing Audio Health callback/deadline counters still cover every callback.
No allocation, logging, string formatting or new lock enters the callback.
Each lane has one writer and publishes atomic snapshots at block/frame ends.
Reset uses an epoch; readers never wait for either writer. Profiling disables
itself on platforms lacking lock-free publication atomics.

## Findings and changes

| Area | Previous behavior | Current behavior / decision |
|---|---|---|
| Main loop | VSync when supported; otherwise explicit 60 Hz delay. Not an unbounded render loop. VSync alone followed the monitor's rate. | Event-wait scheduling independently caps work; input interrupts the wait. |
| Main paint | Entire 640×400 software framebuffer repainted on every visible loop, plus native-resolution waveform overlays. | Idle 10 Hz, playback/animation 30 Hz, recent input/dragging about 60 Hz; no hidden/minimized paint. |
| Texture/present | Existing tiled damage detection reduced uploads, but unchanged frames still copied/presented. | Unchanged idle frames are not copied/presented. Expose/restore/resize, changed pixels and changed native waveform analysis force presentation. Editing/animation retains its scheduled presentation. |
| Event service | Nonrender polling continued at 60 Hz when minimized. | Unattended hidden service is 10 Hz. SDL events wake immediately. Event bursts deliver input promptly but controller/paint work is capped separately. |
| MIDI | Producer queued notes; UI discovered them during its next polling frame. | Publishing an event also wakes SDL. Full-queue panic semantics remain. Callback is detached before SDL shuts down. |
| Hidden tracker | Once initialized, tracker refresh still called sprite, scope and panel drawing outside the tracker workspace. | Transport/service work is separated from optional drawing; hidden tracker scopes, main raster pass and launcher-panel redraw are skipped. |
| Sister window | Model comparison already avoided some redraws, with a 30 Hz cap. | Retained; hidden windows also skip model/waveform refresh. Visible animated Sister/spatial windows can keep service at 30 Hz independently of main-window visibility. |
| Auxiliary windows | Audio Health already capped at 4 Hz; spatial at 30 Hz. | Retained, with hidden/minimized guards. |
| Waveforms | Existing revision/range caches avoid repeated sample scanning; native overlays still have raster/upload costs. | Cache behavior retained and measured. Main painting/presentation no longer follows the audio callback. |
| Voice banks | Empty banks still traversed normalization/handoff helpers every sample. | Exact-zero, empty, settled banks return immediately. Release/handoff residuals keep processing until finished. |
| Master EQ | All five biquads ran even for exact identity filters. | Prepared active-stage mask skips identity stages only when their state is zero and no edit/fade is pending. Active and bypassed nonidentity filters remain warm. |
| Prism | Disabled processing still advances history, lens phase, smoothers, modulation and Matrix clocks. | Retains these semantics. Skips inaudible pan/energy summation at zero wet and avoids unnecessary floor operations within the current phase cycle. |
| Pedalboard | Some zero-mix/no-history early returns already exist. Control smoothers, grain history and active histories still advance. | No blanket zero-mix bypass: doing so would change engagement and tails. Profiling identifies its remaining control/history cost. |
| Fallout | Already exits when inactive; enabled LFO/preset/feedback behavior can produce sound without a voice. | Retained. An enabled generator is not treated as silence merely because keyboard voice count is zero. |
| Sister/Router | Sister-off still carries grouped keyboard input; router bypass advances wet ramps, timers/sequencer, tails and modulation. | Retained; UI cadence never drives these clocks. |
| Tracker DSP | Imported tracker renders through the host callback, not a second hardware output. | Continues independently of visibility. Stopped initialization/queue bookkeeping remains measurable. |
| Master/spatial | Limiter lookahead, meters, output ramps, spatial morph/LFO/test tone and routing continue per audio frame. | Retained; a silent input is insufficient evidence that the output chain can be paused. |
| Floating point | Very small decaying states can enter expensive subnormal arithmetic on older x86. | Callback enables flush-to-zero/denormals-as-zero on SSE2, restoring the caller's mode afterward. Only values far below audible precision are affected. |

This is an activity-aware scheduling pass, not an unconditional suspension of
the audio device. **No voices is not the same as a quiescent graph**: long delay
returns, Sister tape, feedback, Fallout noise, input monitoring, recording,
reference/spatial tones and timed router states can all require output work.
Prism's existing phase/history continuity also survives bypass. An open stream
still receives callbacks, and this change does not claim zero audio CPU while
idle. Further reduction should use these measured states and explicit engine
quiescence contracts, not an amplitude threshold that can erase a future echo.

## Thread and architecture audit

The main SDL thread owns event handling, editing, render caches and presentation.
Audio transport and modulation run in the output callback; visual redraws do not
advance musical time. Small atomic snapshots already carry meters, routing and
playhead state back to the UI. Some controller operations still use the output
lock; the separate PR #129 edit work reduces long exclusion, and lowering UI
polling also reduces repeated acquisition. A wholesale message-queue rewrite is
not needed to decouple painting from audio scheduling.

JACK's worker blocks on a semaphore. Recording writers sleep 2 ms when their
queue is empty **only while recording/stopping**, and exit when idle. Import and
transform workers are job-driven. The embedded tracker does not launch its
standalone scope thread or its own hardware/Live Link producer. The standalone
Live Link implementation contains a final-deadline yield loop, but it is not the
embedded audio path.

Remaining imported tracker queue handshakes contain short spin waits during
queue clearing; replayer/sample-edit waits exist too. They are not a continuous
idle render loop, and this patch does not replace their synchronization contract.
They remain candidates if tracker UI timings expose a stall. Likewise the OS
backend and graphics-driver threads require on-machine profiling if callback +
UI work fail to explain process CPU.

## Reproduction

Use an isolated configuration path when benchmarking. The diagnostic mode skips
the splash/default sample, performs a one-second warmup, profiles for the requested
2–600 seconds, prints the full report, shuts down and does not save configuration.
`loop` creates a deterministic looping sine; `hidden` hides the main window;
`tracker` initializes the embedded workspace. These keywords can be combined.

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release
cmake --build build-perf --target tapesister tapesister_performance_benchmark
TAPESISTER_CONFIG=/tmp/ts-audit.ini build-perf/tapesister --diagnostic-performance 10 idle
TAPESISTER_CONFIG=/tmp/ts-audit.ini build-perf/tapesister --diagnostic-performance 10 hidden-idle
TAPESISTER_CONFIG=/tmp/ts-audit.ini build-perf/tapesister --diagnostic-performance 10 loop
TAPESISTER_CONFIG=/tmp/ts-audit.ini build-perf/tapesister --diagnostic-performance 10 hidden-loop
TAPESISTER_CONFIG=/tmp/ts-audit.ini build-perf/tapesister --diagnostic-performance 10 hidden-tracker-idle
build-perf/tapesister_performance_benchmark
```

The callback benchmark runs faster than real time, so use its **ms/callback**
and calculated percentage of one core at 48 kHz/512, not its profiler calls/s,
for consumption comparisons. Its software-paint timings exclude a GPU. Normal
application reports use actual wall time and the selected device.

For the A8: compare visible idle, minimized idle, visible looping and minimized
looping with the same configuration. Measure process CPU with PROFILE off,
then enable PROFILE for subsystem attribution. For hidden measurements, start
profiling, minimize **all** TapeSister windows, wait 15–30 seconds, restore and
copy promptly; the brief restoration will add a few UI calls. Separately check
waveform zoom/drawing, cursor/hover feedback, track editing/follow, Prism/Sister
visuals, MIDI notes/CC, FX tails and recordings during hide/restore.

## Validation

Release tests retain assertions in every registered test executable. Several
older fixtures perform setup inside assertions; leaving NDEBUG enabled removed
that setup and made Release test results meaningless. The linked production
library still uses Release optimization.

The focused checks cover profile sampling/averages/reset/freeze, cadence and
tick wrap, real SDL wait wakeup after MIDI queue publication, overflow panic,
visibility and damage/present invalidation, Health profile controls, identity EQ
and edited filter response, held-note/sample-edit continuity, and tracker audio.
The Release suite passes 100/103 tests; the established baseline failures are
`test_sister_source_mask`, `test_sister_recursion` and `tapesister_canvas_tests`.
AddressSanitizer + UndefinedBehaviorSanitizer pass the five profile, MIDI wake,
Audio Health/hidden spatial window, HOLD/controller and master EQ targets
(leak detection disabled). The real RtMidi-enabled C branch also compiles with
strict warnings. Native Windows/MOTU and A8/OpenGL/PipeWire listening and CPU
measurements remain on-device checks.

### Local comparison

Measured in the hosted Linux environment with Release code, SDL dummy audio at
48 kHz/512/stereo, software rendering, no other build/test job running, and
profiling enabled. Baseline is PR #129 commit `619b828` with the same initial
timing instrumentation, before scheduling/DSP optimizations. Callback numbers
are medians of three 1,500-block runs after 100 warmup blocks.

| Actual callback fixture | Before, ms/block | After, ms/block | Reduction |
|---|---:|---:|---:|
| Idle | 0.442 | 0.379 | 14.3% |
| Looping stereo sine | 0.460 | 0.399 | 13.3% |

Full application scenarios use a one-second warmup and five-second measurement.
The table reports wall timing, not OS CPU percentage; its UI totals include all
controller, drawing and driver time. Callback peaks remain in the copied report.

| Scenario | Main paints/s | Presents/s | UI wall ms/s | Mean callback ms |
|---|---:|---:|---:|---:|
| Visible idle | 9.8 | 0.0 | 4.164 | 0.447 |
| Hidden idle | 0.0 | 0.0 | 0.600 | 0.467 |
| Visible loop | 29.8 | 29.8 | 72.127 | 0.496 |
| Hidden loop | 0.0 | 0.0 | 1.207 | 0.520 |
| Hidden, tracker initialized/stopped | 0.0 | 0.0 | 2.313 | 0.460 |

These results confirm the scheduler, unchanged-frame suppression, and hidden
render suppression; they also show that an open idle audio graph still has a
cost. The software renderer's visible-loop raster/upload work remains worth
profiling on the A8. Whole-process startup measurements were excluded from the
steady-idle comparison because they include the five-second splash. No A8 CPU,
GPU, PipeWire or MOTU improvement percentage is asserted by these measurements.


## A8 active Prism/pedalboard follow-up

The October 7 visible-window report (48 kHz, 512 frames, PulseAudio, two held
keyboard voices/ARP, harmonic Prism with 16 lenses and the four-slot pedalboard)
measured a 4.541 ms mean callback over 40.4 seconds. Prism accounted for 38.4%
and Pedalboard for 37.2% of the inclusive sampled callback time. Two callbacks
exceeded the 10.667 ms budget; the retained maximum was 14.227 ms. Crackling was
reported, but this backend does not expose native underrun counters.

On the UI side, waveforms took 114.180 ms/s within Main paint's 138.723 ms/s;
texture/damage/copy took 97.827 ms/s. Waveform analysis itself was only 0.022 ms/s.
This identifies repeated raster/compositor work after the existing sample-analysis
cache. Event wait (678.941 ms/s) is waiting, and tracker/controller timings can
include lock waits; these rows must not be added up as independent CPU loads.

This follow-up changes three paths:

- Cache the native-resolution sample raster as well as its analysis. Source
  identity/data/dimensions, sample and UI revisions, range, display mode,
  palette, selection, padding, and the actual grid/loop background invalidate
  it. Moving playheads restore the underlying native pixels from that raster.
- Compare overlays at logical resolution and upload only the affected native
  rectangle. New textures and source changes still upload fully; an upload
  failure forces texture recreation. Moving live Sister/Mosaic surfaces retain
  their full redraw path. Cache storage is UI-owned and bounded by window size.
- Reuse the pedalboard's exact equal-power sine/cosine coefficients when the
  smoothed mix is unchanged. Prism's glass filters remain primed, while a
  nonlinear color calculation with exactly zero contribution is omitted.
  Smoothers, modulation, period-analysis cadence, stereo, tails and zero-mix
  reverb history continue as before. No audio-thread allocation or new lock.

### Local comparison for this follow-up

Release baseline: merged main `86b5a1c`, with the same extended benchmark added
before either production change. Medians of three alternating before/after
runs in the hosted Linux environment, with no simultaneous build or test job.
The representative patch uses the reported lens count/mixes and defaults for
unspecified settings; it is not a reconstruction of the user's exact project.
Callbacks use 48 kHz/512 stereo. The UI case uses a moving playhead at 1280x1024,
including the first uncached render. It measures software drawing/composition,
not SDL driver uploads or GPU presentation. PROFILE is enabled for both builds.

| Case | Before (ms) | After (ms) | Change |
|---|---:|---:|---:|
| Idle callback | 0.368 | 0.375 | +1.9% |
| Dry looping callback | 0.385 | 0.396 | +2.9% |
| Prism16 + pedalboard callback | 1.057 | 0.984 | -6.9% |
| Same, warmed zero-mix reverb | 1.300 | 1.272 | -2.2% |
| Software paint + overlay per frame | 1.204 | 0.265 | -78.0% |
| Main paint per frame | 0.807 | 0.165 | -79.6% |
| Waveform drawing per frame | 0.722 | 0.083 | -88.5% |
| Native overlay composition per frame | 0.396 | 0.100 | -74.8% |

Idle/dry callbacks show no improvement in these measurements. The principal
local gain is visible waveform rendering; the active DSP gain is modest. These
are not A8 CPU percentages and do not establish that crackling is eliminated.

Validation includes bit-for-bit original/optimized output comparisons for
3,072,000 representative callback frames, plus 784,400 frames from direct Prism
and pedalboard streams at 8/44.1/48/96 kHz with shape/mode changes, wet amounts,
bypasses and silence. The committed pedalboard regression compares cached and
forced-uncached coefficients throughout control changes. Pixel comparisons
cover cached versus fresh native renders, mono/stereo/channel modes, palette,
selection, loop, sample edits, zoom, downscaling/noninteger scaling, and renders
that were not presented. SDL texture readback checks verify partial uploads and
workspace transitions. The formerly Make-only native/workspace fixtures are
registered in CMake/CI and updated for Sister's existing borderless screen-fill
behavior (SDL dummy does not implement native window decorations).

Retest the same saved project on the A8 at 512 frames, visible, after warmup:
RESET, enable PROFILE and collect 40–60 seconds. Compare Waveforms, Main paint,
Texture/damage/copy, Prism, Pedalboard and callback overruns. Then compare the
same project minimized; assess whole-process CPU separately with PROFILE off.
The existing PulseAudio start-gap caveat still applies.
