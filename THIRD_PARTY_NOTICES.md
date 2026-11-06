# Third-party runtime notices

TapeSister embeds application sources from FT2 TapeHead Edition at commit
`f053d96df3996a5fd3b26625f069888c3a7d24ab`, based on Olav Sorensen's
FT2 clone under the BSD 3-Clause license. Source notices, original input hashes
and the host integration patch are retained in `third_party/tapehead/application`.
Binary bundles include the code/font licenses and source provenance under
`licenses/tapehead`. TapeHead's upstream repository is not modified.

SisterTracker reuses the original Tapehead/FT2 bitmap fonts by Magnus "Vogue"
Hogdahl, edited by Olav Sorensen, under CC BY-NC-SA 4.0. Packed font pixels
retain the original artwork. Font width tables and adapted glyph drawing are
under Olav Sorensen's BSD 3-Clause license. Source attribution and both license
texts are retained in `third_party/tapehead/` and `licenses/tapehead/` in bundles.

TapeSister release builds execute a curated subset of programs from the Composers
Desktop Project (CDP): `blur`, `distmore`, `distort`, `distshift`, `extend`, `filter`,
`glisten`, `grain`, `hover`, `modify`, `motor`, `pvoc`, `scramble`, `sorter`,
`splinter`, `stretch`, and `stutter`. CDP source identifies the work as copyright 1983–2023
Trevor Wishart and Composers Desktop Project Ltd and licenses it under the GNU Lesser
General Public License, version 2.1 or (at the user's option) any later version.

The standard native bundle builds unmodified CDP8 source pinned at commit
`28bc42c72c1a7cb0fab933acd1c433be958a787b`. CDP programs are shipped as separate
executables under `cdp/bin`; they are not linked into TapeSister. Each generated bundle
also includes the upstream LGPL text, exact corresponding source archive, build and
relinking information, and this notice under `licenses/`. See `docs/CDP8_RUNTIME.md`.
SoundThread source and binaries are not distributed.

SOMA Laboratory WARP is an interaction reference only. TapeSister contains no SOMA
branding, artwork, panel graphics, source code, proprietary algorithms, or trade dress.
SoundThread was inspected as a technical reference; none of its Godot UI, graph model,
or archive is included.

Fallout is an original C reimplementation inspired by Bahiamansa's freely shared
`failure_v2` ppooll act for Max. TapeSister does not include the Max patch, ppooll,
Cycling '74 code, binaries, artwork, or UI assets. Credit for the source concept and
the original patch belongs to Bahiamansa; the TapeSister implementation is released
under the repository's MIT license.

TapeSister includes RtMidi 6.0.0 for portable MIDI input on Windows, macOS, and
Linux. RtMidi is copyright 2003–2023 Gary P. Scavone and is distributed under
its permissive license in `third_party/rtmidi/LICENSE.txt`. The vendored source
matches the Tapehead Edition copy, including its WinMM callback-lock fix.

TapeSister uses miniaudio 0.11.22, pinned at commit
`350784a9467a79d0fa65802132668e5afbcf3777`, for in-process FLAC and MP3 decoding.
miniaudio is copyright David Reid and is available under either the public domain or
MIT No Attribution license. Its bundled `stb_vorbis` decoder provides Ogg Vorbis
support and is likewise available under either the public domain or MIT license.

Windows ASIO support uses RtAudio, copyright 2001–2023 Gary P. Scavone, pinned at
`c0a533d7bb16e8ca0d96cdb2e3fcfb6d1d095df4`. Its permissive license is retained in
`third_party/rtaudio/LICENSE` and included in Windows bundles. The upstream ASIO
interface/loader files carry Steinberg's notices; see
`third_party/rtaudio/include/asioinfo.txt`. ASIO is a trademark of Steinberg Media
Technologies GmbH. A local RtAudio change reads the current ASIO sample rate.

## Ambisonic Toolkit spatial transformations

The Focus, Press, Push and Zoom axial transforms in
`third_party/atk/foa_transform.h` are adapted from ATK for REAPER,
https://github.com/ambisonictoolkit/atk-reaper, commit
`bfff697e3f3977f80a6a3941c35c79c8e316be83`.
Copyright the ATK Community and Joseph Anderson, Josh Parmenter, Trond Lossius, 2013.
Licensed under LGPL-3.0-or-later. The complete license texts and adaptation
notes are in `third_party/atk/` and staged in `licenses/atk/` with releases.
The modified library source is provided and can be rebuilt/relinked using
TapeSister's CMake build. No convolution or HRTF assets are included.

## FFmpeg video playback and preparation

The optional visualizer player dynamically links FFmpeg 7.1.4 (libavformat,
libavcodec, libswscale and libavutil), copyright the FFmpeg developers, under
LGPL 2.1 or later. The bundled build uses no GPL/nonfree components, external
codec libraries, network protocols or FFmpeg command-line programs. The native
LGPL MJPEG encoder and AVI muxer prepare local playback copies for quick seeking.
`cmake/VideoRuntime.cmake` pins the unmodified release archive and SHA-256.
Portable bundles retain that exact source archive, license texts, configuration
log and build recipe under `licenses/ffmpeg`. The libraries remain replaceable:
Windows keeps `avformat-61.dll`, `avcodec-61.dll`, `swscale-8.dll`, `avutil-59.dll`
next to TapeSister.exe; Linux keeps their shared libraries under `video/lib`.
To rebuild, unpack the supplied archive, configure with the flags in the recipe
(and a new prefix), run make and make install, then replace the ABI-compatible
shared libraries. `TAPESISTER_BUNDLE_VIDEO=OFF` links system FFmpeg via pkg-config.
TapeSister does not restrict modification, replacement, or reverse engineering
of these libraries for debugging such modifications. See [FFmpeg](https://ffmpeg.org/).

## Still-image decoding

The visual child uses the existing vendored `third_party/stb_image.h` by Sean Barrett and contributors for PNG, JPEG, BMP and TGA decoding. This copy is available under the MIT license or public-domain alternative in the header. The private implementation is independent of the host's PNG splash decoder and adds no runtime dependency.

## SunVox all-pass DSP

Dispersion coefficient construction in `src/ts_texture_fx.c` is adapted from
`lib_sunvox/psynth/psynth_dsp.cpp` in the official SunVox 2.1.4d developer
package: https://warmplace.ru/soft/sunvox/sunvox_lib-2.1.4d.zip.
The complete SunVox runtime is not linked or distributed.
The following notice applies to this adaptation:

SunVox library is distributed under the MIT license:

Copyright (c) 2007 - 2025, Alexander Zolotov <nightradio@gmail.com>
WarmPlace.ru

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
