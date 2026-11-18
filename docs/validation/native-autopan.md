# Native AutoPan

Add **AUTO PAN** from the FX Canvas effect menu or select it in a Pedalboard
slot. Canvas double-click and the Pedalboard **EDIT PAN** button open the same
oscillator editor. MetaFX supports AutoPan stages and rate/depth/center macro
mapping; right-click an AutoPan stage's type to open its oscillator editor.
Escape returns to the calling window. Click, drag or wheel a control to edit,
right-click to restore its default, and use Ctrl+Z / Ctrl+Y for operation history.
The Canvas inspector also exposes all oscillator options.

- Rate: logarithmic 0.01–100 Hz, or master-tempo divisions from eight bars to
  1/128 notes, including dotted eighths and eighth/sixteenth triplets.
- Wave: sine, triangle, square, saw, stepped random and smooth random.
- Depth and center: modulation amplitude and left/right bias.
- Stereo balance uses equal-power gains with unity at center. At hard pan the
  remaining channel can gain 3 dB. Stereo channels are never summed to mono.
- Dual tremolo modulates both channel gains with adjustable stereo phase;
  180 degrees alternates sides. Stereo phase applies in this mode only.
- Reset on start/home restarts oscillator phase and deterministic random state;
  free-run retains phase. Player starts include direct Mosaic, tracker and ARP
  starts. Pausing does not freeze a still-processing oscillator or cut FX tails.
- A one-millisecond gain slew softens waveform discontinuities and parameter
  changes. Mix, output gain, host bypass fades and host suspension remain active.

Original P1, Canvas nodes, additional Pedalboards and MetaFX use the same native
DSP, without an SDK instance, delay line, callback allocation or worker job.
Controls survive project/preset saves and Canvas consolidation. Existing saved
FX type numbers retain their previous meanings.

`tapesister_autopan_tests` checks all waveforms and both modes at five sample
rates, block/scalar/in-place equivalence, stereo isolation, bounded gain edges,
rate endpoints, tempo and restart behavior, identical output across all four
hosts, project/preset/MetaFX recall, and malformed settings rejection. It is in
the Windows/Linux PR regression manifest with assertions enabled in Release.
The SDL Canvas controller test exercises add/edit/undo/redo/Escape and both
consolidation destinations, plus the original Pedalboard editor.
