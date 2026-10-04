# SisterTracker: embedded TapeHead

F10 opens TapeHead's editor and replayer inside the TapeSister window. This is
compiled TapeHead application code, including its keyboard/mouse dispatch,
editing, clipboard, undo, interpolation, FastTracks and transport. The earlier
native recreation remains a legacy regression fixture; F10 uses the embedded
application.

TapeSister owns the window, audio device, MIDI connection, tiles, router and
project files. TapeHead does not open another process, window or audio device.
Its upstream repository is unchanged. The imported source is pinned to
`Ehule/FT2-Tapehead-Edition` commit
`f053d96df3996a5fd3b26625f069888c3a7d24ab`.

## First use

1. Select an occupied tile on the canvas, then press F10.
2. Enter notes with the original tracker keyboard. The selected tile is assigned
   a stable instrument alias; the TILE selector lists its name.
3. Use **Play ptn.**, **Play sng.**, **Rec. ptn.** or **Rec. sng.**. Space stops
   playback and switches between idle and editing, following TapeHead.
4. F10, Escape or Canvas returns to the canvas. Playback continues when
   the workspace is hidden. F10 returns to the same score and transport.
5. F9 opens the existing Router. Enable TRACK as a Sister source to feed the
   tracker into the routed processing chain. F12 opens Audio Health.
6. Ctrl+S / Ctrl+O use TapeSister's project save/open flow.

**FX**, **Prism**, **Sister** and **Fallout** open the corresponding existing
Sister Machine pages. **Canvas** returns to the tile canvas. These buttons
release held manual notes while tracker transport continues; Escape in the
Sister window returns to the tracker.

The former Disk Op button is **Tiles**, which returns to the canvas for tile
selection. Configuration opens the host's **Audio** panel and Trim opens
**Router**. The redundant Zap shortcut is hidden. Independent sample editing
and file operations are unavailable.
The sample-list area displays host status and capture state.

## Tiles and audio

Aliases 01–80 (hex; 128 tiles) refer to persistent TapeSister tile IDs, across
Sample pages. Moving a tile does not change its score references. Deleting a
tile leaves a missing reference; reusing its old slot does not retarget notes.
Selecting another tile on the canvas chooses its alias on return. Choosing an
alias inside the tracker stays selected until the canvas selection changes.

The adapter prepares immutable float snapshots on the control thread. Stereo
channels, tile tuning, all six loop modes and loop crossfade are preserved by
TapeSister's float reader. Sounding voices retain their previous snapshot while
the tile is edited; the next note uses the new generation. Reference 16-bit mono
data is used for TapeHead's scopes and sample metadata, not audible playback.

TapeHead's replayer executes note timing, song orders, volume/effect columns,
M/N tuning, FastTracks and its voice envelopes/panning/ramps. Its mixed stereo
output enters the existing **TRACK** bus. Sister processing, router order,
Master EQ, limiter, OUT gain and final-output recording remain host operations.
This version supplies one stereo tracker bus; per-lane hardware outputs are not
part of this transplant.

## Controls

The tracker uses TapeHead's original controls. In particular, its clipboard
bindings differ from the earlier native editor's Ctrl+C/X/V shortcuts.

| Control | Action |
| --- | --- |
| F10 / Escape / Canvas | Return to canvas; transport continues |
| Play ptn. / Right Alt | Play the current pattern |
| Play sng. / Right Ctrl | Play the song orders |
| Rec. ptn. / Right Shift | Record into pattern playback |
| Space / Stop | Stop; Space also switches idle/edit mode |
| Arrows / Tab | Move the original field/lane cursor |
| Alt+Arrows / mouse drag | Mark a block |
| Shift/Ctrl/Alt + F3/F4/F5 | Cut/copy/paste track, pattern, or block |
| Ctrl+Z / Ctrl+Y | Original undo/redo |
| F8 while stopped | Extract the marked block into a pattern |
| Ctrl+L | Start/stop literal block looping |
| Shift+Arrows during block looping | Resize at the next loop seam |
| F7 during block looping | Arm capture at the next seam; press again to end at a seam |
| F8 during block looping | Capture one live cycle of final audible output |
| F7 outside block looping | Start/stop final-output file recording |
| Shift/Ctrl/Alt + F1/F2 | Transpose track/pattern/block down/up |
| Shift/Ctrl/Alt + F7/F8 | Transpose only the current tile in that scope |
| Ctrl+Shift+V/B/T/M | Original volume/FX/tuning/note interpolation previews |
| Ctrl+Grave | Silent record entry |
| Grave / Shift+Grave | Increase/decrease STEP, wrapping 0–16 |
| STEP arrows / STEP wheel | Original edit step |
| OCT click/right click/wheel | Increase/decrease octave, clamped 0–7 |
| Backspace | Clear the current full cell, then move up; clamp at row zero |
| Shift+Backspace | Original structural row deletion |
| Ctrl+Alt+Backspace | Pattern-only view |
| Alt+Backspace / Extend | Original extended view |
| F9 / F12 | Host Router / Audio Health |

F10 reserves the host workspace toggle, so the original plain F10 row bookmark
is unavailable. Modified F10 bindings remain with TapeHead. Plain F7 belongs to
host capture; use OCT for octave 6/7. Ctrl+C opens host Audio through TapeHead's
original configuration command. Ctrl+D selects Tiles through Disk Op; Ctrl+E
returns to Canvas through the original extended sample editor command.

Backspace's clear-before-move order and the additional Ctrl+Alt+Backspace alias
are deliberate local adaptations. The original FT2 default clears after moving.

MIDI notes enter the original `recordNote` path while SisterTracker has focus.
Velocity becomes the original volume column. MIDI release, panic, window focus
loss and workspace switching release owned manual notes without stopping the
tracker transport or swallowing releases belonging to the canvas. Host dialogs,
Router, Audio Health and MIDI Learn retain input ownership.

## LEN and FastTracks

LEN and CONTROL are **song-wide**, following the pinned TapeHead source.
Standard, Pattern and Song modes, all 17 ratios, master toggle, direction,
selection, clutches, sync, randomization, Jog/Punch and Z-command behavior use
the original implementation. The logo toggles the FastTracks master; its
Ctrl/Ctrl+Shift actions remain available. This supersedes the previous native
implementation's pending Song mode and custom lane ping-pong traversal.

Literal Ctrl+L block transport ignores LEN/private clocks and auditions only
its marked rows and lanes. F7/F8 capture the live final host output at exact
loop seams, rather than running TapeHead's separate offline WAV renderer.

## Saving and compatibility

The host project owns both the stable pattern/tile model and a versioned score
extension. The extension retains original note/instrument/volume/effect/tuning
bytes, hidden rows, orders, song name, global volume, FastTracks state, mute/trim,
editor position, octave/STEP, color mode and expanded-view choice. It contains
no pointers, copied tile audio, active voices or running transport. Loading a
project starts stopped and clears the original clipboard and undo history.

Legacy version-1 scores still load through the migration adapter. TapeHead's
native note range is C-0–B-7 (MIDI 12–107), its tempo range is 32–255 BPM, and its
instrument limit is 128. A legacy score outside those limits is rejected with
an explicit message before replacing the embedded score; it is not silently
clamped or discarded. Do not open a version-2 score in an older TapeSister build.

Tile audio continues to use the existing host project sample format. The score
extension does not change that format or make project audio storage lossless.
Buffer tiles/external-input tile bindings are a later extension of the host
boundary and are not implemented here.

See [source boundary and validation](SISTERTRACKER_PARITY.md) and
[reproducible import](../third_party/tapehead/application/README.md).
