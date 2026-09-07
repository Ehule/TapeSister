# Straightforward CDP process audit

This pass adds 43 verified one-mono-sound-in/one-sound-out modes, taking Portal
from 88 to 131. It targets the remaining conventional command families with
scalar controls; it is not a claim that every CDP8 executable has been exhausted.
The next planned feature is reusable process chains. Further individual CDP
modes can be added alongside that work when their controls and behavior are clear.

## Included

| Source command group | Added | What it contributes |
| --- | ---: | --- |
| `focus accu/exag/focus/fold/step` | 5 | Spectral sustain, contrast, peak focus, octave folding and regular freeze steps |
| `hilite filter` modes 1–12 | 12 | Spectral high/low/band-pass and notch filtering, with ordinary, level-restored and explicit-gain variants |
| `hilite trace` modes 1–4 | 4 | Loudest-partial selection across the whole spectrum, above, below or inside a band |
| `hilite pluck/bltr` | 2 | Emphasise newly prominent partials; blur and trace together |
| `strange shift` modes 1–5 | 5 | Linear Hz shifts of all, above, below, inside or outside a spectral band |
| `strange glis` modes 1–3 | 3 | Shepard, inharmonic and self-glissando inside the source envelope |
| `strange waver` modes 1–2 | 2 | Oscillation between harmonic and inharmonic spectral relationships |
| `strange invert` modes 1–2 | 2 | Invert partial amplitudes relative to their observed maxima, optionally retaining the source envelope |
| `sfedit cut/cutend/excise`, seconds mode | 3 | Keep a segment, keep the tail, or remove an interior segment |
| `extend doublets` | 1 | Consecutive segment repetition, with native sync option |
| `extend loop` modes 1–3 | 3 | Advancing loops, requested output duration, or requested repeat count |
| `extend scramble` mode 1 | 1 | Random chunks with min/max duration and repeatable positive seeds |

The earlier batch already added 15 envelope modes and nine wavecycle modes,
including rises, falls, troughs, tremolo and repeated envelope shaping. This pass
therefore concentrates on structural edits and the remaining spectral territory.

## Source audit details

Command layout and ranges were checked against the supplied CDP8 source:
`dev/focus/ap_focus.c`, `focus.c`; `dev/hilite/ap_hilite.c`, `hilite.c`;
`dev/strange/ap_strange.c`, `strange.c`; `dev/editsf/ap_edit.c`;
`dev/extend/ap_extend.c`, its processing files; and
`dev/cdp2k/tklib1.c` with the constants in `dev/include`.

The legacy help for `focus exag` describes negative values, but native ranges
require a positive value and the implementation uses its reciprocal as a power.
Portal exposes that actual behavior: below one sharpens contrasts, above one
brings weaker partials forward. Similarly, spectral-filter Q is labelled as
skirt width in Hz, not resonance.

All spectral stages use the established 1024-point PVOC analysis with a 128-frame
hop. Bounds check Nyquist and relevant bin widths. Spectral step length must fit
two hops and the source; waver frequency must fit at least one cycle over the
source. Octave-fold bands must cover at least an octave.

Structure checks source times, splice room, segment lengths and conservative
output-size bounds. CDP's loop parser reserves 50 ms even if a smaller splice is
chosen. Loop-to-duration also needs room for a full loop after its start time.
Segment Repeats must leave at least 10 ms after a segment; setting its segment to
the entire input returned an invalid output file in native testing. Those cases
are rejected before rendering rather than offered as apparently working settings.

## Excluded or deferred

| Candidate | Reason / next requirement |
| --- | --- |
| `extend scramble` mode 2 (Shuffled Chunks) | Defaults rendered, but native endpoint tests produced buffer-pointer and negative-copy-length errors at scatter 0, longer output and larger splices. Excluded pending a separate native investigation. Mode 1 passed. |
| `modify revecho` mode 2 (Modulated Delay) | Earlier audio checks found silent dry output and ineffective seed changes. Remains excluded for separate investigation. |
| `focus freeze/hold`, `blur weave`, `hilite greq/band/vowels` | Require auxiliary schedules, breakpoint/text data or band/vowel definitions beyond scalar controls. |
| `blur shuffle` | Needs a domain-to-image mapping string and corresponding validation/UI, not just numerical parameters. |
| `sfedit cutmany/randcuts/randchunks/zcuts`, syllable splitting | Multiple-output/file-management workflows do not fit this pass. |
| `sfedit insert/replace/join/twixt/sphinx`, `extend sequence2` | Additional audio inputs or sequence definitions. |
| Remaining `extend zigzag/iterate/freeze/drunk/sequence/baktobak`, `blur drunk/scatter`, and specialist standalone tools | Some have scalar modes and remain valid future candidates. They need their own source-dependent duration, traversal and signal checks; they are not declared unsupported. They do not need to delay chain recipes. |
| Stereo and general breakpoint-driven CDP processing | Separate input/channel and automation work. The canvas stereo-gesture support does not change this catalog's mono contract. |

The runtime manifest adds `focus`, `hilite`, `sfedit`, and `strange`; portable
builds therefore receive the executables used by the new recipes. Existing
factory-bank definitions and saved process identities are retained.

## Verification

All 131 catalog defaults rendered successfully through the real CDP executable
pipeline. New-mode parameter endpoints were tested at 44.1 and 48 kHz: accepted
settings produced finite mono audio with valid output and clean job removal;
source-dependent/coupled settings were rejected before execution. Known-tone,
slice-content, repeat-duration, positive-seed reproducibility and cancellation
checks supplement the file-validity checks. The native Portal controller also
covers browsing and real previews, with existing keyboard, loop, collection and
Apply regressions retained.

The exact render harness is `tests/test_portal_final_batch.c`. Windows packaging
and real-hardware listening remain the user-facing release checks.
