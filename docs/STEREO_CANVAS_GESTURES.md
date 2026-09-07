# Stereo canvas gestures

The edit canvas treats a stereo frame as one left/right pair. Every selection,
loop boundary and edit position uses frames, not individual interleaved samples.
Choosing L, R or Sum changes the display; it does not select a channel to edit.

## Gesture audit

| Gesture or control | Stereo behavior |
| --- | --- |
| Click playhead / right-click audition / double-click tile editing | Shared time position; stereo playback |
| Selection drag, selection handles, loop endpoint drag | One time range for both channels |
| Wheel zoom / Shift+wheel pan | View changes only; audio is unchanged |
| Alt+wheel selection resize | Both channels follow the same boundary while looping |
| Ctrl+wheel rotation, Ctrl+Shift+wheel fine rotation | Rotate paired frames; selected-range fallback uses the correct stereo offset |
| Shift+Alt+wheel stretch, Ctrl+Shift+Alt+wheel fine stretch | Common resampling positions and crossfades; surrounding audio stays in place |
| Shift+left drag | Copy selection and mix into destination |
| Shift+right drag | Copy selection and overwrite destination |
| Ctrl+left drag | Move selection and mix into destination; source becomes a gap |
| Ctrl+right drag | Move selection and overwrite destination; source becomes a gap |
| Canvas edge drag, half/double canvas | Add/remove paired frames; silence is silent in both channels |
| DRAW freehand / Shift polyline | One gain profile applied to both channels |
| BODY / EDGE / DRIFT drag or Ctrl+wheel | Stereo material processing with shared range and gesture history |
| WARP / SMEAR / TEAR drag or Ctrl+wheel | Stereo preview, commit and cancel |
| COPY / CUT / PASTE / FIT canvas controls | Stereo clipboard retained; Cut closes the join and pads the end, while Cut+Crop shortens the canvas |

Move/Copy now supports stereo in both the editor and saved edit replay. Placement
outside the canvas extends it, with zero-filled gaps. Overlapping moves read from
an intact source snapshot. Mix peak compensation uses one gain for both channels
so it does not independently normalize left and right. Edge fades also share one
frame position and gain. The drag ghost uses the same Stereo/L/R/Sum view as the
underlying waveform. Each completed move/copy has its own Undo step.

Snapping examines the louder channel across each adjacent frame pair, with left
winning ties. If no crossing exists, nearest-crossing searches use the lowest
peak across the two channels. This avoids treating a silent channel or an
opposite-polarity stereo pair as silence. The boundary remains common to both
channels; independent waveforms are not guaranteed to cross zero simultaneously.
SEL WAVE likewise requires both channels to be silent before trimming an edge.

This audit covers canvas editing gestures. The existing stereo restrictions on
Drone creation, Vary/Create and the mono CDP catalog are separate process features.

## Verification

`tapesister_stereo_gesture_tests` is part of `make test`. It covers all four tape
placements, overlapping moves, prepend/append, independent channel impulses,
shared mix balance, invalid destinations, saved-project replay, Undo/Redo,
opposite-polarity and right-only snapping, selection and loop boundaries,
rotation fallback, clipboard edits, stretch, canvas edges, DRAW and material
control preview/cancel/commit.

The optional `tapesister_stereo_gesture_controller_tests` uses dummy SDL devices
to exercise the actual modifier-drag helpers, all four ghost display modes,
DRAW and the audio callback with held QWERTY notes across edits. Set
`TS_TEST_STEREO_GESTURE_SCREENSHOT` to a PPM path to capture its native render.

Core, existing canvas and sample-channel tests plus the preview keyboard/loop
regressions are also run. Address/undefined-behavior sanitizer checks are used;
leak detection is unavailable in the current environment. Windows compilation
and hardware listening remain the release check.
