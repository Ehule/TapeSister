# Curated CDP Transform architecture

## Product boundary

CDP is an offline backend for TapeSister's Transform stage. It is not called from the
audio callback, Capture recorder, or SDL event handler. The UI exposes a curated
recipe, four fixed musical controls, and one universal Mix—not raw programs, shell
text, breakpoint editing, or a process graph. The fixed control positions and single
active algorithm follow the direct-interaction lesson of SOMA Laboratory WARP without
copying its branding, artwork, algorithms, or panel design.

`TsCdpRecipe` is the versioned factory schema. A recipe declares identity, schema,
recipe, and provenance versions; category; typed stage connections and executable
requirements; analysis configuration; required input and expected output channels;
sample-rate preservation; safety, duration, determinism, and Mix policies; and exactly
four control specifications.
The lower **CDP** bank has the same 16-tile footprint as TapeSister's other banks.
Tile 01 displays `GLIST` and opens the shared Transform workspace with GLISTEN active
and its four controls in the fixed control positions. Empty CDP tiles are inert and
visibly labeled; no placeholder claims to work. The existing factory and user process
recipes live unchanged in the separate **DSP** bank. Future curated CDP-bank recipes
may use a validated CDP8 pipeline or a native C implementation, but must retain the
same safe render/preview/Apply contract. Adding another CDP8 recipe means adding a
validated immutable schema entry, a typed mapping function, argument-plan tests,
minimum-input validation, a declared duration/Mix policy, and adapter/output tests.
User-supplied executable names or command text are not accepted.

## GLISTEN contract verified against supplied CDP8 source

The supplied `dev/new/glisten.c` identifies itself as CDP 7.1.0 and documents this
process form:

```text
glisten glisten inf outf grpdiv setdur [-ppitchshift] [-ddurrand] [-vdivrand]
```

TapeSister uses a 1024-point, overlap-3 standard PVOC analysis and builds three argv
arrays (never a shell command):

```text
pvoc anal 1 input.wav input.ana -c1024 -o3
glisten glisten input.ana glisten.ana DIVIDE HOLD -pSHIFT -dSCATTER -vSCATTER^2
pvoc synth glisten.ana output.wav
```

CDP validates group division from 2 through the analysis channel count minus one and
requires a usable divisor. TapeSister exposes only 2, 4, 8, 16, 32, and 64 for its
fixed 1024-point analysis. Hold is an integer 1–128 subset of CDP's valid 1–1024 window
range. Shift is clamped to CDP's documented 0–12 semitone random range. Scatter stays
inside `[0,1]`; its duration value is linear while division-size randomness is squared
so low settings retain recognizable grouping.

An actual Linux CDP 7.1.0 run produced 48,896 output frames from 48,000 input frames.
That PVOC padding means GLISTEN is declared duration-changing. Its universal Mix
position remains visible but is disabled: TapeSister keeps the complete natural-length
wet result rather than guessing at analysis latency, truncating it, or stretching dry
audio. The general exact-frame Mix implementation remains available for future
duration-preserving recipes.

GLISTEN calls `drand48()` but exposes no seed argument, so the recipe truthfully
declares no seed support and makes no determinism guarantee. The supplied 7.1.0 source
also contains an apparent pitch line that subtracts the newly generated value from
itself before conversion. TapeSister sends the documented `-p` value but does not patch
CDP semantics; runtime distributors should verify the audible Shift behavior of the
specific CDP build they ship.

## Runtime and minimum dependency closure

No CDP source or binary is vendored by PR-29. Runtime discovery checks, in order:

1. **CDP BIN PATH** in TapeSister's Configuration screen (`[Paths] CdpBinPath` in
   `tapesister.ini`);
2. `cdp/bin` beside the TapeSister executable;
3. `cdp` beside the executable; and
4. `./cdp/bin` for development.

The selected folder must contain executable `pvoc` and `glisten` files (`.exe` on
Windows). Paths are canonicalized before a worker starts. TapeSister does not use or
change PATH or any global environment variable.

The supplied CDP CMake targets show the minimum link closure for both offline tools:

- `pvoc` (analysis and synthesis), including its FFT/PVOC sources;
- `glisten`;
- CDP's `cdp2k` support library;
- `newsfsys/sfsys` sound-file library;
- `pvxio2` analysis-file library; and
- the platform math/system libraries selected by CDP.

PortAudio playback/recording programs are not required. The complete upstream tree can
be built reproducibly with CMake and only the two runtime executables plus their actual
dynamic-library dependencies copied into TapeSister's runtime folder:

```bash
cmake -S CDP8-main -B cdp-build -DCMAKE_BUILD_TYPE=Release -DUSE_LOCAL_PORTAUDIO=OFF
cmake --build cdp-build --target pvoc glisten -j2
```

For Windows, use an MSYS2 UCRT64 shell with its UCRT64 GCC, CMake, and Make/Ninja
packages, run the same configure/build commands with an MSYS Makefiles or Ninja
generator, and place `pvoc.exe`, `glisten.exe`, and any DLLs reported by
`objdump -p`/`ldd` in `cdp/bin` beside `tapesister.exe`. This repository does not
download CDP during a normal TapeSister build. Linux execution and Windows execution
must be reported separately; compiling a path does not count as testing it.

## Process and temporary-file safety

Each request snapshots the tile slot, quantized audio hash/revision, frame count,
selection, scope, recipe/schema versions, four mapped controls, Mix, supported seed
state, sample rate/channels, expected stage count/output type, and job/render
generations. The worker exports an owned mono WAV into a unique mode-0700
job folder below the system temporary directory. Only fixed TapeSister filenames are
used. Each process receives an executable path and argv array; stdout and stderr go to
separate per-stage logs, exit status is checked, and textual `ERROR:` is treated as a
failure even with exit code zero. Each expected analysis intermediate must be a regular
file in that job folder before the following stage may start.

The adapter has a 120-second default timeout. Cancellation terminates the process
group on POSIX and a kill-on-close Job Object on Windows, then reaps it. Successful,
failed, timed-out, and cancelled jobs are cleaned. Diagnostics are copied into memory
before cleanup; cleanup failure is surfaced and never makes an invalid render
applicable. Rapid Render requests cancel the old worker and queue only the newest
request.

The final output must be a regular, nonempty mono PCM/float RIFF/WAVE in the isolated
folder, at the input sample rate and within TapeSister's frame limits. Float WAVs are
scanned for NaN/Inf before import. Imported audio is TapeSister-owned memory; preview
does not depend on the temporary file. Peak, DC, clipping, silence, channels, and frame
count produce the compact SAFE/HOT/SILENT/INVALID status. TapeSister does not blindly
normalize a CDP render.

## Selection, preview, Mix, and Undo

The Transform mini waveform directly reads and edits `TsInstrument`'s authoritative
tile-local half-open selection and viewport. There is no copied Transform selection.
Scope switching never clears the stored selection, and entry/exit never invokes Show
All. Moving or resizing selection creates no audio history.

Selection render input is an owned copy of `[first,last)`. Preview is an immutable
owned sample tied to the complete render identity. A stale worker cannot publish after
another request, tile/selection/scope/control/Mix change, edit, Undo/Redo, Capture
replacement, tile switch, workspace close, or shutdown.

Apply stores the preview in the active tile's existing owned audio-patch graph and
replaces `[first,last)` at its natural result length. Audio before the range stays
bit-identical; audio after it stays bit-identical at its shifted position. A short
roughly 1 ms boundary splice is applied inside the replacement only. The new selection
is exactly `[first, first + rendered_frames)`. Same-length edits retain the viewport;
length changes map and minimally bound it without a silent Show All. The existing edit
snapshot captures audio, canvas, selection, viewport, loop, metadata, and graph state,
so Apply is one Undo/Redo transaction while Render, audition, scope, and controls are
history-free.

Universal Mix support is recipe-declared. The exact-frame policy makes Mix 0 clone dry
and Mix 100 clone wet; an intermediate value is rejected if dry and wet frame counts
differ, so neither side is silently truncated, stretched, or approximately
latency-aligned. GLISTEN uses the duration-changing policy described above and therefore
disables Mix. A future duration-changing recipe must do the same or implement and test
a recipe-specific alignment strategy.

## Save/load and licensing

Applied sound is stored through the existing TSR audio-patch representation. No job
file, preview buffer, or alternate audio owner is serialized, and older TSR files need
no migration. Recipe provenance remains in the render identity and diagnostics for
this vertical slice; persisted structured provenance is deferred rather than changing
the stable TSR format casually.

CDP is LGPL-2.1-or-later, copyright Trevor Wishart and Composers Desktop Project Ltd.
Anyone distributing CDP binaries with TapeSister must ship the applicable notices,
license text, corresponding source or a compliant written/source offer, build scripts,
and notices for modifications and dynamically linked dependencies. See
`THIRD_PARTY_NOTICES.md`. TapeSister itself does not become a CDP command shell and this
PR does not redistribute the supplied CDP or SoundThread archives.
