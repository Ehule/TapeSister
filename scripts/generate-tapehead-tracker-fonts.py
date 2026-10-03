#!/usr/bin/env python3
"""Pack Tapehead's original font pixels; no runtime BMP loader is needed.

Usage: python scripts/generate-tapehead-tracker-fonts.py /path/to/Tapehead
The source artwork retains CC BY-NC-SA 4.0; see third_party/tapehead/.
"""
import re
import struct
import sys
from pathlib import Path


def read_bmp(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<ii', data, 18)
    assert struct.unpack_from('<HI', data, 28) == (4, 2)  # RLE4
    position = struct.unpack_from('<I', data, 10)[0]
    pixels = [[0] * width for _ in range(height)]
    x = y = 0
    while position < len(data):
        count, value = data[position:position + 2]
        position += 2
        if count:
            values = [(value >> (4 if i % 2 == 0 else 0)) & 15 for i in range(count)]
        elif value == 0:
            x = 0
            y += 1
            continue
        elif value == 1:
            break
        elif value == 2:
            dx, dy = data[position:position + 2]
            position += 2
            x += dx
            y += dy
            continue
        else:
            values = [(data[position + i // 2] >> (4 if i % 2 == 0 else 0)) & 15 for i in range(value)]
            byte_count = (value + 1) // 2
            position += byte_count + byte_count % 2
        for pixel in values:
            pixels[height - 1 - y][x] = pixel
            x += 1
    return pixels


root = Path(sys.argv[1])
output = Path(__file__).resolve().parents[1] / 'src/ts_tracker_fonts.inc'
lines = ['/* Generated from Tapehead f053d96 original fonts, first pattern variant.',
         ' * Artwork: Magnus "Vogue" Hogdahl, edited by Olav Sorensen.',
         ' * CC BY-NC-SA 4.0; see third_party/tapehead/FONTS-LICENSE.txt.',
         ' * Regenerate with scripts/generate-tapehead-tracker-fonts.py. */']
for name, glyph_width, glyph_height, count in [('font1', 8, 10, 128), ('font3', 4, 7, 43), ('font4', 8, 8, 78)]:
    pixels = read_bmp(root / 'src/gfxdata/bmp' / (name + '.bmp'))
    lines.append(f'static const uint8_t tracker_{name}[{count}][{glyph_height}] = {{')
    for glyph in range(count):
        rows = [sum((1 << x) for x in range(glyph_width) if pixels[y][glyph * glyph_width + x])
                for y in range(glyph_height)]
        lines.append('    {' + ','.join(f'0x{row:02x}' for row in rows) + '},')
    lines.append('};')
tables = (root / 'src/ft2_tables.c').read_text()
widths = re.search(r'font1Widths\[128\].*?\{(.*?)\}', tables, re.S).group(1)
lines.append('static const uint8_t tracker_font1_widths[128] = {' + widths + '};')
output.write_text('\n'.join(lines) + '\n')
