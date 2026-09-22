# Windows ASIO audio

Windows x64 builds include ASIO. Install the manufacturer's 64-bit MOTU driver
or VB-Audio Matrix and its virtual ASIO drivers before starting TapeSister.

## Setup

1. CFG → BACKEND → ASIO → SAVE CONFIG, then close and restart TapeSister.
   Backend changes require a restart. `ON:` shows the backend actually running;
   the selector shows the saved/requested backend. Changing backend resets device
   names from the old backend to defaults; choose the new driver after restarting.
2. In CFG, choose the MOTU or Matrix ASIO driver under OUTPUT. Leave INPUT on
   SYSTEM DEFAULT to share that driver. Select a buffer request, save, and restart
   after changing the ASIO driver or buffer. Start with 256 frames. TapeSister uses
   the driver's current sample rate and negotiated buffer; the Insert panel shows
   the actual values. Set the desired rate in the MOTU/Matrix control panel first.
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
Existing UI/DSP exclusion is respected; the driver callback never waits for a
UI lock and emits silence/counts a missed period if that lock is busy.

Only one ASIO driver may be open in TapeSister. Independent SEND/RETURN device
choices remain available on other backends. Old projects with named independent
endpoints need to be changed to the shared selections above when used with ASIO.
Driver changes and buffer changes are saved for restart. SCAN uses the cached
ASIO catalog while a stream is open: probing other drivers could disturb it.
Restart after installing a new driver. Reset/rate-change/stopped-stream detection
marks both endpoints unavailable; it does not silently switch drivers. Restart
or save CFG after the driver is available to reopen the output, then APPLY PORTS.

## Verification and remaining hardware test

Automated tests exercise the actual adapter against a simulated duplex driver,
including negotiated 48 kHz/128 frames, 16-channel stride, capture-before-output,
exact level/channel mapping, lock contention, stopped streams, start failure and
stale handles. Insert tests compare unity to bypass and check that no independent
Send/Return queues are used. Native callback tests compare a unity cable loop to
bypass through the actual mixer. Windows CI compiles the real RtAudio ASIO backend.
These tests do not substitute for a MOTU or Matrix listening/round-trip test.

To isolate the reported problem, first send a steady TapeSister tone into SunVox
with its effects bypassed and listen there. Then return that same dry signal to
TapeSister and compare Insert/bypass level. Finally enable reverb. Retain the
actual rate, buffer, channel pairs and GAPS counters from each test. A clean
SunVox-native synth return is useful evidence about RETURN, but is not a test of
TapeSister SEND.
