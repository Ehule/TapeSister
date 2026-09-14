#!/usr/bin/env python3
"""Bake sparse native-pixel line art into the shared Zoya point cloud.

No runtime paths, image decoder, RNG, tessellation, rig, or sprite textures.
The hand-authored profile faces the optical path; the renderer mirrors the SAME
points at the output. Broad shoulders/arms, shaped waist/hips and powerful thighs
and calves carry the character at 88 x 120 logical pixels. Curl/flower, corset and
muscle contours refer to TapeSister's existing visual language.
"""
from pathlib import Path
from math import ceil, hypot

# (importance, anatomical region, control points). Regions only distribute
# audio-driven displacement; they are not an animation skeleton.
paths = [
    # Hair and right-facing profile, neck, tied-back curls.
    (3,0,[(28,23),(24,19),(24,14),(26,11),(25,7),(28,4),(32,4),(34,1),(38,2),(41,1),(46,4),(48,8),(45,11)]),
    (3,0,[(42,10),(46,11),(46,14),(50,17),(47,18),(47,21),(43,24),(38,23),(36,20)]),
    (3,0,[(38,23),(37,28),(32,29),(28,27),(30,22)]),
    (2,0,[(43,23),(42,27),(46,29)]),
    (2,0,[(29,5),(32,7),(30,10),(27,10),(27,8)]),
    (2,0,[(35,4),(38,6),(36,9),(33,9),(33,7)]),
    (2,0,[(42,4),(45,7),(43,10),(40,9),(40,7)]),
    (2,0,[(29,12),(32,11),(35,13),(34,17),(31,17),(30,15)]),
    (2,0,[(26,17),(27,21),(31,22),(34,20),(34,18)]),
    (3,0,[(39,14),(43,14),(44,15)]),
    (2,0,[(46,20),(44,20)]),
    # Small flower/coil behind her ear, not a large halo.
    (2,0,[(25,16),(22,15),(20,17),(22,19),(24,18),(25,16)]),
    (2,0,[(23,20),(21,21),(23,24),(26,23),(26,21),(23,20)]),
    # Back arm: deltoid, biceps, forearm and relaxed hand.
    (3,1,[(28,28),(22,28),(17,31),(14,36),(13,42),(10,48),(8,57),(6,62),(8,65),(12,64),(15,60),(16,54),(20,49),(23,45),(25,38)]),
    (2,1,[(20,32),(18,36),(18,41),(21,44),(23,41),(24,35)]),
    (2,1,[(14,43),(16,47),(13,52),(11,58)]),
    (2,1,[(8,61),(10,62),(11,60)]),
    # Front arm: large deltoid/biceps into an open palm at signal height (57).
    (3,2,[(44,28),(50,29),(55,33),(57,39),(56,42),(62,46),(68,50),(75,51),(79,48),(80,49),(78,54),(82,54),(86,52),(87,54),(85,57),(81,59),(75,59),(68,56),(61,54),(55,51),(50,46),(46,41)]),
    (2,2,[(48,32),(52,33),(54,37),(53,41),(49,41),(47,37)]),
    (2,2,[(55,44),(57,48),(61,51),(65,52)]),
    (2,2,[(70,53),(75,55),(79,55)]),
    (2,2,[(81,57),(85,55)]),
    # Feminine torso with broad chest, fitted waist and corset seams.
    (3,3,[(26,35),(26,42),(28,49),(31,54),(30,59),(26,64),(23,69)]),
    (3,3,[(46,38),(47,44),(44,51),(41,56),(43,61),(48,66),(51,72)]),
    (2,3,[(29,31),(33,35),(39,35),(43,31)]),
    (2,3,[(28,38),(32,37),(36,40),(41,38),(45,38)]),
    (2,3,[(28,43),(32,45),(36,44),(39,46),(44,43)]),
    (2,3,[(29,46),(34,56),(33,59)]),
    (2,3,[(42,46),(37,55),(37,59)]),
    (3,3,[(30,59),(35,61),(42,59)]),
    (2,3,[(35,54),(37,54),(35,56),(37,58),(35,58)]),
    # Pelvis and briefs: stance opens into two substantial legs.
    (3,4,[(25,65),(31,67),(36,73),(42,68),(46,64)]),
    (2,4,[(28,62),(34,64),(41,62)]),
    # Rear planted leg: thick thigh, patella, calf, ankle and foot.
    (3,5,[(23,69),(20,76),(20,83),(22,88),(23,92),(21,98),(21,105),(20,111),(17,114),(16,117),(21,118),(28,118),(31,116),(30,113),(28,110),(29,104),(32,98),(35,93),(36,87),(35,80),(36,74)]),
    (2,5,[(25,72),(24,78),(25,85),(28,88),(31,86),(33,81),(33,77)]),
    (2,5,[(26,91),(29,92),(31,90)]),
    (2,5,[(27,96),(25,101),(25,107)]),
    (2,5,[(21,114),(26,115),(28,114)]),
    # Forward leg: powerful quadriceps, diagonal stance and heavy calf.
    (3,6,[(49,68),(53,75),(57,82),(60,88),(60,93),(62,99),(66,105),(68,110),(72,113),(71,116),(67,117),(62,115),(59,112),(56,107),(53,103),(49,96),(47,90),(42,84),(38,80),(36,74)]),
    (2,6,[(46,71),(47,78),(51,85),(55,88),(56,84),(53,78)]),
    (2,6,[(41,74),(42,79),(47,85),(50,88)]),
    (2,6,[(51,91),(54,94),(57,93)]),
    (2,6,[(56,98),(57,102),(61,107),(63,108)]),
    (2,6,[(61,113),(66,115),(69,114)]),
]

points = {}
def put(x,y,importance,region):
    x,y=round(x),round(y)
    if 0<=x<=87 and 0<=y<=119:
        old=points.get((x,y))
        if old is None or importance>old[0]: points[x,y]=(importance,region)

# Bake Catmull-Rom paths to one-pixel samples; no smoothing/AA during playback.
for importance,region,path in paths:
    for i in range(len(path)-1):
        p0,p1,p2,p3=path[max(0,i-1)],path[i],path[i+1],path[min(len(path)-1,i+2)]
        steps=max(2,ceil(hypot(p2[0]-p1[0],p2[1]-p1[1])*2))
        for j in range(steps+1):
            t=j/steps
            xy=[.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t)
                for a,b,c,d in zip(p0,p1,p2,p3)]
            put(*xy,importance,region)

# Sparse interior point bands imply volume without filling a conventional sprite.
for cx,cy,rx,ry,region in [(20,36,4,6,1),(51,36,4,6,2),(31,41,4,3,3),
                           (41,42,3,3,3),(27,79,5,8,5),(48,79,5,7,6),
                           (25,100,2,7,5),(57,101,3,5,6)]:
    for y in range(cy-ry,cy+ry+1):
        for x in range(cx-rx,cx+rx+1):
            if ((x-cx)/rx)**2+((y-cy)/ry)**2<.85 and (x*13+y*7)%9==0:
                put(x,y,1,region)

out=['/* Generated by scripts/generate-prism-zoya.py; same cloud is mirrored at OUT. */',
     'typedef struct { uint8_t x, y, importance, region; } TsPrismZoyaPoint;',
     'static const TsPrismZoyaPoint prism_zoya_points[] = {']
for (x,y),(weight,region) in sorted(points.items(),key=lambda v:(v[0][1],v[0][0])):
    out.append(f'    {{{x},{y},{weight},{region}}},')
out+=['};',f'enum {{ TS_PRISM_ZOYA_POINTS = {len(points)} }};','']
target=Path(__file__).resolve().parents[1]/'src/ts_prism_zoya_points.inc'
target.write_text('\n'.join(out))
print(f'{len(points)} precomputed points, {len(points)*4} bytes: {target}')
