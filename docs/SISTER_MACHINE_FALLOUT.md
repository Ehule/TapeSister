# Sister Machine Fallout

Fallout is a stereo deterioration instrument inserted after Sister Machine's completed
MIX and before the existing Master FX chain:

`Sister MIX -> Fallout -> Distortion -> Delay -> Reverb -> linked safety -> output`

It is an original C reimplementation inspired by Bahiamansa's freely shared
`failure_v2` ppooll act. No Max, ppooll, patcher, artwork, or UI code is embedded.

## Bypass and lifecycle

FALLOUT is a real insert switch. OFF returns the dry Sister frame exactly after its
selected transition, stops loop-buffer activity, and clears history. ON starts from a
clean 20-second stereo store and fades into the selected MIX. TRANSITION is logarithmic
from 10 ms to 60 seconds, so the insert can arrive almost immediately or emerge over a
performance-length ramp. `sister_fallout_transition_ms` selects the startup default in
`tapesister.ini`; presets and projects preserve the live setting. This prevents a stale
fragment from appearing after a later re-enable and keeps the callback allocation free.

## Controls

- MIX blends Fallout with the incoming Sister MIX.
- FEEDBACK returns Fallout's wet-only signal to Sister's rolling write.
- NOISE cycles among WHITE, PINK, BROWN, and BLUE spectra; LEVEL sets its amount.
- DROP creates Gaussian amplitude failures.
- PAN creates smoothed random equal-power positions.
- SKIP selects buffer-relative loop windows; SPAN controls their size.
- BIT applies sample hold and 8–24-bit quantization.
- PITCH uses the discrete `-3, -2, -1, -0.5, 0.5, 1, 2, 3` ratio family.

DROP, PAN, SKIP, BIT, and PITCH each have independent automation toggles. Their RATE
controls range from approximately 20 to 2000 ms. PITCH RAMP reaches 500 ms.

## Generative modulation

Fallout has one sine LFO with RATE and DEPTH controls. RATE is logarithmic from one
cycle per hour to 10 Hz. Its independent RISE layer has TIME and DEPTH controls;
TIME is logarithmic from one second to four hours. RISE can repeat as a sawtooth or
run once. In 1-SHOT mode it reaches the exact apex, drops immediately to the saved
value, and remains there. Clicking the active 1-SHOT button retriggers it, as does
re-engaging Fallout. `sister_fallout_rise_seconds` selects the startup time in
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

## Feedback safety

Fallout feedback is tapped before Fallout MIX and before Master FX. It follows both
the LFO-modulated FEEDBACK value and the insert's transition envelope. It is smoothed,
linked-capped, saturated, delayed by one sample, and then combined with the existing
Master FX return. The shared return enters after Sister INPUT trim and is processed by
the established DC blocker and rolling-write soft saturation. Turning Fallout off
drives its feedback state to exact zero and clears its loop history.
