# Visualizer on devices with more than 16 physical outputs

The supplied October 17 session log switches from stereo WASAPI to Focusrite
USB ASIO at 48 kHz / 512 frames / **26 channels** before all three visualizer
open attempts. Each child reports ready; no visual freeze is requested. The
first open interval has no over-budget callback. The old log does not record
presented frames, so it cannot establish a graphics-driver stall.

The visualizer's callback tap rejected any stream above 16 channels. A hardware
device's exposed channel count can exceed the routing grid's output count;
even audio routed to the first stereo pair was discarded. The visualizer then
had no signal to illuminate its music-driven geometry. A unit reproduction
using a 26-channel stream failed at `ts_mandala_latest` before the fix.

## Fix and bounded work

The packet now stores interleaved physical channels in the same 1024-float
storage. Channels 1–255 are supported; wide devices reduce the number of
stratified sample frames to fit (39 frames at 26 channels, 4 at 255).
This preserves all output sockets, including signals only on outputs 25/26,
without increasing packet size, maximum callback copy size or analysis work.
The callback still has no allocation, lock, waiting or graphics work. Renderer
resolution, motifs, colors, quality selection and frame scheduling are unchanged.

The child publishes frame progress, consumed audio serial, configuration
revision and signal level through lock-free counters after presentation. The
host records them once per second as `visualizer_sample`, together with the
device channel count and freeze state. This provides a direct way to separate
input starvation from a stalled renderer in subsequent session logs.

## Regression coverage

- Packet bounds and correct source stride for 1, 2, 16, 17, 26, 64, 128 and
  255 channels with 1-, 73- and 512-frame callbacks; invalid widths and packet
  bounds are rejected.
- A signal only on outputs 25/26 reaches analysis with correct opposite-phase
  width and balanced left/right energy.
- Concurrent producer/consumer testing alternates 16-, 26- and 255-channel
  packets and verifies metadata/sample coherence and stalled-consumer bounds.
- The actual application callback feeds a live renderer child at 26 channels.
  Frame count and audio serial must advance with nonzero visual signal. Actual
  palette, shape and New Seed editor clicks must be acknowledged after drawing
  further frames. Closing and reopening must resume the same flow while
  TrackSister, Mosaic and ARP continue.

Run `ctest --test-dir build -R mandala --output-on-failure`. The controller uses
SDL dummy video and software rendering; it verifies application/IPC/render
behavior, not a physical Focusrite driver or Windows GPU. The supplied log
identifies a reproducible missing-audio defect; confirmation on the user's
device remains the final real-hardware check.

Local validation: the Release application built, all three Mandala suites
passed, and real application startup/audio/shutdown passed with an 8 MiB stack.
The packet/analysis test also passed AddressSanitizer and UndefinedBehaviorSanitizer;
leak scanning was disabled because this environment blocks LeakSanitizer thread
inspection. Native Windows/Linux release checks run on the PR revision.
