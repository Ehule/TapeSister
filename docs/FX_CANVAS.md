# FX Canvas

Open **Mosaic → FX CANVAS**, beside the current sound information, or press
**Shift+F9** from any workspace. **F9** switches to the routing matrix; Shift+F9
switches back. The same window, graph, stable processor identities, audio
histories and undo stack are used by both views. Opening either view does not
activate or rewrite saved legacy routing. An inactive graph says SAVED GRAPH;
USE SAVED CHAIN uses the existing preservation-aware importer.

The canvas contains effect instances and input/output endpoints. Samples,
TrackSister patterns and generators continue to live in their existing editors.
Mosaic Bus, TrackSister Bus and Live Tile Bus represent their ordinary audio;
explicit tile/track/card routing overrides appear as individual endpoints.
Inherited track-to-tile routing remains inspectable in the Matrix.

- Drag an output port on the right of a node to an input port on the left.
  Every destination sums its incoming connections. Fan out to multiple effects
  for parallel branches and reconnect their outputs to combine them.
- Purple ports/cables are compressor sidechains. Existing delayed feedback
  edges are amber. The Matrix's cycle validation and feedback policy apply.
- Click a cable, then wheel to change its level (Shift: fine). Delete disconnects
  the selected cable. Click empty space to release cable selection before zooming.
- Select an added effect to reveal its existing parameter controls below the
  canvas. Filter also opens its complete editor on double-click. Fixed processors
  retain their existing editors; the Matrix exposes their ordinary routing controls.
- Drag a node to organize it. Middle-drag pans; wheel zooms around the pointer.
  FIT or Home frames the graph. Layout, pan and zoom save with the project.
  View edits are undoable and never reset processor histories. Up to 256 manually
  positioned nodes are retained; other nodes use the automatic layout. Positions
  use stable effect identities so a replacement instance cannot inherit an old
  processor's placement.
- CONNECTED / BUSES shows the working network. ALL ENDPOINTS also exposes the
  fixed processors and available input endpoints. Closed output ports say N/A.
- Undo/Redo and Ctrl+Z/Ctrl+Y are shared with the Matrix. Audition keys pass through
  to the instrument. MOSAIC / CLOSE returns to Mosaic without stopping audio.

## Entering a Meta FX

Double-click a Meta FX node to enter its internal chain. The breadcrumb names
that rack; BACK TO FX or Escape returns to the outer graph. Its four macros can
be adjusted directly without replacing the running processors. Double-click a
stage or click RACK EDITOR for the complete stage, mapping, A/B, preset and file
controls. Existing `.tsmfx` files remain compatible.

This first internal view displays the existing eight-stage serial rack. Parallel
branches are fully supported on the outer canvas. Packaging arbitrary parallel
subgraphs, nested racks, and editing internal connection topology are subsequent
extensions; the canvas does not silently flatten a branched graph into a chain.

The preview design board illustrates the direction. The native screenshots below
are captures of this implementation, including its current internal-rack scope.

![FX Canvas](screenshots/fx-canvas/canvas.png)
![Inside a Meta FX](screenshots/fx-canvas/inside.png)
![Mosaic access](screenshots/fx-canvas/mosaic.png)

## Texture processor levels

Resonator previously averaged six .35-excited combs, leaving its fully wet output
about 17 dB below the test input. A fixed **+12.04 dB wet makeup** follows that
network. Feedback, excitation, damping and dry/bypass behavior are unchanged.
This is a fixed calibration, not an adaptive gain rider or compressor.

Dispersion measures unity RMS at 100% wet after settling. Its dry/wet phase
interference is intentional, so no blanket boost is applied. The normal OUTPUT
control remains available.

Factory Meta FX presets now store visible makeup in their final stage's OUTPUT
control. New/cycled presets receive these trims; saved and custom rack controls
are loaded literally. Serial processing remains equivalent to the same individual
processors. Re-select a factory preset to get its revised output trim.

| Factory rack | Final-stage output trim |
|---|---:|
| Arc | 0 dB |
| Pale Cloud | +4 dB |
| Shimmer Ash | +2 dB |
| Iron Choir | +6 dB |
| Spring Dust | +5 dB |
| Glass Thread | +2 dB |
| Broken Halo | +4 dB |
| Slow Weather | +3 dB |
| Charcoal | 0 dB |
| Resonant Room | +5 dB |

`cmake --build BUILD --target tapesister_fx_gain_probe` builds a reproducible
noise/two-tone RMS and peak report: 48 kHz, ten seconds per condition, measured
across the final five seconds. This is signal-dependent calibration, not a claim
that all sources or macro settings have identical perceived loudness. The regular
Meta FX DSP gate checks steady broadband level, stereo isolation, bypass, preset
bounds, serial equivalence, persistence and lifecycle behavior.
