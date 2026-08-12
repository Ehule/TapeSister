# X220 Parent / Current revision checklist

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
- [ ] Ctrl+Z and Ctrl+Y match the buttons.

## Existing foundation

- [ ] Computer and onscreen keys audition Current at different pitches.
- [ ] Space, Escape, and Stop All stop playback reliably.
- [ ] A bad WAV path reports an error and preserves Parent and Current.
- [ ] Save creates readable schema-2 JSON with source, generator, processing, and crop state.
- [ ] Export creates a valid mono 16-bit WAV containing Current.
- [ ] The supplied Tapehead palette is applied consistently and remains legible.
- [ ] No action leaks through the path-entry overlay.
- [ ] Closing the window exits cleanly.

Record the exact commit, compiler, SDL version, and any interaction, appearance, or musical problems before approving the next DSP slice.
