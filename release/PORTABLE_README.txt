TAPESISTER PORTABLE RELEASE
===========================

TapeSister is portable software. It does not use an installer and does not need
administrator access. Keep the complete extracted folder together so the audio
runtime, artwork, palette, CDP8 programs, and licenses remain available.

WINDOWS
-------
1. Extract the complete TapeSister-Windows-x64 archive to a writable folder.
2. Open that folder and double-click TapeSister.exe.
3. Do not run TapeSister.exe from inside the ZIP file.

TapeSister writes its settings, presets, diagnostic log, and Captures folder in
portable locations associated with the extracted folder. SDL2 and the MinGW DLLs
must remain beside TapeSister.exe. The assets, cdp, and licenses directories must
also remain in place.

For coexistence with Tapehead, REAPER, or VB-CABLE, start with Auto or WASAPI
shared mode and matching sample rates. TapeSister does not provide native ASIO.
See docs/USER_MANUAL.md for the illustrated manual, including Mosaic, and
docs/QUICK_REFERENCE.md for controls and shortcuts. docs/MOSAIC.md covers the
arrangement workflow in detail. Linked guides and screenshots are included in docs/.

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
