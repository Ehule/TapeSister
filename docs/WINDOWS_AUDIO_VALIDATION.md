# Windows audio validation

Run this checklist on physical Windows x64 hardware before release sign-off. Automated
tests cover configuration, fallback policy, logical-to-real ID translation, idempotent
removal, capture/output independence, and pre-initialization ordering; they cannot
prove driver behavior, unplug handling, sleep/wake behavior, or ASIO coexistence.

## Preparation

Record the TapeSister commit, Windows version, SDL version, interface/driver version,
and VB-CABLE version. Use the supported MSYS2 UCRT64 build:

```bash
bash build.sh
```

For each run, retain stderr and `tapesister-diagnostic.log`. Confirm that diagnostics
show the configured and active backend, configured and active device names, real SDL
IDs, sample rate, format, channel count, buffer size, fallback status, connection
state, and exact failure/reopen error.

Use 44.1, 48, and 96 kHz where the devices support them. Repeat representative cases
with 256, 512, and 1024 frame buffers and with matching and mismatched application
rates/buffers.

## Matrix

- [ ] TapeSister alone with backend WASAPI.
- [ ] TapeSister alone with backend DirectSound.
- [ ] TapeSister alone with backend Auto.
- [ ] Tapehead and TapeSister both using WASAPI; test both launch orders.
- [ ] Tapehead routed through VB-CABLE into TapeSister.
- [ ] Tapehead → VB-CABLE → TapeSister; test both launch orders.
- [ ] TapeSister while REAPER uses WASAPI shared mode; test both launch orders.
- [ ] TapeSister while REAPER uses ASIO; test both launch orders and record any
  hardware-driver exclusivity rather than describing ASIO as a TapeSister backend.
- [ ] Remove capture during EXT playback, then reconnect it. Internal tiles, FM,
  audition, and Sister sources must continue; stale ring audio must not replay.
- [ ] Remove output during playback, then reconnect it. The UI must remain responsive,
  the configured device must be retried, and no other output may open silently.
- [ ] Start with a named output unavailable. Verify the exact error and all four choices:
  Retry, approved temporary default, continue without output, and Exit. Verify the INI
  still contains the named device after temporary fallback.
- [ ] Start with a named capture unavailable. EXT/record/monitor must be unavailable,
  internal playback must work, and no default capture device may open silently.
- [ ] Restart TapeSister while Tapehead, REAPER, and/or VB-CABLE remain running.
- [ ] Repeat coexistence cases with matching and mismatched sample-rate/buffer settings.
- [ ] Disconnect/reconnect each device repeatedly. Confirm there is no double close,
  duplicate stream, crash, callback stall, or misleading success message.
- [ ] Sleep and wake Windows with both endpoints active, then with capture closed.
  Confirm deterministic state and recovery.

## Pass criteria

- A named device is never replaced without explicit approval.
- Temporary output fallback is visible and does not overwrite configuration.
- Output loss is recoverable and never freezes the interface.
- Capture loss affects only capture-dependent features.
- Reconnection targets the configured device and resets capture ring/resampling state.
- Device events recover even while another window or workspace is active.
- Healthy-device audio, smoothing, pedalboard behavior, Sister processing, recording,
  limiter behavior, and MIDI remain unchanged.

WASAPI shared mode is the recommended coexistence baseline. Native ASIO is not
implemented by TapeSister, and an ASIO driver's exclusive or single-client limitation
cannot be repaired inside TapeSister.

## Prism instrument audition

- [ ] At 256 frames, audition 2, 4, 8, 12, 16, 20 and 24 lenses with a physical mono synth.
- [ ] Feed FM 12-voice Unison into 12 and then 24 Prism lenses. Compare level and bass
  against bypass at Output 0 dB, then use Output while watching the existing LIM.
- [ ] Select all 24 lenses from the numbered strip, including overlapping points
  at full Focus / Stereo zero. Test mute/solo, wheel trim and reset on lens 24;
  reduce to 12, save/reload, and return to 24 to check retained edits. Confirm
  older twelve-lens patches keep their sound.
- [ ] At 24 lenses, run MENISCUS + into MENISCUS − with Color 100 while recording
  and moving controls. Check for device underruns at the normal buffer size.
- [ ] Pull one hollow point wide and keep another narrow. Test pitch/pan, right-click
  reset, Escape during drag, focus contraction, count changes and saved recall.
- [ ] Explore Spread/Drift through 200%, Body through 300%, and Dry Level 0–200%.
- [ ] At Wet 100, confirm Dry Level does not change 01 BODY; wheel-trim or mute
  01 instead. At Wet 50, Dry Level should change the separate labeled DRY lane.
- [ ] Cycle each big glass lens forward/backward through all six silhouettes.
  Try all 36 pairs with Color zero and then raised; check pitch inversion,
  narrower fans, stereo remapping, warm/hollow filtering, phase color and drive.
  Switch shapes during sustained notes; listen for clicks and assess high-frequency
  saturation texture. Save and reopen a mixed-shape patch with hand-edited points.
- [ ] Check crisp curves and solid handle borders at native and enlarged window
  sizes; output inversion should bend rays across the center inside the output glass.
  At Spread 200 / Drift 200 / Focus 60 with PLANO-CONVEX input and MENISCUS −
  output, watch for independent ray movement with no mouse activity, both while
  playing and after stopping. Repeat with tape POWER on/off. Drift zero and
  Focus 100 should settle the motion.
- [ ] Shift-click mute, Ctrl-click solo and wheel-trim each lens, including 01.
  Confirm muted handles remain clickable, trim changes brightness and audible level,
  and right-clicking either the mode button or preset name restores stock lenses.
- [ ] Repeat main → FM → Mosaic → main and main → Mosaic → FM → main. The
  active view's own grave shortcut must return to the canvas. Reopen FM to confirm
  its parked patch remains intact; test unresolved event edits too.
- [ ] Open/close Sister repeatedly with Tab at full-screen size. Check for desktop
  flashes, retained first frames, F11 restoration and placement on another monitor.
  The headless test checks window ownership; Windows compositor behavior needs this audition.
- [ ] Compare bypass and Wet zero with the original; vary count, mode, Spread,
  Focus and Drift during sustained notes and note changes. Listen for clicks,
  unwanted pitch bends, transient smearing and gain changes.
- [ ] Try speech, breath/contact sounds, percussion, field recordings, tape and
  complex polyphony. Compare Supersaw with Ensemble and confirm source identity.
- [ ] Test left-only, right-only and stereo material without channel collapse.
- [ ] Test both tape POWER states, subsequent FX and Live Link from TapeHead.
- [ ] Record using Prism's REC FILE button while playing and switching pages.
  Confirm recorded sound and final output agree.
- [ ] Map On/Off, Mode, Lenses, Spread, Drift, Focus and Wet using the existing
  MIDI learn flow, including Color. Reopen the project and confirm settings and mappings.

[Prism](PRISM.md) records the headless checks and server CPU measurements. Those
measurements do not certify this Windows interface or its hardware latency.

- Hold a sample keyboard note and an FM latch, open Mosaic without pressing Play,
  then play/stop the arrangement: the existing voices should continue. Release
  the held key over Mosaic and confirm it stops normally (unless latched or
  sustained). Switching views must not free a playing FM preview.

## FM Unison and preview recovery

- [ ] Enable Unison, edit pitches/waveforms on voices 7–12, then switch it off.
  Confirm the original routing, voices, filter and modulation return. Repeat after
  saving/reopening an applied Unison tile and with a silent original patch.
- [ ] Toggle all Unison voices off while holding a note. The waveform and audio
  should become silent after UPDATING clears; enable one voice and confirm both
  return without retriggering the latch.
- [ ] Quickly toggle voices and wheel several pitches while a preview is rendering.
  Controls should respond immediately; only the final settings should become the
  new waveform/audio. Check that there is no sequence of outdated renders afterward.
- [ ] Park FM with Shift+grave and reopen the unchanged tile: retain its edits.
  Then select another tile, clear the source tile, or create a replacement in that
  slot and reopen FM: it should load the newly selected sound. An empty destination
  should open a fresh six-voice patch.
- [ ] Close FM or quit while UPDATING is visible. Reopen and check that no old job
  replaces the new workspace. Check UPDATING visibility at native/enlarged sizes.

## Prism performance and amplitude redraw

- [ ] Switch PRISM off/on while its page is visible. Source Zoya forms, packets
  traverse existing rays, and output Zoya reconstructs once, then settles.
  Sound/Perform/Presets must retain the same continuous introduction. Verify
  independent Sister tape POWER does not trigger it.
- [ ] Hide/minimize Sister or leave Prism during the introduction; return to
  settled figures without replay. Enable Prism from MIDI while hidden; opening
  it must not queue an introduction. Repeat while recording audio.
- [ ] Compare Spread/Drift/Focus/Body/Color/Wet changes and A/B morphing with all
  24 lenses. Figures must not cover glass labels, selection handles or hover help.
  Check a stopped audio engine: the settled figure must not keep wandering.
- [ ] Compare visible/hidden Prism at the normal hardware buffer while recording
  a dense scene. Confirm no new audio clicks/dropouts and acceptable display CPU.

- [ ] Save two Prism-only presets, change tape/FX settings, and recall each Prism
  preset. Tape/FX must remain unchanged. Restart and recall the saved bank.
- [ ] Compare New after a heavily edited patch with New after a simple source;
  neither should inherit unlocked settings. Vary should develop the current patch.
  Test all six lock groups and individual locked sliders.
- [ ] Capture different modes, counts, shapes and mute/solo states in A/B. Sweep
  manually and by MIDI, then reverse timed moves mid-flight. Listen for clicks and
  confirm rays/fades follow the sound. Right-click CAP A/B to edit an endpoint.
- [ ] Sequence lenses 03, 08, 05 while others sustain. Lower count to 5 and raise
  it again; order must survive. Remove an excluded member, reset, clear and stop.
  Change Step rate while audio continues through other workspaces.
- [ ] Tune through concave glass using Up/Down and verify Up raises one cent.
  Shift-Up/Down moves one semitone; Ctrl+Shift-Up/Down moves an octave. Check
  Left/Right for 0.01 pan, Shift for 0.10, and Ctrl+Shift for hard left/right
  through all six output shapes. A fine step away from hard pan must respond
  immediately. Check interval snapping, group/individual octaves, and the body lens.
  Confirm the original dry path is unaffected by octave changes.
- [ ] Check gray EMPTY, green READY, blue EDIT after right-click recall, and amber
  CHANGED after changes. Recapture returns READY; morphing shows SOUND LOCKED.
  Sequence edits must not mark an endpoint dirty. Right-click an empty endpoint.
- [ ] Hover buttons, faders, glass and lens numbers on all three pages. Blue help
  must fit and distinguish sequencer RESET/CLEAR from CAP A/B and audio CAPTURE.
  Test the compact Sustain/Rec File buttons, timer, stop and recording across pages.
- [ ] Sweep Drift Rate from 0.005 to 40 Hz with 24 lenses, both colored glass stages
  and MIDI morphing. Audition at the normal hardware buffer for dropouts and CPU.
- [ ] Draw a ramp on a drone, then a flat line. Draw a section to zero and restore
  it. Check untouched regions, cancel, undo/redo, tile copying and TSR31 save/reopen.
