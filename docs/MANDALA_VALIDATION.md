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

Full regression, final benchmark and CI results will be added before review is
finished. The long fixture records p50/p95/p99/max, callback deadline misses,
arrival gaps, renderer frame time and process memory. Physical underruns cannot
be measured by SDL dummy audio.

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
