# Sister Machine PR6 performance sources and musical state

PR6 separates Sister's interface, audio runtime, portable sound design and project
participation. It preserves the PR5 insert graph and live stereo character while removing
the requirement to keep another window or monitor path active.

## Ownership boundaries

- Window visibility is UI-only. The logo shows/restores/focuses the window. Close and
  Escape hide it. None of these operations change POWER, ROLL, HOLD, MONITOR, sources,
  parameters, voices, Capture or rolling memory.
- POWER is the rolling-buffer allocation/free boundary. CLEAR is the only deliberate
  in-session tape-memory destruction control.
- Named presets are portable sonic designs. They omit transport, routing, masks, devices,
  voices, Capture state, window state and tape contents.
- Project state owns TILES/FM/EXT/AUDITION switches, every page's 16-bit Sister mask, the
  selected page and a complete sanitized Sister sound snapshot.
- Global configuration owns device/buffer defaults, display/palette choices, Capture
  defaults and window position.
- POWER, ROLL, HOLD, MONITOR, visibility, active voices, active Capture and live tape
  audio remain session/runtime-only.

## Complete performance sources

`TILES` is the selected page's performance group and remains its master insert/bypass.
PR7 removes the modal `SISTER SRC` control: Shift-click toggles any occupied Sample Bank
tile directly, while Shift-click on an empty tile preserves Copy. MIDI and QWERTY send one identical `TsNoteEvent`
to every valid member. Each member keeps its own mono/stereo voice, tuning, velocity,
loop and release behavior. Mono is exact dual mono and stereo shares phase/pitch while
reading independent channels. The fixed pool preflights the complete group; if it cannot
fit all members, the entire trigger is rejected deterministically rather than partially
started. Repeated non-latched note-ons allocate overlapping groups.

`FM` consumes the live stereo FM bus. FM Logic renders and performs its patch even when
all tiles are empty, and window focus no longer stops FM voices. When routed, the ordinary
FM speaker path is removed and the bus enters Sister exactly once. A silent synth
contributes an exact zero frame.

`EXT` is an independent consumer of the configured capture device. A central ownership
bitmask represents Record monitoring, active external Record/Overdub and Sister EXT. The
device opens once for the first consumer and remains available until the last releases it.
Sister does not change the Record monitor toggle. MIX/LEFT/RIGHT/STEREO selection remains
the single authoritative channel mapping; RIGHT and STEREO reject a mono-only device
instead of pretending it has a second input. Device loss publishes `EXT - DEVICE
UNAVAILABLE` and silences only that route.

All routed sources retain the PR5 insert law: the selected direct speaker bus is removed,
the source enters INPUT/write once, and audibility returns only through Sister's MONITOR,
DRY and WET controls. MONITOR off never prevents rolling writes or Sister Capture.
`AUDITION` remains the separate canvas/waveform preview bus and is not required for TILES.
When POWER and TILES are on, ordinary occupied-tile clicks select the canvas without
creating a legacy preview voice; explicit AUDITION remains independently switchable.

## Ghost Tone

Ghost Tone applies only to material already stored beneath the write head:

```text
retained_old = ghost_lowpass(old_cell) * (1 - erase)
new_cell = retained_old + fresh_input + bounded_feedback
```

Fresh input is never filtered by Ghost Tone on its first write. Each later revolution
ages that material again, so high-frequency detail disappears faster than low-frequency
body. Zero is an exact branch bypass. The linked stereo amount maps exponentially from
the usable Nyquist-limited maximum (capped at 20 kHz) down to 250 Hz at 100 percent;
each channel has an independent one-pole history and there is no cross-coupling. Changes
ramp over 50 ms. CLEAR, free, reallocation and output/sample-rate restart reset both
histories. Existing DC blocking, soft saturation, feedback bounds and linked safety run
after the retained, fresh and feedback terms are combined.

Ghost Tone is not the shared post-head filter, Duck, ERASE or the excluded input-sensitive
SUPPRESS idea. ERASE controls how much old amplitude survives; Ghost Tone controls how
quickly the surviving spectrum darkens.

## Named presets

PR9 preset schema 3 additionally stores Reverb type/MIX/DECAY/mask, Delay
TIME/FEEDBACK/MIX/mask, Distortion DRIVE/TONE/MIX/mask, and Master FX Feedback.
Tail samples, delay samples, processor pointers, and feedback audio are never
stored. Recall publishes requested values into the existing smoothing and target
ramps; it does not clear tape, stop notes, or reset effect memory.

The compact bottom selector recalls previous/next presets; clicking its name opens a
dedicated manager overlay. The overlay supports Save As, confirmed overwrite, rename,
confirmed delete and cancel. Factory entries are immutable:

- `KAFKA START`
- `GHOST FIELD`
- `REVERSE MEMORY`

Preset files use `TapeSister Sister Presets`, a numeric `Version`, named sections and
explicit parameter keys in `sister-presets.ini` beside the established configuration
file. Saving uses temporary-file replacement. Missing fields inherit safe defaults,
unknown future keys are ignored, and non-finite/malformed values reject the load before
the active bank changes. Recall publishes sanitized parameters through the existing
runtime setter; it never allocates in the callback, clears tape, restarts heads, stops
voices or changes transport/routing.

PR8 advances the preset schema to version 2 and adds `soak`, `bleed` and
`soak_targets`. The target mask is part of the portable Soak/Bleed sound design, unlike
source switches and per-page tile masks. Recall smooths the two values and insertion
fades without serializing or restarting modulation phase.

H3 has level, span and one exact discrete rate. The PR5 `Q` placement was a labeling
ambiguity: it always controlled the shared post-head filter Q. PR6 labels it `FILTER Q`
and the preset/project schemas contain only that shared filter field—there is no H3 Q.

## Project format

PR9 project-state schema 3 stores the same musical PR9 fields. Older state loads
with all three effect MIX values and Master FX Feedback exactly zero and each
mask at MIX. Safe non-audible parameter defaults remain available for the first
intentional raise. Unknown future fields are ignored and masks reuse PR8
sanitization.

Each TSR collection gains an atomically replaced companion file:

```text
<project>.samples/sister-state.ini
```

The versioned text file contains page count/active page, source switches, one hexadecimal
mask per page, the selected preset name and a full parameter snapshot. It contains no
sample frames, tape cells, notes, phases, meters, handles, callbacks or synchronization
state. Legacy projects without the companion load with routes off and empty masks.
Malformed state is rejected; unknown future fields are ignored. Loading an EXT route
does not touch hardware while parsing. The shared device is requested only after the
project is valid and only if Sister is already powered.

PR8 advances the project companion to version 2 and round-trips the same Soak/Bleed
values and sanitized target mask. Version-1 and missing-field state remains audibly
identical: SOAK zero, BLEED 25 percent and MIX selected. Delay history and phase remain
runtime-only.

PR9 advances the schema to version 3 for post-effect controls and masks. PR10
advances it to version 4 and stores the requested 5–60-second `BufferSeconds` value.
Missing fields retain the 40-second identity default. Logical/valid frame counts,
discarded cells, head phase, resize ramps, and live audio remain runtime-only.

## Realtime contract

The callback remains allocation-, file-I/O-, logging- and blocking-lock-free. Input-device
opening, preset/project file operations, Capture allocation and maximum rolling-buffer allocation
remain on the controller/UI side. Callback loops are fixed by the existing voice, source,
head and channel limits. PR10 logical resize commits perform no allocation, free, lock,
or rolling-audio copy. UI waveform rendering continues to consume only lock-free fixed
snapshots and never mutable circular audio.

SUPPRESS, generic routing matrices, program-output recirculation, same-tile feedback,
auto-arming captured tiles, live tape serialization, linked-channel CDP, TapeHead changes
and hidden post-Sister effects remain excluded.
