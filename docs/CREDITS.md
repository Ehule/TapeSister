# Credits

Open **F11 > CREDITS** or press **Shift+F12** from any TapeSister window.
Any key, click, or the window close button dismisses the screen. Opening it
leaves the current workspace and running audio intact. It never opens because
the interface is idle.

After a confirmed quit, the same artwork appears for two seconds, after project
saving and audio shutdown. Any key or click dismisses it sooner. Cancelled quits,
New Project, diagnostic runs, and offline renders do not show the farewell.
Set `exit_credits=0` in `tapesister.ini` to disable the farewell; on-demand credits
remain available. Existing configurations default to enabled.

The approved screen has one Credits heading and one continuous list:

| Credit | Work or role |
| --- | --- |
| Ehule/Sasna Kra | Concept & design |
| FastTracker II | Tracker |
| 8bitbubsy | FT2 clone / Tapehead |
| SunVox | Modules & MetaModules |
| CDP | Composers Desktop Project |
| ppooll | Project & contributors |
| Bahiamansa | Kafka / failure_v2 |
| SOMA Laboratory | Cosmos / ROAT / SimplexFM |
| Namkhai Norbu | Prism |
| ChatGPT / OpenAI | Development assistance |

The short display labels acknowledge origins and inspiration. Specifically,
8bitbubsy's [FT2 clone](https://github.com/8bitbubsy/ft2-clone) supplied the basis
for Tapehead, which preceded TapeSister; they do not attribute Tapehead's additions
or the original FastTracker II to 8bitbubsy. Bahiamansa's ppooll act Kafka inspired
Sister Machine, and failure_v2 inspired Fallout. SimplexFM is SOMA's alternate
[firmware for Metaconformer](https://somasynths.com/metaconformer_simplexfm/).
Namkhai Norbu is acknowledged for Prism's conceptual inspiration.

This artistic acknowledgment accompanies the complete software notices in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and the shipped `licenses/`
directory. Those notices retain individual authors, provenance and licenses.

The screen uses `assets/tapesister_credits.png`, the user-approved artwork.
Missing or corrupt artwork falls back to the same readable list rendered with
the native bitmap font. Builds stage the artwork alongside the executable;
portable Windows packaging checks it as a required asset.
