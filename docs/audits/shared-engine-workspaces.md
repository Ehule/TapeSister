# Shared engine routing workspaces

This change builds on `95ca22ac15c2a51ce321ccbde502202e6587b501` and targets the shared callback, not a particular effect preset. The previous callback cleared and copied the full 186-destination routing capacity several times per sample and finalized all 128 hardware output pairs even when most were unused.

## Runtime changes

- Reuse audio-owned route mixes for the host, manual source groups and Sister runtime. Reset destinations owned by the previous frame; retain hardware availability where required.
- Pass a borrowed, explicitly owned graph into Sister rather than copying the entire source packet. Existing dense `matrix_graph` callers remain supported, including offline/core callers.
- Preserve the original accumulation order: performance input enters MAIN before program gain, then legacy parallel/speaker contributions are added. There is no reassociation of source sums.
- Refresh matrix live-input rows read by current routes and input meters, rather than copying unused interface capacity. Routed high-numbered inputs still work for dense callers even without an active physical device.
- Finalize owned output pairs and pairs owned by the preceding frame. The latter explicitly clears a disconnected output. Feedback writes participate in ownership, so reused buffers cannot replay a retired feedback contribution.

These changes preserve per-sample processing, event timing, graph order, feedback delay, tails, capture taps, routing slews, sound quality and existing latency. They add no callback allocation, threads, automatic bypass or new quality setting. Reusable scratch consumes a small fixed amount of runtime memory; this is not a project-memory reduction claim.

## Verification

`test_source_route.c` compares reusable mixes, graph copies and gain scaling to dense resets across every destination and alternating ownership, including output availability and self-mapped resets. `test_routing_matrix.c` compares dense and sparse execution across all destination banks, API-mode changes, high physical input/output ports, route fades, feedback activation/removal, disconnects, invalid samples and borrowed/dense source packets. It compares output samples, graph buffers, feedback history, route state and meters. The existing source, capture, Gallery, worker, effect-lifecycle and UI regression suite remains required.

For a whole-callback comparison, build `tapesister_audio_benchmark` from the parent and the candidate with the same Release compiler and options. Apply only the benchmark's transport-normalization helper to the parent harness. Its deterministic SunVox seed remains test-only. Use the original Tree-of-Life-Live-FX-04 archive (SHA256 `e6bd4d10604365820d517572a431686605183576a0af45d79151b9f2d33a66a4`). The benchmark loads it without rewriting it.

```sh
TS_BENCH_NORMALIZE_TRANSPORT=1 TS_BENCH_GALLERY=0 TS_BENCH_INTERVAL=30 \
  ./tapesister_audio_benchmark /path/to/Tree-of-Life-Live-FX-04.tsr 608 0 48000 512 4
```

Run both executables sequentially without concurrent builds/tests. Compare both stereo and complete hardware-output hashes. Also compare repeated 30-second runs starting at Keter 450 seconds, Chokhmah/Binah from 300 seconds, 26 output channels, and 128/1024-frame callbacks. Seek cases start with cold effect history and must not substitute for the uninterrupted full Keter run. Keep profiling and workers disabled on both sides. Record callback thread/process CPU, wall-time distributions, interval timings and process peak RSS; RSS includes project loading. Report measured results in the PR rather than treating a graph microbenchmark as a whole-program saving.

## Scope of the result

The full canonical callback includes voices, Gallery, all loaded effects and output processing. The headless benchmark does not measure live ASIO/JACK scheduling, UI rendering, or prove that audible clicks are eliminated. Current hidden-tracker refresh is already gated in `tracker_refresh`; older UI timings cannot establish the cost of the current head. UI painting, project-resource growth, and a true span/block renderer remain separate parts of the whole-program efficiency effort. This change removes shared capacity-based routing overhead while retaining the existing chronological audio semantics.
