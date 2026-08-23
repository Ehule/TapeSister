# Realtime capture workflow

TapeSister treats a completed realtime performance differently from an editor or
generator state: it preserves the original performance before installing an editable
working copy. External input uses `INPUT_...wav`; FM performances recorded directly
to the REC BANK use `SYNTH_...wav`; and Capture-to-New-Tile uses `CAPTURE_...wav`.
All are mono 32-bit float WAV files in `Captures/`
(or `TAPESISTER_CAPTURES`) and use local time, milliseconds, process identity, and a
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

The input callback performs only channel selection/fold-down, external-source recorder writes, one
block peak publication, and an optional lock-free SPSC monitor-ring write. It does no
drawing, file I/O, allocation, or project mutation. The output callback consumes the
dry monitor ring after producing and feeding the internal CAPTURE performance mix, so
monitored input cannot be printed into internal CAPTURE or routed through tile effects.
When REC BANK source is SYNTH, the output callback feeds the synth-only `TsNoteBank`
submix to the same recorder before dry monitoring. This path does not open or round-trip
through a physical input device.

The UI periodically consumes only newly recorded external frames into a fixed 576
column min/max envelope representing roughly the latest ten seconds. Work and memory
therefore stay bounded for long takes. The meter uses the same mono samples and the
same `threshold_amplitude` used by the recorder; its display covers the recorder's
full -90 to 0 dBFS threshold range.

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
