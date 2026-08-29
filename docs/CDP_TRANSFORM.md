# Curated CDP Transform architecture

## Product boundary and navigation

CDP is an offline backend for TapeSister's Transform stage. It is never called from
the audio callback, Capture recorder, or a blocking SDL event handler. TapeSister
exposes fixed musical recipes, not arbitrary executable names, shell text, a terminal,
breakpoint editor, or process graph.

CDP is one top-level mode with two internal pages of 16 recipes. The global shortcuts
remain `1` Sample Tiles, `2` Keyboard, `3` CDP, and `4` DSP. From another mode, `3`
enters CDP at the last-used page. Inside CDP, `3` and the visible **CDP 1 | CDP 2**
toggle switch pages without changing the sample, selection, or viewport. The normal
panel cycle also treats CDP as one mode.

Left click is the performance path: it renders the tile's saved values asynchronously
and applies the validated immutable result in one Undo transaction. Middle click opens
the shared Transform workspace for editing and non-destructive audition. `SAVE/UPDATE`
stores macro values, Mix, and the accepted seed as an optional
`CdpPreset.<recipe-id>` row in `tapesister.ini`; it never stores audio. Legacy numbered
rows remain readable, and factory defaults remain the fallback when a row is absent.

The fixed control positions and one selected algorithm follow the direct-interaction
lesson of SOMA Laboratory WARP without copying its branding, artwork, algorithms, or
panel design. A recipe exposes only the one to four controls its real process supports.

## Versioned registry and configurable catalog

`TsCdpRecipe` is compiled, versioned factory data. Each entry declares a stable ID,
default visibility, display text, category, schema/recipe/provenance versions, one to
three typed stages, executable requirements, input/output channels, minimum input,
duration and Mix policy, determinism/seed support, analysis settings, and one to four
typed musical controls. Enumerated controls carry valid numeric values and musical names.
Unknown user command text cannot enter the adapter. The registry has capacity for 128
recipes; a catalog view maps the first 32 enabled stable IDs onto the fixed two-page
performance surface. Presentation slots are not recipe identity.

The placement is part of the compatibility contract:

| CDP 1 | Recipe | Executable/process |
|---:|---|---|
| 01 | DRUNK | `extend drunk 1` |
| 02 | SHRED | `modify radical 2` |
| 03 | HOVER | `hover hover` |
| 04 | SCRUB | `modify radical 3` |
| 05 | ZIGZAG | `extend zigzag 1` |
| 06 | STUTTER | `stutter stutter` plus a job-local cut-time file |
| 07 | SORTER | `sorter sorter 1-4` |
| 08 | SPLINTER | `splinter splinter 1` |
| 09 | SCRAMBLE | `extend scramble 1` |
| 10 | DOUBLETS | `extend doublets` |
| 11 | MOTOR | `motor motor 1` |
| 12 | GREV | `grain grev 1-5` |
| 13 | TIMEWARP | `grain timewarp` |
| 14 | TELESCOPE | `distort telescope` |
| 15 | FREEZE | `freeze freeze 2` |
| 16 | ITERATE | `extend iterate 2` |

| CDP 2 | Recipe | Executable/process |
|---:|---|---|
| 01 | GLISTEN | `pvoc anal` → `glisten glisten` → `pvoc synth` |
| 02 | SPEC SMEAR | `pvoc anal` → `blur blur` → `pvoc synth` |
| 03 | WAVE SCRAMBLE | `scramble scramble 1-4` |
| 04 | BRASSAGE | `modify brassage 6` |
| 05 | FRACTAL | `distort fractal` |
| 06 | INTERPOLATE | `distort interpolate` |
| 07 | OMIT | `distort omit` |
| 08 | REPLACE | `distort replace` |
| 09 | PITCH | `distort pitch` |
| 10 | SHUFFLE | `distort shuffle` with curated domain/image strings |
| 11 | REFORM | `distort reform 2/4/6/7` |
| 12 | DISTSHIFT | `distshift distshift 1-2` |
| 13 | SEGZIG | `distmore segszig 2` |
| 14 | OVERLOAD | `distort overload 1-2` |
| 15 | FILTER BANK | `filter bank 1-3`, anchored to current audible tuning |
| 16 | GRANULATE | `modify brassage 5` |

Mappings and ranges were read from the supplied CDP8 usage/range source and checked
against SoundThread's process metadata/argument ordering. Parameters are formatted as
separate argv items. Restricted modes, seeds, divisions, shapes, and permutation
strings are quantized or selected inside the factory mapping before execution.

The compiled registry is the authoritative source for each numeric minimum, maximum,
step, default, unit, duration policy, and seed policy. This table records the musical
control-to-CDP mapping so a future edit cannot silently turn a label into a fake macro:

| Recipe | TapeSister controls | CDP mapping and fixed safety choices |
|---|---|---|
| DRUNK | POSITION, RANGE, STEP, CLOCK | `locus`, `ambitus`, `step`, `clock`; output duration follows input; 10 ms splice |
| SHRED | PASSES, CHUNK, SCATTER | `repeats`, `chunklen`, `-s`; native duration-preserving mode 2 |
| HOVER | POSITION, WIDTH, RATE, WANDER | `loc`, `locrand`, `frq`, `frqrand`; 10 ms splice |
| SCRUB | LENGTH, DOWN, UP, DIRECTION | output `dur`, `-l`, `-h`, optional forward-only `-f` |
| ZIGZAG | START, SPAN, LENGTH, GRAIN | `start`, derived `end`, output duration, minimum zig; 10 ms splice |
| STUTTER | CHUNK, REPEAT, PITCH, GAP | CHUNK generates isolated cut times; output duration multiplier, `-t`, `silprop`; fixed safe silence limits |
| SORTER | ORDER, EVENT, SMOOTH | modes 1–4, `esiz`, `-s`; output-spacing/pitch modes intentionally hidden |
| SPLINTER | TARGET, WAVES, PITCH, RATE | `target`, `wcnt`, `-f`, `p2`; shrink/output counts fixed at 8 and `p1=0` |
| SCRAMBLE | MIN SIZE, MAX SIZE, LENGTH | Extend Scramble mode 1 segment limits and output duration |
| DOUBLETS | SEGMENT, REPEATS | `segdur`, `repets`; `-s` splice enabled |
| MOTOR | INNER, OUTER, INNER SIZE, VARIATION | inner `freq`, outer `pulse`, `fratio`; VARIATION jointly drives bounded `-f/-p/-j` |
| GREV | MODE, WINDOW, GROUP, TROUGH | modes 1–5, `wsiz`, `gpcnt`, `trof`; safe fixed repeat/keep/stretch values per mode |
| TIMEWARP | RATIO, BUFFER, GATE, HOLE | timestretch ratio plus `-b`, `-l`, `-h` |
| TELESCOPE | GROUP, SKIP, SHAPE | `cyclecnt`, `-s`, optional average-cycle `-a` |
| FREEZE | POSITION, SIZE, REPEATS, DRIFT | start/end, repetitions; DRIFT jointly drives delay randomization and pitch scatter; gain 1 |
| ITERATE | REPEATS, GAP, PITCH, FADE | mode 2 repetitions plus `-d`, `-p`, `-f` |
| GLISTEN | DIVIDE, HOLD, SHIFT, SCATTER | group division, set duration, `-p`; SCATTER maps to `-d=x`, `-v=x²` |
| SPEC SMEAR | BLUR | spectral `blur` window count, bounded to available analysis frames |
| WAVE SCRAMBLE | MODE, GROUP, PITCH, DECAY | modes 1–4, `-c`, `-t`, `-a`; modes 1–2 use input duration |
| BRASSAGE | GRAIN, DENSITY, PITCH, SPREAD | mode 6 grain size (ms), density, pitch, stereo position; velocity/gain 1 and 10 ms splices |
| FRACTAL | SCALE, SHEEN | integer scaling and loudness; bounded pre-attenuation follows sheen |
| INTERPOLATE | STRETCH, SKIP | integer multiplier and `-s` |
| OMIT | OMIT, GROUP | A cycles omitted out of every B, with A clamped below B |
| REPLACE | GROUP, SKIP | strongest-cycle `cyclecnt` and `-s` |
| PITCH | RANGE, GROUP, SKIP | octave variation, `-c`, `-s` |
| SHUFFLE | PATTERN, GROUP, SKIP | four compiled domain-image strings plus `-c`, `-s` |
| REFORM | SHAPE | validated modes 2, 4, 6, and 7 only |
| DISTSHIFT | MODE, GROUP, SHIFT | mode 1 shift or mode 2 swap; SHIFT omitted in swap mode |
| SEGZIG | REPEATS, SHRINK, PORTION, CURVE | mode 2 repetitions, `-s` ms, `-p`, optional logarithmic `-l` |
| OVERLOAD | MODE, THRESH, DEPTH, FREQ | mode 1 clip or mode 2 pulse; frequency emitted only for mode 2 |
| FILTER BANK | STRUCTURE, WIDTH, Q, STRENGTH | bank modes 1–3; harmonic modes anchor `lof` to root, subharmonic mode anchors `hif`; 100 ms tail |
| GRANULATE | DENSITY | Brassage mode 5, deliberately leaving inter-grain space at values below 1 |

Advanced breakpoint/datafile forms are intentionally hidden except for STUTTER's
internally generated cut list. The fixed values above are part of recipe provenance,
not secret secondary UI pages.

## GLISTEN pipeline and determinism

The supplied `dev/new/glisten.c` documents:

```text
glisten glisten inf outf grpdiv setdur [-ppitchshift] [-ddurrand] [-vdivrand]
```

TapeSister builds these argv arrays with a fixed 1024-point, overlap-3 analysis:

```text
pvoc anal 1 input.wav input.ana -c1024 -o3
glisten glisten input.ana effect.ana DIVIDE HOLD -pSHIFT -dSCATTER -vSCATTER^2
pvoc synth effect.ana output.wav
```

Divide is restricted to 2, 4, 8, 16, 32, or 64, all valid divisions for this analysis
layout. Hold is integral analysis windows; its UI also shows calculated milliseconds.
Shift is CDP's symmetric random semitone range, not a directional pitch promise.
Scatter maps linearly to duration randomness and quadratically to division-size
randomness so low values remain ordered.

GLISTEN calls `drand48()` but exposes no seed argument. It therefore declares no seed
support and no determinism guarantee. TapeSister sends the documented pitch value but
does not patch CDP semantics. PVOC padding can change result length, so GLISTEN disables
Mix and retains its complete natural-length result.

## Runtime discovery and dependency closure

Standard native release builds stage a pinned CDP8 runtime in `cdp/bin` beside
TapeSister. Runtime discovery checks:

1. **CDP BIN PATH** (`[Paths] CdpBinPath` in `tapesister.ini`);
2. `cdp/bin` beside the TapeSister executable;
3. `cdp` beside the executable; and
4. `./cdp/bin` for development.

The folder is canonicalized. Each selected recipe then checks its own stage executable
and reports the exact missing name. TapeSister never edits `PATH` or another global
environment variable.

The current 32-recipe catalog requires this curated executable set:

```text
blur distmore distort distshift extend filter freeze glisten grain hover
modify motor pvoc scramble sorter splinter stutter
```

The TapeSister CMake release path fetches the exact commit recorded in
`cmake/CDP8Manifest.cmake`, builds this closure, and ships its corresponding source and
license. The upstream build links the required programs to CDP's `cdp2k`,
`sfsys/newsfsys`, and `pvxio2` support libraries plus the platform math/system
dependencies selected by CDP.
PortAudio playback/recording utilities are not required. Build the compatible upstream
tree, then copy the 17 programs and their actual dynamic-library closure:

```bash
cmake -S CDP8-main -B cdp-build -DCMAKE_BUILD_TYPE=Release -DUSE_LOCAL_PORTAUDIO=OFF
cmake --build cdp-build --target blur distmore distort distshift extend filter freeze \
  glisten grain hover modify motor pvoc scramble sorter splinter stutter -j2
```

On Windows, use an MSYS2 UCRT64 toolchain with CMake and Ninja or Make. The bundle step
places the native `.exe` files in `cdp/bin` beside `tapesister.exe`; the same build's
runtime DLLs remain beside TapeSister. Windows path quoting uses
CreateProcess argument quoting and a kill-on-close Job Object. A Windows compile is not
reported as runtime execution unless the programs were actually run there.

## Argument, process, and temporary-file safety

Each request snapshots tile identity/audio hash, frame count, tile-local half-open
selection, scope, recipe/schema versions, quantized controls, Mix, supported seed,
tuning used by FILTER BANK, sample rate/channels, stage contract, and job/render
generation. A single worker owns an input copy and exports it into a unique mode-0700
job directory. STUTTER's cut-time list is generated there from its curated CHUNK value.

Every stage uses a canonical executable path plus an argv array—never a shell. Factory
output names are checked for separators/traversal, and every required intermediate and
final output must be a regular file inside the job directory before the next stage.
Stdout/stderr are captured per stage, nonzero exit codes fail, and textual `ERROR:` also
fails even when an executable returns zero.

The adapter's default timeout is 120 seconds. Cancellation terminates and reaps a POSIX
process group or Windows Job Object. Successful, failed, timed-out, cancelled, and stale
jobs are cleaned. Diagnostics are copied into memory before cleanup. Rapid requests use
newest-request-wins; no two jobs share filenames or intermediates.

The final WAV must be regular, nonempty PCM/float RIFF/WAVE at the input sample rate,
with the recipe's expected mono or stereo channel count and a bounded frame count.
BRASSAGE mode 6 declares stereo output, which the existing loader safely downmixes into
TapeSister's owned mono sample memory. Float channels are scanned for NaN/Inf before
import. Peak, DC, clipping, silence, channel, and length checks produce
SAFE/HOT/SILENT/INVALID status. The adapter never blindly normalizes.

## Selection, preview, duration, tuning, and Undo

The Transform mini waveform edits the active tile's authoritative selection and
viewport. There is no copied Transform selection. Scope switching retains the stored
range, and page/workspace changes never invoke Show All.

Selection input is an owned copy of `[first,last)`. A preview is immutable owned memory
tied to the complete identity above. A stale worker cannot publish after a newer job,
selection/scope/control/Mix/tuning change, edit, Undo/Redo, Capture replacement, tile
switch, clear, workspace close, or shutdown. Playback and Capture continue on existing
audio while CDP works.

Apply promotes the already-auditioned preview; it never reruns a random process. The
replacement path preserves outside audio, applies only the established short
inside-boundary splice, accepts the wet result's natural duration, and selects exactly
`[old_first, old_first + rendered_frames)`. Canvas and later audio expand/contract or
shift as required. Same-length edits retain the viewport; length changes minimally
bound it without Show All. The result retains tile identity, metadata, audible tuning,
and keyboard mapping.

The accepted complete result becomes one immutable `TS_POST_MATERIAL_REPLACE`
checkpoint replayed before the live native process stage. Transform input comes from
Current, so it already contains the BODY/EDGE/DRIFT and native-effect settings that
were audible at render time. Apply therefore restarts the new live native stage at its
neutral values: the accepted preview is reproduced exactly once rather than processing
those baked settings twice. Later native-control changes always rebuild from the stable
checkpoint, so the transformed region cannot mask BODY, EDGE, DRIFT, NOISE, SHAPE,
DELAY, or SPACE and A→B→A changes remain deterministic. Undo retains the prior full
edit/process graph; Redo restores the checkpoint and neutral live stage. TSR27 stores
this ordering explicitly while TSR6–TSR20 remain loadable.

Left-click quick Apply uses the identical render → validate → immutable preview → Apply
path, but commits immediately after validation instead of displaying the workspace.
Both paths create one tile-local Undo transaction. Render, audition, page changes,
selection movement, parameter editing, Save/Update, failures, and cancellation create
no audio history.

Universal Mix is recipe-declared. Exact-frame Mix never truncates, stretches, or
approximately aligns audio. Duration-changing or latency-uncertain recipes disable it.

## Licensing

CDP is LGPL-2.1-or-later, copyright Trevor Wishart and Composers Desktop Project Ltd.
TapeSister's bundle step provides the upstream license, exact corresponding source
archive, pinned revision, build instructions, dependency notices, and an explicit
record that no local CDP modifications are applied. See `THIRD_PARTY_NOTICES.md` and
`docs/CDP8_RUNTIME.md`. CDP remains a set of separate child-process executables; this
does not turn TapeSister into a general CDP frontend.
