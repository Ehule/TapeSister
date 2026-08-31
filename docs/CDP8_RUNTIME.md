# Shippable CDP8 runtime

TapeSister release builds include the exact native CDP8 programs required by the
curated transform catalog. Users of those release archives do not need to install or
configure CDP separately: TapeSister discovers `cdp/bin` beside its executable before
consulting `CdpBinPath`.

## Pinned source

- Repository: `https://github.com/ComposersDesktop/CDP8.git`
- Commit: `28bc42c72c1a7cb0fab933acd1c433be958a787b`
- License: GNU Lesser General Public License 2.1 or later
- Local CDP modifications: none

The staged release directory includes the upstream license and an exact source archive
under `licenses/`. The CDP programs remain separate executables invoked as child
processes; they are not linked into TapeSister.

## Building a native release

The cross-platform `bash build.sh` entry point configures a Release build with
`TAPESISTER_BUNDLE_CDP8` enabled. Plain `make` delegates to the same complete build.
CMake fetches the pinned commit, builds only the runtime closure used by the current
recipes, and stages the results after linking TapeSister:

```text
TapeSister/
  tapesister[.exe]
  cdp/bin/
  licenses/CDP8-LGPL-2.1.txt
  licenses/CDP8-source-28bc42c72c1a7cb0fab933acd1c433be958a787b.zip
  licenses/THIRD_PARTY_NOTICES.md
```

Build on Linux to produce Linux executables and with the Windows/MinGW toolchain to
produce Windows `.exe` files. CDP8 is native code; binaries from one operating system
must not be copied into the other package.

For an offline build, set `TAPESISTER_CDP8_SOURCE_DIR` to a complete checkout at the
pinned revision. If that directory is not a Git checkout, also set
`TAPESISTER_CDP8_SOURCE_ARCHIVE` to the exact source ZIP that must accompany the
binaries. Developers who only need the TapeSister core can configure with
`-DTAPESISTER_BUNDLE_CDP8=OFF`.

The runtime manifest is `cmake/CDP8Manifest.cmake`. When a future recipe introduces a
new executable, add it there in the same change. The staging step fails rather than
silently producing an incomplete release.

The `Native release bundles` GitHub Actions workflow performs these native Linux and
MSYS2 UCRT64 Windows builds on version tags or manual dispatch and uploads a complete
platform archive. Before packaging, its CTest pass launches every curated recipe
through the actual platform-native CDP executables against a deterministic audio
fixture; merely compiling or finding the files is not considered sufficient. It never
mixes executables from the two operating systems.

## Configurable process catalog

The compiled catalog has capacity for 128 recipes, while the interface deliberately
shows at most 32 across its two pages. Each process is selected by stable recipe ID in
`tapesister.ini`:

```ini
[CDP Processes]
CdpProcess.drunk=1
CdpProcess.shred=0
CdpProcess.glisten=1
```

The first 32 enabled recipes in canonical catalog order are displayed. If more than 32
are enabled, TapeSister shows the first 32 and reports the overflow at startup and when
the CDP panel is opened. Disable unwanted entries with `=0` and restart TapeSister.
Fewer than 32 selections leave the remaining tiles empty.

CDP transform presets are saved as `CdpPreset.<recipe-id>`, so changing the visible
selection cannot move settings to a different process. Legacy `CdpPreset01` through
`CdpPreset32` rows still load and are migrated to stable IDs on the next save.
