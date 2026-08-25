# TapeSister Sister Machine PR8-PR11 Roadmap

1. **PR8 - Soak/Bleed stereo weave and effect targets.** Establish the
   bidirectional fractional-delay weave, exclusive head-or-MIX target mask,
   independent per-destination history, Capture/feedback contract and reusable
   PR9 seam. Implemented and musically validated.
2. **PR9 - Post effects and explicit master feedback.** Add Reverb, musical Delay
   and Distortion with the shared yes/no target architecture, then add the explicit
   bounded master feedback return. This repository state implements PR9; see
   `SISTER_MACHINE_POST_EFFECTS.md`.
3. **PR10 - Dynamic buffer canvas.** Make rolling storage expand and contract while
   preserving realtime ownership, head validity and click safety. This repository
   state implements PR10 with a fixed 60-second allocation, live 5–60-second logical
   age window, permanent crop invalidation, coalesced requests, and linked handoffs;
   see `SISTER_MACHINE_LIVE_BUFFER_CANVAS.md`.
4. **PR11 - Pathological completed-system stress.** Combine feedback, all post
   effects, target switching, TILES/FM/EXT/AUDITION changes, Capture and live buffer
   resizing under sanitizers and older-hardware listening tests.

The ordering is intentional: validate the stereo primitive and routing vocabulary,
then complete the effect graph, then change storage structure, then stress the final
interactions rather than partial substitutes.
