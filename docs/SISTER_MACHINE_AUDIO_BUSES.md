# Sister Machine PR2 audio-bus contract

> PR9 extension: `post_fx` is now an explicit stereo contribution. With Sister
> active it is owned inside the Sister MIX result; with POWER off it replaces
> the once-only legacy program/EXT contribution before hardware output.

`main_sdl.c` is the single application and callback translation unit. Its callback
owns a `TsAudioMixer`, `TsNoteBank`, `TsPerformanceBank`, Capture recorder, external
monitor reference, and the associated group state. Runtime playback or recording is
not installed by textually including `main_sdl.c` or replacing function names.

## Named frame buses

Every callback route is a `TsStereoFrame`:

- `legacy_preview`: ordinary audition, mouse/playhead playback, Loop Lock, Record and
  Overdub tile playback, and transform/drone preview playback;
- `tile_performance`: QWERTY/MIDI sample voices and multi-tile performance voices;
- `fm`: mono-native FM voices, upmixed to exact dual mono;
- `external`: the explicitly selected input frame before monitor summing;
- `reference`: the mono-native tuning oscillator, upmixed to exact dual mono;
- `monitor`: the audible external-input route;
- `capture`: a non-audible tap selected independently of monitor state;
- `program`: preview + performance + FM after the legacy hard clamp; and
- `output`: program gain, optional monitor, reference, master gain, independent finite
  sanitization, and final per-channel hard clamp.

The order is deterministic and preserves the prior mono result: program sources sum,
program clamps, the established `0.8` program gain applies, monitor and reference are
added, then the final output clamps. Capture is never added merely because its tap is
populated.

Mono samples are exact dual mono. Stereo samples interpolate their channels
independently while sharing phase, rate, tuning, direction, loop transition, attack,
and voice lifetime. Voice normalization counts active voices once and applies the same
`1/sqrt(N)` gain to both channels. The raw multi-tile tap is left unnormalized for one
linked peak-safety pass after Capture.

## Channel policies

Internal Capture exposes **M/S** while arming. M stores `0.5 * (L + R)` and is the
configuration-compatible default; S stores interleaved L/R. The main-page and Sister
Machine buttons mirror this one shared M/S setting. Overdub always records and commits
the base tile's shape. A mono layer becomes dual mono in a stereo result; a
stereo layer folds explicitly when the base is mono. Base identity checks include
frame count, sample rate, channel count, and audio bytes. One peak gain is calculated
over the complete frame pair, preserving balance. Allocation happens before arming.

External input modes retain the numeric configuration mapping: 0 MIX, 1 LEFT, 2 RIGHT,
3 STEREO. MIX averages the bounded device frame, LEFT and RIGHT select one channel,
and all three produce mono recordings. STEREO preserves the first two channels and
produces stereo recordings. A one-channel device safely duplicates its channel in all
applicable modes; more than two channels are bounded, with MIX averaging the bounded
set. Monitoring never changes recorder format, and mode changes are refused while a
take is armed or recording. WAV archive/export preserves recorded channel count.

## Retained wrappers

The five historical `.inc` fragments remain only as source-size partitions for the
ordinary implementations included after `main()`. The preamble retains wrappers for:

- SDL open/close/lock/unlock/pause/quit, to keep logical device handles stable during
  configured device replacement;
- config load/save, to retain device catalog and immediate device application; and
- extended config/UI rendering and hit testing.

These wrappers do not replace note, performance, Capture, Overdub, external-recorder,
or mixer behavior. `main_sdl_audio.c` is an unused compatibility marker and does not
include or duplicate the callback.

## Remaining stereo blocks and headless-engine boundary

Linked-channel WARP, SMEAR, TEAR, tape Move/Copy placement, Drone, Vary/Create, FM
application/stamping, curated DSP, and CDP remain assigned to later phases where PR1
already blocks them. This PR does not implement Kafka, any rolling/write/playback head,
feedback, Wow/Drop/Duck, decorrelation, Sister masks/taps/windows/presets/routing, live
recirculation, linked-channel CDP, or TapeHead changes.

The PR3 headless engine exists as an allocation-free core module with its own
preallocated rolling buffer, three playback heads, H1/H2 feedback, Wow, Drop, Duck,
decorrelation, filter, taps and atomic snapshots. PR4 connects it through a fixed,
disabled-by-default route using this document's independent L/R buses, shared-phase
sample voices and linked normalization. Sister Capture reuses the existing protected
transaction rather than bypassing it. See `SISTER_MACHINE_LIVE_ROUTING.md` for source
switches, masks, tap definitions, lifecycle and PR5's controller boundary.

PR8 adds the Soak/Bleed stereo weave and the reusable H1/H2/H3/MIX target seam.
Head weaving is after the guarded interpolated read and before that head's established
feedback source and audible Drop/Decor/Width/Level path. MIX weaving is post-filter and
post-OUT, immediately before linked safety, and has no return to rolling memory. Capture
continues to consume the published taps. See `SISTER_MACHINE_SOAK_BLEED.md` for the
complete audited order, mappings and mono contract.

## Current Global Router boundary

`TsRouter` traverses a validated permutation of stable processor IDs. The
runtime adapter supplies Prism, Sister, Fallout, Pedalboard and External Insert. Source
selection, voice ownership and pre-FX recording taps remain upstream. The
ordinary entry receives the existing normalized/clamped program plus monitored
input; powered Sister retains its existing source switches and trim normalization.

Sister owns PRE/head inserts, tape operations and isolated head taps. Its completed
wet output becomes the serial stage result; its DRY monitor normally joins at the
fixed Master endpoint. A configured downstream Insert uses the router's generic
pre-stage boundary callback to merge DRY/WET before the bypass crossfade. This
keeps a serial external return from acquiring a parallel undelayed dry path.
The merge remains while configured Insert is bypassed. Pedalboard's global POST chain is a single movable stage.
Router bypass also gates PRE/head insert returns without changing their slot
settings. Master FX retains its existing independent gate over Pedalboard/Fallout.
Powered Sister's linked MIX safety remains at the end of the complete routed wet
chain, before MIX capture and monitor gains, including when its Router block is
bypassed. It does not clip the Sister contribution before downstream processors.

The current sample snapshots both feedback returns before traversal. FX feedback
observes the wet Pedalboard output if encountered after Sister, otherwise Sister's
wet output (upstream effects have already reached the tape input). Dry monitoring
cannot create immediate recursion. With a downstream Insert, FX/Fallout feedback
observations stop at the monitor merge; the external return is not implicitly
recirculated. Fallout feedback retains its bounded previous-frame state. No graph
feedback edge can be created by dragging.

Order handoff fades out for 5 ms, swaps the permutation at zero, then fades in for
5 ms. Bypass/solo ramp each stage's insert mix over 10 ms; inactive processors tick
on silence. No duplicate DSP graph, allocation, locks or UI polling is added to the
callback. Activity envelopes publish through the existing once-per-block atomic
snapshot. EQ, limiter, OUT and FILE OUT remain at the final endpoint.

`TsRouterControls` contains order, bypass mask and single solo ID. The manual
editing APIs do not depend on the UI. Project-state v24 and INI Router/Insert keys
persist that state. Older four-stage permutations append a bypassed, unassigned
Insert. Sound presets do not own it. Future processors need a stable ID, adapter,
UI metadata and persistence migration. Timed routing remains a later phase.

The physical Master callback writes stereo Master on stream channels 1/2 and
zeroes unused channels. Shared-device SEND uses a spare pair in that callback.
A separate named SEND receives stereo frames through its own `TsInputMonitor`
SPSC FIFO; its callback resamples into the selected local pair and zeroes all
others. Internal DSP and recording taps remain stereo. Native SDL negotiation
addresses the first eight channels of negotiated layouts without hidden SEND downmix onto Master.

Auxiliary SEND/RETURN endpoints use `TsAudioEndpoint` lifecycle state on the
control thread. Master/device/rate changes join auxiliary callbacks before FIFO
reconfiguration. Hotplug/APPLY retries explicit names; failures never substitute
another endpoint. A separate SEND matching Master is rejected. Shared SEND stays
muted during temporary Master fallback. Device names belong to global CFG, while
project v24 owns Insert pairs, levels and Router state.

RETURN may share the ordinary capture stream or use a named independent capture
callback. An identical named CFG/RETURN endpoint reuses shared capture. Only the
selected producer publishes to the return FIFO; independent RETURN leaves the
ordinary input and EXT source intact. Shared RETURN is reserved from ordinary
EXT selection, with playback gated until queued pre-reservation samples are
flushed under device locks. Raw EXT recording remains an earlier source tap.

Both FIFOs reuse stereo rate conversion and clock-drift correction. Separate
SEND and RETURN target twice the larger producer/consumer callback duration,
converted to producer-rate frames (bounded 128–8192). The Master callback
publishes its actual burst size; RETURN tracks capture bursts, and the SEND
callback accounts for its own obtained rate and actual frame count. This avoids
repeated starvation when capture blocks are smaller than playback blocks.
The Insert panel reports applied stream rates/buffers, queue targets and
underrun/overflow counters; `--diagnostic-audio` logs both FIFO diagnostics. Return
port-generation acknowledgement precedes consumer discard. SEND's consumer
similarly discards old queued frames on reassignment. FIFO index resets require
their producer and consumer callbacks excluded; no callback allocates or opens
devices. Playback may continue during atomic return-layout invalidation.

Insert bypass/solo uses the same controls as every macro stage. Gain changes slew
over 10 ms; send/return are finite-sanitized and linked peak-bounded. An active
missing path emits silence. No callback queries devices, opens files, allocates,
or waits for external processing. See [External Insert](USER_MANUAL.md#external-insert)
for recording taps and host limitations.

The following PR9/PR10 sections describe the earlier fixed positions.

## PR9 post-effects bus

`post_fx` is derived from a named musical branch, never by mutating the hardware
buffer. Reference is outside it. Sister-active Master FX Feedback taps the
pre-safety Sister post-effect frame only; it cannot recirculate unrelated direct
sources. See `SISTER_MACHINE_POST_EFFECTS.md` for the complete diagrams.

## PR10 logical rolling canvas

PR10 changes no named-bus owner or tap. The Sister write/read branch uses a 5–60-second
logical age window over its preallocated maximum store; `post_fx`, Master FX Feedback,
DRY/WET, H1/H2/H3/MIX Capture, ordinary POWER-off effects, and hardware output remain
at their PR9 positions. See `SISTER_MACHINE_LIVE_BUFFER_CANVAS.md`.

## Compact input and return mixer

Sister-active eligible sources pass through independent smoothed `TILES`, `FM`,
`EXT`, and `AUDITION` trims (0–400%) before the established multi-source
`1/sqrt(source_count)` normalization. The existing `INPUT` control remains the
0–200% master after that normalized sum and before the rolling write/Duck detector.
Thus balancing a quiet interface against loud tiles does not alter source ownership,
duplicate a bus, or change the normalization law.

The 0–200% `FX RET` trim follows the complete fixed post-effect chain and precedes
linked output safety. It is also the explicit level presented to Master FX Feedback.
With POWER off it scales the ordinary `post_fx` contribution; it never allocates or
opens rolling storage. All five controls default to exact unity and smooth over 20 ms.

## Manual Windows/Linux validation

- Confirm a mono project remains centered and unchanged.
- Audition a stereo WAV with unmistakable left/right content.
- Confirm QWERTY, MIDI, multi-tile groups, Loop Lock, Record, and Overdub preserve it.
- Confirm Capture M creates mono and Capture S creates stereo.
- Confirm mono and stereo Overdub, including mono-layer-to-stereo, preserve balance.
- Monitor and record external MIX, LEFT, RIGHT, and STEREO; verify MOTU channel order.
- Switch input and output devices and confirm no crash, stale tape, or stuck note.
- Confirm FM and the reference tone remain centered.
- Listen for new clicks at note starts and loop boundaries.

No SDL hardware validation is implied by the headless test suite.

Native Linux JACK exposes four independent application endpoints; see [JACK_AUDIO.md](JACK_AUDIO.md). Its deadline callback only copies bounded blocks and wakes an SDL worker. Existing DSP/UI exclusion stays on that worker, so UI access cannot block JACK. Insert consumers trim stale backlogs above twice their target with a 32-sample crossfade.
