# Prism performance bank and Morph Matrix

Prism now stores twelve independent sound states **A–L** with two selected morph
ends. Wheel browsing is silent, a click confirms the next destination, right-click
recalls a letter for editing, and middle-click clears only that letter. The same
manual/MIDI fader and shared TIME control perform each transition. See the
[bank controls](PRISM.md#twelve-captured-states-al-and-morph) for the full workflow.

Prism presets, full Sister presets and projects save all twelve states, selected
ends and parked morph position. The versioned format still reads older A/B files.
Transitions stay on the audio clock; UI choices are published only on confirmation.
New/Vary and ordinary preset auditions preserve every stored state.

The [64-step Morph Matrix](PRISM_MORPH_MATRIX.md) now uses these state identities
as destinations. It adds one 8×8 pattern, step duration, Play, Stop/Reset and Loop.
Bank drafts can be edited during playback and saved for the next occurrence.
The current transition keeps two frozen endpoints, and manual takeover holds its
exact blend without overwriting a bank letter.
