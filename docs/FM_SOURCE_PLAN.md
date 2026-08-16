# Six-operator FM tile plan

## Existing sound engine retained

TapeSister retains its four legacy generator kinds for compatible project loading:

- Tonal combines seeded pitch drops, harmonic/inharmonic partials, decaying modulation, and a small noise body.
- Metallic uses inharmonic ratios, fast sweeps, auxiliary partials, decaying FM, and short noise attacks.
- Noise combines colored noise motion, sparse impulses, and a pitched resonant body.
- Pulse supplies hard-edged duty-cycle, saw, and impulse-like transients.

These remain useful internal render paths. Normal Create now produces an FM tile.

## First architectural proof

An FM tile's hidden six-operator patch is deterministically derived from its generator recipe seed and stored FM state.

The proof includes six curated structures:

- Chain: one long modulation stack into one carrier.
- Branch: a forked tree returning to one carrier.
- Twin: two independent stacks and two carriers.
- Parallel: three modulator/carrier pairs.
- Strike: a feedback-driven stack feeding three percussion carriers.
- Cluster: three cross-colored carrier branches.

It also includes six curated ratio sets: Harmonic, Fifths, Subharmonic, Clustered, Metallic, and Mixed. Seeded Depth, Shape, feedback, and transient mix complete the hidden patch. The transient layer reuses the established noise-attack behavior around an FM body instead of making TapeSister a pure FM synthesizer.

Create renders FM into the selected tile. Vary changes the selected FM recipe according to Range. With Chain off it replaces that tile; with Chain on it writes to the next empty tile and selects the result so the next Vary continues the chain.

## Staged follow-ups

1. Expose Structure, Ratio, Depth, and Shape as four compact macros without adding an operator table to the primary interface.
2. Store explicitly adjusted macro values while preserving seed-derived defaults and older-project compatibility loading.
3. Refine Range so it varies hidden FM state musically across its full travel.
4. Curate additional structures and ratio sets by listening tests, with special attention to bells, metal, percussion, bass, and unstable textures.
5. Add an optional advanced view for operator envelopes, routing, ratios, feedback, and macro mapping.
6. Begin separate CDP8 Recipe slices only after FM Create/Vary and native Ingredients are musically stable.

Every stage must retain deterministic rendering, independent tile ownership, waveform editing, tuning, loops, recipes, and FT2 handoff.
