# Master-clock foundation validation

Before: F11 cockpit commit `d7c394e4050267c2930cdfbbaba0b6147bfbd0df`
(PR #162). After: clock foundation `4f3a5d894b3e622f8ccf01306b181fd7f231d4f4`.
The later F11 font/rendering follow-up leaves that audio code unchanged; its
separate UI measurements are in [font validation](f11-font-legibility.md).
This stage adds optional master
tempo and independent group/Home choices. It does not add per-card master clocks,
linked cues, quantized launches or the future persistent Mosaic looper.

## Correctness checks

All 137 registered Linux checks passed across the broad run and permission-only
retries of four locally materialized ELF executables. Those four initially could
not start; their assertions did not fail. After the final control-snapshot and
unopened-Home changes, the seven relevant clock, F11, full routing-controller,
ARP, state and project-save checks passed again. Production DSP remains optimized;
test assertions stay enabled in Release.

The tests cover:

- Default-off/unused-master PCM and routing; actual native versus saved score BPM;
  changes from 160 to 80 BPM without note transposition, row restart or tick-phase
  reset; disabling restores the current local score tempo.
- Tracker-source tempo commands occurring inside an audio buffer, using actual
  native tick-table timing rather than a UI BPM poll. A saved source initializes
  and rebinds correctly with the tracker workspace closed.
- ARP phase across source/tempo changes, independent ARP continuing through master
  pause, held manual notes surviving transport changes, and fractional-sample
  carry over ten simulated minutes. Existing ARP modes keep their old timing law.
- Group membership separate from Home following, including an unjoined running
  follower, a joined player retaining position, zero-player master transport,
  and Home binding a previously unopened tracker before rewinding it.
- Actual F11 route edits without starting, stopping or seeking players; local
  Mosaic cue jumps, loop wraps and loop release while master time continues.
- Overlapping scheduled pattern cards retaining their private ticks, effects and
  ramps with a 137 BPM foreground override; existing pattern seeks, note playing,
  routing/Matrix and editing checks.
- Clock configuration and displayed cursor round trips; native Save/Open over an
  existing project and from New; old projects with and without Sister sidecars;
  New clearing clock state; malformed/non-finite state rejection without publication.
- User-added files/folders retained while clock state changes; failed publication
  rolling back to the previous clock settings; existing interrupted-save recovery.
- Offline exports restoring the tracker tempo route, Mosaic-only exports remaining
  identical, and existing render collision/cancellation/failure/tail checks.

The all-PR baseline now includes clock unit and routing integration checks (17
tests total). Windows compilation/regressions and archive checks run in CI; this
Linux host cannot establish Windows ASIO driver or physical-device behavior.

## Development-host measurements

Shared Linux x86-64 host reporting AMD EPYC 9V74, nine visible virtual CPUs,
eight-CPU quota and 8 GiB limit; GCC Release, generic x86-64, no new CPU-specific
instructions. SDL dummy audio/video with the real application callback and native
FT2 pattern engine. No other local build ran during these measurements. These
are not measurements of the user's X220/X230, HP A8, physical display or saved
project, and are not a supported overlap-count or buffer-size guarantee.

Pattern cases: 48 kHz, 256 frames (5.333 ms budget), eight tracker lanes, one
note per card, 1,024 measured blocks after 64 warm-up blocks. QWERTY note/release
and card gain/pan edits occur every sixteen blocks. Three runs per variant,
alternating variant order. The table reports the median of three per-run thread
CPU means, then median p95 in parentheses. First-seek CPU is reported separately.

| Pattern fixture | Before, ms | Clock off, ms | Master 137 enabled, ms |
| --- | ---: | ---: | ---: |
| 8 overlapping / 1,024 placed | 1.116 (1.465) | 1.066 (1.427) | 1.075 (1.495) |
| 32 overlapping / 32 placed | 3.397 (4.505) | 3.458 (4.588) | 3.368 (4.473) |
| 8 overlapping, seek to 12 s | 1.205 (1.612) | 1.141 (1.502) | 1.145 (1.449) |
| First callback after that seek | 6.892 | 6.074 | 5.727 |

The master case routes the foreground tracker/ARP to internal 137 BPM; the
foreground song/ARP are stopped while the private cards and manual keys play.
All 27 pattern runs have matching before/off/master PCM hashes for each fixture;
the first 8-card run also compares the complete raw eight-channel files byte for
byte. Thus these measurements test private-card isolation and callback cost,
not the sound of intentionally retiming a playing foreground song.

Dense and late-seek loads still exceed the budget in some blocks: across 3,072
blocks per variant, thread-CPU over-budget counts were 69/73/53 for dense-32 and
4/5/6 for the late-seek case (before/off/master). The first seek alone exceeds
5.333 ms in all three median cases. Effect/voice reconstruction is unchanged.
Shared-host variation prevents attributing these differences to clock overhead
or claiming a throughput improvement. Raw rows include wall timing and maxima:
[pattern callback CSV](master-clock-routing.csv).

The separate basic callback benchmark uses 48 kHz/512 frames, 1,500 measured
callbacks per scenario and PROFILE instrumentation. Three counterbalanced runs,
median wall time of each per-run mean:

| Basic fixture | Before, ms | Clock off, ms |
| --- | ---: | ---: |
| Idle | 0.604 | 0.633 |
| Looping sample | 0.602 | 0.621 |
| Prism 16 + pedalboard | 1.253 | 1.314 |
| Same, with warmed zero-mix reverb | 1.473 | 1.525 |

These medians increase 0.019–0.061 ms (up to about 0.6% of the 10.667 ms block
budget). Per-run ranges overlap; this short profiled wall-time test does not
isolate scheduling noise, code layout and clock logic. It is not a zero-overhead
claim. A separate captured pass compares all four complete stereo streams byte
for byte and finds them identical. [Basic callback CSV](master-clock-basic.csv).
New controls are snapshotted once per block; source-rate lookups occur only at
tempo changes. Clock advancement is per audio span, with no allocation, extra
audio pass, wall-clock polling or per-pattern master work in the callback.

Reproduce with the existing Release benchmark targets:

```sh
tapesister_pattern_audio_benchmark --cards 8 --total 1024 --frames 256 --blocks 1024 --tracks 8 --notes 1 --edits
tapesister_pattern_audio_benchmark --cards 32 --total 32 --frames 256 --blocks 1024 --tracks 8 --notes 1 --edits
tapesister_pattern_audio_benchmark --cards 8 --total 1024 --frames 256 --blocks 1024 --tracks 8 --notes 1 --edits --seek 12
```

Repeat the after cases with `--master-bpm 137`; use `--capture <file>` to compare
full PCM. `tapesister_performance_benchmark [capture.f32]` measures the basic
fixtures. No benchmark time is a CI pass/fail threshold.

## Practical F12 test on each actual computer

1. Test the cockpit build first. Then open a **copy** of the important project
   in the clock build; keep its original for A/B comparisons. Active-clock or
   custom-Home saves require a clock-aware build. Older projects start clock off.
2. Keep routing, device, actual rate/buffer and project passage the same. With
   PROFILE off, F12 RESET, play thirty seconds with clock off, then COPY REPORT
   and SAVE SESSION. Repeat with F11 open and closed. Note the Clock Routing
   configuration separately; F12's current setup report does not list these new
   route controls.
3. Enable internal master with no tempo followers: sound and tape time should
   remain unchanged. Then follow master tempo with TrackSister/ARP explicitly.
   Change BPM while playing QWERTY notes and a held chord; release the keys and
   edit during playback. The native score tempo must remain available when the
   route is removed. No sample should transpose merely from following tempo.
4. Let ARP follow tempo but ignore group start/pause. Pause Group: ARP continues,
   master elapsed beats pause, and the tempo remains available. Check group and
   Home choices separately; try Home once before opening the tracker workspace.
   ARP Stop retains its existing pattern-restart behavior.
5. Trigger early/late Mosaic cues, including into overlapping pattern cards.
   Loop a cue and release the loop: only Mosaic changes position. TrackSister,
   ARP and master time must not jump. Increase overlap gradually if testing load;
   retain the F12 maximum after the first late seek.
6. Choose TrackSister as source and try score tempo commands; test stopped and
   playing source states. Save/reopen with that workspace closed; verify routes,
   Home choices, restored paused master location and the saved project view.
7. RESET between stationary, note-playing, editing and seeking cases. Mark an
   audible problem with Ctrl+Shift+F12 and SAVE SESSION. Compare render budget,
   ASIO control skips/recoveries, driver reports and control-hold site separately.
   Counters alone do not establish an audible dropout or its cause.

No external MIDI clock, quantized launch, linked cue mapping, shared-clock pattern
cards or persistent loop cards are implemented here. See [Clock Routing](../CLOCK_ROUTING.md)
for the exact behavior and compatibility boundary.
