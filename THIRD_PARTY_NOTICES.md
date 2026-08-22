# Third-party runtime notices

TapeSister can optionally execute a curated subset of programs from the Composers
Desktop Project (CDP): `blur`, `distmore`, `distort`, `distshift`, `extend`, `filter`,
`freeze`, `glisten`, `grain`, `hover`, `modify`, `motor`, `pvoc`, `scramble`, `sorter`,
`splinter`, and `stutter`. CDP source identifies the work as copyright 1983–2023
Trevor Wishart and Composers Desktop Project Ltd and licenses it under the GNU Lesser
General Public License, version 2.1 or (at the user's option) any later version.

TapeSister does not copy or distribute CDP source, binaries, libraries, or the supplied CDP
and SoundThread archives. A compatible runtime is discovered externally. A distributor
who bundles CDP must independently satisfy the LGPL and all dependency licenses,
including providing the applicable license notices, corresponding source or compliant
source offer, reproducible relinking/build information where required, and a record of
local modifications. The authoritative license for a bundled runtime is the `LICENSE`
file from that exact CDP source release.

SOMA Laboratory WARP is an interaction reference only. TapeSister contains no SOMA
branding, artwork, panel graphics, source code, proprietary algorithms, or trade dress.
SoundThread was inspected as a technical reference; none of its Godot UI, graph model,
or archive is included.

TapeSister includes RtMidi 6.0.0 for portable MIDI input on Windows, macOS, and
Linux. RtMidi is copyright 2003–2023 Gary P. Scavone and is distributed under
its permissive license in `third_party/rtmidi/LICENSE.txt`. The vendored source
matches the Tapehead Edition copy, including its WinMM callback-lock fix.
