# SisterTracker: stage 2 pattern playback

Press **F10** in the main Sample workspace to open SisterTracker. The first
opening creates a blank 64-row pattern; it stays stopped until Play. F10, Back
or Escape returns to Sample; tracker playback continues in the background.
Return from FM, a Mosaic event edit or an open dialog before opening Tracker.

This stage implements the eight-lane **Standard** pattern clock and Main audio
path. Each lane has one independent sample voice. Notes use TapeSister's pitch
convention: MIDI 60 is C4. They reuse the native note reader, tile tuning,
interpolation and loop modes. Two lanes can play the same tile at different
pitches and positions; their volume and lane trims add without lane-count
normalization. The Sample tile's audio, tuning and loop settings remain editable.

## First audible pattern

1. Select an occupied Sample tile, then press F10.
2. Click **USE TILE** at row 000 in lane 1. This binds that tile's stable ID to
   an alias and puts it in the cell; an existing alias is reused.
3. Click the NOTE column and enter a QWERTY note such as Z. The cursor advances
   by STEP, initially one row. Use F1–F8 or OCT to select the keyboard octave.
4. Enter later notes with an empty TILE field to inherit this lane's last
   explicit tile. You can enter two hex digits in TILE or use the alias arrows.
   To bind another tile, return with F10, select it in Sample and reopen Tracker.
5. Click Play or press Enter to restart the selected pattern from row 000.
   Space starts when stopped, otherwise pauses/resumes. Pause freezes the tick
   countdown and sample heads, fades the output and resumes without catching up.
6. Click Stop to release tracker voices only. Existing keyboard, ARP, launched
   tiles, FM, Mosaic and Tapehead retain their own transport/voice ownership.
7. Use SAVE or Ctrl+S for the existing native project save flow. Definitions,
   aliases, mute/trim, tempo, rows and editor position persist; loading a project
   stops tracker playback. Solo is temporary and clears on project load.

## Editing and mixing

| Control | Action |
| --- | --- |
| Click grid / arrows / Tab | Select a cell and its NOTE, TILE or VOL field; Tab moves through fields |
| QWERTY in NOTE | Silent note entry, using the normal keyboard pitch layout |
| Two hex digits in TILE | Assign an existing 01–FF alias; 00 clears to inheritance; an unbound alias is rejected |
| Two hex digits in VOL | Set inherited event volume 00–40; larger inputs clamp to 40 |
| Delete / Backspace | Clear the selected field |
| Shift+Delete / Shift+Backspace / CLEAR | Clear the complete cell, including stored tune/FX data |
| OFF | Release this lane's voice through a short fade; manual Sustain/HOLD does not apply |
| CUT | Immediately cut this lane; explicitly sharp and may click |
| STEP | Advance after entry by 0–16 rows; right click decreases |
| Wheel / Page Up / Page Down / Home / End | Scroll or navigate rows |
| Pattern arrows / NEW | Select or create a pattern; selection alone does not switch the heard pattern |
| BPM / TPL / ROWS | Left click increases, right click decreases; Shift changes ROWS by 16 |
| LOOP | Wrap this pattern or stop after its final row duration |
| M / S | Lane mute / temporary lane solo; clocks and heads continue |
| Lane trim value | Left/right click changes independent trim by 0.1, from 0 to 2 |
| FOLLOW | Keep the heard row visible when it belongs to the selected editor pattern |

An explicit TILE cell changes the lane's default without starting a note. An
empty tile inherits; an unresolved explicit ID stays missing and never falls
back to a slot or another source. A VOL-only cell updates the sounding voice
without restarting its head. Editing definitions publishes future rows; editing
the currently heard row does not execute it again. Play resets tile inheritance
and event volume to 40; a pattern loop retains inherited state.

Mute, trim, OFF, ordinary Stop, pause and source handoffs are smoothed. Retrigger
uses the normal 2 ms native attack and a short residual handoff. Native source
edits replace immutable audio generations and retain normalized playback phase;
missing/deleted tiles release only the affected tracker lanes. Moving a tile
keeps its stable reference. No tile lookup, allocation, free, file I/O or pitch
exponentiation occurs in the per-frame tracker reader.

## Sister and recording

Tracker Main goes through the existing ordinary program/FX/EQ/limiter/output
path. When Sister Machine is powered, its **TRACK** source switch selects the
complete tracker Main sum. It follows the existing Sister source-switch ramps
and normalization across selected source categories. An unselected tracker Main
is silent under Sister's existing insert ownership rule; it cannot leak around
Sister. TRACK is also available to native MIDI learn as `sister.source.tracker`.

**FILE OUT** and Mosaic **OUTPUT** recording include Tracker Main because they
tap the final audible program. Dry keyboard/Synth taps retain their existing
source-specific meaning. Audio Health (F12) reports tracker state, row and voice
count separately, and includes Tracker in Sister's source-switch list.

## Scope and checks

This is pattern playback, not Song/order playback yet. The grid edits NOTE,
TILE and VOL. LEN/CONTROL, private Fast Tracks, ratios/directions, overlap,
M/N and FX execution, direct output lanes, MIDI recording, clipboard and tracker
Undo/Redo remain later stages. Their existing saved definitions are preserved;
this runtime uses Standard forward timing, one voice per lane and Main routing.

The core test traces exact onsets at integer and fractional tick durations,
pause/rate changes, loop/end behavior, inheritance, OFF/CUT, two lanes sharing
one reader generation, mute/trim/solo, source edits, move/delete and reclamation.
The SDL controller test covers note/hex entry, workspace focus, independent Stop,
Sister TRACK ownership, heard/editor pattern separation and exact FILE OUT frames.
Both tests execute their setup and checks in Release builds.
