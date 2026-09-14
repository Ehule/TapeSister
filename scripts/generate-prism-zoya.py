#!/usr/bin/env python3
"""Bake Zoya as a field of particle density, never sampled contour lines.

Only this offline development tool evaluates soft volumes and diffuse wisps.
The application reads a bounded cloud of native pixel positions, local density
and region; it has no procedural anatomy, image decoder, RNG or field solver.
One cloud is mirrored at OUT. The latest reference guides the open, reaching
pose: powerful shoulders/thighs, a shaped waist, one projecting palm and a rear
arm dissolving into the field. Facial, garment and muscle outlines are omitted.
"""
from math import cos, exp, radians, sin, sqrt
from pathlib import Path

# region, strength, center x/y, radius x/y, angle in degrees. These overlapping
# soft masses carry volume only; none of their boundaries becomes a drawn path.
volumes = [
    # Head, swept-back hair, facing profile and neck.
    (0,.92,49,13,5.0,7.0,8), (0,.76,53,16,3.0,4.7,0),
    (0,.50,56,14,1.6,1.7,0), (0,.68,47,23,3.0,5.2,12),
    (0,.66,43,9,8.0,5.6,-20), (0,.62,39,15,5.6,7.3,20),
    (0,.43,34,22,5.8,6.3,30),
    # Heavy shoulders/chest taper into a compact waist and broad pelvis.
    (3,.91,39,32,7.0,6.3,-20), (3,.82,50,32,5.7,5.9,10),
    (3,.82,44,39,8.1,8.0,0), (3,.76,44,48,5.7,7.6,-10),
    (3,.73,42,55,4.7,6.1,-15),
    (4,.88,39,64,7.3,7.5,20), (4,.84,48,65,6.8,7.6,-15),
    # Reaching arm slopes toward the original signal; palm at its height.
    (2,.86,56,37,7.5,4.3,34), (2,.74,64,43,5.1,3.5,30),
    (2,.76,72,48,7.8,3.0,24), (2,.70,81,52,4.6,2.4,22),
    (2,.93,87,53,3.0,2.8,-25), (2,.61,89,50,1.3,2.7,25),
    # Rear arm opens down/back and erodes into the surrounding haze.
    (1,.70,32,40,4.7,8.1,26), (1,.60,26,49,3.9,6.0,30),
    (1,.58,20,56,3.3,6.6,42), (1,.65,15,62,3.4,3.6,-25),
    # Rear leg: substantial upper mass, knee, calf and planted foot.
    (5,.91,33,76,6.5,12.2,19), (5,.70,28,88,4.1,5.0,10),
    (5,.78,25,99,4.4,9.0,8), (5,.63,24,109,2.6,5.2,0),
    (5,.74,28,114,5.6,2.0,8),
    # Forward leg: full quad/calf, slightly bent and planted farther ahead.
    (6,.91,51,77,6.6,12.0,-15), (6,.73,55,89,4.0,4.7,-15),
    (6,.81,59,99,4.5,8.8,-19), (6,.62,63,109,2.4,5.0,-19),
    (6,.79,68,115,6.2,2.0,5),
]
# Low-density clouds interrupt the boundary and leave generous negative space.
# They are STATIC baked matter, not an independent idle animation or outline.
wisps = [
    (0,.12,30,18,12,9,-30), (0,.07,20,27,11,6,20),
    (1,.10,15,46,11,14,20), (1,.08,9,66,8,11,-30),
    (3,.07,28,51,10,7,-20), (4,.11,28,68,12,9,-35),
    (5,.10,17,83,10,10,15), (5,.08,18,104,10,12,-10),
    (6,.09,65,82,10,9,-10), (6,.11,76,105,11,9,25),
    (6,.08,78,114,12,5,0), (2,.09,77,58,12,5,0),
]

def hash_unit(x,y,salt):
    n=(x*0x1f123bb5+y*0x5f356495+salt*0x6c8e9cf5)&0xffffffff
    n^=n>>16;n=(n*0x7feb352d)&0xffffffff
    n^=n>>15;n=(n*0x846ca68b)&0xffffffff;n^=n>>16
    return n/4294967296

def noise(x,y):
    # Low-frequency erosion modulates filled masses, not their contours.
    ix,iy=int(x//5),int(y//5);fx,fy=x/5-ix,y/5-iy
    fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy)
    a=hash_unit(ix,iy,7)*(1-fx)+hash_unit(ix+1,iy,7)*fx
    b=hash_unit(ix,iy+1,7)*(1-fx)+hash_unit(ix+1,iy+1,7)*fx
    return a*(1-fy)+b*fy

def prepare(fields):
    return [(r,w,cx,cy,rx,ry,cos(radians(a)),sin(radians(a)))
            for r,w,cx,cy,rx,ry,a in fields]

def field_at(x,y,fields):
    best,region=0,0
    for r,w,cx,cy,rx,ry,c,s in fields:
        dx,dy=x-cx,y-cy
        q=((c*dx+s*dy)/rx)**2+((-s*dx+c*dy)/ry)**2
        if q>5:continue
        value=w*exp(-1.25*q)
        if value>best:best,region=value,r
    return best,region

core,haze=prepare(volumes),prepare(wisps)
points=[]
for y in range(120):
    for x in range(94):
        mass,region=field_at(x,y,core)
        halo,halo_region=field_at(x,y,haze)
        if halo>mass:region=halo_region
        matter=max(mass,halo)*(.55+.70*noise(x,y))
        if matter<.008:continue
        # Probability fills the interior most densely and erodes the periphery.
        # Brightness also follows local mass; no preferred boundary samples.
        probability=min(.94,.92*sqrt(matter))
        if hash_unit(x,y,43)>=probability:continue
        density=round(255*min(1,sqrt(matter)*(.73+.27*hash_unit(x,y,91))))
        points.append((x,y,max(1,density),region))

out=['/* Generated density field; same cloud is mirrored at OUT. No contour samples. */',
     'typedef struct { uint8_t x, y, density, region; } TsPrismZoyaPoint;',
     'static const TsPrismZoyaPoint prism_zoya_points[] = {']
out += [f'    {{{x},{y},{d},{r}}},' for x,y,d,r in points]
out += ['};',f'enum {{ TS_PRISM_ZOYA_POINTS = {len(points)} }};','']
target=Path(__file__).resolve().parents[1]/'src/ts_prism_zoya_points.inc'
target.write_text('\n'.join(out))
print(f'{len(points)} density-field points, {len(points)*4} bytes: {target}')
