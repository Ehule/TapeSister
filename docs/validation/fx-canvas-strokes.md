# FX Canvas display-pixel strokes

The renderer keeps endpoints and Bezier subdivisions fractional until drawing
into the existing drawable-sized framebuffer. Straight cables, spline cables,
direction arrows, Meta FX chain lines and selection outlines use a solid stroke
with a one-pixel coverage transition at the edge. No whole-window filtering or
font change is involved. The implementation is original; no SunDog code is
copied into TapeSister.

## Geometry and interaction

- Adaptive subdivision targets a quarter drawable pixel of flatness, capped at
  ten subdivision levels (1024 segments). Finite-chord distance handles returning
  and collinear/backtracking curves. Offscreen segments are clipped before
  rasterization; work follows the visible stroke rather than its bounding area.
- Stroke width is two pixels at 100% display scale. It follows the greater of
  drawable/window density and reported display DPI, bounded to 1–4x. Missing DPI
  information falls back to the drawable/window ratio. Window enlargement and
  canvas zoom do not independently thicken the stroke.
- The same flattened geometry is used for click distance and wire box selection.
  The existing eight-logical-pixel click tolerance is retained.
- Per-path coverage is unioned before blending, so adjoining segments and
  arrowheads do not form bright knots. Arrow direction follows the curve tangent.

## Cost and caching

The outer graph caches its background and cables using exact ordered draw
commands. Endpoints, order, colors, route count, spline/straight mode, framebuffer
dimensions, clip rectangle, display scale and background/grid colors invalidate
the cache. No hash collision can conceal a change. Nodes, meters, drag-preview
cables and selection outlines remain live.

Scratch coverage uses one byte per drawable pixel plus two row-span arrays. The
cable cache stores four bytes per graph-area pixel and a small command list.
Storage is reused between frames and released when the Matrix window is destroyed.
There are no audio-thread changes.

An intentionally dense 250-cable benchmark is available with `TS_FX_PREVIEW`.
On the development container, a cached cable pass was approximately 0.1 ms at
1366×699 and 1–3 ms at 3840×2160. A complete redraw is more expensive, especially
while moving a dense 4K graph. These are UI-only measurements, excluding the rest
of the frame, and are not measurements on an X220 or a physical Windows display.

## Validation

`tapesister_fx_canvas_controller_tests` includes:

- pixel coverage checks for shallow, steep, horizontal, vertical, reversed,
  zero-length and far-offscreen lines;
- 1280×840, 1366×699, 1600×1050, 1920×1080, 1920×1260, 2560×1680 and
  3840×2160 surfaces, with 100%, 125%, 150% and 200% simulated display scales;
- analytic curve samples against the subdivision error bound, including returning
  cables, followed by click and marquee checks on the same curve;
- overlapping-segment coverage and exact cached-versus-fresh pixel comparison
  after moving, recoloring, changing style, removing all cables and resizing;
- the existing modal transforms, mixed selections, cable gains/deletion,
  branch copy/paste, undo/redo, project recall and SunVox editor checks.

Native SDL framebuffer captures were visually inspected at laptop and larger
sizes. A focused build of the stroke/cache checks also passed AddressSanitizer
and UndefinedBehaviorSanitizer; LeakSanitizer is unavailable under the container's
process tracing. Physical mixed-DPI monitor transitions remain a manual check.

To reproduce the native captures and optional benchmark:

```sh
mkdir -p /tmp/fx-preview
TS_FX_PREVIEW=/tmp/fx-preview build-pr/tapesister_fx_canvas_controller_tests \
  /tmp/fx-preview/canvas.bmp /tmp/fx-preview/inside.bmp
```

The full PR baseline and native-package Linux/Windows workflows include the FX
controller regression. See the PR checks for their results on the current commit.
