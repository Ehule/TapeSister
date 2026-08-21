# Generative six-voice FM sound logic

TapeSister's default Create source is a deterministic six-voice FM genome. Audio is
rendered offline into an ordinary tile or temporary performance buffer; the editor and
realtime callback never depend on a separate synth runtime.

## Sound model

Each stored patch contains:

- ten routing structures and eight ratio families;
- ten oscillator waveforms per voice, including band-limited saw, square, and pulse;
- per-voice LFO type, rate, and depth with pitch, amplitude, index, filter, random,
  and stepped destinations;
- voice enable mask, global depth, shape, bounded feedback, and transient mix;
- low/high/band-pass filter mode, cutoff, resonance, envelope attack/release/amount;
- eight pairwise interaction modes and an interaction mix;
- stored Drone and Extreme performance/render modes.

The renderer bounds feedback, applies a DC blocker and output saturation, and replaces
non-finite samples with silence. A seed plus complete genome always produces the same
audio. TSR24 stores the complete genome; TSR6 through TSR23 remain loadable and derive
safe defaults for fields that did not exist in those formats.

## FM LOGIC workspace

The Family/Variation row opens **FM LOGIC**. Seven pages reuse exactly six compact
controls: Pitch, Wave, LFO Rate, LFO Depth, LFO Type, Filter, and Structure. Six voice
buttons set the active mask. Five permission buttons decide which mutation domains
Randomize and later Vary operations may change. Randomize also protects the currently
visible page, so a performer can hold the part being shaped while exploring the rest.
The workspace ends above the Sample Bank header on the 640×400 logical canvas, keeping
both rows of all 16 destination tiles visible at 1366×768 while Apply and Chain run.

Drone bypasses amplitude decay, filter attack/release, modulator decay, and the transient
layer, then trims the rendered block to exact zero-valued start and end boundaries.
Extreme widens ratios, depth, feedback, resonance, filter motion, and per-voice LFO
ranges; the renderer's DC blocker, finite checks, saturation, and hard output bound stay
in force. Both switches are stored in the genome. The workspace also repeats Variation
Range so it stays reachable while the modal is open, and wheel input advances every
categorical control by exactly one item per detent.

Control clicks and mouse-wheel changes immediately rebuild the internal preview and
retarget active synth voices without altering the selected tile. Apply obeys the visible
Chain switch: off overwrites the selected tile; on selects the next empty tile. A full
page opens explicit Overwrite, New Sample Page, and Cancel choices. **Back** discards
only the workspace preview. New FM audio is rendered from middle C (261.63 Hz) and uses
MIDI 60 as its unity key.

## Performance and capture

The FM workspace uses the same two-octave QWERTY mapping, F1-F8 octave selection, tile
tuning, and five-voice limit as sample audition. Ordinary keys are polyphonic even when
the tile loop is off; key-up releases one voice, while Hold latches or releases the
currently sounding synth chord. Temporary synth voices are tagged inside `TsNoteBank`;
the output callback can separate their mono
mix from ordinary tile audition while preserving the final mixed output.

That split supports two independent recording routes:

- **Capture-to-New-Tile** records the final output mix. If armed before an FM note,
  the synth performance becomes its explicit source and the committed tile contains
  audio only, not a live generator.
- **REC BANK / SRC SYNTH** feeds only the internal synth mix to the existing threshold,
  pre-roll, silence, tail, early-stop, and Chain recorder. It requires no physical input
  device or operating-system loopback and archives completed takes as `SYNTH_...wav`.

`SRC EXT` retains the existing hardware-input path and optional dry monitor. MONITOR is
not offered for `SRC SYNTH` because the synth is already present in TapeSister's output.

## Variation behavior

Range scales continuous mutation and unlocks increasingly distant categorical choices.
Low values make close timbral relatives; high values may change ratio family, topology,
interaction, waveform, LFO, filter, and voice participation when their permission bit is
enabled. A zero range is an exact genome copy. Deterministic seed handling keeps repeated
builds and project round trips reproducible.

FM LOGIC, CDP/DSP Transform, and Drone Maker miniature waveforms draw the current
audition position from the exact preview sample pointer, so each small playhead follows
the sound that is actually running.
