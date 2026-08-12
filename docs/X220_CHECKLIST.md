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
- [ ] Load lists directories, WAVs, and `.tsr` projects; Save lists directories and `.tsr` files; Export lists directories and WAVs.
- [ ] Mouse wheel scrolls without changing the selected row.
- [ ] The scrollbar thumb size reflects the directory length and can be dragged across the full list.
- [ ] Single-click selects; double-click or Enter opens a directory or loads the selected WAV/TSR.
- [ ] Up/Down, Page Up/Down, Home/End, and Backspace navigation remain visible and bounded.
- [ ] Save/Export filename entry appends `.tsr`/`.wav` when omitted without duplicating an existing extension.
- [ ] A blinking caret sits at the end of the visible Save/Export filename only while that field has focus, including for horizontally clipped long names.
- [ ] A first Save/Export action on an existing destination only arms overwrite; the second performs it.
- [ ] Cancel and Escape close the browser without loading, saving, exporting, or leaking a click to the instrument.
- [ ] A malformed WAV reports the real load error and preserves the prior Parent and Current.
- [ ] A malformed, truncated, or legacy non-self-contained TSR reports the real error and preserves the complete prior instrument.
- [ ] Browser, drag-and-drop, and command-line opening all accept both WAV and TSR files.
- [ ] Saving then reopening a TSR restores exact Parent/Current hashes, loop, crossfade, crop, sample edits, DSP, view, selection, and lineage.
- [ ] A failed Save/Export reports the real write error and leaves an existing destination intact.
- [ ] The last visited directory remains active when switching among Load, Save, and Export.
- [ ] Ctrl+Z and Ctrl+Y match the buttons.

## A/B audition and playhead

- [ ] Current is selected by default; Parent and Current buttons plus `Ctrl+B` switch the audition source without changing Current.
- [ ] The waveform changes immediately with the A/B selector and each source restores its own independent zoom/pan view.
- [ ] Wheel, Shift+wheel, `+/-`, arrows, Zoom Selection, Show All, and Play View all operate on Parent while Parent is displayed.
- [ ] Play All, Play Selection, Play Displayed, computer keys, and onscreen keys all use the chosen audition source.
- [ ] After Crop, Parent selection/displayed audition maps to the matching uncropped Parent frames.
- [ ] Switching Parent/Current during playback preserves relative progress rather than restarting.
- [ ] The amber Current or green Parent playhead tracks the sounding source and range smoothly.
- [ ] Stop All, Space, Escape, and natural playback completion hide the playhead.
- [ ] Save, Export, edits, Undo, Redo, Reset, and Commit continue to target Current regardless of audition choice.

## Zero-crossing selection and forward loops

- [ ] Slow waveform drags show both highlight endpoints jumping live to nearby zero crossings in Current.
- [ ] Magenta zero-crossing pixels remain visible across selection and loop regions and the blue loop flags land directly on them.
- [ ] Left-to-right and right-to-left drags produce the same snapped range; very short drags never invent an unsnapped endpoint.
- [ ] While Parent is displayed, dragging maps through Current's crop and the resulting highlight matches after returning to Current.
- [ ] On a waveform with no sign crossing, selection falls back to its quietest sample without hanging or leaving the valid range.
- [ ] `Ctrl+A` still selects exact frame 0 through the Current frame count.
- [ ] Reverse, Normalize, Amplify, fades, Crop, and Set Loop all use the visibly snapped highlight.
- [ ] Set Loop creates one blue forward-loop region with unmistakable start/end markers; Clear removes it.
- [ ] Each blue flag drags smoothly, remains zero-snapped, updates a playing loop live, and swaps start/end role when crossed.
- [ ] Releasing a dragged loop flag does not release a note held from the computer keyboard or onscreen keyboard.
- [ ] Play Loop repeats until Stop All, Space, or Escape; no note or loop remains stuck.
- [ ] Computer and onscreen notes provide up to five independent voices, sustain the loop while held, and stop individually on release; without a loop they retain one-shot behavior.
- [ ] Shift-click builds a latched onscreen chord up to five notes, toggles an active chord note off, and refuses a sixth without stealing a voice.
- [ ] An ordinary onscreen-key click clears the latched chord and returns to momentary audition.
- [ ] Held and latched notes survive Body/Edge/Drift and DSP rerenders, continuing at the corresponding loop position after the render pause.
- [ ] The 0–50 ms crossfade is clearly audible on difficult joins and remains stable at both limits and on short loops.
- [ ] Switching Parent/Current during loop playback maps the region and relative playhead position without restarting.
- [ ] Crop keeps the overlapping loop portion in correct Current-relative coordinates; Undo restores the prior crop and loop exactly.
- [ ] Loop Set, Clear, and crossfade changes traverse correctly through Undo/Redo.
- [ ] Reset clears the loop and is undoable; Commit carries the loop onto the promoted Parent and clears old history.
- [ ] Saved TSR6 projects contain embedded Parent audio, loop state/range/crossfade, and complete reconstructive state.
- [ ] KEYS hides/shows the onscreen keyboard without affecting computer-key audition or Stop All.

## Existing foundation

- [ ] Computer and onscreen keys audition the selected A/B source at different pitches.
- [ ] Space, Escape, and Stop All stop playback reliably.
- [ ] A bad WAV path reports an error and preserves Parent and Current.
- [ ] Save creates a self-contained TSR6 project that reopens without access to the original WAV.
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
