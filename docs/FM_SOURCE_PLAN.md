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
- eight pairwise interaction modes and an interaction mix.

The renderer bounds feedback, applies a DC blocker and output saturation, and replaces
non-finite samples with silence. A seed plus complete genome always produces the same
audio. TSR22 stores the complete genome; TSR6 through TSR21 remain loadable and derive
safe defaults for fields that did not exist in those formats.

## FM LOGIC workspace

The Family/Variation row opens **FM LOGIC**. Seven pages reuse exactly six compact
controls: Pitch, Wave, LFO Rate, LFO Depth, LFO Type, Filter, and Structure. Six voice
buttons set the active mask. Five permission buttons decide which mutation domains
Randomize and later Vary operations may change. Randomize also protects the currently
visible page, so a performer can hold the part being shaped while exploring the rest.

Control clicks and mouse-wheel changes immediately rebuild the internal preview and
retarget active synth voices without altering the selected tile. **Apply** renders the
shown genome into the selected tile through the existing Create/edit history path.
**Back** discards only the workspace preview.

## Performance and capture

The FM workspace uses the same two-octave QWERTY mapping, F1-F8 octave selection, tile
tuning, five-voice limit, and Shift latch/chord behavior as sample audition. Temporary
synth voices are tagged inside `TsNoteBank`; the output callback can separate their mono
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
