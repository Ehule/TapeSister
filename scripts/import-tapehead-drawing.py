#!/usr/bin/env python3
"""Reproduce the compiled Tapehead extraction and its explicit host patch.

Usage: python scripts/import-tapehead-drawing.py /path/to/TapeHead [--check]
No Tapehead checkout or generator is needed to build SisterTracker.
"""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'third_party/tapehead'
FUNCTIONS = [
    'getCursorBreatheLevel', 'breatheColorToward', 'getPatternLayout',
    'patternXToCursorObject', 'writeCursor', 'pattLayoutCharOut',
    'pattLayoutPlaceholderOut', 'drawAdaptiveCell', 'drawChannelNumbering',
    'drawRowNums', 'drawFastTracksPOCLed', 'drawFastTracksPOCStatus',
    'drawDirectHLine', 'drawDirectVLine', 'drawControlEjectSymbol',
    'drawTrackLengthStatus', 'dimPatternColor', 'drawTrackPlayheadOutline',
    'writePattern', 'pattTwoHexOut', 'pattCharOut', 'drawEmptyNoteMedium',
    'drawKeyOffMedium', 'drawNoteMedium',
]


def function(source, name):
    match = re.search(r'^([^\n;{}]*\b' + name + r'\([^;{}]*?\)[^\n]*)\n\{.*?^\}',
                      source, re.M | re.S)
    if not match:
        raise ValueError('Missing upstream function: ' + name)
    return match.group(0)


def declaration(source, name):
    match = re.search(r'^(?:static )?const [^\n]*\b' + name +
                      r'\[.*?(?:;(?=\n))', source, re.M | re.S)
    if not match:
        raise ValueError('Missing upstream table: ' + name)
    return match.group(0)


def extract(root):
    source = (root / 'src/ft2_pattern_draw.c').read_text()
    tables = (root / 'src/ft2_tables.c').read_text()
    pieces = ['/* Extracted from Tapehead f053d96. BSD-3-Clause; see CODE-LICENSE.txt.\n'
              ' * Regenerate with scripts/import-tapehead-drawing.py.\n'
              ' * Host adaptations are recorded in pattern_draw.patch. */',
              '#define CURSOR_BREATHE_FRAMES 120',
              'static uint8_t cursorBreatheFrame;',
              'static note_t emptyPattern[MAX_CHANNELS * MAX_PATT_LEN];',
              'static const uint8_t *font4Ptr, *font5Ptr;']
    pieces.append(re.search(r'typedef struct pattLayout_t.*?} pattLayout_t;', source, re.S).group(0))
    for name in ['vol2charTab1', 'vol2charTab2', 'columnModeTab', 'pattLayouts',
                 'sharpNote1Char_med', 'sharpNote2Char_med',
                 'flatNote1Char_med', 'flatNote2Char_med']:
        pieces.append(declaration(source, name))
    for name in ['noteTab1', 'noteTab2', 'hex2Dec', 'chanWidths']:
        pieces.append('static ' + declaration(tables, name))
    bodies = [function(source, name) for name in FUNCTIONS]
    pieces.extend(body.split('\n{', 1)[0] + ';' for body in bodies)
    pieces.extend(bodies)
    transport = (root / 'src/ft2_transport_visuals.h').read_text()
    tiny = function((root / 'src/ft2_gui.c').read_text(), 'textOutTiny') + '\n\n' + \
        function(transport, 'tapeheadTextOutTinyWithColon') + \
        '\n\n#define textOutTiny tapeheadTextOutTinyWithColon\n'
    transport = '#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n\n' + '\n\n'.join(
        function(transport, name) for name in [
            'tapeheadTrackUsesIndependentTransportVisual', 'tapeheadTransportVisualPageStart']) + '\n'
    return {'pattern_draw.inc': '\n\n'.join(pieces) + '\n',
            'tiny_draw.inc': tiny,
            'transport_visuals.h': transport,
            **{name: (root / 'src' / name).read_text()
               for name in ['ft2_fasttracks_core.c', 'ft2_fasttracks_core.h']}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tapehead', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((DEST / 'SOURCES.json').read_text())
    for name, expected in manifest['sha256'].items():
        actual = hashlib.sha256((args.tapehead / name).read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit('Upstream source differs from the pinned reference: ' + name)
    outputs = extract(args.tapehead)
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        for name, contents in outputs.items():
            (directory / name).write_text(contents)
        with (DEST / 'pattern_draw.patch').open() as patch:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p0'], cwd=directory,
                           stdin=patch, check=True, stdout=subprocess.DEVNULL)
        for name in outputs:
            contents = (directory / name).read_bytes()
            if args.check:
                if (DEST / name).read_bytes() != contents:
                    raise SystemExit('Extraction differs: ' + name)
            else:
                (DEST / name).write_bytes(contents)
            print(name, hashlib.sha256(contents).hexdigest())


if __name__ == '__main__':
    main()
