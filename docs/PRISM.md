# Prism: real-time refraction

Prism turns live or recorded audio into 2–12 related voices inside Sister Machine.
Supersaw and Ensemble are the first two geometries. No render job or CDP executable
is involved. The existing FM Unison remains available.

![Native Prism panel, displaying the audio engine's smoothed lens state](images/prism.png)

## Play it

1. Open Sister Machine with **Tab**. Its page button cycles **Tape → FX → Fallout → Prism → Tape**.
2. For a direct pedal, leave **POWER off**. Play a tile, FM, Mosaic or TapeHead, or
   enable the main window's external input monitor for a physical instrument.
   Prism runs before the existing ordinary FX/Fallout path.
3. Engage **PRISM ON**. Start with Supersaw, 12 lenses, Spread 50, Focus 0, Drift 15,
   Stereo 80, Body 50, Wet 80 and Output 0 dB. Output offers up to +12 dB if needed.
4. Press **REC FILE** on this page. It records the final stereo output, including
   Prism, subsequent effects, the safety limiter and OUT fader. The existing timer,
   stop control and recording state continue across page changes.

With **POWER on**, Sister owns the musical sources as before: select the sources
on its Tape page and enable Monitor. Prism precedes the PRE slots, tape input trim,
rolling write and heads. For direct instrument playing without delayed tape heads,
set Sister's **DRY 100 / WET 0**. Here DRY means the direct *Sister* input, which
already contains Prism; Prism's own WET controls refraction. PRE FX can process
that direct path. Raise Sister WET to hear the heads and their downstream effects.
Head-tap capture still selects the corresponding head; **REC FILE** uses final OUT.

## Controls and optical meaning

| Control | Audio | Diagram |
|---|---|---|
| Mode | Supersaw or Ensemble; click to cycle | Wide fine-pitch fan plus octave body, or a tighter fan with more time displacement |
| Lenses | 2–12, with fading entry/removal; wheel steps one lens | One ray per contributing lens; coincident pitches can overlap |
| Spread | 0–200%; 50 reproduces the FM offsets at Focus 0, 100 reaches ±208 cents, 200 reaches ±1508 cents | Angular separation follows the actual smoothed pitches |
| Drift | Independent, deterministic slow pitch wandering, 0–200%; up to ±25 cents at 100 and ±190 cents at 200 before Focus; lower settings retain subtle movement | Rays wander with the audio; no separate animation clock |
| Focus | Contracts fine pitch, hand-drawn pitch offsets and added time toward zero | Rays converge; intentional octave relationships remain |
| Stereo | Per-lens stereo balance; body voices stay near center | The control points move laterally with the actual pan values |
| Body | 0–300%; stronger original central lens and stronger low octave lenses | The corresponding rays become more prominent |
| Dry Level | 0–200% trim on the original branch, before the Wet crossfade; 100 is unity | Original beam brightness follows its smoothed gain |
| Wet | Original input versus normalized lens sum | Original straight path fades as refracted rays brighten |
| Output | −12 to +12 dB on the refracted sum | Audio output trim; the existing OUT meters show final level |

The incoming source, refraction, altered rays, recombination and output are always
connected. At zero Wet the rays remain faint as a settings guide and the original
path dominates. This is a parameter diagram, not an amplitude scope or frequency
analyzer. Pitch distances are expanded/compressed for readability; octave bands
are not a linear frequency scale. Vertical position represents pitch on a continuous compressed scale; horizontal
position represents pan. Neither axis represents physical distance or device latency.

Grab a **hollow middle point** and drag **up/down for pitch**, **left/right for pan**.
Each shifted lens accepts an independent ±1 octave offset around its mode/Spread
pitch, so one ray can stay close while another goes wide. Focus contracts these
pitch offsets too; at Focus 100, lower Focus to bend pitches again. Horizontal
edits add to the automatic Stereo placement and stop at hard left/right. The
filled central point, lens 01, is the direct body anchor.

**Shift-left-click mutes a lens**, **Ctrl-left-click solos it**. Solo is additive:
Ctrl-click several lenses to hear them together. Mute wins if both are set; solos
on lenses outside the current count do not silence the active lenses. The dry
branch remains independent. Mute/solo and wheel trim also work on the fixed body
anchor, lens 01. Muted and solo-excluded rays stay faint and clickable; **M/S**
marks show explicit mute/solo states.

**Wheel over a point** trims it from −24 to +12 dB in 1 dB steps; Shift-wheel uses
0.1 dB steps. Brightness follows its actual smoothed level, and the selected lens
shows its trim. Trim and mute/solo sit after the nominal energy reference, so
changing one lens never makes normalization undo the fader or raise the others.

**Right-click a point to reset that lens** (pitch, pan, trim, mute and solo).
**Right-click the mode button or preset name** restores all lenses to stock:
zero offsets/trim and no mute/solo, keeping the global controls and current mode.
**Escape during a drag** restores its starting pitch/pan. Dragging outside the window holds at the edge. Lens identities
and edits survive count changes, modes, project saves and full Sister presets.
The selected ray shows its number; the status line shows its target pitch/pan.
Dry Level trims the original branch before the existing Wet crossfade: Wet 100
still excludes that branch, and disabling Prism restores unity dry output.

![Per-lens trim brightness, a muted ray, and the separate dry control](images/prism-mixer.png)

![Independently placed lenses, with the other rays still near the original fan](images/prism-custom.png)

The renderer reads a coherent atomic snapshot of the audio-owned smoothed values.
It does not read audio buffers or run DSP. It uses Sister's existing maximum
30 fps refresh; hiding or minimizing the page does not affect sound. With no
running engine it displays a stationary **SETTINGS** diagram.

Other native states: [Ensemble](images/prism-ensemble.png),
[full Focus](images/prism-focus.png), [zero Wet](images/prism-dry.png).

## Sound and architecture

The PR99 audit found two different source models: FM Unison creates oscillators;
Sister receives an arbitrary stereo stream. The reusable part is the voicing:
`0, −7, +7, −12, +12, −19, +19, −26, +26, −1200, −1207, −1193` cents.
Both engines now use one shared table. FM's oscillator, editing and reversible
Unison behavior are otherwise unchanged. A streaming reconstruction will have a
different texture from directly generated oscillators.

Prism has one preallocated 80 ms stereo history and bounded per-lens read state.
The original central lens stays direct. Other lenses use complementary Hann
windows over fractional stereo reads. A bounded shared period estimator runs at
about 47 Hz. On periodic sources it aligns the window span to an even number of
source periods, preventing the fixed-window pitch sidebands from displacing the
lower octave. A new span crossfades against the preceding read geometry for 40 ms;
the engine does not slide an entire delay window abruptly when a note changes.
On aperiodic input the last usable span remains in place.

Pitch, pan, added delay, level, Wet and output changes are smoothed. Mode changes
glide through the smoothed pitch targets. Lens identities survive count changes.
The seeded low-frequency trajectories are reproducible from a fresh start;
project recall restores controls, not the audio history or the exact middle of a
running drift cycle. The stream continues to prime during bypass. Once bypass or
Wet zero settles, the output equals the original stereo input exactly, including
when Prism's Output trim is nonzero.

The callback performs no allocation, file I/O, GUI work or new locking. All
storage is allocated during device preparation. Period analysis uses fixed bounds
and no FFT. Left and right remain separate through the reads; there is no mono
folding in the signal path. The period detector observes whichever input channel
has more energy, so opposite-polarity stereo does not cancel its analysis input.

Gain divides each channel's weighted sum by the square root of its squared lens
weights, including the current pan and lens-count fades, before manual trim/mute/solo. This restores decorrelated
voice energy instead of averaging it away as the count grows. The direct body
and lower octave voices keep their original weights and are more audible because
the sum no longer loses 10–12 dB at twelve lenses. This is fixed energy compensation,
not an automatic loudness rider; correlated voices can add up more strongly. The
existing final stereo-linked limiter handles those peaks. Output now spans
−12 to +12 dB on the wet sum; Wet keeps its existing linear crossfade.

A matched 48 kHz test at 12 lenses, Spread 50, Drift 0, Focus 0, Stereo 80,
Body 50, Wet 100 and Output 0 dB measured RMS relative to bypass:

| Input / mode | First run | Revised |
|---|---:|---:|
| 110 Hz bass / Supersaw | −11.81 dB | −0.39 dB |
| 110 Hz bass / Ensemble | −11.73 dB | +0.26 dB |
| Actual FM 12-voice saw Unison / Supersaw | −10.48 dB | +0.95 dB |
| Actual FM 12-voice saw Unison / Ensemble | −12.35 dB | −0.40 dB |

The FM test uses the native FM renderer at 220 Hz. Measurements use the last
three seconds of four, before limiting, with the same input and settings in both
engines. Noise retained roughly −1.6 dB because the windowed reads smooth its
energy. Maximum Output, full Focus and abrupt lens edits produced internal peaks
of 6.828; the existing limiter held both output channels to its −1 dB ceiling
(0.891 linear). No extra Prism limiter or compressor is added.

## Latency and performance

The dry path and central lens add **zero Prism buffering**. Other voices use
variable read ages: nominally a 40 ms window plus a 2 ms guard and small per-lens
offsets. Detected periods can change the window, bounded to 60 ms. The oldest
Ensemble read is under approximately **71 ms** at normal audio rates; Supersaw
adds less offset. This is a blend of different read ages, not one fixed latency
that can be removed by delaying the dry signal. Device buffers and the existing
output limiter add their own latency. Lens count does not increase the window.

Measured on a shared Linux AMD EPYC 9V74 host, C11 `-O2`, stereo 48 kHz,
256 frames (5,333 µs budget), 4,000 measured blocks after warmup:

| Lenses | Prism mean / block | Prism p99 | Prism share of block budget | Sister + Prism + limiter mean |
|---:|---:|---:|---:|---:|
| Bypass | 23.12 µs | 55.31 µs | 0.43% | 305.31 µs |
| 2 | 51.48 µs | 135.90 µs | 0.97% | 320.42 µs |
| 4 | 60.13 µs | 124.45 µs | 1.13% | 327.83 µs |
| 6 | 63.06 µs | 119.69 µs | 1.18% | 328.80 µs |
| 8 | 75.42 µs | 161.65 µs | 1.41% | 340.91 µs |
| 12 | 85.12 µs | 159.74 µs | 1.60% | 355.28 µs |

These are offline processing measurements after the per-lens mixer revision, not
whole-application CPU percentages or hardware underrun certification. Shared-host
scheduling produced outliers: Prism alone reached 1.78 ms at two lenses and
the full runtime reached 6.03 ms in bypass. Full-runtime p99 stayed below 0.57 ms;
at twelve lenses its maximum was 2.15 ms. Physical Windows and Linux interface
tests at 256 frames remain part of live audition.

## MIDI and saved projects

Use the existing **Ctrl+Shift+M** MIDI learn flow. Prism On/Off and Mode are
trigger targets; all nine sliders, including Dry Level, Focus and lens count, use the
existing continuous mapping and pickup system. Lens count is rounded to 2–12.
The sliders participate in the existing parameter-lock system. Individual points
are mouse gestures; they do not repurpose the global sliders or their MIDI targets.

Project state version **15** and Sister user preset version **14** persist all
Prism controls, dry level and per-lens pitch/pan/trim/mute/solo. Missing lens keys
use zero offsets and trim with no mute/solo; missing Dry Level uses unity;
files predating Prism initialize it **off**. The older FX-slot migrations
retain their original version cutoffs, so PR99 slots and locks are not migrated
again. The main TSR29 sample format is unchanged. Sister presets on this page
are the existing full Sister presets, not a second Prism-only preset bank.

## Verification and remaining listening work

- Core tests cover deterministic streaming, abrupt control/count/mode changes,
  stereo channel isolation, retained opposite-polarity stereo, finite output,
  energy compensation, final limiter ceilings, silent input, exact settled bypass,
  focus and control sanitizing. Gain regressions include bass, noise and the actual
  twelve-voice FM Unison at 2/4/6/8/10/12 lenses in both modes.
- Test sources include sine, saw, triangle, pulse, noise, impulses and dynamically
  gated noisy tones at 44.1, 48 and 96 kHz. Spectral checks measure all twelve
  intended pitches at 44.1 and 48 kHz, including the three lower voices.
- Native application tests exercise page cycling, mouse hit regions, MIDI target
  resolution, knob changes, lens-count wheel steps, scaled-window point drags,
  Escape via workspace dispatch, out-of-window release, independent DSP changes
  and per-lens reset, muted-handle hit testing, modifier clicks, wheel trim,
  stock reset on both labels, and the ninth global MIDI target (Dry Level). Both tape power states
  capture 16,384 stereo frames through REC FILE and compare every recorded sample
  against the actual output callback, while changing pages during recording.
- Workspace regressions preserve held sample/FM notes through view changes,
  mix FM and Mosaic simultaneously, release old keys over Mosaic, and preserve
  FM latches and preview storage. Grave shortcuts return directly to the canvas;
  repeated Tab switches keep the main fullscreen window visible. Windows
  compositor appearance still needs a physical desktop check.
- Save/load tests cover every Prism setting, user presets, missing old settings
  and preservation of PR99's explicit FX slots. FM Unison and the other previously passing
  tests retain their results. Four existing failures are reproduced
  with an independently rebuilt, unchanged PR99 tree: Sister routes, source mask,
  recursion, and the canvas/grid assertion. The total is 74 passing / 78 tests.
- AddressSanitizer and UndefinedBehaviorSanitizer pass the new streaming/pitch/state
  tests with the Prism engine instrumented (LeakSanitizer disabled because this
  host cannot inspect process threads). Native screenshots were checked in
  Supersaw, Ensemble, fully focused, zero-Wet, custom-pitch and lens-mixer states.
- Native CDP executable integration was skipped because that runtime is absent
  in this checkout. Prism itself has no CDP dependency.

This is the first playable prototype. Listening with PIPE/breath, speech, field
recordings, actual tape, dirty hardware oscillators, complex polyphony and physical
stereo inputs remains a user audition task; synthetic coverage does not substitute
for those recordings. Granular texture and transient softening can still be
audible, especially with large hand-drawn intervals. Organ, Octaves, Cluster,
Cloud, a named Custom mode, per-lens coloration and a separate
quality mode are future work, not hidden options in this build.

Reproduce the focused checks and diagnostic image after configuring the normal build:

```sh
cmake --build build --target test_prism prism_probe tapesister_mosaic_controller_tests
ctest --test-dir build -R 'test_prism|tapesister_mosaic_controller_tests' --output-on-failure
build/prism_probe prism.ppm
build/prism_probe --bench
```

On Windows the executables have `.exe` suffixes. The application adds no new
runtime dependency or packaging step.
