# Sister Machine Fallout

Fallout is a stereo deterioration instrument inserted after Sister Machine's completed
MIX and before the existing Master FX chain:

`Sister MIX -> Fallout -> Distortion -> Delay -> Reverb -> linked safety -> output`

It is an original C reimplementation inspired by Bahiamansa's freely shared
`failure_v2` ppooll act. No Max, ppooll, patcher, artwork, or UI code is embedded.

## Bypass and lifecycle

FALLOUT is a real insert switch. OFF returns the dry Sister frame exactly after a
12 ms click-free disengage, stops loop-buffer activity, and clears history. ON starts
from a clean 20-second stereo store and fades into the selected MIX. This prevents a
stale fragment from appearing after a later re-enable and keeps the callback allocation
free.

## Controls

- MIX blends Fallout with the incoming Sister MIX.
- FEEDBACK returns Fallout's wet-only signal to Sister's rolling write.
- NOISE adds filtered, wandering tape/debris noise.
- DROP creates Gaussian amplitude failures.
- PAN creates smoothed random equal-power positions.
- SKIP selects buffer-relative loop windows; SPAN controls their size.
- BIT applies sample hold and 8–24-bit quantization.
- PITCH uses the discrete `-3, -2, -1, -0.5, 0.5, 1, 2, 3` ratio family.

DROP, PAN, SKIP, BIT, and PITCH each have independent automation toggles. Their RATE
controls range from approximately 20 to 2000 ms. PITCH RAMP reaches 500 ms.

## Feedback safety

Fallout feedback is tapped before Fallout MIX and before Master FX. It is smoothed,
linked-capped, saturated, delayed by one sample, and then combined with the existing
Master FX return. The shared return enters after Sister INPUT trim and is processed by
the established DC blocker and rolling-write soft saturation. Turning Fallout off
drives its feedback state to exact zero and clears its loop history.
