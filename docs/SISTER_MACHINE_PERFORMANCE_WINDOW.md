# Sister Machine PR5 performance window

PR5 exposes the PR3/PR4 Sister engine without moving audio ownership into UI code.
The main `AudioState` still owns `TsSisterRuntime`; the second SDL window owns only a
`TsSisterUiModel`, renderer, texture and copied snapshots. Closing the window or pressing
Escape hides it. Neither action changes POWER, ROLL, HOLD, MONITOR, feedback, Capture or
the rolling memory. POWER is the explicit engine allocation/free boundary.

## Launcher and window lifecycle

The `TAPESISTER` wordmark has a logical hit target of `x=0..159`, `y=0..31`. Clicking it
creates/restores the independent 640 by 360 Sister window and enables the engine when it
is off. The ordinary application remains usable if window or engine allocation fails.
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

The window exposes POWER, ROLL, HOLD, CLEAR and MONITOR; TILES/FM/EXT/PREVIEW source
switches; H1/H2/H3 level/time/scrub/span/rate/feedback parameters; Wow, Drop, Duck,
decorrelation, width and filter; and MIX/H1/H2/H3 Capture selection. The router remains
the fixed PR4 graph. There is no generic MIX-to-input route and no UI-owned DSP copy.
H2 and H3 use the engine's exact ordered Rate choices:
`-2, -4/3, -1, -2/3, -1/2, 1/2, 2/3, 1, 4/3, 2`.
Continuous controls accept absolute left/right click, either-button drag, and wheel
adjustment (Shift+wheel is fine). Both mouse buttons address the same visible parameter.

Hold has precedence over Roll for writes. Monitor affects only the processed return.
Capture taps, feedback and head motion remain active with the window hidden or Monitor
off. CLEAR uses the PR3 fade/wait/off-thread-clear/fade-in transaction.

## Persistent source selection

The main sample bank has a deliberate `SISTER SRC` mode. While active, tile clicks edit
the runtime's per-page 16-bit Sister mask and do not edit Capture's transient mask or the
focused tile. Occupied protected/locked tiles may be sources. Empty tiles are rejected.
Page switching selects that page's mask; project close clears all masks. Masks remain
runtime-only in PR5 and are not saved in projects or Sister presets.

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
format, both waveform display modes, restart-clear policy and Sister window position.
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
2. Click the `TAPESISTER` logo; confirm the second window opens and POWER is on.
3. Close and reopen it while ROLL runs; confirm memory/head motion continues.
4. Verify STEREO/LEFT/RIGHT/MONO SUM in both waveform displays with a hard-panned file.
5. Arm source tiles on two pages and confirm each page restores its own mask.
6. Trigger the mask from QWERTY in both windows and from MIDI; verify note-off and panic.
7. Exercise TILES/FM/EXT/PREVIEW independently and in combinations.
8. Confirm HOLD prevents writes while heads move; MONITOR off does not stop Capture.
9. Capture each tap as M and S, then test mono/stereo Overdub and undo/redo.
10. Capture to another tile, add it explicitly to the source mask and replay it as a
    second generation.
11. Switch audio/input devices and sample rates; confirm safe clear/restart and no stuck
    notes or stale input.
12. Confirm POWER off leaves ordinary TapeSister playback and recording functional.

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

PR6 may add named Sister preset banks and project-level musical-state persistence. It
must not serialize the live circular-buffer audio. Unrestricted routing, generic MIX
recirculation, same-tile live feedback, linked-channel CDP, TapeHead changes and
master-bus effects remain outside this window contract.
