# Sister Machine PR2 audio-bus contract

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
configuration-compatible default; S stores interleaved L/R. Overdub always records and
commits the base tile's shape. A mono layer becomes dual mono in a stereo result; a
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

The PR3 headless engine now exists as an allocation-free core module with its own
preallocated rolling buffer, three playback heads, H1/H2 feedback, Wow, Drop, Duck,
decorrelation, filter, taps and atomic snapshots. It deliberately has no live route to
these buses. PR4 may rely on independent L/R output, named program/capture/monitor
routes, shared-phase stereo sample voices, linked normalization, and the engine contract
documented in `SISTER_MACHINE_HEADLESS_ENGINE.md`; it must add only the fixed safe
routing graph and must not bypass either contract.

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
