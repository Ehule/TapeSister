# Mosaic scheduling and larger arrangements

Validated on 2026-10-21, against PR #153 head `dd8467a` before this change.

## Changes

- Capacity increases from 128 to 1,024 cards. The card store and audio working
  memory remain bounded; shared immutable audio is not copied for duplicates.
- The application calls `ts_mosaic_begin_block` for each audio buffer, including
  offline rendering. A linear preparation pass gathers the active cards, a
  min-heap of future starts, the arrangement end, and the global solo state.
  Each output sample visits active cards and checks the next pending start.
- Active cards mix in their original storage order, retaining floating-point
  summation order. Starts use the existing tolerance; ends, playback rate,
  fractional loop overshoot, seek phases, and route handoffs are unchanged.
- Preparation uses fixed working arrays, with no allocation, freeing, locks, or
  library sorting. A seek rebuilds the working set within the current buffer.
  Direct card edits are observed at the next buffer boundary under the existing
  audio lock. Model revisions also invalidate the schedule during a buffer.
- Pan targets and route comparisons are prepared once per buffer/voice change;
  smoothing still runs per sample. Audition caches its card slot and validates
  the ID before using it. Scalar callers that omit `begin_block` still observe
  edits on the next read, at the cost of preparing for each sample.
- Seek, rate changes, and Undo retire playback slots; full voice state is reset
  only when a card next sounds. Short loop wraps avoid clearing all inactive
  routing matrices. Route activity meters are cleared when slots are retired.
- Undo uses a circular 32-entry history. A new checkpoint copies one arrangement
  instead of shifting 31 arrangements when history fills. Source collection
  marks the references in cards/history once, then sweeps the source list.
- Source-browser deduplication uses a bounded pointer lookup table. UI/controller
  arrays, Scatter limits, Matrix card rows, and tests use the capacity constant.
  The main Mosaic controller is kept off the main-thread stack.

## Measurements

`tapesister_mosaic_benchmark` is an explicit Release benchmark (`-O3`), not a CI
timing threshold. It measures Mosaic's renderer alone at 48 kHz, 512-frame
buffers, using deterministic stereo audio and several pitches/loop modes. Each
case renders 48 buffers three times; the table reports the best CPU time per
buffer. The optional capture writes the first trial's raw stereo float output.

These are measurements on the development host, not an X220/X230 or a measurement
of the complete effects chain, hardware latency, or maximum safe polyphony.

| Arrangement cards | Sounding cards | Before (ms/buffer) | After (ms/buffer) |
| ---: | ---: | ---: | ---: |
| 1 | 1 | 0.146 | 0.052 |
| 128 | 1 | 0.332 | 0.060 |
| 128 | 16 | 0.857 | 0.535 |
| 128 | 128 | 4.641 | 4.552 |
| 1,024 | 1 | unavailable | 0.055 |
| 1,024 | 16 | unavailable | 0.550 |

The sparse 128-card case uses about 82% less renderer CPU time. At 128 sounding
cards, sample playback dominates and the improvement is small. Inactive cards
still cost one preparation visit per buffer; their cost is not zero.

The original four captured streams match byte for byte (786,432 bytes). The model
reserves 21,224,704 bytes on this 64-bit build, including all 32 undo snapshots and
1,024 playback slots; the old model reserved 2,685,104 bytes. This excludes source
audio and UI storage. Expanding capacity trades bounded memory for larger projects.

```sh
cmake --build build --target tapesister_mosaic_benchmark
./build/tapesister_mosaic_benchmark
# Optional exact stream capture:
./build/tapesister_mosaic_benchmark mosaic-output.f32
```

## Regression coverage

- Buffered rendering compared sample for sample with scalar rendering for 257
  staggered cards, chords, all sample loop modes, one-shots, fades, stereo balance,
  and custom routes at 44.1/48 kHz and buffer lengths 1, 17, 512, and 2,048.
- Live timing/mix edits, future-card solo, add/delete, Undo/Redo, speed changes,
  seeks within buffers, short loop ranges, silent loop tails, pause/resume,
  audition, source revision, and output-rate changes.
- Save/load all 1,024 cards sharing one source, full-capacity rejection, history
  rollover and redo branching, and source retention/reclamation across history.
- Repeated Shift-copy and atomic capacity rejection for 256-, 512-, and 513-card
  groups; primary selection and source pins remain intact.
- Source browser with 1,024 cards/512 distinct sources, last-page selection,
  deletion/collection, and the real application callback's scheduling hook.

Linux application build and all eight local suites passed: Mosaic model/storage,
Mosaic controller, source-route/Matrix controller, offline rendering, project
reset, audio reconfiguration, pedalboard slots, and post-FX DSP. The focused
large-browser/callback and repeated-copy checks also passed. Packaged platform
builds run separately in GitHub Actions.

Project format remains 12. Existing formats 1–12 remain readable. Older 128-card
builds cannot load arrangements containing more than 128 cards. TrackSister
pattern cards are not part of this change; scheduling is still for sample cards.

## Dense playback follow-up

The 2026-10-21 Windows stress reports showed substantial processing time with
many simultaneous cards, separately from a failed device switch and a long
project-load lock hold. Those reports do not establish how many cards were
crossing the playhead at each retained maximum.

This follow-up caches attack/release lengths and ramp steps per voice/output
rate, and normalized fades and loop controls per audio buffer. Settled gain/pan
ramps and completed attacks avoid redundant arithmetic. Pitch, interpolation,
loop phase, mixing order and voice capacity are unchanged.

The extended Release benchmark adds all 1,024 cards sounding and a 128-card
five-note chord case with routes, overlapping fades and live mute/gain/pan/fade
edits. Same development host and methodology as above; before is `5415cb0`.

| Cards | Sounding | Before (ms/buffer) | After (ms/buffer) |
| ---: | ---: | ---: | ---: |
| 128 | 16 | 0.555 | 0.360 |
| 128 | 128 | 4.347 | 2.880 |
| 1,024 | 1 | 0.060 | 0.052 |
| 1,024 | 16 | 0.547 | 0.371 |
| 1,024 | 1,024 | 35.814 | 23.798 |
| 128, five-note chords and routes | 128 | 14.193 | 12.969 |

The dense single-note cases improve about 33%; the chord/routing case improves
about 9%. All eight captured streams, including routed data in the last case,
match byte for byte (20,447,232 bytes). The updated model reserves 21,273,904
bytes on this 64-bit build. The 1,024-card dense case still exceeds the 10.667 ms
buffer budget. These measurements are not a full-application or MOTU benchmark.

Audio Health now supplies total-card and playhead card/note counts without adding
audio-callback work. Session logs retain configuration-switch errors/rollback and
New Project prompt openings to help diagnose recurrence. The reported Windows
driver-switch failure, unexpected New prompt and load-time lock hold are not
claimed fixed by this DSP change.

Linux application build and seven affected suites pass: Mosaic model/storage,
Mosaic controller, Audio Health, audio reconfiguration, source-route/Matrix
controller, offline rendering and project reset. Health coverage includes all
1,024 cards, overlapping chords, muted/finished one-shot counts, stopped transport,
snapshot throttling and exported workload fields.

## Loading and editing lock follow-up

Validated on 2026-10-22 against `bac0572`. The user's running-session logs
separated sparse playback from simultaneous 1,020-card overload, and retained
long `load_instrument` and `mosaic_event` holds. This change addresses verified
work inside those locks; it does not increase simultaneous DSP capacity or claim
to fix the previously intermittent Windows driver-switch/New prompt issue.

- TSR loading prepares all banks, Record Bank, tracker definitions and Mosaic
  sources before acquiring the output lock. Sister/page-manifest consistency is
  checked before publication, so preparation errors preserve the current project.
- Publication swaps bank ownership and replaces the stable Mosaic model address.
  Old audio and definitions are destroyed after unlocking. Freshly loaded Mosaic
  has no live voices or history: publication retires voices and copies definitions
  and settings without writing the unused 17 MiB undo backing store.
- Copy drags no longer auto-space each new card twice and then discard its position.
  Geometry planning includes originals as obstacles on the first copy motion;
  the checkpoint, new cards and final placement publish under one lock. There is
  no intermediate audible pile of copies at their originals' positions.
- Group drag/nudge snapping and spacing run outside the lock, using a compact
  list of unselected obstacles in the same storage order. Audio only reads card
  geometry; the UI remains its sole writer. Auto-Space, snap tie ordering,
  primary selection and Undo/Redo semantics are preserved.

Development-host Release measurements (explicit benchmarks, no CI time gates):

| Operation | Before | After |
| --- | ---: | ---: |
| 128-card copy setup, Auto-Space on | 7.782 ms | 0.064 ms |
| 512-card copy setup, Auto-Space on | 206.245 ms | 0.672 ms |
| 1,024-card tidy, Auto-Space on | 3.634 ms | 3.496 ms |
| 1,024-card align, Auto-Space on | 1.813 ms | 1.738 ms |

Copy setup includes checkpointing and creation, excluding source setup and the
separate group geometry calculation. Values are the best of three wall-time
runs. Tidy/alignment are controls, essentially unchanged.

The load benchmark uses four populated Sample pages, Record Bank, 16 one-second
stereo sources and 1,024 Mosaic cards. Before, the core loader's locked work took
162.759 / 86.188 / 112.012 ms CPU across three trials. After, publication alone
took **2.711 / 0.159 / 0.433 ms CPU**. Preparation took 97.455 / 83.105 / 158.795 ms
and retirement 0.199 / 0.088 / 0.205 ms, both outside the lock. The first trial
includes first-touch page costs. Disk waits are not included in CPU time.

These are synthetic core-loader measurements, not the complete application's
`load_instrument` hold or a MOTU measurement. Sister runtime reset, effect
allocation/restoration and note synchronization still occur under that lock.
Preparation remains synchronous on the UI thread and temporarily retains both
projects. Dense Auto-Space geometry can still occupy the UI, though playback is
not excluded while it is calculated.

```sh
cmake --build build --target tapesister_mosaic_controller_tests tapesister_project_load_benchmark
./build/tapesister_mosaic_controller_tests --edit-benchmark
# Writes a synthetic load-benchmark project in the working directory:
mkdir -p /tmp/tapesister-load-bench
(cd /tmp/tapesister-load-bench && /absolute/path/to/build/tapesister_project_load_benchmark)
```

Regression coverage adds staged-load cancellation, rejection of parked targets,
multi-page/Record Bank replacement, publication after the project folder moves,
retired sample lifetime, duplicate-publication rejection, and failed preparation
through the real Open path. Full prior undo/voice state is replaced; the first
512 routed frames match a fresh load byte for byte, and new Undo starts empty.
Twenty-four group scenarios compare exact positions against the previous
algorithm, including copies, snapping, Auto-Space, primary selection, playback
between planning/publication, and Undo/Redo.

Linux application build and all seven affected suites pass: Mosaic model/storage,
Mosaic controller, Sample pages, project reset/Open, source-route/Matrix controller,
keyboard HOLD/sequences, and embedded TrackSister/project loading. Address and
undefined-behavior sanitizer checks instrument the changed Mosaic and Sample
page modules and their core tests. LeakSanitizer is unavailable in this traced
container; those checks run with leak detection disabled.
