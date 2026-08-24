# TapeSister Sister Machine PR8-PR11 Roadmap

1. **PR8 - Soak/Bleed stereo weave and effect targets.** Establish the
   bidirectional fractional-delay weave, exclusive head-or-MIX target mask,
   independent per-destination history, Capture/feedback contract and reusable
   PR9 seam. This repository state implements PR8.
2. **PR9 - Post effects and explicit master feedback.** Add Reverb, musical Delay
   and Distortion with the shared yes/no target architecture, then add the explicit
   bounded master feedback return. PR8 does not provide a MIX return to tape.
3. **PR10 - Dynamic buffer canvas.** Make rolling storage expand and contract while
   preserving realtime ownership, head validity and click safety.
4. **PR11 - Pathological completed-system stress.** Combine feedback, all post
   effects, target switching, TILES/FM/EXT/AUDITION changes, Capture and live buffer
   resizing under sanitizers and older-hardware listening tests.

The ordering is intentional: validate the stereo primitive and routing vocabulary,
then complete the effect graph, then change storage structure, then stress the final
interactions rather than partial substitutes.
