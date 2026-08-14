# Six-operator FM Source plan

## Existing sound engine retained

TapeSister's four established source kinds remain unchanged:

- Tonal combines seeded pitch drops, harmonic/inharmonic partials, decaying modulation, and a small noise body.
- Metallic uses inharmonic ratios, fast sweeps, auxiliary partials, decaying FM, and short noise attacks.
- Noise combines colored noise motion, sparse impulses, and a pitched resonant body.
- Pulse supplies hard-edged duty-cycle, saw, and impulse-like transients.

These are useful TapeSister materials, not placeholders for FM. Existing direct renders keep their established deterministic path.

## First architectural proof

FM is an additive fifth source kind. Its hidden six-operator patch is deterministically derived from the generator recipe seed, so TSR12 retains the transient candidate and the seed and kind fields needed to reproduce it. TSR6–TSR11 remain loadable.

The proof includes six curated structures:

- Chain: one long modulation stack into one carrier.
- Branch: a forked tree returning to one carrier.
- Twin: two independent stacks and two carriers.
- Parallel: three modulator/carrier pairs.
- Strike: a feedback-driven stack feeding three percussion carriers.
- Cluster: three cross-colored carrier branches.

It also includes six curated ratio families: Harmonic, Fifths, Subharmonic, Clustered, Metallic, and Mixed. Seeded Depth, Shape, feedback, and transient mix complete the hidden patch. The transient layer reuses the established noise-attack behavior around an FM body instead of making TapeSister a pure FM synthesizer.

FM currently participates in Radical source cycling. With Variation set to Radical and Chain enabled, repeated Create actions walk through the existing source kinds and FM; Vary creates a new deterministic FM patch when FM is the active Radical source. Close and Wide continue to operate safely in the proven rendered-sample variation path.

## Staged follow-ups

1. Expose Structure, Ratio, Depth, and Shape as four compact source macros without adding an operator table to the primary interface.
2. Store explicitly adjusted macro values while preserving seed-derived defaults and TSR11 compatibility loading.
3. Make Close, Wide, and Radical vary hidden FM state intelligently rather than relying only on rendered-sample variation.
4. Curate additional structures and ratio sets by listening tests, with special attention to bells, metal, percussion, bass, and unstable textures.
5. Add an optional advanced view for operator envelopes, routing, ratios, feedback, and macro mapping.
6. Begin separate CDP8 Transform slices only after Source and Variation are musically stable.

Every stage must retain deterministic rendering, Parent/Source ownership, collection behavior, waveform editing, tuning, loops, recipes, and FT2 handoff.
