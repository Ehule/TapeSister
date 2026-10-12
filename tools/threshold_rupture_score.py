#!/usr/bin/env python3
"""Threshold / Rupture: 24-minute revision, entirely native tracker events.

150 BPM, 3 ticks/row, 64 rows per 75-BPM phrase bar (two 150-BPM bars).
225 patterns/orders. Preserve the original first 5:07.2, then compose a new
arc with structural contrast, trap subdivisions and separate Terra guide.
"""
import argparse
from pathlib import Path
import threshold_score as original

ROWS = 64
BARS = 448
events = {}
SECTIONS = [(0, 'DUST'), (48, 'FIRST LIGHT'), (96, 'PULSE'),
            (128, 'WEIGHT'), (160, 'IRON WINGS'), (192, 'SHEAR'),
            (216, 'AFTERIMAGE'), (248, 'PRESSURE'), (280, 'BLACK HALO'),
            (312, 'NEGATIVE SPACE'), (328, 'ASCENT'), (352, 'RUPTURE'),
            (384, 'OPEN WOUND'), (400, 'RESOLVE'), (424, 'HANDOFF'),
            (432, 'DJ BRIDGE')]
ROOTS = [26, 34, 29, 24]
CHORDS = [(50, 57, 65), (46, 57, 62), (53, 60, 69), (48, 55, 64)]


def cell(bar, row, lane, alias=0, pitch=-1, volume=-1, fx=0, value=0):
    absolute = bar * ROWS + row
    if not 0 <= absolute < BARS * ROWS:
        return
    key = (absolute // 128, absolute % 128, lane)
    c = events.setdefault(key, [0, -1, -1, 0, 0])
    if alias:
        c[0] = alias
    if pitch != -1:
        c[1] = pitch
    if volume != -1:
        c[2] = max(0, min(64, round(volume)))
    if fx:
        c[3:] = [ord(fx), value]


def note(bar, row, lane, alias, pitch, vol, length=None):
    cell(bar, row, lane, alias, pitch, vol)
    if length:
        cell(bar, row + length, lane, pitch=-2, volume=0)


def swell(bar, lane, alias, pitch, bars, level, attack=.3, release=.25):
    note(bar, 0, lane, alias, pitch, 0)
    total = round(bars * ROWS)
    for r in range(2, total, 2):
        x = r/total
        amp = min(1, x/attack, (1-x)/release)*level
        cell(bar, r, lane, volume=amp)
    cell(bar, total-1, lane, volume=0, pitch=-2)


def cut(bar, row, lanes):
    for lane in lanes:
        cell(bar, row, lane, volume=0, pitch=-3)


def harmony(b):
    if b < 128:
        return ((b-96)//8) % 4
    if 216 <= b < 248:
        return [0, 1, 0, 3][(b-216)//8]
    if 312 <= b < 328:
        return 0 if b < 320 else 3
    if 352 <= b < 400:
        return [0, 1, 0, 3][((b-352)//4) % 4]
    if b >= 400:
        return 0 if b >= 416 else [1, 2, 3, 0][(b-400)//4]
    return ((b-128)//4) % 4


def drums(b, stage):
    """Returns kick positions for written, editable bass ducking."""
    heavy = stage in ('first', 'second', 'peak', 'resolve')
    if stage == 'pulse':
        kicks = [0] if b % 2 == 0 else ([] if b < 112 else [0, 40])
    elif stage == 'groove':
        kicks = [[0, 24, 48], [0, 36, 56], [0, 12, 40], [0, 28, 48, 60]][b % 4]
    elif stage == 'shear':
        kicks = [0, 12, 24, 48] if b % 4 < 2 else [0, 44]
    elif stage == 'build':
        kicks = [0, 40] if b < 264 or 328 <= b < 336 else [0, 24, 40, 56]
    elif stage == 'handoff':
        kicks = [0, 32] if b >= 432 else [0, 24, 32, 56]
    else:
        kicks = [[0, 12, 28, 40, 56], [0, 24, 36, 60],
                 [0, 8, 28, 40, 52], [0, 20, 32, 44, 56]][b % 4]
    if heavy and b % 8 == 7:
        kicks = [0, 24, 40]
    for r in kicks:
        vol = 41 if stage == 'pulse' else 55 if stage == 'groove' else 59
        note(b, r, 0, 1, 60, vol if r == 0 else vol-6)
    if stage == 'pulse' and b < 112:
        return kicks
    snares = [32] if stage == 'pulse' or stage == 'build' and (b < 264 or b < 336 and b >= 328) else [16, 48]
    for r in snares:
        note(b, r, 1, 2, 60, 32 if stage == 'pulse' else 44 if stage == 'groove' else 52)
    # Ghost pickups, asymmetric snare rolls, and pitched metallic responses.
    if heavy and b % 4 == 3:
        for r, vol in [(54, 18), (58, 27), (62, 34)]:
            note(b, r, 1, 2, 60, vol)
    if heavy and b % 2 == 0:
        note(b, 49, 15, 26, 58 if b % 4 == 0 else 65, 24)
    spacing = 16 if stage == 'pulse' else 8 if stage == 'groove' else 4
    for r in range(0, 64, spacing):
        vel = (13 if stage == 'pulse' else 21) + [7, -4, 1, -2][(r//spacing+b) % 4]
        note(b, r, 2, 3, 60, vel)
    if heavy or stage == 'shear':
        # Rolls are phrases, separated by rests: not continuous machine-gun hats.
        if b % 4 in (1, 3):
            start = 20 if b % 4 == 1 else 52
            for i, r in enumerate(range(start, start+8, 2)):
                note(b, r, 2, 3, 60+(i % 3-1), 28-i*3)
        if b % 8 == 7:
            # Six equally spaced hits at four ticks apart, over eight rows.
            for offset, delay, vol in [(0, 0, 29), (1, 1, 21), (2, 2, 25),
                                       (4, 0, 28), (5, 1, 20), (6, 2, 25)]:
                note(b, 48+offset, 2, 3, 60, vol)
                if delay:
                    cell(b, 48+offset, 2, fx='E', value=0xD0+delay)
        if stage == 'peak' and b % 8 == 3:
            for i in range(8):
                note(b, 56+i, 2, 3, 63-i//2, 29-i*2)
    if stage != 'pulse' and b % 2 == 0:
        note(b, 40 if stage != 'handoff' else 56, 3, 4, 60, 24)
    if heavy and b % 8 == 0:
        note(b, 0, 15, 14, 48, 35, 12)
    return kicks


def bass(b, root, kicks, stage):
    heavy = stage in ('first', 'second', 'peak', 'resolve')
    subvol = 30 if stage == 'pulse' else 40 if stage == 'groove' else 42
    reesevol = 28 if stage == 'pulse' else 42 if stage == 'groove' else 55
    alias = 6 if stage in ('pulse', 'groove', 'build', 'handoff') else 7 if stage in ('first', 'resolve') else 25
    if stage == 'handoff':
        factor = max(0, (432-b)/8)
        subvol *= factor
        reesevol *= factor
    if b >= 432 or stage == 'pulse' and b < 120:
        return
    # Phrase-level pitch motion is deliberate; foundation and upper bass separate.
    for lane, asset, pitch, volume in [(4, 5, root, subvol), (5, alias, root+12, reesevol)]:
        note(b, 0, lane, asset, pitch, volume)
        if heavy and b % 4 == 3:
            note(b, 40, lane, asset, pitch+7, volume-4)
            note(b, 56, lane, asset, pitch, volume)
        elif stage == 'shear':
            # A 3+3+2 stabbing phrase against an unbroken drum clock.
            for r in [0, 24, 48]:
                note(b, r, lane, asset, pitch, volume, 10 if lane == 5 else 14)
        elif stage == 'peak' and b % 8 in (2, 6):
            note(b, 48, lane, asset, pitch+12 if lane == 5 else pitch, volume-3)
        for r in kicks:
            cell(b, r, lane, volume=volume*.12)
            cell(b, r+1, lane, volume=volume*.32)
            cell(b, r+2, lane, volume=volume*.65)
            cell(b, r+4, lane, volume=volume)
        if stage == 'peak' and b % 8 == 5:
            # A brief vacuum after the kick, then the upper layer tears back in.
            cell(b, 32, 5, volume=0)
            cell(b, 40, 5, volume=reesevol)


def guide(b, stage):
    if stage in ('pulse', 'shear', 'handoff'):
        return
    if stage == 'groove' and b % 8 >= 4:
        return
    if stage == 'build' and b % 4 != 0:
        return
    # Stable starts; the last two phrase bars are left to live Terra answers.
    phrase = b % 8
    if phrase >= 6:
        return
    theme = [[(0, 74, 20), (32, 77, 20)], [(16, 76, 24)],
             [(0, 72, 24), (40, 69, 16)], [],
             [(0, 77, 24), (32, 81, 20)], [(16, 79, 20), (48, 74, 12)]]
    level = 33 if stage in ('groove', 'build') else 41
    for r, pitch, length in theme[phrase]:
        if stage == 'groove':
            pitch -= 12
        note(b, r, 9, 9, pitch, level, length)


def build():
    original.events.clear()
    original.build()
    for (pattern, row, lane), value in original.events.items():
        oldrow = pattern*128+row
        if oldrow < 96*32:
            absolute = oldrow*2
            events[(absolute//128, absolute % 128, lane)] = value.copy()
    for b in range(96, BARS):
        h = harmony(b)
        chord = CHORDS[h]
        root = ROOTS[h]
        quiet = 216 <= b < 248 or 312 <= b < 328
        stage = ('pulse' if b < 128 else 'groove' if b < 160 else
                 'first' if b < 192 else 'shear' if b < 216 else
                 'quiet' if b < 248 else 'build' if b < 280 else
                 'second' if b < 312 else 'quiet' if b < 328 else
                 'build' if b < 352 else 'peak' if b < 400 else
                 'resolve' if b < 424 else 'handoff')
        heavy = stage in ('first', 'second', 'peak', 'resolve')
        span = 8 if quiet or stage == 'pulse' else 4
        if b % span == 0 and b < 424:
            alias = 20 if quiet or stage in ('pulse', 'build') else 8 if stage in ('groove', 'shear') else 24
            level = {'pulse': 25, 'groove': 34, 'first': 28, 'shear': 23,
                     'quiet': 23, 'build': 33, 'second': 31, 'peak': 36, 'resolve': 32}[stage]
            # Climax attacks quickly and sustains: no repeated long fade-in.
            for j in range(3):
                pitch = chord[j] + (12 if 384 <= b < 400 and j == 2 else 0)
                swell(b, 6+j, alias, pitch, span, level-j*2,
                      attack=.035 if heavy else .22, release=.08 if heavy else .3)
        if quiet:
            if b % 8 == 0:
                swell(b, 10, 22, chord[2], 8, 22)
                swell(b, 11, 11, 38 if b < 240 else 50, 8, 19)
                swell(b, 14, 23, 55, 8, 12)
            if b in (220, 236, 316, 324):
                note(b, 0, 13, 19, 62 if b % 16 else 60, 23, 110)
                note(b, 32, 9, 16, 65 if b < 248 else 74, 20, 90)
            continue
        kicks = drums(b, stage)
        bass(b, root, kicks, stage)
        guide(b, stage)
        if b % 16 == 0 and b < 424:
            swell(b, 11, 11, 50, 12, 16 if heavy else 21)
        if b % 8 == 0 and b < 424:
            if heavy:
                # Distortion-bearing harmonic plane; the clean sub remains separate.
                swell(b, 10, 27, 62 if stage == 'peak' else 50, 6,
                      20 if stage == 'peak' else 15, attack=.06, release=.18)
            else:
                swell(b, 10, 21 if b % 16 else 22, chord[1], 8, 17)
        if heavy and b % 8 == 4:
            # Long upper fifth/ninth appears where the guide has room.
            swell(b, 13, 24, 81 if h == 0 else chord[2]+12, 3,
                  21 if stage == 'peak' else 18, attack=.10)
        if stage == 'peak' and b % 8 == 6:
            note(b, 0, 12, 12, 86 if h == 0 else chord[2]+12, 25, 24)
        if b in (152, 184, 208, 272, 304, 344, 376, 392, 416):
            swell(b, 14, 17, 62, 4, 24 if b < 352 else 33, attack=.8, release=.04)
        if stage == 'build' and b % 8 == 7:
            for r, vol in [(32, 18), (40, 22), (48, 28), (54, 33), (58, 37), (62, 41)]:
                note(b, r, 15, 26, 55+(r//8) % 5, vol)
    # Remove sustained residues at each structural withdrawal. These are score
    # events, not render-only automation, so native playback has the same form.
    for b in (216, 312):
        cut(b, 0, (0, 1, 2, 3, 4, 5, 12, 13, 15))
    # Two full-beat holes immediately before the largest entries. Delete future
    # events inside the gaps, then cut once. Reverb residue survives naturally.
    for b, start in [(159, 48), (279, 48), (351, 32), (383, 48)]:
        lo, hi = b*ROWS+start, (b+1)*ROWS
        for key in list(events):
            absolute = key[0]*128+key[1]
            if lo <= absolute < hi:
                del events[key]
        cut(b, start, range(16))
    # Resolve the final tonic before the DJ transition. Harmony ends at 22:36.8;
    # bass yields at 23:02.4, leaving a precisely counted 75/150-BPM drum bed.
    cut(424, 0, range(6, 16))
    cut(432, 0, (4, 5))
    for lane in range(16):
        events[(224, 0, lane)] = [0, -3, 0, 0, 0]
    events[(224, 127, 0)] = [0, -1, 0, ord('B'), 224]
    # Leave the preserved opening untouched. The new arrangement has denser
    # upper voices; reserve output headroom in the actual editable score.
    for (pattern, row, _), value in events.items():
        if pattern*128+row >= 96*ROWS and value[2] >= 0:
            value[2] = round(value[2]*.78)


def section_name(bar):
    return next(name for start, name in reversed(SECTIONS) if bar >= start)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as f:
        f.write('# THRESHOLD / RUPTURE / 150 BPM / 75 BPM feel / D minor / 3 ticks\n')
        for p in range(225):
            label = section_name(p*2) if p < 224 else 'SILENCE HOLD'
            f.write(f'#@pattern {p} {p:03d} {label}\n')
        for key, value in sorted(events.items()):
            f.write(','.join(map(str, (*key, *value)))+'\n')
    print(f'{len(events)} native cells; 896 bars at 150 BPM; 1433.6 seconds plus tail')
