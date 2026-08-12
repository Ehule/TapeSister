# X220 DSP / Commit revision checklist

This is a musical and interaction checkpoint. Passing tests alone is not approval.

## Parent preservation

- [ ] Drag a recognizable WAV into TapeSister and note its Parent name.
- [ ] Move Body, Edge, and Drift. The imported sound changes, but never returns to the factory waveform.
- [ ] The Parent name remains the imported filename after every processing change.
- [ ] Reseed changes the Current subtly while the imported Parent remains recognizably intact.
- [ ] Generate explicitly replaces it with a clearly different generated Parent.

## Generation

- [ ] Repeated Generate clicks visibly and audibly cycle Tonal, Metallic, Noise, and Pulse Parents.
- [ ] Reseed keeps the displayed generator family but produces a clearly different member.
- [ ] Body, Edge, and Drift materially change each generator family.

## Sample editor

- [ ] Dragging left-to-right and right-to-left selects the expected waveform range.
- [ ] Play All auditions all of Current.
- [ ] Play Selection auditions only the purple/cyan selected range.
- [ ] Zoom Selection fills the display with that range; Play Displayed matches it.
- [ ] Show All restores the full Current display.
- [ ] Crop reduces Current to the selected range while retaining the same Parent.
- [ ] Undo restores the pre-crop Current, selection, and zoom.
- [ ] Redo reapplies the crop.

## FT2 editing and waveform navigation

- [ ] Wheel up/down over the waveform zooms in/out while the sample beneath the pointer stays fixed.
- [ ] Shift+wheel pans the zoomed waveform without changing selection or audio.
- [ ] `=` or `+` / `-` zoom and Left/Right pan; `0` restores Show All.
- [ ] Reverse affects only the selection, or the whole Current when there is no selection.
- [ ] Normalize brings the selected peak close to 0.98 without altering Parent.
- [ ] Amplify Up/Down changes the selected range by 3 dB and can be repeated.
- [ ] Amplify Down after a clipping Amplify Up lowers the level while deliberately preserving the flattened peaks.
- [ ] Fade In reaches silence at the selection start; Fade Out reaches silence at its end.
- [ ] Undo/Redo traverses edits in order and restores exact prior audio.
- [ ] Active Noise/Delay/Space continues to rerender after sample edits.
- [ ] Commit prints all edits and DSP into the next Parent, resets the edit/DSP shelves, and clears history.

## Scrollable file browser

- [ ] Load, Save, and Export open the same compact browser with the correct title and action button.
- [ ] Load lists directories and WAVs only; Save lists directories and `.tsr` files; Export lists directories and WAVs.
- [ ] Mouse wheel scrolls without changing the selected row.
- [ ] The scrollbar thumb size reflects the directory length and can be dragged across the full list.
- [ ] Single-click selects; double-click or Enter opens a directory or loads the selected WAV.
- [ ] Up/Down, Page Up/Down, Home/End, and Backspace navigation remain visible and bounded.
- [ ] Save/Export filename entry appends `.tsr`/`.wav` when omitted without duplicating an existing extension.
- [ ] A blinking caret sits at the end of the visible Save/Export filename only while that field has focus, including for horizontally clipped long names.
- [ ] A first Save/Export action on an existing destination only arms overwrite; the second performs it.
- [ ] Cancel and Escape close the browser without loading, saving, exporting, or leaking a click to the instrument.
- [ ] A malformed WAV reports the real load error and preserves the prior Parent and Current.
- [ ] A failed Save/Export reports the real write error and leaves an existing destination intact.
- [ ] The last visited directory remains active when switching among Load, Save, and Export.
- [ ] Ctrl+Z and Ctrl+Y match the buttons.

## A/B audition and playhead

- [ ] Current is selected by default; Parent and Current buttons plus `Ctrl+B` switch the audition source without changing Current.
- [ ] Play All, Play Selection, Play Displayed, computer keys, and onscreen keys all use the chosen audition source.
- [ ] After Crop, Parent selection/displayed audition maps to the matching uncropped Parent frames.
- [ ] Switching Parent/Current during playback preserves relative progress rather than restarting.
- [ ] The amber Current or green Parent playhead tracks the sounding source and range smoothly.
- [ ] Stop All, Space, Escape, and natural playback completion hide the playhead.
- [ ] Save, Export, edits, Undo, Redo, Reset, and Commit continue to target Current regardless of audition choice.

## Existing foundation

- [ ] Computer and onscreen keys audition the selected A/B source at different pitches.
- [ ] Space, Escape, and Stop All stop playback reliably.
- [ ] A bad WAV path reports an error and preserves Parent and Current.
- [ ] Save creates readable schema-4 JSON with renderer version, lineage, ordered sample edits, all DSP parameters, explicit bypass states, and crop state.
- [ ] Export creates a valid mono 16-bit WAV containing Current.
- [ ] The supplied Tapehead palette is applied consistently and remains legible.
- [ ] No action leaks through the path-entry overlay.
- [ ] Closing the window exits cleanly.

## DSP shelf

- [ ] Noise bypass is exact; enabling it adds an obvious but controllable texture.
- [ ] White, Pink, Brown, and Metallic are audibly distinct as the Color button cycles.
- [ ] Reseed changes deterministic noise detail without replacing imported or committed Parent audio.
- [ ] Delay Time changes echo spacing; Feedback, Damp, and Mix remain stable across their full ranges.
- [ ] Space Decay, Damp, and Mix produce a useful compact ambience without runaway feedback.
- [ ] Noise, Delay, and Space work on both generated sounds and a dragged-in WAV.
- [ ] Undo and Redo restore DSP toggles and values as well as the exact rendered Current.

## Commit lifecycle

- [ ] Reset returns Current exactly to Parent and can be undone/redone.
- [ ] The first Commit click only arms the action; clicking elsewhere cancels it.
- [ ] The second Commit click promotes the heard Current to Parent and increments `GEN`.
- [ ] Immediately after Commit, Current sounds identical to the new Parent and all DSP stages are bypassed.
- [ ] New processing starts from the committed sound rather than the original source.
- [ ] Committing again increments the generation and records the immediately previous Parent as ancestry.

Record the exact commit, compiler, SDL version, and any interaction, appearance, or musical problems before approving the next slice.
