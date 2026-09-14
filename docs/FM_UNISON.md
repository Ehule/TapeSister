# Twelve-voice Unison

![Compact Unison controls](images/fm-unison-first.png)

Open FM Logic with **Shift+grave**. Shape the original sound, then press
**UNISON**. Its button highlights while twelve independent oscillators are active.
Press it again to restore the original patch and waveform. Shape that source
further and switch Unison on again to build a fresh stack from voice 1.
The original ten FM structures retain their six-operator routing.

| Voice | Pitch relative to voice 1 |
| --- | --- |
| 1 | Original pitch |
| 2 / 3 | −7 / +7 cents |
| 4 / 5 | −12 / +12 cents |
| 6 / 7 | −19 / +19 cents |
| 8 / 9 | −26 / +26 cents |
| 10 | One octave below |
| 11 / 12 | One octave below, −7 / +7 cents |

The three lower voices add bass weight. Every voice copies voice 1's waveform
and LFO settings and stays individually editable. Use **VOICES 1–6 / 7–12** to
reach them all. The Pitch, Wave and three LFO pages follow that bank; Filter and
Structure always show six global controls. Wheel changes a unison voice by a
semitone; **Shift+wheel** changes it by one cent.

![Voices 7–12, including the three lower voices](images/fm-unison-extra.png)

Turning Unison on enables Drone and Pitch Lock, protects Structure from
randomization, and starts feedback, interaction mix and transient amount at zero.
It retains the source's global filter. For a saw ensemble, start with **SAW**,
**LFO OFF**, **6000 Hz** low-pass, **20% resonance**, and zero filter envelope.
At extreme pitch limits the center moves inward enough to keep the full spread
and lower voices in range; switching off still restores the original settings.

Switching off restores the complete source, including its routing, active voices,
filter and modulation settings. Edits made to the unison stack are replaced when
you switch it off; the next activation builds from the source again. **APPLY**
stores both the active sound and its original source in the tile, so the toggle
also works after saving and reopening. The usual Chain/Mosaic destination choices
still apply. **Grave** visits Mosaic; **Shift+grave** visits FM.

Restoration also works after editing voices 7–12 or when the original patch is
silent. A quiet preview displays **FM PREVIEW QUIET** and replaces the old waveform
and audio, so a previous sound cannot disguise an all-off or heavily filtered patch.
The usable-signal check still applies when generating or applying a tile.

Sound edits render the full eight-second preview in a background worker. The
header shows **UPDATING** until the latest settings reach the waveform and held
notes together. Rapid edits coalesce over 50 ms; an obsolete result is discarded.
The previous preview keeps playing during that update, and held notes keep their
positions and latches. Initial workspace opening still prepares its first preview
synchronously so immediately played notes have audio available.

Returning to the same unchanged tile keeps the parked patch. Selecting a different
tile or changing/deleting its source starts from the newly selected sound; an empty
destination starts a fresh FM patch rather than inheriting a deleted tile's Unison.
Mosaic event workspaces retain their existing event ownership.

Interaction type, modulation depth and interaction mix are inactive and dimmed in
Unison. Index LFOs have no modulation index to change. Feedback still affects final
output saturation. Filter and output trim remain shared. Selecting an older
Structure routing manually uses only the original six operators.

## Compatibility and checks

New saves use **TSR30 / genome 7**, storing twelve voice settings and a bounded,
nonrecursive copy of the original sound. TSR6–TSR29 projects remain readable.
Older nine-voice tiles retain their original sound with the three new voices off.
They did not save a pre-Unison source: switching those old tiles off isolates
voice 1, and the status message identifies that fallback. New activations save
the complete original.

Newly saved projects require this build or newer. Export WAVs for older builds.
The previously supplied Portal audition pack remains TSR27.

Native tests measure all twelve pitches at 44.1/48 kHz, each carrier in isolation,
cent edits, voice-bank mapping, exact restoration, and persistence of both sounds.
Compatibility checks load and regenerate files produced by the prior six- and
nine-voice builds. Workspace tests cover parked FM state and event ownership,
restoring a quiet source after edits to voices 7–12, all-off/on waveform and held-note
updates, stale worker completion, deleting/replacing the source, and closing while
a preview job is running. The tile renderer still rejects unusable output.
The native controller and FM renderer also pass AddressSanitizer and
UndefinedBehaviorSanitizer checks (LeakSanitizer disabled on the test host).
