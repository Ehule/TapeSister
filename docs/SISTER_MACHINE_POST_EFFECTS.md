# Sister Machine post-effects architecture

> Historical note: this document records the earlier fixed-chain implementation. The
> current instrument replaces that chain with four reorderable, independently placed,
> duplicate-capable Reverb/Delay/Distortion/Grain slots. See the
> [User Manual](USER_MANUAL.md#the-four-slot-fx-pedalboard) for current operation.

The earlier compact fixed chain was **DISTORTION → GRAIN → DELAY → REVERB**, with one
explicit Master FX Feedback return. Those effects shared PR8's generic target bits;
there was no second routing language and no per-head parameter copy.

This fixed order was the design bridge to the four-slot virtual pedalboard. The
then-proposed slot model could select and order four processors, including duplicates,
without patch cables; the bounded four-slot limit also makes timed topology
morphs practical on the X220/X230.

Each proposed slot had exactly one placement: **PRE** fresh Sister input, one or
more internal **H1/H2/H3** heads, or **POST** completed MIX. A single slot never
runs both pre and post. Using the same processor on both sides requires two
explicit duplicate slots, keeping CPU cost visible and bounded. PRE colors only
new source material before the rolling write; feedback already inside Sister is
not silently processed a second time.

## Target and ownership contract

Each effect owns one global control set and one independent mask:
`H1 | H2 | H3 | MIX`. H1/H2/H3 may be combined. Selecting a head clears MIX;
selecting MIX clears all heads; zero is bypass. The delay, reverb, and distortion
state bank for every processor has four stable instances (H1, H2, H3, MIX), so selected heads never
share mutable history. Target removal ramps the send/return down while time-based
state drains with zero input. A head↔MIX ownership change uses an exclusive
14 ms fade-out/dead handoff before the new group fades in, preventing double
application during the transition. State allocation happens on audio-device setup or
restart, never in the callback or on a target click.

```text
interpolated Hn frame
  → PR8 Soak/Bleed when Hn-targeted
  → Distortion → Grain → Delay → Reverb when Hn-targeted
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
         → dry/effected-chain blend by FX RET → post_fx → linked safety → MIX Capture
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

`FX RET` is one smoothed 0–200% linked-stereo effects-return gain. At every selected
head or MIX insertion it scales only the difference between the dry input and the
completed Distortion → Grain → Delay → Reverb output. It applies to ordinary POWER-off MIX
processing and Sister-active head/MIX processing. Its unity default is exact PR9
identity; zero is exact dry bypass while effect tail state continues advancing.

Head masks remain stored but dormant while POWER is off. Effects do not allocate
or start the rolling buffer. Master FX Feedback has no destination and its causal
state is forced to zero. Ordinary MIX tails continue because the stable MIX
processor remains alive across Sister window hide/show and POWER state.

## Reverb

The reverb is one continuous stereo space rather than four named simulations.
Its eight-line Householder-style FDN uses fractional reads, slow decorrelated
delay modulation, per-channel damping, bounded writes, cross-channel injection,
and independent stereo output vectors. SIZE scales the complete prime-spaced
network from close room reflections to an impossible deep field. The upper
quarter progressively opens an extreme region whose maximum physical scale is
twice the original deep-field maximum. SIZE changes crossfade old/new taps for
60 ms without clearing memory. MIX, SIZE, and DECAY also smooth over
approximately 24, 55, and 35 ms.

DECAY maps exponentially from **0.35 to 120 seconds**, with the added extension
blooming progressively in the upper quarter so ordinary preset positions retain
their established response. MIX 0 is exact dry identity and MIX 1 is fully wet.
The middle of the MIX law retains more of the immediate source than a
conventional equal-power insert crossfade, so adding a surrounding field does
not behave like lowering the channel fader. Modulation breaks up stationary
ringing without imposing an audible chorus cycle. Long states flush below
`1e-20` to zero, and linear-at-musical-level saturation keeps the feedback
network finite without prematurely shortening quiet tails.

## Delay

The delay is a four-head stereo tape echo. TIME maps logarithmically from
**8 ms to 2000 ms** and places the heads at asymmetric 0.29, 0.47, 0.71, and
1.00 multiples. Earlier heads are quieter and darker, while alternating
constant-power positions spread the pattern without relying on a fragile
channel offset. The final head feeds the record path through cross-channel tape
bleed, progressive high-frequency loss, gentle saturation, and bounded
conditioning. Subtle shared wow/flutter keeps stationary repeats alive.

TIME movement is continuous tape-speed motion rather than a pitch-preserving
tap crossfade. Sudden lengthening is limited to half-speed playback and sudden
shortening to double-speed playback, producing bounded octave-down and
octave-up gestures without discontinuous reads. Slow LFO movement follows as a
continuous pitch drift. FEEDBACK retains the 0–1.08 regenerative range: the top
can sustain and compress into a dub-like haze but cannot produce a non-finite
line. MIX 0 remains exact identity; feedback zero produces one four-head event.

## Grain

The granular processor is a deterministic stereo cloud with four performance
controls: **SIZE**, **DENS**, **PITCH**, and **MIX**, followed by the shared
post-MIX **GAIN** stage. SIZE maps logarithmically from **8 to 1000 ms**. DENS
maps logarithmically from **0.25 to 120 grains per second**. PITCH spans
**-24 to +24 semitones**, with exact unity at the center. Each target owns a
5.1-second stereo circular history and at most **24 simultaneous voices**.

Grains use fractional reads, randomized constant-power stereo positions, smooth
zero-valued squared-parabolic windows, and overlap-energy normalization. A small
deterministic start-time deviation breaks up rigid pulse trains while retaining
the requested average density. The target-local random sequence also varies read
position and pan reproducibly without collapsing H1/H2/H3/MIX into the same
stereo image.

At +24 semitones the history reserve safely covers the four-times read travel of
a one-second grain. If all 24 voices are occupied, a scheduled grain is skipped;
there is no allocation, stealing scan, or unbounded queue in the audio callback.
History is written continuously while the target is engaged, even at MIX 0, so
bringing the cloud into a performance reveals recent material rather than
starting from an empty buffer. Timed bypass shares the same 10 ms–60 minute
performance envelope as the other three effects and returns to exact dry/unity
when complete.

## Per-effect makeup gain

Reverb, Delay, Distortion, and Grain each provide a post-MIX **GAIN** control spanning
**-12 to +12 dB**, with exact 0 dB as the default. The gain stage follows that
effect's dry/wet blend and precedes the next processor, so it can compensate a
quiet effect, deliberately push the following processor, or trim a stacked
chain. Its linear multiplier smooths over approximately 20 ms; decibel
conversion occurs when controls are published rather than once per sample.

The makeup stage shares the effect's target and timed bypass envelope. As an
effect fades out, its multiplier converges to exact unity even when GAIN is not
0 dB. Consequently GAIN remains useful at MIX 0 while the effect is engaged,
but bypass never leaves a hidden level change behind. Presets and projects save
all four gains, and older files load them at 0 dB.

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

All four effect MIX controls use equal-power dry/wet gains between their exact
0% and 100% endpoints. This prevents a serial chain of moderate MIX settings
from repeatedly halving the direct component, which is especially important
when the whole summed MIX path is targeted.

## Master FX Feedback

The return is owned by the completed Sister `post_fx` tap, before final hardware
safety and after the full fixed chain:

```text
post_fx_chain[n] → FX RET → post_fx[n]
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

At 48 kHz the four delay instances reserve 3,087,424 bytes, the four reverbs
approximately 1.19 MB, and the four grain histories approximately 7.47 MiB
(about 11.55 MiB total). At 96 kHz storage approximately doubles. Sister-active
worst case advances four stable target instances so removed tails can drain;
ordinary POWER-off operation advances only MIX. Storage and time scale linearly
with sample rate. The topology uses eight FDN lines and two-channel scalar filters,
with no convolution, FFT, locks, logging, file I/O, or callback allocation.
Never-used zero-MIX delay/reverb instances and zero-MIX distortion take the
exact-bypass fast path; a time-based instance continues only after it has owned
real history so tails can drain safely.

## Persistence and compatibility

Preset schema version 10 and project schema version 11 add Grain enable, SIZE,
DENS, PITCH, MIX, GAIN, and target mask without saving live grain history. They
store every visible effect parameter and mask. Legacy Hall/Plate/Spring/Cathedral values map to continuous
SIZE positions; legacy state otherwise receives safe midrange DECAY/TIME/FEEDBACK,
MIX targets, and exact-zero Reverb/Delay/Distortion/Grain MIX and Master FX Feedback.
Opening a legacy file does not rewrite it. Invalid enums and values are clamped;
unknown fields are ignored; masks are restricted through the PR8 sanitizer.
