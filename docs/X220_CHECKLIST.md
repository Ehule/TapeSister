# TapeSister independent-tile release checklist

This is a musical and interaction checkpoint. Passing automated tests alone is not approval.

## Independent Bank tiles

- [ ] Loading or generating a sound places it in the selected tile without changing any other occupied tile.
- [ ] Clicking an occupied tile restores that tile's exact audio, selection, viewport, loop, tuning, processing, edit timeline, and Undo/Redo history.
- [ ] Clicking an empty tile produces silence and a blank editor without selecting a fallback tile.
- [ ] Crop, Reverse, Normalize, Amplify, Fade, WARP, SMEAR, TEAR, tape gestures, tuning, loop, and DSP controls affect only the selected tile.
- [ ] Undo and Redo traverse only the selected tile's history; switching tiles never merges or clears histories.
- [ ] Clearing a tile never changes another tile. Any tile, including tile 01, can be cleared.
- [ ] Rename changes only the selected occupied tile's name.
- [ ] Saving and reopening a TSR16 project restores every occupied tile as an independent object and preserves an empty selected tile.

## Create, Vary, and Chain

- [ ] Create writes a new result to an explicitly available destination and does not overwrite an occupied tile.
- [ ] Vary derives a result from the selected occupied tile when Chain is off.
- [ ] With Chain on, each newly created result becomes the source for the next Vary operation.
- [ ] With Chain off, repeated variations continue from the explicitly selected source tile.
- [ ] Chain is the only feature that intentionally carries source choice across tiles.
- [ ] Variation Range cycles Close, Wide, and Radical and the Amount control changes variation strength.
- [ ] Loop, Duration, Pitch, Envelope, and Spectral locks protect their labeled traits.
- [ ] A full bank refuses Create/Vary without replacing any tile.

## Sample editor and waveform navigation

- [ ] Dragging in either direction creates the expected zero-snapped selection.
- [ ] Play All, Play Selection, Play Displayed, and Play Loop audition the selected tile.
- [ ] Wheel zoom, Shift+wheel pan, keyboard zoom/pan, Zoom Selection, and Show All restore correctly per tile.
- [ ] Crop, Reverse, Normalize, Amplify, Fade, and physical tape gestures each create one Undo step.
- [ ] WARP, SMEAR, and TEAR affect the selection, remain deterministic where specified, and traverse Undo/Redo correctly.
- [ ] Processing controls rerender from the selected tile's private baseline and never from another tile.
- [ ] Space and Escape stop one-shots, loops, held notes, and latched chords.

## Loop and tuning

- [ ] Loop Set, Clear, mode, crossfade, and flag dragging affect only the selected tile and are undoable.
- [ ] Forward, Reverse, and Ping-Pong playback match the displayed direction.
- [ ] Crop and tape gestures keep loop coordinates valid; Undo restores the previous loop exactly.
- [ ] Root note, fine tuning, and pitch suggestion belong to the selected tile and survive tile switching.
- [ ] Held notes respond safely to tuning and render changes without becoming stuck.
- [ ] Space plays the persistent selection when present and otherwise preserves the established whole/playhead behavior, all at the displayed audible tuning.
- [ ] Down, Up, and Trim retune an already-running ordinary audition without changing the waveform hash.
- [ ] Occupied Bank tile, Loop/Loop Lock, Drone, and Transform preview audition use the same audible tuning as Space.
- [ ] Keyboard notes remain correctly transposed relative to the accepted mapping root.

## Native DSP transformations

- [ ] Left-clicking a filled DSP tile transforms only the persistent selection, or the whole tile when there is no selection, in one Undo step.
- [ ] Middle-click opens its native DSP Transform workspace and preserves the exact selection and viewport.
- [ ] Wheel and pointer changes to every displayed macro publish a new temporary waveform preview while source audio and history remain unchanged.
- [ ] Space auditions the temporary preview; Apply commits it once; Back/Escape discards it without mutation.
- [ ] SAVE/UPDATE survives closing the workspace and relaunching TapeSister, and the next left click uses the saved macro positions.
- [ ] Starting several previews quickly publishes only the newest result; closing or switching tiles prevents stale publication.
- [ ] BODY is clearly audible at both extremes, EDGE retains its established detail, and DRIFT reads as bounded organic timing motion.

## Files, export, and compatibility

- [ ] Load, Save, and Export browser navigation, scrolling, filename editing, overwrite confirmation, and cancellation behave consistently.
- [ ] A malformed or truncated input reports its real error and leaves the complete project untouched.
- [ ] TSR16 restores all tile audio, baselines, editor state, histories, loops, tuning, paste/stamp patches, and bank selection.
- [ ] Legacy TSR6 through TSR14 projects still open successfully.
- [ ] Current-tile WAV export and whole-bank export contain the expected audio and sampler loop/tuning metadata.
- [ ] Whole-bank export writes one numbered WAV per occupied tile and never replaces an existing folder.
- [ ] FastTracker handoff exports every occupied tile without mutating the project.

## Final regression pass

- [ ] Run `make test` and the CMake/CTest headless suite.
- [ ] Build and launch the SDL application on a machine with SDL2 and SDL2_ttf development packages.
- [ ] Complete one manual pass that edits at least three tiles, enables and disables Chain, saves, restarts, and reloads.
- [ ] Confirm the interface and documentation contain no Parent/Current, Set Current, Commit, Reset, or A/B workflow controls.
