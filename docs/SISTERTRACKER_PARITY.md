# SisterTracker / Tapehead parity inventory

Reference audited: `Ehule/FT2-Tapehead-Edition`, commit `f053d96`, including
README Block Loop, CONFIGURATION extraction, PATTERN_INTERPOLATION, the embedded
`src/helpdata/FT2.HLP` manual, `ft2_edit.c`, `ft2_pattern_ed.c`, `ft2_keyboard.c`,
`ft2_replayer.c`, block-loop spec tests and block-extraction checks.

This inventory describes working behavior. Saved controls alone are not audio
implementation. TapeSister retains its own tile engine, stable IDs, Sister,
recording, FX and transport ownership.

| Operation | Status in this PR | Adaptation / remaining work |
| --- | --- | --- |
| Pixel font, recessed lanes, LEN + FastTracks header | Implemented | Original fonts/attribution retained; native palette and dimensions |
| Hybrid transport drawing / FOLLOW | Source extracted and compiled | Master-centered normal scrolling, stationary per-lane pages, source-mapped input/marks/cursor; compact/expanded native geometry |
| LEN dimming / private lane colors / phase LEDs | Source extracted and compiled | Exact Tapehead dim formula, FT text, LEN/FT/hybrid/CONTROL palette outlines, audio-derived lag/sync/lead |
| Independent rectangle, cursor, heard row | Corrected | Pattern-bound stored corners; ordinary navigation/entry preserve marks |
| Mark lane / whole pattern | Implemented | Tapehead Alt+C / native Ctrl+A; cursor does not move |
| Mouse marking / Shift navigation | Implemented | Drag starts a fresh mark; Shift extends its independent active corner |
| Cell/block/track/pattern clipboard | Implemented | Ctrl+C/X/V and Shift/Ctrl/Alt+F3/F4/F5; ordinary and block paste at cursor |
| Clipboard masks and mix paste | Implemented | NOTE/TILE/VOL/TUNE/FX; explicit zero is data; stable tile IDs |
| Undo/redo of edits | Implemented | 32 gestures, full hidden-row snapshots, no transport rewind |
| Clone whole pattern | Implemented | Copies hidden rows; NEW/CLONE creation undo remains outstanding |
| F8 block extraction | Implemented this batch | Rebased rows, original lanes, all fields, untouched source/clipboard/orders; identity-preserving creation undo/redo |
| Ctrl+L literal block audition | Implemented this batch | Standard clock, selected lanes; no FastTracks/LEN/private-clock execution |
| Shift+Arrow live loop resize | Implemented this batch | Latest bounds at next seam, independent of edit cursor; current row never replayed |
| Pause/resume, mute/solo/trim | Implemented | Retains native voices/tick time and independent non-tracker transports |
| F7 live performance capture | Implemented this batch | Seam-quantized existing final-output WAV/RF64 writer |
| F8 one-cycle capture during looping | Implemented this batch | Live final output between seams; Tapehead's offline rendering/resume is intentionally not ported |
| Insert/delete rows, lane/all lanes | Implemented | Insert/Backspace, Shift for all lanes; preserves rows beyond active length |
| Expand/shrink pattern | Implemented this batch | EDIT controls; even-row transformation, bounds, full undo, hidden-row preservation |
| Transpose all tiles in lane/pattern/block | Implemented | Shift/Ctrl/Alt+F1/F2; Ctrl+Up/Down and Shift for octave |
| Transpose only current tile | Outstanding | Tapehead modified F7/F8 variants need inherited stable-tile matching |
| Volume interpolation | Implemented, partial parity | Ctrl+Shift+V commits directly; endpoint validation; no preview/confirm yet |
| FX and M/N interpolation | Outstanding | Tapehead Ctrl+Shift+B/T; preserve separate native command meanings |
| Melodic Walk / scales / live spacing preview | Outstanding | Do not reuse Ctrl+Shift+M, which belongs to TapeSister MIDI Learn |
| Fill, reverse, repeat block | Implemented | Masks for fill/reverse; repeated block clips to active rows |
| Numeric tile/volume/tune/FX entry | Implemented | Hex/keypad, original breathing digit locator; commands stored, not audio-executed |
| Arrows, Home/End, PageUp/Down | Implemented | Page size follows visible layout; seven fields per lane |
| Tab, F9–F12, saved row bookmarks | Intentional differences / outstanding | Tab remains Sister/FX, F9 Router, F10 Tracker, F12 Audio Health; Tapehead row bookmarks need alternate bindings |
| Alt+track-jump keys / extra delete masks | Outstanding | Current masks and explicit field navigation remain available |
| Ctrl+Alt+Backspace expanded pattern | Implemented this batch | Shared geometry; 19/29 rows without names; Ctrl+E opens tools in both modes |
| Compact M/S, contextual field help | Implemented this batch | Default names and repeated field-label strips removed; custom names retained |
| Pattern clock and Main output | Implemented | Native sample readers, Sister TRACK, final-output recording |
| LEN/CONTROL and Pattern FastTracks | Upstream clock core compiled unchanged | Shared tick, 17 rational ratios, phase-preserving live changes, forward/reverse, per-lane heads; native ping-pong extension |
| LEN bypass / FastTracks uses LEN | Implemented | Saved switches, logical blank extension, physical rows never expose hidden data |
| FastTracks master/clutches/sync/randomize | Outstanding | Not part of the saved lane-clock activation batch; Z execution also pending |
| M/N tuning and FX command execution | Definitions only | Native semantics differ from Tapehead microtuning/drift; requires explicit DSP design |
| Song/order playback | Outstanding | Order definitions save; no order transport yet |
| MIDI note recording / direct lane outputs / overlap | Outstanding | Preserve current native input/routing ownership until implemented |
| Matrix, sampler, disk operations, queues | Excluded | TapeSister's native workspaces and tile engine remain authoritative |

The next coherent editing batch is current-tile transpose plus non-destructive
interpolation previews and Melodic Walk, with explicit shortcut conflict
resolution. The next audio batches are command execution and Song/order
playback. Saved Song assignments are displayed as pending and currently use
Standard playback. Native Pattern FastTracks execute row zero once on Play;
the rest of the accumulator and CONTROL handoff follow Tapehead. Ping-pong
is a native extension with no repeated endpoints.

## Source boundary

The drawing pass now compiles functions extracted from Tapehead's
`ft2_pattern_draw.c`, plus its tiny-font and transport-visual helpers. The
standalone `ft2_fasttracks_core.c/.h` files are compiled byte-for-byte unchanged.
The previous native grid/glyph/dimming/outline/LED/cursor recreation and clock
loop have been removed. See `third_party/tapehead/UPSTREAM.md`, `SOURCES.json`
and `pattern_draw.patch` for exact provenance and host adaptations; the importer
reproduces the extraction against checked source hashes.

The native window chrome, stable tile model, RGBA selection representation,
MIDI range/CUT, sample engine, recording and editing operations remain host
code. Song, clutch, Jog and Punch are still absent audio capabilities; retaining
upstream status structures does not make those transports implemented.

## Verification

The lane-clock suite checks all 17 ratios at TPL 1/2/6/31 with integer and
fractional tick lengths, 1:1 alignment across shared loops, every crossing at
5:1, reverse/ping-pong, LEN 1/256, blank extensions, natural/CONTROL boundaries,
live phase changes, pause/rate changes and literal block isolation. Controller
tests check real header/keyboard/wheel input, both layouts, overlay ownership,
immutable publication and per-lane heard positions.

Core tests exercise exact integer/fractional onsets and 100 loop seams, pause,
pending lane/row changes, partial-pattern inheritance, hidden rows, edits during
playback and source-generation lifetime. Editor tests check extraction including
explicit zero/tuning/FX data, fixed-ID redo, clipboard/focus/order preservation,
referenced-creation undo rejection and expand/shrink bounds.

SDL controller tests exercise real keys and pointers, independent marking through
entry, paste destinations, other-pattern edits during loops, compact/expanded
hit tests, hidden EQ/OUT exclusion, custom-name geometry, existing overlays,
F7/F8 dispatch, recorder ownership and exact final-output capture frames. UI
renders of compact, expanded and EDIT views are inspected before delivery.
