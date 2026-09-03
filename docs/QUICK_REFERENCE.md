# TapeSister Quick Reference

For explanations and complete workflows, see the [User Manual](USER_MANUAL.md).

## Global navigation

| Control | Action |
| --- | --- |
| `Tab` | Open Sister Machine or move focus between Sister and the main window |
| `` ` `` | Open/close FM Logic |
| `1` | Show Sample Tiles; press again to cycle Sample pages |
| `Shift+1` | Open external REC BANK |
| `2` | Show performance keyboard |
| `3` | Show CDP; press again to cycle CDP pages |
| `4` | Show native DSP; press again to cycle DSP pages |
| `F1`–`F8` | Select keyboard octave |
| `Space` | Play selection/from playhead; press again for panic stop |
| `Escape` | Cancel active gesture/dialog; otherwise request exit |

## QWERTY note keyboard

Lower row: `Z S X D C V G B H N J M`

Upper row: `Q 2 W 3 E R 5 T 6 Y 7 U`

MIDI note 60/C4 is unity for created FM material. MIDI velocity controls sample voice
level. MIDI All Notes Off is honored.

## Project and editor shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` | Load WAV, TSR, or TSP |
| `Ctrl+S` | Save active project / open Save browser |
| `Ctrl+E` | Export selected WAV or collection |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+A` | Select all audio |
| `Ctrl+C` / `Ctrl+X` | Copy / Cut selection |
| `Ctrl+V` | Exact Paste |
| `Ctrl+Shift+V` | Fit Paste into selection |
| `Ctrl+R` | Reverse |
| `Ctrl+N` | Normalize |
| `Ctrl+I` / `Ctrl+U` | Fade in / Fade out |
| `Ctrl+Up` / `Ctrl+Down` | Gain +3 dB / -3 dB |
| `+` or `=` / `-` | Zoom in / out |
| `Left` / `Right` | Pan waveform |
| `0` | Show complete tile |

## Waveform mouse gestures

| Gesture | Action |
| --- | --- |
| Click | Place edit playhead |
| Right-click | Play from pointer |
| Drag | Make selection |
| Wheel | Pointer-anchored zoom |
| `Shift+wheel` | Horizontal pan |
| `Ctrl+wheel` | Rotate through zero crossings |
| `Ctrl+Shift+wheel` | Fine zero-crossing rotation |
| `Alt+wheel` over selection | Expand/contract nearest selection endpoint |
| `Shift+Alt+wheel` | Tape-length change in semitones |
| `Ctrl+Shift+Alt+wheel` | Tape-length change in cents |
| Escape during gesture | Restore the pre-gesture audio |

Ordinary sliders accept click/drag, wheel, and Left/Right while hovered. Shift makes
wheel/arrow adjustment finer in Sister Machine and coarser where the main interface
explicitly indicates it.

## Tile interaction

| Gesture | Occupied tile | Empty tile |
| --- | --- | --- |
| Click | Select and audition | Select destination |
| Double-click | Select/audition | Create silent editable tape |
| Shift-click | Toggle Sister source membership | Copy active tile here |
| Plain click during performance | Launch/release layer | Select destination |

The active editing tile, current preview, Sister source membership, loop state, and
Capture destination use separate visual marks.

## Create and Variation

| Control | No selection | With selection |
| --- | --- | --- |
| CREATE | Fresh FM sound replaces selected tile | Fresh FM sound is fitted into range |
| VARY, Chain off | Replace current tile with related sound | Replace range with related sound |
| VARY, Chain on | Put relative in next empty tile | Stamp and advance same-width range |
| RANGE | Controls family distance | Controls variation distance |

Precise-duration recipe: double-click empty tile → select desired time → CREATE → VARY
as desired → CROP.

## Main Capture

1. Select/double-click destination tile.
2. Choose M or S.
3. Press CAPTURE or enable OVERDUB.
4. Deliberately trigger a different tile, QWERTY/MIDI note, loop, or staged chord.
5. Press STOP/Space to keep; Escape to cancel.

| Format | Stored result |
| --- | --- |
| M | `0.5 × (L + R)` mono |
| S | independent stereo L/R |

The main and Sister M/S controls mirror one shared setting.

## REC BANK

| Control | Meaning |
| --- | --- |
| `Shift+1` | Open REC BANK |
| SRC EXT | Record configured physical input |
| SRC SYNTH | Record internal FM voices only |
| REC ARM | Wait for threshold / begin recorder workflow |
| MONITOR | Add dry external input to output; use headphones |
| CHAIN | Advance to next empty REC tile and rearm |
| KEEP | Copy all REC tiles into empty Sample slots, then clear REC BANK |

External input modes: MIX averages all channels; LEFT uses input 1; RIGHT uses input 2;
STEREO maps odd channels to L and even channels to R.

## Sister Machine transport and routing

| Control | Meaning |
| --- | --- |
| POWER | Allocate/release Sister engine and histories |
| ROLL | Move write head and accept writes |
| HOLD | Stop writing while playback heads continue |
| CLEAR | Safely clear rolling memory |
| MONITOR | Gate complete Sister DRY+WET return |
| BUFFER | Live 5–60 second rolling tape |
| TILES | Route page-specific Shift-click tile mask |
| FM | Route live FM Logic |
| EXT | Route external input |
| AUDITION | Route preview/audition bus |

A routed source leaves its ordinary direct speaker path and returns through Sister.

## Sister heads and tape controls

| Area | Controls | Function |
| --- | --- | --- |
| H1 | Level, Time, Feed | anchored delay/feedback head |
| H2 | Level, Scrub, Rate, Feed | movable reverse/forward feedback head |
| H3 | Level, Span, Rate | independent movable head |
| Character | Wow, Drop, Duck, Decor, Width | movement, failure, dynamics, stereo shape |
| Filter | Type, Cutoff, Q, Gain | completed head-sum filter |
| Monitor/write | Input, Dry, Wet, Out, Erase, Ghost | gain staging, monitoring, memory retention |
| Stereo weave | Soak, Bleed, H1/H2/H3/Mix | changing delayed cross-channel transfer |

H2/H3 rates: `-2, -4/3, -1, -2/3, -1/2, 1/2, 2/3, 1, 4/3, 2`.

The T/F/E/A/X mixer trims Tiles, FM, External, Audition (0–400%), and effects return
(0–200%). Sister's internal OUT is 0–400% and does not change isolated head taps.

Shift-click an adjustable Sister/FX field to lock or unlock it.

## Sister Capture

| Selector | Choices |
| --- | --- |
| Tap | H1, H2, H3, MIX; final MIX becomes OUT in FILE mode |
| Format | M or S |
| Destination | CURRENT, NEXT EMPTY, FILE |

FILE records until stopped and automatically upgrades WAV to RF64 when required. OUT
file recording remains available when Sister is powered off; H1/H2/H3 require Sister.

## FX pedalboard

Signal order is slot `1 → 2 → 3 → 4`. Each slot can be Empty, Reverb, Delay,
Distortion, or Grain; duplicates are allowed.

| Type | Parameters |
| --- | --- |
| Reverb | Gain, Size, Decay, Mix |
| Delay | Gain, Time, Feedback, Mix |
| Distortion | Gain, Drive, Tone, Mix |
| Grain | Gain, Size, Density, Pitch, Mix |

Placement is exactly one of:

| Placement | Position |
| --- | --- |
| PRE | new source before INPUT/write/Duck |
| H1/H2/H3 | after selected head read, before later head character/level |
| POST | after Sister MIX and Fallout |

Effect and Master transitions: 10 ms–60 min. Slot Gain: -12 to +12 dB. FX Feedback:
0–135%.

## Fallout

| Section | Controls/function |
| --- | --- |
| Main | Mix, Feedback, Noise type/level |
| Drop | random amplitude failure and rate |
| Pan | smoothed random position and rate |
| Skip | buffer loop span and rate |
| Bit | sample hold, bit depth, rate |
| Pitch | discrete ratio, ramp, event rate |
| Transitions | Preset, Parts, Master; each 10 ms–60 min |
| LFO | sine, 1 cycle/hour–10 Hz, symmetric depth |
| Rise | Saw or 1-Shot, 1 second–4 hours |
| Retrigger | restart every Rise target together |

MOD targets: Mix, Feedback, Noise, Drop Rate, Pan Rate, Skip Span/Rate, Bit
Sample/Depth/Rate, Pitch Ratio/Ramp/Rate. `L` assigns LFO; `R` assigns Rise.

## Final output

Final order: mix → linked limiter → OUT fader → L/R meter and FILE OUT.

| Readout | Meaning |
| --- | --- |
| LIM | global limiter enabled |
| GR 0.0 | no current gain reduction |
| GR-x.x | limiter reducing by x.x dB |
| LIM OFF | limiter bypassed |

OUT is after the limiter. Lower pre-limiter stages to reduce gain reduction.

## Files and folders

| Item | Contains | Portable rule |
| --- | --- | --- |
| `.tsr` | complete editable project/page state | keep inside its named project folder |
| `samples/` | extractable 16-bit PCM WAV copies | move with the project folder |
| `project-data/` | additional pages and REC BANK | move with the project folder |
| `sister-state.ini` | Sister/Fallout project state | move with the project folder |
| `manifest.txt` | collection map | move with the project folder |
| `.tsp` | processing recipe, no audio | standalone |
| `.wav` | ordinary audio export/capture | standalone |
| `Captures/` | immutable 32-bit float performance archive | intentionally outside projects |

Saving `Name.tsr` creates the movable folder `Name/`. Share or back up that whole folder.

## File browser

| Control | Action |
| --- | --- |
| Up/Down | Move selection |
| Page Up/Page Down | Move by page |
| Home/End | First/last entry |
| Enter/double-click | Open/accept |
| Backspace | Parent directory when file list owns focus |
| Escape | Cancel current browser action |

Save and Export append the proper extension. Replacing a file requires a deliberate
confirmation.

## Safety

- Space is the immediate playback panic stop.
- Escape cancels the active gesture or dialog.
- Keep LIM on during feedback and Extreme exploration.
- Lower Sister/FX/Fallout levels before the limiter when GR is excessive.
- Use headphones for microphone monitoring.
- Sister Capture refuses a destination that is also a live Sister source.
