# Mosaic live loops — stage 1

Live loops are audio cards on the existing Mosaic canvas. Ordinary audio and
TrackSister pattern cards keep their timeline playback. There is no second
canvas or LoopSister window. Built from main after master-clock PR #163 merged
(`d11b052285b93f5dcf63c732bb39e8345d53eb00`).

## First performance

1. Open Mosaic and click **LOOPS**, beside **SISTER** in the header, or press
   **Ctrl+L**. A compact performance panel leaves the source tiles and most of
   the canvas visible. **ADVANCED** exposes setup and individual card controls;
   **SIMPLE** returns to performance. Escape returns to Simple, then hides it.
   Drag the Simple panel's header to move it. Double-click the header to restore
   its right-side dock; Escape during a drag restores its starting position.
   Position is saved in the usual configuration and clamped inside the window.
2. In Advanced, select **SRC: EXT INPUT**. The channel/pair comes from the existing recording
   input configuration. **MONITOR** controls listening, independently of capture.
3. Choose **SINGLE TAKE** or **CONTINUOUS ON**. Click LENGTH for presets, wheel
   for individual seconds/beats, or select MANUAL. Right-click LENGTH switches
   seconds/beats. Beat lengths require the master clock enabled in F11.
4. In Simple, press the **big central button**: first press starts recording;
   each later press finishes the current take, launches its own loop card after
   preparation, and immediately records the next separate layer. This button
   always enables continuous creation, including when a loop card is selected.
   A fixed length advances automatically, including inside a callback buffer.
   Advanced's separate **RECORD** action respects SINGLE/CONTINUOUS instead.
5. **Pause / Stop Recording** finishes and keeps the current partial layer.
   Earlier loops keep playing while you wait as long as you want. REC starts
   another take later. **DROP PART** in Advanced stops
   creation without making a card for that partial layer. Both leave earlier
   loops playing. Takes shorter than two frames cannot become audio cards.
6. Simple's **Stop** finishes recording, preserves complete/partial takes, and
   stops all live-loop players. Pending cards are published stopped. Held notes,
   other instruments, and the main Mosaic timeline retain their transports.
   Later timeline encounters can still launch stopped cards. Advanced's
   **STOP LOOPS** and existing MIDI `stop_all` action keep their earlier
   loops-only behavior for compatibility.

The ring shows current-take progress and seconds remaining during fixed-length
capture. Manual capture shows elapsed seconds against the existing 60-second
guard. Between recordings it shows the selected live card's first voice/cycle
and estimated seconds left in that cycle (limited by a finite card's end).
Tempo-following tape rate and that voice's pitch are included. This is a UI
snapshot, not a hardware-latency countdown. The large red button always records.
The current take's waveform grows on the canvas and in the panel's small strip;
the strip shows the selected audio card between recordings.

The seven icon buttons are First, Previous, Play, Pause/Stop Recording, Stop,
Next and Last. Hover gives their names; all are MIDI Learn targets. The **ONE /
ALL** selector sets which loop players Play launches. Launches are additive:
other loops keep playing, and an already-running player keeps its phase.
First/Previous/Next/Last select, reveal and launch a card in existing Card-ID
creation order, independent of canvas position. Previous/Next stop at the ends;
there is no automatic wrap, timeline seek, or restart of a running player.
Advanced retains explicit selected-card Stop and Restart.

### Convert an existing audio card

Select any ordinary/imported audio card and enable **LOOP EVENT** in Simple or
Advanced, then press Play. That switches the card to independent playback;
the Canvas editor's source-repeat setting remains a separate control.
Conversion keeps the same Card ID, source range, notes, tuning, gain/pan,
fades and route. Returning to Timeline restores the original source-repeat
choice. Conversion is undoable and saved in the existing format 15.

This stage retains forward live playback with exact source periods and short
edge fades. Source overlap-crossfade metadata is retained for Timeline mode.
Reverse, ping-pong or sample-intro playback cannot be newly converted silently:
the card stays unchanged with an explanation. TrackSister pattern cards still
need independent native players. Those capabilities and assignable/shared Loop
IDs remain tracked in [MOSAIC_LOOP_ID_PLAN.md](MOSAIC_LOOP_ID_PLAN.md).

### Playing source tiles with QWERTY

Simple starts with **QWERTY ON**. Without a source choice, new notes use the
selected Canvas instrument's Current/Parent choice and existing performance
group/routing behavior. Click an audio source tile in Mosaic to choose it for
new QWERTY notes. A gold **K** and BANK/TILE (or CARD) label identify that source,
separately from the currently selected arrangement card. Source changes can
happen while recording; the big button establishes each layer's boundary.
Automatic selection of a newly captured card does not change the keyboard sound.

Each explicit choice snapshots its audio once on the UI thread. Its tuning,
loop/range/direction and tile/card route are used for new notes; existing held
notes and already-created loops retain their own sound. Retired source copies
are collected after their notes release, using the existing bounded snapshot
pool. Browsing does not rewrite the Canvas parent, bank selection or project.
Pattern sources remain timeline instruments. MIDI note playing retains the
existing global instrument/group selection; this source selector is explicitly
for QWERTY. MIDI Learn performance actions remain usable from any workspace.

**EDIT SHORTCUTS** returns letter keys to Mosaic editing; Ctrl shortcuts and
pointer edits remain available in QWERTY mode. F1–F8 change keyboard octave and
Shift+S toggles the normal sustain policy. Key releases still reach held notes
after switching modes or hiding the panel. Closing/replacing the scene clears
the temporary keyboard-source choice; it is not a new saved project instrument.

Each completed layer is a separate gold card, initially ongoing. Layers are
placed side by side at the session's initial timeline position, to the right
of cards already crossing that insertion line; the view reveals
the new card when Mosaic is open. Publication waits until an active card/pan/
fade gesture finishes, while capture and already-running loops continue. Move/resize them
with the normal Mosaic gestures. Select a card and use **LAUNCH**, **STOP**, or
**RESTART**. Disabling **LOOP EVENT** restores timeline ownership and stops that
independent player. Pattern cards cannot be
converted in this stage; they still play alongside live audio loops.

Gold borders, LIVE STOP/WAIT/PLAY/END labels, and a phase strip distinguish
live cards. Its moving tick below the state label shows the first voice's
position in the source cycle,
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
The waveform adds a fixed 32 KiB overview: 4096 time-tagged peak bins at about
20 ms resolution. The disk writer computes it from successfully written frames
before releasing their ring slots. UI reads atomic bins; the audio callback
does no peak analysis, allocation, waveform construction or disk access.
Preview can trail capture during writer backlog and is not a recovery promise.

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

Enable the usual MIDI Learn and click the large central button for the stable
continuous-layer action (`mosaic.live.primary`). The existing selected-card
toggle remains `mosaic.live.toggle`. The new icon
targets are `first_loop`, `previous_loop`, `play`, `stop_capture`, `halt`,
`next_loop` and `last_loop`, all under `mosaic.live.`. Scope and card role are
also learnable. `halt` stops capture and players; the existing `stop_all` keeps
its loops-only meaning. Advanced also offers RECORD/NEXT,
STOP RECORDING, DROP PART, STOP LOOPS, LAUNCH, STOP and RESTART. RECORD/NEXT is
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
The draggable transport and growing waveform follow-up has its own
[validation and performance exercise](validation/mosaic-looper-cockpit.md).
