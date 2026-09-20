# Master EQ validation

Baseline: merged `main` at `aedcebc90acd963a6430cc3cecf80399eafb7ffb`
(PR #113, keyboard arpeggiator/sequencer). Validation used GCC 13.3 and Linux SDL2,
with dummy audio/video drivers for native controller fixtures.

- Full native application and test targets build successfully. Existing format
  truncation warnings remain in the large application/controller translation unit.
- New DSP tests pass: all six filter types at 8/44.1/48/96 kHz, analytic vs rendered
  response, five-band sum, exact flat/bypass, stereo matching, rapid extreme edits,
  continuous filter/bypass transitions, silence decay, non-finite sanitization,
  rate changes, stacked +12 dB boosts into the limiter, OUT mute, project/INI
  round trips, legacy flat defaults, malformed data, and sound-preset isolation.
- Native controller tests pass: EQ entry, confirmed reset, project dirty state,
  scaled graph drag, modal ownership, existing MIDI pickup/dispatch, EQ edits during
  all four QWERTY/HOLD/ARP source routes, global stop, and FILE OUT/hardware equality.
- AddressSanitizer + UndefinedBehaviorSanitizer pass the new DSP tests. LeakSanitizer
  cannot run under this host's tracing environment, so `detect_leaks=0` was used.
- Full CTest run: 80 pass, 3 fail. The three failures reproduce at the identical
  assertions in a separate clean build of untouched baseline `main`:

| Existing failure | Baseline and branch assertion |
| --- | --- |
| `test_sister_source_mask` | `test_sister_source_mask.c:35`, expected four performance voices |
| `test_sister_recursion` | `test_sister_recursion.c:69`, expected four performance voices |
| `tapesister_canvas_tests` | `test_canvas.c:180`, expected boundary resolution 3 |

The passing suites include Master FX/post effects, limiter, Sister runtime/lifecycle,
Fallout, Prism and Morph/Matrix, Mosaic core/controller editing and recording, FM and
unison, keyboard HOLD/ARP/sequence, MIDI, transport/controller behavior, capture,
recording/export, project/preset persistence, audio configuration and lifecycle.
The Prism migration fixture now locates the version field instead of hardcoding 21;
its version-12 migration assertions are preserved.

`images/master-performance-eq.png` is the native rendered page from the SDL
controller fixture, not a mockup. It shows a 40 Hz high pass, a −2.5 dB low shelf,
−4 dB and −2 dB bells, and a +1.5 dB high shelf, with the limiter on.

## Real-world checks before merge

- Listen on the target PA/headphones while sweeping all types, especially low
  frequencies and high Q; compare bypass at sensible input levels.
- Exercise a physical MIDI controller's pickup, band frequency/gain/Q, and bypass.
- Test Windows/WASAPI device changes and small buffers with Mosaic, Sister,
  Prism/Matrix, Fallout, Master FX, FM/HOLD, and ARP playing together.
- Record a sustained FILE OUT/Mosaic OUTPUT performance and check the WAV against
  what was heard. Confirm source captures/exports retain their intended dry taps.
- Save/reopen a project, restart the session, and recall sound presets; inspect the
  page at the intended window scale and palette.

## Band-button follow-up

The numbered row now toggles bypass, with Ctrl-click exclusive solo, independent
of graph-node selection. The duplicate selected-band bypass button is removed.
New assertions cover preserved per-band settings and bypass masks, soloing a
bypassed band, moving/clearing solo, inert inter-button gaps, MIDI-learn selection,
reset, effective response at the output sample rate, global bypass, device
reconfiguration, solo persistence/defaults, and malformed solo values. Rapid-edit
stress now includes solo changes.

The native app builds; the five targeted suites (EQ DSP, native keyboard/EQ
controller, Sister runtime, project state, limiter) pass. ASan/UBSan validation
uses the existing host limitation described above. The full-suite count above
records the initial EQ validation; this follow-up checks the affected paths.
Both normal and solo screenshots were refreshed through the native fixture.

## Insert and layout follow-up

The EQ buttons now fit their labels, with separate hit areas for OUT and ROUTER.
The recording header uses one REC indicator instead of overlapping REC and IN
labels. The native controller suite exercises the new coordinates and renders
normal EQ, band solo, and active file-recording views at 640 × 400.

![EQ during file recording](images/master-eq-recording.png)

The Insert controller fixture also compares FILE OUT and Mosaic REC OUT samples
against Master channels 1/2 while simulated stereo SEND/RETURN runs on channels
3/4, Router order/bypass/solo change, and held notes, ARP and Mosaic continue.
These are headless I/O tests; physical channel mapping and round-trip listening
remain in [Windows audio validation](WINDOWS_AUDIO_VALIDATION.md).
