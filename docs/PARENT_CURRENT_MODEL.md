# Parent / Current model

TapeSister owns one immutable source buffer called **Parent** and one rendered buffer called **Current**.

```text
generated recipe or imported WAV
              |
           Parent
              |
 crop + pre-DSP edits + processing + ordered post-DSP tape edits
              |
           Current
```

Parent changes only when the user explicitly imports, generates, reseeds a generated Parent, or confirms Commit. Body, Edge, Drift, selection, zoom, crop, audition, undo, redo, saving, and export do not replace Parent.

Generate and WAV import always install neutral processing before Current is created, making a new Parent and Current sample-for-sample identical. Opening a self-contained TSR project is intentionally different: it restores the saved embedded Parent and reconstructive edit/DSP state, so its saved A/B difference returns exactly.

The audition selector does not change this ownership model. Current remains the only edit, Save, and Export target; Parent is an alternate read-only playback source. Parent and Current keep independent viewports for zoom, pan, Show All, and displayed-range audition. Selection playback still translates Current-relative frames through `crop_first` when Parent is selected. A live A/B switch maps fractional progress from the active source range to the equivalent position in the other source range.

Keyboard voices are playback state, not rendered sample state. Up to five voices may remain active while Current is rebuilt. The audio device is locked during the offline render, then each voice is remapped onto the replacement buffer and corresponding loop range before callbacks resume. This preserves the musical gesture across parameter changes without weakening Parent immutability.

Selection endpoints and loop boundaries are Current-relative editor metadata. Mouse selection snaps to Current zero crossings even while Parent is displayed. Parent loop audition adds `crop_first` to reach the corresponding immutable-source frames. Forward, Reverse, and Ping-Pong travel plus loop crossfade are playback state and never rewrite either buffer. Reset clears loop metadata as an undoable edit; Commit preserves it because the heard Current becomes the new Parent with one-to-one frame coordinates.

The sample-family bank is a sibling collection, not part of the Parent-to-Current render chain. A new generated/imported source copies its initial Parent into immutable bank slot 1. Capturing Current, Selection, or Loop deep-copies rendered audio into slots 2–16. Renaming a sibling changes only its bank/export name. Auditioning a sibling temporarily places that stable buffer in the waveform display without changing the Current editing target. **Set Current** deliberately checks the displayed sibling out as both the new clean Parent and Current so future deterministic renders use that audio rather than snapping back to the former Parent. It preserves the entire bank, transfers stored loop metadata, advances genealogy, records the previous Parent hash, and clears edit history. Commit likewise changes Parent while preserving the family root and captured siblings. Starting a genuinely new source starts a new bank.

The processing-recipe bank is separate from both the sample-family bank and the Parent. Its eight factory and eight user slots store only `TsProcessRecipe` values and names. Applying a recipe replaces the processing shelf and rerenders Current as one ordinary undoable edit; it does not alter Parent, crop, selection, loop, tape timelines, or sample-family members. A TSP1 file serializes exactly that source-independent processing state. Saving or loading one is therefore not a genealogy boundary.

TSR9 is the native self-contained project container. It stores the embedded Parent audio, every occupied bank slot with integrity hashes and loop metadata, plus every field required to deterministically rebuild Current—including the ordered post-DSP tape timeline, filter, and shaper. Loading validates the complete container before replacing the live instrument. TSR6 through TSR8 remain readable and receive deterministic defaults for fields that did not yet exist; TSR6 receives a root-only bank from its embedded Parent.

Before the first physical tape placement, Reverse, Normalize, gain, and fades are stored as deterministic selection-aware operations between Parent/Crop and the live DSP. A copy or move captures the rendered source into a temporary buffer, then replays its mix/overwrite placement after DSP; this makes overlap safe and keeps undo snapshots compact. Later sculpting and Crop join that ordered post-DSP timeline so edits continue to compose in the order the musician heard them. Noise, nonlinear shaping, resonant filtering, Delay, and Space still render only into Current. Reset is an undoable edit that clears crop, both edit timelines, and the DSP recipe so Current becomes sample-for-sample identical to Parent.

Waveform selection, zoom, and panning are view/editor state. They never alter Parent audio. Commit prints the heard Current into the next Parent generation, then clears the edit stack, DSP, and history as a deliberate hard boundary.

Commit and Set Current are deliberate bridges between shaping and genealogy. Commit requires confirmation and deep-copies Current; Set Current checks out a previously captured sibling. Both establish identical Parent/Current audio, record the previous Parent hash as the new generation's immediate ancestor, reset the processing state, and clear old edit history. This prevents Undo from crossing an ancestry boundary accidentally.

Crop is represented as a range into Parent rather than as a destructive rewrite. Undo history therefore stores compact edit state instead of copying large audio buffers. Current is deterministically rerendered from Parent and that state.

For a generated Parent, Reseed changes the generator seed while retaining its generator family. For an imported or committed Parent, Reseed changes the processing seed and rerenders Current while the Parent hash remains identical.

Future destructive sample operations must either remain replayable edit operations or explicitly create a new committed Parent. They must never blur the Parent/Current boundary.
