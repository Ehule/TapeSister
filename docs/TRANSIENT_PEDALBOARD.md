# Transient pedalboard

Click any pedalboard slot's type button until **TRANSIENT** appears, or add an
independent **TRANSIENT** instance in the routing matrix.

| Control | Range | Use |
| --- | --- | --- |
| ATK | ±15 dB at 1×, up to ±60 dB at 4× | Soften or emphasize the start of a sound |
| SUS | ±24 dB at 1×, up to ±96 dB at 4× | Shorten or emphasize its decay |
| PWR | 1× / 2× / 3× / 4× | Multiply attack and sustain shaping; 1× retains the original behavior |
| RESP | 0.25× to 4× | Scale detector timing; 1× is the starting point |
| MIX | 0–100% | Blend the shaped signal with its input |
| GAIN | −12 to +12 dB | Match the resulting level |

ATK/SUS at zero and GAIN at zero give exact unity at every MIX value. The
processor adds no audio delay. Both channels share one detector and gain, so
opposite-polarity stereo material still triggers correctly and keeps its balance.
Sustain increases existing decay; it does not generate a reverb tail from silence.
Higher RESP values follow longer events; lower values suit shorter ones.

Click **PWR** to cycle its four strengths, use the wheel to step, or right-click
to restore 1×. The ATK/SUS readouts show the effective dB values at the chosen
power. This multiplies the envelope-shaping amount; it is not a cascade of
independent detectors and does not promise the exact sound of several stacked
instances. Neutral ATK/SUS remain exact unity at every power. Existing projects
and presets load at 1×. Boosts at 3×/4× can produce large level changes; MIX and
GAIN remain available for balancing the result.

## Five saved states

Each Transient slot and independent Matrix instance has five state buttons and
its own **GLIDE** time, using Grain's capture/recall gestures:

- Click an empty state to capture ATK, SUS, RESP and PWR.
- Click a stored state to recall it. Right-click it to enter editing.
- Click the edited/changed selected state to save, or Shift-click any state to overwrite it.
- Middle-click a state to clear it while retaining the current sound.
- Drag or wheel GLIDE from instant to 600 seconds; right-click for instant.

MIX, GAIN, routing and bypass fades stay independent of these shape states.
State recalls glide through effective attack/sustain dB and Response, including
when the destination uses a different power. A new recall starts from the
audible in-progress setting. Manual shape/power edits or edit recalls interrupt
the glide while keeping detector history and normal control smoothing.

State banks and power survive project and user-preset save/load, device changes
and slot reordering. Grain and Transient retain separate banks when the slot's
type changes. Runtime edit flags and recall commands are not saved. Matrix
instances also retain their states in configuration and routing Undo/Redo.

A useful first experiment is a dry drum loop, MIX 100%, RESP 1× and GAIN 0 dB.
Try ATK +6 dB, then −6 dB. Return ATK to zero and try SUS −12 dB, then +6 dB.
Match loudness with GAIN before deciding which version you prefer. All five
slots support PRE, individual heads, POST, reorder, MIDI learn, parameter locks,
and the existing independent effect/master transition clocks.

## Routing

In the normal chain, the pedalboard already processes the full signal. In the
routing matrix, select the **PEDALS** row and choose **MODE: INSERT** to process
the full signal there. Route the source through PEDALS and remove that source's
parallel direct route when you want transient cuts to affect the whole sound.

**MODE: SEND** retains the existing wet-return behavior. A parallel dry path
continues to carry the original transient. Insert bypass restores its input;
Send bypass silences its return. The mode change slews over 12 ms and advances
the shared DSP only once per sample. Mode selection is saved in projects/config
and participates in matrix Undo/Redo and Original/Edited history. Existing
matrix projects retain their mode until explicitly changed.

### TrackSister group processing

Open **F9 → TRACKS** and select **TRACKSISTER BUS**, directly below the individual
track rows. It collects tracks using their normal output, after the F11 TrackSister
level. Its default route is Main, preserving the existing mix. To shape the group,
disconnect the bus from Main, connect it to PEDALS, and use **MODE: INSERT** on the
pedalboard row. The bus also has the usual level, pan, mono, hardware-output and
compressor/sidechain routing controls.

An explicit individual track route bypasses this bus. To put any selection of
tracks back into the group, select those track rows and click **TO TRACK BUS** at
the bottom left. Ctrl/Shift selection and Undo/Redo work for this action. **USE TILE**
still follows each playing tile's route; only tiles using their normal output
then reach the TrackSister bus. Live keyboard tiles, FM, Mosaic and preview audio
remain separate. F11 TrackSister level controls both grouped and direct tracks.

Reset to Default returns tracks to the bus; Clear All disconnects it along with
the other routes. Project-state version 38 saves the new source row, and older
projects receive a unity route to Main. Existing destination IDs are unchanged.
The duplicate F9 hint has been removed from the COMP button; it remains in the
window title and footer.

## Design and compatibility

This is an original digital interpretation of the differential-envelope idea
explained in SPL's [Transient Designer 4 Mk2 documentation](https://spl.audio/en/spl-produkt/transient-designer-4-mk2/)
and [Transient Designer 2 manual](https://spl.audio/wp-content/uploads/transient_designer_2_9946_manual.pdf).
The familiar attack/sustain ranges and threshold-free interaction are the
reference. The detector timing and adaptive release are our own design; no
hardware measurements or matched listening comparison establish circuit-level
or sonic equivalence. Response, parallel Mix and TapeSister's placement/transition
controls extend that starting point.

The detector compares normalized fast and slow envelopes, with separate onset
and decay paths. A relative ripple dead band rejects small carrier fluctuations.
A smooth near-silence guard protects floating-point residue; ordinary signals
above −140 dBFS use the normalized response. Control edits slew over 12 ms;
Mix and makeup gain slew over 20 ms. Coefficient exponentials are cached at
control changes. The sample path has bounded work and no allocation or locks.

Processor IDs and all existing MIDI/lock parameter IDs retain their values.
Slot 5's five parameters are appended after Prism instead of taking its IDs.
Project-state version 37 introduced the new slot, as did preset version 21; older versions
load it empty. Each slot/location owns independent history. Visual snapshots
include the new type without reassigning Prism's Mandala channel.

The fifth slot increases the board's preallocated reverb/delay/grain storage by
about 15.8 MiB at 48 kHz, even when empty. The transient processor itself uses
only small fixed state. This preserves the existing allocation-free live type
switching behavior. Physical-device CPU headroom and musical tuning still need
listening on the target machine.

## Verification

`tapesister_transient_tests` checks unity, onset/tail boost and cut, level scaling,
stereo linking, silence, nonfinite input, Response timing, steady carriers,
8/44.1/48/96/192 kHz processing, independent head history, fifth-slot project and
preset recall, legacy project/preset defaults, sample-rate reconfiguration, three-minute
slot and independent master fades, and matrix Insert/Send audio behavior.
It also checks the extended ranges at five sample rates, stronger onset/tail
shaping, independent Matrix power, neutral unity at every power/Mix, interrupted
state glides and project/preset/configuration state recall.

`tapesister_transient_controller_tests` drives the native controller, checking
all fifth-row fields, MIDI targets, stable IDs, parameter locks, reorder, both
clock hitboxes, and matrix mode Undo/Redo. With any argument it also writes native
framebuffer screenshots. The legacy hit-test helper also guards its optional model pointer, allowing the
existing Sister UI fixtures to run. The Mandala controller fixture checks distinct Prism
and Transient channels. Both new fixtures are mandatory Linux/Windows release
gates.
The same controller fixture checks Transient power, capture/edit/clear, glide,
reorder and Matrix state Undo/Redo. These fixtures, the Grain/pedalboard fixture
and the independent Matrix instance fixture are also baseline PR gates.

The routing and source-route controller fixtures additionally check TrackSister
group audio, independent track overrides, live tile isolation, F11 trim, bus
meters, return-to-bus Undo/Redo, current/legacy project recall and reset behavior.

![Native five-slot pedalboard](screenshots/transient-pedalboard.png)

![TrackSister bus in the native routing matrix](screenshots/matrix-tracksister-bus.png)
