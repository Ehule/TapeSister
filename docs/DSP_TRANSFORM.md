# Native DSP Transform architecture

## Two curated pages, one DSP mode

DSP remains one top-level TapeSister mode beside Sample Tiles, Keyboard, and CDP.
Its internal pages are **DSP 1 PROCESS** and **DSP 2 PRIMITIVES**, with 16 fixed
starting points on each page. Pressing `4` enters DSP and restores the last-used
page; pressing `4` again while DSP is active toggles the page. The visible page
buttons do the same without changing the active tile, selection, or viewport.

`TsDspRecipe` is the compiled, versioned registry for all 32 entries. Each recipe
has a stable ID, bank/slot address, category, description, and exactly four musical
controls. `TsDspRecipeValues` stores normalized control positions, a stable render
seed, and the render-time tuning value. Factory definitions are trusted code; no tile
can inject function names, shell commands, or raw processing parameters.

DSP 1 contains SPACE, CAVE, ROOM, ECHO, TAPE, DUB, COMB, RESONATE, LOW, HIGH,
BAND, NOTCH, CHORUS, FLANGE, DRIVE, and CRUSH. The implementations deliberately
reuse the existing native filter, shaper, delay, reverb, BODY, and DRIFT stages where
their graph is a good fit. Focused renderers provide feedback combing, a moving biquad
notch, chorus/flange modulation delay, and sample/bit-rate reduction. Extreme values
remain bounded and finite without reducing the processors to polite studio ranges.

DSP 2 contains SINE, SHAPE, PULSE, SUB, METAL, CHIME, DRONE, BEAT, RUMBLE, HISS,
DUST, KNOCK, PING, FM, AM, and CHAOS. They are configurations of a compact offline
toolkit—oscillators, deterministic noise/impulses, modulation, envelopes, and
resonant material—not 16 real-time synthesizers. Every primitive renders exactly the
requested selection or whole-tile frame count into ordinary TapeSister-owned waveform
memory. The fourth macro is `SOURCE`: zero is generated replacement, 100% is bit-exact
dry source, and intermediate positions mix the aligned equal-length signals. Generated
material receives short edge fades and DC correction before source mixing.

## Interaction, ownership, and graph order

Left click is the performance path: apply a tile's saved values to the exact half-open
selection `[first,last)`, or to the whole occupied tile when no selection exists.
Middle click is the design path. It opens the same compact shell used by CDP: the
active tile's authoritative selection and viewport, miniature editable waveform, four
fixed controls, immutable preview playback, Apply, Save/Update, and Cancel/Back. DSP
and CDP share this workflow and commit convention but keep separate processors and job
identities. No preview becomes another tile audio owner.

Native jobs snapshot the tile slot, audio hash, frame count, persistent selection,
scope, DSP recipe and values, job ID, and render generation. A result publishes only
when all identity fields still match; tile edits, selection moves, parameter changes,
Undo/Redo, tile switching, workspace close, Capture completion, and newer requests
make it stale. Preview samples are owned memory and do not alter history. Apply
revalidates once and commits one tile-local Undo transaction. Back or Cancel frees the
preview and leaves source audio, selection, viewport, metadata, and history untouched.

Accepted audio uses the PR31 material-checkpoint graph position. It becomes editable
source material before the live native processing shelf, so BODY/EDGE/DRIFT and
Noise/Shape/Delay/Space continue to affect it. Parameter movement reconstructs from a
stable basis rather than accumulating processing, and the native shelf is not applied
twice when a rendered transform is accepted.

## Curated macros and persistent tile settings

Control positions clamp to `[0,1]`, then map linearly or logarithmically into honest
units such as Hertz, milliseconds, seconds, drive ratio, or bit depth. Labels and
mappings change with the recipe while the four screen positions stay fixed. A macro
may deliberately coordinate several existing DSP values without exposing a plug-in
style engineering panel.

`SAVE/UPDATE` copies the four working positions back to that stable DSP tile. Values
are written as optional `DspPreset01` through `DspPreset32` rows in `[DSP Presets]` in
`tapesister.ini`; absent rows retain defaults and invalid/non-finite rows are rejected.
Rows 01–16 address DSP1 and rows 17–32 address DSP2. Presets never own audio. Existing
TSP files remain loadable and immediately apply their stored legacy native processing
recipe, preserving portable-file compatibility independently of the fixed bank tiles.

## Tuned audition

Waveform audio is never resampled merely because ROOT/PITCH changes. TapeSister keeps
an audible tuning and a keyboard-mapping tuning that move equally in opposite
directions when the audible root is changed. `ts_tuning_pair_audition_pitch()` derives
the waveform playback ratio from half their separation; the keyboard continues using
the mapping tuning. Ordinary full, selection, playhead, Bank, Loop/Loop Lock, Drone,
and Transform auditions all use this common calculation. An active audition updates
its step immediately after a tuning edit, while the sample hash remains unchanged.

## Performance and testing

DSP preview and quick Apply run on the existing single Transform worker. Repeated
macro changes cancel obsolete work and queue only the newest preview. Selection scope
copies and renders only the selected material. The SDL/audio threads continue using
the unchanged committed sample until a validated Apply.

Automated coverage checks all 32 registry entries, default and extreme renders,
finite/bounded/non-silent output, SOURCE/REPLACE/MIX endpoints, predictable oscillator
pitch movement, config round trips across both pages, temporary preview ownership,
stale identity rejection, selection-only replacement, outside-audio identity,
Apply/Undo/Redo, BODY extremes, panel navigation, tuned audition, keyboard
transposition, and unchanged sample data across ROOT/PITCH edits.
