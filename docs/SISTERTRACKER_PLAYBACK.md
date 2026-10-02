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
   an alias and puts it in the cell; an existing alias is reused. New aliases
   prefer the Sample slot number when that alias is free.
3. Click the NOTE column and enter a QWERTY note such as Z. The cursor advances
   by STEP, initially one row. Use F1–F8 or OCT to select the keyboard octave.
4. Enter later notes with an empty TILE field to inherit this lane's last
   explicit tile. You can enter two hex digits in TILE or use the alias arrows.
   An unbound 01–10 can also bind an occupied Sample tile on the current page
   directly. To choose a different page, return with F10, select it in Sample
   and reopen Tracker. Red Sample selection alone never rewrites pattern cells.
5. Click Play or press Enter to restart the selected pattern from row 000.
   Space starts when stopped, otherwise pauses/resumes. Pause freezes the tick
   countdown and sample heads, fades the output and resumes without catching up.
6. Click Stop to release tracker voices only. Existing keyboard, ARP, launched
   tiles, FM, Mosaic and Tapehead retain their own transport/voice ownership.
7. Use SAVE or Ctrl+S for the existing native project save flow. Definitions,
   aliases, mute/trim, tempo, rows and editor position persist; loading a project
   stops tracker playback. Solo is temporary and clears on project load.

## Editing and mixing

Each lane has Tapehead's separate two-row header: **LEN OFF** or **LEN1–256**
on the top strip, and its saved **FT ratio** on the second strip. The ratio
bank follows Tapehead's 17 entries, from 1:2 through neutral 1:1 to 5:1.
The suffix is `-` for Standard, `P` for Pattern, or `S` for Song; the eject symbol
in the LEN strip identifies a saved CONTROL lane. Names and mute/solo/trim sit underneath.
These are read-only saved-setup displays in this stage. The footer explicitly
marks LEN/FT readouts inactive: playback still uses one Standard clock and the
physical pattern row count. Private LEN/Fast Tracks clocks remain the next
playback stage. Clicking a header selects its lane and explains this limitation.

The pattern view reuses Tapehead's original normal, tiny and pattern pixel fonts,
recessed black lane panels, hex row numbers on both sides, current-row band,
colored field groups and CONTROL symbol. It honors the shared palette, including
PatternEmpty for empty dots. Sample selection, Sister routing outlines, locked
tile dimming and performance source borders are suppressed in the final audio UI
overlay while Tracker is visible. They continue to render in the Sample workspace.

All five stored groups are visible: NOTE, TILE, VOL, M/N and FX. Tuning and FX
have separate command and two-digit parameter cursor positions. These definitions
save, copy, paste and undo, but their commands do not yet execute in audio. The
footer labels command audio inactive alongside LEN and Fast Tracks.

| Control | Action |
| --- | --- |
| Click grid / arrows | Select NOTE, TILE, VOL, tuning command/value, or FX command/value; arrows wrap through all seven positions across eight lanes |
| Shift+arrows / Shift+click / left drag | Select a rectangular block of complete cells |
| Tab / visible FX button | Open Sister/FX; Tab there returns to the main workspace |
| F9 / F10 / F12 | Router / Tracker–Sample / Audio Health |
| QWERTY in NOTE | Silent note entry, using the normal keyboard pitch layout |
| Two hex digits in TILE | Assign a stable 01–FF alias; 00 clears to inheritance; unbound 01–10 may bind the corresponding occupied current-page Sample slot |
| Numpad digits / Enter after one digit | Enter hex digits / commit a single digit as 01–0F |
| Two hex digits in VOL | Set inherited event volume 00–40; larger inputs clamp to 40 |
| M or N in the tuning command | Store root or fine tuning and move to its parameter; M accepts 00–7F, N accepts 00–FF |
| 0–9 / A–Z in the FX command | Store the command and move to its 00–FF parameter; explicit 000 and Z00 are preserved |
| Delete / Shift+Delete / CLEAR | Clear the selected field / complete cell; a selected block uses the field mask, or all fields with Shift |
| Insert / Backspace | Insert / remove a row in the current lane, keeping the pattern length |
| Shift+Insert / Shift+Backspace | Insert / remove a row across all eight lanes |
| OFF / CUT | Fade this lane's voice / immediately cut this lane; manual Sustain/HOLD does not apply |
| STEP / OCT | Advance after entry by 0–16 rows / keyboard octave; right click decreases |
| Wheel over grid | One row per SDL wheel detent, respecting reversed scrolling; manual scrolling suspends FOLLOW until re-enabled or Play |
| Wheel over BPM / TPL / ROWS / lane trim / OUT | Adjust the hovered visible control; fractional wheel deltas apply to continuous trim/output controls |
| Page Up / Page Down / Home / End | Navigate rows; manual navigation suspends FOLLOW |
| Pattern arrows / NEW / CLONE PAT in EDIT | Select / create / duplicate a pattern; selection alone does not switch the heard pattern |
| BPM / TPL / ROWS | Hold left mouse button to increase, right to decrease; Shift changes ROWS by 16 |
| LOOP | Wrap this pattern or stop after its final row duration |
| M / S / lane trim | Mute / temporary solo / independent trim from 0 to 2; clocks and heads continue |
| FOLLOW | Re-enable keeping the heard row visible when it belongs to the selected editor pattern |
| EDIT | Visible editing tools and clipboard field masks |

### Tapehead editing tools

| Shortcut | Action |
| --- | --- |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste the selected block or current cell |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo pattern edits (32 gestures) |
| Ctrl+A / Ctrl+L | Select the whole pattern / current lane |
| Shift+F3 / F4 / F5 | Cut / copy / paste the current lane |
| Ctrl+F3 / F4 / F5 | Cut / copy / paste the whole pattern |
| Alt+F3 / F4 / F5 | Cut / copy / paste the selected block |
| Shift+F1 / F2 | Transpose the current lane down / up one semitone |
| Ctrl+F1 / F2 | Transpose the whole pattern down / up one semitone |
| Alt+F1 / F2 | Transpose the selected block down / up one semitone |
| Ctrl+Up / Down, plus Shift | Transpose selection or current lane by a semitone / octave |
| Ctrl+Shift+V | Interpolate volume between explicit endpoints in every selected lane |
| Ctrl+D / Ctrl+R / Ctrl+B | Fill down from the first selected row / reverse selected rows / repeat the block immediately after itself |
| MIX PASTE in EDIT | Paste populated fields only; explicit volume 00, MIDI note 0 and FX Z00 remain populated values |
| Escape | Close EDIT, clear selection, then return to Sample |

The EDIT field mask independently enables NOTE, TILE, VOL, TUNE and FX for
clipboard operations, block clearing, fill and reverse. Stored TUNE/FX data is
preserved even though it is not displayed/executed by this playback stage.
Normal paste includes empty fields; mix paste keeps destination fields where
the source is empty. Paste clips at the active pattern/lane boundaries; it
never modifies hidden rows. Lane/pattern F5 pastes begin at row 000; ordinary
paste begins at the selection's top-left cell or current cursor.

Clipboard cells hold stable tile IDs, not Sample slot numbers. Clipboard and
undo history are internal to the current project and clear on project load.
Undo/redo restores pattern cells and row count, including hidden rows; it leaves
transport, voice phase, manual held notes, tile aliases and global tempo alone.
Creating or cloning a pattern is outside pattern-edit undo. One held ROWS
gesture makes one undo step. New edits discard redo history.

A bright outline always marks the current field. A blinking underline shows
the current hex digit; the first digit remains visible while the second is
pending. The footer shows the effective explicit/inherited alias and the
Sample tile that USE TILE will assign. Bound aliases are never silently
retargeted, including missing tile references. If a typed Sample slot is already
bound to another alias, that existing alias is used and reported in the status.

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
M/N and FX execution, direct output lanes, MIDI recording and cross-project
clipboard remapping remain later stages. Their existing saved definitions are preserved;
this runtime uses Standard forward timing, one voice per lane and Main routing.

The core test traces exact onsets at integer and fractional tick durations,
pause/rate changes, loop/end behavior, inheritance, OFF/CUT, two lanes sharing
one reader generation, mute/trim/solo, source edits, move/delete and reclamation.
The SDL controller test covers note/hex entry, workspace focus, independent Stop,
Sister TRACK ownership, heard/editor pattern separation and exact FILE OUT frames.
The editor test covers stable identity, masks, explicit zeros, clipped/mix paste,
row shifts, hidden-row preservation, transpose, interpolation and bounded undo.
The controller also exercises selection/clipboard/history during playback,
Tab/FX navigation, held-button repeat and wheel ownership/direction/FOLLOW.
All tests execute their setup and checks in Release builds.
