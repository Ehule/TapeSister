#!/usr/bin/env python3
"""Bake sparse native-pixel line art into the shared Zoya point cloud.

No runtime paths, image decoder, RNG, tessellation, rig, or sprite textures.
The hand-authored profile faces the optical path; the renderer mirrors the SAME
points at the output. Broad shoulders/arms, shaped waist/hips and powerful thighs
and calves carry the character at 94 x 120 logical pixels. Curl/flower, bodice and
muscle contours refer to TapeSister's existing visual language.
"""
from pathlib import Path
from math import ceil, hypot

# (importance, anatomical region, control points). Regions only distribute
# audio-driven displacement; they are not an animation skeleton.
# Design-space coordinates keep the anatomy readable while authoring. They are
# baked to a 94 x 120 native-pixel cloud below. The stance follows the approved
# reference: rear leg braced back, front leg planted forward, BOTH arms projecting
# toward the lens, fingers open. No reference image is loaded at runtime.
paths = [
    # Swept curls and a right-facing profile; a tied-back coil behind the ear.
    (3,0,[(116,345),(106,337),(104,327),(109,317),(120,314),(120,305),(128,299),(137,301),(143,295),(153,297),(159,295),(169,299),(177,299),(182,305),(180,314),(174,318)]),
    (3,0,[(164,316),(174,316),(174,323),(180,329),(175,332),(175,338),(169,342),(158,340),(153,334)]),
    (2,0,[(163,321),(169,322),(171,324)]),
    (2,0,[(173,335),(168,335)]),
    (3,0,[(157,338),(153,350),(163,356)]),
    (3,0,[(140,337),(141,349),(132,354)]),
    (2,0,[(126,305),(132,304),(136,309),(131,313),(127,311),(126,305)]),
    (2,0,[(140,302),(147,301),(151,305),(147,310),(142,309),(140,306)]),
    (2,0,[(155,302),(162,304),(163,309),(158,312),(154,308)]),
    (2,0,[(168,305),(173,308),(171,313)]),
    (2,0,[(117,318),(123,315),(127,320),(124,325),(117,324)]),
    (2,0,[(134,314),(141,315),(143,321),(137,325),(132,321),(134,314)]),
    (2,0,[(114,330),(120,328),(125,332),(122,339),(115,338),(114,330)]),
    (2,0,[(129,330),(136,328),(140,331),(137,337),(130,340),(126,337)]),
    (2,0,[(145,323),(150,322),(152,328),(149,332),(146,329)]),
    # Small flower behind the curls.
    (3,0,[(110,319),(106,314),(102,317),(104,323),(98,321),(96,326),(101,330),(100,335),(105,337),(110,332),(114,334),(118,330),(114,325),(117,321),(113,318),(110,319)]),
    (2,0,[(106,324),(110,322),(113,326),(110,330),(106,329),(106,324)]),
    # Far shoulder and arm reach above the nearer arm. The open hand angles up.
    (3,1,[(156,354),(169,353),(180,360),(187,367),(202,369),(220,379),(241,383),(259,382),(270,375),(278,364),(281,361),(280,369),(275,381)]),
    (3,1,[(275,381),(285,370),(290,363),(292,364),(288,373),(280,385),(273,390),(259,391),(239,391),(220,388),(201,385),(181,381),(167,375)]),
    (2,1,[(171,359),(180,365),(180,371),(174,374)]),
    (2,1,[(187,374),(201,375),(215,381)]),
    (2,1,[(226,383),(243,387),(257,387)]),
    # Near deltoid, heavy triceps/biceps and extended forearm, palm and fingers.
    (3,2,[(134,353),(121,352),(113,358),(110,369),(113,380),(123,389),(141,394),(166,397),(183,395),(203,400),(224,402),(243,401),(258,396)]),
    (3,2,[(134,353),(146,358),(154,367),(170,375),(182,382),(196,386),(220,390),(241,393),(254,389),(266,379),(271,370),(274,370),(272,381),(265,391)]),
    (3,2,[(265,391),(275,383),(281,377),(284,378),(279,387),(271,394),(276,395),(283,393),(285,395),(278,400),(268,400),(258,396)]),
    (2,2,[(122,357),(131,358),(142,365),(145,375),(139,383),(128,384),(117,377)]),
    (2,2,[(146,377),(157,382),(170,384),(180,388)]),
    (2,2,[(141,390),(157,392),(173,390)]),
    (2,2,[(192,393),(214,396),(237,397),(253,393)]),
    # Torso turns toward the projecting hands: broad chest, narrow strong waist.
    (3,3,[(116,381),(119,395),(127,405),(136,414),(132,426),(117,435),(107,446)]),
    (3,3,[(167,399),(164,409),(155,420),(153,428),(160,440),(166,453)]),
    (2,3,[(137,347),(146,352),(153,361),(159,366)]),
    (2,3,[(135,397),(145,400),(157,399)]),
    (2,3,[(133,403),(141,413),(140,424)]),
    (2,3,[(157,405),(151,414),(147,428)]),
    (2,3,[(135,428),(142,431),(150,428)]),
    (2,3,[(136,434),(129,442),(126,450)]),
    # Hip and garment edge: no skirt, both quadriceps remain distinct.
    (3,4,[(112,440),(126,440),(138,449),(143,461),(155,452),(159,441)]),
    (2,4,[(111,448),(122,448),(132,455),(135,465)]),
    # Rear leg drives backward into a wide stance: large thigh and loaded calf.
    (3,5,[(107,446),(96,461),(91,479),(90,497),(83,515),(73,529),(58,547),(47,571),(39,591),(34,602),(29,609),(33,614),(48,614),(59,609),(60,601),(66,585),(81,566),(99,549),(108,534),(114,517),(126,497),(136,478),(140,462)]),
    (2,5,[(110,452),(105,466),(102,483),(104,495),(112,497),(122,486),(130,469)]),
    (2,5,[(98,501),(94,514),(99,519),(106,516)]),
    (2,5,[(85,530),(72,544),(67,558),(70,561),(83,551),(95,534)]),
    (2,5,[(57,567),(52,581),(47,595)]),
    (2,5,[(38,604),(45,607),(56,605)]),
    # Forward leg opens at the hip: sweeping quad, bent knee, full calf, flat foot.
    (3,6,[(165,450),(175,467),(186,490),(191,510),(190,526),(198,546),(206,568),(216,593),(220,603),(232,609),(237,613),(229,615),(213,613),(205,608),(201,596),(189,575),(178,555),(173,540),(158,522),(149,505),(137,487),(134,476),(140,462)]),
    (2,6,[(157,460),(164,473),(173,490),(178,505),(174,513),(167,507),(157,491),(147,476)]),
    (2,6,[(178,517),(182,523),(186,520)]),
    (2,6,[(181,535),(188,543),(197,563),(198,575),(191,571),(183,555)]),
    (2,6,[(204,594),(209,603),(219,606)]),
]

points = {}
def put(x,y,importance,region):
    x,y=round((x-28)*.35),round((y-294)*.37)
    if 0<=x<=93 and 0<=y<=119:
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
for cx,cy,rx,ry,region in [(129,370,13,12,2),(171,365,9,8,1),
                           (158,387,15,5,2),(207,395,20,4,2),
                           (111,478,10,21,5),(162,484,11,21,6),
                           (79,548,8,14,5),(189,551,7,15,6)]:
    for y in range(cy-ry,cy+ry+1):
        for x in range(cx-rx,cx+rx+1):
            if ((x-cx)/rx)**2+((y-cy)/ry)**2<.85 and (x*13+y*7)%23==0:
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
