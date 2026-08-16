# TapeSister

TapeSister is a standalone sample-making laboratory for FT2 Tapehead Edition. Its compact loop is **Create or Load -> Edit -> Transform -> Vary -> Keep -> Repeat**.

## Independent Bank tiles

Each of the 16 Bank tiles is a complete editable sound object. A tile owns its audio, tuning, loop, selection, viewport, processing and edit state, and Undo/Redo history. There is no separate Source, Parent, Current, promotion, or commit workflow in the interface.

Clicking an occupied tile selects it, restores its own editor state, and auditions it. Clicking an empty tile selects that exact Create/Load destination without playing anything. A thick gold outline always marks the active editing tile; a separate cyan mark identifies a bank tile being viewed or auditioned. Clearing the active tile leaves the same empty destination selected. Bank 01 has no special protection or authority.

With no waveform selection, Create and Load replace only the selected tile. Clone makes an independent editable copy. Ordinary edits and WARP, SMEAR, and TEAR mutate only the selected tile. Vary with Chain off replaces the selected FM tile; Vary with Chain on places the result in the next empty tile and makes that result the next link in the chain. With a waveform selection, Create and Vary instead stamp a fitted FM sound into exactly that range on the active tile; audio outside the range and the tile duration stay unchanged, and Chain does not move the stamp to another tile.

Over the waveform, the mouse wheel keeps pointer-anchored zoom and Shift+wheel scrolls
horizontally. Ctrl+wheel rotates the editable waveform through the configured coarse
number of zero-crossing candidates; Ctrl+Shift+wheel uses the fine count instead.
Set `rotate_wheel_fine` (1–20, default 5) and `rotate_wheel_coarse` (20–100,
default 50) in the `[Waveform]` section of `tapesister.ini`.

The compact **WARP** control is a spring-loaded, deterministic offline Transform for
the selected tile, or only for the fixed waveform selection when one is active. Hold and drag
right to hear increasing read-position bend, ease left to hear less, and release to
commit one Undo/Redo action; releasing at zero restores the exact gesture-start audio.
Ctrl+wheel over WARP provides fine exploration and commits when Ctrl is released.

**TEAR** is the third spring-loaded native Transform. It groups nearby zero-crossing
regions into practical waveset packets, then progressively reveals one deterministic
field of local swaps and reversals. It is length-preserving and selection-aware;
Ctrl+wheel over TEAR provides the same fine frozen-start exploration as WARP and SMEAR.
Every preview auto-auditions from the same frozen starting point, and the control
springs back to zero after commit or Escape cancellation.

## Zero-snapped selection and loop modes

Every mouse-created or adjusted selection endpoint snaps live to the nearest zero crossing in the selected tile. Magenta pixels mark the visible crossings directly on the waveform. The highlight always shows the actual snapped range used by Reverse, Normalize, gain, fades, Crop, and Set Loop. `Ctrl+A` deliberately keeps exact sample boundaries. If a sound has no mathematical sign crossing, selection falls back deterministically to its closest-to-zero sample.

The Loop page turns the current selection into one loop, clears it, plays it continuously, selects **Forward**, **Reverse**, or **Ping-Pong** travel, and sets a 0–50 ms wrap crossfade. If no selection exists, Set Loop first selects and loops the exact whole tile. Blue boundaries and handles distinguish the loop from the purple/cyan selection; direction arrows show the active mode directly in the waveform. Either handle can be dragged live, remains zero-snapped, and automatically becomes the opposite endpoint when crossed. Computer and ordinary onscreen notes sustain the loop only while held; dragging a loop flag never releases them. Play Loop continues until Space or Escape. Loop range, mode, and crossfade belong to the tile and participate in its Undo/Redo history.

The lower panel switches between **KEYS** and **BANK**. KEYS provides the five-voice chord/drone keyboard: Shift-click toggles latched notes, while an ordinary click clears the chord and returns to momentary audition. Sustained voices follow the selected tile.

TapeSister starts in FT2-style borderless desktop fullscreen. Escape first cancels an active dialog, preview, or editing gesture; otherwise it stops playback and opens the exit confirmation. Closing the window uses the same confirmation, with an explicit warning whenever the instrument or collection differs from the last opened or saved TSR project.

## Pitch and tuning

The **Tune** page gives the selected tile its own pitch readout and ±100-cent audible Trim. The two-octave keyboard pitches audio relative to that tile's accepted root instead of assuming the first C is always unity. The default root is C3 (MIDI 48). **Down** lowers both the held sound and displayed note/frequency; **Up** raises all three; moving Trim right sharpens them. Shift-right-clicking an onscreen key assigns the tile's root; held and latched notes retune in place without restarting.

**Suggest Pitch** analyzes the snapped Selection first, then the Loop, then the whole selected tile. It temporarily places the suggested mapping on the keyboard so new, held, and latched notes can audition it immediately; the Tune readout follows that preview, while saved tuning and Undo history remain untouched. A second explicit click accepts it. Escape cancels the preview and still performs Stop All; Space stops audition without discarding the preview. Quiet, noisy, or unstable material is rejected rather than forced into a misleading note. Manual tuning remains authoritative.

Root and fine tuning participate in the selected tile's state and Undo/Redo history. Selected-tile and Collection WAV exports write a standard `smpl` unity-note/pitch-fraction chunk, and WAV import reads it when present. The same chunk carries loop start/end/type; importing the WAV restores that loop in TapeSister. User-captured TSP2 recipes optionally carry tuning; the eight factory recipes remain processing-only and never retune a sound unexpectedly.

## FastTracker handoff and configuration

**Config** opens one compact modal with blank-safe editable paths for the sample root, FastTracker executable, and FT2 exchange folder. The values persist in portable `tapesister.ini`; Tab or Up/Down changes field, the usual caret keys edit anywhere in a path, Ctrl+Backspace clears it, and **Use CWD** copies the current directory. The configured sample root becomes the file browser's starting directory while normal browsing still remembers later navigation.

Fresh launches also use two boolean startup settings (both default to `1` when absent): `startup_welcome_sample` installs `assets/tapesister_welcome.wav` as the ordinary imported working sound in bank 01, and `startup_welcome_autoplay` auditions it once after the splash closes. Set autoplay to `0` to keep the waveform without the greeting, or sample to `0` to start with an empty selected bank 01. Command-line WAV/TSR loading takes precedence and is never overwritten by the welcome artifact. See `tapesister.ini.example`.

**Send FT2** exports every occupied collection slot into an automatically numbered folder under the exchange path (falling back to the sample path), then launches the configured FastTracker executable without shell interpolation. No existing handoff folder is replaced. The handoff remains deliberately file-based: TapeSister does not link against tracker state, and FT2's existing folder importer decides whether the collection replaces the current instrument, fills another instrument, or becomes a launcher bank.

Every handoff WAV includes root/fine-tune metadata plus loop start, inclusive end, and standard Forward/Ping-Pong/Backward type. FT2 already reads the loop record, so forward and ping-pong collection members arrive with looping enabled instead of requiring manual flags. Its current WAV loader treats the standard backward type as ping-pong; exact reverse-loop interpretation and `smpl` root-note adoption belong to the reciprocal FT2-side handoff slice.

**Config -> Palette** opens a live 12-color RGB editor using the same named fields as FT2 Tapehead Edition. **Import TH** reads `tapehead.pal`, **Export TH** writes it, and **Save TS** writes `tapesister.pal`, which TapeSister automatically reloads on the next launch. If that file is absent, the bundled `assets/tapehead.pal` is used. `TAPESISTER_PALETTE` can override the TapeSister palette path. Older six-color Tapehead palettes are accepted, with the additional waveform colors inheriting `PatternText`.

## Sound collection and variation

All 16 slots are peers. A newly created or imported sound fills the currently selected destination. Any empty slot can also capture useful zero-aligned material from the active tile:

- Shift-click an empty slot to capture the full active tile;
- Alt-click an empty slot to capture the active Loop, including its crossfade setting; or
- Ctrl-click an empty slot to capture the snapped Selection; or
- Ctrl+Shift-click an empty slot to Clone the active tile's complete editable state and history.

Click an occupied slot to select its independent editable state and audition it. Click an empty slot to select that exact CREATE/LOAD destination without auditioning. A slot with saved loop metadata auditions continuously in its own Forward, Reverse, or Ping-Pong mode, while one without a loop remains a one-shot. A blue mark identifies looped slots. While a filled bank waveform is visible, the Loop page edits that slot directly. Right-click any occupied slot to rename it, or Shift-right-click to clear it. **Clear All** requires a confirming second click and empties all 16 peer slots.

The **Variation** page exposes one continuous Range and the **Chain** switch. Vary requires a selected FM tile. With Chain off, Vary replaces only that tile. With Chain on, Vary writes to the next empty tile, selects it, and uses it as the basis for the next chained result. If the bank is full, chained Vary refuses instead of overwriting another tile.

The top **Export** button and `Ctrl+E` ask whether to export the selected tile or the complete Collection. Collection export writes every occupied slot as a numbered, loop-aware WAV into a new folder. Existing folders are never silently replaced. A failed export removes the partial files and folder.

## Six-operator FM source proof

Create produces a deterministic six-operator FM tile. Each seed selects a complete hidden patch from curated Structure and Ratio sets, plus Depth, Shape, feedback, and a short transient layer. Vary changes that stored FM recipe according to Range. A later focused slice can expose a small set of musically useful macros without adding an operator table to the primary interface.

## Physical tape gestures

Start any tape gesture inside the existing snapped selection. A cyan ghost waveform follows the pointer and previews the zero-crossing-aware destination before release:

- Shift + left-drag copies and peak-matches its merge with the audio underneath;
- Shift + right-drag copies and overwrites the audio underneath;
- Ctrl + left-drag lifts/moves and uses the same peak-matched merge at the destination; and
- Ctrl + right-drag lifts/moves and overwrites at the destination.

Where source and existing audio overlap, Mix measures the source peak and underlying destination peak, sums the waveforms normally, then applies one gain to the complete merged region so its peak equals the louder original. This preserves the stronger layer's level without the thinning of a fixed average or the clipping of uncontrolled addition. Material extending beyond the existing sample keeps its original source level. Move captures the complete source before clearing it, so overlapping placement cannot corrupt itself. The lifted range remains the same duration and is filled with silence, with a roughly 1 ms protective fade at exposed edges. Every completed drag is one replayable Undo/Redo operation.

## Processing recipes and shaping

The lower panel now cycles **KEYS**, **BANK**, **RCPE**, and **INGR**. INGR is an empty navigation scaffold for future ingredient shelves; entering it does not alter the selected tile, bank contents, recipes, or Undo history. RCPE contains eight immutable factory recipes and eight user slots. Click a filled slot to apply its processing to the selected tile as one undoable render. Shift-click an empty user slot to capture the live processing shelf, right-click a filled user slot to rename it, and Shift-right-click to clear it. Factory recipes and their names remain immutable. Manual shelf changes remove the active-slot highlight without altering the stored recipe.

The compact **LOOP** audition button repeats a fixed selection when one exists, otherwise it follows the visible tile view as that view is zoomed or panned. A short boundary crossfade uses the existing audition engine. While LOOP is active it remains the audition owner, so WARP, SMEAR, and TEAR continue publishing spring-loaded previews without their usual timed playback retriggers. Loading or creating another sound, selecting a bank tile, Stop, or quit cancels workbench LOOP ownership.

Portable `.tsp` files contain named processing settings and, for user captures, optional tuning metadata—never tile audio, crop, selection, loop, tape edits, or bank members—so the same treatment can be applied to unrelated material. Factory recipes omit tuning. While RCPE is visible, Save or `Ctrl+S` writes a TSP instead of a full project. Load, drag-and-drop, and command-line opening accept TSP files, add them to the next free user slot, and apply them without replacing the selected tile. Full `.tsr` projects remain the self-contained way to save a complete bank.

The **Shape** page combines a bypassable resonant Lowpass, Highpass, or Bandpass filter with a bypassable Tape, Clip, or Fold shaper. Cutoff uses logarithmic travel, while resonance, drive, and wet/dry mix expose the musically useful range. These deterministic stages live in the processing recipe and render before Delay and Space; existing ordered tape placements remain downstream. Held and latched notes are remapped across recipe application just like other Current rerenders.

## DSP shelf

The switchable Noise, Shape, Delay, and Space pages preserve the compact interface while exposing useful sound-shaping depth:

- deterministic white, pink-ish, brown-ish, and metallic noise;
- resonant lowpass, highpass, and bandpass filtering with explicit bypass;
- Tape, Clip, and Fold nonlinear shaping with drive, mix, and explicit bypass;
- mono delay with time, feedback, damping, mix, and explicit bypass;
- compact mono Schroeder-style ambience with decay, damping, mix, and explicit bypass;
- one deterministic offline render chain for display, audition, export, Reset, Commit, Undo, and Redo.

Every stage is equally available to created and imported Sources. Bypass is explicit state, not a zero-value convention.

## Editor slice

- drag across the waveform to select a range;
- mouse-wheel zoom anchored to the sample beneath the pointer;
- Shift+wheel panning, direct `=` or `+` / `-` keyboard zoom, arrow-key panning, and `0` Show All;
- Play All, Play Selection, and Play Displayed;
- Zoom Selection and Show All;
- nondestructive Crop with Source preservation;
- selection-aware Reverse, Normalize, 3 dB Amplify Up/Down, Fade In, and Fade Out;
- cross-tile Copy, ripple Cut, exact Paste, and duration-preserving Fit Paste;
- selection-scoped Create and Vary FM stamping;
- Undo and Redo for processing, crop, and sample-edit operations;
- two-octave computer and onscreen keyboard audition;
- mono PCM/float WAV loading, including multichannel fold-down;
- self-contained project saving with all bank slots, tuning, editor state, selection, loop metadata, edit timelines, and DSP parameters; and
- mono 16-bit selected-tile export with sampler-compatible root/fine-tune and loop metadata.

Sample edits are deterministic and tile-owned. With no selection they affect the whole selected tile; with a selection they affect only that range.

The audio clipboard survives tile changes for the lifetime of the app, while every tile keeps its own selection. **Paste** replaces the destination selection with the copied sound at its exact duration: a shorter destination makes the tile grow and a longer destination makes it shrink. With no destination selection, Paste inserts at the source selection's original time position, clamped to the end of a shorter target. **Fit** stretches or compresses the copied sound into the destination selection, keeping the tile length fixed and deliberately changing playback speed and pitch. Cut ripple-removes the selected range; cutting the whole tile is refused in favor of the explicit Clear command. All mutations participate in the destination tile's Undo/Redo history.

Amplify Up is deliberately bounded by hard clipping. Amplify Down attenuates the result without reconstructing clipped peaks, preserving that flattened distortion as a repeatable sculpting operation.

## File browser

Load, Save, and Export now open one shared FT2-informed browser rather than writing fixed filenames or requiring a typed path:

- Load lists WAV source files, self-contained `.tsr` projects, and portable `.tsp` processing recipes and preserves the existing instrument if any is invalid;
- Save lists directories and `.tsr` projects, or `.tsp` processing recipes while RCPE is visible;
- Export lists directories and WAV files;
- mouse wheel, draggable scrollbar, Up/Down, Page Up/Down, Home/End, and row clicking navigate long directories;
- double-click or Enter opens a directory, WAV, TSR project, or TSP recipe;
- Save and Export remember the current directory, provide filename entry with a focus-aware blinking caret, support Left/Right/Home/End navigation plus insertion, Backspace, and Delete at the caret, and append the proper extension;
- replacing an existing file requires a deliberate second Save/Export action; and
- completed Save/Export files replace their destination atomically, so a failed write does not leave a partial result.

TSR16 stores every occupied tile as a complete independent object: audio, private render baseline, tuning, loop, selection, viewport, processing and edit timelines, Undo/Redo stacks, and the audio patches needed to replay Paste and FM stamp history. The selected tile may also be empty, so saving never invents a fallback or gives Bank 01 special status. TSR6 through TSR15 remain loadable for compatibility. TSP2 remains audio-independent and therefore complements rather than replaces the project format; TSP1 remains loadable as processing-only.

The browser owns all keyboard and mouse input while open. Escape or Cancel closes it without changing the sound or writing a file. WAV, TSR, and TSP files can also be dragged onto the window or passed on the command line.

The default colors come from `assets/tapehead.pal`, and the live palette remains compatible with Tapehead's named palette fields. The interface remains standalone: FT2 and the archived prototype are reference shelves, not inherited architecture, and TapeSister does not depend on or modify FT2 Tapehead Edition.

## Build on Linux

```bash
sudo apt install build-essential cmake libsdl2-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/tapesister
```

The small Makefile is also available:

```bash
make test
make
./tapesister
```

Pass a WAV, TSR, or TSP path on the command line, drag it onto the window, or choose it through **Load**.

## Keys and files

- Lower octave: `Z S X D C V G B H N J M`
- Upper octave: `Q 2 W 3 E R 5 T 6 Y 7 U`
- Stop all: `Space`; `Escape` cancels the active gesture/dialog first, otherwise opens exit confirmation
- Load browser: `Ctrl+O`
- Save browser / choose Export Selected Tile or Collection: `Ctrl+S` / `Ctrl+E`
- Undo / Redo: `Ctrl+Z` / `Ctrl+Y`
- Select all: `Ctrl+A`
- Copy / Cut / exact Paste: `Ctrl+C` / `Ctrl+X` / `Ctrl+V`
- Fit Paste into the target selection: `Ctrl+Shift+V`
- Reverse / Normalize: `Ctrl+R` / `Ctrl+N`
- Fade in / Fade out: `Ctrl+I` / `Ctrl+U`
- Amplify up/down 3 dB: `Ctrl+Up` / `Ctrl+Down`
- Zoom in/out: `=` or `+` / `-`
- Pan waveform: `Left` / `Right`
- Show all: `0`
- Browser navigation: `Up` / `Down`, `Page Up` / `Page Down`, `Home` / `End`
- Browser parent directory: `Backspace` while the file list is focused
- Browser confirm/cancel: `Enter` / `Escape`
- Build/toggle a five-note chord: `Shift` + onscreen-key click
- Create FM tile / vary selected FM tile, or stamp the selected range: **Create** / **Vary**
- Config paths: top **Config** button
- Tapehead-compatible colors: **Config -> Palette**
- Export collection to exchange folder and launch FT2: top **Send FT2** button

Multiple loops, automatic loop candidates, the zero-crossing loop-maker transformation, deeper synthesis and modulation stages, and multi-source variation remain separate, visually verified slices.
