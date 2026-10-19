# Performance Transport

Open **CFG → Transport**, **F11**, or **Ctrl+Shift+Space**. **Alt+F11** toggles
fullscreen. This small modeless window mixes ARP,
TrackSister (SisterTracker), and Mosaic while you work elsewhere. A fourth
**Live Tiles** fader controls your live instrument without transport buttons.
The right-hand panel adds the active device's live inputs and an optional tuner.

![Performance Transport and live input tuner](screenshots/performance/performance-inputs-tuner.png)

Screenshot uses a synthetic A3 test tone, seven cents sharp.

## Live inputs and Terra tuning

Choose the input device and physical input range in Audio Config. F11 lists the
channels actually exposed by that driver, six at a time; the arrow buttons page
through wider devices. Channels outside the configured range are unavailable.

- Check an input to add it to **Main Mix**, then drag or wheel its trim from
  silent to +6 dB. Right-click or double-click returns it to 0 dB.
- A single checked channel is centered. Checking both members of a physical
  pair (1/2, 3/4, etc.) preserves left/right stereo. Trim and selection changes
  are smoothed. All inputs start unchecked; project files retain their choices
  and trims. Closing F11 keeps checked inputs audible.
- These are additional live monitor feeds. Existing F9 physical-input routes,
  external Insert returns, and raw input recording remain independent. If an
  input already reaches Main Mix through F9, checking it here adds another feed.
- Switch **Tuner: On** and choose one physical input with the adjacent arrows.
  Hold one steady Terra note. The display shows its nearest chromatic note,
  frequency, and signed cents: negative is flat, positive is sharp. The center
  marker highlights within three cents. Reference tuning is A4 = 440 Hz.
- The tuner reads before these trims, so its mixer input may be unchecked or
  silent while tuning. Silence or an unreliable pitch produces no note; chords,
  heavy modulation, or noise may not yield a stable fundamental.
- The tuner starts off, is never enabled by a project load, and stops when F11
  closes or is minimized. Analysis runs at most ten times per second on the UI
  thread. The audio callback only performs bounded sample decimation/copying
  while tuning is enabled; it never performs pitch analysis or allocates memory.

ASIO duplex input uses the current audio block without an additional FIFO.
Independent capture/output clocks use the existing bounded monitor bridge.
Unavailable channels retain their saved settings but produce no input audio.

| Control | Behavior |
| --- | --- |
| Channel checkbox/name | Includes that player in group commands; it does not mute it |
| Channel Play/Pause | Resumes or pauses TrackSister at its current row and Mosaic at its current time; ARP retains Play/Stop |
| Channel Home | Stops and rewinds just that player |
| Channel Gain | Silent through +6 dB; drag or wheel, double-click or right-click for 0 dB |
| Live Tiles | Shared QWERTY/MIDI and tile-launcher level, independent of the three players |
| Always on Top | Keeps the transport above other windows |
| ARP Clock | Local timing, or TrackSister quarter, eighth, or sixteenth notes |
| Play Group / Pause Group | Resumes or pauses only checked players; running players continue on Play |
| Return to Start | Stops and rewinds checked players |
| Shift+Space | Pauses the group if any checked player is running; otherwise resumes it |
| Space | Keeps the active workspace's existing play/stop or Follow/live-edit behavior |

Selection, gains, clock link, and Always on Top save with the project.
Older projects open at 0 dB with ARP alone checked, local timing, and Always
on Top off. A saved legacy Restart Together setting is ignored. Opening this
window does not change sound.
Closing it leaves playback running. Unchecked players remain independent.

Channel gains act before routing, including effects sends, Matrix routes,
direct hardware outputs, and the tracker tape-input route. Changes are smoothed
over at most five milliseconds. ARP's fader trims only its sequenced voices;
manually held notes keep their own levels. Its existing volume/LFO still applies.
Live Tiles trims manual sample/FM notes, tile launchers and Sister tile groups,
including their custom outputs and sends; Mosaic audio and card gains stay intact.
Its default Matrix path is the Live Tile Bus. A silent channel keeps advancing, and shared effects can finish their tails.

The ARP clock link follows TrackSister BPM, including score tempo commands
within an audio buffer. A tempo change preserves the current note's beat phase.
The link temporarily overrides note intervals without replacing saved ARP slot
timing. Switch back to Local to use those intervals again. ARP slot-change and
LFO timers keep their own seconds settings. When TrackSister is stopped, its
current tempo still supplies the interval. Starting a channel independently does
not reposition another channel; use Return to Start, then Play Group for a
common beginning.

Mosaic shares the group play/pause and explicit rewind. It keeps its seconds
timeline and tape speed: the ARP clock link does not stretch Mosaic or alter its
pitch. TrackSister remains the tempo source, with its own pattern timing.

Group changes publish under one shared output exclusion so checked players begin
at the same next audio callback boundary. Preparation happens first. If ARP has
no notes/playable audio or Mosaic is empty, group Play reports the problem before
starting the other players. Pause remains available. TrackSister resumes at its
paused row; Mosaic resumes at its paused time. Repeated Pause does not rewind.
Only Home or Return to Start rewinds these players in F11, including when Mosaic
has reached its end. Mosaic repeat mode still loops normally during playback.
ARP retains its existing pattern-start behavior after Stop.
Text-entry fields and modal dialogs keep shortcut ownership.

In TrackSister, **double-click an occupied tile** to open that tile's editable
canvas, including tiles on another Sample page. Single-click still selects the
tracker instrument. Playback continues; F10 returns to TrackSister. Empty cells,
page arrows, and gutters do not open a canvas. Finish capture and return from a
parked Record/Mosaic bank before opening a Sample tile.

**Render Offline…** opens the native headless WAV renderer for Mosaic, TrackSister, or both.
It initially copies their group checkbox choices; render selection does not change group
transport. During export the button reads **Render Progress…**. See [Offline rendering](OFFLINE_RENDER.md).
