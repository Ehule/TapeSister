# Impermanent Mandala

F11 opens Performance Transport. **VISUALIZE** opens a separate native SDL
window. Its music-reactive geometry grows from the project's seed. The same
motifs recur at different scales; level, transients, stereo energy and a slow
cycle determine which structures appear, persist and dissolve. Silence settles
into darkness. This is procedural geometry, not playback of the concept image.

In Performance Transport:

- VISUALIZE / CLOSE VISUALIZER controls the visual process only.
- FREEZE VISUAL / RESUME VISUAL stops visual evolution and repeated drawing.
- NEW SEED, SYMMETRY and EVOLUTION change saved project settings.
- EDIT VISUALIZER opens a compact live palette and shape editor.

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
in `tapesister.ini`. The project sidecar stores `Mandala.Version=2`, a
64-bit nonzero seed, palette 0–5, symmetry 4–12, evolution period 30–1800
seconds and the editor's colors/form controls. Version-1 Mandala projects migrate
to the same original palette and default form controls; projects saved with
version 2 require a build that supports the editor. Filename changes do not affect identity. Legacy projects initialize an
identity that becomes permanent on the next save. Invalid or unknown visual
fields/versions reject the load transaction rather than partially applying it.
Frame history and display configuration are never written into a project.

## Palette and shape editor

Open **F11 → EDIT VISUALIZER**. The modeless dialog can stay open beside the
visual window. OPEN VISUALIZER starts the preview; if frozen, its button offers
RESUME VISUALIZER so changes become visible. Musical playback is unaffected.

The existing Amber, Lunar and Ember palettes are joined by **Bleed and Soak**
(crimson/scarlet with hot orange highlights) and **TapeSister Gold** (gold,
copper and violet inspired by the splash artwork). Choosing a palette changes
colors only; it does not change shape settings, audio or pedal settings.

- Select any of seven ink swatches or the backdrop. Drag/wheel the RGB controls,
  or click the hex field, enter six hex digits and press Enter. Esc cancels hex
  entry. Editing a preset copies its colors into Custom. Visiting another preset
  preserves Custom; editing that preset starts a new custom palette.
- Choose **Mixed, Triangles, Squares, Circles or Hexagons**. Mixed is the original
  motif library. The other choices replace its repeated closed shapes and open
  brackets while preserving the overall seeded arrangement and fine threads.
- **Density** (10–100%) reduces the quality-dependent draw budget. **Size**
  (50–150%) scales the composition; larger settings can extend past the viewport.
  **Motion** (0–100, default 50) slows or speeds local breathing/drift without
  changing audio response or the independent EVOLUTION cycle. **Trails**
  (0–100, default 50) adjusts baseline trace length from short to long; reverb
  and delay still add their own response. Right-click a form slider to reset it.
- Symmetry and evolution support left-click to increase, right-click to decrease,
  or wheel adjustment. Evolution changes in 30-second steps and displays minutes
  and seconds. RESET FORM restores shape, density, size, motion, trails, symmetry
  and evolution defaults while preserving colors and the seed.
- UNDO/REDO (Ctrl+Z / Ctrl+Y, also Ctrl+Shift+Z), REVERT OPENING and NEW SEED
  support experimentation. DONE, Esc and window Close keep the current changes.
  Save the project to keep them across launches. Loading another project resets
  the editor's undo/revert origin to that project's appearance.

The editor only paints when dirty and performs no DSP or device locking. Color
and scalar edits update the renderer without rebuilding its seed library; shape,
symmetry and seed edits rebuild only the bounded motif/instance arrays. No extra
renderer textures or audio analysis are introduced. A custom backdrop replaces
the original near-black silence background. See the
[editor captures and validation](MANDALA_EDITOR.md).

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
allocation, sample analysis, clock, file operation, graphics call, wait, retry loop or
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
instances, one 64×64 soft sprite, one bounded history texture, four 320×200 delay echoes
and a 160×100 diffusion/compositing texture. Internal
resolution is 768×480 / 960×600 / 1280×800. Instance budgets are 1152 / 1792 /
2304. Transforming/drawing instances has real cost; geometry sharing only avoids
reconstruction and per-instance allocation. One retained texture implements
fading traces; the fixed four-slot echo history never grows. No growing cache,
particle queue or performance-time readback exists. Effect layers reuse the tiny
compositing texture and add 1,088,000 bytes of texture pixel storage. Sustained expensive frames reduce instance
count, resolution and cadence to 15, then 8 FPS (down to a 512×320 internal
texture). Frame deadlines also cap drawing during event floods. SDL 2.0.18+
supports accelerated and software
paths; no dedicated GPU is required. Priority and process isolation do not
eliminate CPU/memory-bandwidth contention.

## Effects shape the image

Amplitude remains the main brightness and structural-presence input. Built-in
Pedalboard and Prism states now add distinct transformations:

| Audio control | Visual response |
| --- | --- |
| Distortion wet mix / drive | Geometry separates, bends and loses edges; greater drive adds soft smearing |
| Grain wet mix / size / density | Motifs shrink into fine fragments or grow into larger overlapping forms; density changes gentle local breathing |
| Reverb wet mix / size / decay | Diffusion, broader halos and longer traces |
| Delay wet mix / time / feedback | Up to three time-spaced expanding echoes; feedback strengthens them |
| Prism wet mix / current lens spread | Displaced teal/plum layers follow the current refracted lens spread |

The visual follows current DSP engage amounts, Master FX fades and both sides
of an effect-type crossfade. A four-minute engage therefore produces a gradual
visual change. It does not jump to the target switch state. Built-in control
smoothing is followed by a 450 ms visual easing time, with signal-meter gating
and the final-output silence floor. These are artistic interpretations of
module activity, not per-slot audio isolation, spectral reconstruction or exact
waveform-aligned delay repeats. Delay spacing is visually bounded to 80 ms–2 s.
EVOLUTION still controls the independent slow cycle, without BPM synchronization.

While visuals are active and unfrozen, the existing audio-owned runtime snapshot
publishes 14 extra scalar values. The readout examines at most four slots, two
morph endpoints and five placements, using existing smoothed controls and peak
meters. It performs no new audio analysis, allocation, lock, wait or graphics
work. A lock-free gate skips the readout while closed/frozen. UI-side reduction
runs at most about 30 times per second, reading only the atomic snapshot. The
small result travels in the existing bounded IPC queue; smoothing, blur and
transforms stay in the renderer process. The audio-sample packet is unchanged.

Blur is composited at the internal capped resolution. Echoes are combined at
160×100 before one internal-size pass; their total reinjection gain stays below
the trace decay, preventing runaway brightness. Existing load shedding also
applies when effects make rendering more costly. See
[effect validation and native comparisons](MANDALA_EFFECTS.md).

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
not the supplied concept art and not captures of a hardware performance. An optional fifth argument selects
reverb=0, delay=1, distortion=2, grains=3, Prism=4, or combined=5 for diagnostics.
Two further optional arguments select palette (0–4, or default Custom=5) and
shape (Mixed=0, Triangles=1, Squares=2, Circles=3, Hexagons=4). For example,
`tapesister --mandala-capture capture.bmp 314159 900 5 4 1` captures TapeSister
Gold with triangles and combined diagnostic effects. These effects are supplied
by capture mode; choosing that palette during playback does not engage effects.

Measured results and remaining hardware checks are recorded in
[MANDALA_VALIDATION.md](MANDALA_VALIDATION.md).
