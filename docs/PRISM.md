# Prism: real-time refraction

Prism turns live or recorded audio into **2–24 related voices** inside Sister Machine.
The cap is **23 shifted lenses plus lens 01, a clean wet body anchor**.
The default remains twelve, and existing patches retain their saved lens count.
The **DRY** lane is a separate copy of the original signal.
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
| Lenses | 2–24, with fading entry/removal; wheel steps one lens | One ray per contributing lens; the numbered strip selects overlapping points |
| Spread | 0–200%; 50 reproduces the voicing table at Focus 0; the full fan reaches ±248 cents at 100 and ±1798 cents at 200 | Angular separation follows the actual smoothed pitches |
| Drift | Independent, deterministic slow pitch wandering, 0–200%; up to ±25 cents at 100 and ±190 cents at 200 before Focus; lower settings retain subtle movement | Rays wander with the audio; no separate animation clock |
| Focus | Contracts fine pitch, hand-drawn pitch offsets and added time toward zero | Rays converge; intentional octave relationships remain |
| Stereo | Per-lens stereo balance; body voices stay near center | The control points move laterally with the actual pan values |
| Body | 0–300%; stronger wet body anchor and stronger octave-body lenses | The corresponding rays become more prominent |
| Dry Level | 0–200% trim on the original branch, before the Wet crossfade; 100 is unity | Original beam brightness follows its smoothed gain |
| Wet | Original input versus normalized lens sum | Separate DRY lane fades as refracted rays brighten |
| Output | −12 to +12 dB on the refracted sum | Audio output trim; the existing OUT meters show final level |
| Input shape | Maps the mode/Spread intervals before hand edits and drift | Changes the incoming refraction fan |
| Output shape | Maps the edited intervals and stereo placement | Rays bend or cross inside the output glass before recombining |
| Color | 0–100% blend of each selected shape's tone on shifted voices | Tone labels above the glass identify the active profiles |

The incoming source, refraction, altered rays, recombination and output are always
connected. At zero Wet the rays remain faint as a settings guide and the original
path dominates. This is a parameter diagram, not an amplitude scope or frequency
analyzer. Pitch distances are expanded/compressed for readability; octave bands
are not a linear frequency scale. Vertical position represents pitch on a continuous compressed scale; horizontal
position represents pan. Neither axis represents physical distance or device latency.

The **01–24 numbered strip** selects individual lenses even when their points
overlap. Select a number, then drag its highlighted point; the selected point wins
an exact overlap. Shift/Ctrl-click, wheel trim and right-click reset also work
directly on the numbers. Numbers above the active count are dim and inactive.

Grab a **hollow middle point** and drag **up/down for pitch**, **left/right for pan**.
Each shifted lens accepts an independent ±1 octave offset around its mode/Spread
pitch, so one ray can stay close while another goes wide. Focus contracts these
pitch offsets too; at Focus 100, lower Focus to bend pitches again. Horizontal
edits add to the automatic Stereo placement and stop at hard left/right. The
filled central point is labeled **01 BODY**. It belongs to the wet sum: Body,
its own wheel trim, mute/solo and Wet control its contribution. **Dry Level does
not control 01**; it controls the separate original lane around both optical
stages. At Wet 100 that lane is silent, while 01 can still provide clean body.
This distinction preserves existing patches and gives both paths independent gain.

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

## Play the glass shapes

**Left-click either large lens (or its name) to cycle forward; right-click to
cycle backward.** Input and output are independent, giving 36 combinations.
Both start at BI-CONVEX, which preserves PR100's pitch and tone. Color defaults
to 50; clear glass still adds no coloration. Set Color to zero to explore only
the pitch and stereo mappings, or raise it for the glass character.

| Shape | Interval map | Color character on each shifted voice |
|---|---|---|
| BI-CONVEX | Unchanged | Clear |
| PLANO-CONVEX | 65% of the interval | Warm low-pass filtering |
| MENISCUS + | Upper intervals ×1.25; lower ×0.75 | All-pass phase coloration |
| BI-CONCAVE | Invert around the original pitch | Soft saturation |
| PLANO-CONCAVE | Invert, then narrow to 65% | Hollow high-pass filtering |
| MENISCUS − | Invert; upper intervals ×0.75, lower ×1.25 | Phase coloration into saturation |

These are musical mappings inspired by optical shapes, not a physical simulation.
“Invert” means a downward octave becomes an upward octave; it does not reverse
playback or flip the audio waveform's polarity. Two BI-CONCAVE shapes restore
the base pitch organization, while their coloration still combines. Input acts
on the mode's intervals before manual pitch offsets and drift. Output acts on
the edited result and on stereo pan; middle handles show the state before that
output map, so dragging upward still raises the intermediate pitch even when
output glass inverts what is heard. Focus retains the shaped octave bands.

The pitch maps compose into one streaming reader per shifted voice. The input
and output **color** stages then run serially on each resulting voice, before
trim, pan and summation. Filter and phase frequencies differ by voice and stage;
no mono folding or cross-channel feed is added. Changes crossfade smoothly.
Both the separate dry lane and the 01 body anchor bypass these color stages.
There are no additional per-voice color selectors in this revision.

![Meniscus phase coloration followed by concave inversion and saturation](images/prism-shapes.png)

The optical curves use dense sampling with **solid native-pixel strokes**, matching
the rest of the instrument. The initial PR101 antialiasing made thin rays and
handles look blurred when enlarged; it has been removed. Sister explicitly uses
nearest-neighbor texture scaling. Hollow handles have their original stronger
borders, and the pitch diagram has 19% more vertical travel to expose subtle
Drift changes. The inverse drag mapping uses the same expanded coordinates.
All six glass silhouettes and sound mappings remain available.

The displayed movement comes from the audio snapshot, including during silence;
there is no UI animation oscillator. At Drift zero or full Focus it settles.
A 12-second native-frame sequence at Spread 200, Drift 200, Focus 60, Stereo 100,
PLANO-CONVEX input and MENISCUS − output demonstrates the reported settings
with all 24 lenses:

![Crisp Prism motion driven by live DSP snapshots](images/prism-drift.gif)

The **24-lens cap** adds ten fine-detuned voices and two lower-octave voices.
The first twelve keep their original pitch, pan and weight definitions. New pan
positions interleave with the original fan; adding lenses does not reassign the
surviving voices. The added lenses share the existing audio history.

![All 24 lenses with independently edited points and two colored glass stages](images/prism-24.png)

## Sound and architecture

The PR99 audit found two different source models: FM Unison creates oscillators;
Sister receives an arbitrary stereo stream. The reusable part is the voicing:
`0, −7, +7, −12, +12, −19, +19, −26, +26, −1200, −1207, −1193` cents.
FM uses these first twelve entries of the shared table and remains a twelve-voice
synth. Prism adds lenses 13–24 at
`−3, +3, −10, +10, −16, +16, −23, +23, −31, +31, −1203, −1197` cents.
In Supersaw, lenses 10–12 and 23–24 form the lower-octave body band. The original
fine fan still reaches ±208 cents at Spread 100 and ±1508 at 200. FM's oscillator,
editing and reversible Unison behavior are unchanged. A streaming reconstruction will have a
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
Wet zero with Dry Level 100 settles, the output equals the original stereo input exactly, including
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

A matched 48 kHz test at 12 and 24 lenses, Spread 50, Drift 0, Focus 0, Stereo 80,
Body 50, Wet 100 and Output 0 dB measured RMS relative to bypass:

| Input / mode | First run, 12 lenses | Revised, 12 lenses | Revised, 24 lenses |
|---|---:|---:|---:|
| 110 Hz bass / Supersaw | −11.81 dB | −0.39 dB | −0.41 dB |
| 110 Hz bass / Ensemble | −11.73 dB | +0.26 dB | +0.02 dB |
| Actual FM 12-voice saw Unison / Supersaw | −10.48 dB | +0.95 dB | +1.36 dB |
| Actual FM 12-voice saw Unison / Ensemble | −12.35 dB | −0.40 dB | −0.70 dB |

The FM test uses the native FM renderer at 220 Hz. Measurements use the last
three seconds of four, before limiting, with the same input and settings in both
engines. Noise measures roughly −1.6 dB at twelve and −2.1 dB at 24 because
the windowed reads smooth its energy. Maximum Output, full Focus and abrupt lens
edits at 24 lenses produced internal peaks of 9.673; the existing limiter held both output channels to its −1 dB ceiling
(0.891 linear). No extra Prism limiter or compressor is added.

## Latency and performance

The dry path and central lens add **zero Prism buffering**. Other voices use
variable read ages: nominally a 40 ms window plus a 2 ms guard and small per-lens
offsets. Detected periods can change the window, bounded to 60 ms. The oldest
Ensemble read at 24 lenses is under approximately **78 ms** at normal audio rates; Supersaw
adds less offset. This is a blend of different read ages, not one fixed latency
that can be removed by delaying the dry signal. Device buffers and the existing
output limiter add their own latency. Lens count does not increase the window.

Measured with both color stages fully active (MENISCUS + into MENISCUS −,
Color 100) on a shared Linux AMD EPYC 9V74 host, C11 `-O2`, stereo 48 kHz,
256 frames (5,333 µs budget), 4,000 measured blocks after warmup:

| Lenses | Prism mean / block | Prism p99 | Prism share of block budget | Sister + Prism + limiter mean |
|---:|---:|---:|---:|---:|
| Bypass | 57.46 µs | 154.26 µs | 1.08% | 362.77 µs |
| 2 | 87.60 µs | 166.18 µs | 1.64% | 415.01 µs |
| 4 | 107.01 µs | 166.42 µs | 2.01% | 420.46 µs |
| 6 | 132.10 µs | 212.91 µs | 2.48% | 469.65 µs |
| 8 | 154.23 µs | 222.21 µs | 2.89% | 490.14 µs |
| 12 | 200.64 µs | 297.31 µs | 3.76% | 519.46 µs |
| 16 | 249.66 µs | 385.76 µs | 4.68% | 564.45 µs |
| 20 | 296.67 µs | 447.51 µs | 5.56% | 616.18 µs |
| 24 | 348.12 µs | 570.77 µs | 6.53% | 659.21 µs |

These are offline processing measurements after the 24-lens expansion, not
whole-application CPU percentages or hardware underrun certification. Shared-host
scheduling produced outliers: Prism alone reached 2.33 ms at 24 lenses, and the
full runtime reached 25.82 ms at eight. Full-runtime p99 stayed below 0.99 ms.
The shared-host stalls prevent using these measurements as a device deadline
guarantee. Physical Windows and Linux interface tests at 256 frames remain part
of live audition. Drawing the native 24-lens panel averaged **0.232 ms** over
1,000 frames, excluding SDL presentation and the desktop compositor.

## MIDI and saved projects

Use the existing **Ctrl+Shift+M** MIDI learn flow. Prism On/Off and Mode are
trigger targets; all ten sliders, including Color, Dry Level, Focus and lens count, use the
existing continuous mapping and pickup system. Lens count is rounded to 2–24;
a learned hardware fader now spans that range, while saved counts are not rescaled.
The sliders participate in the existing parameter-lock system. Individual points
and the two shape selectors are mouse gestures; they do not repurpose the global sliders or their MIDI targets.

Project state version **17** and Sister user preset version **16** persist all
Prism controls, both shapes, Color, dry level and all 24 lenses' pitch/pan/trim/mute/solo.
Edits and mute/solo bits for lenses 13–24 survive reducing the count and reloading.
Missing lens keys
use zero offsets and trim with no mute/solo; missing Dry Level uses unity;
missing shape keys use clear BI-CONVEX glass and missing Color uses 50%;
files predating Prism initialize it **off**. The older FX-slot migrations
retain their original version cutoffs, so PR99 slots and locks are not migrated
again. The main TSR29 sample format is unchanged. Sister presets on this page
are the existing full Sister presets, not a second Prism-only preset bank.

## Verification and remaining listening work

- Shape tests stream all 36 combinations with 24 lenses at 8 Hz (stability edge case), 44.1,
  48 and 96 kHz with maximum ranges through the real final limiter. A spectral
  test verifies that concave glass moves the sub-octave above the source. Noise
  checks verify low-pass energy reduction, all-pass energy retention and audible
  differences for all five colored profiles, with exact clean-anchor/dry comparisons.
- Core tests cover deterministic streaming, abrupt control/count/mode changes,
  stereo channel isolation, retained opposite-polarity stereo, finite output,
  energy compensation, final limiter ceilings, silent input, exact settled bypass,
  focus and control sanitizing. Gain regressions include bass, noise and the actual
  twelve-voice FM Unison at every even lens count from 2 through 24 in both modes.
- Test sources include sine, saw, triangle, pulse, noise, impulses and dynamically
  gated noisy tones at 44.1, 48 and 96 kHz. Spectral checks measure all 24
  intended pitches at 44.1 and 48 kHz, including the five lower voices.
- A callback-to-pixel motion regression covers both tape power states, playback
  and silence. At the reported wide-spread settings every sampled transition
  changes the diagram; Drift zero and full Focus produce stationary frames after
  settling. This checks the actual snapshot/model/render path without mouse events.
- The numbered strip is exercised at 1280×800 for all 24 lenses, including
  selection when points overlap, high-index mute/solo/trim/reset, dragging lens 24,
  inactive numbers and the MIDI/wheel count cap.
- Native application tests exercise page cycling, mouse hit regions, MIDI target
  resolution, knob changes, lens-count wheel steps, scaled-window point drags,
  Escape via workspace dispatch, out-of-window release, independent DSP changes
  and per-lens reset, muted-handle hit testing, modifier clicks, wheel trim,
  stock reset on both labels, cycling both glass shapes in both directions,
  intermediate/output snapshot differences, and the Dry Level and Color MIDI targets. Both tape power states
  capture 16,384 stereo frames through REC FILE and compare every recorded sample
  against the actual output callback, while changing pages during recording.
- Workspace regressions preserve held sample/FM notes through view changes,
  mix FM and Mosaic simultaneously, release old keys over Mosaic, and preserve
  FM latches and preview storage. Grave shortcuts return directly to the canvas;
  repeated Tab switches keep the main fullscreen window visible. Windows
  compositor appearance still needs a physical desktop check.
- Same-host output signatures match the pre-expansion engine exactly for sampled
  stereo streams at 2/4/6/8/10/12 lenses in both modes, with drift and both color
  stages active. This checks preservation of existing sounds independently of
  the 24-lens tests.
- Save/load tests cover every Prism setting, all 24 lens edits and high mute/solo
  bits, user presets, pre-expansion PR101 files, populated PR100 files
  without shape/Color keys, missing old settings
  and preservation of PR99's explicit FX slots. FM Unison and the other previously passing
  tests retain their results. Four existing failures are reproduced
  with an independently rebuilt, unchanged PR99 tree: Sister routes, source mask,
  recursion, and the canvas/grid assertion. The total is 74 passing / 78 tests.
- AddressSanitizer and UndefinedBehaviorSanitizer pass the new streaming/pitch/state
  tests with the Prism engine instrumented (LeakSanitizer disabled because this
  host cannot inspect process threads). Native screenshots were checked in
  Supersaw, Ensemble, fully focused, zero-Wet, custom-pitch, lens-mixer and
  all six glass-profile states.
- Native CDP executable integration was skipped because that runtime is absent
  in this checkout. Prism itself has no CDP dependency.

This is the first playable prototype. Listening with PIPE/breath, speech, field
recordings, actual tape, dirty hardware oscillators, complex polyphony and physical
stereo inputs remains a user audition task; synthetic coverage does not substitute
for those recordings. Granular texture and transient softening can still be
audible, especially with large hand-drawn intervals. Organ, Octaves, Cluster,
Cloud, a named Custom mode, independently selectable per-lens color profiles and a separate
quality mode are future work, not hidden options in this build.

Reproduce the focused checks and diagnostic image after configuring the normal build:

```sh
cmake --build build --target test_prism prism_probe tapesister_mosaic_controller_tests
ctest --test-dir build -R 'test_prism|tapesister_mosaic_controller_tests' --output-on-failure
build/prism_probe prism.ppm
build/prism_probe --bench
build/prism_probe --bench color
build/prism_probe --render-bench
build/prism_probe --motion
# Optional native PPM sequence (15 fps):
build/prism_probe --motion drift-frame 2 .6 24
build/prism_probe shapes.ppm 24 0 0 .8 custom 2 3 .7
# Compare output signatures with the previous revision on the same host:
build/prism_probe --signatures
```

On Windows the executables have `.exe` suffixes. The application adds no new
runtime dependency or packaging step.
