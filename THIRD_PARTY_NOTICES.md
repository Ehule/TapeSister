# Third-party runtime notices

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
