# Matrix interaction and playback overhead

The reported Windows WASAPI session (48 kHz, MOTU M Series) overran its
512-frame deadline ten times in a fresh 31-second measurement, although its
longest control hold was only 0.086 ms. A subsequent 1024-frame measurement
still used about 73% of the current buffer budget. These reports establish
deadline pressure; they do not identify the cause of each audible click.

This follow-up to the expandable pedalboard removes three avoidable costs:

- Matrix maintenance no longer locks audio on every UI refresh when there is
  no retired processor ready to collect. Pointer ownership stays on the UI
  thread, countdowns are atomic, and actual detachment still uses exclusion.
- Graph compilation builds dependency masks once instead of rescanning every
  source and destination for every candidate. Stable processor order and
  sidechain/feedback dependencies are preserved.
- Only rows with explicit delayed feedback compute and store the bounded
  feedback history. Forward processing, effect tails and modulation still run.
  Changing delayed edges already clears history before it can become audible.

The visualizer already launches a separate, lower-priority rendering process;
its open/close code does not acquire the audio lock. Its lifecycle is unchanged.
The long project/instrument-loading hold in the earlier accumulated report is
also a separate issue and is not addressed by this patch.

## Diagnostics

Audio Health now names added effects and their sidechains using their types
and stable instance IDs, instead of reporting them as `OUTPUT`. It lists each
instance's enabled state and mix. `Matrix effects DSP` is a separate sampled
profile row within the inclusive Router total. Do not add it to Router time.

## Reproducible routing benchmark

Build `tapesister_matrix_benchmark` and run it against the same Release
configuration before and after the change. The baseline is commit `5d32ef5`.
The harness uses sixteen identity processors, including ordinary and sidechain
connections, to isolate routing cost. Timings use process CPU time and include
output hashing. They are not measurements of a whole project or Windows audio.

Observed Linux GCC 13 Release ranges across the comparison runs:

| Operation | Baseline | Updated |
|---|---:|---:|
| Graph edit | 0.106–0.134 ms | 0.011–0.013 ms |
| Graph playback / 512 frames, no feedback | 0.920–1.002 ms | 0.680–0.724 ms |
| Graph playback / 512 frames, one feedback edge | 0.935–1.130 ms | 0.690–0.758 ms |

For all comparison runs, the complete eight stereo outputs over 480,000 frames
had identical before/after byte hashes:

- No feedback: `286f59a26189021c`
- Feedback enabled: `65bcad35119d0129`

Regression coverage checks 80 deterministic shuffled graphs against the old
ordering algorithm, cycle rejection, sidechain dependencies, repeated feedback
enable/disable with exact delayed sample values, and cleanup both before and
after the retirement countdown. One hundred idle cleanup polls acquire zero
audio locks; a completed retirement acquires one. Health report coverage checks
effect and sidechain names.

The Linux Release application built successfully (local CDP bundling and MIDI
disabled). All seven targeted CTest suites passed: Matrix/spatial controller,
Audio Health, profile, Matrix instances, routing graph, visualizer controller,
and expanded pedalboard. The updated profile table was rendered and inspected.

The remaining acceptance check is playback on the reported Windows/MOTU setup.
After applying a buffer change and loading the project, reset Audio Health,
then repeat the visualizer, routing and pedalboard interactions. Compare
overruns and the new Matrix-effects row. A clean local test run does not prove
that the reported audible clicks are resolved.
