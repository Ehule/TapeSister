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

## FX workspace

The outer FX view keeps the familiar 1280 x 840 logical surface. Enlarging the
window enlarges controls, fonts, inspector and graph together. Graph zoom remains
a separate editing operation. Native cable rasterization remains at drawable
resolution; display density affects stroke quality without imposing a UI size cap.

The existing saved divider, double-click reset and Escape cancel remain.
Two consistent toolbar rows, collapsed help and removal of the duplicate bottom
parameter editor recover space within that layout. The inspector owns parameter
editing and replacement; EDITOR opens advanced Matrix controls.

With the default inspector and help closed, the logical graph grows from
980 x 566 to 980 x 696, about 23% more area. That relative gain persists when
the window is enlarged; it does not depend on shrinking controls. Inner
Meta/SunVox editors retain their existing scaling.

## Movement filter visibility

New and pasted effects are retained as temporary view exceptions until the
movement filter changes. Selected/inspected effects are also visible. Nodes
outside the movement say `OUTSIDE MOVEMENT / EDITING`; the creation status
explains the exception. Creation pans the viewport to the new node, and Z frames
the selection. Filtered FIT excludes unrelated global text-label extents.
These exceptions do not assign ownership, create routes or activate playback.
They are transient and keyed by instance identity, so deleted slot reuse is safe.
