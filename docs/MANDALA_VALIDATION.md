# Mandala validation — 12 October 2026

Implementation base: `ae596039` (main, PR #136 merged). No open PR was found at
start. Branch: `feature/impermanent-mandala`.

Environment: Linux x86_64 container, AMD EPYC 9V74 host, 8 CPU quota, 8 GiB memory
limit; GCC 13.3, Release/O3, SDL 2.30 software renderer with dummy video/audio.
There is no physical sound card, desktop window manager or projector here.

Passed so far:

- Project identity/config round-trip and rename, explicit reseed, distinct new
  identities, legacy migration, invalid fields and unknown version rejection.
- Four-slot queue capacity/drop behavior, unsigned index wraparound and disabled
  producer; anti-phase stereo, duplicated 16-channel output and silence floor.
- Actual child launch, freeze, SIGSTOP stall, SIGKILL termination, repeated
  close/reopen, unchanged routing and continuing TrackSister/Mosaic/ARP playback.
- Native Performance Transport reseed changes the saved-state fingerprint.
- Renderer fullscreen/escape events and missing-display/topology fallback with
  SDL dummy video (physical display behavior remains unverified).

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

A preliminary short six-period callback run had no 10.667 ms deadline misses
in any mode. Longer comparison and CI results are being collected. Physical
underruns cannot be measured by SDL dummy audio.

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
