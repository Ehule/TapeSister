# Sister Machine PR10 Windows/Linux manual checklist

Record OS, device/driver, sample rate, callback size, build, and CPU before testing.

1. [ ] Start at the existing 40-second default; sound is unchanged before BUFFER is touched.
2. [ ] Grow slowly from 5 to 60 seconds while rolling.
3. [ ] Shrink slowly from 60 to 5 seconds while rolling.
4. [ ] Rapidly sweep between minimum and maximum; the newest request wins without backlog.
5. [ ] Existing material remains at the same audible age when it survives.
6. [ ] Newly grown space begins blank.
7. [ ] Shrunk/discarded audio does not return after regrowth.
8. [ ] H1 inside the retained region does not jump.
9. [ ] H2 outside the crop boundary transitions safely to the oldest retained boundary.
10. [ ] H3 span clamps safely on a shorter canvas.
11. [ ] Forward and reverse heads remain valid and preserve rate/direction.
12. [ ] Playback does not stop during resize.
13. [ ] No POWER cycle occurs during resize.
14. [ ] ROLL-stopped state remains stopped while head playback continues.
15. [ ] HOLD remains held.
16. [ ] CLEAR during or near resize follows its established fade and behaves deterministically.
17. [ ] Resize while EXT writes stereo; L/R order remains correct.
18. [ ] Resize while EXT writes mono; the signal remains centered and defined.
19. [ ] Resize while Soma Terra performs over MIDI; no stuck or interrupted notes occur.
20. [ ] Resize while QWERTY performs.
21. [ ] Resize while a multi-tile ensemble plays.
22. [ ] Resize while FM plays.
23. [ ] Resize during MIX Capture without a timing gap or channel swap.
24. [ ] Resize during H1/H2/H3 Capture without changing tap placement.
25. [ ] Resize during Overdub without changing destination occupancy or stale-base behavior.
26. [ ] Soak/Bleed continues without a phase or history reset.
27. [ ] Reverb tails continue.
28. [ ] Delay repeats and feedback continue.
29. [ ] Distortion remains continuous.
30. [ ] Master FX Feedback remains causal, active, and bounded.
31. [ ] Extreme established head feedback does not double during transition.
32. [ ] Waveform, write marker, and H1/H2/H3 markers resize and remain legal.
33. [ ] Preset recall performs a live resize without clearing tape, notes, or effects.
34. [ ] Project reload restores duration but not live buffer audio.
35. [ ] Window hide/show does not interrupt a resize or lose its target.
36. [ ] Device/sample-rate switching follows the established safe restart-clear policy and leaves no stale pointers.
37. [ ] Memory and CPU remain practical at 44.1/48/96 kHz on X220/X230-class systems and the fanless Windows mini-PC.
38. [ ] No new clicks, callback overruns, denormal slowdowns, crashes, stale frames, stuck notes, invalid heads, or callback stalls occur.
39. [ ] With Sister focused, F1-F8 change the QWERTY octave exactly as in the main/FM windows.
40. [ ] Balance TILES, FM, EXT, and AUDITION with the compact source trims; each control affects only its named source.
41. [ ] INPUT remains the master write level after the four source trims.
42. [ ] FX RET controls the complete post-effect branch with Sister both on and off, and zero does not destroy tails.
43. [ ] Compare sustained PR10 playback before/after the bounded ring-wrap fix at the same device buffer; verify reduced crackle/overruns without a sound change.
