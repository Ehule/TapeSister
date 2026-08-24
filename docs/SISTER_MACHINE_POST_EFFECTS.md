# Sister Machine PR9 post-effects architecture

PR9 adds one compact fixed chain—**DISTORTION → DELAY → REVERB**—and one
explicit Master FX Feedback return. The effects share PR8's generic target bits;
there is no second routing language and no per-head parameter copy.

## Target and ownership contract

Each effect owns one global control set and one independent mask:
`H1 | H2 | H3 | MIX`. H1/H2/H3 may be combined. Selecting a head clears MIX;
selecting MIX clears all heads; zero is bypass. The delay, reverb, and distortion
state bank has four stable instances (H1, H2, H3, MIX), so selected heads never
share mutable history. Target removal ramps the send/return down while time-based
state drains with zero input. A head↔MIX ownership change uses an exclusive
14 ms fade-out/dead handoff before the new group fades in, preventing double
application during the transition. State allocation happens on audio-device setup or
restart, never in the callback or on a target click.

```text
interpolated Hn frame
  → PR8 Soak/Bleed when Hn-targeted
  → Distortion → Delay → Reverb when Hn-targeted
  → established raw H1/H2 feedback send
  → Drop / Decor / Width / head level
  → Hn Capture and head sum
```

The ordering keeps PR8's successful weave placement unchanged. Head effects are
upstream of the existing H1/H2 rolling feedback sends, so their output can become
tape material without another hidden loop. H3 still has no new private feedback
control.

```text
head sum → headroom → Duck → Filter → OUT → PR8 MIX Soak/Bleed
         → Distortion → Delay → Reverb → post_fx → linked safety → MIX Capture
```

MIX effects never enter a head send. H1/H2/H3 Capture remains pre-MIX-effects;
MIX Capture contains the final protected post-effect result. OUT retains its PR8
meaning: it scales MIX before MIX Soak and PR9 effects, but does not scale head
Capture. Mono Capture remains `0.5 × (L + R)` and stereo Capture retains L/R.

## Ordinary POWER-off bus

The MIX instance is independent of rolling storage. With Sister POWER off, the
legacy ordinary contribution is reconstructed at its established point
(`clamp(preview + tiles + FM) × 0.8`, plus enabled EXT monitor), processed once,
then placed on the explicit `post_fx` bus. The direct components are removed for
that frame. Reference remains outside the musical effect chain. This preserves
exact legacy gain when all effect MIX controls are zero and prevents duplicates.

```text
AUDITION + TILES + FM --legacy clamp/0.8--+
enabled EXT monitor ----------------------+→ MIX FX instance → post_fx → output
reference ----------------------------------------------------------→ output
```

Head masks remain stored but dormant while POWER is off. Effects do not allocate
or start the rolling buffer. Master FX Feedback has no destination and its causal
state is forced to zero. Ordinary MIX tails continue because the stable MIX
processor remains alive across Sister window hide/show and POWER state.

## Reverb

The reverb is a stereo four-line Householder-style FDN. Each line uses fractional
linear reads, per-channel damping, bounded writes, and cross-channel injection.
Type changes crossfade old/new delay taps for 60 ms without clearing memory.
MIX and DECAY smooth over approximately 24 ms and 35 ms.

| Type | Line lengths at nominal tuning | Decay range | Character |
|---|---|---|---|
| Hall | 31.1, 43.7, 59.3, 71.9 ms | 0.45–8 s | balanced diffusion and damping |
| Plate | 19.7, 27.1, 37.9, 49.3 ms | 0.30–5 s | bright, dense, stronger stereo crossfeed |
| Spring | 13.1, 21.7, 34.9, 55.3 ms | 0.22–4 s | FDN plus bounded paired resonant dispersion |
| Cathedral | 43.1, 59.9, 78.7, 96.1 ms | 1.2–24 s | slow, dark, deep field |

DECAY is exponential within each type's range. MIX 0 is exact dry identity and
MIX 1 is fully wet. A bounded 2.25× return calibration prevents the complete
MIX target from collapsing behind unaffected head paths. Long states flush
below `1e-20` to zero.

## Delay

TIME maps logarithmically from **8 ms to 2000 ms**. L and R use the same time but
independent samples. Reads use linear fractional interpolation. A TIME change
starts a 25 ms dual-tap crossfade, so it is pitch-preserving rather than a tape
glide and is independent of callback block size. FEEDBACK maps to an internal
0–1.08 coefficient through unity-preserving bounded conditioning: ordinary
samples through 0.9 remain unchanged, while larger recursive values enter a
smooth knee below unity. The top can sustain and scream but cannot produce a
non-finite line. MIX 0 is exact identity and feedback zero produces one event.

## Distortion

The RAT-inspired digital chain is:

```text
linked controls → 1–60× drive → midpoint/current 2× nonlinear evaluation
→ asymmetric tanh shaping → 700 Hz–15.4 kHz tone low-pass
→ DC blocker → bounded output conditioning → wet/dry mix
```

The midpoint/current pair is a low-cost 2× antialiasing policy rather than an
exact analog emulation. L/R have independent filter and DC state. DRIVE, TONE,
MIX, and routing smooth over about 20/20/20/12 ms. MIX zero is exact identity.

All three effect MIX controls use equal-power dry/wet gains between their exact
0% and 100% endpoints. This prevents a serial chain of moderate MIX settings
from repeatedly halving the direct component, which is especially important
when the whole summed MIX path is targeted.

## Master FX Feedback

The return is owned by the completed Sister `post_fx` tap, before final hardware
safety and after the full fixed chain:

```text
post_fx[n] → smoothed 0–1.35 gain → linked ±1.5 conditioner → tanh
           → one-sample state z⁻¹ → write sum[n+1]

source → INPUT trim -----------------------+
retained Ghost/Erase cell -----------------+→ DC block/saturate → rolling write
existing H1/H2 feedback -------------------+
causal Master FX return (not INPUT-trimmed)-+
```

Zero clears the return and is mathematically inactive. POWER-off also clears it.
POWER-on always ramps from zero even when a project retains a nonzero setting.
The return is stereo linked for limiting, joins after source INPUT trim, and has
no block-size-dependent or zero-delay algebraic recursion.

## Realtime state and resource cost

UI/preset/project code owns requested parameters. The audio callback owns smooth
values, delay indices, FDN/filter histories, target ramps, and the previous-sample
master return. Published parameters are sanitized before entering the engine.
No live line samples, tails, pointers, rolling audio, or feedback audio are saved.

At 48 kHz the four delay instances reserve 3,087,424 bytes and the four reverbs
451,584 bytes (3.375 MiB total). At 96 kHz the total is 6.750 MiB. Sister-active
worst case advances four stable target instances so removed tails can drain;
ordinary POWER-off operation advances only MIX. Storage and time scale linearly
with sample rate. The topology uses four FDN lines and two-channel scalar filters,
with no convolution, FFT, locks, logging, file I/O, or callback allocation.
Never-used zero-MIX delay/reverb instances and zero-MIX distortion take the
exact-bypass fast path; a time-based instance continues only after it has owned
real history so tails can drain safely.

## Persistence and compatibility

Sister preset and project schemas are version 3. They store every visible PR9
parameter and mask. Legacy state receives Hall, safe midrange DECAY/TIME/FEEDBACK,
MIX targets, and exact-zero Reverb/Delay/Distortion MIX and Master FX Feedback.
Opening a legacy file does not rewrite it. Invalid enums and values are clamped;
unknown fields are ignored; masks are restricted through the PR8 sanitizer.
