# Sister Machine PR4 live-routing contract

PR4 connects the headless three-head engine to TapeSister's named stereo buses through
`TsSisterRuntime`. The route is compiled and callback-valid, but the runtime is disabled
by default and has no user interface. PR5 owns the launcher, second window and controls.

## Fixed graph and callback order

The router is deliberately not a patch matrix. Its only input switches are `TILES`,
`FM`, `EXT` and `PREVIEW` (labeled `AUDITION` in the window); its only capture taps are
`MIX`, `H1`, `H2` and `H3`.
There is no `MIX -> input` switch.

Each callback frame follows this order:

1. Render the existing preview and ordinary tile-performance buses.
2. Render FM and obtain one selected external-input frame.
3. Render the dedicated Sister tile-performance bank.
4. Add only armed `TILES`, `FM`, `EXT` and `PREVIEW` frames.
5. Apply one stereo-linked `1/sqrt(N)` gain for the number of armed source buses.
6. Copy that compensated non-feedback input to the Duck sidechain.
7. Evaluate `TsSisterMachine` exactly once.
8. Publish `H1`, `H2`, `H3` and `MIX`.
9. Write the selected tap to the preallocated Sister recorder when recording.
10. Remove each armed source from its ordinary direct speaker bus.
11. Put `DRY * trimmed input + WET * MIX` on the named `sister` output bus only when
    Monitor is enabled.
12. Let the existing mixer add unrouted program, optional Sister return, external monitor
    and reference tone before its established final clamp.

Capture, Roll, Hold and feedback do not depend on Monitor. Disabling one input switch
removes only that bus. A routed source is removed from its ordinary audible bus and its
post-INPUT dry plus processed MIX return together through Sister when Monitor is on.
Unrouted sources remain independently audible. A disabled runtime returns exact zero
taps, does not mute ordinary sources and does not allocate its rolling buffer.

## Source and gain contract

Every route is a `TsStereoFrame`. Mono FM and mono tiles enter as exact dual mono;
stereo tiles, external STEREO and stereo previews preserve independent channels. Each
source retains its existing internal voice normalization. The Sister-only performance
bank therefore applies TapeSister's established linked `1/sqrt(voice_count)` once,
and the router does not normalize it again by selected tile count.

When multiple source switches are armed, one additional `1/sqrt(armed_bus_count)` gain
applies to the complete source sum. Armed switches, not instantaneous silence or zero
crossings, determine the gain. Both channels receive the same gain. PR3's bounded write
safety remains the only continuous rolling-buffer protection.

The compensated source sum passes through one smoothed 0-200 percent pre-tape INPUT
trim. That trimmed frame is the rolling-memory input, the Sister DRY return and the Duck
sidechain. It never contains H1/H2 feedback or a previous Sister MIX frame.

## Persistent tile source masks

`TsSisterRuntime` owns one 16-bit mask per Sample page, up to the existing 1024-page
project bound. Masks survive focus changes and page navigation for the lifetime of the
open runtime, but are not saved in TSR projects or presets. Project close/load clears
all masks. Record Bank visibility does not redefine a Sample-page mask.

Controller operations can set, toggle, clear, query and validate the current mask.
Blank, deleted or invalid samples are removed. Locked/protected tiles remain valid
sources. Before a source sample is replaced, its dedicated voices are stopped under
the audio-device lock; the retained mask refers to the replacement only on a later
note event. Page switching releases Sister voices before changing the active mask.

## Sister performance voices

Sister uses a dedicated `TsPerformanceBank`. It does not borrow Capture's transient
group, the ordinary dry bank or UI focus state. QWERTY and MIDI both enter through
`TsNoteEvent`; one event fans out once to every valid masked tile. Tuning, C4 unity,
velocity gain, loops, channel shape, attack, release and deterministic capacity remain
owned by the existing performance implementation. One-shot Note Off follows ordinary
TapeSister audition and lets the sample finish; looped Note Off stops the loop. Channel
panic, global panic, page changes and project close stop voices immediately.

This separation permits one gesture to feed Sister without accidentally adding the
ordinary performance bus to rolling input a second time. PR5 removes the selected
source from the ordinary speaker bus completely; Sister's DRY control rebuilds the
trimmed input return inside the MONITOR-gated output. Unrouted sources keep their
ordinary TapeSister monitoring.

## Taps and monitoring

- `H1`: after H1 level and optional decorrelation, before global Duck/filter/safety.
- `H2`: after H2 level, Drop and optional decorrelation, before global processing.
- `H3`: after H3 level, Drop and optional decorrelation, before global processing.
- `MIX`: after head sum, Duck, global filter, protected output gain and linked final safety.

All taps are finite stereo frames, are valid with Monitor off, and become zero when the
runtime is disabled or failed silent. Monitor copies `DRY * trimmed input + WET * MIX`
to `TsAudioBuses.sister`; it does not govern rolling or Capture. H1/H2/H3 remain before
the MIX output stage.

## Sister Capture and recursion

The router owns a normal `TsCaptureRecorder` and uses the existing preallocation,
callback-write and atomic commit functions. Capture selects one tap and an explicit M/S
shape. M stores `0.5 * (L + R)`; S stores interleaved stereo. Overdub always adopts the
base tile's channel shape and retains byte-for-byte base identity validation. Completed
stereo material receives one linked peak-safety gain only when required.

Capture provenance is recorded as `SISTER MIX`, `SISTER H1`, `SISTER H2` or `SISTER H3`.
The destination must be a blank silent, unlocked tile and cannot be in either the
Sister mask or Capture's transient source mask. Overdub requires an occupied unlocked
base and is blocked when that tile is a Sister source. No active sample buffer is
modified in place.

The destination helper accepts an explicit eligible target or searches the current
page for the nearest blank unlocked non-source tile. It never overwrites occupied
audio or silently creates a page.

Safe recursion is transactional:

1. Trigger masked source tiles into Sister.
2. Capture one tap into a different tile and complete the commit.
3. Explicitly add the new tile to the Sister mask.
4. Trigger it by QWERTY or MIDI as the next generation.

Captured tiles are not automatically armed as sources. Same-target live feedback and
generic MIX recirculation remain prohibited; H1/H2 are the only live feedback paths.

## Lifecycle and snapshots

Enablement validates a stereo output contract and allocates the configured buffer off
the callback. Disable occurs while the device is stopped or locked, hard-silences the
return, cancels Sister Capture, releases dedicated voices and frees engine storage.
No 40-second buffer exists while disabled.

Output restart reconfigures rate-derived DSP and clears rolling memory while preserving
parameters, source switches, Roll and Hold. Failure disables Sister and leaves ordinary
TapeSister operational. Input loss makes EXT silent and publishes a warning without
stopping tile/FM playback or feedback. Project close clears masks, voices, Capture and
rolling memory. Shutdown frees Sister only after audio callbacks stop.

Roll allows selected input and feedback writes. Roll off preserves memory and playback.
Hold takes precedence over Roll and suppresses writes while the clock and heads move.
Monitor affects only audibility.

The atomic routing snapshot adds enable/Roll/Hold/Monitor, switches, active-page mask,
Sister voice count, tap selection, Capture/destination state, input and tap peaks,
warnings, source-target conflict, overload count and processed-frame revision. PR5 can
draw this state without reading the rolling buffer, performance voices or recorder.

## PR5 controller requirements

PR5 must call these ordinary controller operations while respecting the existing SDL
device lock: enable/disable, Roll, Hold, Monitor, four source switches, per-page source
mask operations, parameter updates, tap/M-S selection, safe destination search,
Capture/Overdub arm/trigger/stop/cancel/commit, Clear and snapshot reads. Window
visibility must never own engine lifetime. PR5 may add the second window, logo launcher,
waveform/head display, controls and persistence; it must not add unrestricted routing
or zero-delay MIX recirculation.
