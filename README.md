# TapeSister

TapeSister is a standalone sound-making, sample-sculpting, and performance instrument
for Windows and Linux. It combines independent sample tiles, generative six-voice FM,
Mosaic event sequencing, waveform editing, native DSP and curated CDP8 processes, real-time recording, Sister
Machine's rolling tape memory, Fallout deterioration, and a four-slot effects pedalboard.

Its basic creative loop is simple:

> Create or load → sculpt → vary → perform → capture → repeat.

![TapeSister main sample workflow](docs/images/manual/main-sample-workflow.png)

## Documentation

- [Complete User Manual](docs/USER_MANUAL.md) — workflows, every major instrument,
  recording, routing, saving, and performance techniques.
- [Quick Reference](docs/QUICK_REFERENCE.md) — keys, mouse gestures, control ranges,
  capture modes, signal placement, and file types.
- [Mosaic](docs/MOSAIC.md) — free event placement, independent pitched loops,
  event editing, shared effects routing and REC FILE.
- [CDP Portal](docs/CDP_PORTAL.md) — explore processes with source/result waveforms,
  save reusable chains, shape named macros, explore four starter chains, and
  build a personal process-pin bank. Thirteen verified processes support stereo.
- [Design Charter](DESIGN_CHARTER.md) — the principles behind the instrument.
- [Technical documentation](#technical-documentation) — architecture, exchange,
  packaging, and certification notes.

## What TapeSister contains

### Independent sample tiles

Every Sample page holds 16 complete sound objects. Each tile owns its audio, mono/stereo
shape, tuning, loop, selection, viewport, processing, protection state, and private
20-step Undo/Redo history. Add Sample pages as the collection grows.

Click an occupied tile to select and audition it. Click an empty tile to select a
destination. Double-click an empty tile to create editable silent tape.

**LOAD** accepts WAV, FLAC, MP3, and Ogg Vorbis, then opens an immutable waveform
preview before changing the destination tile. Any other file can be interpreted as
raw sample data with live controls for encoding, byte order, channels, sample rate,
offset, and normalization. That makes arbitrary bytes available as sound material and
lets the same source be auditioned through several interpretations before import.

### Create and Variation

Left-click CREATE to render a fresh deterministic six-voice FM sound. Right-click
to explore CDP variations of the retained clean waveform; middle-click cancels the
variation and restores that clean sound. Successive CDP rolls use the same source.
VARY answers the material that
actually exists now: after drawing, tape gestures, tuning, pasting, or other destructive
shaping, the audible waveform becomes the source of the next variation.

For a precisely timed sound:

1. Double-click an empty tile.
2. Select the desired duration in the silent canvas.
3. Press CREATE.
4. Explore VARY and its Range.
5. Press CROP when the sound is ready.

CHAIN can place successive relatives in empty tiles or advance variation stamps through
a selected timeline.

![A bank developing through related variations](docs/images/manual/create-and-variation.png)

### FM Logic

FM Logic exposes the six-voice genome behind Create: pitch ratios and scales, ten
routing structures, waveform families, per-voice LFOs, filtering, interaction modes,
feedback, transient behavior, mutation permissions, Drone, and Extreme ranges.

The live preview can be played from QWERTY or MIDI before it is applied. MAKE BANK
creates a complete 16-sound family in one atomic operation.

![Six-voice FM Logic](docs/images/manual/fm-logic.png)

### Mosaic

Mosaic places sample events freely on a top-to-bottom timeline. Drag sources into
place, stretch events to allow more loop repetitions, and overlap them side by side.
Each event has its own loop/one-shot setting and up to five pitched notes with
independent playback speeds. A chord can turn one fragment into an evolving texture
as its different loop lengths move in and out of alignment.

![Mosaic's free canvas with independent pitched events](docs/images/mosaic.png)

Double-click an event to use the familiar waveform editor. Audio edits accumulate
until you return to Mosaic, then **NEW TILE** preserves the original or **UPDATE
TILE** changes only that event. NEW TILE reveals the new card and also stores a
reusable copy in the regular Sample banks. Copies share source audio until an edit creates a
new version. Sources browses all Sample banks and the event audio versions.

Shift-drag a box to select a group; drag to move it or Shift-drag a selected tile to
copy the group. M/S mutes or solos, middle-click seeks, and FOLLOW scrolls with the
playhead. **Shift+grave** visits Mosaic and returns to the previous main or FM
workspace. Its colors and selection highlight are editable in CFG → PALETTE.

The footer controls level, stereo balance and fade-in/out for an event or selection.
A centered global SPEED slider covers 0.5×–2×, changing pitch and arrangement timing
together with smooth live movement. These settings save with the project. CLEAR SOLO
remains visible while any card is soloed, including cards outside the view.

Mosaic plays through the existing global Fallout, pedalboard, and Sister routing.
**REC FILE** records the final stereo output while you arrange or edit. Saving
preserves the arrangement and shared audio versions in the project folder. See the
[illustrated Mosaic chapter](docs/USER_MANUAL.md#mosaic) and
[controls reference](docs/QUICK_REFERENCE.md#mosaic).

### Waveform and transform tools

The editor includes selection-aware Copy, Cut, Paste, Fit, Crop, Reverse, Normalize,
gain, fades, canvas resizing, Draw, Drone Maker, loops, tuning, Warp, Smear, Tear,
Body, Edge, Drift, native DSP, and curated offline CDP8 processes.

Transforms render outside the audio callback and remain previews until accepted. Failed
or canceled work leaves the tile untouched.

### Performance and recording

Tiles, loops, QWERTY notes, MIDI notes, FM, and staged chords can be layered while
TapeSister records the final performance into a new tile. Shift-clicked source groups
fan notes across several tiles. Plain-clicked one-shots and loops form a separate live
performance layer. **Shift+S / SUSTAIN** selects whether QWERTY and MIDI notes
release with the key or continue to their end (or keep looping). This setting is
shared with FM, Portal, import preview, and Sister Machine; Portal and file previews
now accept MIDI notes. See [Keyboard Sustain](docs/KEYBOARD_SUSTAIN.md).

The main and Sister Machine **M/S** buttons mirror one capture-format setting:

- **M** stores `0.5 × (L + R)` mono.
- **S** preserves stereo.

![Capturing a stereo performance into a tile](docs/images/manual/capture-to-tile.png)

The separate REC BANK records either configured external input or the internal FM
performance bus with threshold, pre-roll, tail, and optional sequential Chain recording.
KEEP moves completed REC tiles into the Sample collection.

Every completed real-time take is also preserved as a timestamped 32-bit float WAV in
`Captures/`.

## Sister Machine

Sister Machine is a live 5–60 second rolling stereo tape memory with one moving write
head and three playback heads. It accepts selected tiles, FM, external input,
audition/preview audio, and Tapehead's direct Live Link. Sources routed into Sister
behave like hardware inserts: they leave their direct path and return through Sister's
DRY/WET monitor section.

Its controls include:

- Roll, Hold, Clear, and live buffer resizing;
- H1 anchored time/feedback;
- H2 and H3 movable reverse/forward-rate heads;
- Wow, Drop, Duck, decorrelation, width, and filtering;
- Input, Dry, Wet, internal Out, Erase, and Ghost Tone;
- Soak/Bleed stereo tape weave;
- source and effects-return mixers;
- isolated H1/H2/H3 or complete MIX capture;
- tile, Overdub, or long-form WAV/RF64 file destinations;
- parameter locks and named presets.

Closing the Sister window does not stop the machine. POWER is the explicit audio-engine
boundary.

![Sister Machine during a routed capture](docs/images/manual/sister-machine.png)

## Four-slot FX pedalboard

The pedalboard holds four independent instances of Reverb, Delay, Distortion, Grain,
or Empty. Effects can be duplicated and reordered. Every slot chooses exactly one
placement:

- PRE — newly arriving material before the tape write;
- H1, H2, or H3 — one playback head and its recurrence;
- POST — after Sister MIX and Fallout.

Each slot has Mix and ±12 dB Gain. Reverb reaches approximately two-minute decay,
Delay spans about 8–2000 ms, and Grain ranges from isolated fragments to dense clouds.

Effect and Master transitions span 10 ms to 60 minutes. Live type, placement, and order
changes morph without stopping audio. FX Feedback returns the effect contribution into
the rolling write and reaches 135% for deliberately self-building structures.

**MASTER FX** bypasses both the pedalboard and Fallout, including Fallout feedback,
over its selected transition. Individual settings are preserved. With Sister off,
ordinary playback and Mosaic still use Fallout and POST slots; PRE and head slots
require Sister. Turning Sister on preserves the shared effect setup.

![Four reorderable and independently placed FX slots](docs/images/manual/fx-pedalboard.png)

## Fallout

Fallout is a stereo deterioration instrument between Sister's completed MIX and the
POST pedalboard location. With Sister off, it processes ordinary playback, including
Mosaic, before POST effects. Drop, Pan, Skip, Bit, Pitch, colored Noise, and Feedback can
be combined or modulated.

Its three independent transition clocks—Preset, Parts, and Master—each span 10 ms to
60 minutes. A shared sine LFO spans one cycle per hour through 10 Hz. Rise can repeat
as a saw or run once over 1 second to 4 hours. A target matrix assigns either modulator
to Mix, Feedback, Noise, and the event parameters of each deterioration process.

Those extremes are performance tools: effects can emerge across a movement, Fallout
can deteriorate an hour-long set almost imperceptibly, or a one-shot Rise can coordinate
Mix, Feedback, and Noise toward a formal climax.

![Fallout deterioration and long transition controls](docs/images/manual/fallout.png)

![Fallout LFO and Rise routing](docs/images/manual/fallout-modulation.png)

## Master output and limiter

The linked-stereo limiter, final OUT fader, L/R meter, and gain-reduction readout remain
visible across the main and Sister windows. Final order is:

> TapeSister mix → limiter → OUT fader → meter and FILE OUT.

The limiter is a safety boundary for extreme synthesis and feedback. Gain should still
be managed at Sister INPUT/internal OUT, the source mixer, effect slots, and feedback
controls.

## Portable project folders

Saving `Terra Night.tsr` creates one movable `Terra Night/` folder containing:

```text
Terra Night/
├── Terra Night.tsr
├── manifest.txt
├── sister-state.ini
├── project-data/
└── samples/
```

The TSR and project data preserve complete editable state. `samples/` contains an
ordinary 16-bit PCM WAV for every occupied Sample and REC tile, with standard tuning
and loop metadata for extraction or interchange.

Mosaic's layout is stored in `project-data/mosaic.tsm`; its shared source versions
are lossless 32-bit float `mosaic-000.wav`, etc., in the same directory.

Move, share, or back up the complete named folder. The persistent `Captures/` archive
remains outside projects by design.

![Project overwrite confirmation](docs/images/manual/project-save.png)

## MIDI Learn and TapeHead status

Current MIDI support includes Note On/Off, 7-bit CC, channel-specific 14-bit pitch
bend, velocity, channels 1–16 or Omni, All Notes Off, a 64-voice sample pool, and the
shared QWERTY/FM performance path. `Ctrl+Shift+M` opens Ableton-style MIDI Learn across
both windows; safe controls are highlighted, mappings use Pickup takeover by default,
and assignments are stored globally in `tapesister.ini`. Ice-cyan controls are
available, pale ice is armed, and electric blue is already mapped. Button targets
accept Note or CC while continuous targets accept CC or pitch bend, so high-resolution
fader jitter cannot steal a pending tile assignment.

![MIDI Learn across the active tile positions](docs/images/midi-learn-main.png)

The sixteen tile targets always address positions 1–16 on the active Sample page.
Sister Machine exposes its live tape, mixer, Fallout, pedalboard, transition, timer,
LFO, and Rise controls while excluding destructive editing and file operations. See
the [MIDI Learn chapter](docs/USER_MANUAL.md#midi-learn-and-performance-controllers)
for controller setup and unmapping behavior.

FT2 LINK provides atomic folder-based exchange with Tapehead. For real-time audio,
Tapehead can select **TapeSister Live Link** as its output and appear as TapeSister's
fifth musical source without opening another hardware device. See the
[Live Link guide](docs/LIVE_LINK.md).

`Ctrl+Tab` moves directly between running TapeSister and Tapehead instances without
changing either workspace. It remains available when Live Link audio is off.

## Build on Linux

Install dependencies once:

```bash
sudo apt install build-essential cmake git libsdl2-dev libasound2-dev
```

Build the application and bundled CDP8 runtime:

```bash
bash build.sh
./build-linux/tapesister
```

Plain `make` delegates to the same complete release build. Development checks remain:

```bash
make test
make stress-sister
make benchmark-sister
```

Use `--diagnostic-audio` for optional callback, device, buffer, and external-input
diagnostics.

### Windows audio coexistence

TapeSister defaults to `audio_backend=Auto`. On Windows, CONFIG can also select
WASAPI or DirectSound; save and restart after changing the backend. Auto/WASAPI
shared mode is the recommended starting point when TapeSister must coexist with
TapeHead, REAPER, or VB-CABLE. TapeSister does not provide native ASIO: running
beside REAPER/ASIO is an interoperability case and still depends on the interface
driver's sharing and exclusivity rules.

Configured and active devices are tracked separately. A named output or input is
never silently replaced with the system default. If a named output fails at startup,
TapeSister offers Retry, a temporary explicitly approved system-default output,
continued operation without physical output, or Exit. Capture hardware is opened only
while EXT, external recording, or input monitoring needs it; capture loss does not stop
tiles, FM, audition, or Sister's internal sources. CONFIG shows connection state, while
stderr and `tapesister-diagnostic.log` record the active backend, real SDL device IDs,
negotiated format, rate, channels, buffer, fallback state, and last error.

See the [Windows audio validation checklist](docs/WINDOWS_AUDIO_VALIDATION.md) before a
release build is signed off on physical Windows hardware.

## Build on Windows

From an MSYS2 **UCRT64** terminal with CMake, Ninja, SDL2, and the UCRT64 toolchain:

```bash
powershell.exe -ExecutionPolicy Bypass -File scripts/build-windows-portable.ps1
```

That single command builds TapeSister and the pinned native CDP8 runtime, stages the
Windows DLLs and assets, and creates the friend-ready archive at
`dist/TapeSister-Windows-x64.zip`. Its `TapeSister.exe` includes the application icon
and Windows product/version metadata. Extract the whole archive before running it; do
not separate the executable from its DLLs, `assets`, `cdp`, or `licenses` directories.

For an ordinary developer build without creating the ZIP, continue to use
`bash build.sh`. It stages the same runtime under `build-windows/`. Set
`TAPESISTER_BUILD_JOBS` to change the default two-job build.

## Technical documentation

- [Realtime Capture](docs/CAPTURE_WORKFLOW.md)
- [FM Source Model](docs/FM_SOURCE_PLAN.md)
- [Native DSP Transform](docs/DSP_TRANSFORM.md)
- [Curated CDP Transform](docs/CDP_TRANSFORM.md)
- [Bundled CDP8 Runtime](docs/CDP8_RUNTIME.md)
- [FT2 Exchange](docs/FT2_EXCHANGE.md)
- [Tapehead Live Link](docs/LIVE_LINK.md)
- [Sister Audio Buses](docs/SISTER_MACHINE_AUDIO_BUSES.md)
- [Sister Headless Engine](docs/SISTER_MACHINE_HEADLESS_ENGINE.md)
- [Sister Live Routing](docs/SISTER_MACHINE_LIVE_ROUTING.md)
- [Sister Performance State](docs/SISTER_MACHINE_PERFORMANCE_STATE.md)
- [Sister Live Buffer](docs/SISTER_MACHINE_LIVE_BUFFER_CANVAS.md)
- [Sister Soak/Bleed](docs/SISTER_MACHINE_SOAK_BLEED.md)
- [Sister Fallout](docs/SISTER_MACHINE_FALLOUT.md)
- [Sister Realtime Audit](docs/SISTER_MACHINE_PR11_REALTIME_AUDIT.md)
- [Universal TapeSister/TapeHead Palette](docs/UNIVERSAL_PALETTE.md)
- [Windows Audio Validation](docs/WINDOWS_AUDIO_VALIDATION.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
