TAPESISTER PORTABLE RELEASE
===========================

TapeSister is portable software. It does not use an installer and does not need
administrator access. Keep the complete extracted folder together so the audio
runtime, video decoder, artwork, palette, CDP8 programs, and licenses remain available.

WINDOWS
-------
1. Extract the complete TapeSister-Windows-x64 archive to a writable folder.
2. Open that folder and double-click TapeSister.exe.
3. Do not run TapeSister.exe from inside the ZIP file.

The GitHub Actions Windows download needs only one extraction. The resulting
folder contains TapeSister.exe, its DLLs, assets, cdp, licenses and docs directly;
there is no second TapeSister ZIP to extract. Keep cdp/bin and all its programs.

TapeSister writes its settings, presets, diagnostic log, and Captures folder in
portable locations associated with the extracted folder. SDL2 and the MinGW DLLs
must remain beside TapeSister.exe. The assets, cdp, and licenses directories must
also remain in place.

For coexistence with Tapehead, REAPER, or VB-CABLE, start with Auto or WASAPI
shared mode and matching sample rates. Native ASIO is also included: install the
interface's 64-bit driver, select ASIO in CFG, choose the duplex driver and buffer,
then SAVE CONFIG. Backend/device/buffer changes apply without closing the app;
finish or cancel recording first. F12 shows the actual rate, buffer and channels.
See docs/ASIO_AUDIO.md for MOTU/VB-Audio Matrix setup and driver-sharing limits.

F9 opens the unified routing matrix. Default audio goes through Main, EQ and the
limiter to OUT 1/2. External hardware uses explicit output/input connections.
Activity indicators, Level/Row Pan wheel edits and right-click resets are covered
in docs/ROUTING_MATRIX.md, along with Clear All, Reset to Default and I/O naming.
MATRIX LIVE means active routing, not a patch-state sequencer. Existing projects
retain their old engine until explicitly converted.

Save/copy the whole named project folder to keep routing, hardware names,
TrackSister notes/recorded velocities and all audio. Older projects may have an
adjacent .tsr.samples folder; keep that with the TSR.
See docs/USER_MANUAL.md for the illustrated manual, including Mosaic, and
docs/QUICK_REFERENCE.md for controls and shortcuts. docs/MOSAIC.md covers the
arrangement workflow in detail. Linked guides and screenshots are included in docs/.

LINUX
-----
The GitHub Actions Linux download is the .tar.gz itself, without an outer ZIP.
Extract the complete TapeSister-linux-x86_64.tar.gz archive to a writable folder
and run ./tapesister there. Keep assets, cdp and licenses with the executable.
The x86-64 release targets Ubuntu 22.04 / Linux Mint 21 or newer (glibc 2.35+).
The SDL2 and ALSA runtime libraries must be installed. On Ubuntu 22.04 / Mint 21:

  sudo apt install libsdl2-2.0-0 libasound2

If double-clicking opens no window, open a terminal in the extracted folder and
run ./tapesister 2>&1 to see the startup error. A GLIBC or GLIBCXX version error
means that build needs a newer runtime; use the release built for this baseline.
The docs folder contains the same manual, quick reference, guides and screenshots.
JACK users should read docs/JACK_AUDIO.md; the runtime and a running JACK server
(or the distribution's PipeWire JACK setup) must be available. Audio setup applies
from CFG without closing the application.

BUILDING THE WINDOWS ARCHIVE
----------------------------
Install MSYS2 and these UCRT64 packages once:

  git
  mingw-w64-ucrt-x86_64-toolchain
  mingw-w64-ucrt-x86_64-cmake
  mingw-w64-ucrt-x86_64-ninja
  mingw-w64-ucrt-x86_64-SDL2

Then open an MSYS2 UCRT64 terminal in the repository and run:

  powershell.exe -ExecutionPolicy Bypass -File scripts/build-windows-portable.ps1

The shareable archive is written to dist/TapeSister-Windows-x64.zip.

LICENSES AND SOURCE
-------------------
Third-party notices and the exact source archive for the bundled CDP8 runtime
are included under licenses/. Additional notices are in
THIRD_PARTY_NOTICES.md. TapeSister source is available from:

  https://github.com/Ehule/TapeSister

VISUALIZER VIDEO
----------------
F11 > EDIT VISUALIZER > VIDEO > ADD VIDEO FILE. Keep all FFmpeg DLLs beside
TapeSister.exe on Windows and video/lib beside tapesister on Linux. Video sound
is off. Mosaic file/folder drops or right-clicking empty canvas space create video change objects. Right-click
for random positions and transitions; Ctrl+R scatters copies. VIDEO tab > MANDALA
OFF shows clean footage. Local playback copies prepare automatically; keep your
original video folders available when moving projects. See docs/MANDALA_VIDEO.md for playback, formats and project references.
