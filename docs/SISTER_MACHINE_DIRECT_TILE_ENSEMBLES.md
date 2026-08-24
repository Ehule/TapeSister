# Sister Machine PR7 direct tile ensembles and live source editing

PR7 removes the modal main-bank `SISTER SRC` workflow. The ordinary Sample Bank now has
one occupancy-sensitive Shift-click contract:

| Target | Shift-click result |
| --- | --- |
| Empty tile | Copy Current to the empty destination; leave it unmarked |
| Occupied, unmarked tile | Add it to the current page's Sister mask |
| Occupied, marked tile | Remove it from the current page's Sister mask |

Copy still rejects occupied destinations and invalid Current material. Source toggling
never selects or auditions the tile, never writes its sample, and remains legal for an
occupied protected tile. Plain click selects the active canvas and never changes the
mask. Capture's armed transient Shift-click group remains a separate contextual workflow.

## State and border language

`TsSisterRuntime::page_source_masks` remains the only Sister membership store. PR6's
project companion saves all page masks; presets do not. The Sister window's TILES switch
is the master insert/bypass and never clears membership.

An inactive ordinary tile has no extra outline. Active-only uses `ActiveTile`. A source
uses one split perimeter: `SisterSourceHorizontal` on top/bottom and
`SisterSourceVertical` on left/right. When active and source coincide, the split source
perimeter remains outermost and one inset `ActiveTile` outline identifies the canvas.
These borders are redrawn after protected-tile dimming. Legacy palettes default the new
roles to `PatternNote` and `PatternEffect`.

## Performance and routing

One MIDI or QWERTY `TsNoteEvent` is preflighted as an indivisible group, then starts one
voice for every valid marked tile. The fixed pool rejects the entire group when capacity
is insufficient; it never plays an arbitrary subset. Every admitted group stores one
linked `1/sqrt(member count)` gain for its lifetime. Mono is exact dual mono, stereo uses
one pitch/phase/envelope with independent L/R reads, and channels do not increase N.
Different-length one-shots end independently without renormalizing the survivors.
Looped Note Off, Loop Lock, overlapping retriggers, and panic retain their established
contracts.

With Sister POWER and TILES enabled, occupied-tile plain click is selection-only. It
cannot create a `legacy_preview` voice around the TILES insert. MIDI/QWERTY feed the
named tile-performance bus, which is removed from the direct mixer and returned exactly
once by Sister. AUDITION remains the independent named canvas/waveform preview source.
POWER off or TILES off preserves ordinary click audition.

## Immutable live-edit generations

When TILES owns the performance and no legacy preview or ordinary bank voice is reading
Current, the UI renders Warp, Smear, and Tear while the callback continues on its old
immutable source. The controller prebuilds the validated replacement generation, then
takes the audio-device lock only for bounded voice-metadata publication. Other workflows
retain their established device-lock protection. Callback voices retain generations with atomic reader
counts; retired generations are reclaimed only outside the callback after the last voice
releases them. The callback never allocates, frees, logs, blocks, or performs file I/O.

One-shots finish the generation from which they started and the next trigger uses the
replacement. A repeating voice stages both channels and adopts the new generation at a
loop boundary. It maps the new loop range deliberately, restarts at the matching boundary,
and uses a five-millisecond equal-power linked stereo crossfade from the old loop. Frame
count changes therefore cannot reuse an invalid old index. A failed render or generation
clone leaves the current playback generation intact.

Warp applies one deterministic displacement per frame to both channels. Smear uses the
same window/hop timing while preserving independent channel spectra. Tear derives one
zero-crossing packet map and applies the identical frame permutation to L and R. All
three preserve interleaving and reject channel shapes other than mono or stereo.
