# TapeSister Design Charter

**Status:** Approved v0.1, amended for embedded SisterTracker on 2026-10-03
**Date:** 2026-08-12

## The instrument

TapeSister is a standalone sample-instrument forge for making strange, tactile, musically useful sounds quickly. It combines deterministic sound generation, destructive sample shaping, and direct audition in one compact instrument.

The canvas remains the primary sound-making surface. F10 opens SisterTracker, an embedded TapeHead workspace that sequences the canvas tiles and sends its output through the existing router. The user approved transplanting the original tracker application rather than recreating its commands individually. The instrument should remain immediate to play and exact enough to recall a saved result.

## The experience

The central loop is:

1. Begin with generated sound, an imported sample, or both.
2. See and hear the material immediately.
3. Push it through a small set of consequential transformations.
4. Keep the result as an independently editable Bank tile.
5. Continue exploring without changing unrelated tiles.

The interface should encourage listening and discovery rather than parameter management. A useful result should never be buried behind setup screens, routing diagrams, or a long session workflow.

## Character

TapeSister should be:

- **Sample-centered.** The waveform and the sound are the center of the instrument.
- **Compact and immediate.** One primary working surface; minimal modal interruption.
- **Tactile.** Keyboard and mouse actions should feel like operating an instrument, not completing a form.
- **Visually restrained.** A fixed-pixel, FT2-informed visual language is welcome, but copied FT2 chrome is not the goal.
- **Deterministic when saved.** Randomness may be lively during exploration, but a seed and recipe must reproduce the chosen result.
- **Performance-preserving.** Generated and edited states may stay ephemeral, but a completed realtime performance is precious and keeps an immutable original.
- **Productively unstable.** Drift, interaction, feedback, stepped randomness, and mutation should create controlled surprise rather than arbitrary noise.
- **Musically bounded.** Extreme behavior is welcome; NaNs, runaway levels, accidental silence, corrupt files, and irrecoverable edits are not.

## The core object

A TapeSister **Bank tile** is the sound object. It owns its audio, generator or import provenance, tuning, loop, selection, viewport, processing and edit state, and Undo/Redo history. Sample pages retain the compact 16-tile physical layout while allowing the library to grow; all tiles are peers, with no privileged Source slot or separate Parent/Current promotion workflow.

Create and Load operate on the selected tile. Clone creates an independent copy. Editing and transforms affect only the selected tile. Vary with Chain enabled is the one intentional cross-tile continuation mechanism.

A processing Recipe is a reusable procedure applied to material. It is not the identity or owner of a tile.

## The performance boundary

Anything TapeSister records from realtime activity—external input or an internal CAPTURE performance—gets a human-readable immutable original in `Captures/`. Working tiles remain freely destructive, and ordinary edits, renders, previews, generators, and history states do not create archive files. The REC BANK is a reusable capture buffer; KEEP explicitly graduates its current tiles into available Sample-page slots without overwriting existing sounds.

## Approved embedded tracker boundary

TapeSister lives in its own repository, build, tests, releases and issue history.

- TapeSister work must never modify FT2 TapeHead Edition's repository.
- Vendored TapeHead application code may run as an isolated subsystem inside
  TapeSister. Keep its pinned source, licenses and reviewable local patch.
- TapeSister owns SDL/window lifetime, the audio and MIDI devices, project files,
  stable tile identities and the routing/recording chain.
- SisterTracker reuses original editing and transport globals behind an explicit
  host adapter. Standalone disk/sample/configuration workflows are replaced by
  host tile, canvas and audio actions.
- Tracker sample slots are bindings to host tiles, not a second sample library.
  Sounding generations remain immutable; missing identities never resolve by a
  reused slot number.
- The archived prototype remains preserved on `archive/prototype-v1`.

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

The original first checkpoint did not include the following. Later work has
added several of them, including the explicitly approved SisterTracker transplant:

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

These are historical checkpoint boundaries, not prohibitions on subsequently approved work.

## Development agreement

Each development slice must create a user-visible or audible capability that can be tested in the running program. Architecture and tests support that capability; they do not substitute for it.

Creative and interaction decisions are made collaboratively before implementation. Broad prompts such as “extract the FT2 sampler” are not implementation specifications. Automated coding work should be reserved for bounded tasks whose behavior, visual result, and acceptance tests have already been defined.

No phase is complete merely because it compiles or passes isolated tests. If the intended change cannot be recognized and used in the actual application, it is not complete.

## Success test

TapeSister succeeds when it becomes faster and more inviting to make a distinctive playable sample with it than to assemble the same process in a tracker, DAW, or modular patch—and when the result can still be recalled exactly, exported normally, and carried elsewhere.
