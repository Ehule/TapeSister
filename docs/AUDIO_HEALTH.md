# Audio Health

Press **F12** or click **CFG → AUDIO HEALTH** to open a separate, resizable
monitor. It stays on top while you adjust controls in the instrument, so the
readings remain visible. F12, Escape or the window close button hides it;
playback continues. Counters continue while it is hidden.

Normal sessions are now recorded automatically, including while this window is
closed. Use **SAVE SESSION** and **LOG FOLDER** to share the timeline; optionally
mark something you heard with **HEARD A CLICK** or **Ctrl+Shift+F12**. See
[Session recording](audio-session-log.md) for contents, storage limits and controls.

For a controlled comparison, get the notes, ARP and effects running, press **RESET**,
then repeat the parameter edit or keyboard drag. **COPY REPORT** puts the current
measurements on the clipboard. Include that report, which control you moved,
and whether you heard a click. Compare a run with controls stationary against
one with the same controls moving.

Action buttons highlight for about a second and show their result: **COPIED**,
**RESET DONE**, **CLICK MARKED**, or **FOLDER OPENED**. Failed actions show an
error border and a failure label. **SAVE SESSION** first confirms **SAVE QUEUED**,
then highlights again as **SESSION SAVED** or **SAVE FAILED** when the background
export finishes. The profile button stays highlighted while profiling is running.
These indicators use the Health theme colors and do no audio-thread work.

The report's **Active setup** section lists keyboard HOLD and active/held voice
instances, ARP and outer ARP transport, Sister power/transport/monitor/source
switches, Fallout, Prism and its Matrix/lens sequence, all configured pedalboard slots
with their types/mixes/placements, Insert availability, and Router order/bypass.
Enabled switches and effective routing are shown separately: an enabled Prism
can still be bypassed, and Fallout also depends on the FX master. These are
transport/settings observations, not proof of audible output; fades and tails
can continue. Voice counts are instances across playback routes, not distinct
pitches. Prism's base mode/lens count are configured values; Matrix morphs can
use different values.

Mosaic reports total arrangement cards, cards crossing the running playhead, and
the notes assigned to those cards. Muted/solo-excluded cards and finished one-shots
still count while their card duration crosses the playhead. These are workload
counts, not detected sound; stopped Mosaic reports zero at the playhead. This
distinguishes a large sparse arrangement from many simultaneous cards/chords.
The report also includes the exact Mosaic position, repeat/range settings, and
total muted/solo cards, so repeated reports can distinguish an unmoving clock
from cards that remain muted.

For silence with a frozen playhead, copy the report **while the problem is
present**, before Reset or reopening the project. Callback progress uses the
last measured completion time, even if Health was closed when playback stalled.
`NO RECENT COMPLETION` means no timed application callback has completed for at
least one second; it does not identify the cause. Compare it with ASIO control-lock
skips and the current UI audio lock. A held UI lock includes its nesting depth,
elapsed hold and original call site. The session timeline records this progress
once per second while its UI service is running.

With **Unified routing matrix: ON**, the report lists active connections with
levels and row pan, names the hardware pairs, and labels legacy order/source
switches/Insert assignments as inactive. Processor power remains relevant. For
actual signal activity, use the softly fading indicators in [F9](ROUTING_MATRIX.md);
the setup snapshot reports controls, not measured sound.

Setup is copied at most four times per second while the monitor is visible,
PROFILE is enabled, or the session recorder is running,
during the UI's existing locked state read. Formatting happens after that lock
is released; this adds no audio-callback work or additional lock acquisition.
The report includes snapshot age. Setup describes the recent state, **not the
state at the time of a retained maximum**; reset before the gesture you want to
investigate. Before the first UI snapshot it reports that setup is unavailable.

| Reading | Meaning |
| --- | --- |
| Actual output / buffer duration | The opened device's rate and buffer size, which can differ from CFG. Buffer duration is not measured round-trip latency. |
| Processing load | Callback processing time divided by the duration of audio rendered, over the last completed quarter-second of audio. This is not system-wide CPU utilization. |
| Recent average / peak | Callback execution time in that recent window. |
| Callback progress | Age of the last timed callback completion. Recent load figures may be retained from before a stall. |
| Current UI audio lock | Whether the reporting UI thread still holds the measured output lock. This does not report locks held by other threads. |
| Maximum / over budget / near | Retained since startup or Reset. Over means execution reached the callback's own buffer duration; near means 90–100%. |
| Long start gaps | Callback arrival exceeded the previous block's duration by more than the larger of 1 ms and 25% of that duration. The maximum includes smaller excess gaps too. |
| Maximum hold / over buffer | Time an instrument control held the existing output-device lock after acquiring it. Nested locks count as one outer hold. Holds reaching one buffer duration are counted. |
| Maximum UI wait | Time a control spent waiting to acquire that lock. It is distinct from the period during which that control excluded playback. |
| ASIO driver reports | Native stream status reported by RtAudio; it may cover input or output. |
| ASIO control-lock skips | The callback could not acquire the shared control mutex within its bounded retry budget, so the buffer was silent. |
| ASIO short-lock recoveries | An initially busy control mutex became available within the smaller of 0.5 ms and one eighth of the buffer period; the callback rendered rather than skipping that buffer. |
| JACK worker queue gaps | Native JACK worker deadline/queue failures. These do not identify which operation caused them. |

Other SDL backends show that dropout counters are unavailable. A zero timing
counter does not certify clean hardware output. Start gaps can include operating
system scheduling, backend buffering and callback batching; they do not by
themselves prove a dropout or identify a control as its cause. Callback processing
time excludes native capture processing, driver work and time spent waiting
outside the application callback, including the ASIO short-lock retry. The monitor does not measure external Insert
latency, acoustic latency or waveform discontinuities.

The report includes the longest control-hold call site to help locate expensive
edits. TSR loading prepares files and decodes samples before the locked publication
step, and retires old sample storage after unlocking. Runtime/effect setup still
contributes to `load_instrument` holds. Mosaic group placement plans outside the
audio lock; its checkpoint/copies/final coordinates publish together. Compare a
freshly reset measurement while repeating these operations, since an earlier
maximum remains in the report until Reset.

Raw backend/device lifecycle operations are not all covered by these
control-lock measurements. Pause, resume and output replacement restart callback
cadence and the recent window. Session maxima persist until Reset. Native counts
restart when their stream is recreated; the report names that scope explicitly.
Briefly stale recent readings are dimmed when callbacks stop; the report retains
the last completed window. Individual session counters are approximate snapshots
while audio runs; the recent load window is published consistently.

## Implementation and validation

Timing uses two performance-counter reads per output callback and per outer
control hold (plus one before acquisition to separate wait). Counter updates are
bounded, with no allocation, file writing, extra lock or retry loop added by the
telemetry. The session recorder adds a bounded packet enqueue; its background
thread handles logging and system counters. The ASIO adapter separately uses the bounded retry described above.
Instrumentation is enabled only when its atomics are lock-free. Rendering,
formatting and clipboard work stay on the UI thread. The visible panel updates
at most four times per second, uses no vsync wait, and does no drawing when hidden
or minimized. Reading telemetry never acquires the audio lock. Reset briefly
uses the existing device lock to exclude writers; use it before the test gesture.

No DSP, sample-rate, buffering, oversampling, interpolation, voice-count or
effect-quality settings are changed. Tests cover deterministic load/deadline
calculations at 44.1/48/96 kHz, block-size changes, lifecycle gaps, wait versus
hold, reset, nested control locks, concurrent snapshots, the actual output
format, clipboard controls and window input isolation. The native ASIO fixture
separately checks a driver report and a forced lock miss.

The actual-callback benchmark can compare instrumentation off/on:

```sh
build/tapesister_realtime_controller_tests --benchmark 2 2000 0
build/tapesister_realtime_controller_tests --benchmark 2 2000 1
```

Scenarios 0/1/2 are dry, pedalboard, and pedalboard plus monitored Sister, each
with two held notes and inner/outer ARP sequencing. Compare several alternating
runs on the same machine; the printed audio hash must match. Physical Windows
driver/buffer limits and listening checks still require the actual interface.

Local validation after PR 120: 25 focused audio/controller regressions pass;
the new core/window tests also pass AddressSanitizer and UndefinedBehaviorSanitizer
(LeakSanitizer is unavailable in this environment). Three alternating on/off
runs of 2,000 actual callbacks produce the same audio hash in each scenario:
`58c15db6455c314d` dry, `cb66fd0304634970` board, `00a4cbc82359a24d` Sister.
Those whole-callback timings vary enough that they do not establish a reliable
overhead percentage. An isolated Release timing loop, including both clock reads
and the counter/window updates, measured 0.076–0.085 microseconds per callback
over five runs of one million calls on this Linux host. This excludes display
rendering and native backend processing and is not a Windows hardware guarantee.
