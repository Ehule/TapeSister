# Native DSP Transform architecture

## Interaction and ownership

The DSP bank contains TapeSister-native C transformations; it does not invoke CDP.
Left click is the performance path: apply a filled tile's saved process to the exact
half-open selection `[first,last)`, or to the whole occupied tile when no selection
exists. The replacement uses the existing owned audio-patch graph, boundary splice,
tile-local state, and one Undo transaction.

Middle click is the design path. It opens the same compact workflow shell used by CDP:
the active tile's authoritative selection and viewport, a miniature editable waveform,
fixed macro positions, immutable preview playback, Apply, and Cancel/Back. DSP and CDP
share this workflow and commit convention, but keep separate processors and job
identities. No preview becomes another tile audio owner.

Native preview jobs snapshot the tile slot, audio hash, frame count, persistent
selection, scope, DSP preset slot, exact process values, job ID, and render generation.
The worker processes an owned selection/whole-tile copy. A result publishes only when
all identity fields still match; tile edits, selection moves, parameter changes,
Undo/Redo, tile switching, workspace close, Capture completion, and newer requests
make it stale. Preview samples are TapeSister-owned memory and do not alter history.
Apply revalidates the identity and commits exactly once. Back or Cancel frees preview
memory and leaves source audio, selection, viewport, metadata, and history untouched.

## Curated macro profiles

`TsDspPresetSpec` describes each factory profile's two to four controls while
`TsPortableRecipe` stores their normalized positions and the exact mapped
`TsProcessRecipe`. Values clamp to `[0,1]`; displayed units are derived from the
control specification. The profiles intentionally expose musical controls rather
than the complete engineering shelf:

- NEUTRAL: BODY, EDGE, DRIFT
- WARM TAPE: BODY, DRIVE, EDGE, MIX
- DARK DRONE: BODY, CUTOFF, RES, DRIFT
- BRIGHT DUST: CUTOFF, DUST, EDGE, DRIFT
- DUB ECHO: TIME, FEEDBACK, TONE, MIX
- HOLLOW SPACE: FOCUS, RES, SPACE, MIX
- HARD CLIP: DRIVE, EDGE, BODY, MIX
- BROKEN FOLD: FOLD, DUST, DRIFT, MIX

BODY is a broad spectral-weight macro. Above center it reinforces the slow/low
component and slightly controls fast detail; below center it removes low mass and
emphasizes the faster component. Center remains bit-neutral, and neither direction is
implemented as uncontrolled gain. EDGE preserves the established high-motion plus
saturating-detail character. DRIFT remains coherent: a deterministic low-rate sine
and seeded random walk move the read position by a bounded number of samples, with a
very small level-dependent noise contribution. It is not a bag of unrelated values.

`SAVE/UPDATE` copies the working process back to the DSP tile. Factory macro positions
are written as optional `DspPreset01` through `DspPreset08` rows in `[DSP Presets]` in
`tapesister.ini`; absent rows retain defaults and invalid/non-finite rows are rejected.
User recipes already serialize the exact underlying process in TSP, so no rendered
audio is stored in a preset.

## Tuned audition

Waveform audio is never resampled merely because ROOT/PITCH changes. TapeSister keeps
an audible tuning and a keyboard-mapping tuning that move equally in opposite
directions when the audible root is changed. `ts_tuning_pair_audition_pitch()` derives
the waveform playback ratio from half their separation; the keyboard continues using
the mapping tuning. Ordinary full, selection, playhead, Bank, Loop/Loop Lock, Drone,
and Transform auditions all use this common calculation. An active audition updates
its step immediately after a tuning edit, while the sample hash remains unchanged.

## Performance and testing

DSP preview rendering runs on the existing single Transform worker path. Repeated
macro changes cancel obsolete work and queue only the newest preview. Only the chosen
selection is copied and processed for selection scope. Immediate left-click application
keeps the established instrument-like one-click behavior and performs one direct
native render/commit.

Automated coverage checks schema/control ranges, parameter clamping, config round trips,
temporary preview ownership, stale identity rejection, selection-only replacement,
outside-audio identity, Apply/Undo/Redo, direct application, BODY extremes, whole and
selection audition ranges, shared tuning ratios, keyboard transposition, and unchanged
sample data across ROOT/PITCH edits.
