# Mosaic live loops — stage 1

Live loops are audio cards on the existing Mosaic canvas. Ordinary audio and
TrackSister pattern cards keep their timeline playback. There is no second
canvas or LoopSister window. Built from main after master-clock PR #163 merged
(`d11b052285b93f5dcf63c732bb39e8345d53eb00`).

## First performance

1. Open Mosaic and click **LOOPS**, beside its transport, or press **Ctrl+L**.
2. Select **SRC: EXT INPUT**. The channel/pair comes from the existing recording
   input configuration. **MONITOR** controls listening, independently of capture.
3. Choose **SINGLE TAKE** or **CONTINUOUS ON**. Click LENGTH for presets, wheel
   for individual seconds/beats, or select MANUAL. Right-click LENGTH switches
   seconds/beats. Beat lengths require the master clock enabled in F11.
4. Press **RECORD**. Press the same button/pedal again to finish the layer.
   In continuous mode the next recording starts at that boundary. A fixed
   length does this automatically, including inside an input callback buffer.
5. **STOP CAP** finishes and keeps the current partial layer. **DROP PART** stops
   creation without making a card for that partial layer. Both leave earlier
   loops playing. Takes shorter than two frames cannot become audio cards.
6. **STOP LOOPS** releases all currently active/waiting live loops. It does not
   stop recording, held instrument notes, or the Mosaic timeline. Later timeline
   encounters can still launch stopped cards.

Each completed layer is a separate gold card, initially ongoing. Layers are
placed side by side at the session's initial timeline position, to the right
of cards already crossing that insertion line; the view reveals
the new card when Mosaic is open. Publication waits until an active card/pan/
fade gesture finishes, while capture and already-running loops continue. Move/resize them
with the normal Mosaic gestures. Select a card and use **LAUNCH**, **STOP**, or
**RESTART**. **MAKE LIVE** converts a selected audio card; **TO TIMELINE** restores
timeline ownership and stops that independent player. Pattern cards cannot be
converted in this stage; they still play alongside live audio loops.

Gold borders, LIVE STOP/WAIT/PLAY/END labels, and a moving cursor distinguish
live cards. The cursor shows the first voice's position in the source cycle,
independently of the main playhead. Muting/solo exclusion can make a playing
card inaudible without stopping its cursor. F11 reports active live loops even
when the Mosaic timeline is stopped; F12's setup report includes their count
and whether loop capture is active.

## Length, repetition and sound

| Setting | Meaning |
| --- | --- |
| Source cycle | The trimmed source audio, played forward repeatedly. |
| UNTIL STOP | Ongoing playback. The visible card length is a footprint; its end does not stop the player. |
| CARD ENDS | Stop after the visible card duration, measured from launch in the player's local tape time. Resizing changes this stop time. |
| Source/card readout | Shows both durations and the card/source ratio; e.g. 2 s source, 8 s card means four normal-rate cycles in CARD ENDS mode. Fractional final cycles are allowed. |
| IN | Fade in once after launch. The visible card's effective fade amount applies. |
| OUT | Fade at the finite stop boundary. Disabled for UNTIL STOP; its saved value remains available if CARD ENDS is restored. |
| Level, pan, mute, solo, Matrix route | Same controls and routing destinations as ordinary cards. Solo is shared across the entire Mosaic arrangement. |

Live cards use short source-edge fades, up to 2 ms, instead of an overlapping
splice that would shorten the period. Stop releases over 5 ms. Live mode uses
forward playback of the trim region; reverse/ping-pong/START and the editor's
overlap-crossfade setting remain ordinary-card controls in this stage. Notes
and tuning still affect sample pitch, and each pitched voice has its own edge
fade. The displayed source/card ratio describes the source at its original
pitch; transposition/chords can produce different individual repeat rates.
Existing ordinary-card playback is unchanged.

The main Mosaic speed and its timeline volume envelope do not advance or
shape independent players. The Mosaic bus gain/trim, per-card controls, and
later routing/effects still apply. This lets a timeline fade/pause leave an
independent loop running while retaining the existing output paths.

## Timeline and clock choices

| Event or control | Default / selected behavior |
| --- | --- |
| Timeline crosses a card start | Launch a stopped card from its beginning. A running or waiting card keeps its one existing player. |
| Timeline seeks/jumps over a start | No synthetic launch. Landing exactly on the start launches on the next playing frame. |
| Pause, timeline end, Home, cue jump or timeline repeat | Already-running independent loops continue. Stopped cards can launch at their next actual encounter. |
| Explicit RESTART | Start this player's cycle and finite duration again. |
| TEMPO: FREE | Own seconds clock, independent of Mosaic speed and master tempo. |
| TEMPO: MASTER TAPE | Scale tape rate using the saved source-cycle beat length and the current master quarter-note duration. Tempo changes preserve source progress and also change pitch. |
| GROUP: FOLLOW | Future F11 group Play launches the card; group Pause releases it. Next Play starts again. Independent of tempo following and the native players' group membership. |
| SEEK: RESTART | Mosaic seeks/cue/repeat wraps restart a running card. F11 master Home also restarts it. Does not start a stopped card or map arbitrary master positions into a loop. |
| LAUNCH NEXT BEAT | Manual/newly captured layers wait for a shared quarter-note edge. The master phase runs even with its transport cursor paused. Timeline encounters remain immediate. |

Changing a route flag does not itself launch, stop, or relocate a card. Master
off falls back to free rate; waiting launches proceed immediately. Re-enabling
tempo following changes rate without a phase snap. These are tape-rate controls,
not pitch-preserving stretch, bar quantization, or external MIDI clock.

Beat-count capture locks its period at capture start, carrying fractional frames
between complete takes to avoid cumulative rounding drift. Published full takes
retain the exact chosen integer beat length for playback. Tempo edits during
that capture do not move its input boundaries; resulting cards can subsequently
follow the changed master rate. Capture starts immediately; NEXT BEAT applies
to playback launch, not recording start. Initial beat length is derived from
the actual selected clock duration, including TrackSister's native clock.

## Capture, latency and limits

EXT captures the selected raw device channel/pair and excludes the internal
loop mix. DRY KEYS captures the existing dry note tap before later processing,
excluding Mosaic. OUTPUT deliberately captures the audible program, including
already-playing loops and later effects. The panel states this difference.
Record monitoring does not silently change the chosen capture source.

One writer archives a session WAV while the audio producer publishes contiguous
frame ranges. File allocation, I/O, range loading and waveform building are
outside the audio callback. Completed ranges become immutable sample sources
on the UI thread; the next range is already recording. Thus capture boundaries
have no re-arm gap, but **playback return is delayed by writer/UI preparation**
and possibly waiting for the next beat. This is not zero-latency overdubbing.
Long takes can briefly delay UI work while loading/building their waveforms.

The queue holds four seconds of audio and 32 pending layer descriptors. A take
is limited to 60 seconds; MANUAL reaches a visible stop at that safety limit.
Live import stops if total retained Mosaic source audio would exceed 128 MiB
(including ordinary sources and undo-retained audio), or the 1024-card capacity
is exhausted. Existing loaded/imported projects are not truncated to that
budget. Completed cards keep playing when capture creation stops at a limit.

The session WAV remains in **Captures** (or `TAPESISTER_CAPTURES`). **COPY WAV**
copies its path. It contains complete layers and the current/discarded partial;
the cards in the saved project hold separate source ranges. Queue/full-card or
import failure retains the archive for manual import/trim. A disk failure can
only retain the successfully written prefix. Headers are checkpointed at layer
boundaries and periodically; this is not a power-loss/fsync guarantee.

No input/output-device latency calibration, drift compensation between separate
hardware clocks, quantized capture arm/stop, seamless zero-delay return, or
per-pattern live players is promised by this stage. Those need a separate stage
and actual interface measurements. The usable stage here includes independent
playback, manual/automatic successive capture, MIDI control and persistence.

## MIDI Learn and persistence

Enable the usual MIDI Learn, open LOOPS, click RECORD/NEXT, STOP CAP, DROP PART,
STOP LOOPS, LAUNCH, STOP or RESTART, then press the controller. RECORD/NEXT is
one trigger: it arms from idle and advances while recording. Releases do not
advance unless explicitly learned as release triggers. A mapping continues to
work from other workspaces. With the panel closed, learning an individual live
card creates a stable card-ID toggle target. Selected-card controls instead
follow the current selection. Mappings use the existing application config;
card IDs belong to the currently opened project.

Project saves include loop roles, length/finiteness, independent route flags,
beat length, mix and source audio, plus capture settings. Opening a project
leaves live players stopped and capture idle. Saving does not interrupt current
playback. A save made during capture contains the already-published cards; stop
capture and wait for FINISHING to clear to include the current take. Closing or
changing project before publication still retains the raw session archive.
Undo preserves unaffected live players; deletion/role removal stops
the affected player, and undoing a deletion does not resurrect sound.

Older projects default to ordinary cards and unchanged transport. Saves without
live cards or changed loop settings retain Mosaic format 14. Live-enabled saves
use format 15, so old builds reject them instead of silently losing behavior.
Use a project copy when comparing with an older build.

See [validation and the hardware exercise](validation/mosaic-live-loops.md).
