# Parent / Current model

TapeSister owns one immutable source buffer called **Parent** and one rendered buffer called **Current**.

```text
generated recipe or imported WAV
              |
           Parent
              |
    crop range plus ordered sample edits plus processing
              |
           Current
```

Parent changes only when the user explicitly imports, generates, reseeds a generated Parent, or confirms Commit. Body, Edge, Drift, selection, zoom, crop, audition, undo, redo, saving, and export do not replace Parent.

The audition selector does not change this ownership model. Current remains the only edit, Save, and Export target; Parent is an alternate read-only playback source. Selection and displayed-range playback translate Current-relative frames through `crop_first` when Parent is selected. A live A/B switch maps fractional progress from the active source range to the equivalent position in the other source range.

Reverse, Normalize, gain, and fades are stored as deterministic, selection-aware operations between Parent/Crop and the live DSP. Noise, Delay, and Space also render only into Current. Reset is an undoable edit that clears the crop, ordered sample-edit stack, and DSP recipe so Current becomes sample-for-sample identical to Parent.

Waveform selection, zoom, and panning are view/editor state. They never alter Parent audio. Commit prints the heard Current into the next Parent generation, then clears the edit stack, DSP, and history as a deliberate hard boundary.

Commit is the deliberate bridge between shaping and genealogy. It requires confirmation in the interface, deep-copies Current into a new Parent, records the previous Parent hash as the new generation's immediate ancestor, resets Current to that Parent, and clears the old edit history. This prevents Undo from crossing an ancestry boundary accidentally.

Crop is represented as a range into Parent rather than as a destructive rewrite. Undo history therefore stores compact edit state instead of copying large audio buffers. Current is deterministically rerendered from Parent and that state.

For a generated Parent, Reseed changes the generator seed while retaining its generator family. For an imported or committed Parent, Reseed changes the processing seed and rerenders Current while the Parent hash remains identical.

Future destructive sample operations must either remain replayable edit operations or explicitly create a new committed Parent. They must never blur the Parent/Current boundary.
