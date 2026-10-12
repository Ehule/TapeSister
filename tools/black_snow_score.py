#!/usr/bin/env python3
"""Deterministic original score for the optional native composition utility.

No audio is synthesized or mixed here: this is equivalent to entering notes,
volume commands and effects in TrackSister. 72 BPM / 3 ticks / 32 rows per bar.
"""
import argparse
from pathlib import Path

events = {}


def cell(bar, row, lane, alias=0, note=-1, vol=-1, fx=0, value=0):
    absolute = bar * 32 + row
    assert 0 <= absolute < 88 * 32
    key = (absolute // 128, absolute % 128, lane)
    old = events.get(key, [0, -1, -1, 0, 0])
    # Volume automation retains existing notes; later explicit notes replace.
    if alias:
        old[0] = alias
    if note != -1:
        old[1] = note
    if vol != -1:
        old[2] = max(0, min(64, vol))
    if fx:
        old[3:] = [ord(fx) if isinstance(fx, str) else fx, value]
    events[key] = old


def note(bar, row, lane, alias, pitch, volume, length=None):
    cell(bar, row, lane, alias, pitch, volume)
    if length and bar * 32 + row + length < 88 * 32:
        cell(bar, row + length, lane, note=-2)


def section(bar):
    if bar < 8:
        return 'intro'
    if bar < 24:
        return 'first'
    if bar < 40:
        return 'develop'
    if bar < 48:
        return 'break'
    if bar < 56:
        return 'rebuild'
    if bar < 80:
        return 'climax'
    return 'outro'


# F minor(add9), Dbmaj7, Ab(add9)/Eb, Eb suspended then major.
# Deliberate moving upper voices; Db softens the opening, while a chromatic
# lower neighbor to C adds tension at the end of the eight-bar motif.
chords = [(53, 60, 68), (49, 60, 65), (51, 58, 67), (51, 58, 65)]
roots = [29, 25, 32, 27]
# Eight-bar motif: F Ab G Eb | C (rest) Eb G | Ab C Bb G | F Eb C ...
motif = [
    [(0, 77, 42, 5), (7, 80, 35, 3), (12, 79, 39, 7), (24, 75, 34, 5)],
    [(4, 72, 39, 10), (20, 75, 31, 3), (26, 79, 33, 4)],
    [(0, 80, 40, 9), (12, 84, 32, 4), (20, 82, 38, 5), (28, 79, 28, 3)],
    [(0, 77, 40, 12), (16, 75, 33, 5), (24, 72, 35, 6)],
    [(4, 77, 41, 8), (16, 80, 35, 6), (26, 79, 32, 4)],
    [(0, 75, 37, 8), (12, 72, 33, 8), (26, 70, 29, 4)],
    [(0, 75, 38, 7), (12, 79, 34, 6), (24, 77, 39, 5)],
    [(0, 72, 37, 12), (18, 71, 26, 3), (24, 72, 32, 5)],
]


def build(mode):
    for b in range(88):
        s = section(b)
        h = (b // 2) % 4
        root = roots[h]
        chord = list(chords[h])
        intense = s == 'climax'
        groove = s in ('first', 'develop', 'climax') or s == 'rebuild' and b >= 52 or s == 'outro' and b < 84
        # Low choir triad: staggered entrances and articulation leave air.
        if b % 2 == 0:
            level = 32 if s == 'intro' else 36 if s == 'break' else 44 if intense else 40
            if s == 'outro':
                level -= (b - 80) * 4
            for j, pitch in enumerate(chord):
                note(b, 2 + j, 6 + j, 8, pitch, level - j * 2, 56)
            if intense and b % 8 == 6:
                note(b, 19, 13, 8, chord[2] + 12, 20, 28)
        if b % 4 == 0:
            note(b, 0, 11, 11, 53 if b < 80 else 41, 24 if groove else 37, 112)
        if s == 'intro':
            if b in (0, 4):
                note(b, 6, 10, 15, 65 if b == 0 else 60, 34, 64)
            if b >= 4:
                note(b, 16, 1, 2, 60, 14 + (b - 4) * 3)
                for r in (6, 22, 28):
                    note(b, r, 2, 3, 60, 16 + r % 7)
            if b in (2, 6):
                note(b, 12, 9, 16, 77 if b == 2 else 75, 27, 30)
        if groove:
            kicks = [0, 11, 24] if b % 2 == 0 else [0, 14, 22, 28]
            if s == 'develop':
                kicks = [0, 10, 23, 27] if b % 2 == 0 else [0, 7, 14, 26]
            if intense:
                kicks = [0, 11, 20, 26] if b % 2 == 0 else [0, 6, 14, 23, 30]
            if b % 8 == 7:
                kicks = [0, 11, 22]  # leave the last beat to the fill
            for r in kicks:
                note(b, r, 0, 1, 60, 60 if r == 0 else 49 + (r % 3) * 4)
            note(b, 16, 1, 2, 60, 54 if intense else 49)
            if b % 4 == 3:
                note(b, 29, 1, 2, 60, 19)
            if b % 8 == 7:
                for r, v in ((26, 23), (28, 29), (30, 38), (31, 23)):
                    note(b, r, 1, 2, 60, v)
            hats = range(0, 32, 2) if s != 'first' or b >= 16 else range(2, 32, 4)
            for r in hats:
                if r in (14, 30) and b % 2 == 0:
                    continue
                note(b, r, 2, 3, 60 + (1 if r % 8 == 6 else 0), 21 + (r * 7 + b * 3) % 17)
            if s in ('develop', 'climax') and b % 4 == 1:
                for r in (25, 27, 29, 31):
                    note(b, r, 2, 3, 60, 18 + r % 9)
            if b % 8 in (3, 7):
                # Six triplet-spaced events in one beat. Tick delays on the
                # 32nd grid produce exact 1/24-note spacing at this tempo.
                for r, delay, v in ((24, 0, 32), (25, 1, 22), (26, 2, 26), (28, 0, 30), (29, 1, 20), (30, 2, 25)):
                    note(b, r, 2, 3, 60, v)
                    if delay:
                        cell(b, r, 2, fx='E', value=0xD0 + delay)
            for r in ([6, 26] if b % 2 == 0 else [10]):
                note(b, r, 3, 4, 60, 26)
            # Bass sustains while volume commands open space for kick attacks.
            # Tracker cells remain editable; no detector/sidechain is added.
            for lane, alias, pitch, volume in ((4, 5, root, 49), (5, 7 if intense else 6, root + 12, 43 if intense else 41)):
                note(b, 0, lane, alias, pitch, volume)
                if b % 2 == 1:
                    note(b, 20, lane, alias, pitch + 7, volume - 5)
                    note(b, 26, lane, alias, pitch, volume)
                if b % 8 == 7:
                    cell(b, 28, lane, fx='2', value=5)
                    cell(b, 29, lane, fx='2', value=5)
                    cell(b, 30, lane, note=-2)
                for r in kicks:
                    cell(b, r, lane, vol=8 if lane == 4 else 11)
                    if r < 30:
                        cell(b, r + 1, lane, vol=volume // 2)
                        cell(b, r + 2, lane, vol=volume)
            if b % 4 == 2:
                note(b, 27, 15, 14, 60, 24 if intense else 17)
        if s in ('first', 'develop', 'climax'):
            # Early phrases leave whole bars unanswered; later the motif rises.
            active = not(s == 'first' and b < 16 and b % 2)
            if active:
                for r, pitch, vol, length in motif[(b - 8) % 8]:
                    if s == 'develop' and b >= 32:
                        pitch -= 12
                    if intense and b >= 72 and r in (0, 4):
                        pitch += 12
                    note(b, r, 9, 9, pitch, vol + (4 if intense else 0), length)
            if s == 'develop' and b % 4 == 2 or intense and b % 4 == 0:
                note(b, 10, 12, 12, chord[1] + 24, 29, 8)
                note(b, 26, 12, 12, chord[2] + 12, 22, 9)
            if b % 8 == 4:
                note(b, 8, 10, 10, 65, 29, 42)
        if s in ('break', 'rebuild'):
            if b in (40, 46, 50):
                note(b, 8, 13, 19, 65 if b != 46 else 60, 33, 100)
            if b % 2 == 0:
                note(b, 0, 10, 15 if b < 48 else 18, 60 + (h == 1) * 3, 38, 56)
                for r, pitch, vol, length in motif[b % 8][:2]:
                    note(b, r + 4, 9, 16, pitch - 12, vol - 9, length * 2)
            if b % 4 == 2:
                note(b, 20, 12, 12, 77, 23, 20)
            if s == 'rebuild' and b < 52:
                for r in (4, 12, 20, 28):
                    note(b, r, 2, 3, 60, 18 + (b - 48) * 3)
                note(b, 16, 1, 2, 60, 22 + (b - 48) * 5)
        if s == 'outro':
            if b in (80, 82, 84):
                note(b, 4, 9, 16 if b == 84 else 9, 77 if b == 80 else 72, 33 - (b - 80) * 4, 32)
            if b == 84:
                note(b, 2, 10, 15, 60, 27, 100)
            if b >= 84:
                cell(b, 0, 4, note=-2)
                cell(b, 0, 5, note=-2)
        if b in (6, 22, 38, 54, 70, 78):
            note(b, 6, 14, 17, 65, 19 if b == 6 else 26, 55)
        if b in (7, 23, 39, 55, 71):
            note(b, 2, 15, 13, 60, 22, 27)
        if b in (8, 24, 56, 72):
            note(b, 0, 15, 14, 48, 34, 8)
    # Final written decrescendo, followed by a full silent beat. There are no
    # unsaved gain curves applied to the delivered render.
    for r in range(32):
        for lane in range(16):
            cell(87, r, lane, vol=max(0, 20 - r))
            if r == 24:
                cell(87, r, lane, note=-3)
    if mode == 'groove':
        keep = {0, 1, 2, 3, 4, 5}
        cropped = {}
        for (p, r, l), value in events.items():
            if 2 <= p < 4 and l in keep:
                cropped[(p - 2, r, l)] = value
        events.clear()
        events.update(cropped)
    elif mode == 'section':
        cropped = {(p - 6, r, l): v for (p, r, l), v in events.items() if 6 <= p < 10}
        events.clear()
        events.update(cropped)
    else:
        # TrackSister's song transport wraps at the end. Keep it in a silent
        # terminal order with an ordinary Bxx jump, so playback never restarts
        # the introduction. Effect returns can finish naturally.
        for lane in range(16):
            events[(22, 0, lane)] = [0, -3, 0, 0, 0]
        events[(22, 127, 0)] = [0, -1, 0, ord('B'), 22]


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    parser.add_argument('--mode', choices=['full', 'groove', 'section'], default='full')
    args = parser.parse_args()
    build(args.mode)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as f:
        f.write('# Black Snow / 72 BPM / 3 ticks / 128-row patterns / native TrackSister\n')
        for key, value in sorted(events.items()):
            f.write(','.join(map(str, (*key, *value))) + '\n')
    print(f'{args.mode}: {len(events)} cells -> {args.output}')
