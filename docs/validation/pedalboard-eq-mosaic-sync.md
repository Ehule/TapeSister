# Pedalboard, EQ and shared source cards

The Pedalboard's number between `-` and `+` is the number of slots to add or
remove. It starts at one; wheel over the number to change it. Compact `UP`,
`DN` and `X` controls reorder or remove an individual slot. Removing a middle
slot shifts the following slots while preserving their identities. A board
can shrink to one slot; `X` on the last slot clears it. Ctrl+Z restores edits.

`EQ` is a native five-band insert available as a Pedalboard slot and a standalone
FX Canvas card. `EDIT EQ` or double-click opens its own curve. Mix, output gain,
bypass, presets and project recall are supported. EQ can be consolidated into
a Pedalboard; MetaFX consolidation reports that this processor is unsupported.

The fixed Master EQ card also opens on double-click. Master and insert EQ use
a separate, resizable, maximized, always-on-top window. Alt+Enter toggles full
screen. Escape or the window close button closes only EQ and returns focus to
the caller. Insert curves never modify Master EQ. A removed/replaced processor
cannot redirect an open editor to a different slot.

Mosaic cards own their names and colors. F2 or right-click > Name / Color opens
the same editor from either canvas. Mosaic audio/pattern card bodies open the
menu; handles keep their reset gestures and video cards keep video settings.
F2 edits video names/colors too (outside QWERTY source-play mode). Existing projects keep Mosaic's name and
automatic source color, overriding old independent FX Canvas aliases/colors.
Custom colors are stored in Mosaic formats 24 (WAV) and 25 (shared assets);
projects without custom colors retain their previous format. Source cards in
FX Canvas support M (mute), Alt+S (solo), and matching context-menu commands.
Shift applies mute/solo to cards sharing the selected source in that movement.

Default cables use their source card's effective color and follow subsequent
color edits. Explicit cable overrides remain independent. Resetting a cable's
color restores inheritance. Selection/focus highlights still apply.

`tapesister_editor_sync_tests` covers native EQ DSP equivalence, project/preset
recall, one-slot boards, batch add, middle deletion/undo, shared names/colors,
mute/solo, WAV/shared-asset recall, movement-qualified identities, cable color
inheritance, native EQ return behavior and Master EQ double-click. It is included
in the Windows and Linux PR regression manifest.
