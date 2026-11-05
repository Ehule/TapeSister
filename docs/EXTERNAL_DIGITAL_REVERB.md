# Complete interface I/O and an external stereo reverb

TapeSister uses the active backend's **capture and playback layouts independently**.
ASIO numbers are absolute driver channel numbers. SDL uses the negotiated endpoint
layout. JACK numbers identify TapeSister application ports, which need patchbay
connections. Neither a playback number nor a JACK application port identifies a
physical socket by itself. Analog, digital and loopback channels are available
when exposed by that backend. TapeSister does not invent separate headphone
streams or derive a layout from the interface's model name.

F9 presents stereo pairs (the last odd channel has a silent right side), with
vertical source scrolling, an INPUTS filter, and horizontal destination scrolling.
Middle-click an input label/output header, or select it and use NAME I/O, to set
an alias. Numbers remain visible. The identity strip below the controls shows both
channel numbers, direction, ASIO driver names when supplied, and the saved alias.
Other backends have honest numbered fallbacks; JACK says APP / PATCHBAY REQUIRED.
The alias never changes channel mapping. Saved unavailable assignments remain
visible as N/A and silent. No other channel is substituted. Unassigned nonexistent
ports are hidden. The eight small main-window input LEDs remain a compact summary;
F9 input rows and output columns provide meters across the complete layout.

Recording/monitoring's CFG input selector retains MIX, LEFT, RIGHT and STEREO
(the historical all-input folds), then offers individual IN n and stereo IN n/n+1
selections for the active capture layout. Select **IN 11/12**, not STEREO, to
record only that return. The F11 performance microphone mixer retains its separate
fixed eight-channel controls; additional inputs are patched directly through F9.

The endpoint API represents up to 255 channels in each direction, including odd
layouts, with 128 pair slots. That is a storage/API ceiling, **not** an advertised
device count. ASIO rejects larger layouts explicitly. JACK ALL follows discovered
physical port counts independently; an explicit LAST can provision more application
ports for software/aggregate patching. When no physical ports are discoverable,
JACK defaults to two application ports per direction. Nothing auto-connects.
Speaker/ambisonic layouts remain limited to 16 logical speakers and keep their
own assignments; additional playback channels do not add speakers.

## Parallel and serial routing in F9

For a parallel reverb, use an upstream source/bus, or MAIN MIX, as the send tap.
Keep its existing dry connection, add a connection to the chosen OUT pair, and
adjust that cell's LEVEL. Patch the external input pair to EQ (or another chosen
downstream processor/output) and adjust that return cell's LEVEL. Clicking that
cell off mutes the return; disabling the send does not erase the external tail.
Run SunVox 100% wet. Default routing keeps EQ → LIMITER → OUT 1/2.

Example parallel mix:

| Source row | Destination | Level / purpose |
| --- | --- | --- |
| MAIN MIX | MASTER EQ | Existing dry connection, 0 dB |
| MAIN MIX | OUT 3/4, alias S/PDIF SEND | Start at -18 dB, adjust reverb excitation |
| IN 11/12, alias SUNVOX REVERB RETURN | MASTER EQ | Wet level; cell off mutes wet |
| MASTER EQ | LIMITER | 0 dB |
| LIMITER | OUT 1/2 | 0 dB, main monitoring |

For a **serial** insert at the same point, remove MAIN MIX → MASTER EQ, keep
MAIN MIX → OUT 3/4, and use IN 11/12 → MASTER EQ as the replacement path.
An unavailable return is silent; it does not fall back to dry. For serial effect
processing choose the desired dry/wet law in SunVox.

F9 does not auto-create a return-to-send connection. Keep the return downstream
of the send tap. For example, returning to MAIN MIX while sending MAIN MIX out
would deliberately close an external feedback loop; internal graph cycle detection
cannot see hardware/patchbay connections. Do not include the return in the external
send mix in Focusrite Control or SunVox. Loopback is another possible external
feedback path and starts unpatched.

The **legacy Insert** panel is separate: it controls the legacy serial Router's
SEND/RETURN and trim, not matrix cells. Its selectors now reach the full active
layout. Shared SEND numbers retain their old meaning (1 = physical 3/4); dedicated
SEND numbers retain 1 = device 1/2. RETURN remains zero based internally (0 = 1/2).
Do not enable legacy Insert expecting it to control an active matrix. Existing
projects and their choice of routing engine are preserved.

## Scarlett 18i20 4th gen ↔ Hifime S2 Digi / t630 at 48 kHz

The following mapping is based on the manufacturer documentation linked below,
checked 2026-11-04. It is a setup procedure, not a hardware test result.

1. Connect Scarlett USB to the main TapeSister computer and S2 Digi USB to the
   t630. In Focusrite Control 2, Preferences → Sample rate & clocking, choose
   **48 kHz / Optical S/PDIF**. Restart the interface if requested. Optical 2 is
   now S/PDIF; Optical 1 can still carry eight ADAT channels at 48 kHz. Do not use
   Dual ADAT for this loop. Digital availability/order changes with mode, rate,
   firmware and driver; rescan/reopen audio after changing them.
2. Connect **Scarlett Optical OUT 2 → Hifime optical IN**, and **Hifime optical
   OUT → Scarlett Optical IN 2**. Fit the appropriate supplied mini-optical/TOSLINK
   adapters. Optical S/PDIF and optical ADAT are different formats despite sharing
   the connector.
3. In Focusrite Control 2's Routing tab, explicitly assign **S/PDIF L/R source =
   Playback 3–4**. This creates the example's playback mapping. TapeSister OUT 3/4
   feeds that software playback pair; Focusrite Control sends it to the digital
   socket. This is **not** a claim that S/PDIF has a fixed playback number or that
   output numbers match inputs. If the selected playback pair is already in use,
   choose another exposed pair and change both F9 and Focusrite Control together.
   Save a Focusrite preset and verify its S/PDIF output meters. Route monitor and
   headphone outputs to the wanted monitor mix, separately from this send.
4. Use the **Hifime's outgoing S/PDIF stream as clock leader**. Start its output
   on the t630 at 48 kHz, then set Scarlett clock source to **S/PDIF** and verify
   the interface reports external sync/lock and 48 kHz. Scarlett's outgoing stream
   then follows that recovered clock, closing an audio loop without a clock loop.
   This arrangement follows Focusrite's documented external S/PDIF clock option;
   it does not require an undocumented Hifime playback-slave mode. Hifime documents
   simultaneous input/output but does not specify all of its USB/receiver clock
   behavior. Confirm continuous duplex capture/playback on the actual S2 Digi;
   do not claim this combination is validated until that check passes. Both USB
   applications must run at 48 kHz. Matching nominal rates with both boxes set to
   independent internal clocks is not synchronization.
5. On the t630 select the S2 Digi as SunVox's audio input/output device, enable
   stereo live input, connect Input → reverb processing → Output, and set the
   effect to **100% wet**. Avoid an additional dry Input → Output connection.
   Start at a conservative buffer such as 512 frames and verify both channels.
   JACK on the t630 can explicitly patch S2 capture → SunVox input and SunVox
   output → S2 playback if needed. Use one duplex S2 device, not unrelated USB
   input/output clocks. Retain the actual negotiated period, not just the request.
6. On Windows choose the Focusrite **ASIO** driver in TapeSister. Use FIRST 1 /
   LAST ALL for both directions initially. In this Scarlett mode at 48 kHz,
   manufacturer DAW input order is analog 1–8, loopback 9/10, **S/PDIF 11/12**,
   ADAT 13–20. Choose and name IN 11/12 in F9 and apply the parallel table above.
   Higher ADAT and loopback inputs are separate source pairs, initially unpatched.
   A range FIRST 11 / LAST 12 still means physical inputs 11/12, never 1/2.
7. On Linux JACK use the active Scarlett hardware backend/graph. Connect
   TapeSister `master_3` / `master_4` to the Scarlett playback ports corresponding
   to Playback 3/4 in that driver, and Scarlett S/PDIF capture L/R to TapeSister
   `input_11` / `input_12`. Connect `master_1/2` to the monitoring playback ports.
   If using a different app-port mapping, select those app input/output pairs in
   F9 instead. Inspect the actual hardware port names and patchbay connections;
   JACK app port 11 is not inherently Scarlett hardware input 11. A saved JACK
   session/patchbay owns these connections, separately from the TapeSister project.

The 4th gen headphone sockets can have independent hardware mixes/sources in
Focusrite Control. They are not automatically extra independent DAW playback
channels. The driver layout determines the software channels; routing any of
those sources to headphones is a separate hardware routing choice. TapeSister
never creates duplicate outputs merely because headphone sockets exist.

If the Hifime stops transmitting its leader clock, the Scarlett can lose lock.
Restore the S2 output and external lock before reopening TapeSister. Do not try
making both devices follow each other. If actual S2 duplex operation cannot stay
locked in this arrangement, obtain the vendor's clocking details or use a device
with explicitly documented duplex synchronization; do not hide slips with a rate
setting alone.

## Hardware acceptance checklist

- Confirm driver/firmware, optical mode, active input/output counts and actual
  48 kHz rate/period in F12 on Windows ASIO and Linux JACK. Confirm all analog,
  loopback, S/PDIF and ADAT ports exposed in that mode can be selected. Headphone
  sources must follow their Focusrite routing without imaginary DAW channels.
- With the return muted, send alternating left-only/right-only pulses to the
  chosen playback pair. Confirm the correct S/PDIF output meter, SunVox input
  channel, Scarlett input 11/12 meter and F9 row. Reverse the pulse and confirm
  orientation; test a high ADAT pair/output above 16 when the hardware is present.
- Verify Scarlett external S/PDIF lock while Hifime output is running, then run
  continuous stereo duplex audio for at least 30 minutes. Check for clicks,
  drift, discontinuities and xruns. Record the SunVox, JACK and TapeSister actual
  buffer sizes/counters. Test stop/start and loss/recovery of the leader stream.
- With SunVox fully wet, turn the F9 wet cell off: dry stays audible. Restore wet,
  adjust send and return independently, and confirm the return never excites its
  own send. Remove the dry cell for serial mode and confirm only the return plays.
- Save/reopen the project with aliases/routes; disable a range and reconnect the
  device in a smaller layout. The saved high assignments must remain N/A/silent,
  then return at the same numbers after restoring the layout. Check an ordinary
  2-in/2-out device exposes only its one active pair unless saved assignments exist.
- For latency, bypass external reverb temporarily, send/record an impulse through
  the full loop and compare send/return sample positions. Report samples and ms
  at 48 kHz, device periods and any SunVox buffering. Repeat to check stability,
  then restore 100% wet. No round-trip latency or listening result is supplied
  by the simulated automated tests; TapeSister adds no automatic external-delay
  compensation.

## References

- [Focusrite channel order](https://userguides.focusrite.com/hc/en-gb/articles/21616107507218-Scarlett-18i20-4th-Gen-Specifications)
- [Focusrite digital modes](https://support.focusrite.com/hc/en-gb/articles/23955710708242-Digital-I-O-Modes)
- [Focusrite routing and clock source](https://userguides.focusrite.com/hc/en-gb/articles/21616151554578-Using-Focusrite-Control-2-with-your-Scarlett-18i20-4th-Gen)
- [Focusrite hardware/headphone routing](https://userguides.focusrite.com/hc/en-gb/articles/21616151329810-Scarlett-18i20-4th-Gen-hardware-features)
- [Hifime S2 Digi simultaneous optical I/O](https://hifimediy.com/product/s2-digi/)

## Saved identity and format notes

All old IDs are retained: processor 0–6, output destinations 7–14, input source
rows 7–10, buses 11–14, compressor 15, tracker row 16, effect rows 17–32,
sidechain destinations 16 and 33–48, ambisonics 49 and speakers 50–65. Additional
input rows append at 33; additional output destinations append at 66. Source and
destination namespaces are independent. Never use a pair number as a node offset.

Text matrix rows accept historical 15/17/49/66-column lengths and zero the added
routes. New rows are appended; existing aliases and layout order are retained.
Tile TSR43/44, tracker v12 and Mosaic v20/21 store wider routes only when needed;
otherwise their existing formats are emitted. All older supported formats remain
readable. New files containing additional routes require this build or newer.
Device disappearance never rewrites saved routes or aliases. These are numbered
assignments in the selected backend/device namespace; selecting a different device
is an explicit choice, not automatic cross-device remapping.
