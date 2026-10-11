# Windows ASIO audio

Windows x64 builds include ASIO. Install the manufacturer's 64-bit MOTU driver
or VB-Audio Matrix and its virtual ASIO drivers before starting TapeSister.

## Setup

1. In CFG, choose BACKEND → ASIO. The device rows immediately list installed
   ASIO drivers, before changing the running backend. Choose your MOTU or Matrix
   driver in either INPUT or OUTPUT; both rows follow the same duplex driver.
2. Choose the buffer request, then SAVE CONFIG. TapeSister stops playback and
   reopens audio in place; the app and project stay open. Backend, driver and
   buffer changes do **not** require an app restart. Recording must be finished
   or cancelled first. Start with 256 frames. The driver negotiates the actual
   rate/buffer shown in Insert; set its rate in the MOTU/Matrix control panel first.
   `ON:` identifies the backend actually running. If a selected device cannot
   open, TapeSister attempts to restore the previous audio setup, reports the
   failure and leaves CFG open without saving the failed choice. No other device
   is silently substituted. If restoration also fails, choose an available device
   and save again to recover from silence.
3. F9 → INSERT SETUP: select MASTER DEVICE SPARES for SEND and SHARED CFG INPUT
   for RETURN. Pick channel pairs and APPLY PORTS. For example, Master 1/2,
   Send 3/4 and Return 5/6. These are channels of the one ASIO driver, not the
   separate stereo endpoint names shown by WASAPI. Save CFG to remember them.
4. Route Send to SunVox/REAPER and its processed output to the Return pair.
   Keep the return pair out of any path feeding itself. Toggle Router INSERT
   bypass to compare. With an unchanged loopback, SEND and RETURN at 0 dB are
   unity. Right-click each level to restore 0 dB.

Matrix's virtual ASIO driver can carry this software loop without the MOTU.
Connect its final Master channels to your playback device in Matrix. Both
applications must agree on the sample rate. Whether multiple applications may
open a hardware driver simultaneously depends on that driver's sharing support;
TapeSister does not change those restrictions.

## What changes in this backend

Master, ordinary input, Insert Send and Insert Return share one full-duplex
ASIO stream, sample clock and callback size. SEND writes directly to the selected
spare output pair in the Master callback. RETURN reads the corresponding input
block without the independent-device FIFO, resampler or clock-drift correction.
The existing routing controls address channels 1–8; wider device strides are
preserved and unused outputs are silent. Main output remains channel 1/2.
ASIO drivers using common PCM integer or floating formats are converted by the
vendored RtAudio ASIO backend. Unsupported driver formats fail to open.

The displayed Insert queue target is zero for this synchronous path; this does
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
save CFG after the driver is available to recover, then APPLY PORTS if needed.

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

To isolate the reported problem, first send a steady TapeSister tone into SunVox
with its effects bypassed and listen there. Then return that same dry signal to
TapeSister and compare Insert/bypass level. Finally enable reverb. Retain the
actual rate, buffer, channel pairs and GAPS counters from each test. A clean
SunVox-native synth return is useful evidence about RETURN, but is not a test of
TapeSister SEND.
