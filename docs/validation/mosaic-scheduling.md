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
