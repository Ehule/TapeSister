/* Native C adaptation of ATK for REAPER's generateFocus/Press/Push/ZoomMatrix
 * from plugins/libraries/atk/atkMatrixLibrary.jsfx-inc, commit
 * bfff697e3f3977f80a6a3941c35c79c8e316be83.
 * Copyright the ATK Community and Joseph Anderson, Josh Parmenter, Trond Lossius, 2013.
 * LGPL-3.0-or-later. See COPYING and COPYING.LESSER in this directory.
 * Modified for TapeSister: apply the sparse axial matrix directly to FuMa WXYZ.
 */
#ifndef TS_ATK_FOA_TRANSFORM_H
#define TS_ATK_FOA_TRANSFORM_H
#include <math.h>
static inline void ts_atk_axial(float f[4], int kind, float angle)
{
    const float root2 = 1.4142135623730951f;
    float sine = sinf(angle), cosine = cosf(angle), w = f[0], x = f[1];
    if (kind == 0 || kind == 3) { /* Focus / Zoom */
        float gain = kind == 0 ? 1.f / (1.f + fabsf(sine)) : 1.f;
        f[0] = (w + sine * x / root2) * gain;
        f[1] = (x + sine * root2 * w) * gain;
        f[2] *= cosine * gain; f[3] *= cosine * gain;
    } else { /* Press / Push */
        f[1] = root2 * sine * fabsf(sine) * w + cosine * cosine * x;
        float transverse = kind == 1 ? cosine : cosine * cosine;
        f[2] *= transverse; f[3] *= transverse;
    }
}
#endif
