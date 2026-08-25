# Sister Machine PR9 manual Windows/Linux checklist

Record OS, audio device/driver, sample rate, buffer size, build, and CPU before
running. Repeat the routing checks with mono and true stereo tiles and, where
available, a MOTU stereo input.

- [ ] All effect MIX controls and Master FX Feedback at zero are indistinguishable from PR8.
- [ ] Hall, Plate, Spring, and Cathedral are materially different.
- [ ] Reverb type changes neither click nor delete the existing tail.
- [ ] Delay TIME sweeps do not click; FEEDBACK reaches bounded self-oscillation.
- [ ] Distortion spans subtle coloration through harsh/noisy sound; TONE remains useful.
- [ ] H1 only leaves H2/H3 unchanged; H1+H3 works.
- [ ] MIX clears heads and selecting any head clears MIX, independently per effect.
- [ ] Distortion H1, Delay H2, and Reverb H3 can run simultaneously.
- [ ] Head effects enter established head feedback; MIX effects do not.
- [ ] H1/H2/H3 and MIX Capture contain the documented taps in mono and stereo.
- [ ] Ordinary tiles, FM, EXT monitor, and AUDITION use MIX effects with POWER off once only.
- [ ] With POWER+ROLL active, TILES off/no armed Sister tiles produces no direct MIDI/QWERTY tile leak; stopping ROLL restores ordinary audition.
- [ ] MIX-target effects retain practical loudness relative to head targets, including several effects near 50% MIX.
- [ ] Reference remains outside post FX and EXT does not duplicate.
- [ ] POWER off preserves ordinary tails but makes Master FX Feedback dormant.
- [ ] POWER on with saved feedback ramps from silence without a stale burst.
- [ ] Master FX Feedback writes post-effect material back causally and remains finite at maximum.
- [ ] Reverb tails, delay repeats, distortion, and Soak/Bleed become useful recursive tape material.
- [ ] TILES/FM/EXT/AUDITION changes neither duplicate nor leak sources.
- [ ] QWERTY and MIDI work from Main, FM Logic, and Sister FX/Tape pages.
- [ ] Preset recall changes types/targets continuously without clearing tape or stopping notes.
- [ ] Rapid wheel sweeps and target changes do not spill into adjacent controls.
- [ ] Hide/reopen preserves the page, effects, tails, notes, and LFO state.
- [ ] Device restart produces a safe ramp/reset with no stale samples or crash.
- [ ] Stereo MOTU input preserves L/R order; explicit mono remains centered and defined.
- [ ] CPU remains practical on X220/X230 and the fanless Windows mini-PC.
- [ ] No click, stuck note, denormal slowdown, stale tail burst, NaN, or crash appears in a long session.
