# Keyboard arpeggio and sequencing

Click **ARP** above the main or FM keyboard. With **EDIT ON**, click keys or press
the QWERTY note keys to add/remove pitches, then press **PLAY**. **Ctrl-click**
also changes the selection with the panel closed or EDIT off. Up to 24 pitches
can be selected across different octaves; changing the visible octave does not
transpose or lose them. Empty selections stay silent.

**Shift+Space** starts/stops ARP, including with the panel closed or the tile
bank visible. It also works from FM, Mosaic, the EQ page, and Sister Machine.
Text fields, dialogs, Portal, and file preview retain their existing key handling.

Hover a button or slider for **600 ms** to see help and shortcuts in the bottom
status line. Moving away restores the operation message; fresh messages get
2.4 seconds before help returns over the same control. Dragging or typing into
a dialog suppresses help.

![Keyboard arpeggiator with an ordered selection](images/keyboard-sequence.png)

Selected keys are teal, and the current gated step is gold. Numbers show the
order in which keys were selected. A small pink/red mark identifies a selected
key that also has a manually played voice. Turn **EDIT OFF** to play normally
alongside the sequence, including HOLD, Shift-click chords, and MIDI.

| Control | Action |
| --- | --- |
| PLAY / STOP | Start from the first step / release the sequencer's voices only |
| Shift+Space | The same ARP-only transport, even with its controls hidden |
| UP | Ascending pitch |
| DOWN | Descending pitch |
| UP/DOWN | Ascend then descend without repeating the end notes |
| ORDER | Follow the numbered key selection, like Prism's lens sequence |
| RANDOM | Pick a selected pitch each step; repeats are possible |
| RESET | Restart at the beginning while playing; stay silent if stopped |
| CLEAR | Empty the selection and stop the sequence |
| FROM HELD | Copy the active QWERTY chord, sorted by pitch; the original notes keep playing |
| ARP VOL | Independent sequence level: 0–200%, initially 100%; 0% mutes audio while the clock keeps running |
| LFO ON/OFF | Enable a sine volume LFO affecting only ARP; initially off |
| CYCLE | Duration of one LFO cycle, 50 ms to one hour; initially 4 seconds |
| DEPTH | How far the LFO lowers ARP below the volume fader; initially 50% |
| EDIT ON/OFF | Switch ordinary key clicks/QWERTY presses between sequence editing and live playing |
| LOOP / ONCE | Repeat, or stop after one traversal (one full up/down traversal in that mode) |
| X | Close the controls; playback continues |

Click the mode button to move forward, or right-click it to move backward.
Removing then re-adding a key moves it to the end of ORDER. This is an ordered
selection of unique pitches, rather than a grid of repeated notes or rests.
RANDOM with ONCE makes as many choices as there are selected pitches.

**STEP** is the interval between note starts: 30 ms to one hour, initially
250 ms. **GATE** is how much of that interval sounds: 5–100%, initially 80%.
For example, Step 1 second and Gate 50% gives half a second of sound followed by
half a second of silence. The note restarts from its source range on each step.
Saved loop modes, main LOOP, and keyboard HOLD determine whether that source
repeats during a long gate; otherwise a short sample can finish before the gate.

Drag either slider or use the wheel; Shift-wheel makes finer changes. Right-click
restores its default. Both controls update during playback. Step retains elapsed
time; shortening it past the elapsed duration advances once, without a burst of
missed notes. Editing the selected keys preserves the current pitch and phase if
it remains selected. Removing that pitch or changing mode starts a fresh step.

**ARP VOL** balances the pattern against manually held drones, MIDI notes and
other sources. It changes the sequence's audio level before it joins those
sources, without changing note velocity or retriggering the pattern. Shared
downstream effects and dynamics still respond to the combined signal. Drag or
wheel the fader; Shift-wheel changes it in 1% increments, and right-click restores
100%. At 200% the ARP is twice its original amplitude (about +6 dB).

The optional **LFO** creates volume swells or tremolo. The fader sets the peak:
at 50% depth the LFO moves between half that level and the full level; at 100%
depth it reaches silence. **CYCLE** is independent of note STEP time and continues
across note changes and while the panel is hidden. PLAY and RESET start the cycle
at its peak. STOP freezes it. Volume, depth, enable and phase-reset changes have
a short gain ramp (at most 5 ms for a full-range change). The gold marker under
the volume fader displays the current modulated gain, while the fader retains its
base position. Wheel/Shift-wheel and right-click defaults work on CYCLE and DEPTH.

For example: hold a low C drone, select C/G/E-flat/B-flat for ARP, switch EDIT off,
and set ARP VOL to 40%. Enable LFO with an 8-second cycle and 75% depth to let the
pattern rise from 10% to 40% of its original level over the steady drone.

![The same ARP volume and LFO controls in FM](images/keyboard-sequence-fm.png)

The sequencer uses the current tile/Source choice, FM preview, or selected
tile/Sister ensemble. Sound and routing changes are prepared outside the audio
callback and swapped into playback. Existing manual notes retain their own
ownership. If the source becomes unavailable, sequencing stops instead of
continuing to trigger stale audio; press PLAY once a source is ready again.

Selecting another occupied tile keeps the selected pitches, order, current step,
and timing running while the sound follows the new tile. An active tile/Sister
ensemble keeps its group source. While ARP runs, a plain tile click selects its
source without adding a separate click-launched layer, even with PLAY ON SEL or
main LOOP enabled. Existing click-launched layers keep playing. Normal tile
launching resumes after ARP stops.

ARP STOP, Shift+Space, CLEAR, and the end of ONCE release only sequence voices. They do not
release a manually held chord, same-pitch MIDI notes, or click-launched tiles.
Space/Stop retains its global stop behavior. A short 5 ms fade removes abrupt
sequence boundaries. An explicitly locked main loop retains its established
resume behavior after the sequence stops. Hiding the panel or moving to Sister
does not itself stop the sequence. Actions that normally stop audition, such as
closing FM, switching Sample pages, or clearing the active source, also stop ARP;
the selection remains ready for PLAY. Capture staging keeps its Shift-click controls.

Sequence audio follows the existing tile/FM buses through Sister, Prism, master
effects, and recording. Mosaic REC DRY includes it; FM's SYNTH tap includes FM
sequences. Normal source switches and monitoring still determine what is heard.
The sequence selection and settings are session controls in this first version;
they are not stored in project files or Prism presets. Portal and file-preview
keyboards keep their existing audition behavior.

## Verification

`tapesister_keyboard_sequence_tests` covers ordering, bounce endpoints, Random,
Once, stereo preservation, gates, live duration edits through one hour, removal
of the active/last key, 24-note limits, source loss, and one-shot retriggering.
It also checks volume ratios, exact mute, gain-ramp bounds, LFO extrema and depth,
rate changes, long cycles, live edits and malformed control values. The native
controller tests compare held-note samples with ARP muted, exercise the controls
in both main/FM panel positions, and check hidden-panel modulation continuity.
The native keyboard HOLD suite also renders the audio callback across tile, FM,
group, and Sister routes, checks the dry/FM recording buses, preserves manual
and MIDI voices on ARP Stop, changes octave and source during playback, checks
tile selection with ordinary/locked LOOP and PLAY ON SEL on/off, and verifies
Shift+Space ownership, key-repeat suppression, and playback with hidden controls.
Physical Windows listening remains part
of the release checklist.
