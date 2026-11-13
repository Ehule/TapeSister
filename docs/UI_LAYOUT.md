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

The outer FX view uses display-density units, independently of window size and
graph zoom. The existing saved divider value is interpreted as an inspector
width relative to the original 1280-wide surface; old projects retain their
preferred width. Drag, double-click reset, Escape cancel and persistence remain.
Inspector pages adapt to available height, using 52-unit wrapped parameter rows.

Two 28-high toolbar rows replace uneven fixed-width controls. Help is collapsed
by default. The inspector owns parameter editing; EDITOR opens the existing
Matrix editor for advanced actions such as transient states, replacement and
removal. The duplicate bottom parameter editor no longer covers the outer graph.
Inner Meta/SunVox editors retain their existing layout in this change.

At 100% display density, with the default inspector and help closed:

| Client size | Previous graph | New graph | Graph area recovered |
| --- | --- | --- | --- |
| 1280 x 840 | 980 x 566 | 980 x 696 | 23% |
| 1366 x 699 | approx. 1046 x 471 | 1066 x 555 | 20% |
| 1920 x 1080 | approx. 1470 x 728 | 1620 x 936 | 42% |

These are viewport measurements, not a claim that every node is always framed.
Resize preserves graph zoom; Home/FIT frames the map and Z frames the selection.
At 150% Windows DPI a 1920-pixel-wide client has 1280 layout units, with controls
and text enlarged together for readability. Extremely small clients retain a
960 x 480 minimum logical surface and are downscaled to fit.

## Movement filter visibility

New and pasted effects are retained as temporary view exceptions until the
movement filter changes. Selected/inspected effects are also visible. Nodes
outside the movement say `OUTSIDE MOVEMENT / EDITING`; the creation status
explains the exception. Creation pans the viewport to the new node, and Z frames
the selection. Filtered FIT excludes unrelated global text-label extents.
These exceptions do not assign ownership, create routes or activate playback.
They are transient and keyed by instance identity, so deleted slot reuse is safe.

## Mosaic and Gallery presentation

Mosaic now reflows its existing framebuffer into fixed-density toolbar, source,
volume-control and footer regions, plus an expanding canvas and ruler. Native
waveforms and pointer conversion use the same rectangles. Resize compensates
view zoom to preserve world pixels per second and card width; event durations,
audio and routing are unchanged. The existing volume-lane toggle remains.

At 1920 x 1080 and 100% DPI, the default canvas grows from approximately
1242 x 788 to 1468 x 864 (30% more area). Controls retain their 1280 x 800
reference size. Gallery and other foreground dialogs use a centered,
density-sized surface instead of stretching to fill the window. Native card
text is capped at the control density; the ruler shares graph rounding.

Very small clients fit the original 640 x 400 surface. Source-palette rows
remain fixed-height, leaving room below them on taller displays; their small
waveform previews use the existing logical renderer. The main canvas and ruler
retain native rendering and cached repaint behavior.


## Audio Health

Ordinary measurements and retained maxima use neutral text. Red identifies new
over-budget processing or skipped callbacks; amber identifies new near-budget
processing, delayed starts, long control holds, or backend reports. Start gaps
alone are explicitly not described as proven dropouts. Theme severity roles
remain configurable; the default red tracker text no longer colors normal data.

The NEW rows count increments in the last completed observation window (normally
one second, with its actual duration shown). TOTALS are since counter reset or
device restart; Reset preserves the session timeline. Reset/device changes,
counter regression and tick wrap rebase the observation window. Reports retain
historical counts and include the recent window separately. Reading and formatting
reuse existing snapshots on the UI thread; no additional audio callback work.

Health actions use shared 32-high reference metrics with label-based widths and
real gaps. The small, draggable, always-on-top window retains its normal size.
When enlarged, its content stops growing at the display-density reference size
and is centered; painting and pointer input share that transform. Module profile
rows are limited to 16 visible entries with wheel/Page Up/Page Down/Home/End
navigation; report and session exports retain every row.

## Regression and hardware checks

Controller tests cover button edges/gaps, responsive canvas geometry, native
clipping and ruler alignment, window resize zoom invariance, centered Gallery
and looper overlays, movement-filter creation/selection/paste/undo, recent-event
severity/reset/wrap, and profile pagination. Existing exact recorded-output,
routing, playback and scrolling checks remain enabled. FX cache-hit checks guard
against rebuilding the graph every idle frame.

Before release, exercise Windows 100/125/150/200% DPI and mixed-monitor moves on
hardware, plus Linux desktop scaling, including laptop-sized clients. During the
same heavy project, compare idle/playing callback load and new Health event counts
with FX/Mosaic closed and open, then while resizing and moving dividers. Listen to
the captured output for clicks; automated dummy-device tests do not establish
real ASIO/WASAPI/JACK performance.
