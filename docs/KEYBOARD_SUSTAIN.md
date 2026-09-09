# Keyboard Sustain

**Shift+S** toggles one Sustain setting for QWERTY, onscreen keyboard notes, and
MIDI. Click **SUSTAIN ON/OFF** above the main keyboard, in file preview, or in
Sister Machine. Portal uses **SUS** beside Auto Preview. The active button color
shows that Sustain is enabled. FM uses the main keyboard's control.

![Main keyboard with Sustain enabled](images/keyboard-sustain.png)

Sustain starts **off** each session. It does not change the sample, loop points,
saved recipe, or tile. Text fields and destination/preset dialogs retain their
normal input; Shift+S does not interrupt typing.

| Sustain | Loop | After releasing a key |
|---|---|---|
| Off | Off | Stop the note |
| Off | On | Stop the looped note |
| On | Off | Let the sample finish |
| On | On | Keep repeating the loop |

Turning Sustain **off** releases notes whose keys are already up and keeps keys
still held sounding. A new press of the same key retriggers it normally. Sustain
does not add voices: QWERTY and FM/Portal/import preview retain the existing
five-note limit. MIDI tile playback retains its larger independent voice pool.

The earlier canvas behavior implicitly let non-looping samples finish while
releasing looped notes at key-up. The switch makes the release choice explicit.

**FM:** physically held keys repeat the rendered preview. With Sustain on,
releasing an ordinary QWERTY, onscreen or MIDI key lets the current pass finish
once; it does not latch an endless loop. With Sustain off, key-up stops it.
**HOLD / HELD** and explicit Shift-click latches keep repeating until released
with their own controls. HOLD can also catch a note that is still finishing.
Changing FM parameters preserves whether a note is held or finishing; a fresh
key press retriggers normally.

Explicit Shift-click latches, FM HOLD, staged capture chords, and plain-clicked tile
launches retain their own controls. Space/Stop and MIDI panic still clear their
respective voices. Closing or replacing preview audio detaches its notes, including
sustained notes. Stop does not turn the Sustain setting off.

## MIDI in previews

CDP Portal now receives MIDI notes for the selected **Source** or **Result**,
including its audition selection and Loop setting. C4/MIDI 60 plays at original
pitch. Source/result and loop-range changes use the existing preview voice path.

File preview also accepts MIDI, preserving stereo. MIDI velocity and channel
identity are retained; channel panic stops that channel even with Sustain on.
Preview note-ons are blocked during text/destination editing; note-offs remain
routed and obey Sustain. File-list navigation and text entry remain separate.

## Tile loop preservation

**Set Loop → click the current tile** now stores the live editor before launching
it. The previous click path reloaded the older stored tile and could discard loop
points, selection metadata, and other unstored editor changes. Clicking the current
tile now preserves those edits and their Undo history; playback uses the updated
loop range. Switching away and back also retains it.

## SCRUB direction correction

Factory SCRUB's direction names were reversed. **0 is BOUNCE; 1 is FORWARD**.
Only the labels change: existing recipes keep their numeric values and sound.
Forward passes CDP's `-f` flag for one sweep and ignores the requested length.

## Verification

`make tapesister_keyboard_sustain_tests` builds the optional SDL controller harness.
Run `./tapesister_keyboard_sustain_tests` with SDL's dummy audio/video drivers
(the harness selects them). It checks:

- QWERTY and MIDI release behavior for tile, FM and preview voices, with and
  without loops; released versus physically held notes when Sustain is disabled.
- Repeated notes, explicit latches, natural one-shot completion, and panic.
- Real FM preview audio finishing after QWERTY/MIDI key release with Sustain,
  while physical keys and explicit HOLD/latches keep looping; rerender and retrigger.
- Main/FM MIDI routing and Portal Source/Result, selected ranges, pitch, channel
  panic, text focus, and stereo import-preview output.
- Shift+S auto-repeat protection, visible button click targets, and Sister's
  shortcut/preset-dialog behavior.
- Set Loop, clicking/launching the same tile, switching away/back, Clear Loop,
  and Undo/Redo without changing the audio hash.
- SCRUB's displayed values against the actual generated `-f` flag.

The accompanying Portal controller, live-loop, stereo voice, group/Sister,
canvas output-recording, bank, and core tests cover the surrounding workflows.
Windows compilation and physical QWERTY/MIDI listening remain user checks.
