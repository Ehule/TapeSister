# Visualizer backoff under audio pressure

The reported WASAPI/MOTU session still clicks when opening the visualizer, with
screen recording making it worse. Earlier reports show callback deadline
overruns with short control holds. This is consistent with resource contention,
but does not establish which workload or driver causes each audible click.

The renderer already runs in a separate lower-priority process. Its existing
backoff only measured render time, so a fast renderer could keep drawing while
audio missed deadlines. This change feeds existing timing telemetry to it from
the UI thread, without adding audio callback work or an audio lock.

## Policy

- Sample existing atomic timing counters at most every 100 ms while open.
- Level 1: recent average load at least 65%, a recent peak at least 85% of the
  buffer budget, or a new near-overrun. Lower internal resolution/detail and
  cap animation at 15 fps.
- Level 2: a new callback overrun or recent average load at least 85%. Lower
  internal resolution to 512x320, halve the selected detail budget and cap
  animation at 8 fps.
- Restore one level after ten seconds with average load at most 55%, peak at
  most 75%, and no new near-overruns/overruns. Reset the recovery interval when
  headroom becomes marginal. A stream with no callbacks for a second can also
  recover, rather than retaining its last busy window forever.
- Retained maxima and historic overruns do not trigger the guard on launch.
  Start gaps alone do not trigger it because backend batching can cause them.
- Renderer-local backoff remains independent. Saved quality, musical settings
  and transport state are unchanged. Freeze and close retain their behavior.
- Report open/frozen state, requested quality, renderer category, animation
  cadence cap and guard level in copied Audio Health text. The cap is not a
  measured frame rate; hidden/frozen animation reports zero.

Resizing uses the existing renderer target-recreation path, so a change of
level resets retained visual trails. It does not reset any audio effect history.

## Validation

Linux Release application built with SDL 2.30 (local MIDI and CDP bundling
disabled). All three visualizer CTest suites passed:

```sh
ctest --test-dir build -R '^tapesister_mandala(_queue|_controller)?_tests$' --output-on-failure
```

New checks cover historical counters, fresh overruns, average/peak pressure,
recovery hysteresis, stopped/reset streams, SDL tick wrap, independent renderer
backoff, quality preservation and native child-process receipt of an 8 fps cap.
The integration check also verifies unchanged audio-lock counts and the new
Audio Health report. Existing freeze, suspended/killed renderer, close/reopen,
transport, bounded queue, physical output and editor checks remain passing.
Rendered and inspected the 512x320 guard output scaled into the normal window,
including the controls and frame-rate/guard diagnostics.

## Remaining hardware check

This is a mitigation, not a verified cure for the Windows/MOTU clicks. It reacts
after telemetry arrives and cannot prevent the first miss, the initial graphics
driver setup cost, or load from a separate screen recorder. The local renderer
uses software/dummy video; it cannot reproduce Windows GPU/driver scheduling.

On the new build, load the same project and reset Audio Health after choosing
the buffer. Compare playback with the visualizer closed, open, then frozen,
initially without screen recording. If clicks recur, copy Audio Health including
the build header and visualizer line; record whether Freeze Visual or closing
the window immediately stops them. Screen recording can then be compared as a
separate workload.
