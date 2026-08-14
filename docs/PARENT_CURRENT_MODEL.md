# Source / Current model

The interface calls the immutable audition base **Source**. The C API and TSR11 fields retain their established `Parent` and `family_*` identifiers so existing projects remain byte-compatible; those internal compatibility names are not user-facing concepts.

TapeSister owns one immutable source buffer called **Source** (`Parent` internally) and one rendered buffer called **Current**.

```text
generated recipe or imported WAV
              |
           Parent
              |
 crop + pre-DSP edits + processing + ordered post-DSP tape edits
              |
           Current
```

Source changes only when the user imports a genuinely new source, confirms Commit, or explicitly promotes a collection slot with Set Current (including the Shift+Create shortcut). Body, Edge, Drift, ordinary Create/Vary, selection, zoom, crop, audition, undo, redo, saving, and export do not replace Source.

Startup generation and WAV import install neutral processing before Current is created, making a new Parent and Current sample-for-sample identical. Once a project exists, Generate and Reseed write candidates only into empty family slots. Opening a self-contained TSR project restores the saved embedded Parent and reconstructive edit/DSP state, so its saved A/B difference returns exactly.

The audition selector does not change this ownership model. Current remains the only edit, Save, and Export target; Parent is an alternate read-only playback source. Parent and Current keep independent viewports for zoom, pan, Show All, and displayed-range audition. Selection playback still translates Current-relative frames through `crop_first` when Parent is selected. A live A/B switch maps fractional progress from the active source range to the equivalent position in the other source range.

Keyboard voices are playback state, not rendered sample state. Up to five voices may remain active while Current is rebuilt. The audio device is locked during the offline render, then each voice is remapped onto the replacement buffer and corresponding loop range before callbacks resume. This preserves the musical gesture across parameter changes without weakening Parent immutability.

Root mapping and audible pitch are paired instrument metadata shared by Parent and Current, so A/B never compares two different keyboard mappings. The first onscreen C represents MIDI 48. A source-root assignment establishes both values; subsequent Down/Up/Trim gestures move the audible readout in their literal direction while moving the sampler-unity mapping inversely by the same interval. This is why keyboard playback and exported `smpl` metadata remain correct while the interface behaves like a pitch control. Tuning changes are ordinary undoable metadata edits, and active voices recalculate their step without restarting. Reset and Commit preserve this musical identity. A generated source initializes it from the generator's seeded base pitch; WAV import uses a standard `smpl` chunk when present and otherwise defaults to C3.

A pitch suggestion is temporary UI playback state rather than instrument metadata. While previewing, the Tune readout and note bank use the suggested mapping, including voices already held, but save/export, family capture, and processing continue to own the accepted instrument tuning. Accept converts the preview into one ordinary tuning edit. Escape discards it and stops all playback; Space stops playback while retaining it for another audition.

Selection endpoints and loop boundaries are Current-relative editor metadata. Mouse selection snaps to Current zero crossings even while Parent is displayed. Set Loop without a selection creates the deliberate exact-boundary whole-Current selection; manually drawn loop ranges remain zero-snapped. Parent loop audition adds `crop_first` to reach the corresponding immutable-source frames. Forward, Reverse, and Ping-Pong travel plus loop crossfade are playback state and never rewrite either buffer. Reset clears loop metadata as an undoable edit; Commit preserves it because the heard Current becomes the new Parent with one-to-one frame coordinates.

The sound collection is not part of the Source-to-Current render chain. A new created/imported source copies its initial Source and tuning into immutable bank slot 01. Capturing Current, Selection, or Loop deep-copies rendered audio and the active tuning into slots 02–16. Create adds a seeded Close, Wide, or Radical result to the next empty slot; Vary creates another result with the last range and source. Every created slot records its direct source slot, variation range, seed, amount, active trait locks, chain step, and generator recipe. Chain mode chooses the preceding result as the next source. These metadata describe provenance without restricting kept or unrelated samples.

Renaming a sibling changes only its bank/export name. Auditioning a sibling temporarily places that stable buffer in the waveform display without changing the Current editing target. Each sibling owns independent loop range, mode, crossfade, and tuning metadata. **Set Current** deliberately checks the displayed sibling out as both the new clean Parent and Current so future deterministic renders use that audio rather than snapping back to the former Parent. It preserves the complete bank, transfers stored loop and tuning metadata, advances genealogy, records the previous Parent hash, and clears edit history. Commit likewise changes Parent while preserving the family root and siblings. Starting a genuinely new source starts a new bank.

The processing-recipe bank is separate from both the sample-family bank and the Parent. Its eight factory slots store only `TsProcessRecipe` values and names, so factory treatments never retune a source. User captures may additionally store the active tuning. Applying one replaces processing and, when present, tuning as a single ordinary undoable edit; it does not alter Parent, crop, selection, loop, tape timelines, or sample-family members. TSP2 serializes that optional mapping while remaining free of source audio; legacy TSP1 remains processing-only. Saving or loading one is therefore not a genealogy boundary.

TSR11 is the native self-contained project container. It stores the embedded Parent audio, instrument tuning, Family controls, and every occupied bank slot with its audio, tuning, integrity hash, loop metadata, and lineage, plus every field required to deterministically rebuild Current—including the ordered post-DSP tape timeline, filter, and shaper. Loading validates the complete container before replacing the live instrument. TSR6 through TSR10 remain readable and receive deterministic defaults for fields that did not yet exist; TSR6 receives a root-only bank from its embedded Parent.

Before the first physical tape placement, Reverse, Normalize, gain, and fades are stored as deterministic selection-aware operations between Parent/Crop and the live DSP. A copy or move captures the rendered source into a temporary buffer, then replays its mix/overwrite placement after DSP; this makes overlap safe and keeps undo snapshots compact. Mix measures source and underlying destination peaks, performs an ordinary sum, then scales the complete overlap so its peak equals the louder original region. Extension beyond Current remains unscaled. Later sculpting and Crop join that ordered post-DSP timeline so edits continue to compose in the order the musician heard them. Noise, nonlinear shaping, resonant filtering, Delay, and Space still render only into Current.

Waveform selection, zoom, and panning are view/editor state. They never alter Parent audio. Commit prints the heard Current into the next Parent generation, then clears the edit stack, DSP, and history as a deliberate hard boundary.

Commit and Set Current are deliberate bridges between shaping and genealogy. Commit requires confirmation and deep-copies Current; Set Current checks out a previously captured sibling. Both establish identical Parent/Current audio, record the previous Parent hash as the new generation's immediate ancestor, reset the processing state, and clear old edit history. This prevents Undo from crossing an ancestry boundary accidentally.

Crop is represented as a range into Parent rather than as a destructive rewrite. Undo history therefore stores compact edit state instead of copying large audio buffers. Current is deterministically rerendered from Parent and that state.

Reseed is bank-aware at the interaction layer: it advances the last candidate seed and preserves that candidate's relationship, locks, mutation amount, and non-trajectory anchor. It never changes Parent. Stochastic DSP remains independently seedable through its processing recipe.

Future destructive sample operations must either remain replayable edit operations or explicitly create a new committed Parent. They must never blur the Parent/Current boundary.
