# Sister Machine PR5 performance window

PR8 retains the 640x400 logical size while shortening the waveform viewport enough to
add one compact `SOAK / BLEED / H1 / H2 / H3 / MIX` row above status and Capture. The
two continuous fields use the existing guarded wheel and drag language. The four target
boxes are binary, exclusive between the head group and MIX, and reuse current palette
roles. No new focus mode or keyboard interception is introduced.

PR5 exposes the PR3/PR4 Sister engine without moving audio ownership into UI code.
The main `AudioState` still owns `TsSisterRuntime`; the second SDL window owns only a
`TsSisterUiModel`, renderer, texture and copied snapshots. Closing the window or pressing
Escape hides it. Neither action changes POWER, ROLL, HOLD, MONITOR, feedback, Capture or
the rolling memory. POWER is the explicit engine allocation/free boundary.

## Launcher and window lifecycle

The `TAPESISTER` wordmark has a logical hit target of `x=0..159`, `y=0..31`. Clicking it
creates/restores the independent 640 by 400 Sister window. Since PR6 it never changes
POWER: visibility is UI state and POWER is audio-engine state. The ordinary application
remains usable if window or engine allocation fails.
The main logo shows engine state with both color and shape: a live underline/run marker,
a two-bar Hold marker, and a warning color. The engine remains disabled at startup.

SDL events are dispatched by window ID before main-window hit testing. Sister QWERTY
note events reuse the same `TsNoteEvent` path as the main window, so MIDI and QWERTY
fan-out remain identical. Modal text/edit state suppresses Sister-window QWERTY input.
Application quit stops callbacks before destroying either window or freeing Sister.

## Rolling display contract

`TsSisterWavePublisher` receives the exact bounded stereo frame accepted by rolling
memory and its actual circular write position. It incrementally reduces that stream into
256 fixed L/R min/max bins. Publishing uses atomic values and an odd/even monotonic
revision; the UI copies a coherent snapshot and never scans or reads the live circular
buffer. Hold and ROLL-off do not publish imaginary writes. Clear and device restart reset
the overview explicitly.

Write and H1/H2/H3 positions come from the engine snapshot and are drawn as distinct
shapes as well as colors. Buffer duration changes only the time represented by each
normalized bin, not UI cost.

## Waveform display modes and palette

Both ordinary sample waveform and Sister overview use the reusable
`TsWaveformDisplayMode` contract:

- `STEREO`: independent L/R extrema; Sister uses two labeled lanes.
- `LEFT`: channel one only.
- `RIGHT`: channel two only.
- `MONO SUM`: display-only `0.5 * (L + R)`.

Mono material has a safe dual-mono visual fallback. These modes never rewrite samples,
alter selections, change Capture format or change frame indexing. The palette adds
`StereoWaveLeft`, `StereoWaveRight` and `StereoWaveSum`. Missing keys inherit the
legacy `PatternNote`, `PatternEffect` and `PatternInstrument` colors.

## Controls and fixed routing

PR9 adds a `FX PAGE`/`TAPE` page switch in the same 640×400 non-modal window.
The FX page presents three readable panels—Reverb TYPE/MIX/DECAY, Delay
TIME/FEEDBACK/MIX, Distortion DRIVE/TONE/MIX—each with H1/H2/H3/MIX toggles,
plus one wide FX FEEDBACK control. It deliberately exposes no ordering, per-head
amount, modulation matrix, or hidden advanced page. FX controls remain available
with POWER off; transport-only actions retain their POWER contract. The existing
high-DPI mapping and wheel target guard apply to every new field.

The window exposes POWER, ROLL, HOLD, CLEAR and MONITOR; TILES/FM/EXT/PREVIEW source
switches; H1/H2/H3 level/time/scrub/span/rate/feedback parameters; Wow, Drop, Duck,
decorrelation, width and filter; and MIX/H1/H2/H3 Capture selection. The router remains
the fixed PR4 graph. There is no generic MIX-to-input route and no UI-owned DSP copy.
H2 and H3 use the engine's exact ordered Rate choices:
`-2, -4/3, -1, -2/3, -1/2, 1/2, 2/3, 1, 4/3, 2`.
Continuous controls accept absolute left/right click, either-button drag, and wheel
adjustment (Shift+wheel is fine). Both mouse buttons address the same visible parameter.

The dedicated monitor/memory row separates six independent laws:

- `INPUT` is a smoothed 0-200 percent pre-tape trim. It scales the compensated selected
  source before the rolling-memory write and Duck detector, so a hot tile/FM/input bus
  can be controlled before saturation.
- `DRY` scales Sister's post-INPUT source return. The selected TILES, FM, EXT or PREVIEW
  bus is removed completely from its ordinary TapeSister speaker path and returns only
  here, like a physical insert. Sources not selected in Sister keep ordinary monitoring.
- `WET` scales the Sister MIX return when MONITOR is enabled.
- `OUT` is a post-filter 0-400 percent MIX stage before linked final safety. It affects
  the audible Sister return and MIX Capture, but not the individual H1/H2/H3 taps.
- `ERASE` sets how much of the previous rolling-memory cell the moving write head removes.
- `GHOST`/`GHOST TONE` spectrally ages only the retained old cell on repeated passes.

INPUT, DRY and WET are smoothed over 20 ms. H1/H2/H3/MIX Capture taps,
ordinary Capture source and external recording remain pre-monitor. OUT defaults to
400 percent in the application to compensate the
engine's deliberately conservative H1 and fixed-headroom defaults; final linked safety
still bounds the result. MONITOR gates the complete Sister DRY+WET return. With MONITOR
off—or DRY and WET both at zero—routed sources are silent at the speakers while Sister
continues rolling and Capture remains available.

Decor is deliberately an ON/OFF character switch. Filter is a discrete named type selector;
cutoff, Q and filter gain remain continuous. One wheel detent changes one Decor/filter/rate
choice, while continuous controls retain normal and Shift-fine wheel adjustment.

Current TapeSister transforms are destructive or preview-domain operations rather than a
live insert rack. Their rendered tile/preview audio reaches Sister exactly as heard. Future
live source effects belong before the Sister input split when they are meant to print to
tape; Sister's own character/filter remains inside the machine; future master-bus effects
belong after the DRY/WET sum. This preserves an explicit pre-tape/post-tape distinction.

ERASE 100 is the original full-overwrite law. Lower values retain old material at the
write position before adding new input and H1/H2 feedback: `retained = old * (1-erase)`.
For example, ERASE 20 retains 80 percent of the previous cell on each complete pass.
The retained stereo pair uses one linked amount, then enters the existing DC blocker,
soft saturation and bounded-write safety. ERASE is distinct from H1/H2 feedback and from
output Duck. A later COSMOS-inspired suppressor may make erase input-sensitive, but this
version deliberately keeps ERASE predictable and independent of instantaneous loudness.

Hold has precedence over Roll for writes. Monitor affects only the complete Sister return.
Capture taps, feedback and head motion remain active with the window hidden or Monitor
off. CLEAR uses the PR3 fade/wait/off-thread-clear/fade-in transaction.

## Persistent source selection

The main sample bank has no source-selection mode. Shift-clicking an occupied tile
toggles the runtime's per-page 16-bit Sister mask without changing the active canvas,
auditioning, or touching Capture's transient mask. Occupied protected/locked tiles may
be sources. Shift-clicking an empty tile retains the established Copy operation and the
copy is not marked automatically. Page switching selects that page's mask. PR6 project
state saves every page mask while named sonic presets deliberately omit masks. The
Sister-window TILES button remains the non-destructive insert/bypass for that mask.

## Capture and Overdub

The performance window selects MIX/H1/H2/H3, M/S, and CURRENT/NEXT EMPTY before arming.
Allocation occurs on the UI thread while the audio device is locked, then callback writes
reuse the PR4 recorder. Completion commits through the existing protected transaction,
including linked stereo peak protection, channel-shape identity, provenance, canvas
growth and undo/redo. Overdub follows the base tile's channel shape. A Sister source tile
cannot also be a target; recursion remains capture-to-another-tile followed by an explicit
source-mask action.

## Preferences and restart policy

Configuration now preserves buffer duration/channel shape, clear fade, Sister Capture
format, both waveform display modes, restart-clear policy, INPUT trim, DRY/WET monitor levels,
MIX output gain, ERASE strength and Sister window position.
Musical source masks and parameter state are intentionally runtime-only. Output restart
retains the PR4 policy: rolling memory and published waveform are cleared; failure leaves
Sister disabled while ordinary TapeSister audio remains available.
If the output contract is unavailable or not stereo, POWER reports that state and keeps
the Sister engine off; it does not compromise ordinary output setup.

## Rendering performance

The Sister renderer compares its copied model with the last presented model and skips
unchanged frames. Active motion is capped at one software-texture update every 33 ms
(about 30 Hz). Circular-memory reduction is incremental in the callback and the UI's
fixed 256-bin copy has constant cost regardless of a 1- or 120-second buffer. No render
operation or framebuffer allocation occurs on the audio thread.

## Retained wrappers

The device/config compatibility wrappers documented in
`SISTER_MACHINE_AUDIO_BUSES.md` remain. They normalize SDL device switching,
configuration fields and UI config helpers only. No wrapper intercepts Sister DSP,
notes, performance, Capture, routing, the callback, or either window renderer.

## Manual Windows/Linux validation

1. Start TapeSister and confirm no Sister window appears and ordinary mono/stereo audio
   is unchanged.
2. Click the `TAPESISTER` logo; confirm the second window opens without changing POWER.
3. Close and reopen it while ROLL runs; confirm memory/head motion continues.
4. Verify STEREO/LEFT/RIGHT/MONO SUM in both waveform displays with a hard-panned file.
5. Arm source tiles on two pages and confirm each page restores its own mask.
6. Trigger the mask from QWERTY in both windows and from MIDI; verify one-shots finish
   after key-up, looped notes stop, polyphony works and panic remains immediate. Leave
   FM open, focus Sister and confirm its keyboard still plays/holds the FM synth.
7. Exercise TILES/FM/EXT/PREVIEW independently and in combinations.
8. Confirm HOLD prevents writes while heads move; MONITOR off does not stop Capture.
   Route each source individually and confirm its ordinary audition/monitor bus becomes
   silent. Set MONITOR off, or DRY and WET to zero, and confirm silence without stopping
   rolling or Capture. Confirm unrouted sources retain ordinary monitoring. Lower INPUT
   on a hot internal tile/FM source and confirm the write/input overload is reduced.
   Raise OUT and verify MIX return/Capture rise while H1/H2/H3 taps remain unchanged.
9. Compare ERASE 100 with ERASE 20 over multiple buffer passes. Full erase should replace
   old material; ERASE 20 should leave progressively aging stereo ghosts beneath new input.
10. Capture each tap as M and S, then test mono/stereo Overdub and undo/redo.
11. Capture to another tile, add it explicitly to the source mask and replay it as a
    second generation.
12. Switch audio/input devices and sample rates; confirm safe clear/restart and no stuck
    notes or stale input.
13. Maximize and freely resize the Sister window. Visible controls must retain exact mouse
    targets, and clicks in any letterbox margin must do nothing. Restore Sister to a smaller
    window and confirm that it stays above TapeSister while visible tiles in the main window
    remain independently clickable.
14. Confirm POWER off leaves ordinary TapeSister playback and recording functional.

No hardware validation is claimed by this document.

The intended performance loop is:

```text
Open Sister
-> arm source tiles
-> play by QWERTY/MIDI
-> manipulate three heads
-> select a tap
-> capture into a new tile
-> add that tile as a source
-> repeat
```

PR6 adds named Sister preset banks and project-level musical-state persistence without
serializing live circular-buffer audio. Unrestricted routing, generic MIX
recirculation, same-tile live feedback, linked-channel CDP, TapeHead changes and
master-bus effects remain outside this window contract.
