# Mosaic — first playable phase

Mosaic arranges sample events on a free canvas. Time runs downward. Horizontal
position and width organize the picture; they do not change timing or pan.
The arrangement is another input to TapeSister's existing global effects,
Sister Machine and output recorder.

![Mosaic in the native renderer](images/mosaic.png)

## Playing and arranging

Open **MOSAIC** in the main toolbar, or press **Ctrl+M**. Drag an occupied source
from the left bank onto the canvas. The current Sample page supplies that bank;
return to CANVAS to load/create sources or change Sample pages.

| Action | Gesture |
| --- | --- |
| Select / move an event | Click / drag its body |
| Extend the event's playback window | Drag its bottom edge |
| Change visual width | Drag its right edge |
| Align starts, ends or midpoints | Hold Shift while moving or extending |
| Edit the event | Double-click |
| Copy / paste at the playhead | Ctrl+C / Ctrl+V |
| Delete | Delete or Backspace |
| Undo / redo arrangement edits | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Play / pause | Space or PLAY/PAUSE |
| Stop and rewind | Escape or STOP |
| Seek | Click the time ruler |
| Return to the beginning | Home |
| Scroll through time | Mouse wheel |
| Scroll horizontally | Shift+wheel |
| Zoom around the pointer | Ctrl+wheel |
| Fit the arrangement | FIT ALL |
| Record the final output | REC FILE or Ctrl+Shift+F |

Events that overlap in time occupy separate horizontal space, with a small
invisible gutter. Collision handling moves only the event being placed and
never changes its time. Events may meet exactly end to end. There are no tracks,
beats or mandatory snapping.

Each event supports one to five notes. A C5 voice traverses the source twice as
fast as C4; C3 takes twice as long. Every voice keeps its own phase. Extending
a looping event increases its available repetitions without restarting its
voices or changing their pitch. The waveform drawing shows source repetitions
at the C4 reference rate; chord voices can cross those visual divisions at
different times. **REPEAT** repeats the complete arrangement at its last event.

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
Completed drawing/transform gestures publish a replacement source; allocations
and source cleanup happen outside the audio callback.

Live distortion, reverb and the other effects remain global. Mosaic feeds the
existing **TILES** input when Sister Machine is powered. With Sister off it
feeds the ordinary shared effects/output path. A single rendering of each
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
event happens to be on screen. In an event's Portal, use Apply to update that
event; duplicate the event in Mosaic first when a separate variation is wanted.
The older Transform workbench must finish its worker before leaving the event.

## Recording and projects

REC FILE writes the final stereo OUT signal, including global effects and
master output controls, through the existing asynchronous WAV recorder. It
continues across editor/window navigation. STOP REC finishes the file without
stopping Mosaic, allowing effects tails to be recorded deliberately. Completed
takes use the existing timestamped `Captures/` archive.

SAVE stores event positions, durations, notes, source regions, loop modes,
names and arrangement repeat setting in the project transaction. Shared audio
versions are written once each as lossless 32-bit float WAVs under
`project-data/`. Older projects open with an empty Mosaic. A project marked as
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
- Open the other copy, set EVENT ONCE and edit its audio. The first copy and
  original source bank must retain their sound.
- Start a CDP render, return to Mosaic and open the other event. Confirm that
  the completed result belongs to its requesting event.
- Record while using the editor and Sister/pedalboard. Stop the file recording,
  then audition the saved WAV.
- Save, close and reopen a project. Check event geometry, chords, loops and
  edited source versions before using the build for a longer composition.

Automated coverage lives in `test_mosaic.c` and `test_mosaic_controller.c`:
independent clocks, 24 seek combinations, one-shots, resizing, source sharing,
undo/redo, geometry, project transactions, background native CDP completion,
and sample-for-sample output WAV capture. Set `TS_TEST_CDP_BIN` to the bundled
`cdp/bin` directory for the native CDP checks. Hardware listening and interaction
on the target machine remain part of acceptance.
