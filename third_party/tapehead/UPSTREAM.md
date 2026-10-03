Tapehead tracker fonts and adapted pattern drawing
==================================================

Source: https://github.com/Ehule/FT2-Tapehead-Edition
Commit: f053d96 (2026-10-02 reference checkout).

Original bitmap font artwork by Magnus "Vogue" Hogdahl, with edits by
Olav Sorensen, retained under CC BY-NC-SA 4.0 (FONTS-LICENSE.txt).
The packed glyph representation is generated without changing the pixels.
Normal font width table and glyph drawing are adapted from ft2_tables.c,
ft2_gui.c and ft2_pattern_draw.c under BSD 3-Clause (CODE-LICENSE.txt).
The grid geometry, framebuffer clipping, transport and tile identity are
adapted to TapeSister. No Tapehead replay engine or global application state
is included. See scripts/generate-tapehead-tracker-fonts.py to regenerate.

Block extraction and even-row expand/shrink adapt ft2_edit.c and
ft2_pattern_ed.c from the same reference under CODE-LICENSE.txt. Independent
marking, Alt+C, Ctrl+L, pending loop-seam bounds and F7/F8 interactions follow
ft2_keyboard.c/ft2_replayer.c and the Tapehead manual. Native stable IDs,
immutable sample generations and the live final-output WAV recorder remain
TapeSister implementations. See docs/SISTERTRACKER_PARITY.md for differences.

LEN/CONTROL domain resolution and Pattern FastTracks rational clock arithmetic
adapt ft2_fasttracks_core.c, ft2_fasttracks.c and ft2_replayer.c from f053d96
under CODE-LICENSE.txt. The 17 ratios, normalized phase changes, traversal,
multi-crossing execution and deferred private-CONTROL boundary follow that
reference. Native ping-pong and a starting-row event are explicit adaptations;
Song/order transport, clutches and Z commands are not included. The user-supplied
feature-tapehead-app-icon archive has identical FastTracks/core sources; its
older replayer lacks the newer block-loop implementation retained here.

Transport drawing is adapted directly from ft2_pattern_draw.c and
ft2_transport_visuals.h at f053d96 under CODE-LICENSE.txt: master scrolling,
stationary private pages, logical source-row mapping, dimPatternColor,
breatheColorToward, four playhead palette roles, FT populated-field colors,
CONTROL symbol and lag/sync/lead status LEDs. Native compact/expanded geometry,
M/S/trim controls, logical edit cursor and blank-extension write guards remain
TapeSister adaptations. Song/clutch/jog/punch visuals await those transports;
this port does not represent unavailable audio modes as active.
