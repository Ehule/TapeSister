# Universal TapeSister / Tapehead palette

TapeSister and Tapehead share one text palette named `palette.pal`. The
canonical section is `[Palette]`; readers also accept the legacy
`[TapeheadPalette]` section.

When an exchange directory is configured, `palette.pal` lives in that shared
directory. Without one, each application uses `palette.pal` beside its normal
working files. `TAPESISTER_PALETTE` remains a full-path override for
TapeSister.

TapeSister universal saves write and preserve these 32 color keys:

- Shared interface colors: `PatternText`, `BlockMark`, `TextOnBlock`, `Mouse`,
  `Desktop`, `Buttons`, `PatternNote`, `PatternInstrument`, `PatternVolume`,
  `PatternTuning`, `PatternEffect`, and `PatternEmpty`.
- TapeSister colors: `WaveSelection`, `ActiveTile`, `StereoWaveLeft`,
  `StereoWaveRight`, `StereoWaveSum`, `SisterSourceHorizontal`, and
  `SisterSourceVertical`.
- Tapehead transport colors: `TrackLengthPlayhead`, `FastTracksPlayhead`,
  `ControlPlayhead`, `FastTracksSync`, `FastTracksPhase`, `FastTracksSong`, and
  `FastTracksLengthPlayhead`.
- Mosaic colors: `MosaicHighlight`, `MosaicTile1`, `MosaicTile2`, `MosaicTile3`,
  `MosaicTile4`, and `MosaicTile5`.
- Contrast values: `DesktopContrast` and `ButtonsContrast`, each from 1 to 100.

Colors use `#RRGGBB`. Each application may present its own friendly names, but
the persisted keys above are stable.

TapeSister can load older local `tapesister.pal` and `tapehead.pal` files.
Missing Sister source colors inherit `PatternNote` (horizontal) and
`PatternEffect` (vertical). Other missing application-specific colors receive safe visual fallbacks and appear
as neutral, unset Tapehead eyedropper swatches. Loading never rewrites a legacy
file. Choosing **Save Shared** explicitly writes the complete universal schema
to the canonical `palette.pal` path.

The Mosaic strip in the palette editor selects its highlight or one of five
source colors without adding more sliders. RGB, PgUp/PgDn, the Tapehead
eyedropper, Reset, Cancel and Save Shared all use the selected entry. Older
palettes omit these six keys and load the original five source colors plus a
bright red selection highlight. Tapehead versions that predate these keys may
omit them when saving the shared file; loading a palette never rewrites it.
