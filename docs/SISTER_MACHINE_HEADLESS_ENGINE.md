# Sister Machine PR3 headless engine

PR3 implements Kafka's DSP identity as a standalone, deterministic C engine. It is
compiled into `tapesister_core`, but it is not connected to `TsAudioBuses`, tiles,
Capture, MIDI, external input, monitoring, configuration, or SDL UI. That routing is
the responsibility of PR4.

## Machine and buffer contract

The machine has one continuously advancing `uint64_t` master clock, one write head,
and three playback heads. The write head is not a fourth playback head. All playback
heads read the same preallocated circular buffer before the current frame is written.

`TsSisterBuffer` stores either mono frames or interleaved stereo frames. Mono reads
return exact dual mono; stereo reads interpolate L/R independently at one shared
fractional position. The default duration is 40 seconds and the validated maximum is
120 seconds. Capacity is `ceil(sample_rate * seconds)`, so 40-second stereo storage is
about 13.5 MiB at 44.1 kHz, 14.6 MiB at 48 kHz, and 29.3 MiB at 96 kHz.

Initialization, destruction and reconfiguration allocate off the audio thread.
`ts_sister_machine_process_frame()` and `ts_sister_machine_process_block()` perform no
allocation, file access, logging, or locking. Reconfiguration allocates all replacement
storage before releasing the old storage; failure leaves the running engine untouched.
The current restart policy preserves musical parameters and Roll/Hold state but clears
rolling memory and resets all sample-rate-derived DSP state.

## Process order and taps

For every frame:

1. Read H1/H2/H3 from existing circular-buffer content.
2. Form raw H1 and H2 feedback returns and hard-clip each branch/channel to `[-1,1]`.
3. Add selected stereo input plus both feedback returns.
4. Reject non-finite values, apply a 5 Hz DC blocker and a bounded unit-slope soft
   saturator, then write when Roll is enabled and Hold is off.
5. Apply each head's modulation, decorrelation, Width and 50 ms level ramp.
6. Sum the three heads with fixed headroom; do not normalize by active-head count.
7. Apply the separately supplied Duck sidechain, global stereo biquad and one linked
   balance-preserving final safety gain.
8. Publish `H1`, `H2`, `H3`, `MIX`, positions, peaks and state.

`TsSisterOutput.head[0..2]` are post-level/post-character taps. H2/H3 include Drop;
all three include decorrelation and Width. `mix` is post-Duck, post-filter and
post-safety. The feedback pickup is deliberately raw, before level, Drop,
decorrelation, Width or final safety, matching Kafka's topology.

## Head behavior

- **H1 delay:** level `0..1`, time `0..4000 ms`, feedback `0..1`; effective delay is
  at least one frame. Small changes use a sample-rate-independent 20 ms smoothing law.
  Discontinuous changes use dual reads and a 15 ms crossfade rather than Doppler glide.
- **H2 full-buffer scrub:** level, normalized scrub and feedback are `0..1`. Scrub
  covers the complete buffer and ramps over 50 ms. Its signed phase preserves the
  selected write-clock relationship at rate `+1`.
- **H3 short span:** level and normalized span are `0..1`; the span covers at most
  eight seconds regardless of sample rate or backing-buffer duration. H3 has no
  feedback.

H2 and H3 use the exact rational rate table:

```text
-2, -4/3, -1, -2/3, -1/2, +1/2, +2/3, +1, +4/3, +2
```

All circular reads use positive wrapping and linear interpolation. A head is kept at
least one frame from the current write cell. Crossing the guarded write position starts
a 10 ms dual-read crossfade. The interpolation helper remains isolated so a later
Hermite option can be added without changing head ownership.

## Wow and Drop

Wow is one deterministic seeded random source shared by H2 and H3. A 10 Hz random
target is low-passed at 2 Hz. The `0..10` amount scales the same smooth drift for both
heads from zero to a maximum four-millisecond phase excursion. Zero is exact identity
and performs no random scheduling.

H2 and H3 have independent seeded Drop generators. Each schedules at a fixed 10 Hz,
matching Kafka's `metro 100`, creates a Box-Muller Gaussian target with mean `0.7` and
sigma `0.25`, clamps it to `0..1.5`, and reaches it with a five-millisecond linear
ramp. Drop `0` is exact unity and disables scheduling. Drop `0..100` linearly blends
between unity and the stochastic target. The ambiguous Max `!-~` metro connection is
not reproduced.

## Duck

Duck is stereo-linked and receives a separate sidechain frame, never the feedback-
inclusive write sum. `OFF` is exact unity.

- `SAFE` calculates stereo RMS, maps sensitivity `0..1` to a threshold from `-6 dB`
  to `-60 dB`, then applies a bounded threshold/envelope gain. Attack is 10 ms and
  recovery is 50 ms, retaining Kafka's faster-attenuation/slower-recovery character.
- `KAFKA_BIAS` explicitly adds `sensitivity * 75` to both RMS inputs before Kafka's
  `clamp(1-rms)` law. It is available for later listening comparisons but is not the
  safe default.

Silence and invalid sidechain values cannot create persistent non-finite state.

## Filter, stereo character and safety

The global filter uses identical validated coefficients with independent L/R state.
Types are bypass, low-pass, high-pass, band-pass, notch, peak, low shelf and high
shelf. Cutoff is constrained to `10 Hz .. 0.45 * sample_rate`, Q to `0.1..20`, and
gain to `-24..24 dB`. Coefficients ramp over 20 ms and invalid coefficients or state
recover to a neutral finite filter.

Kafka decorrelation is separate from buffer channel count. When enabled, each head's
left side stays direct while the right side passes through an approximately 2 kHz
one-pole low-pass and a sample-rate-derived 20 ms delay. Enable/bypass crossfades over
20 ms. Width is a non-destructive mid-to-field interpolation: `0` is centered dual
mono, `1` preserves the complete stored/decorrelated field, and intermediate values
use `mid + width * (channel - mid)`.

H1/H2 feedback branches retain Kafka's independent hard clipping. The combined write
adds finite sanitization, DC rejection and mild saturation. MIX uses fixed headroom and
a single linked gain when either channel exceeds unity, so protection never changes
stereo balance. An overload counter records invalid input, feedback/write overload,
filter recovery and final limiting.

## Hold, Clear and snapshots

Hold prevents writes but does not stop the master clock or playback heads. Resume
writes at the current moving clock position, not the position where Hold began. Roll
can independently disable writing without stopping playback.

Clear never scans the buffer in the processing function:

1. `ts_sister_machine_request_clear()` starts the configured output/feedback fade.
2. The engine enters `TS_SISTER_CLEAR_WAITING` and reports safe-to-clear.
3. The control thread pauses or locks processing and calls
   `ts_sister_machine_perform_clear()` to clear preallocated storage and reset state.
4. Processing resumes through a click-safe fade-in.

`ts_sister_machine_clear_offline()` executes the same handshake deterministically for
headless tools and tests.

Snapshots publish write/head positions, normalized markers, Roll/Hold/Clear state,
head and MIX peaks, Duck gain, Wow/Drop state, overload count, channel count, sample
rate, duration and a monotonic revision. Every published field is atomic; an odd/even
revision protocol prevents readers from accepting a mixed snapshot. UI code never
needs the live buffer or mutable DSP state.

## Defaults and PR4 boundary

Native defaults are: 40-second stereo buffer, Roll on, Hold off, H1 level `0.45`, time
`500 ms`, feedback `0.25`; H2/H3 level `0`, rate `+1`; H2 scrub `0.5`; H3 span `0.5`
(four seconds); Wow/Drop/Duck/decorrelation off; Width `1`; filter bypass; fixed
headroom `0.5`; and Clear `20 ms`. `ts_sister_parameters_kafka_start()` supplies the
verified Kafka rate/filter starting character without claiming absent ppooll preset
values as cold-start defaults.

PR4 must connect fixed TapeSister source buses and Capture taps without adding an
arbitrary routing graph. This PR does not add source masks, live bus routing, tile
Capture, monitoring, MIDI control, a second SDL window, logo behavior, presets,
persistence, live recirculation, linked-channel CDP, or TapeHead changes.
