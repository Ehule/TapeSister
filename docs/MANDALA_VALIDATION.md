# Mandala validation — 12 October 2026

Implementation base: `ae596039` (main, PR #136 merged). No open PR was found at
start. Branch: `feature/impermanent-mandala`.

Environment: Linux x86_64 container, AMD EPYC 9V74 host, 8 CPU quota, 8 GiB memory
limit; GCC 13.3, Release/O3, SDL 2.30 software renderer with dummy video/audio.
There is no physical sound card, desktop window manager or projector here.

Automated feature checks passed:

- Project identity/config round-trip and rename, explicit reseed, distinct new
  identities, legacy migration, invalid fields and unknown version rejection.
- Four-slot queue capacity/drop behavior, unsigned index wraparound and disabled
  producer; anti-phase stereo, duplicated 16-channel output and silence floor.
- Actual child launch, freeze, SIGSTOP stall, SIGKILL termination, repeated
  close/reopen, unchanged routing and continuing TrackSister/Mosaic/ARP playback.
- Close also retires a suspended child within the bounded timeout. External-only
  matrix input reaches the visual tap in stereo/four-channel output; setting the
  final output fader to zero produces zero visual samples.
- Native Performance Transport reseed changes the saved-state fingerprint.
- Renderer fullscreen/escape events and missing-display/topology fallback with
  SDL dummy video, plus texture recreation after a device reset (physical
  display behavior remains unverified).

On code revision `b79c6206`, both Linux and Windows release CI passed all 53
selected audio/UI/mandala regression tests, the separate package guard, and
portable archive assembly with bundled CDP8. The CI full-suite step is skipped
for pull requests; this does not claim every test passed in CI.
[Workflow and downloadable archives](https://github.com/Ehule/TapeSister/actions/runs/38321578180).
After the soak, the final local Release build and all eight focused mandala,
project-state, routing, Audio Health, diagnostics and rendering checks passed.

Initial software prototype: 1,558 instances, 960x600 retained texture,
1,800 diagnostic frames, average 6.027 ms / maximum 44.285 ms. Adding filled
facets and a cached halo sprite gave 8.763 ms average / 20.235 ms maximum in a
second capture run. These are frame-render measurements in this container, not
real-time audio acceptance or older-hardware predictions. Model storage was
97,112 bytes; the Standard history texture was 2,304,000 bytes, plus SDL/window
buffers. No GPU memory figure is inferred from process RSS.

Captures in `screenshots/mandala/` are from the actual software renderer using
synthetic diagnostic audio features. They are not the supplied concept image.

The full local suite ran 114 tests. After correcting an executable permission
on a local build artifact, 108 pass. Six failures also reproduce on an isolated,
unmodified `ae596039` build: `test_sister_source_mask`, `test_sister_recursion`,
`test_sister_ui_model`, `test_sister_source_ui`, `test_sister_capture_ui`, and
`tapesister_canvas_tests`. They are pre-existing and outside this feature.
Increasing the stack limit did not change the three UI-fixture crashes.

The concurrent queue test completed 100,000 publications under a deliberately
slow/stalled reader, without torn snapshots or producer backpressure. The
100,000-call producer microbenchmark (including clock overhead) measured:

| Path | Mean | p99 | Maximum |
| --- | ---: | ---: | ---: |
| Disabled | 26.4 ns | 31 ns | 75.5 µs |
| Full queue / drop | 29.1 ns | 31 ns | 228.7 µs |
| 64 frames × 16 channels copied | 492.9 ns | 491 ns | 133.4 µs |

The large isolated maxima include container scheduling; they are reported,
not hidden by the sub-microsecond median/mean. No hard worst-case latency is
inferred from these measurements.

## Sustained comparison

The same fixture/build ran six four-minute measurement periods, each after a
two-second warm-up, in OFF/Economy/Standard/Standard/Economy/OFF order. No musical
transport reset occurred between periods. The workload used 48 kHz, 512-frame
buffers, four physical output channels, TrackSister, 16 looping Mosaic events,
ARP, Prism, Sister Machine, delay/reverb, Fallout, a four-minute Pedalboard
transition and synthetic external input through a routing matrix.

The sustained-run binary is revision `0bcbe4ff`; subsequent changes at
`b79c6206` add bounded child retirement, graphics-reset recovery and more
aggressive visual overload reduction. The audio callback and ordinary
Economy/Standard rendering paths are identical. The final revision also passed
the platform CI checks above. This distinction avoids attributing a long run to
a binary that was not actually measured.

Physical underruns cannot be measured by SDL dummy audio. Callback deadline
misses below mean measured execution above 10.667 ms. Arrival gaps above 16 ms
are recorded separately. The dummy backend's wall period includes callback
execution, so 24 minutes of process lifetime does not mean 24 minutes of audio
timeline playback. Results characterize this container, not a sound device.

The harness completed in 1,460.1 seconds including exit polling; the six
periods themselves span at least 24 minutes 12 seconds. All three musical
transports remained active, with 1,134.67 seconds (18 minutes 55 seconds) of
audio timeline elapsed. No visualization packets were dropped.

| Period | Callbacks | Mean ms | p50 ms | p95 ms | p99 ms | Max ms | Deadline misses | Gaps >16 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1: OFF | 17,528 | 3.582 | 3.409 | 4.588 | 6.166 | 37.866 | 16 | 228 |
| 2: ECONOMY | 17,542 | 3.570 | 3.378 | 4.673 | 6.385 | 22.143 | 22 | 269 |
| 3: STANDARD | 17,568 | 3.552 | 3.345 | 4.666 | 6.389 | 33.526 | 20 | 257 |
| 4: STANDARD | 17,556 | 3.560 | 3.343 | 4.698 | 6.354 | 33.226 | 21 | 253 |
| 5: ECONOMY | 17,559 | 3.559 | 3.375 | 4.650 | 6.148 | 34.827 | 17 | 222 |
| 6: OFF | 17,736 | 3.423 | 3.269 | 4.170 | 5.772 | 40.319 | 20 | 167 |

Both OFF periods already had deadline misses. Visual-on p99 was often higher
than OFF, while the deadline-miss counts were 17–22 versus 16 and 20 OFF; this
run does not establish clean real-time acceptance or zero resource contention.
The renderer caused no transport reset, queue growth, or producer backpressure.
Hardware comparison remains pending rather than treating a dummy backend as a
glitch-free audio result. See the [raw soak output](validation/mandala-soak.txt).

The software renderer delivered 21,402 frames across the four visual-on periods,
averaging 8.593 ms per render/present, with a 158.548 ms maximum including
scheduling stalls. No load-shedding tier was entered. It used 768×480 in Economy
and 960×600 in Standard, with a single dummy display and 1,570 generated motif
instances subject to the quality-specific draw budgets.

Ten-second `/proc` samples cover process seconds 473–1243, including both
Standard periods and the second Economy period. The original monitor could not
resolve the renderer across the executor PID namespace; only the corrected,
verified samples are retained. Discarding samples within 20 seconds of mode
boundaries, renderer CPU averaged 29.15% and 29.09% of one core in Standard and
11.72% in the second Economy period (20 samples each). This measures process CPU,
not GPU utilization. Renderer RSS was 19.51–20.22 MiB and remained 20.22 MiB in
all three complete sampled periods. Host RSS was 66.38–67.12 MiB; the fixture
touches additional pages in its fixed timing arrays as it records callbacks.
Host CPU averaged 26.41% of one core across the sampled interval.
See [resource samples](validation/mandala-resource-samples.csv).

## Remaining manual hardware acceptance

- Compare OFF/Economy/Standard at the same sample rate/buffer/project on the
  X220/X230 and HP A8, with normal desktop compositing and real input/output.
  Use repeated roughly five-minute alternating trials and a 24-minute soak.
- Record Audio Health arrival gaps, deadline misses and backend/device underruns
  separately; distinguish existing baseline instability from visual regressions.
- Test external Terra/other synth through the matrix, silence, sustained drone,
  dense/percussive material, stereo and the actual four/eight-channel interface.
- Drag between different-DPI displays; enter/exit fullscreen on the projector;
  disconnect it, change resolution, reconnect, and reopen with a missing saved
  monitor. Verify accessible fallback and uninterrupted transports/morphs.
- Freeze for several minutes, resume, close, force-terminate the child, and
  reopen during a four-minute Pedalboard transition. Capture in OBS.

This PR cannot certify these hardware cases from a container. Process isolation
prevents graphics locks/crashes from crossing into audio, but does not promise
zero resource contention or universal glitch immunity.
