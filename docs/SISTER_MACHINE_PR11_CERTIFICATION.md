# Sister Machine PR11 certification and hardware checklist

## Automated certification record

The integration baseline at `ba55055` passed all 60 Make test executables with
`make -j9 test` and the default strict C11 flags (1m43.038s). Its CMake list declared
59 because the existing post-FX executable was accidentally omitted from CTest.
PR11 adds the realtime diagnostic and pathological harness tests and registers the
post-FX test in CMake, bringing both build systems to 62. The long harness is invoked
explicitly:

```bash
make stress-sister
make benchmark-sister
./tapesister --diagnostic-audio
```

The certification soak completed 107,200 deterministic state transitions and
7,600,000 rendered frames, representing 7,600 seconds (2 h 6 min 40 s) at the
harness rate. It checked every output frame for finite values and verified buffer
pointers, logical bounds, head phase/range, resize state, note release, and fixed
storage identity.

| Seed | Transitions | Frames | Checksum |
|---:|---:|---:|---:|
| `0x48324831` | 35,734 | 2,533,336 | -19632.008183193 |
| `0xc11c5afe` | 35,733 | 2,533,332 | 1548.171499309 |
| `0x6d2b79f5` | 35,733 | 2,533,332 | -10113.054455148 |

The run made 6,710 resize requests and ended with no crash, hang, stuck voice,
non-finite sample, out-of-range head, pointer replacement, or escaped feedback.
Use the printed seed and checksum when comparing another platform.

## Build, sanitizer, and performance record

- Clean Release: `make -j9 test` passed all 62 test executables with GCC 13.3.0 and
  `-std=c11 -O2 -Wall -Wextra -Wpedantic` (1m42.772s wall time).
- Focused Debug: pathological, routes, heads, resize, and post-FX tests passed with
  `-O0 -g` and the same warnings.
- ASan+UBSan: pathological, routes, runtime, heads, resize, and post-FX tests passed
  with `-fsanitize=address,undefined`; leak detection was disabled because LeakSanitizer
  cannot operate under this host's ptrace wrapper.
- TSan: the atomic snapshot test passed. This is useful but limited coverage, not a
  substitute for a native concurrent SDL session.
- `git diff --check` passed.
- CMake, CTest, SDL application compilation/smoke, real audio devices, Windows, and
  native 30–60 minute realtime soak were unavailable on this host because CMake and
  SDL2 development tools/devices were absent.

Equivalent 48 kHz, 256-frame, 999,936-frame, fully active three-head/Soak/all-FX/
feedback benchmarks used GCC `-O2`, the same source signal and configuration
`0x1ffd`, and preserved checksum `-1009.596012716176`. Five-run median cost was
2606.084 ns/frame at `ba55055` and 2591.662 ns/frame after PR11 (0.55% lower): treat
this as no regression, not an optimization claim. Offline 128/256/512/1024 block
runs averaged 2636/2676/2601/2628 ns/frame. Isolated 128/256 worst-time overruns were
host scheduler preemption in a process benchmark; real device deadline behavior is
still a hardware check.

## Full Windows/Linux session checklist

- [ ] Start at 48 kHz/1024 samples, then repeat stable portions at 512, 256, and 128.
- [ ] Repeat core checks at 44.1 and 96 kHz.
- [ ] Verify mono interface input is exact dual mono; verify stereo L/R order, then left-, right-, and mix-mode selection.
- [ ] On MOTU hardware, identify channel numbers and confirm no swap or collapse.
- [ ] Run a long EXT session and a long stereo-instrument session; listen for stale input, crackles, and channel disagreement.
- [ ] Trigger one and several marked Sister tiles from QWERTY and MIDI; every marked tile starts from the same event and the ensemble does not double-feed the direct bus.
- [ ] Feed FM into Sister with every tile empty.
- [ ] Use QWERTY/MIDI in both windows, change F1–F8 octaves in each, change focus, release notes, and panic; confirm no stuck notes.
- [ ] Place H1/H2/H3 very close, run equal and unequal rates in both directions, force repeated crossings and wrap boundaries, and watch/listen specifically for the reported H2→H1 jump.
- [ ] Sweep position/span/rate/feed and legal feedback, including controlled self-oscillation; verify intentional gestures remain lively but no unrelated head moves.
- [ ] Resize 5→60→5 seconds and rapidly alternate sizes while playing, recording, HOLDing, ROLLing, feeding back, and changing sources. Crop through every head.
- [ ] Repeat resize with active delay/reverb tails and verify retained history follows the age contract.
- [ ] Start/stop Capture M and S and Overdub; verify Capture is inaudible and committed stereo ordering is correct.
- [ ] Sweep Soak/Bleed, every H1/H2/H3/MIX target mask, reverb types/decay, delay time/feedback, distortion, and Master FX Feedback.
- [ ] Compare front `T/F/E/A/FX` controls with the deep mixer in both directions; confirm one value and a click-free linked-stereo response.
- [ ] Toggle every source, MONITOR, ROLL, HOLD, CLEAR, and POWER over a sustained tone and tail; listen for unintended clicks or double feeds.
- [ ] Close/hide/reopen the Sister window during playback; machine, heads, notes, octave, and parameters must persist.
- [ ] Replace a playing source through edit, Undo/Redo, Warp, Smear, and Tear; old one-shots finish safely and new triggers use the replacement.
- [ ] Save/reload schema-v5 projects and presets; open an older project and confirm deterministic unity mixer defaults.
- [ ] Switch/restart the audio device and sample rate; confirm clean recovery and no stale audio.
- [ ] Run `./tapesister --diagnostic-audio`; record average/worst callback time, near-overruns, overruns, actual rate, and device frames during the torture chain.

Pass means no new click/crackle, deadline overrun, hang, stuck note, stale audio,
channel swap/collapse, level surprise, or UI/audio disagreement.

## Short sound-check safety pass

- [ ] 48 kHz/1024: verify stereo input and MOTU L/R ordering.
- [ ] Trigger one tile, a multi-tile ensemble, FM without a tile, and EXT; confirm no parallel double feed.
- [ ] Play from both windows, change F1–F8, then panic; confirm silence and no stuck notes.
- [ ] Cross H1/H2 repeatedly at unequal forward/reverse rates while watching markers and listening.
- [ ] Resize 5→60→5 while ROLLing, then test HOLD and CLEAR.
- [ ] Exercise Soak/Bleed, head/MIX masks, delay/reverb/distortion, and moderate FX feedback.
- [ ] Make a short stereo Capture and Overdub, then save/reload the project.
- [ ] Toggle MONITOR and POWER over sustained audio; reject any hard click, stale tail, or routing disagreement.
