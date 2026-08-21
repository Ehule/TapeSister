# TapeSister

TapeSister is a standalone sample-making laboratory for FT2 Tapehead Edition. Its compact loop is **Create or Load -> Edit -> Transform -> Vary -> Keep -> Repeat**.

## Paged independent Bank tiles

Each Sample page contains 16 complete editable sound objects. A tile owns its audio, tuning, loop, selection, viewport, processing and edit state, and Undo/Redo history. There is no separate Source, Parent, Current, promotion, or commit workflow in the interface. Press `1` while Sample Tiles is already visible to cycle `SAMPLE 1/N -> SAMPLE 2/N -> ...`; only available Sample pages participate, and the compact bank hint shows the current page. `Shift+1` opens the external recorder directly without putting the REC BANK into that cycle.

Clicking an occupied tile selects it, immediately restores its own editor state for waveform selection, and auditions it. Clicking an empty tile selects that exact Create/Load destination without playing anything; double-clicking it activates a silent editable canvas. When the audio clipboard has a source timeline, the canvas adopts that full duration and sample rate so Paste-in-place lands at the same time. With no clipboard it defaults to one second at 44.1 kHz. A thick Active Tile-color outline always marks the active editing tile; a separate cyan mark identifies a bank tile being previewed. Clearing the active tile leaves the same empty destination selected. Bank 01 has no special protection or authority.

With no waveform selection, Create and Load replace only the selected tile. With a selection on an occupied sound or silent canvas, choosing a WAV from Load asks for exact Paste, duration-preserving Fit, or Cancel; Paste and Fit are both Undo/Redo edits. TSR and TSP loading retain their existing behavior. Clone makes an independent editable copy. Ordinary edits and WARP, SMEAR, and TEAR mutate only the selected tile. Vary with Chain off replaces the selected FM tile; Vary with Chain on places the result in the next empty tile and makes that result the next link in the chain. With a waveform selection, Create and Chain-off Vary stamp a fitted FM sound into exactly that range on the active tile. Chain-on Vary stamps there, advances the same-width selection to the right with a tiny overlap, and uses the new variation as the source of the next stamp.


## External REC bank

Press `Shift+1` to open the complete 16-tile **REC BANK** directly. The window title
and bank hint identify the temporary recording workspace. Every Sample page is parked
intact while REC is active; pressing plain `1` returns to the current Sample page.
Recorded tiles are ordinary TapeSister tiles, so the same selection, loop, DSP, CDP,
export, and FT2 Link tools work immediately after capture.

Choose **SRC EXT** for the configured capture device or **SRC SYNTH** for the internal
FM performance bus, then select an empty REC tile and click **REC ARM**. Both sources keep a circular
pre-roll, and waits without writing a take until the signal crosses
`record_threshold_db`. Silence plus the configured tail ends the take automatically;
**STOP REC** or Space keeps a shorter take, while Escape or clicking the armed button
cancels it without changing the tile.

SRC SYNTH records the FM LOGIC QWERTY/latch performance directly, without opening an
input device or using operating-system loopback. It records only synth voices, not
other tile playback. SRC EXT retains hardware input and the optional dry MONITOR;
monitoring is unnecessary and disabled for SRC SYNTH.

While armed, the main waveform area becomes a live input display with current and
held-peak dBFS, clipping feedback, and a threshold line driven by the recorder's exact
trigger amplitude. Once triggered, a bounded min/max envelope grows across the display
without drawing or rescanning the take in the audio callback. **MONITOR** adds the dry
input to TapeSister's output after the internal performance/CAPTURE path, so it cannot
enter TapeSister effects or internal CAPTURE and never controls whether metering or
recording works. Monitoring defaults off, remains visibly latched while enabled, and is
turned off when leaving the REC BANK; use headphones to avoid microphone feedback.

**KEEP** copies every occupied REC tile, in tile order, into the first empty Sample
slots across the existing pages, creating another page when required. Existing Sample
tiles are never overwritten. Only after every copy succeeds is the REC BANK cleared,
making it a reusable capture buffer without using save/reload as a migration step.

When **CHAIN** is on in the Family page, a completed take advances to the next sequential
empty REC tile and immediately rearms. This makes it possible to arm once, walk to an
external synth or microphone, make a sound, wait for silence, change the source, and
continue until the bank is full. CHAIN stops rather than overwriting an occupied tile or
wrapping past tile 16.

`[External Recording]` in `tapesister.ini` controls the input device and channel plus
threshold, pre-roll, silence, tail, and maximum take length. `record_input_channel=0`
mixes the device inputs to mono; `1` records the first/left channel and `2` the
second/right channel. These settings are intentionally editable without recompiling.

Every completed take is also written immediately as a float WAV under the
human-readable `Captures/` folder, with an `INPUT_YYYY-MM-DD_HHMMSS_mmm_...wav`
or `SYNTH_YYYY-MM-DD_HHMMSS_mmm_...wav` name. TapeSister never purges this folder and ordinary edits, Clear, KEEP, project
loading, and project saving never rewrite or delete those originals. Set the optional
`TAPESISTER_CAPTURES` environment variable to place the archive somewhere else.
See [`docs/CAPTURE_WORKFLOW.md`](docs/CAPTURE_WORKFLOW.md) for archive, threading,
project-bundle, and hardware-validation details.

## Capture a performance to a new tile

Capture prints TapeSister's final live audition mix into an independent Bank tile. In
**BANK**, double-click an empty tile to make blank tape, set its capacity with the
normal canvas controls, then click **CAPTURE**. Select a different occupied source
tile and deliberately start its Play, Loop, playhead, or first keyboard note. Arming
stops any audition that was already sounding, so this new trigger starts both the
performance and recording on frame zero without clipping its onset. The armed tile
stays fixed while the source waveform, selection, loop, pitch, and other live
performance gestures remain available.

With Capture armed, Shift-click up to five onscreen keys to assemble a silent staged
chord; click any staged key normally to launch every voice sample-synchronously. With
no staged chord, ordinary notes start and join recording at the time they are played.
The red frame and tape-capacity bar remain visible until the destination fills. Click
**STOP** or press Space to keep a shorter take and shrink the tile to it; Escape
cancels and leaves the blank destination unchanged. A completed take becomes the
active tile and one tile-local Undo removes the whole capture while Redo restores it.
The unedited realtime performance is first archived in `Captures/` with a
`CAPTURE_YYYY-MM-DD_HHMMSS_mmm_...wav` name. This archive boundary applies only to
completed realtime input and internal CAPTURE performances—not to edits, generated
sounds, DSP/CDP renders, previews, or Undo states.

Over the waveform, the mouse wheel keeps pointer-anchored zoom and Shift+wheel scrolls
horizontally. Ctrl+wheel rotates the editable waveform through the configured coarse
number of zero-crossing candidates; Ctrl+Shift+wheel uses the fine count instead.
Alt+wheel coarsely expands or contracts the selection endpoint on the pointer's side
of its center, using the configured coarse zero-crossing count per detent.
Shift+Alt+wheel previews the selected audio by one tape-speed semitone per
detent around the visible playhead (or the selection center when it lies outside):
expansion blends into neighboring audio, contraction severs at zero crossings, and
the waveform reports the resulting pitch and time ratio. Releasing either modifier
commits the complete gesture as one Undo/post-edit; Escape restores the untouched
starting audio without history.
Set `rotate_wheel_fine` (1–20, default 5), `rotate_wheel_coarse` (20–100,
default 50), and `playhead_zero_snap` (0/1, default 1) in the `[Waveform]`
section of `tapesister.ini`.

Selection CHAIN stamping uses `chain_stamp_crossfade_ms` (0–50, default 3)
from the same section. The overlap is capped at one quarter of the selection so
short stamps still advance decisively.

**DRONE** turns the current selection into a purpose-built seamless loop. It
chooses the quietest overlap-safe crossing near the selection midpoint, rotates the
two halves, and uses a
constant-sum raised-cosine crossfade where the old selection end meets its beginning.
The dialog draws that actual temporary waveform and highlights the internal crossfade.
Hover it and use the wheel for coarse zero-snapped crossfade changes or Shift+wheel for
fine changes; either highlighted edge can also be dragged and remains zero-snapped.
Preview Loop repeats the temporary result without changing audio, history, or project
state, including these dialog-only adjustments. Copy New Tile places only the loop in
the first available independent tile,
preserves the source tuning, and marks the whole result as a zero-crossfade forward loop;
Replace Selection splices the shorter result in place, smooths the two surrounding
joins without altering the loop itself, and selects it exactly. Zero-crossfade Drone
loops retain cyclic interpolation through their outer boundary at every playback
rate. Both commit modes support Undo/Redo. The internal crossfade defaults to 50 ms, clamps to at
most one quarter of the selection, and can be changed with
`drone_crossfade_ms=50` in `[Waveform]`.

## Curated offline CDP Transform

Press `3` or cycle the lower panel to **CDP**. CDP is one top-level mode with two
internal 16-tile pages: use the visible **CDP 1 | CDP 2** toggle, or press `3` again
while already in CDP. Leaving and returning with `3` restores the last page. Page
changes never alter the active sample, selection, or viewport.

Left-click any CDP tile for the fast path: TapeSister renders its saved settings
offline and applies the accepted result to the selection, or the whole tile when no
selection exists, as one Undo step. Middle-click opens that tile in the compact
Transform workspace. Its mini waveform uses the active tile's exact persistent
selection and viewport—drag either selection edge to resize it, drag inside it to move
it, or drag elsewhere to make a new range. Adjust the recipe's one to four musical
controls, choose **Selection** or **Whole**, and press **Render**. Rendering runs off
the UI and audio threads; ordinary playback keeps using the unchanged tile while CDP
works.

A successful render becomes independent audition memory and never changes the tile
until **Apply** is pressed. Space auditions or stops that preview, Enter renders, A
applies, U saves the current macro values/Mix/seed as the tile's preset, and Escape
cancels a running job before it returns to the editor. Clicking the recipe name requests
a fresh take; seeded recipes advance an explicit stored seed while processes without a
CDP seed remain honestly nondeterministic. Parameter,
scope, selection, tile, audio, Undo, and Redo changes invalidate prior work. Apply
replaces only the selected range at the render's natural duration (or replaces the
whole tile), selects the exact result, and creates one tile-local Undo transaction.

GLISTEN is CDP 2 tile 01. It maps **Divide** to valid power-of-two spectral group counts, **Hold** to valid
analysis windows with a millisecond readout, **Shift** to CDP's documented symmetric
random semitone range, and **Scatter** deliberately drives duration randomization plus
a gentler squared group-size randomization. Actual CDP8 validation shows that PVOC
analysis/resynthesis adds padding, so GLISTEN is declared duration-changing and Mix is
disabled rather than silently truncating, stretching, or approximately aligning audio.

TapeSister does not bundle CDP. Put compatible CDP8 executables in `cdp/bin` beside
TapeSister, or set the **CDP BIN PATH** folder in the
Configuration screen (stored as `CdpBinPath` under `[Paths]` in `tapesister.ini`). A
missing executable is named for the selected recipe and leaves the tile unchanged.
The complete catalog uses a curated 17-executable subset rather than exposing the CDP
suite. See
[`docs/CDP_TRANSFORM.md`](docs/CDP_TRANSFORM.md) for the verified command pipeline,
runtime closure, safety model, platform notes, and licensing obligations.

Every ordinary slider responds to click/drag, mouse wheel while hovered, and Left/Right while the pointer is over its box; Shift makes wheel or arrow adjustments coarser. This includes Palette RGB and contrast controls. WARP, SMEAR, and TEAR remain spring-loaded gestures and deliberately keep their separate Ctrl+wheel behavior.

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

Every mouse-created or adjusted selection endpoint snaps live to the nearest zero crossing in the selected tile. A left click places the persistent edit playhead without changing the selection; a right click places it and plays from that point. Clicked playheads zero-snap by default (`playhead_zero_snap=0` opts into exact placement). Space plays the persistent selection when one exists; otherwise it toggles playback from the playhead to the tile end. On an idle tile with neither a selection nor a playhead, Space creates the playhead at frame 0 and immediately plays. Middle-clicking the waveform or pressing Escape clears the selection and returns the playhead to frame 0. Left-drag keeps ordinary selection behavior, while right-drag makes a selection and keeps the playhead at the drag-start edge: left for a left-to-right drag, right for a right-to-left drag. Tile playheads are restored with their editor state, using the same proportional position when a target tile has no saved playhead or a saved frame cannot fit. Magenta pixels mark the visible crossings directly on the waveform. The highlight always shows the actual snapped range used by Reverse, Normalize, gain, fades, Crop, and Set Loop, with an Effect-color readout at its visible upper-left reporting the complete selection duration. **SEL ALL**, `Ctrl+A`, and an unmodified double-left-click in the waveform select the exact half-open whole-tile range `[0, frames)`, including silent canvas margins. **SEL WAVE** instead selects from the first non-silent sample through one frame after the last non-silent sample, preparing the audible waveform for editing or Crop without changing the audio. If a sound has no mathematical sign crossing, ordinary dragged selection falls back deterministically to its closest-to-zero sample.

The Loop page turns the current selection into one loop, clears it, plays it continuously, selects **Forward**, **Reverse**, or **Ping-Pong** travel, and sets a 0–50 ms wrap crossfade. If no selection exists, Set Loop first selects and loops the exact whole tile. Blue boundaries and handles distinguish the loop from the purple/cyan selection; direction arrows show the active mode directly in the waveform. Either handle can be dragged live, remains zero-snapped, and automatically becomes the opposite endpoint when crossed. Computer and ordinary onscreen notes sustain the loop only while held; dragging a loop flag never releases them. Play Loop continues until Space or Escape. Loop range, mode, and crossfade belong to the tile and participate in its Undo/Redo history.

The compact workbench **LOOP** button follows the current selection, or the visible view when there is no selection. Shift-clicking it enables **LOOP LOCK**, a persistent performance transport that follows occupied, silent, and empty tiles and immediately adopts each active tile's current selection or visible view. It survives Create, Vary, transformations, selection changes, clipboard edits, canvas changes, Undo/Redo, and Capture; buffer replacements are transferred under the audio lock. Plain LOOP, Space, and incidental Stop paths cannot release it. Only Shift+LOOP releases LOOP LOCK (application exit still performs a final safety stop).

The lower panel switches between **KEYS** and **BANK**. KEYS provides the five-voice chord/drone keyboard: Shift-click toggles latched notes, while an ordinary unmodified click or computer-key note clears the chord and returns to momentary audition. Every key carries its actual note/octave label. Hover KEYS and use Shift+wheel to move its starting note one semitone at a time, or choose C0–C7 directly with F1–F8. Keyboard navigation never stops the latched chord, so another Shift-click can add a note from the new range. Sustained voices follow the selected tile.

TapeSister starts in FT2-style borderless desktop fullscreen. **Alt+Enter** toggles between fullscreen and a normal resizable window without changing the active tile, selection, clipboard, or playback state. Escape first cancels an active dialog, preview, or editing gesture; otherwise its first press resets the selection/playhead and a second press opens exit confirmation once the editor is already reset. Closing the window uses the same confirmation, with an explicit warning whenever the instrument or collection differs from the last opened or saved TSR project.

## Pitch and tuning

Every new, imported, recorded, captured, or FM-generated tile uses **middle C / MIDI 60** as its portable unity key: clicking the tile and pressing C4 play the stored waveform unchanged. The octave label may differ in another sampler, but the exported WAV `smpl` metadata always communicates the unambiguous MIDI note number. FM generation starts at the corresponding 261.63 Hz reference before its stored ratios and modulation shape the sound.

The **Tune** page contains an independent reference pitch, defaulting to C4 / MIDI 60 / 261.63 Hz. **Down**, **Up**, and the ±100-cent Trim change that target without altering audio. Clicking the Hz button latches a sine reference on its own audition layer, so WAV, tile, and keyboard playback can sound alongside it; click the Hz button again to stop it. **REF VOL** adjusts that layer from 0–100 and persists in `tapesister.ini`. The reference layer is mixed only after both capture routes and is never saved or exported. Shift-right-clicking an onscreen key sets the same reference note.

**Detect Pitch** analyzes the snapped Selection first, then the Loop, then the whole selected tile. A successful reading shows its note and confidence; **Tune to Reference** then performs a destructive tape-speed resample on that selection or whole tile. The waveform and duration change together, the result is reset to MIDI-60 unity, and the complete audio-plus-mapping change is one Undo/Redo step. Quiet, noisy, or unstable material is rejected rather than forced into a misleading pitch.

Selected-tile and Collection WAV exports write the standard `smpl` unity-note/pitch-fraction chunk at MIDI 60. WAV import preserves the audio and loop metadata while normalizing TapeSister playback to the same portable unity convention. User-captured TSP2 recipes can still carry legacy tuning data; legacy factory TSP processing recipes remain processing-only.

## FastTracker handoff and configuration

**Config** replaces only the framed waveform panel with compact blank-safe editable paths for the sample root, FastTracker executable, and FT2 exchange folder, leaving the toolbar and every control below `y=205` visible. The values persist in portable `tapesister.ini`; Tab or Up/Down changes field, the usual caret keys edit anywhere in a path, Ctrl+Backspace clears it, and **Use CWD** copies the current directory. Double-click a path field to browse: the sample and exchange fields select a folder, while the FastTracker field selects the executable file. Browser Cancel returns to the unchanged, still-unsaved Config edit. The configured sample root becomes the file browser's starting directory while normal browsing still remembers later navigation.

Fresh launches also use two boolean startup settings (both default to `1` when absent): `startup_welcome_sample` installs `assets/tapesister_welcome.wav` as the ordinary imported working sound in bank 01, and `startup_welcome_autoplay` auditions it once after the splash closes. Set autoplay to `0` to keep the waveform without the greeting, or sample to `0` to start with an empty selected bank 01. Command-line WAV/TSR loading takes precedence and is never overwritten by the welcome artifact. See `tapesister.ini.example`.

**FT2 Link** is the bidirectional handoff control. For sending, choose whether every
occupied tile on the current page should become sample slots inside **Page -> One** or
become **Page -> Split** instruments in Tapehead. When two or more Sample pages exist,
**All Pages** maps each page to one Tapehead instrument and preserves each tile number
as that instrument's sample number. TapeSister atomically publishes a new numbered
folder containing the WAVs and a versioned `exchange.tsexchange` manifest under the
exchange path (falling back to the sample path), then launches the configured
FastTracker executable without shell interpolation. No existing handoff folder is
replaced.

TapeSister also watches the exchange path for complete Tapehead-to-TapeSister manifests.
It stages the newest transfer and shows its sample count and source layout before doing
anything. **Import** validates every WAV in temporary memory and then replaces the
16-tile bank as one accepted batch; **Later** leaves both the transfer and the current
bank untouched. Imported WAVs retain their standard tuning and supported loop metadata.
The handoff remains deliberately file-based: TapeSister does not link against tracker
state. See [`docs/FT2_EXCHANGE.md`](docs/FT2_EXCHANGE.md) for the exact reciprocal
protocol and Tapehead-side requirements.

Each application refreshes a small presence marker in the exchange path. Normal sends
reuse a live Tapehead instead of opening another copy; toggle **New Instance** in the
FT2 Link dialog when a separate tracker process is intentionally wanted.

Every handoff WAV includes root/fine-tune metadata plus loop start, inclusive end, and standard Forward/Ping-Pong/Backward type. FT2 already reads the loop record, so forward and ping-pong collection members arrive with looping enabled instead of requiring manual flags. Its current WAV loader treats the standard backward type as ping-pong; exact reverse-loop interpretation and `smpl` root-note adoption belong to the reciprocal FT2-side handoff slice.

**Config -> Palette** replaces the same waveform panel with a live 14-color RGB editor: Tapehead's original 12 persisted fields plus TapeSister's independent **Wave Selection** fill and **Active Tile** outline. The editor uses TapeSister-facing names while import/export keeps the established keys. It also exposes Tapehead-compatible Desktop and Buttons contrast values: button and tile bevels derive from Controls/`Buttons` and Buttons Contrast, while Pointer/`Mouse` and Active Tile are independent. **Import TH** reads `tapehead.pal`, **Export TH** writes only Tapehead's original fields, and **Save TS** writes `tapesister.pal` including both TapeSister-only colors, which TapeSister automatically reloads on the next launch. If an imported or older palette omits Wave Selection it inherits `BlockMark`; if it omits Active Tile it inherits `Mouse`, preserving the former appearance. Older six-color Tapehead palettes still give the other additional waveform colors `PatternText`. `TAPESISTER_PALETTE` can override the TapeSister palette path.

Tapehead compatibility mapping:

| Tapehead `.pal` key | TapeSister editor label | TapeSister role |
| --- | --- | --- |
| `PatternText` | Title / Text | logo, browser text, red-channel accent |
| `BlockMark` | Active Control | active buttons and selected blocks |
| `TextOnBlock` | Active Text | text/waveform drawn over selected blocks |
| `Mouse` | Pointer | slider knobs, carets, pointer/focus accents |
| `Desktop` | Desktop | application background |
| `Buttons` | Controls | buttons, tiles, and their contrast bevels |
| `PatternNote` | Waveform | ordinary waveform and modal titles |
| `PatternInstrument` | Primary | primary labels and main sound controls |
| `PatternVolume` | Edge / Zero | edge controls, zero crossings, warnings |
| `PatternTuning` | Loop / Drift | loop markers, tuning, drift, damping |
| `PatternEffect` | Effect | effect accents and selection-duration text |
| `PatternEmpty` | Spare | reserved compatible color |
| `WaveSelection` (TapeSister only) | Wave Selection | waveform selection fill only |
| `ActiveTile` (TapeSister only) | Active Tile | active Bank tile outline only |

## Sound collection and variation

All 16 slots are peers. A newly created or imported sound fills the currently selected destination. Any empty slot can also capture useful zero-aligned material from the active tile:

- Shift-click an empty slot to capture the full active tile;
- Alt-click an empty slot to capture the active Loop, including its crossfade setting; or
- Ctrl-click an empty slot to capture the snapped Selection; or
- Ctrl+Shift-click an empty slot to Clone the active tile's complete editable state and history.

Ctrl+Alt-click an occupied slot to protect or unprotect it. Protected tiles retain full
audition, keyboard, rename, Save, and Export behavior, but reject replacement, Clear,
Clear All, non-Chain Vary, FM Apply/Create, WAV load, performance capture, and whole-bank
Tapehead import until explicitly unlocked. A bright `L` rail marks protected tiles.

Click an occupied slot to select its independent editable state and audition it; its saved selection is immediately active in the waveform. Click an empty slot to select that exact CREATE/LOAD destination without auditioning, or double-click it to activate silence for editing and paste. A slot with saved loop metadata auditions continuously in its own Forward, Reverse, or Ping-Pong mode, while one without a loop remains a one-shot. A blue mark identifies looped slots. Right-click any occupied slot to rename it, or Shift-right-click to clear it. **Clear All** requires a confirming second click and empties every unlocked peer slot while protected tiles remain intact. **+ Page** creates and switches to a deliberately empty Sample page; key `1` continues to cycle existing pages.

The **Variation** page exposes one continuous Range and the **Chain** switch. Vary requires a selected FM tile. Without a waveform selection, Chain off replaces the active tile and Chain on writes to the next empty tile, selects it, and continues from that result. With a selection, Chain off repeatedly stamps that range. Chain on instead behaves like an unrolling strip of paper: each Vary stamps the current range, crossfades its edges, moves the unchanged-width selection right by `width - overlap`, and extends the canvas with silence when the next destination reaches EOF. Each click is one tile-local Undo step containing the stamp, any extension, and the ready-next selection. A full bank blocks only the no-selection tile-chain path; selection chains stay inside their current tile.

The top **Export** button and `Ctrl+E` ask whether to export the selected tile or the complete Collection. Collection export writes every occupied slot as a numbered, loop-aware WAV into a new folder. Existing folders are never silently replaced. A failed export removes the partial files and folder.

## Audio canvas and grid

Every occupied tile is an explicit audio canvas whose sample count is independent of the visible viewport. **X2** appends exact digital silence and **/2** removes the right half at the nearest safe zero crossing. The small open handles just inside the waveform edges resize either side: outward motion adds silence, inward motion removes audio at a safe boundary, left-side changes shift selection/playhead/loop coordinates with the sound, and right-side changes keep existing coordinates fixed. The pointer is captured and returned to the handle during the drag, so it behaves like an endless relative control. Release commits one tile-local Undo step; Escape or focus loss restores the frozen pre-drag state exactly. A one-second, 44.1 kHz silent canvas remains the named default when an empty tile is double-clicked without clipboard timing.

The restrained waveform grid is anchored to the complete canvas rather than the zoomed view. **<** and **>** choose 2, 4, 8, 16, 32, or 64 divisions. The snap button cycles **OFF**, **SNAP**, and **MOVE**. SNAP quantizes both newly drawn boundaries and movement gestures; MOVE leaves selections and loop flags tightly zero-snapped while quantizing tape placement and destructive canvas movement; OFF uses zero-crossing safety without macro-grid quantization. If no crossing exists in the valid search range, the existing deterministic lowest-amplitude fallback is used. Continuous WARP/SMEAR amounts, rotation, Drone seams, zoom, and navigation are not quantized. Grid division and snap mode are stored independently with each tile but are not added to audio Undo history.

## Generative FM sound logic

Create produces a deterministic six-voice FM tile. **FM LOGIC** on the Variation page
opens seven six-control pages for Pitch, Wave, LFO Rate, LFO Depth, LFO Type, Filter,
and Structure, plus per-voice enables and mutation permissions. Ten waveforms, ten
routing structures, eight ratio families, eight interaction modes, per-voice LFOs,
bounded feedback, a transient layer, and an envelope-driven multimode filter form the
stored genome. Click or wheel a control for immediate non-destructive preview; Randomize
protects the visible page and obeys the permission switches. The Pitch page can keep
randomized ratios locked, or quantize them to a selectable root and chromatic, major,
minor, pentatonic, or whole-tone scale while C4 remains the universal unity note.
**Apply Pitches** explicitly snaps every enabled voice to that tonal set and immediately
refreshes its displayed note; disabled voices stay untouched, even with Pitch Lock on.
**Make Bank** treats the current patch as tile 01 and generates 15 Range-controlled
relatives. Chain off derives every tile from that anchor; Chain on walks forward from
each result. One confirmation can replace an unlocked page or create a new Sample page,
and the complete 16-tile action is one Undo/Redo transaction. With **Chain off**, Apply
prints the genome over the selected tile. With **Chain on**, it preserves occupied work
and fills the next empty tile. A full page asks whether to overwrite the current tile,
continue on a new Sample page, or cancel.
Inside FM Logic, `Shift+R` invokes Randomize and `Shift+B` opens Make Bank. Plain `R`
and `B` remain playable notes on the computer keyboard.

**Drone** removes the amplitude and filter attack/release envelopes, suppresses the
transient, and trims the render to exact zero-valued boundary crossings. The disabled
envelope controls gray out while the other controls remain live. **Extreme** opens
substantially wider ratios, modulation, feedback, resonance, and LFO ranges while the
DC blocker and output limiter remain active. Range is duplicated inside the workspace,
Chain is duplicated there too, so Apply routing is always visible.

QWERTY notes are genuinely five-voice polyphonic regardless of tile loop state: key-up
releases only that note, and **Hold** latches the currently sounding chord. F1-F8 and
tile tuning still apply. Every miniature waveform in FM LOGIC, Transform, and Drone
Maker carries a tiny playhead tied to the preview actually being heard. An armed
Capture-to-New-Tile can print this live performance, and REC BANK SRC SYNTH can record
its synth-only bus.

## Physical tape gestures

Start any tape gesture inside the existing snapped selection. A cyan ghost waveform follows the pointer and previews the zero-crossing-aware destination before release:

- Shift + left-drag copies and peak-matches its merge with the audio underneath;
- Shift + right-drag copies and overwrites the audio underneath;
- Ctrl + left-drag lifts/moves and uses the same peak-matched merge at the destination; and
- Ctrl + right-drag lifts/moves and overwrites at the destination.

Where source and existing audio overlap, Mix measures the source peak and underlying destination peak, sums the waveforms normally, then applies one gain to the complete merged region so its peak equals the louder original. This preserves the stronger layer's level without the thinning of a fixed average or the clipping of uncontrolled addition. Material extending beyond the existing sample keeps its original source level. Move captures the complete source before clearing it, so overlapping placement cannot corrupt itself. The lifted range remains the same duration and is filled with silence, with a roughly 1 ms protective fade at exposed edges. Every completed drag is one replayable Undo/Redo operation.

## Processing recipes and shaping

The lower panel cycles **KEYS**, **BANK**, **CDP**, and **DSP**. The unmodified top-row
number keys select them directly: `1` Sample Tiles, `2` Keyboard, `3` CDP, and `4` DSP.
CDP is one curated offline-transform mode with two internal 16-tile pages. Pressing `3`
inside CDP toggles the page; there is no fifth top-level mode or shortcut. All 32 fixed
tiles are functional, from DRUNK through ITERATE on CDP 1 and GLISTEN through GRANULATE
on CDP 2. CDP tiles use left-click quick Apply and middle-click editing, matching the
DSP interaction language while retaining the external offline engine. DSP is also one
top-level mode with two internal pages. Press `4` again, or use the visible toggle, to
move between **DSP 1 PROCESS** and **DSP 2 PRIMITIVES** without changing the active
tile, selection, or viewport.

DSP1 provides 16 curated native processors: SPACE, CAVE, ROOM, ECHO, TAPE, DUB,
COMB, RESONATE, LOW, HIGH, BAND, NOTCH, CHORUS, FLANGE, DRIVE, and CRUSH. DSP2
provides 16 offline material generators: SINE, SHAPE, PULSE, SUB, METAL, CHIME,
DRONE, BEAT, RUMBLE, HISS, DUST, KNOCK, PING, FM, AM, and CHAOS. DSP2's `SOURCE`
macro travels continuously from generated replacement through aligned source mix to
the exact dry waveform. Generated results are ordinary editable TapeSister material,
not a separate synthesizer state.

Left-click a DSP tile for the fast path: its saved settings transform the current
selection, or the whole tile when no selection exists, as one Undo step. Middle-click a
DSP tile to shape it in the shared Transform workspace. Its four musical
macros update an owned temporary preview without changing tile audio; Space auditions,
Apply commits, Back/Escape discards, and Save/Update stores the tile's macro settings.
All 32 updates persist in `tapesister.ini`; DSP1 occupies rows 01–16 and DSP2 occupies
17–32. Existing TSP processing files remain loadable and immediately applicable. See
`docs/DSP_TRANSFORM.md` for the registry, shared render/preview transaction, primitive
engine, and SOURCE alignment contract.

The compact **LOOP** audition button repeats a fixed selection when one exists, otherwise it follows the visible tile view as that view is zoomed or panned. A short boundary crossfade uses the existing audition engine. While LOOP is active it remains the audition owner, so WARP, SMEAR, and TEAR continue publishing spring-loaded previews without their usual timed playback retriggers. Ordinary LOOP remains temporary. Shift-click **LOOP** to engage LOOP LOCK across every editing and tile operation; Shift-click it again to release.

Portable `.tsp` files contain named processing settings and, for user captures, optional tuning metadata—never tile audio, crop, selection, loop, tape edits, or bank members—so the same treatment can be applied to unrelated material. Factory recipes omit tuning. While DSP is visible, Save or `Ctrl+S` writes a TSP instead of a full project. Load, drag-and-drop, and command-line opening accept TSP files, add them to the next free user slot, and apply them without replacing the selected tile. A `.tsr` project bundle saves the complete paged Sample library and the temporary REC BANK.

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

- click to place the playhead, right-click to play from it, or drag to select a range;
- mouse-wheel zoom anchored to the sample beneath the pointer;
- tile-local canvas **/2**, **X2**, captured left/right edge resizing, and full-canvas grid snapping;
- Shift+wheel panning, direct `=` or `+` / `-` keyboard zoom, arrow-key panning, and `0` Show All;
- Play All, Play Selection, and Play Displayed;
- Zoom Selection and Show All;
- nondestructive Crop with Source preservation;
- selection-aware Reverse, Normalize, 3 dB Amplify Up/Down, Fade In, and Fade Out;
- cross-tile Copy, ripple Cut, exact Paste, and duration-preserving Fit Paste;
- selection-scoped Create and Vary FM stamping;
- Undo and Redo for processing, crop, and sample-edit operations;
- two-octave computer and onscreen keyboard audition with semitone Shift+wheel movement and F1–F8 octave selection;
- mono PCM/float WAV loading, including multichannel fold-down;
- project-bundle saving with every Sample page and REC tile, tuning, editor state, selection, loop metadata, edit timelines, and DSP parameters; and
- mono 16-bit selected-tile export with sampler-compatible root/fine-tune and loop metadata.

Sample edits are deterministic and tile-owned. With no selection they affect the whole selected tile; with a selection they affect only that range.

All ordinary sample audition uses the tile's current audible tuning without rewriting
its waveform. This includes Space for the whole tile or persistent selection, playhead
and tile audition, Loop/Loop Lock, Drone and Transform previews. Changing ROOT/PITCH
updates an audition that is already playing and every subsequent audition. Keyboard
notes continue to transpose against the paired keyboard-mapping tuning.

The audio clipboard survives tile changes for the lifetime of the app, while every tile keeps and immediately restores its own selection. **Paste** replaces the destination selection with the copied sound at its exact duration: a shorter destination makes the tile grow and a longer destination makes it shrink. With no destination selection, Paste overwrites at the source selection's original time position, preserves the target duration when the material fits, and extends the tile only when necessary; a target shorter than that time position is padded with silence. **Fit** stretches or compresses the copied sound into the destination selection, keeping the tile length fixed and deliberately changing playback speed and pitch. Cut ripple-removes the selected range; cutting the whole tile is refused in favor of the explicit Clear command. All mutations participate in the destination tile's Undo/Redo history.

Amplify Up is deliberately bounded by hard clipping. Amplify Down attenuates the result without reconstructing clipped peaks, preserving that flattened distortion as a repeatable sculpting operation.

## File browser

Load, Save, and Export now open one shared FT2-informed browser rather than writing fixed filenames or requiring a typed path:

- Load lists WAV source files, `.tsr` projects, and portable `.tsp` processing recipes and preserves the existing instrument if any is invalid;
- Save lists directories and `.tsr` projects, or `.tsp` processing recipes while DSP is visible;
- Export lists directories and WAV files;
- mouse wheel, draggable scrollbar, Up/Down, Page Up/Down, Home/End, and row clicking navigate long directories;
- double-click or Enter opens a directory, WAV, TSR project, or TSP recipe;
- Save and Export remember the current directory, provide filename entry with a focus-aware blinking caret, support Left/Right/Home/End navigation plus insertion, Backspace, and Delete at the caret, and append the proper extension;
- **New Dir** temporarily turns the filename field into a validated folder-name field, creates and enters the folder, then restores the pending Save/Export filename; Escape or **Back** cancels only folder creation;
- replacing an existing file requires a deliberate second Save/Export action; and
- completed Save/Export files replace their destination atomically, so a failed write does not leave a partial result.

TSR26 stores every occupied tile as a complete independent object, including its persistent protection flag and the full FM genome, Drone/Extreme modes, and random-pitch lock/root/scale: audio canvas, tile-local three-state grid mode, private render baseline, tuning, loop, selection, playhead, viewport, persistent native-shelf selection scope, processing and edit timelines, Undo/Redo stacks, Capture-performance provenance, and the audio patches needed to replay Paste, FM stamp, tape-length, canvas-resize, performance-capture history, and pre-process Transform material checkpoints. Undo is a rolling 20-step history; the `UNDO nn/20` toolbar readout exposes its current depth, and internal edit graphs checkpoint retained states automatically instead of demanding a manual Commit at their fixed ceiling. The selected tile may also be empty, so saving never invents a fallback or gives Bank 01 special status. TSR6 through TSR25 remain loadable for compatibility; older 24-step histories retain their newest 20 states. TSP2 remains audio-independent and therefore complements rather than replaces the project format; TSP1 remains loadable as processing-only.

The chosen `.tsr` remains an ordinary first-page TSR26.
Additional pages, the REC BANK, and a tiny manifest live beside it in
`<project>.tsr.samples/`. Move, copy, or back up the `.tsr` and that companion folder
together. A legacy project with no companion folder opens as one Sample page with an
empty REC BANK. Saving uses temporary files plus replacement for each member; the
long-lived `Captures/` archive is deliberately outside this project bundle.

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
- Play selection, otherwise play from the edit playhead / Stop all: `Space`; `Escape` cancels the active gesture/dialog first, otherwise opens exit confirmation
- Select lower panel directly: top-row `1` Sample Tiles, `2` Keyboard, `3` CDP, `4` DSP; press `1` again to cycle Sample pages
- Open external REC BANK directly: `Shift+1`; plain `1` returns to Samples
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
- Send/receive through the exchange folder and launch FT2: top **FT2 Link** button

Multiple loops, automatic loop candidates, the zero-crossing loop-maker transformation, deeper synthesis and modulation stages, and multi-source variation remain separate, visually verified slices.
