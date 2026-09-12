# Mosaic

Mosaic arranges sample events on a free canvas. Time runs downward. Horizontal
position and width organize the picture; they do not change timing or pan.
The arrangement is another input to TapeSister's existing global effects,
Sister Machine and output recorder.

![Mosaic in the native renderer](images/mosaic.png)

## Playing and arranging

Open **MOSAIC** in the main toolbar, or press **Shift+grave** (Shift+the backtick
key). The same shortcut returns to the main canvas or finishes editing an event;
**Ctrl+M** remains available. From Sister, the shortcut brings Mosaic forward,
even when it was already open behind Sister. From FM, it visits Mosaic and returns
to the same FM patch on the next press. Plain **grave** opens FM directly from
Mosaic; pressing it again returns to Mosaic. **Tab** continues to visit Sister
without changing the open event, and **Escape** in Sister restores the main
application window. Escape from an event editor still returns to Mosaic.
**Ctrl+Shift+P** also reaches the Portal from these workspaces. Destination dialogs
retain keyboard focus until resolved.

Sources browses **all Sample banks**, including banks created in FM or the main
canvas. **BANK 01**, **BANK 02**, etc. retain their original slot numbers and empty
slots. The arrows underneath move through banks, then **EVENTS** pages containing
the audio versions used by placed events. Browsing sources does not switch the
main editor's active Sample bank or change routing. New banks appear on the next
refresh; returning from FM shows its active bank with the correct page number.

Drag an occupied source onto the canvas. Click a source to highlight its waveform;
dragging shows a ghost of the proposed event. An accepted audio edit appears in
the EVENTS pages. Shared originals and the original Sample banks remain available.

| Action | Gesture |
| --- | --- |
| Select / move an event | Click / drag its body |
| Extend the event's playback window | Drag its bottom edge |
| Change visual width | Drag its right edge |
| Align starts, ends or midpoints | Hold Alt while moving or extending |
| Edit the event | Double-click |
| Box-select a group | Shift+left-drag on empty canvas |
| Move a selected group | Drag any selected event body |
| Copy an event or selected group | Hold Shift when starting a drag on a tile |
| Cancel a move / copy drag | Escape |
| Mute / unmute selected events | M |
| Solo / unsolo selected events | S |
| Clear every solo, including offscreen events | CLEAR SOLO in footer |
| Set selection level, pan and fades | LEVEL / PAN / IN / OUT in footer |
| Global tape speed, 0.5×–2× | SPEED slider above canvas |
| Reset a mix or speed control | Right-click or double-click |
| Copy / paste at the playhead | Ctrl+C / Ctrl+V |
| Delete | Delete or Backspace |
| Undo / redo arrangement edits | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Play / pause | Space or PLAY/PAUSE |
| Stop and rewind | Escape or STOP |
| Seek | Middle-click the canvas or click the time ruler |
| Scroll with playback | Enable FOLLOW |
| Return to the beginning | Home |
| Scroll through time | Mouse wheel |
| Scroll horizontally | Shift+wheel |
| Zoom around the pointer | Ctrl+wheel |
| Fit the arrangement | FIT ALL |
| Record the final output | REC FILE or Ctrl+Shift+F |

Events that overlap in time occupy separate horizontal space, with a small
invisible gutter. Collision handling moves only the event being placed and
never changes its time. Group moves preserve all relative times and positions;
the group shifts sideways together if it meets another event. Shift-dragging a
tile duplicates the selection, keeping its relative timing, spacing and playback
settings. The originals stay in place. A click without movement creates no copies;
one Undo removes the copied group. If there is not enough capacity for the whole
group, nothing is copied. Alt retains optional time alignment during a move or resize. A dashed waveform
ghost marks the original position while moving. Events may meet exactly end to end. There are no tracks,
beats or mandatory snapping. Click an unselected event to return to a single
selection. Selected tiles and Sources previews use the bright red highlight color by default. Muted events and events excluded by
solo are dim; MUTE/SOLO appears on the relevant cards, and excluded cards say
SOLO OUT (EXCL on narrow cards). CLEAR SOLO stays visible in the footer whenever any event is soloed,
including events outside the viewport. Multiple events may be
soloed together. Mute takes precedence over solo. These switches use a short
fade, and all voice clocks continue while inaudible, so unmuting resumes their
current phases. Delete applies to the selection; copy/paste uses the last
clicked event. FOLLOW keeps the playhead in view while playing and suspends
scrolling during a drag.

Each event supports one to five notes. A C5 voice traverses the source twice as
fast as C4; C3 takes twice as long. Every voice keeps its own phase. Extending
a looping event increases its available repetitions without restarting its
voices or changing their pitch. The waveform drawing shows source repetitions
at the C4 reference rate; chord voices can cross those visual divisions at
different times. Event and source waveforms render at the window's actual pixel
resolution, with antialiased edges, while the surrounding pixel UI stays familiar.
The brighter body shows typical energy and the outer envelope preserves peaks.
Waveform detail follows fixed sample intervals attached to the event. Scrolling
moves each complete tile—waveform, labels, borders and repeat dividers—together
in single physical-pixel steps. The tile keeps its height and waveform shape;
header and footer spacing stays attached as it crosses the canvas edges.
Tiles that meet end to end share the same drawn boundary. The time ruler and its
labels scroll with the grid, keeping their alignment with the tiles.
Overview drawing reuses cached 2,048-bin peak/RMS envelopes; close zoom reads
actual sample ranges instead of enlarging those bins. Source thumbnails cache
extrema at their displayed width. None of this analysis runs in the audio callback. **REPEAT** repeats the complete arrangement at its last event.

## Performance controls

Select an event, then use **LEVEL**, **PAN**, **IN** and **OUT** in the footer.
A selected group receives the same adjustment in one undoable gesture. These controls
shape the event's playback without changing its source audio or restarting its voices.
LEVEL runs from silence to +6 dB (center = 0 dB). PAN is a stereo balance: center
preserves both original channels, and moving to either side attenuates the opposite
channel without summing it into the other. Mono samples can also be positioned this way.
IN and OUT set linear fade times in arrangement seconds, from zero to the selected
event's duration. For a group, each fade is capped at that member's duration. Fades
that overlap after editing or shortening a tile scale proportionally to fit its length.

**SPEED** above the canvas is global tape speed: left = **0.5× / one octave down**,
center = **1× / C4 reference**, right = **2× / one octave up**. Movement is continuous
and smoothed; the playhead, starts, durations, fades and every note's source phase
advance together. Chord intervals and relative event placement stay intact. It affects
Mosaic before the shared effects, so pedalboard delays and reverbs retain their own
settings. A stopped arrangement uses the selected rate when playback starts.

Drag a control, or wheel over it for increments. Speed wheel steps are one semitone;
LEVEL steps are 1 dB, PAN steps are 5%, and fade steps are 0.1 second. **Shift+wheel**
uses one-tenth steps. **Right-click or double-click** resets to unity level, center pan,
zero fade or 1× speed. **Escape during a drag** restores its initial setting; Undo/Redo
also includes these controls. The saved project includes level, pan, fades and speed;
older projects start with center pan, zero fades and 1× speed. Projects saved with
these controls use Mosaic format 3 and need this version or newer to reopen.

## Mosaic colors

**CFG → PALETTE** has a compact **MOSAIC** swatch strip: **HILITE** and **1–5**.
Click one, then use the existing RGB sliders or Tapehead eyedropper to change it.
PgUp/PgDn cycles through the main colors and these six Mosaic entries. The five
source colors consistently color waveforms and tile borders; the highlight color
is separate and defaults to bright red. **Save Shared** keeps all six colors in
`palette.pal`; **Cancel** restores the palette from before editing. Older palettes
that omit these keys use the default Mosaic colors.

![Mosaic colors in the palette editor](images/mosaic-palette.png)

## Editing one event

Double-click opens the familiar main editor for that event. Click the onscreen
keyboard to add/remove persistent chord notes, up to five. One note remains
selected. The **EVENT LOOP / EVENT ONCE** button above the keyboard sets playback
mode. Existing loop handles choose the source region, direction and crossfade.
If no loop is defined, selection or the whole source supplies the region.

A one-shot voice ends after one traversal or at the event's bottom, whichever
comes first. A longer one-shot rectangle therefore reserves time without
retriggering the sound. Each note can finish at a different time.

**MOSAIC** returns to the arrangement. Event voices keep playing while the main
editor, CDP Portal or Sister window is open. Sample editing uses the event's
own document. Copies share immutable source audio until an actual sample edit
creates a new version for that event. The original Sample bank stays intact.
Make as many drawing or processing edits as you like, including Warp, Smear
and CDP. The working waveform stays in this event's editor. The destination
prompt appears **once when returning to Mosaic**, only if its audio has changed:

- **NEW TILE** (Enter or N) keeps the original event and places a new event
  beside it with all the accumulated audio edits. It also places a reusable snapshot
  into a free regular Sample-bank tile, adding a bank if needed. The arrangement
  scrolls to reveal the new event and Sources highlights its bank tile; the status
  names the bank and slot. The main editor's previous selection stays intact.
- **UPDATE TILE** (U) replaces only this event's audio, then returns to Mosaic and
  highlights its current source. Existing bank snapshots remain independent.
- **KEEP EDITING** (Escape) closes the question and retains all working edits.

An occupied or protected bank tile is never overwritten by NEW TILE. Arrangement
Undo can remove the new event while its reusable bank snapshot remains available.
If a new bank snapshot cannot be stored, the working edit stays open for another choice.

Undoing all audio edits removes the question. Opening an event and leaving it
unchanged never prompts. Loop/one-shot and chord switches remain immediate
per-event properties; audio-dependent bounds travel with the working audio.
Mosaic keeps playing its existing immutable sources while the working audio
can be auditioned in the editor. Originals and sibling events retain their
samples until a destination is chosen. Allocations and cleanup stay outside the
audio callback. Saving, opening a project or closing the main window waits for
this decision and then continues the requested action.

![Choosing the destination of an audio edit](images/mosaic-edit-choice.png)

Live distortion, reverb and the other effects remain global. Mosaic feeds the
existing **TILES** input when Sister Machine is powered. With Sister off it
feeds the ordinary shared Fallout/pedalboard/output path. Fallout's controls
and processing are available with Sister off. **Master FX** bypasses the pedalboard
and Fallout together, including Fallout's feedback return. Its configured transition
time still applies. Individual pedal/Fallout settings and modulation clocks are
preserved, so enabling Master FX restores the chosen setup. This bypass affects
the live processors; audio previously recorded into Sister's buffer retains its
recorded sound. Sister power preserves the shared pedal settings, transitions,
tails and Fallout modulation state; it only changes the rolling-machine route. A single rendering of each
voice supplies those routes.

Workspace switching and audition stop leave Mosaic playback running. The toolbar
MOSAIC button stays highlighted while it plays, and stopping an audition reports
**AUDITION STOPPED - MOSAIC CONTINUES**. Stop the arrangement with its STOP control
or Escape in Mosaic; Master FX bypass leaves the unprocessed source audible.

## CDP ownership

An event render remembers its requesting event ID, project generation, source
revision and source audio snapshot. Closing the Portal or selecting another
event does not cancel that event's worker. Quick-apply returns to the requesting
event; ordinary previews stay available for explicit Apply. Reopening the Portal
shows the retained result and its event label. RELOAD takes a new snapshot of
the currently edited event after the running job finishes.

If the requesting event was edited, deleted or belongs to a previous project,
Apply retains the preview and reports the conflict. It cannot replace whichever
event happens to be on screen. Apply into the currently open event joins its
working edits and uses the same exit-time destination question. A result returning
to a parked event updates that owner directly. Publishing the exact worker input
on exit does not invalidate the job. Creating a new variation retains the original
event document for any job it already requested; incompatible results remain
available for review instead of overwriting another event.
The older Transform workbench must finish its worker before leaving the event.

## Recording and projects

The header and footer **REC FILE** buttons, or `Ctrl+Shift+F` in the main window,
operate the same recorder. It writes the final stereo OUT signal, including global effects and
master output controls, through the existing asynchronous WAV recorder. It
continues across editor/window navigation. STOP REC finishes the file without
stopping Mosaic, allowing effects tails to be recorded deliberately. Completed
takes use the existing timestamped `Captures/` archive.

SAVE stores event positions, durations, notes, source regions, loop modes,
names, level/pan/fades, mute/solo flags, global speed and arrangement repeat setting in the project transaction. Shared audio
versions are written once each as lossless 32-bit float WAVs under
`project-data/`. Earlier Mosaic projects load with events unmuted and unsoloed; projects from
before Mosaic open with an empty arrangement. A project marked as
containing Mosaic fails to load if its arrangement file is missing, preserving
the current session. Transport position and editor/arrangement undo history
are session state.

The initial capacity is 128 events, with five voices per event. This is a storage
limit, not a promise that every machine can run 640 voices plus all global
processing simultaneously. DISTSHIFT and further CDP expansion are unchanged.

## Performance update acceptance

- Smear an event and choose NEW TILE with enough overlapping cards to fill the view.
  Confirm the new waveform is revealed, selected and present in the regular Sample
  banks. Save/reopen and audition the bank tile and event.
- Solo a card and scroll it offscreen. Check CLEAR SOLO and the other cards' SOLO OUT
  labels; clear solo and confirm deliberate mute switches remain intact.
- Move SPEED during a long chord. Check both octave endpoints, continuous phase,
  pause/resume, repeat, and reset to center. Record a take while moving the control.
- Shape a group with level, pan and fades. Check undo, reset, stereo separation and
  save/reopen. Existing projects should play as they did before these controls.

## First build acceptance

- Place two copies of one fragment with overlapping times. Move and extend them;
  confirm that spacing changes do not move their start times unintentionally.
- On a long loop choose C4, E4 and C5. Listen for independent repeating gestures;
  extend the event while it plays and check that the phases continue.
- Open the other copy, set EVENT ONCE and make several audio edits. Return to Mosaic and choose NEW TILE,
  UPDATE TILE and KEEP EDITING on separate attempts; check that siblings and the
  original source bank retain their sound in every case.
- Create a new full bank from FM while the welcome sample remains on the first
  bank. Browse both banks in Mosaic and place a sample from each; confirm that
  the main editor stays on its original active bank.
- Change HILITE and the five Mosaic swatches through RGB and the Tapehead
  eyedropper. Save Shared, reopen and check the colors.
- Enlarge the window and check smooth event/source waveforms, clipping and playhead visibility.
- Left-click CREATE for FM; right-click repeatedly for CDP variations of the same
  source; middle-click during and after rendering to restore that source.
- Start a CDP render, return to Mosaic and open the other event. Confirm that
  the completed result belongs to its requesting event.
- Record while using the editor and Sister/pedalboard. Stop the file recording,
  then audition the saved WAV.
- Save, close and reopen a project. Check event geometry, chords, loops and
  edited source versions before using the build for a longer composition.

## Interaction update acceptance

- Click a source, drag it onto the canvas, and check its highlight and placement
  ghost. Edit one of two shared events and check the separate source previews.
- Shift-drag a box around two events. Shift-drag the selection to copy it, then Undo. Move the pair against another event and
  confirm their relative gaps/times stay intact. Undo the group move.
- Toggle M and S while playing pitched loops; confirm the dimming and smooth
  return at the continuing phase. Save/reopen to check mute/solo state.
- Middle-click inside a tile to seek. Enable FOLLOW and play beyond the visible
  canvas; disable it to scroll freely.
- Adjust the pedalboard, start a long effect transition, and toggle Sister
  power. Check that settings and transitions survive. Use Fallout with Sister
  off, including its power switch, modulation and feedback.

Automated coverage lives in `test_mosaic.c` and `test_mosaic_controller.c`:
independent clocks, 24 seek combinations, one-shots, resizing, source sharing,
undo/redo, geometry, project transactions, background native CDP completion,
and sample-for-sample output WAV capture. Set `TS_TEST_CDP_BIN` to the bundled
`cdp/bin` directory for the native CDP checks. Hardware listening and interaction
on the target machine remain part of acceptance.
