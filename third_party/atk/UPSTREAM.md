# Ambisonic Toolkit transform subset

Source: https://github.com/ambisonictoolkit/atk-reaper
Pinned commit: bfff697e3f3977f80a6a3941c35c79c8e316be83
Source file: plugins/libraries/atk/atkMatrixLibrary.jsfx-inc
License: LGPL-3.0-or-later (COPYING.LESSER and incorporated GPL text COPYING).
Copyright the ATK Community and Joseph Anderson, Josh Parmenter, Trond Lossius, 2013.

`foa_transform.h` adapts only generateFocusMatrix, generatePressMatrix,
generatePushMatrix and generateZoomMatrix to direct sparse matrix application
on FuMa WXYZ samples. The neutral matrices, axial orientation, gain terms and
transverse terms are retained. TapeSister supplies angle rotation, encoding,
decoding, control smoothing and its own interface. No REAPER or SuperCollider
runtime is required. No convolution kernels, HRTFs, graphics, or third-party
4x4 inversion code were copied.

The corresponding modified library source is included in this repository;
rebuild TapeSister with CMake to relink a modified version. The source release
and upstream notices accompany binary distributions.
