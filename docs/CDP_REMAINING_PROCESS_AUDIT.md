# CDP expansion audit — September 2026

This document records the pre-implementation audit at the 131-process baseline.
See [CDP expansion](CDP_EXPANSION.md) for the subsequent implementation, native
stereo policies, original chain instruments and CREATE integration.

## Recommendation

There is substantial useful CDP territory left. The strongest next step is a
bounded **native stereo execution path**, followed by a small batch of new
time-domain processes. CDP can process interleaved stereo itself for many of
these operations, sharing timing, random choices and gain across channels.
This avoids imposing independent mono decisions on related channels.

This PR records an audit and reproducible probes. It **does not enable new
processes**, change playback, or certify new modes for release. The existing
catalog remains 131 raw processes, 32 curated factory instruments and four
starter chains; the existing stereo allowlist remains 13 raw processes.

Suggested implementation order:

1. Establish native stereo routing using the existing Normalize Peak, Force
   Peak, Feedback Delay, variable/sweeping filters and phasing modes. Compare
   native mono results with the existing Portal path and retain saved identities.
2. Add **Tape Acceleration, Transposition Stack, Back-to-Back, Bounce and
   Low/High-Pass Filter**. These offer different musical behavior with scalar
   controls. Four use existing executables; Bounce adds one runtime target.
3. Expose full raw controls for existing ZIGZAG, DRUNK, ITERATE and FREEZE
   instruments, including their additional duration/repetition modes. Test their
   stereo route with shared seeds and source-dependent bounds.
4. Follow with speech-envelope and spectral batches. Speech repetition/shrinking
   is particularly relevant to vocal material; spectral magnification, folding
   and formant operations offer another set of timbral instruments.

These are candidate batches, not a commitment to enable everything that made
a readable WAV. Channel policy and useful defaults still need individual review.

## Scope and evidence

TapeSister baseline: `aa09233376de2d6ca462ed51d4116decf6e2307a` (merged main).
The supplied CDP8 archive matches all **634 tracked files** at the runtime's
pinned upstream revision
[`28bc42c72c1a7cb0fab933acd1c433be958a787b`](https://github.com/ComposersDesktop/CDP8/tree/28bc42c72c1a7cb0fab933acd1c433be958a787b).
Archive SHA-256:
`1876c7421fdebd09439197a6d3c128b0d8ab53dfc4731bef71abc9bea03e45f5`.

The inventory found **224 distinct executable target names in 227 uncommented
CMake declarations**, including utilities and platform alternatives. These are
not counts of DSP modes, nor claims that all targets build. TapeSister currently
bundles 22 executable targets. The [complete target inventory](audits/cdp8-target-inventory.json)
records source arguments, current bundling and modes probed here; an empty
`probed_modes` list means no direct mode probe in this pass. PVOC was built and
used as a helper. This is broad inventory plus focused investigation, not an
exhaustive test of every mode in all 224 targets.

Built 24 executables from the unmodified pinned source on Linux. Probed **62
process/mode combinations**: 49 direct soundfile modes and 13 analysis-file
modes with PVOC analysis/resynthesis. Their relationship to the current catalog:

| Coverage | Modes probed |
| --- | ---: |
| New candidates, including mono spectral modes | 33 |
| Additional modes of processes already offered as factory instruments | 3 |
| Existing factory modes, investigated for full controls/stereo | 5 |
| Existing raw mono modes, investigated for native stereo | 20 |
| New stereo-only spatial candidate | 1 |

At 44.1 and 48 kHz, time-domain inputs were two-second synthetic harmonic bursts:
mono, unequal/different stereo channels, `R = -0.25 L`, and a silent right channel.
Spectral probes used mono input, 1024-point PVOC with `-o3`. Exact commands,
fixtures and measurements are retained in
[the render results](audits/cdp8-remaining-probes.json).

Of **418 probe runs**, 384 produced bounded, finite, non-silent WAVs with the
expected sample rate/channel count and unchanged input. Of the remaining 34,
32 were expected mono-only/stereo-only input rejections and two were malformed
stereo outputs from `constrict`. Readable output alone is not a stereo pass:
`constrict` also leaked left audio into a silent right channel; Scrub changed
the linked-channel relationship. Clip and Phase intentionally change it.

Forty time-domain modes passed the basic linked-channel and silent-channel
measurements at the selected settings. This is **candidate evidence**, not
approval of all ranges, source types or channel policies.

An additional [32 focused signal checks](audits/cdp8-signal-checks.json) passed:

- Eight seeded traversal/repetition modes reproduced PCM exactly with the same
  positive seed and changed with a different seed, retaining `R = -0.25 L`.
- Normalize/Force Peak applied one gain even when the right channel held the
  largest peak; maximum sample error against that reference was below `3e-8`.
- Silence padding retained the input samples exactly and appended exact zeros.
- The selected low-pass settings attenuated a 6 kHz tone about 32 dB relative
  to a 200 Hz tone. This is one band comparison, not full filter characterization.
- Tape Acceleration shortened a two-second tone to about 1.271 seconds and
  increased its measured pitch from roughly 220 Hz to above 440 Hz.
- A three-layer octave stack produced the expected 220/440/880 Hz components.
- MIDI 48 tuned delay produced impulse spacings of 337 frames at 44.1 kHz and
  367 frames at 48 kHz, with the other channel silent.
- Bounce produced progressively shorter impulse intervals; at 48 kHz these
  were 14400, 10800, 8100, 6075, 4556 and 3417 frames.

These are local native renders and measurements. No new Portal integration,
parameter-endpoint sweep, sanitizer run, CI run, Windows build or hardware
listening is claimed by this audit.

## New direct soundfile candidates

| Candidate | Musical behavior and integration requirement |
| --- | --- |
| `modify speed 5` | Accelerate/decelerate tape playback. Start time, target ratio and time to reach it; the acceleration continues beyond the target time if input remains. Needs source/time/rate bounds and a duration estimate. |
| `modify stack` | Mix transposed copies at a semitone interval, with count, relative layer levels, attack alignment and gain. Scalar interval needs no text file. Conservative gain and copy-count limits are important. |
| `extend baktobak` | Join a reversed tail of the sound to its forward version at a chosen source time. Native stereo preserved the linked fixture. Validate join time and splice room. |
| `bounce bounce` | Repetitions get closer together and decay, like a bouncing object. Native impulse timing checked. Truncation/splices, overlap gain and long outputs need bounded controls. |
| `filter lohi 1` | Low- or high-pass filtering specified by pass/stop edges and attenuation; edge ordering chooses the direction. Mode 2 uses MIDI units for the same role, so need not become a duplicate entry. |
| `dvdwind dvdwind` | Fast scanning through a sound using retained chunks, with speed and chunk-length controls. Successful native mono/stereo probes; still needs chunk-boundary and endpoint checks. |
| `newdelay newdelay` | Pitch-tuned feedback delay/resonator. Verified impulse period and silent-channel isolation. Feedback, tail length and peak compensation need integration bounds. |
| `envspeak` modes 1, 2, 5, 6 | Detect envelope segments, repeat them, reverse-repeat them, or repeat while shrinking from either end. Native stereo copies channel groups together. Needs real speech/singing tests and robust handling of weak/absent segmentation. |
| `tremenv tremenv` | Tremolo introduced after the sound's peak, with envelope-related shaping. Successful native probes; detector behavior on stereo and quiet signals needs more characterization. |
| `silend silend 1` | Append exact silence. Useful chain utility; exact prefix/tail behavior checked. Mode 2 targets a total duration and remains unprobed. |
| `gate gate` modes 1, 2 | Silence or delete material below a threshold. Native executable explicitly requires mono. Deletion changes duration; test splices and threshold extremes. |
| `clip clip` modes 1, 2 | Absolute-level clipping or half-wave clipping. Mode 1 restores the original overall peak afterward, so quiet-channel balance changes as part of the distortion; mode 2 rejects stereo. |
| `quirk quirk` modes 1, 2 | Nonlinear amplitude shaping relative to half-wave or signal amplitude ranges. Both reject stereo. Test incomplete wavesets and edge handling: these fixtures shortened from 2 s to about 1.832 s. |

`envspeak` is envelope segmentation, not linguistic recognition. At 44.1 kHz,
the tested mono/stereo repetition lengths differed by 40 ms despite preserving
the relationship within each stereo frame; at 48 kHz they matched. Treat
segmentation/window rounding as a remaining check, not as interchangeable mono
and stereo timing. Modes 13–24 need cut files; modes 10/22 extract multiple
files, and mode 25 uses a times file. They are separate workflows.

## Existing modes with promising native stereo results

| Existing coverage | Commands investigated |
| --- | --- |
| Peak control | `modify loudness 3/4` |
| Delay and filters | `modify revecho 1`; `filter variable 2`, `sweeping 2`, `phasing 1/2` |
| Structural edits | `sfedit cut 1`, `cutend 1`, `excise 1`; `extend doublets`, `loop 1/2/3`, `scramble 1` |
| Envelopes | `envel dovetail 1/2`, `swell`, `tremolo 1`, `warp 2` |
| Factory traversal/repetition | `extend zigzag 1`, `drunk 1`, `iterate 2`, `freeze 2`; additional modes `drunk 2`, `iterate 1`, `freeze 1` |

All commands in this table passed the selected linked and silent-channel
fixtures. Native normalization scans interleaved samples and applies one gain;
mode 3 raises peaks and rejects an already higher peak, while mode 4 forces the
requested level. These existing semantics must be preserved.

Seeded native traversal schedules apply to channel groups. Same-seed
reproducibility was checked within each rate, not promised across sample rates
or CDP builds. Requested duration is sometimes a stopping condition with a
remaining segment/tail rather than an exact output length: the tested Zigzag
request of 3 s yielded about 4.615 s, and Iterate-to-duration yielded about
4.744 s. The adapter must use actual output lengths and conservative bounds.

Native stereo should be represented as an explicit execution policy distinct
from the existing split-mono allowlist. Keep the current path for established
processes until native equivalence has been checked. Each enabled chain stage
must advertise its own input/output channel contract. Before enabling a batch,
exercise cancellation, changing-duration selection assembly, chain caching,
Apply/Undo/New Tile, stale results and all-channel output safety through the
actual Portal controller.

## Spectral candidates — mono first

All 13 selected spectral modes produced finite mono output at both rates.
They remain subject to PVOC padding, frame quantization and phase behavior.

| Commands | Contribution / default considerations |
| --- | --- |
| `spec magnify` | Hold one analysis window as a sustained texture. Choose an energetic source position; a quiet fixed position can give an almost silent drone. |
| `spec gate` | Remove low-amplitude spectral components. A normalized threshold needs a conservative default for quiet inputs. |
| `blur drunk` | Walk backward/forward through analysis windows. Different from soundfile DRUNK; needs time/window bounds and an explicit randomness description. |
| `blur scatter` | Keep scattered groups of spectral bins. Tested with normalization disabled (`-n`); arbitrary bin counts/normalization still need checks. |
| `caltrain caltrain` | Frequency-dependent spectral blurring. Potentially a different haze from uniform Spectral Blur. |
| `superaccu superaccu 2` | Sustain spectral bands with resonances initially tuned to equal temperament. Decay is a retained-level factor per second. A trial at 0.8 grew a 2 s input to roughly 58 s; at 0.1 the tail ended around 7.25 s. Bound decay and output size explicitly. |
| `spectstr stretch` | Alternative spectral stretching with random frequency variation intended to reduce artifacts. The tested factor 2 yielded about 4.02 s. Needs transient/timbre listening and clear randomness behavior. |
| `specfold specfold 1/2/3` | Fold, invert or permute a selected spectral region. Mode 3 has a seed. Validate bin ranges against the chosen analysis size. |
| `specfnu specfnu 1/3/4` | Narrow, invert or rotate formants. Some modes analyze harmonic/formant structure internally; controls must explain behavior on unpitched windows and quiet material. |

Do not split stereo into independent PVOC renders and label the result
stereo-safe. Earlier Spectral Blur tests already demonstrated changes in the
channels' phase relationship. A linked spectral design is a separate project.

## Exclusions and further territory

| Item | Finding / next requirement |
| --- | --- |
| `constrict constrict` | **Block stereo.** Both linked fixtures produced an odd count of interleaved samples and unreadable stereo WAVs, despite exit code 0. Silent-right fixtures put signal into the right channel. Needs an upstream/frame-alignment investigation. |
| Factory Scrub (`modify radical 3`) | Readable WAVs, but `R = -0.25 L` error reached about `1.21e-5`. Random output also varies by run. Investigate interpolation/channel alignment before stereo enablement. |
| `phase phase 2` | Stereo-only spatial effect; deliberately moves signal between channels and changes cancellation. This needs an explicit image-changing policy, not the channel-preservation criteria above. |
| `extend scramble 2`, `modify revecho 2` | Prior endpoint/audio failures remain unresolved. They were not retested or rehabilitated here; see the earlier audit. |
| `hover2`, `iterfof`, `envnu expdecay/peakchop`, scalar modes of `retime` and `specnu` | Further source/help candidates, not native-tested here. May provide zero-crossing traversal, tuned iteration, envelope cutting, event retiming and harmonic removal. |
| `pitch`, `repitch`, `psow`, `fofex` | Significant pitch-aware/voice territory. Many modes need extracted pitch tracks or grain data. An automatic analysis step is possible, but it needs voiced/unvoiced handling and reusable intermediate data. |
| `formants put/vocode`, `combine`, `morph`, `newmorph`, `specross`, convolution | Two-source or auxiliary-analysis workflows. A second sound/tile input would unlock useful cross-synthesis and morphing, with alignment and source ownership rules. |
| `blur shuffle/weave`, `focus freeze/hold`, text-based filter/vowel definitions, texture/sequence tools | Mapping strings, schedules, breakpoint files or note data. Curated choices could expose subsets, but this is richer input/persistence work. Existing factory Wave Shuffle is a different waveset process. |
| Multi-output cutters, syllable extraction, spectrum/data exporters | Need managed result collections or explicit non-audio outputs. Do not interpret every produced file as a playable sound. |
| Spatial/multichannel tools, generators, analysis/utilities and external tools | Listed in the target inventory. They require different channel, input or result workflows and were not exhaustively reviewed in this pass. |

The supplied SoundThread help catalog was useful for discovery. Its process
metadata was not used as proof of channel safety; executable source and measured
outputs determine the findings above.

## Reproduction

Use a CDP checkout at the pinned revision, a C compiler and CMake. Python probes
require NumPy and SciPy. Build products/WAVs belong in scratch, not the repo.

```sh
cmake -S "$CDP_SOURCE" -B "$CDP_BUILD" -DCMAKE_BUILD_TYPE=Release -DUSE_LOCAL_PORTAUDIO=OFF -DUSE_COMPILER_OPTIMIZATIONS=OFF
cmake --build "$CDP_BUILD" --parallel 4 --target extend modify sfedit envel filter blur pvoc bounce dvdwind constrict gate silend clip quirk envspeak spec spectstr specfnu specfold caltrain superaccu newdelay phase tremenv
python3 tests/audit_cdp_inventory.py --cdp-source "$CDP_SOURCE" --out "$AUDIT_DIR/inventory.json"
python3 tests/audit_cdp_candidates.py --cdp-bin "$CDP_BIN" --out-dir "$AUDIT_DIR/probes"
python3 tests/audit_cdp_signals.py --cdp-bin "$CDP_BIN" --out-dir "$AUDIT_DIR/signals"
```

CDP's build places these executables in `NewRelease` under the source checkout;
set `CDP_BIN` accordingly and ensure they are executable. The candidate harness
retains failed commands and completes successfully even when probes are
rejected; its `valid` field means only a bounded readable render. The signal
harness exits nonzero on any failed stated expectation. Both retain local logs
and WAVs for investigation. Their two-second fixtures are reproducible feasibility
tests, not substitutes for parameter-boundary tests and musical listening.

Native source entry points: `dev/modify/ap_modify.c`, `gain.c`, `radical.c`;
`dev/extend/ap_extend.c` and its traversal/iteration files;
`dev/filter/ap_filter.c`; `dev/science/bounce.c`, `dvdwind.c`, `specfold.c`,
`specfnu.c`, `spectstr.c`, `tremenv.c`; `dev/new/newdelay.c`, `superaccu.c`,
`silend.c`; `dev/standnew/envspeak.c`, `clip.c`, `quirk.c`, `caltrain.c`;
`dev/standalone/constrict.c`, `gate.c`, `phase.c`; `dev/spec/ap_simple.c`;
and `dev/blur/ap_blur.c`. Resolve all paths against the pinned upstream above.
