# Follow-up: universal loop events and assignable identities

These are tracked design requirements, not capabilities implemented by the
cockpit PR. Every loop should receive a persistent identity, whether recorded
live or converted from an existing card. Existing audio/card edits, native
pattern cards and project compatibility are part of that goal.

The [loop-sequencer follow-up](MOSAIC_LOOP_SEQUENCER.md) now saves a separate
performance order and background membership while preserving existing Card
IDs. Shared player identities and linked launchers remain future work.

## Identity

- Preserve existing internal Card IDs and their MIDI targets.
- Give an independent player an immutable Loop ID. An editable visible number
  or name can be assigned without renumbering its underlying identity.
- Moving or reordering a card preserves identity and should not move playback.
- Explicit linked launch cards can refer to one existing Loop ID/player.
  Another encounter must not create a duplicate voice.
- Copy/split needs distinct choices: another linked launcher for the same full
  loop, or independently editable pieces with new Loop IDs. The original
  recording's lineage should remain available. Never infer sharing from equal
  source audio: identical recordings can be independent layers.
- Save identities, links and a deliberate performance order in the project;
  migrate existing live cards without breaking their Card-ID MIDI bindings.

## Ownership and conflicts to settle before linking

A shared player needs one defined source range, mix, mute/solo, route and clock
policy. Launcher-card controls and player controls must be distinguishable.
Deleting one launcher must not destroy a still-referenced loop or its audio.
Editing, copying, splitting, undo/redo and saving must retain valid references
and preserve a running player's sound until an explicit change is applied.

Keep tempo following, group transport and seek/cue following separate. Specify
how repeated timeline encounters, explicit Restart, finite length and loop
navigation affect a shared player. Retain the staging/validation/rollback save
path, and validate missing, duplicate and cyclic links during load.

## Universal conversion

The cockpit exposes the existing independent role for ordinary/imported audio
cards, preserves the original Timeline repeat flag, and rejects unsupported
directions instead of changing their sound. Live audio still uses forward
cycles with short edge fades; reverse, ping-pong, intro and overlapping-splice
semantics need their own playback/clock tests.

Native TrackSister pattern cards are explicitly excluded by the current launch
and load paths. They need independently owned native players, clear limits and
tests for edits, retriggers, clock changes, routes and saved project migration.
Simply enabling the audio live flag would make them invalid or silent.

The overall requirement remains: a clear Loop Event path for every supported
Mosaic card type, with an automatically assigned identity for converted cards
as well as captured loops. Each stage must state its supported types honestly.
