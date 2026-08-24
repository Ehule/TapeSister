# Sister Machine PR8 manual Windows/Linux checklist

Use unmistakably asymmetric stereo material and repeat the same checks at 44.1, 48
and 96 kHz where the device supports them.

## Identity and channel movement

- [ ] With SOAK at zero, pre-PR8 and PR8 audio are indistinguishable.
- [ ] A left-only stereo source stays left at zero; a right-only source stays right.
- [ ] Raising SOAK sends delayed left material into right and delayed right into left.
- [ ] BLEED ranges from glacial drift to clearly audible weaving.
- [ ] The motion is neither simple autopan nor a static collapse toward mono.
- [ ] High SOAK approaches a moving delayed swap without permanent center collapse.
- [ ] Mono tiles remain exact centered dual mono for every target and setting.
- [ ] Stereo-to-mono Capture remains useful, bounded and explicitly 0.5 * (L + R),
      including opposite-polarity/cancellation material.

## Targets, feedback and Capture

- [ ] MIX treats the three-head wet sum as one stereo object.
- [ ] H1 alone leaves H2 and H3 discrete.
- [ ] H1+H3 processes both simultaneously with visibly/audibly staggered motion.
- [ ] Selecting MIX clears H1/H2/H3; selecting a head clears MIX.
- [ ] Turning the last active target off is a safe bypass.
- [ ] Head-target weaving accumulates through that head's established feedback.
- [ ] MIX-target weaving creates no hidden return to rolling tape.
- [ ] H1/H2/H3 Capture includes only the selected head insertion result.
- [ ] MIX Capture includes the post-filter, post-OUT, linked-safe weave.
- [ ] Capture M folds explicitly; Capture S preserves MOTU L/R channel order.
- [ ] Protected destinations, source conflicts, anti-recursion and non-auto-arming
      behavior remain unchanged.

## State, interaction and stability

- [ ] Preset recall changes SOAK/BLEED/targets without a click, tape clear, phase
      restart, playback restart or stopped note.
- [ ] Project reload restores settings without restoring live delay or tape memory.
- [ ] A legacy project opens with SOAK zero, BLEED 25 percent and MIX selected.
- [ ] Rapid SOAK/BLEED wheel sweeps do not spill into an adjacent control.
- [ ] Main, FM Logic and Sister windows remain mutually interactive.
- [ ] MIDI, QWERTY, direct tile ensembles, FM, EXT and AUDITION still work.
- [ ] Hide/show leaves phase and routing intact; ROLL off and HOLD do not freeze weave.
- [ ] No click occurs at rolling-buffer wrap, target changes, rapid sweeps, preset
      recall, device restart or hide/show.
- [ ] POWER cycling and output-device/sample-rate changes leave no stale delayed sound.
- [ ] Extreme SOAK plus H1/H2 feedback, ERASE and Ghost Tone stays finite.
- [ ] CPU remains practical on X220/X230 Linux and the fanless Windows mini-PC at
      44.1/48 kHz, with a separate 96 kHz observation if available.
