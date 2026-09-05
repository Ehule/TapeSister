# Tapehead → TapeSister Live Link

Live Link makes Tapehead a native stereo source inside TapeSister. It replaces the
VB-CABLE-style handoff with a local shared-memory audio ring, leaving TapeSister as the
only application that owns the speakers or audio interface.

## Start it

1. Start TapeSister with the desired physical output device.
2. In Tapehead, open **Config → Audio** and select **TapeSister Live Link**.
3. Open Sister Machine. The source strip shows **LINK** when Tapehead is present and
   **WAIT** while TapeSister is waiting.
4. With Sister Machine off, Tapehead enters the ordinary program/post-FX path. With
   Sister Machine on, click **TAPEHEAD** to route it through the tape, heads, Fallout,
   and placed pedalboard effects.

Start order does not matter. TapeSister checks once per second and reconnects after a
Tapehead restart. The TAPEHEAD switch can remain armed while **WAIT** is shown; its
source route fades in automatically when the producer appears.

## Signal and capture behavior

The transport carries 32-bit float stereo. TapeSister accepts Tapehead rates from
8–384 kHz and linearly resamples to its current output rate. An adaptive roughly 50 ms
buffer corrects small independent-clock drift. Start, stop, disconnect, underrun, and
session replacement use short fades rather than discontinuous cuts.

The TAPEHEAD source has its own 0–400% mixer trim. It is stored in `tapesister.ini`,
Sister projects, and Sister presets. As with the other Sister inputs, a selected source
is a closed insert while Sister Machine is powered: it leaves the ordinary direct path
and returns through Sister's DRY/WET monitoring. If Sister is off, it remains audible
through the ordinary post-effects rack.

Tapehead audio can be captured in the same places as other musical sources:

- main Capture or Overdub prints the complete performed TapeSister output to a tile;
- Sister H1/H2/H3/MIX capture prints the chosen tape tap to a tile;
- FILE records the selected tap or final OUT as a long-form WAV/RF64 take.

Only one consumer and one active producer are supported. If multiple Tapehead instances
select Live Link, the newest session becomes authoritative and TapeSister reconnects to
it.

## Why it avoids the Windows conflict

Tapehead opens no hardware device while Live Link is selected. TapeSister alone owns
the physical output and keeps its existing Auto/WASAPI/DirectSound policy. This removes
the cross-backend ownership fight that can occur when Tapehead, TapeSister, REAPER/ASIO,
and a virtual cable independently request the same interface.

## Release check

- Test both application start orders and restart each side independently.
- Confirm **WAIT → LINK** and clean fades on connection/disconnection.
- Test matching rates plus 44.1 kHz → 48 kHz and 96 kHz → 48 kHz.
- Verify the TAPEHEAD trim, ordinary post-FX, Sister Machine, Fallout, all four pedalboard
  placements, tile Capture/Overdub, and FILE capture.
- Leave a link running long enough to check the audio diagnostic overrun counters.
