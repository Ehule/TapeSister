# X220 first-slice checklist

This is an interaction checkpoint, not a formality. Stop if the instrument does not feel visibly different from the archived prototype.

- [ ] Debug build and core tests pass.
- [ ] Opening TapeSister immediately shows the waveform-centered instrument.
- [ ] The built-in seeded sound is present on launch and audible from `Z` and `Q`.
- [ ] Onscreen keys audition the same sample at visibly different pitches.
- [ ] `Space`, `Escape`, and **Stop All** stop playback reliably.
- [ ] Dragging a valid WAV onto the window replaces the waveform and auditioned sound.
- [ ] **Load WAV** accepts a valid path; a bad path reports a visible error and preserves the current sound.
- [ ] Body, Edge, and Drift visibly rerender the waveform and materially change the sound.
- [ ] Reseed produces a related but different sound.
- [ ] Save creates a readable deterministic recipe.
- [ ] Export creates a valid mono 16-bit WAV that opens in FT2 and another audio program.
- [ ] No action leaks through the path-entry overlay.
- [ ] Closing the window exits cleanly.

Record the exact commit, compiler, SDL version, and any interaction or appearance problems before approving the next slice.
