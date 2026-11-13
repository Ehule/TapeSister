# Pedalboard instances — draft implementation

An FX Canvas pedalboard occupies one of the existing 16 effect ports and owns
5–20 serial native effect slots. Its children do not consume outer ports. The
original shared pedalboard remains P1; added canvas boards appear as P2, P3,
and so on in the existing FX page. Double-click a canvas board, or use OPEN
BOARD in its inspector, to select its page. Arrow buttons page through tabs.

Tabs select an editor view. They do not switch audio routing or mute other
boards. Runtime identities remain stable even when deleting a board changes
the displayed tab numbers. Deleting the selected board returns the view to P1.

## Implemented in this slice

- Independent native DSP, allocated only for populated slots; the large legacy
  Sister engine is not duplicated for each board.
- Slot parameters, bypass transitions, ordering, 5–20 slot count, parameter
  locks, filter editor, and native grain/transient state controls.
- Existing canvas routing, whole-board bypass/mix/gain, lifecycle staging,
  suspension, and board-level profiling.
- Project persistence and matrix undo/redo. Selecting tabs does not add undo
  entries. Existing Sister preset recall stays scoped to P1; added boards are
  saved with the project.
- Sample and block processing parity, including in-place block buffers.

The 16 outer ports remain shared with standalone canvas effects. Sixteen
boards can structurally contain 320 native processors, but real-time capacity
depends on the chosen effects and available CPU. The 320-processor regression
uses neutral transient processors and is not a real-time performance claim.

## Remaining work on this draft

- SunVox child slots and effect selection, including access from P1. Their
  scheduling, latency/dry alignment, assets, persistence and failure recovery
  need integration before the SunVox option can be exposed.
- Child-level topology staging. Changing a populated slot's type or moving
  different effect types currently crossfades a replacement of the entire
  board using the existing matrix lifecycle. Parameters and empty row count
  changes retain processing history; topology edits can shorten other tails.
- Per-child MIDI identity and profiling. MIDI learning for independent-board
  child controls is intentionally unavailable in this slice.
- Hardware audio validation, including Gallery transitions,
  idle suspension and long-delay/reverb tails under editing.

Boards contain native effects in this slice, not recursive pedalboards. Their
slots are serial; feedback and parallel branches belong to the outer canvas.

## Validation

`tapesister_pedalboard_instances_tests` covers serialization, malformed input,
320 native children in 16 ports, instance isolation, suspension, block/sample
parity, in-place processing and empty-row topology stability.

`tapesister_pedalboard_controller_tests` drives the SDL controller and checks
P1/P2/P3 isolation, undo/redo, count changes, removal confirmation by mouse and
keyboard, keyboard scrolling, tab paging, scaled input and stale identity
handling. Both tests are included in the PR regression baseline.

## Future Gallery sequencing (deferred)

The planned Gallery sequencer may move between markers in the same canvas or
between markers in different canvases. It is outside this PR's implementation
scope; no sequencer UI or playback rules are being added here.

Board and child identities must remain independent of tab numbers, editor
selection and marker positions. Later sequence actions should target stable
canvas/marker identities and use the existing transport and Gallery transition
paths. Preserve the distinction between a same-canvas seek and a cross-canvas
transition when testing effect history, tails and suspension. Switching an
editor tab must never become a playback or effect-reset command.
