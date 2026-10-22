# Mosaic project portability

## Failure and fix

A supplied Windows-saved project failed on Linux with `Invalid or incomplete
Mosaic project data`. Its `mosaic.tsm` used CRLF line endings. The loader compared
the header against an exact LF-terminated string, so it rejected the file before
reading the otherwise valid arrangement.

The loader now reads in binary mode and normalizes a terminal CRLF on the header
before the existing strict version check. Subsequent records already parse
whitespace. New saves use binary mode to emit LF consistently on Windows and
Linux. The file format version and project content are unchanged.

## Verification

- The new CRLF regression failed with the original loader.
- Storage tests load LF and CRLF variants of the current format and existing
  version 1–3 and 6–8 fixtures, checking identical arrangement hashes.
- A binary read verifies new saves contain no CR bytes, including on Windows.
- Invalid header suffixes, unsupported versions, and malformed arrangement data
  still fail without replacing the current arrangement.
- The full project transaction test loads a CRLF Mosaic file and checks both the
  arrangement and sample hashes. Missing Mosaic data still fails transactionally.
- The supplied bundle opens with the fixed loader: four pages, 96 Mosaic cards,
  13 sources, and the saved Sister state. Its repaired LF copy opens with the
  original loader and produces the same Mosaic hash, `1843471133942785825`.
- The repaired archive retains all 82 members; comparison of decompressed data
  confirms only Mosaic CRLF-to-LF normalization changed. No user project or audio
  data is included in this repository.
- Linux application startup passed with an 8 MiB stack.
- All four targeted Linux suites passed: Mosaic storage/DSP, Mosaic controller,
  sample pages, and tile EQ.

## Wide Auto-Space layouts (October 21, 2026)

`ManyMosaicCards-01` reproduced a different load rejection. All 1,020 cards had
been aligned to time zero with Auto-Space enabled. Their horizontal positions
range from 11,862.697758066004 to 110,419.10183441937; 111 cards exceed the old
reader's 100,000-unit bound. The editor and writer allowed those positions, so
the saved project was valid editor output that the reader refused. Both the
loader before staged project opening and the staged loader reproduced the error.

Horizontal positions now use a shared 1,000,000,000-unit numerical guard in
storage, source drops, nudging and panning. Saved view offsets and horizontal fit
scales support the expanded range. This does not extend time or change audio.
Save checks horizontal positions/views before opening the output file, preserving
an earlier good save if invalid geometry reaches the numerical guard. Negative,
nonfinite and out-of-range coordinates remain invalid. Format 12 is unchanged;
older builds still reject wide arrangements.

Verification:

- The supplied archive opens through the actual `load_instrument` path twice,
  including replacement of a populated project: 1,020 cards, 10 sources,
  unchanged maximum position and Mosaic hash `5617727719280061890` both times.
  The loaded arrangement produces finite, nonzero audio and renders in the UI.
- All 27 file members extracted from the original archive remain byte-identical.
  No project repair or user audio is included in the repository.
- A generated 1,024-card row using maximum-width legacy cards crosses one million
  units and round-trips exactly, including its saved far view, LF and CRLF files.
  Guard-edge coordinates/fit views round-trip; invalid coordinate saves preserve
  the previous file. Wide selections nudge by the requested amount without
  jumping back to the former boundary.
- Linux application build and all three affected suites pass: Mosaic model/storage,
  Mosaic controller and project Open/reset.

To reproduce an external project through the application's Open path:

```sh
./build/tapesister_new_project_tests --open-project /path/to/project/project.tsr
```

## Linux test compilation (October 22, 2026)

Native release run #220 stopped while compiling the embedded TrackSister test:
`lstat` and `kill` were implicitly declared. Three controller fixtures include
headers for their call-interposition helpers before including `main_sdl.c`.
That makes the application's `_POSIX_C_SOURCE` definition too late for libc's
feature selection, hiding declarations used by the offline renderer.

The embedded TrackSister, keyboard HOLD and project Open fixtures now request
POSIX.1-2008 before their first header, with the same non-Windows guard as the
application. Checking all three with strict C11 and
`-Werror=implicit-function-declaration` reproduced the errors before the fix and
passes afterward. Their Release targets rebuild successfully, and all four
affected tests pass: keyboard HOLD, project Open/reset, and both ordinary and
realtime embedded TrackSister runs.

## Source actions and reported mute behavior (October 22, 2026)

Source cards now support double-left-click to open the canvas editor, right-click
to select their placed instances, and A to toggle a source preview. A selected
timeline instance takes priority. Source previews use a transient event, without
changing arrangement cards, history, playback position or saved data. The UI
retains preview audio through the stop fade and releases it after the callback
has retired the preview; the callback never clears a pointer the UI collector
is inspecting.

Controller checks cover source/instance audition priority and pitch, repeated A,
right-click group selection and primary preservation, bank/event-version editor
opening, continued arrangement playback, and modified double clicks. Model checks
cover source-only preview retention, stop/completion and project replacement.
The Mosaic model/controller, project Open/reset and offline-render suites pass.
The changed Mosaic module and model tests also pass AddressSanitizer/UBSan
(leak detection disabled in the traced container).

Persistent silence after Shift+M has **not been reproduced**. Reopening the
same project was an attempt to recover sound; the user had not changed card
positions, so a separate data-revert failure has not been established. Added
coverage verifies:

- Reopening the active TSR through the pending Load operation restores changed
  card positions, mute, gain and copies, resets history and stops transport.
- A 1,020-card, two-source arrangement handles Shift+M, Ctrl+A, Shift+M, Shift+M
  with actual routed audio, then Undo/Redo. Audio resumes after unmuting.
- The supplied 1,020-card project reopens twice to the same saved Mosaic hash
  `5617727719280061890`. Its actual callback and saved Matrix/effects produce
  audio after the same sequence, including 800 buffers (about 8.5 seconds) fully
  muted across a loop wrap. Effects can continue producing tails while muted.

These are Linux reproductions, not confirmation of the reported Windows behavior.
The supplied project is read only; no user samples are committed. Reproduce with:

```sh
./build/tapesister_mosaic_controller_tests --source-actions
./build/tapesister_new_project_tests --mosaic-revert-mute /path/to/project/project.tsr
```

The subsequent report of an unmoving playhead, ineffective stop/start, and
partial UI responsiveness is still unresolved. A new test drives the production
ASIO adapter from a separate thread using the fixture driver. It renders 1,020
cards while actual controller keydown/keyup events repeat the group-mute sequence
three times, including more than a second muted across a loop wrap. The clock
advances in every stage, sound resumes, and UI lock nesting returns to zero after
each key action. This fixture does not reproduce a physical MOTU driver failure.

Audio Health now reports the actual last callback completion age, current UI
lock depth/hold/site, Mosaic time/range, and total muted/solo cards. Opening the
window after a stall no longer makes an old callback count look newly active.
Session logs retain progress once per second, provided UI service continues.
The Linux application build and diagnostics, Health, and audio reconfiguration
suites pass, including the new threaded test. These changes improve diagnosis;
they do not establish or repair the cause of the reported freeze.
