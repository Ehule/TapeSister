# SunVox effects in TapeSister

TapeSister includes the SunVox DSP engine, its 20 standard effects, and the complete pinned bank of 235
loadable effects. Installing the SunVox application is unnecessary.

## Using a patch

1. Open FX Canvas with Shift+F9, add **SUNVOX FX**, and choose a patch in the browser.
2. Use **BACK TO ROUTES**, then connect the node's input and output. Newly added
   effects start unconnected, like the other independent processors.
3. Double-click the node or choose **BROWSE SUNVOX** to reopen its controls.
   Search accepts names, author prefixes and categories. Click the category
   button to cycle categories; scroll the list or use PREV/NEXT.
4. Drag a patch controller to adjust it; Shift+wheel makes fine adjustments,
   and right-click restores its saved default.
   Controller pages expose up to 128 controls. The Canvas inspector provides
   TapeSister's mix, output gain, bypass and bypass fade.

F9 shows the same instance and connections in the Routing Matrix. Patch changes
retain the instance ID and its external routes. Parameter edits support Matrix
undo/redo. Multiple copies have separate audio inputs and DSP histories.

To load a custom compound patch, select **BROWSE** to choose a `.sunsynth` file,
drop it onto the open SunVox browser, or paste its path into the field and select
**LOAD PATCH**. Windows **Copy as path** quotation marks are accepted. LOAD PATCH
with an empty path opens the file chooser. In the chooser, double-click a folder
or file, or select it and press Enter / OPEN. UP or Backspace goes to the parent
folder; Esc / FACTORY PATCHES returns to the factory bank. Its bytes and
controller overrides are embedded in the TapeSister project, so the original
file is unnecessary when reopening or moving the project. A `.sunvox` song must
first be packaged as a `.sunsynth` MetaModule; this host does not import songs or
provide SunVox's internal patch editor.

The bank retains all upstream entries: 28 chorus, 15 compressor, 26 distortion,
16 EQ, 58 miscellaneous, 10 note utilities, 13 phaser, 48 reverb, 19 stereo and
2 vocoder patches. The note category contains event processors, not standalone
audio effects. Matrix wires carry stereo audio. NOTE ON/OFF and the note number
can trigger an imported patch containing a synth, but this version does not wire
MIDI events between Matrix nodes or expose every internal SunVox module as a
separate Canvas node. Vocoder and other compound patches retain their own
internal routing and controller requirements.

## Timing and recall

Each SunVox instance adds a fixed 64-frame host buffer (1.33 ms at 48 kHz), plus
any latency inherent in the selected patch. Its own dry and wet paths are
aligned, including bypass. Different external Matrix paths are not automatically
latency compensated; a direct parallel path can therefore phase against a
SunVox path. The same processing runs for live audio and offline rendering.
Project recall restores the patch and controls, not the transient contents of
reverb/delay buffers.

Loading and replacing engines happens outside the audio callback. The existing
12 ms Matrix lifecycle crossfade switches between patches. Old engines are
collected off the audio thread. Patch controls are delivered at 64-frame
boundaries. Upstream controllers may resize their own internal buffers when
changed; loading the full bank is not a claim that every patch or every control
change is suitable for low-latency performance on an X220/X230 or t630.

The immutable imported-patch cache is process-local, capped at 128 MiB with an
8 MiB limit per file. This retains data referenced by undo history and project
snapshots. Restarting clears it. Factory patches are embedded separately. DSP
working memory can be substantially larger than the patch file.
Import errors identify missing/unreadable files, invalid modules, the per-file
size limit, and an actually full cache separately. Failed imports retain the
current patch and routes. Reimporting identical bytes reuses the cached resource;
a valid file can repair a partial cache entry left by an interrupted project load.

## Source and validation

The headless float32 SunVox 2.1.4d engine is compiled from a checked, pinned source
archive; no runtime download or separately installed DLL is required. CMake uses
Python 3 to generate the embedded factory bank. See
[provenance and licenses](../third_party/sunvox/PROVENANCE.md) and the
[per-patch manifest](../third_party/sunvox/EFFECTS.tsv).

`tapesister_sunvox_tests` checks all 235 patches and 20 standard effects, stereo input, instance isolation,
controller changes and resets, five sample rates, fixed host delay, Matrix
routing, bypass, replacement, project recall and truncated custom-patch rejection.
The separate `sunvox_project_write`/`sunvox_project_read` tests verify recall in a
fresh process. The FX Canvas controller test covers the browser, search, shared
Matrix identity, control edits and undo/redo. These are part of the Linux and
Windows PR baseline.

The **Standard effects** category offers Amplifier, Compressor, DC Blocker, Delay,
Distortion, Echo, EQ, FFT, Filter, Filter Pro, Flanger, LFO, Loop, Modulator,
Pitch shifter, Reverb, Smooth, Vocal filter, Vibrato and WaveShaper. These use the
same per-instance controls, buffering, project recall and MIDI paths as factory
patches. Synthesizers and miscellaneous note/control modules are not effects in
this catalog. In a pedalboard, selecting SUNVOX leaves the board visible; click
**EDIT SUNVOX** to select the module or patch.
