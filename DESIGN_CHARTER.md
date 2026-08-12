# TapeSister Design Charter

**Status:** Approved v0.1  
**Date:** 2026-08-12

## The instrument

TapeSister is a standalone sample-instrument forge for making strange, tactile, musically useful sounds quickly. It combines deterministic sound generation, destructive sample shaping, and direct audition in one compact instrument.

It is not a tracker panel, a general-purpose modular synthesizer, or a reduced copy of FT2. It should feel like a purpose-built instrument: immediate enough to play by ear, deep enough to produce sounds that would be difficult to plan in a conventional sampler, and exact enough that a saved recipe can reproduce a result.

## The experience

The central loop is:

1. Begin with generated sound, an imported sample, or both.
2. See and hear the material immediately.
3. Push it through a small set of consequential transformations.
4. Capture a result as a reproducible recipe and an ordinary sample.
5. Keep exploring without losing the parent sound.

The interface should encourage listening and discovery rather than parameter management. A useful result should never be buried behind setup screens, routing diagrams, or a long session workflow.

## Character

TapeSister should be:

- **Sample-centered.** The waveform and the sound are the center of the instrument.
- **Compact and immediate.** One primary working surface; minimal modal interruption.
- **Tactile.** Keyboard and mouse actions should feel like operating an instrument, not completing a form.
- **Visually restrained.** A fixed-pixel, FT2-informed visual language is welcome, but copied FT2 chrome is not the goal.
- **Deterministic when saved.** Randomness may be lively during exploration, but a seed and recipe must reproduce the chosen result.
- **Productively unstable.** Drift, interaction, feedback, stepped randomness, and mutation should create controlled surprise rather than arbitrary noise.
- **Musically bounded.** Extreme behavior is welcome; NaNs, runaway levels, accidental silence, corrupt files, and irrecoverable edits are not.

## The core object

A TapeSister **recipe** is the reproducible description of a sound: its source material, seed, synthesis and processing choices, interaction structure, and render settings. The rendered sample is an outcome of the recipe, not a replacement for it.

Recipes should eventually be able to produce related children while preserving their parent. Genealogy is a future creative feature, not a requirement for the first instrument slice.

## Raw material, not inherited architecture

The archived prototype and FT2/Tapehead source are salvage shelves.

- Reuse DSP, renderer, waveform, audition, file-format, or UI code only when it serves the new design.
- Import components deliberately, with provenance and focused tests.
- Do not carry over the prototype's screen structure merely because it already exists.
- Do not rebuild the previous generic TapeSister shell and then decorate it with FT2-like controls.
- FT2's sampler and sample editor are references for proven interaction ideas, not an obligation to reproduce the whole tracker interface.

## Hard boundary around Tapehead

TapeSister lives in its own repository, build, tests, releases, and issue history.

- TapeSister work must never modify FT2 Tapehead Edition.
- TapeSister must not depend on Tapehead's tracker-wide globals or runtime state.
- Any future Tapehead handoff must use an explicit file or interchange boundary.
- The current TapeSister prototype remains preserved on `archive/prototype-v1`; it is not the design authority for `main`.

## First visible checkpoint

The first checkpoint is a playable single-sound forge, not infrastructure presented as a product.

When the executable opens, it must visibly present a real sample-working instrument. From that surface the user must be able to:

- load a WAV;
- generate a deterministic sound from a built-in recipe;
- see the resulting waveform;
- audition it immediately from the computer keyboard and onscreen keys;
- change a small, intentionally chosen set of sound-shaping controls and hear a materially different rerender;
- stop all sound reliably;
- save the recipe; and
- export a valid mono 16-bit WAV.

The checkpoint is accepted only after it is compiled and handled on the X220. A screenshot or framebuffer capture, deterministic render tests, valid non-silent bounded output, and a short manual interaction checklist are part of the checkpoint—not afterthoughts.

## Explicitly later

The first checkpoint does not include:

- tracker or Tapehead integration;
- XM or XI export;
- external MIDI control;
- multiple simultaneous instruments;
- loops or advanced sample mapping;
- recipe genealogy;
- full interaction matrices, hidden modulators, FM/feedback networks, or moving-ratio systems;
- an elaborate session browser;
- broad UI extraction from FT2; or
- preserving every prototype feature.

These may follow only after the core instrument is visibly and audibly convincing.

## Development agreement

Each development slice must create a user-visible or audible capability that can be tested in the running program. Architecture and tests support that capability; they do not substitute for it.

Creative and interaction decisions are made collaboratively before implementation. Broad prompts such as “extract the FT2 sampler” are not implementation specifications. Automated coding work should be reserved for bounded tasks whose behavior, visual result, and acceptance tests have already been defined.

No phase is complete merely because it compiles or passes isolated tests. If the intended change cannot be recognized and used in the actual application, it is not complete.

## Success test

TapeSister succeeds when it becomes faster and more inviting to make a distinctive playable sample with it than to assemble the same process in a tracker, DAW, or modular patch—and when the result can still be recalled exactly, exported normally, and carried elsewhere.
