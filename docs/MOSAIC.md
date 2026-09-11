# Mosaic — first playable phase

Mosaic arranges sample events on a free canvas. Time runs downward. Horizontal
position and width organize the picture; they do not change timing or pan.
The arrangement is another input to TapeSister's existing global effects,
Sister Machine and output recorder.

![Mosaic in the native renderer](images/mosaic.png)

## Playing and arranging

Open **MOSAIC** in the main toolbar, or press **Ctrl+M**. Drag an occupied source
from the left bank onto the canvas. Sources shows the audio versions used by placed events first, followed by
unused samples from the current Sample bank. An edited event's waveform updates
there when edits are accepted on returning to Mosaic. Shared originals remain available while other events use
them; the original Sample bank is preserved. Use the arrows beneath Sources
for additional pages, and return to CANVAS to load/create samples or change
Sample banks. Click a source to highlight its waveform; dragging shows a ghost
of the event at its proposed position.

| Action | Gesture |
| --- | --- |
| Select / move an event | Click / drag its body |
| Extend the event's playback window | Drag its bottom edge |
| Change visual width | Drag its right edge |
| Align starts, ends or midpoints | Hold Shift while moving or extending |
| Edit the event | Double-click |
| Box-select a group | Shift+left-drag on empty canvas |
| Move a selected group | Drag any selected event body |
| Mute / unmute selected events | M |
| Solo / unsolo selected events | S |
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
the group shifts sideways together if it meets another event. A dashed waveform
ghost marks the original position while moving. Events may meet exactly end to end. There are no tracks,
beats or mandatory snapping. Click an unselected event to return to a single
selection. Selection has a bright border. Muted events and events excluded by
solo are dim; MUTE/SOLO appears on the relevant cards. Multiple events may be
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
Overview drawing reuses cached 2,048-bin peak/RMS envelopes; close zoom reads
actual sample ranges instead of enlarging those bins. Source thumbnails cache
extrema at their displayed width. None of this analysis runs in the audio callback. **REPEAT** repeats the complete arrangement at its last event.

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
  beside it with all the accumulated audio edits, then returns to Mosaic.
- **UPDATE TILE** (U) replaces only this event's audio, then returns to Mosaic.
- **KEEP EDITING** (Escape) closes the question and retains all working edits.

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
and processing are available with Sister off, independently of the pedalboard
master switch. Sister power preserves the shared pedal settings, transitions,
tails and Fallout modulation state; it only changes the rolling-machine route. A single rendering of each
voice supplies those routes.

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

REC FILE writes the final stereo OUT signal, including global effects and
master output controls, through the existing asynchronous WAV recorder. It
continues across editor/window navigation. STOP REC finishes the file without
stopping Mosaic, allowing effects tails to be recorded deliberately. Completed
takes use the existing timestamped `Captures/` archive.

SAVE stores event positions, durations, notes, source regions, loop modes,
names, mute/solo flags and arrangement repeat setting in the project transaction. Shared audio
versions are written once each as lossless 32-bit float WAVs under
`project-data/`. Earlier Mosaic projects load with events unmuted and unsoloed; projects from
before Mosaic open with an empty arrangement. A project marked as
containing Mosaic fails to load if its arrangement file is missing, preserving
the current session. Transport position and editor/arrangement undo history
are session state.

The initial capacity is 128 events, with five voices per event. This is a storage
limit, not a promise that every machine can run 640 voices plus all global
processing simultaneously. DISTSHIFT and further CDP expansion are unchanged.

## First build acceptance

- Place two copies of one fragment with overlapping times. Move and extend them;
  confirm that spacing changes do not move their start times unintentionally.
- On a long loop choose C4, E4 and C5. Listen for independent repeating gestures;
  extend the event while it plays and check that the phases continue.
- Open the other copy, set EVENT ONCE and make several audio edits. Return to Mosaic and choose NEW TILE,
  UPDATE TILE and KEEP EDITING on separate attempts; check that siblings and the
  original source bank retain their sound in every case.
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
- Shift-drag a box around two events. Move the pair against another event and
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
