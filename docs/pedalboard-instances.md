# Pedalboard instances

An FX Canvas pedalboard occupies one of the existing 16 effect ports and owns
5–20 serial native or SunVox slots. Its children do not consume outer ports.
P1 is the shared Sister board; independent canvas boards appear as P2, P3,
and so on in the existing FX page. Double-click a canvas board, or choose
OPEN BOARD in its inspector. Arrows page through the tabs.

Tabs select a view; they do not change routing or mute other boards. Saved
board and child identities are independent of tab numbers and editor rows.
Deleting the selected board returns the editor to P1.

## Using SunVox

Cycle a slot's type to SUNVOX to open the existing factory/custom-patch picker.
EDIT SUNVOX reopens it. The three quick controls show the patch's first three
parameters; the full editor exposes all available controls, category/search,
file browsing, dropping a custom `.sunsynth`, and the 64/256/1024-frame setting.
P1 retains its PRE/head/POST placements. Canvas board slots are serial; patch
parallel paths and feedback on the outer canvas.

Each SunVox child has its own engine. Serial dependencies complete at their
render boundaries and use bounded spans, without taking additional outer
matrix job slots. Latencies add through the chain. The board's dry reference
follows each child's buffering and topology crossfade, including quantum
changes, so partial board mix and P1 send subtraction stay aligned.
Preparation and retired-engine destruction happen outside audio processing.
An unsuccessful preparation retains the committed board.

## Editing, saving and control

- Adding an effect to an empty slot starts at 0% mix for live editing. Cycling
  through effect types, including SunVox, retains the mix you have set.
- Reordering children retains their processors and histories. Replacing a
  child prepares and fades only that child; unaffected delays/reverbs retain
  their tails. Rapid topology edits wait for the preceding replacement to
  finish; parameter edits can continue during the transition.
- Projects and undo/redo store each board, child identity and SunVox controls.
  Custom patch bytes are embedded, so the source file need not remain present.
  Project state version 51 and preset version 27 continue loading older files.
- Added boards have a separate `pedalboard-presets.ini` bank through the usual
  preset controls: browse, Save As, rename, overwrite and delete. Recall changes
  only the selected board. P1 keeps its existing Sister preset bank.
- MIDI Learn addresses each child by board ID and child ID. Native controls,
  advanced filter controls and SunVox parameters can be mapped; reordering or
  changing editor tabs does not redirect a mapping. Removed targets are inert.
  Existing legacy P1 mappings remain supported.
- F12 PROFILE cycles through subsystem, matrix and BOARD FX pages. Child rows
  include stable IDs, average/peak block time and estimated cost. Each child
  samples 1/640 callbacks; these are inclusive sampled measurements, not
  worst-case CPU bounds. Copy/save includes the child rows.

Sixteen boards can structurally contain 320 processors, alongside the original
P1 board. The outer 16 ports are shared with standalone canvas effects. Actual
capacity depends on the selected effects, memory, buffer size and CPU. Boards
do not contain recursive boards; heavy SunVox patches remain heavy.

## Validation

The pedalboard core regression covers serialization, malformed data, 320
native children, independent histories, suspension, scalar/block/in-place
parity, 20 serial SunVox children at mixed quanta, dry alignment during quantum
changes, embedded custom assets, P1 SunVox sends, preserved delay tails after
child replacement/reordering, and child profile publication/reset.

The SDL controller regression covers tab paging and scaled input, independent
edits and undo/redo, mouse/keyboard removal confirmation, stale identities,
MIDI after reordering/tab changes, the SunVox picker on P1/P2, child parameter
edits and nested board preset round-trips. The rendered FX page was inspected.
Gallery ownership tests exercise the same crossfade/pause/shared-chain rules
with mixed SunVox/native boards as with standalone effects.

Real audio-device listening and sustained load validation remain necessary on
the user's hardware, particularly heavy patches and long feedback chains.

## Future Gallery sequencing (deferred)

A later Gallery sequencer may move between markers within one canvas or
between canvases. No sequencer UI or playback policy is added here. Sequence
actions should use stable canvas/marker identities and the existing transport
and Gallery transition paths. Keep same-canvas seeks distinct from cross-canvas
transitions. Selecting a pedalboard tab must remain a view-only action.
