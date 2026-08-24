# Universal TapeSister / Tapehead palette

TapeSister and Tapehead share one text palette named `palette.pal`. The
canonical section is `[Palette]`; readers also accept the legacy
`[TapeheadPalette]` section.

When an exchange directory is configured, `palette.pal` lives in that shared
directory. Without one, each application uses `palette.pal` beside its normal
working files. `TAPESISTER_PALETTE` remains a full-path override for
TapeSister.

Every universal save writes and preserves these 26 color keys:

- Shared interface colors: `PatternText`, `BlockMark`, `TextOnBlock`, `Mouse`,
  `Desktop`, `Buttons`, `PatternNote`, `PatternInstrument`, `PatternVolume`,
  `PatternTuning`, `PatternEffect`, and `PatternEmpty`.
- TapeSister colors: `WaveSelection`, `ActiveTile`, `StereoWaveLeft`,
  `StereoWaveRight`, `StereoWaveSum`, `SisterSourceHorizontal`, and
  `SisterSourceVertical`.
- Tapehead transport colors: `TrackLengthPlayhead`, `FastTracksPlayhead`,
  `ControlPlayhead`, `FastTracksSync`, `FastTracksPhase`, `FastTracksSong`, and
  `FastTracksLengthPlayhead`.
- Contrast values: `DesktopContrast` and `ButtonsContrast`, each from 1 to 100.

Colors use `#RRGGBB`. Each application may present its own friendly names, but
the persisted keys above are stable.

TapeSister can load older local `tapesister.pal` and `tapehead.pal` files.
Missing Sister source colors inherit `PatternNote` (horizontal) and
`PatternEffect` (vertical). Other missing application-specific colors receive safe visual fallbacks and appear
as neutral, unset Tapehead eyedropper swatches. Loading never rewrites a legacy
file. Choosing **Save Shared** explicitly writes the complete universal schema
to the canonical `palette.pal` path.
