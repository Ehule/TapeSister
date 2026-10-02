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
