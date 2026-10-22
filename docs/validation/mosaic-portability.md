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
