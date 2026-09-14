#!/usr/bin/env python3
"""Bake Zoya as a field of particle density, never sampled contour lines.

Only this offline development tool evaluates soft volumes and diffuse wisps.
The application reads a bounded cloud of native pixel positions, local density
and region; it has no procedural anatomy, image decoder, RNG or field solver.
One cloud is mirrored at OUT. Zoya sits upright with crossed legs and both
hands resting on her knees; the output holds the same pose slightly aloft.
Broad shoulders, a shaped waist and substantial thighs suggest her character.
Facial, garment and muscle outlines are omitted.
"""
from math import cos, exp, radians, sin, sqrt
from pathlib import Path

# region, strength, center x/y, radius x/y, angle in degrees. These overlapping
# soft masses carry volume only; none of their boundaries becomes a drawn path.
volumes = [
    # Upright head, diffuse swept hair and a relaxed, centered neck.
    (0,.92,46,26,5.7,7.6,0), (0,.70,46,36,3.0,4.5,0),
    (0,.67,44,19,6.7,5.0,-8), (0,.57,39,25,4.5,6.8,8),
    (0,.45,53,23,3.8,5.4,-8), (0,.30,37,33,4.6,5.4,-15),
    # Level shoulders, upright chest, soft waist and seated pelvis.
    (3,.87,36,47,6.2,4.8,-12), (3,.87,56,47,6.2,4.8,12),
    (3,.85,46,52,9.2,8.8,0), (3,.79,46,62,7.2,8.4,0),
    (3,.73,46,72,6.1,7.2,0), (4,.86,46,83,10.0,7.3,0),
    # Arms hang loosely outward; open hands rest on the outer knees.
    # Space between forearms and waist keeps the posture legible at native size.
    (1,.81,31,56,4.1,8.3,15), (1,.73,27,68,3.6,5.0,14),
    (1,.75,23,80,3.2,8.0,24), (1,.88,20,90,4.3,2.7,-8),
    (2,.81,61,56,4.1,8.3,-15), (2,.73,65,68,3.6,5.0,-14),
    (2,.75,69,80,3.2,8.0,-24), (2,.88,72,90,4.3,2.7,8),
    # Full thighs open outward into a stable cross-legged seat.
    (5,.87,33,90,12.5,6.3,-25), (5,.84,20,95,7.0,5.0,0),
    (6,.87,59,90,12.5,6.3,25), (6,.84,72,95,7.0,5.0,0),
    # Folded shins cross low in front; feet dissolve into the opposite thigh.
    (5,.79,38,102,19.0,4.4,16), (5,.69,57,105,6.1,3.0,-8),
    (6,.77,54,102,19.0,4.4,-16), (6,.65,35,105,6.1,3.0,8),
]
# Low-density clouds interrupt the boundary and leave generous negative space.
# They are STATIC baked matter, not an independent idle animation or outline.
wisps = [
    (0,.11,34,24,9,9,-20), (0,.07,56,29,8,10,15),
    (1,.08,24,52,8,10,10), (2,.09,70,57,8,12,-15),
    (1,.07,13,77,8,10,20), (2,.08,80,80,8,10,-20),
    (5,.11,12,96,9,8,15), (6,.10,80,98,9,8,-15),
    (5,.09,31,111,15,4,-5), (6,.08,60,111,15,4,5),
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
