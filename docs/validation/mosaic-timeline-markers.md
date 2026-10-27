# Mosaic timeline markers

The bottom strip now holds sixteen numbered transport cues. Alt+M adds the playhead
to the next empty slot; clicking a filled slot seeks through Mosaic's existing
seek path without changing its playing/paused state. Shift-click captures a
particular slot. Ruler flags can be dragged, and right-click opens name, exact
time, and delete controls. The original five canvas views remain under VIEWS.

Marker metadata is part of arrangement Undo/Redo, the project hash, and Mosaic
format 14. Earlier projects retain their saved views and start with empty marker
slots. Absolute seconds are independent of card edits and global speed. MIDI
bindings cover all sixteen cues and the LOOP toggle through existing MIDI Learn.
Solo-state recall is deferred. M/Shift+M retain mute/source-wide mute; Alt+M places
a marker. Solo shortcuts are unchanged.

## Verification

- Core persistence checks cover fractional seconds, names, LF/CRLF files,
  legacy formats, no-op edits, Undo/Redo, and rejection of invalid metadata
  without replacing the current arrangement.
- Controller checks exercise Alt+M and key repeat, empty/full slots, Shift capture,
  paused/playing jumps, drag commit/cancel/focus loss, editor ownership,
  validation, rename/delete, epoch replacement, and the revised mute shortcuts.
- Twelve alternating playback jumps match direct `ts_mosaic_seek` reference
  output within 1e-6 over 256 stereo samples per jump.
- Native SDL compositor captures cover 1280×800 and 1729×973 windows. At 2×,
  the entire marker/mix strip is checked against the logical framebuffer.
- Tests use generated audio and SDL dummy audio/video on Linux. These checks do
  not certify live controller latency or X220/X230 hardware performance.

Cue looping uses the next strictly later marker, independently of slot order.
Duplicate times cannot create zero-length loops. The last marker uses the current
arrangement end. Loop boundaries are cached with the playback schedule, not
searched for every audio sample. Enabling midway selects the preceding cue.
Disabling leaves the clock, voice phase and transition unchanged and prevents an
older manual repeat range from taking over. The saved manual range remains intact;
loop mode and the active cue are temporary transport state.

The loop regression compares multiple cue wraps against ordinary range playback,
then releases halfway through a cue and compares continuous stereo output across
the next boundary. Other cases cover duplicate/out-of-order markers, the last and
empty slots, paused recalls, endpoint edits, Undo, deletion, and manual seek/Repeat.
MIDI controller tests learn all sixteen note targets and the CC loop toggle, reload
the saved config, exercise real decoded messages and note releases/velocity zero,
check modal hit testing, and trigger/release loops from another workspace.

## Native captures

These are application framebuffer/compositor captures, not design mockups.

![Timeline markers, 1280×800](../screenshots/mosaic-performance/timeline-markers.png)

![Timeline markers, 1729×973](../screenshots/mosaic-performance/timeline-markers-fractional.png)

![Marker editor](../screenshots/mosaic-performance/timeline-marker-editor.png)

## Try it

1. Play a Mosaic arrangement and press Alt+M at several useful moments.
2. Click the numbered slots repeatedly; pause, then recall a slot again.
3. Shift-click an empty number, drag its ruler flag, and undo/redo the move.
4. Right-click a slot to rename it or enter an exact time; save and reopen.
5. Open VIEWS and recall a previously saved canvas view.
6. Enable LOOP, trigger a cue, then switch LOOP off halfway through the passage.
7. Learn a MIDI key for a cue and a CC button for LOOP; repeat the same release.

![MIDI Learn for markers](../screenshots/mosaic-performance/timeline-marker-midi.png)
