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
## Main-window scaling

Mosaic, Gallery, FM and TrackSister use the full window. Maximizing enlarges
their interface, lettering and mouse targets. The familiar proportional
presentation is retained; there are no fixed-density top/left islands and no
small centered editor surrounded by unused background. Canvas zoom remains
the user's separate view setting.

A fixed-density reflow was rejected after hands-on testing because it made
controls smaller and left substantial empty margins. Readability and the
familiar layout take precedence over increasing the numerical canvas area.
The scaling regression checks the presented control size, top/right and
left/bottom coverage, and inverse pointer mapping at 1280 x 800, 1920 x 1080,
2560 x 1440 and 1366 x 768.

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
When enlarged, its interface and mouse targets scale with the full window,
using the same proportional mapping for painting and input. Module profile
rows are limited to 16 visible entries with wheel/Page Up/Page Down/Home/End
navigation; report and session exports retain every row.

## Regression and hardware checks

Controller tests cover button edges/gaps, growing controls, occupied window
margins, native clipping and ruler alignment, full-window pointer mapping,
movement-filter creation/selection/paste/undo, recent-event
severity/reset/wrap, and profile pagination. Existing exact recorded-output,
routing, playback and scrolling checks remain enabled. FX cache-hit checks guard
against rebuilding the graph every idle frame.

Before release, exercise Windows 100/125/150/200% DPI and mixed-monitor moves on
hardware, plus Linux desktop scaling, including laptop-sized clients. During the
same heavy project, compare idle/playing callback load and new Health event counts
with FX/Mosaic closed and open, then while resizing and moving dividers. Listen to
the captured output for clicks; automated dummy-device tests do not establish
real ASIO/WASAPI/JACK performance.

## File browser location

File browsers share the last successfully visited folder for the current
application session. Reopening after loading or Cancel, switching between
Tiles/TrackSister, Gallery, palette import/export and SunVox patches, and
starting a new project retain that location. The first browser uses its normal
startup folder: the working directory unless an explicit initial path, such as
the configured sample folder, was supplied. No location is persisted on exit.

Configuration path pickers retain their configured starting paths and do not
replace ordinary file-browser history. Explicit navigation for recovery and
project save destinations also remains available. Saving settings does not
reset the sample browsing folder. Remembered paths are absolute; if a folder
is removed or a drive disappears, opening falls back to the working directory.
Failed navigation does not update the remembered folder or leave a stale list.

The browser regression covers first open, reopen/Cancel, multiple browser
instances and file types, selected-file retention, explicit path overrides,
configuration isolation, a changed process working directory, failed navigation
and a removed remembered directory. All folder bookkeeping stays on the UI thread.


## TrackSister Mosaic palette and portrait

New tracker defaults use the approved dark charcoal/plum surfaces, pale text,
and Mosaic's pastel gold, teal, sage, blue and mauve fields. The Config palette
Default button restores this appearance. Existing custom tracker preferences
and project palettes retain their saved colors; the host's theme overrides
continue to take precedence. assets/tracksister.pal contains the same default
for explicit import. Other windows and the preset collection are unchanged.

The header portrait is adapted from user-provided artwork in plum tones. The
embedded 128-pixel luminance/alpha asset is tinted from the current tracker
background, panel, volume/mauve and text colors. Its raster is cached until
those colors change. The main renderer draws it at output resolution inside
the original logo button, with a software fallback in the native framebuffer.
The title, FASTRACKS toggle, LEN control and input geometry retain their size.
Config screens, native dialogs and host overlays suppress the extra artwork.
There are no audio-callback operations, file reads or per-frame allocations.

The tracker appearance regression checks exact default surface/accent colors,
custom palette save/reload, theme-only raster invalidation, hidden-logo states,
and the FASTRACKS button. Check the header at 100%, 125%, 150% and 200% display
scaling and when maximized; open Config and a file browser to confirm the logo
stays behind foreground UI.
