#!/usr/bin/env python3
"""Threshold: an original 24-minute, fixed-75-BPM native performance score.

Only tracker events are generated here. Audio generation/mixing is native.
448 musical bars, 32 rows/bar, 3 ticks/row. Sixteen lanes, 23 tile aliases.
"""
import argparse
from pathlib import Path

events = {}
BARS = 448
SECTIONS = [(0, 'DUST'), (48, 'FIRST LIGHT'), (96, 'PULSE'),
            (144, 'WEIGHT'), (176, 'UNDERCURRENT'), (224, 'REMEMBER'),
            (272, 'SUSPENSION'), (288, 'ASCENT'), (336, 'OPEN SKY'),
            (384, 'RADIANCE'), (400, 'HANDOFF'), (416, 'CLEAR SPACE'),
            (432, 'DJ BRIDGE')]


def cell(bar, row, lane, alias=0, pitch=-1, volume=-1, fx=0, value=0):
    absolute = bar * 32 + row
    if not 0 <= absolute < BARS * 32:
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
        cell(bar, row + length, lane, pitch=-2)


def swell(bar, lane, alias, pitch, bars, level, start=0, end=0):
    """Explicit volume envelope: no unsaved render automation."""
    note(bar, 0, lane, alias, pitch, start)
    total = bars * 32
    for r in range(4, total, 4):
        x = r / total
        amp = start + (level - start) * x / .3 if x < .3 else level if x < .65 else level + (end-level)*(x-.65)/.35
        cell(bar, r, lane, volume=amp)
    cell(bar, total-1, lane, volume=end)
    if end == 0:
        cell(bar, total-1, lane, pitch=-2)


# D minor with Bb-major-seventh, F-major and C-suspended/major colours.
# Low Bb is voiced one octave up to keep the sub above approximately 30 Hz.
ROOTS = [26, 34, 29, 24]
CHORDS = [(50, 57, 65), (46, 57, 62), (53, 60, 69), (48, 55, 64)]
# An original eight-bar theme. All attacks lie on eighth-note subdivisions;
# whole unanswered bars make room for the choir and the rhythm section.
THEME = [
    [(0, 74, 10), (12, 77, 8), (24, 76, 7)],
    [(8, 72, 15)],
    [(0, 69, 14), (24, 72, 7)],
    [],
    [(0, 77, 10), (16, 81, 10)],
    [(8, 79, 10), (24, 76, 7)],
    [(0, 74, 20)],
    [],
]
ANSWER = [[(0, 81, 14), (24, 79, 7)], [], [(8, 77, 18)],
          [(16, 76, 12)], [(0, 74, 22)], [], [(8, 72, 14)], []]


def harmony(bar):
    if bar < 48:
        return 0
    if bar < 96:
        return ((bar-48)//16) % 4
    if bar < 176:
        return ((bar-96)//8) % 4
    if 272 <= bar < 288:
        return 2 if bar < 280 else 3
    return ((bar-176)//4) % 4


def build():
    # The opening's stretched choir becomes the recognizable final harmony.
    for b in range(0, 96, 8):
        h = harmony(b)
        chord = CHORDS[h]
        level = 15 + 12*b/96
        for lane, alias, pitch, lag in [(6, 21, chord[0], 0), (7, 22, chord[1], 2),
                                       (8, 20, chord[2], 4)]:
            # Every layer breathes on a different boundary.
            if b + lag + 8 <= 96:
                swell(b+lag, lane, alias, pitch, 8, level-(lane-6)*2)
        if b % 16 == 0:
            swell(b, 10, 15 if b % 32 == 0 else 18, 50 if b<48 else 57, 12, 19+b/16)
            swell(b, 14, 23, 55, 16, 8+b/24)
        if b in (24, 40, 56, 72, 88):
            note(b, 0, 9, 16, 62 if b%32 else 65, 17, 60)
        if b in (68, 84):
            note(b, 16, 12, 12, 74, 12, 18)
    # Continuous low fifths beneath the opening, with a long emergence.
    for b in range(0, 96, 16):
        swell(b, 11, 11, 38 if b<48 else 50, 16, 14+b/8)

    for b in range(96, BARS):
        h = harmony(b)
        chord = CHORDS[h]
        root = ROOTS[h]
        pulse = b < 176
        breath = 272 <= b < 288
        ascent = 288 <= b < 336
        peak = 336 <= b < 400
        handoff = b >= 400
        groove = not pulse and not breath

        # Eight-bar waves in the early pulse; four-bar harmonic breaths later.
        span = 8 if pulse else 4
        if b % span == 0 and b < 416:
            level = 27 if pulse else 34 if breath else 35 if ascent else 42 if peak else 32
            for j in range(3):
                alias = 20 if pulse or breath else 8
                # Sustained Halo supports the later one-shot choir's breath.
                swell(b, 6+j, alias, chord[j], span, level-j*2)
        if b % 16 == 0 and b < 400:
            swell(b, 11, 11, 50 if not peak else 62, 16, 16 if groove else 24)
            swell(b, 10, 22 if b%32 else 21, chord[2], 12, 21 if peak else 16)
        if b % 32 == 16 and b < 400:
            swell(b, 14, 23, 60 if not peak else 67, 12, 13 if not peak else 22)
        if breath:
            if b in (272, 280):
                note(b, 0, 13, 19, 62 if b==272 else 60, 25, 110)
                note(b, 16, 9, 16, 65 if b==272 else 64, 24, 90)
            continue

        # A sparse 75 BPM pulse gradually becomes a steady backbeat.
        if b < 112:
            kicks = [0] if b%2==0 else []
        elif b < 144:
            kicks = [0, 20] if b%2==0 else [0]
        elif pulse:
            kicks = [0, 12, 24] if b%2==0 else [0, 20]
        elif handoff:
            kicks = [0, 16] if b>=416 else [0, 12, 16, 28]
        elif peak:
            kicks = [0, 12, 20, 28] if b%2==0 else [0, 6, 16, 28]
        else:
            kicks = [0, 12, 24] if b%2==0 else [0, 16, 28]
        # Planned short gaps give the longer form identifiable punctuation.
        turnaround = b%32 == 31 and b<400
        if turnaround:
            kicks = [0]
        for r in kicks:
            note(b, r, 0, 1, 60, 40 if b<128 else 56 if r==0 else 49)
        if b >= 120:
            snare_rows = [16] if pulse or ascent and b<304 else [8, 24]
            for r in snare_rows:
                note(b, r, 1, 2, 60, 25 if b<144 else 42 if pulse else 48 if peak else 44)
        if groove and b%8==7 and not handoff:
            for r, vol in [(28, 17), (30, 25)]:
                note(b, r, 1, 2, 60, vol)
        if b >= 112:
            spacing = 8 if b<144 else 4
            hats = range(4 if pulse else 0, 32, spacing)
            for r in hats:
                if turnaround and r>=24:
                    continue
                note(b, r, 2, 3, 60, (12 if pulse else 20)+(r//4+b)%4*3)
            if peak and b%4 in (1, 3):
                for r in (18, 22, 26, 30):
                    note(b, r, 2, 3, 60, 17+(r+b)%7)
            if groove and not handoff and b%8==7:
                for r, delay, vol in [(24, 0, 27), (25, 1, 19), (26, 2, 23),
                                      (28, 0, 26), (29, 1, 18), (30, 2, 21)]:
                    note(b, r, 2, 3, 60, vol)
                    if delay:
                        cell(b, r, 2, fx='E', value=0xD0+delay)
            if b >= 160 and b%2==0:
                note(b, 20 if not handoff else 28, 3, 4, 60, 20)

        # Sub and Reese retain separate editable parts. Written kick ducking.
        if b >= 128 and b < 432:
            basslevel = 30 if pulse else 44
            if handoff:
                basslevel *= max(0, (432-b)/32)
            for lane, alias, pitch, vol in [(4, 5, root, basslevel),
                                          (5, 7 if peak else 6, root+12, basslevel-5)]:
                note(b, 0, lane, alias, pitch, vol)
                if groove and b%4==3 and b<400:
                    note(b, 20, lane, alias, pitch+7, vol-4)
                    note(b, 28, lane, alias, pitch, vol)
                for r in kicks:
                    cell(b, r, lane, volume=vol*.20)
                    cell(b, r+1, lane, volume=vol*.58)
                    cell(b, r+2, lane, volume=vol)
                if turnaround:
                    cell(b, 24, lane, volume=0, pitch=-2)

        # Motif: sparse early glimpses, clear groove statements, then long
        # high answers. No per-note delay effects on the lead lane.
        play = b>=160 and b<400 and not(224<=b<240)
        if play:
            theme = ANSWER if 240<=b<256 or 352<=b<368 else THEME
            notes = theme[b%8]
            if pulse:
                notes = notes[:1] if b%4==0 else []
            if ascent and b<304:
                notes = notes[:1] if b%2==0 else []
            if b%32 >= 24:
                notes = notes[:1] if b%4==0 else []
            for r, pitch, length in notes:
                if 192<=b<224 or 256<=b<272:
                    pitch -= 12
                vol = 26 if pulse else 34 if ascent else 39 if peak else 35
                note(b, r, 9, 9, pitch, vol, length)
        if b in (400, 404, 408, 412):
            note(b, 0, 9, 9, [77, 76, 74, 74][(b-400)//4], 32-(b-400), 24)
        if (groove and b%8 in (3, 7) and b<400) or b in (152, 168):
            note(b, 16, 12, 12, chord[2]+12, 19 if not peak else 24, 12)
        if peak and b%4==2:
            swell(b, 13, 20, chord[2]+12, 2, 21)
        if b%32==30 and b<400:
            swell(b, 14, 17, 62, 2, 22)
        if b%16==0 and b>=176 and b<400:
            note(b, 0, 15, 14, 48, 23, 8)

    # Explicitly close sustained layers at section boundaries. The final
    # sixteen bars contain rhythm only, ready for another track's bass/key.
    for b, lanes in [(272, (4, 5)), (400, (10, 11, 13, 14)),
                     (416, (6, 7, 8, 9, 12)), (432, (4, 5))]:
        for lane in lanes:
            cell(b, 0, lane, volume=0, pitch=-2)
    # The closing drum groove is not faded. Stop on the next bar and let wet
    # returns decay in the terminal order.
    for lane in range(16):
        events[(112, 0, lane)] = [0, -3, 0, 0, 0]
    events[(112, 127, 0)] = [0, -1, 0, ord('B'), 112]


def section_name(bar):
    return next(name for start, name in reversed(SECTIONS) if bar >= start)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as f:
        f.write('# THRESHOLD / 75 BPM / D minor / 3 ticks / 24-minute performance draft\n')
        for p in range(113):
            label = section_name(p*4) if p<112 else 'SILENCE HOLD'
            f.write(f'#@pattern {p} {p:03d} {label}\n')
        for key, value in sorted(events.items()):
            f.write(','.join(map(str, (*key, *value)))+'\n')
    print(f'{len(events)} native cells; 448 musical bars; 1433.6 seconds plus tail')
