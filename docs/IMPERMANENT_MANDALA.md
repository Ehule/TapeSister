# Impermanent Mandala

F11 opens Performance Transport. **VISUALIZE** opens a separate native SDL
window. Its music-reactive geometry grows from the project's seed. The same
motifs recur at different scales; level, transients, stereo energy and a slow
cycle determine which structures appear, persist and dissolve. Silence settles
into darkness. This is procedural geometry, not playback of the concept image.

In Performance Transport:

- VISUALIZE / CLOSE VISUALIZER controls the visual process only.
- FREEZE VISUAL / RESUME VISUAL stops visual evolution and repeated drawing.
- NEW SEED, PALETTE, SYMMETRY and EVOLUTION change saved project settings.

In the visual window:

| Control | Action |
| --- | --- |
| F | Borderless fullscreen on the selected display; press again to restore |
| D | Select the next connected display (number appears in the revealed controls) |
| Q | Economy 15, Standard 30, Enhanced 60 FPS target |
| Space | Freeze / resume visuals |
| Esc | Exit fullscreen; in window mode, close visuals |
| Top-right X | Same exit behavior as Esc |
| I | Optional renderer diagnostics |

Move the pointer to reveal controls. Controls disappear after 2.5 seconds;
the cursor also hides in fullscreen. Windowed output can be dragged to another
monitor or captured in OBS. Aspect ratio is preserved with dark letterboxing.
Display topology changes or an unavailable saved monitor exit fullscreen and
recenter a 960x600 window on the primary display. Physical projector, DPI and
window-manager behavior still require platform testing.

The first-run mode is Economy. Quality, monitor and fullscreen preference live
in `tapesister.ini`. Project sidecar version 35 stores `Mandala.Version=1`, a
64-bit nonzero seed, palette 0–2, symmetry 4–12 and evolution period 30–1800
seconds. Filename changes do not affect identity. Legacy projects initialize an
identity that becomes permanent on the next save. Invalid or unknown visual
fields/versions reject the load transaction rather than partially applying it.
Frame history and display configuration are never written into a project.

## Architecture and bounded cost

The same packaged executable has an early `--mandala-renderer` dispatch. This
child initializes SDL video/timer only, before the host's MIDI, audio or tracker
setup. It runs at below-normal priority on Windows / nice +10 on POSIX. A
separate SDL thread would avoid launching another process, but shares crash
fate and platform window-thread constraints. The child-process boundary was
chosen because it also avoids holding existing audio/editor locks during draw,
present, display changes, or a graphics-driver stall. No separate helper binary,
framework, CDP process or new audio engine is packaged.

The actual callback calls `ts_mandala_audio` once after final physical output is
written: routing, Master EQ/limiter, OUT gain, spatial decode, clean-output mix
and the final hardware guard have already run. All physical channels are sampled
once, up to the existing 16-channel routing limit. Hardware sends are included
because they are actual output; the visualizer does not assume which sockets
feed speakers. RMS is the mean of channel energies, so mirroring a stereo pair
does not multiply level, and opposite phase cannot cancel the display. Stereo
width is pairwise difference energy; balance compares odd/even channel energy.
These are approximate energy measurements, not spectral analysis or true peaks.

The callback copies at most 64 stratified frames × 16 floats into a four-slot
SPSC ring. The changing positions avoid coherent decimation nulls. No visual
allocation, analysis, clock, file operation, graphics call, wait, retry loop or
lock runs in the callback. Closed/frozen sampling is gated by one atomic load.
The sole callback producer writes a slot then release-publishes its index; the
UI acquire-reads the newest complete slot and release-returns ownership. Full
rings drop packets. The UI forwards the latest bounded packet and configuration
through a second four-slot SPSC ring in shared memory. It never waits for the
child. Queue indices are monotonic unsigned integers, with wraparound tested.

All synchronization objects are native `atomic_uint`; launch checks lock-free
operation in local and mapped memory. This IPC implementation targets the
coherent Windows/Linux x86/x64 machines supported here. Shared memory is private
to the local session (0600 POSIX object; Windows local-session mapping). POSIX
names are unlinked after the child's startup acknowledgement; mappings survive
until each process closes them. The child detects parent exit. The host polls
process exit without waiting, disables sampling and reclaims handles. Startup
timeout is 10 seconds. A child that cannot honor Close within one second is
terminated and reaped by nonblocking polling, including a suspended POSIX child.
Child stall/crash cannot backpressure the callback.

Analysis and smoothing run only in the child. A fixed -60 dB floor and bounded
normalization avoid noise-driven gain chasing. Audio changes membership, size,
refraction and illumination; a project-defined slow cycle controls accumulation
and dissolution. Missing telemetry decays after 250 ms. Time uses elapsed clock
time, capped at 250 ms after a stall; no catch-up simulation occurs.

The renderer uses 12 seed-built motifs (at most 49 points each), at most 2304
instances, one 64×64 soft sprite and one bounded history texture. Internal
resolution is 768×480 / 960×600 / 1280×800. Instance budgets are 1152 / 1792 /
2304. Transforming/drawing instances has real cost; geometry sharing only avoids
reconstruction and per-instance allocation. One retained texture implements
short fading traces; no recursive history, growing cache, particle queue or
performance-time readback exists. Sustained expensive frames reduce instance
count, resolution and cadence to 15, then 8 FPS (down to a 512×320 internal
texture). Frame deadlines also cap drawing during event floods. SDL 2.0.18+
supports accelerated and software
paths; no dedicated GPU is required. Priority and process isolation do not
eliminate CPU/memory-bandwidth contention.

## Reproducing validation

Build normally, then run `ctest -R mandala --output-on-failure`.
The controller fixture runs the real application callback and launches the real
renderer executable. On Linux it suspends the child with SIGSTOP and terminates
it with SIGKILL; on Windows it tests abrupt process termination. It also checks
transport continuity, freeze, repeated close/reopen, project dirty-state changes,
fullscreen events and missing-display fallback under SDL's dummy video driver.

`tapesister_mandala_controller_tests --soak 240` runs six four-minute measurement
periods (OFF, Economy, Standard, Standard, Economy, OFF), each preceded by a
two-second warm-up. The same 48 kHz / 512-frame, four-output callback runs
TrackSister, 16 looping Mosaic instances, ARP, Prism, Sister Machine, Pedalboard,
Fallout and synthetic external input through an explicit routing graph. The
process remains alive for over 24 minutes. The fixture reports callback timing
percentiles/maxima, deadline misses and arrival gaps; SDL dummy audio cannot
measure physical-device underruns. It does not establish X220/HP A8 acceptance.

`SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software tapesister --mandala-capture
capture.bmp 314159 1800` writes a deterministic actual-renderer capture using
synthetic test features at 120 seconds of simulated evolution. Diagnostic
capture is the only path with readback. Screenshots are implementation evidence,
not the supplied concept art and not captures of a hardware performance.

Measured results and remaining hardware checks are recorded in
[MANDALA_VALIDATION.md](MANDALA_VALIDATION.md).
