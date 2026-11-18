# Performance navigation

Right-click a card for recursive input/output tracing or connected-network
selection. Right-click selected cables to select their immediate sources or
destinations, or trace upstream/downstream. Traversal includes implicit Mosaic
bus links, sidechains, shared buses, branches, control links and feedback.

Choose **Bundle source cables** to summarize cables from the same movement and
group to the same input. Custom cable colors stay separate. Count badges identify
bundles; click or marquee a bundle to select and expand all its exact cables.
Selection then supports the existing gain, color, delete and Ctrl+Shift-click
reroute actions. **Expand all cables** returns the ordinary view.

**Focus selection** dims unrelated cards and cables and holds that focus when
selection changes. **Show all paths** clears focus. Use **Select connected
network**, then focus and Z to inspect an entire chain. These are view actions:
they do not change routing, processor lifetime or the operation undo history.

Below 45% zoom, cables omit arrows and control dashes. Curves remain finely
rasterized and use identical geometry for paint, click and marquee. A bounded
geometry cache covers endpoint, curve-style and drawable-size changes; the
existing raster cache covers colors, focus, bundles, detail level and viewport.

Controller coverage exercises branches, feedback, sidechains, shared and parked
sources, menu hit targets, bundle expansion, focus independence, history and
geometry reuse. Native-resolution cable rendering checks remain in the same
Windows/Linux regression executable.
