# Saving and reopening a project

A successful project save includes Mosaic card content and properties, mute and
solo states, routing, sample banks, the tracker definition and Sister settings.
Unknown ordinary files and folders inside an existing named bundle are retained
under the project-save preservation rules. A save must not be judged successful
merely because those extra files survive: the project data must round-trip too.

## Saved editor location

The optional `ProjectView` section in `sister-state.ini` records the editor
location at the time Save is requested:

- Main workspace: Canvas, Mosaic or TrackSister.
- Canvas panel choice and parent waveform viewport.
- Mosaic vertical/horizontal scroll and zoom, playhead position, follow switch,
  sample/pattern browser tab and page, primary card and ordered card selection.
- Host tracker scroll, expansion and field; the embedded tracker already stores
  its own editor position with its score.

The selection survives the first controller refresh after loading. Mosaic opens
paused at the stored position. Runtime voices and live audio buffers are not a
session-resume snapshot. Temporary dialogs, uncommitted working documents,
FM/Portal workspace focus and auxiliary window placement are not restored by
this section. Complete Mosaic card edits through the existing apply/create
choice before saving. View navigation alone does not mark the musical project
dirty; use Save to update the stored view.

The section has its own version and is staged, parsed and validated with the
existing Sister sidecar before publication. Invalid, incomplete or duplicate
records reject the load before replacing the live project. Older projects
without this section still load normally, but have no saved viewport to recall.
The Sister state version is 44; bundle and Mosaic format versions are unchanged.

## Older flat TSRs and save locations

An older project can be a standalone `Example.tsr`, with companion data in
`Example.tsr.samples`. Saving it uses the existing migration to the named bundle
`Example/Example.tsr`. The original flat file and companion folder are retained.
They do not become the newly saved version. Files around a legacy flat project
are not automatically gathered into that bundle.

The confirmation now says **Save in new project bundle?**, **The original TSR
will be kept**, and **Save bundle** when this migration is needed. For an existing
named bundle it continues to offer Overwrite. After a successful save, the file
browser points to the actual saved bundle and selects its TSR for the next Open.
An explicitly chosen older file remains independently openable.

F12 session logs now record save/open paths, Mosaic hashes, card/mute/solo counts
and workspace/viewport values in `project_save_request`, `project_saved` and
`project_opened` events. These details are part of the local log and its exported
copies. They help distinguish reopening another file from a serialization loss;
they do not contain the project audio.

## Evidence and validation

The reported Windows log recorded a successful save, 96 Mosaic cards and 20
muted/zero solo cards. It contained neither saved-file contents nor paths, and
ended at the save snapshot before a later reopen. It therefore does not establish
why the reported musical edits disappeared.

A new native controller regression overwrites a 96-card project after actual
M/S keyboard actions and changes to card geometry, name, gain, pan and fades.
The content, mute/solo states and extra files round-tripped before the view fix;
its saved-workspace/viewport assertion failed. That same assertion now passes,
including after a New project and fresh Open, primary/multiple selection, zoom,
scroll and paused seek position. Canvas and TrackSister workspace restoration
and malformed-view load isolation are also covered.

A separate native controller case loads a real flat TSR, edits Mosaic, saves
through the active-project controller, reopens the selected saved bundle through
the browser and verifies its content. The older flat file remains readable with
its original sample content. It checks both migration and ordinary overwrite
confirmation choices. Core sidecar tests cover precision/extreme valid zoom,
invalid save refusal without truncating a good file, malformed/incomplete/schema
and duplicate-selection records, and older sidecars without view data.

These are host and CI measurements, not tests on the user's own computers.
Native desktop dialog appearance and the originally reported musical-edit loss
still need a user retest. The recovery flow and limitations are described in
[PROJECT_RECOVERY.md](PROJECT_RECOVERY.md).
