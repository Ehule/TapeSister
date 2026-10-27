# Pattern-card callback investigation

Investigation baseline: `main` at `47d38395d89f83a3930495263a5fd7cfb8ab4c53`
(Mosaic pattern cards). PR #156 (markers, cue loops and pattern updates) and #157
(project-save protection) were open when measurements began. The final change
is rebased onto `225fffc9109d22e312c83c2c5cda516f000d2a34`, after both merged.
Its diff contains the lookup improvement, tests and documentation. The audit
supplied hypotheses; current source, fixtures and callback measurements supplied
the evidence below.

## Findings and scope

The embedded pattern backend looks up a player by Mosaic voice identity and note
on **every output sample**. The original lookup scans the reserved player array;
with many simultaneous cards/chord notes, successful lookups repeatedly traverse
the same large player structures. Control-side reservation is five players per
placed pattern card plus an audition chord. A sparse arrangement's reserved
capacity is different from its active DSP workload.

Each Mosaic voice now retains one unsigned player-index hint per chord note.
The backend validates the index, owner and note before using it. A missing,
out-of-range or stale hint uses the original scan and oldest-player policy.
Indices survive pool relocation; source, generation, rate, loop mode and backward
target checks still run before consuming audio. No player sharing, stealing,
render cap, resampling change, routing change, clock change or quality reduction
is introduced. The hints are runtime state and do not change project formats.
They add about 20 KiB of index payload per Mosaic (plus structure padding), with
no additional heap allocation or lock in the callback. No CPU-specific compiler
flags or instruction requirements are added.

This removes repeated lookup work from the steady path; actual FT2 effects and
mixing still cost time for every independent performance. A profiling pilot of
the original 32-card callback put about 15% of sampled self time in `engine_read`
and substantial time in tile mixing, sample readers and sanitation. That sample
does not isolate the lookup's cost; the paired callback measurements do.

Late seeks remain expensive. `player_skip` reconstructs effects and voice phase
from the pattern start, advancing tracker ticks and live sample voices. This PR
preserves that reconstruction, the existing seek attack and route handoff.

## Windows ASIO control exclusion

Current `src/ts_asio_backend.cpp` clears native output before attempting the
shared control mutex. It retries for the smaller of **0.5 ms** and **one eighth
of the actual negotiated buffer period**. If the mutex stays busy, it increments
control-lock skips and returns the cleared buffer without calling the application
renderer. Brief contention can recover within the budget. Driver-reported xruns
are counted separately. The budget is computed after device negotiation.

The adapter fixture forces a 50 ms control hold. At the fixture's actual
128 frames / 48 kHz, its period is 2.667 ms and its retry allowance is 0.333 ms.
It verifies an all-zero skipped buffer, one control skip, zero driver reports,
successful subsequent playback, channel stride/ranges and counter reset. The
bounded-lock unit cases cover immediate acquisition, brief recovery, exhaustion
and time-counter wrap. The fixture now prints its measured callback wall time
alongside that budget; it adds no production telemetry.

This demonstrates what sustained contention does, not what caused a particular
past click. Increasing the retry budget trades silence for later rendering and
does not make an expensive control publication safe. The production ASIO locking
policy is unchanged. Audio Health already exposes skips, recoveries, driver
reports, control hold/wait and call sites. Application render timing excludes
the native mutex retry, capture and driver work. A small render load cannot rule
out a control-lock skip; a zero skip counter cannot rule out render overruns.

## Measurement method

All performance numbers here are from a **Linux development container on an
Intel Xeon Platinum 8573C**, with nine visible virtual CPUs, an eight-CPU cgroup
quota and 8 GiB memory limit. GCC Release (`-O3 -DNDEBUG`, generic x86-64) was used;
the benchmark explicitly keeps its assertions enabled. There was no physical
audio output or ASIO driver. These are not measurements on an X220, X230 or HP A8,
and cannot establish a usable buffer size or safe pattern count on those PCs.

`tests/benchmark_pattern_audio.c` drives the canonical `audio_callback` with real
embedded FT2 pattern instances, the normal Sister runtime, diagnostics, pan/gain
and per-source sends. Output has eight channels, including the normal unused
channel clearing. The fixture has a 256-row pattern at 125 BPM / six ticks, a
looped sine tile, notes with vibrato every four rows, and eight lanes unless
specified. Card transpositions and pan differ. The effects case adds 16-lens
Prism plus distortion, delay, reverb and grain. QWERTY/edit cases call the real
Z-key note-down handler, hold/release the note, and change card gain/pan between
callbacks under the measured control lock.

The optional comparison target compiles the exact original backend from the base
commit under a renamed installer. Two independent fixtures run in the same
process, alternating callback order on successive blocks. They share the final
callback harness and optional-backend dispatch; the reference uses the existing
read fallback. Every measured block is compared byte for byte on all eight
output channels, and each run records a cumulative PCM hash. This isolates the
backend lookup change; it is not a comparison of two complete shipping binaries.

There are 64 warm-up callbacks per fixture and five process trials per case.
Most cases measure 384 blocks; dense cases measure 128, and the seek case eight
so its expensive first block is visible. Clock reads surround only the callback;
PCM validation, hashing, setup, card creation and control gestures are outside
that interval. Linux thread CPU time separates execution cost from wall time;
both are retained. The Windows benchmark uses `GetThreadTimes`, which can have
coarse per-block resolution; use wall time and F12 for Windows driver tests.
No real GUI rendering, key-delivery latency, device contention or simultaneous
UI/audio threads are benchmarked by this harness.

The host was noisy: identical cases varied substantially between trials, and
wall maxima include descheduling. Results use paired comparisons within each
trial, retain tail/deadline observations, and do not treat benchmark threshold
counts as physical dropouts. No build or second benchmark ran concurrently.
The full timing matrix was collected before the integration rebase; it is not
a timing measurement of the final combined shipping binary. The combined code
retains #156's source-version and cue-phase arguments and is checked separately
by callback PCM comparisons, pattern-update/marker tests and native CI.

## Results

| Case | Before CPU median / p95, ms | After CPU median / p95, ms | Paired median reduction |
| --- | ---: | ---: | ---: |
| 1 card | 1.036 / 2.920 | 1.071 / 3.173 | -2.0% |
| 4 cards | 2.203 / 8.232 | 2.285 / 7.449 | -4.9% |
| 8 cards | 2.005 / 5.017 | 2.015 / 4.915 | -0.1% |
| 16 cards | 3.879 / 11.041 | 3.834 / 11.197 | +0.4% |
| 32 cards | 10.563 / 22.028 | 9.773 / 21.916 | +5.3% |
| 4 cards × 5 notes | 4.664 / 10.468 | 4.640 / 11.005 | +0.5% |
| 4 cards × 32 lanes | 3.716 / 8.073 | 3.611 / 8.759 | +0.9% |
| 8 cards + QWERTY/mix edits | 2.523 / 6.713 | 2.520 / 6.696 | +0.1% |
| 16 silent patterns | 1.612 / 4.796 | 1.439 / 4.221 | +6.1% |
| 1 active / 64 placed | 0.739 / 1.809 | 0.724 / 1.932 | +1.7% |
| 16 cards, 44.1 kHz / 512 | 7.916 / 19.005 | 7.651 / 20.152 | +3.7% |
| 4 cards + Prism/4 FX | 2.711 / 8.648 | 2.746 / 10.466 | +1.2% |
| 16 cards, seek 29.5 s | 4.866 / 8.366 | 5.087 / 7.484 | +0.2% |
| 64 cards | 19.537 / 39.626 | 17.893 / 36.126 | +10.7% |
| 128 cards | 27.411 / 44.264 | 17.632 / 31.488 | +30.5% |
| 160 active / 1020 placed | 56.809 / 86.780 | 36.323 / 55.489 | +35.5% |
| 1 active / 1020 placed | 0.855 / 2.639 | 0.887 / 2.837 | -2.6% |
| 32 cards × 5 notes | 55.814 / 118.978 | 34.988 / 97.784 | +37.3% |

The raw per-trial aggregate data, including CPU/wall averages, percentiles,
maxima, first-callback time, nominal-deadline counts, control hold/wait and hashes,
is in [realtime-pattern-audio.csv](realtime-pattern-audio.csv). Timings are in ms.
The table reports the median of five per-trial callback CPU medians and the
median paired percentage reduction; the latter need not equal the percentage
between the two displayed medians. Positive percentages mean less CPU time.

All 26,280 measured callback pairs across 90 process runs matched bit for bit.
At both 128 and 160 active cards, every one of the 640 measured callbacks per
variant exceeded the 5.333 ms nominal CPU budget. Worst wall times reached about
1.3 seconds on this shared host, including an after-change outlier worse than
the reference maximum; tail latency is not bounded by this optimization.
The 29.5-second seek first callback ranged from 31.9–95.8 ms CPU before and
32.4–107.7 ms after. It has no demonstrated improvement.

Dense overlap benefits materially; small cases show no consistent improvement,
and some individual measurements regress. Expensive DSP and large overlaps can
still exceed the 5.333 ms nominal period at 48 kHz / 256 frames. This is a removal
of avoidable work, not a general dropout cure. The seek case's first callback is
reported separately in the CSV; it remains far beyond a short audio period in
some trials. QWERTY/control timing is not established as improved by this change.

## Validation and reproduction

The 26 selected local CTest cases passed, including the ASIO adapter, Audio
Health and diagnostics, QWERTY HOLD, tracker playback/edit/controller, embedded
tracker realtime and pattern integration, Mosaic/controller, source sends,
routing matrix, MIDI and stereo note-bank cases. The changed backend code in
the lookup test also passed AddressSanitizer and UndefinedBehaviorSanitizer;
other linked libraries were Release builds and leak checking was disabled.
Native Linux and Windows release builds and their regression suites are checked
by the PR workflow; see the PR checks for their final status.

The dedicated lookup test compares actual audio and player ownership/state with
the scan path through invalid indices, stale owners, repeated and changed notes,
source replacements, generation changes, backward seeks, sample-rate and loop
changes, pool growth, failed growth, zero capacity and exhaustion. Allocator
wrappers reject backend allocation during reads. Existing pattern integration
tests cover independently sounding overlaps, fractional speed, seeks, mixed
audio/pattern cards, source replacement/Undo, routing, foreground tracker state,
audition and project round-trips.

To reproduce the paired harness in a normal SDL/FFmpeg development environment
(Bash/MSYS2; adapt paths for your checkout):

```sh
git show 47d38395d89f83a3930495263a5fd7cfb8ab4c53:src/ts_pattern_tapehead.c > /tmp/tapesister-pattern-before.c
cmake -S . -B build-pattern -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTAPESISTER_BUNDLE_CDP8=OFF \
  -DTAPESISTER_PATTERN_REFERENCE_SOURCE=/tmp/tapesister-pattern-before.c
cmake --build build-pattern --target tapesister_pattern_audio_compare
build-pattern/tapesister_pattern_audio_compare --cards 128 --blocks 128
build-pattern/tapesister_pattern_audio_compare --cards 160 --total 1020 --blocks 128
build-pattern/tapesister_pattern_audio_compare --cards 16 --seek 29.5 --blocks 8
build-pattern/tapesister_pattern_audio_compare --cards 8 --edits
```

Repeat each command five times in a clean fixture directory with no other build
or benchmark running. The CSV records every workload parameter, including the
32-lane, five-note chord, silent-pattern, sparse-arrangement, effects and
44.1 kHz / 512-frame variants. Timing comparisons intentionally have no CI speed
gate. The short callback smoke and lookup correctness tests run on Linux and
Windows in the native release workflow.

For the practical hardware comparison, use the phased
[F12 Audio Health test](../AUDIO_HEALTH.md): stationary playback, QWERTY notes,
live edits, early/late cue seeks and overlap steps, with RESET and saved reports
per phase and an immediate Ctrl+Shift+F12 mark for anything heard. Older PCs,
real drivers, other tracker effects/tiles, source-update publication and long
locked control operations remain subjects for measurement. No historical
audible event is attributed to either hypothesis by this PR.
