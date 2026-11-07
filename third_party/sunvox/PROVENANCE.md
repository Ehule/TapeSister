# Embedded SunVox engine and effects

Powered by SunVox (modular synth & tracker)
Copyright (c) 2008 - 2026, Alexander Zolotov <nightradio@gmail.com>, WarmPlace.ru

Engine: official [SunVox 2.1.4d developer package](https://warmplace.ru/soft/sunvox/sunvox_lib-2.1.4d.zip).
Original ZIP SHA-256: `abe851de9d65a10e06673bf33257154591a2ff63ad2bb298488a5941cc5f4057`.

Factory effects: all 235 `.sunsynth` files under `instruments/effects` at
[warmplace/sunvox commit c1fc021](https://github.com/warmplace/sunvox/tree/c1fc021c3a20fde37b62d366c708b05ad60ca047/instruments/effects).
The original names, author prefixes, categories and bytes are preserved.
`EFFECTS.tsv` lists each path, size and SHA-256. The distribution's freeware
redistribution notice is retained as `licenses/distribution.txt`.

`sunvox-2.1.4d.tar.gz` is a source archive containing the unmodified SDK
`lib_sunvox`, `lib_sundog`, `lib_dsp`, `lib_flac`, `lib_mp3`, `lib_vorbis`
directories and the effect directory renamed to `effects`.
Archive SHA-256: `cbe39f7fdbee6ab51ab832b7ea693d027e7d294666f397870281bc2177e71d75`.
No precompiled library or standalone SunVox application is included.

CMake verifies this archive, compiles the headless float32 engine and decoders,
and embeds the patches into the application. The host is `src/ts_sunvox_fx.cpp`;
upstream source files are not patched. Main-thread loading and independent engines
allow multiple Matrix instances with separate input, controls and DSP history.

The engine and SunDog source notices are MIT. The SDK's additional attribution
requirements and its accompanying notices are copied to `licenses/`, including
Tremor, Ogg, FLAC and dr_mp3. Release bundles include this folder, this provenance
and the original MIT notice. The existing all-pass coefficient adaptation in
`src/ts_texture_fx.c` still uses the same upstream DSP reference.
