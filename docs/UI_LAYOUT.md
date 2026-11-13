# Interface sizing

TapeSister keeps its bitmap lettering, theme colors and mouse-oriented controls.
Control measurements belong to `ui_metrics.h`, separately from canvas zoom or
window dimensions. Its reference body font is the existing 14-pixel glyph.
Legacy 640-pixel surfaces use half-sized logical coordinates with the same
relative measurements.

| Element | Reference measurement |
| --- | --- |
| Toolbar / toggle | 28 high; label plus 10 on each side; minimum width 56 |
| Dialog action | 32 high; label plus 12 on each side; minimum width 72 |
| Related controls | 6 gap |
| Groups / panel insets | 12 gap |
| Parameter rows | 40, or 52 for wrapped labels |

Measure toggles using their longest state. Equalize related dialog actions to
the longest label (including confirmation states); do not distribute them across
the entire available width. Stretch text fields, sliders and editing surfaces.
Use the same `TsUiRect` for painting and hit testing, with an exclusive right and
bottom edge. Gaps must not activate adjacent controls.

The floating F2 editor now uses 96 x 32 action buttons, with its original font.
Gallery actions use the same metrics, including room for CONFIRM without changing
width. Both retain their existing keyboard commands and editing transactions.

## Validation

Exercise pointer edges and gaps, longer state labels, rename/import confirmations,
keyboard Apply/Cancel, and saved names/colors. Continue testing fractional DPI,
small laptop windows and mixed-monitor resizing when a surface adopts these
metrics. Layout-only operations must not stage DSP processors or take audio locks.
