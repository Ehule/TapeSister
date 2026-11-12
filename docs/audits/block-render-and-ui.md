# Shared matrix spans and native canvas reuse

This extends the shared workspace change in #210. The production callback now
renders independent matrix processors in bounded spans, and the native Mosaic
canvas retains unchanged card artwork. It changes dispatch and data movement,
not effect algorithms, sample quality, processing quantum or musical settings.

## Audio execution

- Sources are generated chronologically into fixed, audio-owned scratch. The
  graph visits each source, connection and processor once per span. Stable matrix
  coefficients route all frames per connection; native effect dispatch and
  SunVox buffer staging also operate on spans.
- A span cannot exceed the existing 64-frame feedback delay or the next SunVox
  render boundary. Feedback history is read before it is overwritten. Ready
  SunVox jobs drain in the original graph order, with the existing optional
  worker admission rules. Even patches using SDK-global random state retain
  their serial render order. Workers remain off by default.
- Source summation, multiply/add grouping, source/output ramps, sidechains,
  limiter peaks, recorder taps and hardware output are retained per sample.
  The output phase runs chronologically within the same device callback. There
  is no new queue, additional audio latency, callback allocation or suspension
  based on input silence.
- Coupled paths retain chronological rendering: the Sister tape/serial router,
  Sister-owned performance voices, live loop capture, source-stopping capture,
  tracker-synchronized recording, pedalboard master fades, route slews and
  effect replacement fades. Completed transitions can return to spans. The
  general standalone frame/block APIs remain available.

The callback regression compares the span path with a forced chronological
oracle, including simultaneous notes, audition and Mosaic; 255 duplex input
channels; 26 output channels; callback sizes 1/17/63/64/127/128/257/512/1024;
source/output ramps; route/master edits; and capture completion. The graph
regression compares every hardware output, sidechain, feedback history, meter
and route voice, using every matrix effect type, mixed and replaced SunVox
quanta, bypass, deletion and silent tails.

## UI execution

The native Mosaic card surface now caches its unchanged raster. Its key covers
scene identity, event data, immutable sources, tracker version/pending flags,
selection, hover, viewport and palette. Moving playheads and overlays use the
existing logical-to-native compositor and dirty texture rectangles. Resizing,
editing, dragging, live-loop graphics and recording invalidate or bypass the
cache. No waveform detail, antialiasing, font resolution or refresh cadence is
reduced. Opaque panels explicitly mark their coverage so matching background
colors cannot reveal the cached canvas underneath.

The surface owns and frees its cache. Pixel regressions compare warm and cold
rendering at 640, 1280, 1919 and 2560 pixels wide, checking reported dirty regions
as well as playhead movement, scrolling, zoom, selection, mute/solo, pan/fade
edits, dragging, overlays, palette changes and skipped presentation.

## Reproduction

`tools/benchmark_audio_callback.c` loads an unchanged native project and calls
the production callback. `TS_BENCH_SCALAR=1` selects the chronological oracle;
`TS_BENCH_NORMALIZE_TRANSPORT=1 TS_BENCH_GALLERY=0` selects uninterrupted Keter.
Run 608 seconds with no warmup to include its dense ending and the saved end.
`TS_BENCH_INTERVAL=30` reports intervals without resetting effect history.

`TS_BENCH_UI=1920` runs the production painter, native compositor, damage planner,
texture uploader and SDL software copy/present on the loaded project. Optional
`TS_BENCH_UI_DETAIL=1` selects a close view; `TS_BENCH_UI_PAN=1` moves it every
frame; `TS_BENCH_UI_COLD=1` invalidates the card raster each frame. Audio and pixel
hashing are outside measured sections. The UI run uses the dummy/software
backend, so its timings exclude real desktop GPU/driver/VSync behavior.

The deterministic SunVox seed override belongs only to the benchmark. Production
randomness and compiler settings are unchanged. The test project is the original
Tree of Life Live FX 04 archive, SHA-256
`e6bd4d10604365820d517572a431686605183576a0af45d79151b9f2d33a66a4`.

## Validation results

Final measured results and native CI evidence are recorded in the pull request.
Headless CPU savings do not establish that ASIO/JACK scheduling or audible clicks
are resolved; the complete top branch still needs the user's interface test.
