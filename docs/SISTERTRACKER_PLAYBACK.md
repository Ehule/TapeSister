# SisterTracker: pattern playback, LEN and FastTracks

Press **F10** in the main Sample workspace to open SisterTracker. The first
opening creates a blank 64-row pattern; it stays stopped until Play. F10, Back
or Escape returns to Sample; tracker playback continues in the background.
Return from FM, a Mosaic event edit or an open dialog before opening Tracker.

Playback supports eight **Standard** or private **Pattern FastTracks** clocks,
song-wide LEN/CONTROL and the Main audio path. Each lane has one independent sample voice. Notes use TapeSister's pitch
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
   by STEP, initially one row. Use F1–F7 or OCT to select the keyboard octave.
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

Each lane has Tapehead's two-row header: **LEN OFF** or **LEN1–256** on
the top strip, and **FT** when Standard or the active ratio when Pattern
FastTracks is enabled. The 17 ratios range from 1:2 through 1:1 to 5:1.
Click FT to enable, click/wheel the ratio to change it, and click the three-LED
bank to return to Standard. Right-click the strip, or click the direction area
between the ratio and LEDs, for forward/reverse/ping-pong; only reverse **R** or
ping-pong **B** needs a badge. A saved Song assignment has an **S** badge but
continues playing Standard until order playback is implemented.
The LEDs show lag / exact synchronization / lead from the audio clock. Both
outer LEDs light when rows match but fractional phases differ. The eject
symbol selects CONTROL. Its red color comes from the CONTROL palette entry.
Compact M/S switches, the source row in hex and trim sit underneath. Default
TRACK 1–8 names are omitted; custom names add one strip.

With FOLLOW on, **normal lanes scroll through a fixed master-row band**.
**LEN and Pattern FastTracks lanes stay stationary**, with their own outlined
playheads moving over the data. Long private lanes change page only when their
actual source head crosses a viewport boundary. CONTROL sets the cycle timing;
it does not replace the master row used by normal lanes or the side numbers.
Pause freezes this view along with the audio clocks.

The shared Tapehead palette supplies distinct outlines: **LEN cyan**, **FT
amber**, **FT + LEN violet**, and **CONTROL red** by default. Populated FT
fields use TextOnBlock; empty fields retain PatternEmpty. Rows beyond an active
explicit LEN dim toward Desktop using Tapehead's exact `(color + 2*Desktop)/3`
rule, including empty fields. PAT mode removes LEN dimming from private tracks;
LEN bypass restores ordinary scrolling to LEN-only tracks and dims the FT
outlines and LEN/CONTROL header colors. The normal master band uses Desktop with white row
numbers rather than an extra colored outline across every lane.

Clicks, drags, marks and the edit locator use each lane's displayed source rows.
Pointer editing does not switch the running coordinate model or suspend FOLLOW.
Keyboard navigation or manual wheel scrolling suspends FOLLOW for an ordinary
shared editing view; FOLLOW or Play restores transport following. Editing a
different pattern also uses that ordinary view. Blank extension rows can be
shown, but cannot expose or edit retained hidden data. Literal Ctrl+L block
playback uses the shared row model without LEN dimming or private FT pages.

The pattern view reuses Tapehead's original normal, tiny and pattern pixel fonts,
recessed black lane panels, hex row numbers on both sides, current-row band,
colored field groups and CONTROL symbol. It honors the shared palette, including
PatternEmpty for empty dots. Sample selection, Sister routing outlines, locked
tile dimming and performance source borders are suppressed in the final audio UI
overlay while Tracker is visible. They continue to render in the Sample workspace.

All five stored groups are visible: NOTE, TILE, VOL, M/N and FX. Tuning and FX
have separate command and two-digit parameter cursor positions. These definitions
save, copy, paste and undo, but their commands do not yet execute in audio. The
footer labels command audio inactive.

### LEN, CONTROL and private clocks

| Control | Action |
| --- | --- |
| LEN click left/right or wheel | Increase/decrease 0–256; Shift steps by 8; Ctrl sets OFF |
| Eject symbol | Assign/unassign the one CONTROL lane |
| FT / active LED bank | Enable Pattern / return to Standard; preserves the stored ratio/direction |
| Ratio click / wheel | Next ratio / either direction through all 17 ratios |
| Right-click FT strip or click direction area | Cycle forward, reverse, ping-pong |
| Ctrl+Shift+1–8 | Toggle the corresponding Pattern FastTrack |
| Alt+Shift+1–8 | Cycle the corresponding ratio |
| Left-margin LEN/OFF | Bypass/restore all LEN and CONTROL without erasing settings |
| Left-margin FTL/PAT | Private tracks use lane LEN / physical pattern rows |

Standard lanes advance once per master row. Explicit LEN loops that lane over
its own domain. Without CONTROL, the longest explicit LEN sets the shared cycle;
if all LENs are OFF, physical pattern length sets it. CONTROL selects that lane's
LEN instead; CONTROL with LEN OFF selects physical pattern length. Thus a shared
cycle can be shorter or longer than the pattern container. Extension rows are
blank and never play retained hidden data or stop a sustaining sample by themselves.

Pattern FastTracks use Tapehead's rational accumulator on every shared audio
tick, including 1:1. Ratios mean source rows per master row. All crossed events
execute in order, even when 5:1 at TPL 1 crosses five rows in one tick. Private
heads keep their position and phase across shared loops. Reverse starts at row
zero and moves backward with wrap. Native ping-pong visits both endpoints once
per turn; LEN 1 remains a repeating one-row clock.

A private CONTROL finishes a cycle after its row domain's number of crossings
(a full out-and-back traversal for ping-pong), and requests the next audio tick
as the shared boundary, matching Tapehead's replayer boundary handoff. A slow
CONTROL can keep the master running across physical wraps. LEN-OFF Standard
lanes follow those physical wraps unless the shared domain extends beyond the
container, in which case the extension remains blank. Muting/soloing CONTROL
does not change its timing authority. LOOP off stops at the shared boundary.

Live ratio and TPL edits preserve normalized fractional row phase with Tapehead's
integer rounding. Direction edits preserve position and phase. Enabling a
Pattern clock aligns it with the current master phase; disabling returns to
Standard on the next master row. LEN changes apply at the next row/crossing and
reset cycle counters without replaying the current cell. Tempo/rate changes
preserve remaining tick time. Pause freezes all clocks and sample heads.
Ctrl+L always auditions literal block rows and ignores LEN, CONTROL, ratios and
directions. These controls remain editable during a block, for the next normal Play.

| Control | Action |
| --- | --- |
| Click grid / arrows | Move the edit cursor independently of the marked block; select NOTE, TILE, VOL, tuning command/value, or FX command/value; arrows wrap through all seven positions across eight lanes |
| Shift+arrows / Shift+click / left drag | Mark a rectangle of complete cells; Shift+arrows moves the stored active corner even after ordinary cursor navigation |
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
| LOOP | Wrap or stop at the shared LEN/CONTROL boundary |
| M / S / lane trim | Mute / temporary solo / independent trim from 0 to 2; clocks and heads continue |
| FOLLOW | Re-enable keeping the heard row visible when it belongs to the selected editor pattern |
| EDIT | Visible editing tools and clipboard field masks |

### Tapehead editing tools

| Shortcut | Action |
| --- | --- |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste the selected block or current cell |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo pattern edits (32 gestures) |
| Ctrl+A / Alt+C | Mark the whole pattern / current lane without moving the edit cursor |
| Ctrl+L | Start/stop literal block looping; replaces the old select-lane binding |
| F8 outside Block Loop | Extract the marked rectangle into a new pattern without changing focus or clipboard |
| F7 / F8 in Block Loop | Arm/stop live performance capture / capture one live cycle at loop seams |
| Ctrl+Alt+Backspace | Expand/restore the pattern view; transport shortcuts remain available |
| Ctrl+E | Open/close EDIT tools, including in expanded view |
| EXPAND X2 / SHRINK /2 in EDIT | Double row spacing / keep even rows and halve length; undo restores all original cells |
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
displayed and editable; its command audio execution remains a later stage.
Normal paste includes empty fields; mix paste keeps destination fields where
the source is empty. Paste clips at the active pattern/lane boundaries; it
never modifies hidden rows. Lane/pattern F5 pastes begin at row 000; ordinary
paste begins at the edit cursor, even while a separate source block is marked.
Alt+F5 also pastes at the cursor. This lets you copy a block, move freely and
paste elsewhere without losing the source mark.

Clipboard cells hold stable tile IDs, not Sample slot numbers. Clipboard and
undo history are internal to the current project and clear on project load.
Undo/redo restores pattern cells and row count, including hidden rows; it leaves
transport, voice phase, manual held notes, tile aliases and global tempo alone.
Block extraction is creation-aware: undo removes the new pattern; redo restores
the same stable pattern ID and complete contents. A referenced pattern is not
removed if doing so would change the order list. NEW and CLONE remain outside
pattern-edit undo. One held ROWS
gesture makes one undo step. New edits discard redo history.

A breathing outline marks the logical current field without painting over its text. A blinking underline shows
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

## Independent marks, block playback and extraction

A mark stores its own pattern ID and both corners. Plain arrows, clicks, note
entry, digit entry and automatic advancement never resize or clear it. Escape
closes EDIT first, then clears the current pattern's mark, then returns to Sample.
Switching editor patterns hides a mark belonging to another pattern. Switching
back restores it unless you deliberately marked a replacement elsewhere. Marks
and block transport are transient and reset on project load.

**Ctrl+L** starts the selected literal rows and lanes under the Standard clock.
It does not change user mute/solo/trim settings or claim manual/ARP/Mosaic voices.
A second Ctrl+L stops tracker playback. Space pauses/resumes with the exact tick
countdown and sample phase; Enter/PLAY starts ordinary full-pattern playback.
The pattern LOOP switch does not disable a running block loop.

Ordinary navigation leaves the loop alone. Deliberate marking gestures on its
playing pattern queue the latest rectangle, which becomes audible at the next
loop seam. Shift+arrows operates from the mark's stored moving corner, not an
unrelated cursor position. An amber lane-edge rail shows the active audible
bounds, selection fill shows the edited bounds, the normal playback band follows the master row, and the edit field retains its
own breathing outline and blinking digit underline.
Clearing the mark with Escape does not stop the running loop. Marking another
editor pattern cannot retarget the playing loop.

At block start, selected lanes inherit their last explicit TILE and VOL from
rows before the start, without executing any earlier notes. With no earlier
values they start with no tile and volume 40. An unresolved explicit tile remains
missing. Within repeated cycles, inherited values carry forward. Newly included
lanes are seeded from the rows before the new start; removed lanes release their
voices with the existing short handoff. Live row edits never replay the current
row. Shortening a playing pattern lets the current row finish, then clamps the
loop to valid rows at its next boundary; shortening does not erase hidden cells.

**F8 outside Block Loop** copies the entire marked rectangle, including tuning
and FX, into a newly allocated pattern. It rebases rows to zero and keeps the
original lane positions; other lanes stay empty. Clipboard masks do not filter
extraction. An empty block or full pattern bank is rejected. The source pattern,
mark, cursor, clipboard and order list stay unchanged. Unlike Tapehead's numeric
slot bank, TapeSister allocates a fresh stable ID; redo recreates that exact ID.

**EXPAND X2** inserts empty rows between all active rows and doubles the length,
up to 256. **SHRINK /2** keeps rows 0, 2, 4, ... and halves the length (rounded
down); discarded odd-row events can be restored with Undo. Rows outside the
operation's destination remain stored. These edits do not reposition audio.

## Compact and expanded views

The default eight-lane layout shows 19 rows, or 18 with custom lane names.
**Ctrl+Alt+Backspace** expands to 29 rows, or 28 with names. It hides the transport
and lower editing buttons; Enter, Space, Ctrl+L, F7/F8, Ctrl+E, Tab, F9 and F10
remain available. The renderer, row paging, follow scrolling and mouse hit tests
use one shared geometry calculation. Hidden BPM/ROWS/OUT/EQ controls cannot
receive clicks or wheel/MIDI-learn input in expanded mode.

Field labels are explained in bottom contextual help instead of repeated across
every lane. Hover a field, compact mix switch or LEN/FT header for its meaning.
The expanded view keeps recording feedback in its footer instead of overlaying
the grid with the ordinary FILE OUT display.

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

### Seam-quantized block capture

Plain **F7** arms the existing streaming WAV recorder at the next block seam.
Press F7 again before that seam to disarm, or after recording begins to finish at
the next seam. Resizing the block during a take is included in the performance.
Plain **F8** during Block Loop records from the next seam to the following seam.
Both leave block playback running and save automatically to the normal Captures
folder (`TAPESISTER_CAPTURES` overrides it), with BlockPerformance or BlockCycle
in the filename. A busy file/tile recorder must finish first.

These are **live final-output captures**: they include Sister/FX/EQ/output gain
and any other audible native sources, including pauses and effect tails within
the take. This intentionally adapts Tapehead's offline F8 rendering to
TapeSister's live processing and external inserts. It does not reset effects or
append post-cycle tails. Arm while paused waits for the next seam after resume;
an already active take continues recording the actual output while paused.
Stopping tracker playback ends the take at the stop, which can produce a partial
cycle. Ctrl+Shift+F offers an immediate file stop through the existing recorder.

Boundaries are gated per output frame in the callback. File creation, WAV/RF64
writing and finalization stay on the controller/writer thread; no allocation,
freeing, file I/O or mutable editor access was added to the callback.

## Scope and checks

This is pattern playback, not Song/order playback yet. The grid edits NOTE,
TILE, VOL, M/N and FX definitions. Song transport, overlap, M/N and FX execution
(including Z commands), direct output lanes, MIDI recording and cross-project
clipboard remapping remain later stages. FastTracks master suspend, sync,
clutches and randomization are not provided by this batch. Saved Song definitions
are preserved and clearly identified; this runtime treats them as Standard.
There is one sample voice per lane with Main routing.

Clock regressions cover every ratio at TPL 1/2/6/31 and integer/fractional sample
timing; Standard/1:1 alignment through many shared loops; multiple crossings,
reverse/ping-pong, LEN 1/256, short/long domains, hidden-data exclusion, CONTROL,
bypass/use-LEN options, live changes and pause/rate preservation. SDL tests drive
actual header clicks, wheel modifiers, shortcuts, overlay ownership, both views,
live publication and per-lane display rows.

The core test traces exact onsets at integer and fractional tick durations,
pause/rate changes, loop/end behavior, inheritance, OFF/CUT, two lanes sharing
one reader generation, mute/trim/solo, source edits, move/delete and reclamation.
The SDL controller test covers note/hex entry, workspace focus, independent Stop,
Sister TRACK ownership, heard/editor pattern separation and exact FILE OUT frames.
The editor test covers stable identity, masks, explicit zeros, clipped/mix paste,
row shifts, hidden-row preservation, transpose, interpolation and bounded undo.
The controller also exercises selection/clipboard/history during playback,
Tab/FX navigation, held-button repeat and wheel ownership/direction/FOLLOW.
Additional checks cover independent marks through navigation and entry,
block starts partway through patterns, selected-lane execution, fractional seam
timing, pending resize through pause, shortening past the heard row, extraction
identity/order/clipboard preservation, creation undo/redo, expand/shrink hidden
rows, expanded hit testing and final-output frame-exact captures. Actual F7/F8
controller dispatch and the streaming writer are exercised, including busy-file
ownership. All tracker tests execute their setup and checks in Release builds.

See [Tapehead parity inventory](SISTERTRACKER_PARITY.md) for inherited operations,
intentional adaptations and the remaining implementation stages.
