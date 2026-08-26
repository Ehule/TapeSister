# Realtime capture workflow

TapeSister treats a completed realtime performance differently from an editor or
generator state: it preserves the original performance before installing an editable
working copy. External input uses `INPUT_...wav`; FM performances recorded directly
to the REC BANK use `SYNTH_...wav`; and Capture-to-New-Tile uses `CAPTURE_...wav`.
Archives are 32-bit float WAV files in `Captures/` and preserve the take's explicit
channel shape. Internal Capture defaults to mono (**M**) and may deliberately preserve
stereo (**S**); external MIX/LEFT/RIGHT takes are mono and STEREO takes are stereo.
FM/SYNTH remains native mono. Files live in `TAPESISTER_CAPTURES` when set and use local
time, milliseconds, process identity, and a
collision counter. Files are written to a temporary name and renamed only after the
WAV closes successfully. TapeSister never scans this folder for cleanup and no tile or
project operation owns an archived file.

Canceled/empty takes are not archived. Normal edits, generated sounds, transforms,
previews, and history checkpoints are not archived. If archive I/O fails, the working
take is still retained and the UI reports the failure explicitly; fixing an unavailable
or full destination remains a user action because silently deleting a playable take
would be worse.

## Sample pages and project compatibility

Each page is a complete existing 16-tile `TsInstrument`, preserving its established
tile-local history and render caches. Page switches park the current object and swap in
another complete object while playback, CAPTURE, and Transform previews are stopped.
The REC BANK uses the same parking mechanism and is not a Sample page.

KEEP deep-copies occupied REC tiles in ascending order into ascending empty Sample
slots, then clears the REC BANK only after every copy succeeds. Allocation/copy failure
rolls back all new destinations and any pages created by that KEEP operation.

The primary `.tsr` is page 1 and remains a normal single-page project; TSR27-aware
builds can open it without the companion folder.
`<project>.tsr.samples/manifest.txt` declares the page count, active page, and optional
REC BANK; later pages and REC state use ordinary TSR27 members. Opening a project is
transactional: all members load into temporary instruments before current state is
replaced. The bundle is intentionally simpler and safer than widening the fixed
16-tile serializer, but users must keep the `.tsr` and companion folder together.

## Audio and rendering boundaries

Sister PR9 head Capture includes that head's PR8 weave and targeted fixed-chain
effects because the insertion precedes the established tap. Unselected head taps
remain unchanged. MIX Capture includes MIX-target post effects after OUT and
retains the existing linked safety and explicit mono `0.5 × (L + R)` fold. MIX
effects never leak into raw head taps; ordinary main Capture remains at its
established pre-monitor source point.

PR10 duration changes never stop or re-arm Capture. The selected H1/H2/H3/MIX tap is
published at the same stage while rolling history is resized by age; Capture recorder
capacity and destination transaction remain independent of Sister buffer seconds.

The input callback accepts the negotiated float capture layout from one through eight
channels. MIX (Mono Sum) averages every channel to mono and sends it equally to EXT L/R;
LEFT/RIGHT select hardware channel 1/2 as dual mono (RIGHT uses 1 on a mono device);
STEREO averages hardware channels 1/3/5/7 to left and 2/4/6/8 to right, preserving
ordinary stereo at unity gain and paired multichannel layouts with deterministic
headroom. It performs only that conversion, channel-aware external-source recorder
writes, one block peak publication, one atomic activity-mask publication, and an
optional lock-free SPSC monitor-ring write. It does no
drawing, file I/O, allocation, or project mutation. The output callback consumes the
dry monitor ring after producing and feeding the internal CAPTURE performance mix, so
monitored input cannot be printed into internal CAPTURE or routed through tile effects.
When REC BANK source is SYNTH, the output callback feeds the synth-only `TsNoteBank`
submix to the same recorder before dry monitoring. This path does not open or round-trip
through a physical input device.

The UI periodically consumes only newly recorded external frames into a fixed 576
column min/max envelope representing roughly the latest ten seconds. Work and memory
therefore stay bounded for long takes. The meter uses the maximum magnitude of the
selected frame pair and the same `threshold_amplitude` used by the recorder; its display covers the recorder's
full -90 to 0 dBFS threshold range.

The main header always shows eight compact `IN` positions. Positions beyond the
negotiated channel count are dim, available-but-silent positions use the inactive
button color, and channels crossing 0.001 linear amplitude (-60 dBFS) light in the
waveform color. Detection happens during the existing capture scan; the UI provides a
140 ms visual hold without locks or callback-side timing work.

For a MOTU M6 performance setup, connect Terra L/R to hardware inputs 1/2 and the
second computer's L/R to 3/4 with phantom power off and hardware gains set first. In
the Linux audio control, select the M6 multichannel or Pro Audio capture profile rather
than a stereo-only profile, then start TapeSister. Select the M6 as INPUT, select
STEREO (`record_input_channel=3`), and save the configuration. At least `IN` positions
1-4 must appear available; the exact additional available positions depend on the
profile/backend's M6 channel exposure. Play each source separately and confirm only
1/2 or 3/4 lights, then play both and confirm 1-4. If only two positions are available,
fix the Linux device profile before troubleshooting Sister. No JACK graph is required.

Playback and capture request 256-frame SDL buffers. SDL may still add platform/device
latency, and the monitor ring primes 128 input frames to tolerate callback scheduling.
Selectable 64/128/256/512-frame device buffers remain a follow-up: exposing them safely
requires config migration and device-specific fallback/validation beyond this workflow.

## Manual hardware checks

Automated tests cover page/KEEP ordering and preservation, legacy and multi-page
project loading, REC state round trips, archive uniqueness/immutability, meter
publication, rate conversion, and bounded live envelopes. Release validation still
requires real SDL devices on Linux and Windows: arm/threshold/pre-roll/tail, long-take
display cost, device/channel selection, dry-monitor stability and latency, headphone
feedback safety, early stop/cancel, and recording with MONITOR both off and on.
