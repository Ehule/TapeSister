# Mosaic grid, Repeat, source arrays, cross-canvas copy and FX placement

This branch builds on `feature/pedalboard-eq-mosaic-sync` (PR #233).

## Behavior

- GRID draws snap lines below cards in both logical and native rendering. The native raster cache tracks the effective display interval, including tempo; unchanged views still reuse the raster. Dense grids thin visually without changing snap precision.
- Enabling REPEAT or L uses selected cards’ earliest start and latest end. With no selection it uses the stored range or whole arrangement. Turning Repeat off keeps the range. Explicit marker spans and marker cue looping retain priority until card selection changes. Range plus Repeat is one undo operation.
- + EFFECT and Ctrl+N start above Main Mix / Master EQ / Limiter. Pointer additions start at the requested position. Both search clear space using actual scaled card bounds across all movements, filling three columns and growing upward. Existing cards keep their positions and the new card is brought into view.
- Ctrl+C copies all selected cards. Switch Gallery canvas, set the destination playhead, then Ctrl+V. Copy Cards and Paste Cards are also in Mosaic’s right-click menu, including empty canvas space. Add Video remains available in that menu; right-clicking a video card still opens its settings. The earliest start lands at the playhead; horizontal offsets and timing are preserved. Auto-space shifts the whole group. Paste is one undo step and capacity failures leave the destination unchanged.
- The clipboard owns immutable audio, editable EQ audio and captured tracker instruments/scores. Video references, source routing, names/colors, notes, fades and mix parameters survive. Repeated paste reuses identical destination audio. Existing FX remain shared project processors. New/Open Project clears the clipboard. Copy works on whole selected cards, without trimming to time-range boundaries.
- Array Sources offers Count + Grid, Fill Range, and Fit Count. Choose the count and grid-step distance, fill a selected range at that spacing, or fit an exact count evenly into the range. The dialog displays spacing and total card count before creation. It uses the selected sidebar source(s) and original right-click position, preserving full card lengths and source edits. Grid choices are local to the dialog; Fit Count calculates spacing independently of the grid. SPACE off permits overlap; SPACE on separates cards horizontally without moving their grid starts. All cards are selected and inserted as one undo step. Capacity or placement failure leaves the arrangement unchanged.

## Regression coverage

- Warm/cold native raster comparisons at 640×400, 1280×800, 1919×1079 and 2560×1440: grid toggling, unit/division changes, tempo changes, unchanged-cache reuse and normal scene edits.
- Repeat button and keyboard behavior, automatic selected bounds, one-step undo/redo, existing unselected ranges, whole-arrangement repeat and marker precedence.
- Add-menu placement with an enlarged obstacle, snap alignment, actual non-overlap and operation undo/redo.
- Copy a mixed audio/EQ, tracker and video group, switch Gallery canvas, delete the originating canvas, paste from the empty-canvas menu, check settings/assets/routing/offsets, undo/redo, repeat paste with audio reuse and reject insufficient capacity atomically.
- Array through the real range-selection/sidebar/context-menu workflow, including a clamped menu, horizontal scroll/zoom, non-grid range boundaries, exact range-end exclusion, dense grids, tempo changes, edited source groups, overlap/spacing, exact-count compression, count/grid-distance placement without a range, modal cancellation, input validation, one-step undo/redo and atomic capacity rejection. The new controller test is included in both platform baseline manifests.

## Validation results

- Linux Release application, audio benchmark and every target in `.github/pr-regressions.txt` built successfully.
- All **111 baseline regressions passed**, selected from that manifest with CTest; final run completed in 213.63 seconds.
- The corrected empty-canvas copy/paste and video-insertion controller tests also passed independently.
- Native grid rendering was visually inspected at 1920×1200; the compositor check also passed at 1729×973. Grid lines remain behind cards and align to the configured quarter-second interval.
- `git diff --check` passed.
- The 111-test result above predates the source-array addition. The expanded array controller test passes all three modes, typed and wheel-adjusted counts, spacing, cancellation and capacity rejection. The dialog layout was visually inspected at 1280×800. The baseline now contains 112 tests; final application and hosted Windows/Linux results are recorded on this branch’s PR. Nothing has been merged.

