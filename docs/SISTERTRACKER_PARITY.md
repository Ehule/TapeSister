# SisterTracker source boundary

F10 now embeds the TapeHead application at `f053d96df3996a5fd3b26625f069888c3a7d24ab`.
The previous drawing-only extraction and native editing/playback recreation are
superseded for the active workspace. The old controller suite still exercises
that legacy implementation as a separate fixture.

| Area | Implementation | Verification |
| --- | --- | --- |
| Drawing, field hit tests, SDL keyboard/mouse handlers | Original application sources | Real F10 host event dispatch and compact/expanded framebuffer inspection |
| Editing, clipboard, extraction and undo | Original `ft2_edit.c`, pattern editor and undo | Actual note input, current-cell Backspace, undo/redo, Alt+F4/F5 and F8 extraction |
| Song/Pattern transport, LEN/CONTROL, FastTracks, commands | Original replayer and FastTracks | Integrated playback and representative command checks; broad original modes are compiled, not individually reimplemented |
| MIDI recording | Host input feeds original `recordNote` | Note pitch, velocity, tile identity and undo through the actual host MIDI handler |
| Recording preferences | Original callbacks and recording paths, embedded Config | Silent Record writes notes with silent monitored output; IPL/INP/Shift-copy and REC+ create correctly sized patterns |
| Layout and palette | Original layout callbacks and palette editor, portable preferences | Both Config pages, preset/RGB/field colors, layout/font changes, defaults and project roundtrip |
| Ratio/LEN wheels and direction | Embedded hit tests plus original private clocks with Bounce extension | Fractional wheel events, bounds, compact/full view, direction cycling; callback bounce sequence and all five crossings at 5:1 |
| Audible sample reads | Host float stereo reader under original voices | Opposite-polarity stereo, tuning/loops, tile generation lifetime and output-rate changes |
| Source identity | Stable host tile IDs mapped to original aliases | Move, delete and slot reuse do not retarget an existing alias |
| Router and capture | Existing TRACK source and final-output writer | Audible callback output, exact live cycle boundaries and recorded-frame equality |
| Project persistence | Original score bytes in a validated version-2 extension | Standalone codec, deep copies, malformed data rejection and complete paged project roundtrip |
| Window, audio devices, MIDI devices, sampler, disk operations | Host ownership; standalone entry points disabled or redirected | Overlay ownership, workspace switching, build and packaging checks |

## Local adaptations

`src/ts_tapehead_embed.c` owns the import/export, tile snapshots and lifecycle
boundary. `src/main_sdl_tracker_embed.inc` connects it to the actual application
event loop and callback. The complete source delta is recorded in
`third_party/tapehead/application/embedded.patch`, against checked upstream
input hashes. Builds use committed sources and do not fetch TapeHead.

Local source changes replace hardware/window ownership, suppress standalone
configuration writes, redirect sampler/disk screens, restore tracker configuration,
add portable preferences and Bounce, provide float tile
voice reads and tick spans, run scopes on the host UI thread, fix unaligned help
parser reads, and adapt Backspace/full-view controls. Shared duplicate symbols
are namespaced at compile time. The standalone `main`, MIDI device backend,
Live Link device setup and scope thread are not started.

Stereo tile reads use TapeSister's interpolation and loop-crossfade policy.
This is not a promise of bit-identical XM output from TapeHead's integer sample
mixer. The original sequencer, effect execution, envelopes, periods, panning and
volume ramps remain in the playback path.

The original application supports many modes beyond the focused host tests.
Compiling those modes does not substitute for manual musical testing. Test the
PR builds with real MIDI devices, route combinations and performance gestures
before merging. Per-lane device outputs, standalone file/sample workflows and
future buffer tiles are outside this boundary.

The old full suite has three independently reproduced baseline failures:
`test_sister_source_mask`, `test_sister_recursion` and `tapesister_canvas_tests`.
They also fail on the unchanged PR head. They are unrelated to the transplant
and have not been hidden or changed to make this PR appear green.

See [PR124 audit and verification limits](PR124_AUDIT.md) for the follow-up
findings and the distinction between restored controls and exercised behavior.
