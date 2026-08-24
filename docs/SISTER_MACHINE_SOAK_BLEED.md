# Sister Machine PR8 Soak/Bleed stereo weave

## Mental model and DSP law

Soak/Bleed treats the two channels as wet tape strips that permeate one another.
It is not Width, autopan, chorus, ping-pong delay or a full-level additive
crossfeed. Each destination owns two bounded fractional-delay histories. For one
stereo frame:

```text
L' = L + (delay(R, d_RL) - L) * a_RL
R' = R + (delay(L, d_LR) - R) * a_LR
```

The complementary crossfades remain bounded by their source and delayed-source
endpoints. `SOAK` maps linearly from 0 to 1 and scales both transfer amounts:

```text
a_RL = SOAK * (0.78 + 0.22 * sin(phase + 90 degrees))
a_LR = SOAK * (0.78 + 0.22 * sin(phase + 150 degrees))
```

The unequal amounts and unequal delayed samples prevent a permanent collapse to
center. At full depth, both directions spend part of the trajectory near a delayed
swap. SOAK changes use a 20 ms one-pole approach. A settled zero is an exact branch
bypass; the delay calculation cannot leave phase residue in the audible frame.

`BLEED` is normalized 0..1 and maps logarithmically:

```text
rate_hz = 0.003 * (3.0 / 0.003) ^ BLEED
```

The endpoints are a 333.33-second cycle and a 0.333-second cycle. The legacy-safe
default is 25 percent, approximately 0.01687 Hz or 59.28 seconds per cycle. Rate
changes approach over 50 ms and never assign or restart phase.

The two cross-delay trajectories are sinusoids separated by 120 degrees. Both span
0.75..18.0 ms at every supported sample rate and use linear fractional interpolation
between adjacent circular-buffer samples. Storage includes guard frames, reads clamp
inside the allocated history and wrap by bounded modular indexing. This range produces
short migration and blur without becoming PR9's musical delay.

## Target representation and processor ownership

`TsSisterParameters::soak_targets` uses the generic effect bits `H1`, `H2`, `H3`
and `MIX`. `ts_sister_effect_targets_sanitize()` and
`ts_sister_effect_targets_toggle()` define the reusable policy:

- H1/H2/H3 are an independent multi-select group.
- Selecting any head removes MIX.
- Selecting MIX removes every head.
- Re-selecting the only active target can produce a safe zero-target bypass.
- Unknown bits are masked. A malformed combination containing MIX resolves to MIX.

There is one visible SOAK/BLEED parameter set and four preallocated processor states.
H1, H2 and H3 start at deterministic master phases 0, 120 and 240 degrees. MIX starts
at 180 degrees. Histories, write indices, smoothed rates and route fades are never
shared. Target changes set a 10 ms insertion fade and do not allocate, free or reset
phase. During that click-safe transition only, the outgoing and incoming insertion
gains may overlap; the published target mask remains exclusive.

This container is the PR9 seam: Reverb, Delay and Distortion can reuse the same target
validation and per-destination state ownership while retaining one global visible
parameter set per effect.

## Audited audio graph and insertion points

The callback constructs the named TILES, FM, EXT and AUDITION frames, sums only armed
Sister sources with `1/sqrt(source count)` normalization, then passes that discrete
stereo frame to the machine. The machine's actual order is:

```text
INPUT trim
  -> rolling-memory fresh-input branch
  -> H1/H2/H3 interpolated reads
       -> selected head Soak/Bleed
       -> H1/H2 established bounded feedback sends
       -> audible branch: H2/H3 Drop as established
          -> per-head Decor/Width -> per-head Level and clear envelope
  -> head sum -> Headroom -> Duck -> shared filter -> OUT
  -> selected MIX Soak/Bleed -> clear envelope -> linked final safety
```

The code applies a selected head weave immediately after its guarded interpolated
playback read. That woven read replaces the source used by the established H1 or H2
feedback send, then continues through the pre-existing Drop/Decor/Width/Level audible
path. This preserves feedback gain and head level meanings without applying either
twice. H3 has no pre-existing feedback send.

MIX weave runs after the shared filter and OUT and before clear gain and linked final
safety. It receives the completed wet three-head object and is never referenced by an
individual feedback send or the rolling-memory write equation. PR8 therefore adds no
MIX-to-write loop.

The rolling write equation remains:

```text
write = ghost_filter(old_cell) * (1 - ERASE)
      + trimmed_input
      + bounded_H1_feedback
      + bounded_H2_feedback
```

DC blocking and soft saturation remain authoritative on the written frame. Wow changes
read position; Drop remains on H2/H3; Duck, the shared filter and OUT remain post-sum.
The main callback removes every selected direct source bus, returns
`DRY * trimmed_input + WET * final_mix` only when MONITOR is enabled, and the main audio
mixer performs its established speaker gain, sanitization and per-channel clamp.

## Capture and channel contract

The published H1/H2/H3 taps are their audible post-Level/clear frames. A selected head
tap therefore includes its weave; an unselected tap remains discrete. The MIX tap is
the post-OUT, post-weave, linked-safe final wet result. MIX mode leaves all individual
head taps raw. Sister Capture records the selected published tap before DRY/WET monitor
levels. Stereo Capture stores L/R; mono Capture calls the established exact
`0.5 * (L + R)` fold. OUT continues to affect MIX Capture and never affects an
individual head Capture.

When rolling memory is explicitly mono, every weave insertion returns its input
unchanged before touching delay history. The engine's existing mono read is exact dual
mono, so SOAK, BLEED, target choice and delay contents cannot change it. The hardware
phase still advances while Sister is powered. Explicit stereo remains independently
addressable even when its current L and R values happen to match.

## Realtime, lifecycle and state

All four maximum 18 ms histories allocate on POWER or sample-rate reconfiguration and
free on POWER off. The callback performs no allocation, free, file I/O, logging or
blocking lock. POWER reset and successful device reconfiguration start clean histories;
parameter and target publication retain existing storage. Inputs, smoothed values,
phase, reads and outputs are finite-sanitized, and tiny smoothing state is flushed.

The master phase advances per processed frame regardless of window visibility,
MONITOR, SOAK, target mask, ROLL or HOLD. Hiding/showing the window and changing BLEED
never restart it. At SOAK zero the expensive delay/interpolation path is skipped while
phase and rate smoothing continue, keeping legacy projects inexpensive.

Preset and project formats are version 2. Both store SOAK, BLEED and the sanitized
target mask. They never store delay samples, write indices, phases, rolling audio,
voices, callback pointers or device state. Legacy/missing fields default to exact
SOAK zero, BLEED 25 percent and MIX. Recall uses the normal smoothed parameter and
route publication path; it does not clear tape, restart heads or stop notes.

## Compact UI

The 640x400 Sister window retains its top transport and bottom Capture/preset rows. The
waveform viewport is shortened slightly and the source/head/global rows move upward.
One compact final DSP row contains `SOAK`, `BLEED`, `H1`, `H2`, `H3`, `MIX` and the
`STEREO WEAVE` label. It reuses established Pattern Note/Effect/Tuning and stereo-wave
palette roles. The two sliders use the existing pointer, drag and guarded wheel
language; target boxes are binary clicks and do not take keyboard performance focus.

The cross-platform listening and hardware pass is recorded in
`SISTER_MACHINE_PR8_MANUAL_CHECKLIST.md`.
