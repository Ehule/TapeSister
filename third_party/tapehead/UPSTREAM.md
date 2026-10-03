Tapehead source extraction
==========================

Source: https://github.com/Ehule/FT2-Tapehead-Edition
Commit: f053d96df3996a5fd3b26625f069888c3a7d24ab
License: BSD 3-Clause (CODE-LICENSE.txt), except the font artwork below.

This directory contains code compiled by SisterTracker, not just a description
of equivalent behavior. The earlier native drawing recreation is superseded.

Compiled source
---------------

* `pattern_draw.inc`: extracted functions and tables from `ft2_pattern_draw.c`.
  Includes `writePattern`, `drawAdaptiveCell`, `writeCursor`,
  `patternXToCursorObject`, `drawRowNums`, `drawTrackLengthStatus`,
  `drawFastTracksPOCStatus`, `dimPatternColor`, `breatheColorToward`,
  direct pixel lines, playhead outlines and medium-note/character routines.
* `tiny_draw.inc`: original `ft2_gui.c` tiny-text renderer and
  `ft2_transport_visuals.h` colon wrapper, unchanged.
* `transport_visuals.h`: the original independent-transport predicate and
  page-start function, unchanged. Both input mapping and drawing call them.
* `ft2_fasttracks_core.c` / `.h`: complete files, byte-for-byte unchanged.
  The audio path calls their rational clock, shared-boundary decision and
  blank-extension predicate. The former local clock loop has been removed.

Reproduce and review
--------------------

`SOURCES.json` pins SHA-256 checksums for every input source. Run:

    python scripts/import-tapehead-drawing.py /path/to/TapeHead --check

The importer extracts the named upstream functions and tables, then applies
`pattern_draw.patch` with zero fuzz. Omitting `--check` regenerates the compiled
files. The local patch is the complete, reviewable difference from the extracted
source. Builds use the committed files and need neither Python nor Tapehead.

Host boundary
-------------

`src/ts_tracker_tapehead.c` binds a UI-thread-only render snapshot to the source
renderer: the framebuffer and palette, stable pattern/tile IDs, cell encoding,
transport telemetry and native compact/expanded geometry. Native window chrome,
M/S/trim, custom track names and the editing tools remain in `ts_tracker_ui.inc`.

The patch retains Tapehead's eight-channel, volume-visible layout and removes
unused note-size modes, FT2 coordinate tables, application popup/scrollbar
hooks, and Jog/Punch paths. Native 12-pixel row spacing and a logical edit cursor
are explicit geometry adaptations. The source's glyph positions, single-digit
locator, pulse, ratio badge spacing, placeholders, row numbers, LEN/FT headers,
field shading and moving outlines are now the actual drawing implementation.

Native cell differences are translated at the boundary: MIDI 0-127 with C4=60,
CUT, unresolved aliases, explicit volume zero / FX 000, and pending hex digits.
FT2's selection XOR uses palette indices in pixel alpha; the host uses opaque
RGBA and logical per-lane marks, so selection fill is supplied before the
original glyph/dimming pass. Input uses the original field hit-test. Private
row mapping normalizes a stale audio head after a live LEN edit just as the
source renderer does. Native mute dimming remains a host operation.

The runtime retains immutable sample generations, note readers, output routing,
live recording, literal block audition and native ping-pong traversal. On Play,
native row-zero execution pairs with the upstream clock's first-tick suppression.
Live publication rescales ratio and TPL together before subsequent core ticks.
Song/order traversal, clutches, Jog, Punch and Z commands remain unimplemented;
no active snapshots for unavailable transport modes are fabricated.

Fonts and earlier editing work
------------------------------

Original bitmap font artwork by Magnus "Vogue" Hogdahl, with edits by
Olav Sorensen, is retained under CC BY-NC-SA 4.0 (FONTS-LICENSE.txt).
`scripts/generate-tapehead-tracker-fonts.py` packs those pixels without changing
them. The drawing adapter unpacks the tiny/medium sheets into the source's
original row-major layout; no replacement glyph shapes are introduced.

Block extraction and even-row expand/shrink adapt `ft2_edit.c` and
`ft2_pattern_ed.c` from the same reference under CODE-LICENSE.txt. Independent
marking, Alt+C, Ctrl+L, pending loop-seam bounds and F7/F8 interactions follow
`ft2_keyboard.c` / `ft2_replayer.c`. Those editing operations are native
adaptations, not claimed as direct source transplants. See
`docs/SISTERTRACKER_PARITY.md` for remaining differences.
