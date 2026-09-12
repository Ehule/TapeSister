# Sister Machine Fallout

Fallout is a stereo deterioration instrument inserted after Sister Machine's completed
MIX and before pedalboard slots placed at POST:

`Sister MIX -> Fallout -> POST slots in 1-2-3-4 order -> linked safety -> global limiter -> master OUT`

Pedalboard slots placed at PRE or H1/H2/H3 enter at their named earlier locations and
therefore do not appear in this post-MIX shorthand. See the
[User Manual](USER_MANUAL.md#the-four-slot-fx-pedalboard) for the current rack.

Fallout also processes ordinary playback with Sister powered off, including Mosaic:
the shared input mix enters Fallout, then POST slots, the limiter, and final OUT.
PRE and head placements require Sister. Mosaic supplies Sister's **TILES** input
when Sister is powered, and uses this ordinary route when it is off. Switching
Sister power preserves the global effect settings, transitions, and modulation state.

It is an original C reimplementation inspired by Bahiamansa's freely shared
`failure_v2` ppooll act. No Max, ppooll, patcher, artwork, or UI code is embedded.

## Bypass and lifecycle

FALLOUT is a real insert switch. OFF returns the dry input frame exactly after its
selected transition, stops loop-buffer activity, and clears history. ON starts from a
clean 20-second stereo store and fades into the selected MIX. TRANSITION is logarithmic
from 10 ms to 60 minutes, so the insert can arrive almost immediately or emerge over a
performance-length ramp. `sister_fallout_transition_ms` selects the startup default in
`tapesister.ini`; presets and projects preserve the live setting. This prevents a stale
fragment from appearing after a later re-enable and keeps the callback allocation free.

The pedalboard's **MASTER FX** switch separately governs all live pedalboard and
Fallout processing, including Fallout feedback, on both the Sister and ordinary
routes. Its selected Master transition applies to this combined bypass. Individual
Fallout settings and modulation clocks survive Master FX bypass; the FALLOUT switch
above has its own history-reset behavior. Bypass leaves dry playback audible and
does not undo processing already recorded into Sister's tape memory.

## Controls

- MIX blends Fallout with the incoming Sister MIX or ordinary playback mix.
- FEEDBACK returns Fallout's wet-only signal to Sister's rolling write when powered,
  or through the bounded ordinary playback return with Sister off.
- NOISE cycles among WHITE, PINK, BROWN, and BLUE spectra; LEVEL sets its amount.
- DROP creates Gaussian amplitude failures.
- PAN creates smoothed random equal-power positions.
- SKIP selects buffer-relative loop windows; SPAN controls their size.
- BIT applies sample hold and 8–24-bit quantization.
- PITCH uses the discrete `-3, -2, -1, -0.5, 0.5, 1, 2, 3` ratio family.

DROP, PAN, SKIP, BIT, and PITCH each have an independent master switch. Their RATE
controls range from approximately 20 to 2000 ms. PITCH RAMP reaches 500 ms.

## Generative modulation

Fallout has one sine LFO with RATE and DEPTH controls. RATE is logarithmic from one
cycle per hour to 10 Hz. Its independent RISE layer has TIME and DEPTH controls;
TIME is logarithmic from one second to four hours. RISE can repeat as a sawtooth or
run once. In 1-SHOT mode it reaches the exact apex, drops immediately to the saved
value, and remains there. Every selected RISE target shares this one phase clock.
The main-page RETRIGGER button restarts them together; assigning a target or opening
MOD never restarts the clock. Re-engaging Fallout also restarts the one-shot.
`sister_fallout_rise_seconds` selects the startup time in
`tapesister.ini`; presets and projects preserve the live modulation settings.

Clicking MOD opens a shared target matrix for MIX, FEEDBACK, NOISE and the applicable
DROP, PAN, SKIP, BIT, and PITCH parameters. Each row has independent LFO and RISE
switches, and either modulator may control several targets at once.

Modulation is centered on each saved parameter value and never rewrites it. DEPTH sets
a symmetric excursion using the available room on both sides. For a 0–10 control set
to 5, 20% depth travels from 4 to 6 and 100% depth travels from 0 to 10. A center near
an edge receives a smaller symmetric range rather than spending part of the cycle
flattened against a limit. Slowly moving rates and ranges continue to provoke Fallout's
independent random events, allowing a fixed source pitch to develop generatively.

When LFO and RISE target the same parameter, RISE moves the center from the saved
value toward the upper limit while the sine continues oscillating symmetrically around
that moving center. At the sawtooth edge or one-shot endpoint, the center returns to
the saved value and the LFO continues around it. The saved slider never moves, so the
performance can always return precisely to its authored state.

The main page shows compact live position traces for both modulators. RISE uses a
saw-line and marker, including its terminal drop; LFO uses a simple phase line. These
indicators report the shared modulators without moving the authored parameter faders.
At the end of either SAW or 1-SHOT, the displayed phase still drops immediately while
the modulation signal crosses the reset through a fixed 10 ms de-click ramp. Manual
retriggers use the same protection, preventing abrupt MIX or FEEDBACK changes from
becoming audible clicks without perceptibly changing the rise timing.

## Fallout presets

On the Fallout page, the footer preset controls address an independent Fallout bank;
the same controls on the Tape and Master FX pages continue to address the complete
Sister Machine bank. Fallout user presets persist in `fallout-presets.ini` and support
the established Save As, Overwrite, Rename, Delete, previous, and next workflow. The
selected identity remains attached after an edit and gains a `*`, so a modified user
preset can be overwritten instead of being demoted to CUSTOM. Factory presets remain
recall-only and become Save As sources when modified. Previous/next arrows and the bank
position are also available inside the manager.
Factory starting points are APPROACHING TRAIN, DUST WEATHER, and DEAD TRANSMISSION.

Presets store every Fallout sound and modulation setting except master power and live
phase. Recall therefore never engages or bypasses the insert unexpectedly. When Fallout
is active, the TRANSITION value visible before recall governs the complete changeover:
the current sound fades to dry over the first half, the destination is installed at the
silent midpoint, and the new sound fades in over the second half. The destination's
shared RISE restarts at that midpoint. When Fallout is bypassed, recall quietly prepares
the destination and the next power-on begins it from a clean phase.

DROP, PAN, SKIP, BIT, and PITCH are strict master gates. A disabled process ignores
remembered LFO and RISE assignments. PITCH OFF returns playback to unity through a
10 ms de-click ramp. PITCH ON samples the saved or modulated discrete RATIO at its
RATE and reaches it over RAMP. Disconnecting an event-rate modulation target re-arms
that event immediately so its saved panel value resumes without a stale interval.

## Feedback safety

Fallout feedback is tapped from the wet signal before Fallout MIX. It follows
the LFO-modulated FEEDBACK value, the insert's transition envelope, and the Master FX
gate, so a bypassed master cannot continue feeding Fallout into the tape. It is smoothed,
linked-capped, saturated, delayed by one sample, and then combined with the existing
Master FX return. The shared return enters after Sister INPUT trim and is processed by
the established DC blocker and rolling-write soft saturation. Turning Fallout off
drives its feedback state to exact zero and clears its loop history.

With Sister off, the runtime uses a separate bounded, one-sample-delayed return
around the ordinary Fallout input. Master FX gates that return too. No powered
rolling-tape engine is needed for this route.
