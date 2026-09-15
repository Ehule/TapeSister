# Prism performance morph bank — future work

The intended next step is a performance bank of roughly 12 named Prism sound
states that can be adjusted and morphed between throughout a set. This is a
roadmap item, not a feature in the current A/B implementation.

The bank should build on the current separation between auditioned sound,
stored endpoints, and the live sequencer. Editing or browsing a sound must never
silently replace another saved state. Each state needs explicit capture, recall,
replace, and clear operations, with visible unsaved changes.

Before implementation, settle how performers select source/destination states,
whether transitions have per-state or shared times, and how manual fader movement
interrupts a transition. Store the bank with a versioned preset format that can
still read existing A/B presets. Keep interpolation on the audio clock, with
bounded state publication from the UI; preset file I/O stays off the audio thread.

Validate a complete set: load the bank, traverse all states, edit and replace
one state, save/reload, and continue playing with predictable MIDI pickup and
no changes to unrelated Sister routing or effects.
