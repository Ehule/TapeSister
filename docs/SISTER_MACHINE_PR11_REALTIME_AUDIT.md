# Sister Machine PR11 realtime transition audit

This is the release-boundary audit for the machine merged through PR10. The
audited integration base is `ba55055` (`feature/sister-machine`); PR10 tip
`770dcd9` is an ancestor. “Controller” below means the SDL/UI thread. It mutates
engine topology only while the output device is locked. “Callback” means the
single audio owner of live samples, phases, histories, ramps, and prepared-buffer
publication.

## Transition inventory

| Transition | Command and owner | Callback transfer and continuity | Ownership/lifetime | Automated coverage and remaining risk |
|---|---|---|---|---|
| POWER on/off; device restart | Controller `enable`/`disable` under device lock | Parameters and transport state are retained; a 20 ms final-output topology crossfade bridges Sister and ordinary paths. A restart reinitializes rate-derived audio state. | Maximum tape and FX storage allocate/free outside the running callback. | Lifecycle, runtime, post-FX and pathological tests. Real device restart remains a hardware check. |
| ROLL | Controller setter | Write admission changes immediately (non-audible at the command sample); heads and clock preserve phase. Direct tile ownership now hands off over 20 ms. | No storage change; callback retains tape and DSP history. | Runtime, transport, continuity and pathological tests. |
| HOLD | Controller setter | Write is suppressed while clock/heads continue; no output topology change or phase reset. | Callback-owned tape remains live. | Transport, runtime, continuity and pathological tests. |
| CLEAR | Controller request, callback completion | Existing clear envelope fades the machine, clears bounded prepared state at its safe point, then fades back; no UI-side tape access. | Request/status handshake; callback owns live storage. | Buffer, runtime, resize and pathological tests. Manual listen for very hot tails. |
| Window open/hide/close/reopen and focus | Controller/UI model | Visibility never owns the engine and transfers no audio state. Focus only changes key dispatch; F1–F8 share the canonical octave. | Runtime outlives the window; snapshots are atomic copies. | Visibility, UI-model, source-UI, lifecycle and MIDI tests. Native window-manager behavior is manual. |
| TILES/FM/EXT/AUDITION source switches | Controller setter | Each insert ramps 0↔1 over 20 ms. The ordinary dry bus uses the exact complementary gain. Linked bus normalization follows route energy during the handoff. EXT availability uses the same ramp. | Fixed runtime ramps; no callback allocation or pointer replacement. | Routes, mixer, performance-source, input-ownership, pathological continuity/stress. |
| Program/output/reference/Capture routing | Fixed callback graph; Capture commands from controller | Program is assembled once, Sister/post-FX is returned once, final output is sanitized/clamped. Reference remains outside musical FX. Capture is an inaudible pre-monitor tap. Start/stop does not change speaker routing. | Named frame values only; recorder storage is preallocated and committed off-callback. | Audio-mixer, capture stereo/archive, Sister Capture/effect-routing tests. |
| Front `T/F/E/A/FX` and deep mixer | Controller publishes one `TsSisterParameters` object | Source trims and FX return use linked 20 ms ramps; both views read/write the same canonical state. | Callback owns ramp cursors; UI/project owns requested values. | Routes, UI-model, preset/project-state tests. Manual simultaneous-window manipulation. |
| Master INPUT, DRY/WET and MONITOR | Controller parameter/setter | INPUT is smoothed before tape. DRY/WET use 20 ms approaches. MONITOR insertion now ramps over 20 ms without affecting recording, heads, or Capture. | Scalar callback state only. | Runtime, routes, post-FX and pathological continuity tests. |
| Mono/left/right/stereo/mix external modes | Controller/input device | The negotiated 1-8 channel layout is folded at the device boundary. Mono is exact dual mono; L/R selection is explicit; STEREO averages odd/even hardware channels into L/R with unity gain for two channels. Mode/device absence cannot invalidate Sister and EXT fades to silence. | Input ring is preallocated; callback reads bounded frames and publishes one atomic activity mask. | External-channel, input-monitor/ownership, stereo/UI tests. MOTU profile selection remains manual. |
| MIDI/QWERTY note on/off, octave, panic, focus | Controller events into runtime | One event fans out to the selected immutable generations. Attack/release and loop-boundary replacement crossfade apply; one-shots intentionally finish. Panic clears bounded voice state. | Dedicated fixed-capacity performance bank; generations stay referenced until voices release. | MIDI, note-bank, performance, direct-source, recursion, lifecycle and pathological tests. Hardware MIDI/focus is manual. |
| One/many Sister tiles; live source membership | Controller mask/update | Group members start from the same event with fixed linked `1/sqrt(N)` normalization. Membership affects the next trigger; it does not retarget an active one-shot. | Per-page masks; immutable sample generations. | Source-mask, performance-source, recursion, project-state tests. |
| H1 enable/time/level/feedback | Parameter publish | Level/feedback are smoothed; time changes preserve the head’s private phase and use its private read handoff where a retarget is required. | H1 phase, old/new positions and feedback state are independent callback fields. | Heads, feedback, resize, H2-identity and pathological tests. Extreme feedback also manual. |
| H2 enable/scrub/rate/direction/feedback | Parameter publish | Scrub/rate are intentional tape gestures; bounded fractional traversal/wrap preserves H2 identity. Cropping uses H2’s own 15 ms handoff. Level/feedback smooth. | H2 owns phase, handoff and feedback state; no H1 alias. | Dedicated H2/H1 identity/crossing regression plus heads, resize, feedback and stress. |
| H3 enable/span/rate/direction | Parameter publish | Intentional motion is not globally smoothed; wrap is bounded. Span/crop retargets H3 only, with its private handoff. Level smooths. | H3 private phase/handoff; no feedback send. | Heads, modulation, resize and pathological tests. |
| Close positions, crossing, loop/wrap/reverse | Callback phase advance | Heads may legitimately cross. Every read is finite and mapped to the live logical canvas; UI marker collisions are offset only while published positions remain exact. | Per-head indexes and snapshot slots remain distinct. | New deterministic identity/crossing regression and stress seeds. Visual confirmation is manual. |
| H1/H2/H3/MIX effect masks | Controller parameter publish | Per-head changes use route fades; head↔MIX uses the existing exclusive 14 ms dead handoff. Removed tails drain with zero input. | Four stable preallocated instances per effect; histories never alias. | Effect-routing, post-FX, Soak/Bleed and pathological tests. |
| SOAK/BLEED | Controller parameter publish | SOAK approaches over 20 ms; BLEED rate over 50 ms without phase reset; target insertion fades 10 ms. Deliberate fast weave remains musical. | Four private preallocated delay histories. | Weave, stereo, effect-routing and pathological tests. |
| Wow, Drop, Duck, decorrelation, width, filter/tape controls | Controller parameter publish | Continuous gains/filter values use existing smoothing; modulation phase persists. Binary modes change bounded processing state without reallocating. Intentional Drop/tape gestures are not erased by global smoothing. | Callback-local scalar/filter histories. | Modulation, duck/filter, stereo, heads and stress. Final musical click judgment is manual. |
| Reverb type/decay/mix | Controller parameter publish | Type uses 60 ms old/new tap crossfade; decay/mix smooth over 35/24 ms; tiny tails flush. | Four stable FDN state banks allocated at device setup. | Post-FX, effect-routing and pathological tests. |
| Delay time/feedback/mix | Controller parameter publish | Time uses 25 ms dual-tap crossfade; mix/routing smooth; recursive values use bounded conditioning. | Four stable preallocated stereo lines. | Post-FX, feedback and pathological tests. |
| Distortion drive/tone/mix | Controller parameter publish | Controls smooth about 20 ms and route about 12 ms; intentional nonlinear edges are allowed. | Four independent fixed filter/state instances. | Post-FX, effect-routing and pathological tests. |
| Master FX feedback | Controller parameter publish | Gain approaches over 20 ms; one-sample causal return, linked limiter and `tanh` prevent non-finite escape. POWER off clears causal state; POWER on starts from zero. | One callback-owned previous frame; no zero-delay loop. | Feedback, post-FX and pathological self-oscillation stress. |
| BUFFER 5↔60 s, rapid resize, crop through heads | Controller publishes latest requested duration | 25 ms coalescing then O(1) age-anchored commit. Surviving ages persist; cropped heads and recurrence use 15 ms handoffs. | One maximum store prepared at POWER; no callback allocation/copy/free; current/pending generations remain valid. | Resize, canvas, heads, continuity and pathological alternating-resize tests. |
| Resize while Roll/Hold/feedback/Capture/source change/tails | Same request path | Resize does not reset transport, notes, Capture, effects, feedback, or source ramps; fixed overview remap is bounded. | Callback owns commit and retirement point. | Resize and pathological chain tests. Native long recording remains manual. |
| Capture M/S and Overdub arm/start/stop/commit | Controller arms preallocated recorder; callback writes | Capture is pre-monitor, so start/stop is inaudible. M folds `0.5(L+R)`; S preserves L/R. Commit validates destination and swaps immutable sample data off-callback. | Recorder capacity fixed while active; no live sample mutation. | Capture, capture-stereo/archive, recursion and project tests. |
| Edit/undo/redo/Warp/Smear/Tear replacement | Controller/editor | Active one-shots retain their generation; new notes use replacement; locked loops adopt at loop boundary with linked crossfade. | Immutable generation references and deferred release. | Editor-contract, smear, tear, transform, bank and performance tests. Long simultaneous editing is manual. |
| Preset/project/config load/save/legacy defaults | Controller/file path, never callback | Recall republishes normal parameter/resize transitions; live audio, phases and tails are never serialized. Schema-v5 canonical mixer values round-trip; old files get deterministic defaults. | Temporary-file replacement; loaded state copied into owned runtime structures. | Preset, project-state, config/audio-config, TSR and sample-pages tests. |
| Sample rate/callback size/device loss | Controller device lifecycle | Restart occurs with callback stopped/locked, preserves requested musical state, rebuilds rate-sized storage, and clears incompatible live history. Diagnostics publish callback timing without callback logging. | Device owns callback lifetime; all rate-sized memory prepared outside it. | 44.1/48/96 kHz engine tests and optional 128/256/512/1024 benchmark. SDL/hardware switch remains manual. |

## Post-PR11 head-motion and wheel follow-up

The final listening pass on a ThinkPad X230 exposed transition restarts that
the original finite-state and broad discontinuity tests did not measure tightly
enough:

- H2/H3 normal Rate traversal could cross the newest/oldest live-canvas seam
  without arming the write-boundary handoff. Scrub gestures near zero were
  guarded, but ordinary fractional and fast playback wraps were not. Both
  directions now anticipate the seam and carry the last audible stereo frame
  into the established 10 ms landing handoff.
- A second H1 Time command received during its 15 ms dual-tap handoff restarted
  from the prior target tap rather than the sample actually being heard. Rapid
  drags and coarse wheel steps now restart from the last audible H1 frame.
- Delay Time and Reverb Type had the same interrupted-transition shape under
  fast wheel input. Their 25 ms and 60 ms read handoffs now retain the last
  audible wet frame when a new target arrives before completion.
- Sister wheel steps remain intentionally coarse at 5% (Shift: 1%), while rate,
  filter type, reverb type, and buffer duration use their discrete domains.
  Parameter transfer stays under the SDL audio-device lock, but preset-label
  lookup, formatting, and config mirroring now happen after unlock so a burst of
  wheel events cannot enlarge the callback exclusion window with UI work.

The dedicated head regression now distinguishes a pure H1/H2 crossing from a
canvas seam: an interior head-to-head crossing must remain continuous, while a
deliberately discontinuous 0.98-amplitude loop seam must be distributed across
the landing handoff. Separate hostile tests interrupt H1, Delay, and Reverb
handoffs every few samples. These are continuity assertions rather than only
finite/bounded-state checks.

## Fallout wheel and modulation-bank follow-up

Fallout's event engines already protected their own intentional changes: insert
engage/disengage, preset recall, skip relocation, pitch motion, drop, pan, and
RISE wrap all had bounded ramps or handoffs. The remaining controller edge was
outside those event transitions. A wheel update replaced the continuous panel
centers immediately, and LFO/RISE target membership was binary. Adding FEEDBACK
halfway through a 60-minute shared RISE therefore applied the entire current
half-rise on one sample even though the shared phase itself never jumped.

All continuous Fallout panel values now chase their published targets over 20
ms and restart an interrupted chase from the value actually used by the audio
thread. Each of the 13 LFO and RISE destinations owns an independent 20 ms
membership blend. Adding a destination fades it from its saved panel center to
the current shared modulation value without resetting the LFO or RISE clock;
removing it performs the inverse fade. A destination added after a completed
one-shot re-arms that one-shot, while additions to a running one-shot or SAW
only catch up to the existing phase. The explicit retrigger control remains the
manual shared-clock restart.

Discrete noise-type and DROP/PAN/SKIP/BIT/PITCH gate edits carry the exact last
audible output and wet-feedback frames into a 10 ms handoff, including repeated
edits before an earlier handoff completes. Pitch ratios retain their musical
quantization but use a 10 ms minimum tape-speed ramp. Fallout toggle UI work is
also completed after releasing the SDL audio-device lock.

The dedicated regression simulates a target joining a half-complete one-hour
RISE, insertion and removal of all 13 destinations, per-sample alternating
wheel targets during simultaneous LFO/RISE modulation, completed one-shot
insertion, explicit retrigger, and rapid discrete toggle edges. It asserts
phase preservation, exact target-blend progress, bounded continuous-control
motion, finite output, and first-frame identity at every discrete handoff.
The maximum-load callback benchmark now enables Fallout as well as the three
post effects, with every Fallout gate and all 13 destinations active at the
fastest LFO/RISE/event settings; realtime diagnostics publish a dedicated
Fallout configuration bit so that load is visible rather than implied.

## Callback and storage findings

The active sample path contains no allocation, reallocation, free, filesystem,
console, configuration, UI call, blocking wait, or mutex. Its loops are bounded by
fixed head/effect counts, fixed waveform bins on the rare resize commit, or the SDL
callback frame count. Live canvas changes publish scalar targets into one maximum
allocation; no rolling audio is copied. Sample voices retain immutable generations,
so replacement cannot leave a retired pointer in an active voice.

The optional `--diagnostic-audio` mode takes one SDL performance-counter pair per
callback and updates fixed atomics after confirming they are lock-free on the target;
otherwise the optional mode disables itself. Formatting and five-second reporting
happen on the controller thread. It records callback count, frames, average/worst time,
near-deadlines, overruns, sample rate, device frames, and an active configuration
bitset. EXT diagnostics additionally expose the monitor generation, capture callback
and frame counts, largest delivered block, ring faults, occupancy, and adaptive clock
correction. Normal mode avoids callback clocks and realtime timing diagnostics; the
EXT bridge retains only its bounded lock-free SPSC and control atomics.

Finite sanitization and bounded feedback remain at external buses, tape write,
head/mix taps, and final output. Delay/reverb tails flush below their tiny-state
thresholds. Frame capacities and sample-rate-derived allocations are validated before
publication. No new blanket amplitude clamp was introduced. EXT deliberately holds
four negotiated capture blocks so delayed PipeWire/PulseAudio deliveries cannot force
repeated silence/re-prime gaps. Its bounded adaptive ratio uses linked-stereo linear
interpolation, so correction does not introduce raw sample skips or holds; the selected
256/512/1024 device size makes the reserve explicit and testable.

## H2/H3 position investigation

The video-confirmed H2 jump was a real DSP read relocation also reported faithfully by
the UI. H2 and H3 converted their fixed 60-second physical-store phase to a signed age
at the store's 30-second midpoint, then wrapped that value by the independently selected
5–60-second logical buffer. For a 46-second buffer, crossing 30 seconds therefore
changed the equivalent physical age by 60 seconds and remapped it to a different
interior position modulo 46 seconds. H3 shared the same conversion and was affected.
H1 and the write marker do not use this free-head conversion.

H2/H3 now own an authoritative logical age in the active musical buffer; their physical
store phase is derived from it after each write-clock advance. Scrub/Span ramps alter
that logical age, Rate advances it independently per head, and the existing guarded
read/handoff remains unchanged. Atomic snapshots still derive their normalized values
from the exact guarded audio read position, and the waveform marker remains a linear
mapping of that normalized value. A deterministic 46-second regression records the old
`16001 -> 30001` interior jump (14,000 frames at a -2-frame expected increment) and
requires `16001 -> 15999` after the fix. Multi-duration tests cover two complete
traversals, reverse/slow/unity/fast rates, mono/stereo, muted/audible heads, marker/audio
identity, edge wraps, live Scrub/Span ramps, HOLD, and ROLL.

The earlier display-collision correction remains valid for exact marker overlaps: it
only gives coincident colors small symmetric pixel offsets and never changes DSP or
snapshot positions.

## Certified gain order

| Stage | Gain/normalization rule |
|---|---|
| Source voice | Per-voice channel-preserving gain; a marked tile ensemble uses one fixed linked `1/sqrt(tile count)` factor. |
| Source insert | `T/F/E/A` trim × 20 ms route ownership; active route energy gives one linked `1/sqrt(sum(route²))` factor only when that energy exceeds one. |
| Tape input | Smoothed master `INPUT`; this feeds fresh tape input, Sister DRY, and Duck sidechain. |
| Heads | Private read/FX/feedback path, then per-head level; H1/H2 feedback enters the next tape write once. |
| MIX | One head sum → headroom → Duck/filter → OUT → MIX Soak/post-FX. No second head normalization. |
| Return | Smoothed `FX RET` scales only each post-effect wet-minus-dry contribution; then Sister uses `DRY × input + WET × MIX`. MONITOR insertion ramps. Capture stays outside this audible return. |
| Speaker | Unrouted ordinary program + complementary direct handoff + Sister/post-FX + explicit EXT monitor + reference, followed by established master gain and per-channel safety. |

Unity defaults preserve the established route. Mono is dual mono, stereo never counts
channels as voices, and Capture is never added implicitly.
