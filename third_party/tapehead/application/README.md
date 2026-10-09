# Embedded TapeHead application

This directory contains the complete `src/` tree and code license from
`Ehule/FT2-Tapehead-Edition` at commit
`f053d96df3996a5fd3b26625f069888c3a7d24ab`. `SOURCES.json` records SHA-256
checksums of every original input. `embedded.patch` records every local source
adaptation. TapeHead's repository is read only during import and verification.

Builds compile these committed sources without fetching TapeHead. Reproduce
them with an upstream checkout containing the pinned inputs:

```sh
python3 scripts/import-tapehead-application.py /path/to/TapeHead --check
```

Omit `--check` to refresh the vendored sources after verifying the same pinned
inputs and applying the patch with zero fuzz. Source or patch changes must be
recorded explicitly; the importer does not accept a different upstream revision.

## Host boundary

`cmake/TapeHeadEmbedded.cmake` compiles the original editor, input dispatch,
clipboard, undo, FastTracks integration, replayer, graphics and support modules.
The standalone `ft2_main.c`, Live Link/companion entry points and duplicate
FastTracks core translation unit are excluded. TapeSister already compiles the
identical pinned FastTracks core. Four overlapping drawing symbols are
namespaced with target-local definitions. The older drawing extraction is
retained only for the legacy native tracker fixture.

`src/ts_tapehead_embed.c` is the lifecycle, score and tile adapter.
`src/main_sdl_tracker_embed.inc` connects F10 and the host event loop. TapeSister
owns the window, SDL initialization, audio/MIDI devices, routing, recording,
dialogs and project files. The original main loop, device initialization,
scope thread and standalone configuration writes are disabled. Scopes tick on
the host UI thread. Host controls retain input ownership while overlays are
open; focus loss releases only manual notes owned by the embedded workspace.

Original sample aliases bind stable host tile IDs, with 128 usable instruments.
Float stereo tile snapshots feed original voices, envelopes, effects and pan
through the host's interpolation/loop reader. Sounding snapshots remain alive
until the original voices release them; replacements become available on the
next trigger. Tile tuning compensates for original instrument tuning
quantization. The adapter mixes fixed-size spans without allocating its audio
buffers in the callback. The original REC+ path can allocate a newly generated
pattern during replay; this inherited path is not claimed to be allocation-free.
The TRACK source goes through the existing Router and final output chain.
Capture uses that audible host output and original block-loop tick boundaries.

Disk operations redirect to host project Open/Save; sample-library screens
redirect to the canvas. Configuration retains tracker recording/layout options
and the original palette editor, with host Audio Health as a separate action.
Trim becomes Router. Standalone sampler, hardware and module/audio storage are
not exposed. A future buffer tile can use the same host tile boundary.

## Saved score

The version-2 host tracker codec retains raw original note, instrument, volume,
FX and tuning fields for all 256 rows, including hidden rows. The bounded wire
extension also stores pattern identities/order, editor position, song-wide
LEN/CONTROL, FastTracks state, mute/trim, STEP/octave, colors and view choice.
The `STH2` payload adds Forward/Reverse/Bounce directions and a validated
128-byte preference tail containing recording/layout options and the custom
palette. Older `STH1` payloads load without that tail. Explicit Save defaults
persists the same preference representation in the host configuration folder.
It stores no pointers, tile audio or running voices. It is validated before
import and matched against stable native pattern identities. Version-1 native
scores migrate when their pitches, tempo and aliases fit original limits.
Zero speed is retained as an original editor value; restart uses a safe
nonzero initial speed. Loaded projects start stopped with clean undo/clipboard.

## Adaptations and validation

The patch changes host ownership and tile reads, redirects menus, preserves the
requested current-cell Backspace behavior, adds Ctrl+Alt+Backspace full view,
and fixes unaligned original help/scoped-sample reads. Original code otherwise
provides editing and playback behavior. Standalone support modules are retained
for source completeness even when their screens are inaccessible.

The embedded Follow toggle separates manual editing from transport position.
Follow-off navigation and note edits leave the running row/tick untouched; mouse
selection anchors also remain separate from the edit cursor. The host keeps this
state in `src/ts_tapehead_follow.inc`, reusing the project's existing Follow field.

`tapesister_tapehead_embedded_tests` runs the actual host event and audio paths:
F10, original editing/undo, block clipboard/extraction, MIDI, STEP/octave,
Song/Pattern transport, volume/FX commands, LEN/CONTROL, float stereo, exact
live-cycle capture, tile replacement/move/deletion, rate changes and project
roundtrip. Follow-up coverage exercises visible Config, palette edits, persisted
defaults, Silent Record audio, IPL/INP/REC+, ratio/LEN wheels and Bounce through
the callback. See `docs/PR124_AUDIT.md` and `docs/SISTERTRACKER_PARITY.md` for
verification scope.

Code is BSD-3-Clause (see `LICENSE` and `src/LICENSE.txt`). Font artwork retains
its original CC BY-NC-SA 4.0 terms in the parent `FONTS-LICENSE.txt`. Other
embedded notices, including stb, remain in their source files. Binary bundles
carry the licenses, manifest and adaptation patch under `licenses/tapehead`.

Module import also uses the bundled MOD/SoundTracker and lossy IT converters.
The host preflights offsets, packed patterns and supported dimensions before
staging tiles. The IT patch bounds compressed bitstream reads and propagates
decoding failures; the compatibility warning is shown in the host load UI.
