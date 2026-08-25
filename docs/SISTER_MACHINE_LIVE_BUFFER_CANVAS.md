# Sister Machine PR10 live buffer canvas

PR10 separates the musical tape duration from its physical allocation. POWER
allocates one bounded 60-second mono or stereo store. `BUFFER` selects a logical
5–60-second age window inside that store while audio is running. Changing it does
not allocate, free, scan, or move rolling audio in the callback.

## Age-anchored resize law

“Now” is the moving write boundary; tape positions are defined by age behind it.

```text
now/write ── newest retained history ───────── oldest logical boundary
 age 0                 age t                         age BUFFER
```

- Grow keeps every existing frame at the same age and adds valid silence at the
  oldest side. The new region becomes real rolling history as the transport moves.
- Shrink keeps the newest requested duration and invalidates the older remainder.
  Regrowing cannot reveal those discarded cells: the valid-age horizon grows only
  behind newly advanced physical frames.
- H1/H2/H3 inside the retained window preserve age, fractional phase, rate, and
  direction. A cropped head is clamped to the oldest surviving frame and makes one
  linked-stereo 15 ms handoff from its prior read.
- H2 SCRUB and H3 SPAN remain normalized controls. H3 is still limited to eight
  seconds and is additionally clamped by a shorter canvas.

Requests are sanitized to 5–60 seconds. The UI publishes only the latest target;
the callback waits a fixed 25 ms coalescing interval, then performs an O(1) logical
commit. Rapid drag or wheel input therefore cannot queue obsolete resize work.

## Storage, recurrence, and continuity

```text
60 s fixed physical chronology (allocated at POWER/sample-rate setup)
        ↓ age-addressed fractional reads
5–60 s logical canvas + valid-history horizon
        ↓
H1/H2/H3, retained Ghost/Erase cell, write, overview snapshot
```

The moving physical chronology stores one frame per callback. When ROLL is off or
HOLD is on, a transport-only carry advances the unchanged logical tape cell; it is
not reported as a program write and does not publish a waveform event. This preserves
the established moving-head behavior without accepting input.

Erase/Ghost recurrence reads exactly one logical duration behind now. At a resize,
the old and new recurrence reads crossfade for 15 ms, after which only the new
boundary is authoritative. The write path retains its DC blocker, soft saturation,
bounded H1/H2 feedback, and causal Master FX Feedback state.

The 256-bin overview is remapped by age on a committed resize. Retained bins move to
their new normalized positions, cropped bins disappear, and a grown oldest region is
blank. This is a fixed-size summary remap, not a rolling-audio copy.

## DSP graph and state ownership

```text
age reads → PR8 weave → PR9 head FX → H1/H2 feedback → head taps/Capture
                                                     ↓
heads → sum/Duck/filter/OUT → MIX weave/FX → MIX Capture/output
                                      ↓ z^-1 Master FX Feedback
source/feedback + retained logical cell → bounded write at now
```

Callback-owned state includes the logical/current and pending frame counts, coalesce
counter, valid-history horizon, head phases/handoff ramps, recurrence handoff, write
clock, PR8 weave state, PR9 effect histories, and causal Master FX return. UI-owned
state includes the visible requested seconds and pointer/wheel state. Atomic snapshots
publish current seconds, target seconds, and pending status.

Resize does not reset ROLL, HOLD, CLEAR, notes, source masks, Capture, H1/H2 feedback,
Soak/Bleed phase/history, post-effect tails, or Master FX Feedback. CLEAR still uses
its existing fade/owner handshake. Device/sample-rate restart remains the documented
safe reset boundary: it allocates a replacement maximum store outside the callback,
preserves musical parameters and transport state, and clears live audio/history.

## Persistence, mono, and resource cost

Sister preset and project schemas are version 4 and store `buffer_seconds`. Missing
legacy fields default to 40 seconds. Live tape cells, valid-age state, head phases,
effect tails, and resize ramps are never serialized. Configuration remains the startup
default; preset/project recall is the musical live parameter and uses the same
coalesced transition.

Mono storage remains exact dual mono at every head and Capture contract. Stereo uses
one shared fractional position with independent L/R samples and linked crossfades.

The fixed rolling store uses:

| Rate | Mono | Stereo |
|---:|---:|---:|
| 44.1 kHz | 10.1 MiB | 20.2 MiB |
| 48 kHz | 11.0 MiB | 22.0 MiB |
| 96 kHz | 22.0 MiB | 43.9 MiB |

Two guard frames are included. Per-frame PR10 overhead is constant: age arithmetic,
three bounded head checks, and one physical frame store. A committed resize adds one
fixed 256-bin overview remap; no work scales with the number of audio frames retained.

