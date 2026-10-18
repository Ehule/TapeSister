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
