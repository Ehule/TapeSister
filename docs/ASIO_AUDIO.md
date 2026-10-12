# Windows ASIO audio

Windows x64 builds include ASIO. Install the manufacturer's 64-bit MOTU driver
or VB-Audio Matrix and its virtual ASIO drivers before starting TapeSister.

## Setup

1. In CFG, choose **BACKEND → ASIO**. The device rows immediately list installed
   ASIO drivers. Choose MOTU or VB-Audio Matrix in either INPUT or OUTPUT; both
   rows follow the same duplex driver.
2. Choose the buffer request and **SAVE CONFIG**. Audio stops briefly while the
   device reopens; the app, project and routing stay open. Backend, driver and
   buffer changes do not require an app restart. Finish or cancel recording first.
   Choose the driver sample rate in its control panel and verify the actual
   rate/frames/channels in **F12 Audio Health**. CFG `ON:` identifies the running backend.
3. Open **F9**. New configurations use Main → EQ → limiter → OUT 1/2. Add input
   connections and processor/output connections as needed. ASIO pairs refer to
   channels of the one driver, not the separate stereo endpoint names shown by WASAPI.
4. For a series hardware loop, disconnect Main → EQ, connect Main → OUT 3/4 and
   IN 5/6 → EQ, and keep EQ → limiter → OUT 1/2. Wire those channels to/from the
   processor, or map them in VB-Audio Matrix for SunVox/REAPER. Name them in F9
   with a middle-click. See [Routing Matrix](ROUTING_MATRIX.md) for the full example.

If a chosen device cannot open, TapeSister attempts to restore the previous
setup, reports the failure and leaves CFG open without saving the failed choice.
No other device is silently substituted. If restoration also fails, select an
available device and save again to recover from silence.

VB-Audio Matrix's virtual ASIO driver can carry a software loop without the MOTU.
Connect its final Master channels to playback in that application. Both programs
must agree on sample rate; simultaneous access to a hardware driver depends on
its sharing support. VB-Audio Matrix is separate from TapeSister's F9 routing matrix.

### Legacy projects

Legacy serial projects retain Insert assignments. Use Matrix refuses conversion
while an external Insert is active; it does not silently drop a hardware loop.
Their old Insert setup uses **MASTER DEVICE SPARES** for SEND and **SHARED CFG
INPUT** for RETURN, with channel pairs such as send 3/4 and return 5/6, then
APPLY PORTS and SAVE CONFIG. Legacy INSERT bypass compares the internal path
against the 100%-wet return. These controls do not route audio in matrix mode.
See the [legacy reference](LEGACY_ROUTING.md#legacy-external-insert).

## What changes in this backend

The matrix's physical outputs and inputs share one full-duplex ASIO stream,
sample clock and callback size, without independent Insert queues. Matrix outputs
address up to sixteen channels and inputs up to eight; N/A pairs stay silent.

In legacy routing, Master, ordinary input, Insert Send and Insert Return share
that same stream. SEND writes directly to the selected
spare output pair in the Master callback. RETURN reads the corresponding input
block without the independent-device FIFO, resampler or clock-drift correction.
The legacy Insert controls address channels 1–8; wider device strides are
preserved and unused outputs are silent. Legacy Master remains channel 1/2.
ASIO drivers using common PCM integer or floating formats are converted by the
vendored RtAudio ASIO backend. Unsupported driver formats fail to open.

The legacy Insert queue target is zero for this synchronous path; this does
not mean zero round-trip latency. Driver buffers, the ASIO adapter, converters,
Matrix and external processing still contribute. GAPS reports ASIO/adapter
missed periods for the shared stream, so the two sides show the same counter.
Existing UI/DSP exclusion is respected. If a note edit briefly holds the audio
lock, the callback retries for at most the smaller of 0.5 ms and one eighth of a
buffer period. This avoids replacing a whole buffer with silence for a short
overlap. It never sleeps or takes an unbounded lock; a longer edit still emits
silence and counts a missed period. F12 Audio Health distinguishes short-lock
recoveries from control-lock skips and driver reports. Callback processing
timings exclude that retry interval. No extra audio buffer is queued.

Only one ASIO driver may be open in TapeSister. Independent SEND/RETURN device
choices remain available on other backends. Old projects with named independent
endpoints need to be changed to the shared selections above when used with ASIO.
Driver and buffer changes close both endpoint views and reopen one duplex stream.
SCAN uses the cached ASIO catalog while a stream is open: probing other drivers
could disturb it. Applying a new setup releases the driver and refreshes discovery.
Reset/rate-change/stopped-stream detection marks both endpoints unavailable;
save CFG after the driver is available to recover; reapply legacy Insert ports if needed.

For other SDL backends, the new backend must be applied before SDL can enumerate
its device list. Save CFG once to activate it, reopen CFG to choose devices, then
save again. Neither step requires closing TapeSister.

## Verification and remaining hardware test

Automated tests exercise the actual adapter against a simulated duplex driver,
including negotiated 48 kHz/128 frames, 16-channel stride, capture-before-output,
exact level/channel mapping, long lock contention, stopped streams, start failure
and stale handles. Deterministic clock tests cover brief contention recovery,
retry expiry and the smaller budgets needed for low buffer sizes. Native synth
tests cover note attack/release and velocity through the unified matrix. Tracker
tests record five velocity levels and verify their volume cells survive TSR
save/reload. The CFG integration fixture switches SDL → ASIO, switches duplex
drivers and buffers, returns to SDL, blocks changes during recording, and checks
rollback after a missing device or failed start. It also verifies persisted
choices, stable logical handles and preserved matrix routes/aliases. Insert tests compare unity to bypass and check that no independent
Send/Return queues are used. Native callback tests compare a unity cable loop to
bypass through the actual mixer. Windows CI compiles the real RtAudio ASIO backend.
These tests do not substitute for a MOTU or Matrix listening/round-trip test.

To isolate a loop problem, send a steady TapeSister tone into SunVox with its
effects bypassed and listen there. Return that dry signal to TapeSister, compare
it with the internal route, then enable the effect. Retain the actual rate,
buffer, channel pairs and F12 counters at each stage. In legacy mode, also record
Insert's GAPS counters. A clean SunVox synth return tests RETURN, not TapeSister SEND.
