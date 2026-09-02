# PR12 Windows packaging handoff

PR11 deliberately does not create an installer or distribution archive. This document
records the current reproducible boundary so PR12 can focus on dependency acquisition,
portable layout, and friend-friendly delivery.

## Known build contract

- Tested user environment before PR11: 64-bit Windows and Linux recording sessions.
- Documented Windows toolchain: MSYS2 **UCRT64**, 64-bit GCC/MinGW-w64, CMake ≥3.16,
  Ninja, and the UCRT64 SDL2 development package. PR11's managed host did not provide
  a Windows compiler or SDL2, so a fresh Windows build remains required.
- C is C11. RtMidi enables C++11. MIDI uses WinMM on Windows.
- CMake switches: `TAPESISTER_BUILD_SDL` (default ON) and
  `TAPESISTER_ENABLE_MIDI` (default ON).
- Make entry points are `make`, `make test`, `make stress-sister`, and
  `make benchmark-sister`. CMake/CTest is the portable release entry point.
- Expected executable: `tapesister.exe`. MinGW reserves a 16 MiB stack.

Fresh MSYS2 UCRT64 build:

```bash
bash build.sh
```

The shared wrapper verifies that it is running in UCRT64, supplies the Ninja and
Release configuration, enables the bundled pinned CDP8 runtime, and targets only the
application and its required dependencies. `SDL_MAIN_HANDLED` is supplied by the
CMake target itself rather than as a user-provided compiler flag.

For certification, configure a second tree without limiting the target, build it,
then run `ctest --test-dir build-windows --output-on-failure`. A Debug tree changes
optimization/debug information only; it is not a callback-performance reference.

## Current portable layout

```text
tapesister.exe
SDL2.dll
libgcc_s_<toolchain>.dll
libstdc++-6.dll          # when MIDI/RtMidi is enabled
libwinpthread-1.dll      # when MIDI/RtMidi is enabled
assets/
  tapesister_splash.png
  palette.pal
  tapesister_welcome.wav # optional when present in the source tree
```

CMake already copies the matching DLLs and assets beside the build-tree executable.
Keep the executable, all copied DLLs, and `assets` directory together. No external font
file is required. Named Sister presets are written to `sister-presets.ini` beside the
resolved configuration file; they are user data, not a required factory asset.
`tapesister.ini.example` is the configuration template and should accompany a package
as documentation, not silently overwrite an existing user configuration.

This historical PR11 handoff predates the shippable CDP8 runtime. Current release
builds enable `TAPESISTER_BUNDLE_CDP8`, stage the pinned Windows programs under
`cdp/bin`, and include their license and exact corresponding source archive. See
`docs/CDP8_RUNTIME.md` for the current packaging contract. `CdpBinPath` remains an
explicit developer override.

## Configuration and writable data

Configuration resolves from `TAPESISTER_CONFIG`, then `tapesister.ini` in the current
directory, beside the executable, or in its parent. The app writes configuration on
exit. Palette search uses `TAPESISTER_PALETTE`, the config/exchange/current/executable
locations, then the packaged asset. Captures use `TAPESISTER_CAPTURES` or a `Captures`
directory under the current working directory. Saving a TSR creates one named project
folder containing the TSR, its manifest and state, internal page data, and extractable
per-tile WAV files.

This works in a writable portable folder but is unsuitable for a conventional
read-only Program Files install. PR12 must choose and document a per-user writable
config/preset/capture location, migration behavior, portable-mode override, and errors
for unwritable project folders.

## Repository audit

- No runtime developer-machine absolute path was found. `/home/user/...` occurs only
  in renderer test fixtures. CDP temporary work honors the platform temporary path.
- Runtime assets are referenced with the case shown above; packaging must preserve it.
- Do not ship build trees, object/test executables, generated captures, local INI files,
  or saved project folders as application assets.
- Shell-specific build lines belong to developer documentation; the packaged program
  must launch from Explorer without an MSYS2 `PATH`.

## Notices

Ship the repository license plus notices/licenses for SDL2, vendored RtMidi, stb, and
every runtime redistributed by the chosen MinGW/MSYS2 packaging policy. If CDP or any
CDP process is bundled, review and include its separate license/notices. PR12 should
produce one consolidated third-party notice and record exact dependency versions and
download sources.

## Package smoke test

1. Build from a clean clone with the Release command above.
2. Copy only the declared portable layout to a directory outside MSYS2.
3. Launch `tapesister.exe` from Explorer with no UCRT64 terminal and no developer PATH.
4. Verify splash/palette/welcome lookup, create and reload configuration/presets, and
   confirm Captures and a complete TSR project folder are created in documented writable locations.
5. Exercise WAV/TSR/TSP load/save, mono/stereo input, FM, QWERTY, MIDI/WinMM, Sister
   POWER/ROLL/HOLD/CLEAR, a multi-tile ensemble, effects, Capture, and device restart.
6. Run the short PR11 sound-check checklist and inspect `--diagnostic-audio` output.
7. Repeat on a clean 64-bit Windows machine without MSYS2 installed.

## PR12 blockers and decisions

1. Establish a pinned, automated dependency/toolchain acquisition path.
2. Select installer versus signed portable archive (or both), version metadata, icon,
   signing, uninstall/upgrade behavior, and release checksums.
3. Move default writable data out of an installed read-only directory while retaining
   an explicit portable mode and existing environment overrides.
4. Generate and ship complete third-party notices and confirm runtime redistribution.
5. Add clean Windows CI for Release build, CTest, dependency/layout validation, and an
   Explorer-style launch smoke test.
6. Validate 44.1/48/96 kHz and practical 128/256/512/1024 buffers on the target Windows
   hardware; do not derive release claims from Debug/sanitizer timing.
