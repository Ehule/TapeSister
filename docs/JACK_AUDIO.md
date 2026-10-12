# JACK audio and Insert troubleshooting

Select **CFG → BACKEND → JACK** and **SAVE CONFIG**. Linux builds
include a native JACK adapter; SDL itself does not need JACK support. The JACK
runtime (`libjack.so.0`) and a running JACK server are required. For PipeWire's
JACK compatibility layer, launch TapeSister through `pw-jack` where your distro
requires it. Windows also has native ASIO; remaining backends use SDL.

SAVE CONFIG applies the backend/device change in place, with a brief audio stop;
finish or cancel recording first. Reopen CFG to select **TapeSister Master/Input**
after applying the backend, then save again if needed. No app restart is required.
Saved names remain explicit; failed setup changes attempt rollback and stay unsaved.
CFG **SCAN** refreshes input/output lists without interrupting active streams.
Legacy Insert has its own RESCAN/APPLY PORTS controls.

## JACK routing

JACK presents these application endpoints. Connect hardware and other programs
in QjackCtl, Carla, or another JACK/PipeWire patchbay. Ports appear when the
corresponding stream opens. TapeSister does not connect them automatically.

| Selector | Endpoint | JACK port names | Purpose |
| --- | --- | --- | --- |
| CFG output | TapeSister Master | `master_1` … `master_16` | Matrix OUT pairs; legacy Master on 1/2 plus spare sends/spatial outputs |
| CFG input | TapeSister Input | `input_1` … `input_8` | Matrix IN pairs; legacy EXT/shared Insert return |
| Legacy Insert SEND | TapeSister Insert Send | `send_1`, `send_2` | Dedicated stereo output to the external processor |
| Legacy Insert RETURN | TapeSister Insert Return | `return_1`, `return_2` | Dedicated stereo input from the external processor |

### Matrix path

For a software/hardware loop, route **Main → OUT 3/4** in F9, then **IN 5/6 → EQ**
and **EQ → limiter → OUT 1/2**. Disconnect Main → EQ for a series loop. In the
JACK patchbay connect `master_3/4` to the processor, its return to `input_5/6`,
and `master_1/2` to speakers. Other input/output pairs work the same way.
Matrix routes do not use the dedicated legacy Insert Send/Return endpoints.

### Legacy Insert path

The following describes the retained serial engine/old Insert controls, not an
active matrix. For a legacy software Insert:

1. Use **TapeSister Master** as CFG output; connect `master_1/2` to your playback
   interface in the patchbay.
2. In F9 → INSERT SETUP, select **TapeSister Insert Send** and **TapeSister Insert
   Return**, with local **DEVICE CH 1/2** on each. Click **APPLY PORTS**.
3. Connect `send_1/2` to SunVox or REAPER's inputs, and that application's outputs
   to `return_1/2`. Enable input monitoring/processing in the external application.
4. Enable Insert in the Router. Its steady active output is entirely the return.
   Bypass auditions the internal path. Save CFG to retain the choices.

Ordinary input stays independent when using the dedicated return. To monitor or
record EXT, select TapeSister Input and connect the required physical/software
sources to its input ports. Those eight ports are application channels, not a
claim about the physical interface's channel count. Separate application instances
may receive JACK client-name suffixes; use the actual patchbay names.

JACK determines the sample rate and period. TapeSister reports those negotiated
values; CFG's buffer request does not override the server. Stop playback before
changing server rate/period. Such a change or server shutdown marks old streams
unavailable and silences them, rather than processing with stale timing. Once JACK
is available, save CFG to reopen audio in place and reconnect the patchbay ports.
Missing JACK does not silently select another backend.

## Discovery and supported layouts

The backend selector offers native JACK on Linux plus supported SDL backends
present in the installed SDL build (for example PipeWire, PulseAudio, ALSA,
WASAPI, DirectSound, WinMM, and CoreAudio). Availability in the selector does
not guarantee a running server, accessible device, or driver sharing support.
Only devices exposed by the active backend can be opened. Windows also has a
[native ASIO backend](ASIO_AUDIO.md) with one shared duplex driver.

Insert now attempts explicit devices even when SDL cannot report their channel
layout before opening. General capture accepts the first valid negotiated layout
instead of repeatedly rejecting it for differing from the request. Native layouts
larger than eight channels are accepted and traversed at their actual stride;
EXT and legacy Insert selectors address the first eight channels; matrix outputs
address up to sixteen. Unassigned outputs are silent. General mono output folds Master to mono; Insert endpoints
still require a stereo pair. Missing named endpoints never substitute another
physical device.

## Legacy Insert queue delay and dropouts

This applies equally to REAPER, SunVox, a cable loop, and other processors.
The SEND and RETURN bridges each size their queue for both callback bursts and
sample rates. A 4096-frame endpoint previously exceeded the 4096-frame priming
cap, allowing starvation. The cap is now 8192 frames with a 32768-frame ring.
This does not enlarge the normal matching 128/256/512/1024 queue targets.

A stalled consumer can fill a ring with stale audio. Insert now detects backlog
above twice its target at a callback boundary, discards down to its target, and
crossfades the recovery over 32 samples. This bounds the extra stale delay; it
cannot recover audio lost during the stall. **DROP** includes those discarded
frames as well as overflow. This recovery policy applies to both Insert bridges;
ordinary EXT monitoring retains its previous consumption policy.

CFG now also offers **128 frames**. At matching 48 kHz/128, each Insert bridge's
priming target is about **5.3 ms**, versus 10.7 ms at 256. These are queue targets,
not measured total round-trip latency. JACK's worker/port transfers, hardware,
the external application, and scheduling add delay. Smaller buffers demand more
CPU scheduling headroom.

Run with `--diagnostic-audio` while reproducing the problem. Every five seconds,
the log reports each bridge's actual stream rate, device buffer, queue occupancy,
target, underruns, dropped frames, and rate correction. On JACK it also includes
`jack_worker_xruns` for each endpoint (missed worker deadlines/overflow, distinct
from server-wide JACK xruns). The Master endpoint is logged separately too.
The on-screen GAPS counters describe Insert queues, not every external dropout.

Test SEND and RETURN separately as well as together: monitor/record SEND in the
external app, then feed a known clean tone into RETURN. Compare a direct loop
with the external app bypassed against a processing loop. Keep a common rate,
and note actual backend/buffer settings in both programs. Windows MOTU listening
and measured round-trip validation remain necessary; simulated tests do not
establish that the user's hardware issue is resolved.

## Automated validation

`tests/test_jack_backend.c` tests named endpoints, server-negotiated timing,
stereo transfer, pause/lock behavior, shutdown/rate/buffer changes, and cleanup.
`--live` additionally opens all four endpoints and connects stereo Send/Return
and multichannel Master/Input through a running JACK server. The JACK audio CI
workflow runs that test with JACK's dummy hardware driver at 48 kHz/128 frames.

The Insert regression covers independent clocks, unequal buffers/rates, delayed
callbacks, 128-frame streams, 4096-frame endpoints, sixteen-channel device stride,
and stalled consumers. Native controller tests exercise missing metadata, larger
negotiated devices, and failed enumeration alongside existing lifecycle tests.
