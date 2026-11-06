# SunVox DSP reference

Official source: https://warmplace.ru/soft/sunvox/sunvox_lib-2.1.4d.zip

Archive SHA-256: `abe851de9d65a10e06673bf33257154591a2ff63ad2bb298488a5941cc5f4057`

`lib_sunvox/psynth/psynth_dsp.cpp` SHA-256: `0a2188c44008e14f4715c43f52fa872242e029541a36cfff2c886c121b8e665c`

Only all-pass coefficient construction was adapted into `src/ts_texture_fx.c`.
The SunVox engine, binary libraries and bundled user patches are not included.
The coefficient notice and complete MIT license are retained alongside this file
and in the shipped third-party notices. TapeSister supplies the surrounding
network, state, parameter smoothing, stereo spread and integration.
