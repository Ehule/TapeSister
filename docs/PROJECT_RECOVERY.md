# Interrupted project-save recovery

An interrupted save can leave sibling `.tapesister-backup` and
`.tapesister-partial` entries. The current project folder may also exist: a
save can publish successfully and then fail to remove its backup. These names
alone do not tell us which project is complete or preferred.

User-added files can be split between backup and staging if a save stops while
preserving them. Deleting either folder or blindly restoring the backup can
discard the only copy of a user file. TapeSister therefore keeps the original
save refusal and adds a guided, non-destructive recovery flow.

## The recovery dialog

Saving a project with a leftover backup or protected/unrecognized stage opens
**TapeSister project recovery**. The same dialog appears when opening a TSR in
a transaction folder through the file browser. A save that fails and leaves
recovery material also opens it immediately. Each location is shown in full,
with its type or “not found” status:

- **Current location** is the original project directory.
- **Backup location** may contain the previous save or a file obstructing that
  reserved name.
- **Staged location** may contain the interrupted new save and user files.

These labels describe locations, not measured validity or chronology.

| Choice | What happens |
| --- | --- |
| **Cancel — keep copies** | Cancels the pending file operation. The live session, its saved/dirty state and all disk copies stay as they were. Enter and Escape default to this choice. |
| **Save current as…** | Available on an attempted save. Opens Save As with a suggested `-recovered.tsr` name. It saves the in-memory project elsewhere; it does not gather extra files from the old folders. |
| **Copy all for recovery** | Copies every present location into a new sibling workspace, then opens the file browser there. The live session stays open until you explicitly select another TSR. The workspace path is copied to the clipboard when available. |
| **Copy paths** | Copies the locations and instructions to the clipboard, then returns to the dialog. |

The recovery flow never selects a winning version, merges user files, moves a
source folder, or deletes a source entry. It does not mark a failed/cancelled
save as successful, and it cancels any pending “save then quit/new” continuation.
If a native dialog cannot be shown, the operation is cancelled safely and its
instructions are offered through the clipboard/status message.

## Inspecting the copies

The workspace has a unique name, such as:

```text
My Project.tapesister-recovery-001/
  RECOVERY.txt
  current/
  backup/
  staged/
```

Only present locations are copied. A regular file obstructing a reserved path
is copied as a file at the corresponding workspace name. Empty directories and
unknown ordinary files are included. An existing workspace is never reused or
overwritten; another number is selected. `RECOVERY.txt` records the source paths
and explains the next steps.
The workspace root is private to its owner on POSIX systems; on Windows it
inherits the parent folder's access controls.

1. Inspect the available copied TSRs in the browser. Opening uses the ordinary
   project validation; copying itself does not certify a playable project.
2. Choose the project data you want to keep. Current and backup copies with
   valid manifests can be saved normally inside their copied folders. A copied
   stage retains its transaction marker: it can be opened for inspection, and
   Save offers Save As rather than silently discarding that marker.
3. Check **all** copied folders for notes, recordings, custom WAVs and other user
   material. Collect those files into the final working project folder yourself.
   If names conflict, keep both under different names until you decide.
   The audio/project browser filters other file types: paste the workspace path
   into your file manager's address bar to see notes and `RECOVERY.txt`.
4. Verify the working project, audio and extra files before any manual cleanup
   or relocation of the originals. The app provides no automatic delete/restore
   button for an ambiguous transaction.

Saving directly into an original transaction directory or a project nested
inside it is refused by the public saver. A legitimate project whose matching
folder and TSR are themselves named with a suffix-like name remains supported.
Copied project folders use ordinary names (`current`, `backup`, `staged`), and
the originals remain available throughout inspection and later editing.

## Copy failures and practical limits

The workspace starts with `RECOVERY_INCOMPLETE`. The marker is removed only
after all entries and instructions have been written and closed successfully.
A failure or interrupted copy keeps the marker, partial workspace and every
original. Retry creates another workspace; it never cleans the previous one.

Copying is synchronous on the control thread, outside the audio lock. It uses
bounded buffers and no audio-callback work, but can take time and needs enough
disk space for all locations. There is no in-copy cancel/progress worker. Stop
recordings and close other instances/external writers first; this is not an atomic filesystem
snapshot against concurrent external edits.

The copier handles ordinary file contents and folders. It refuses links,
Windows reparse points, special/unreadable entries, unsafe or overlong names,
and nesting beyond 64 levels. It does not archive permissions, timestamps,
alternate data streams or hard-link relationships; the originals retain those.
Existing narrow Windows path APIs remain a limitation: UTF-16 names that cannot
round-trip through the active code page cause a clear refusal rather than a
lossy copy. Windows writers/sharing locks can also prevent copying safely.

The normal saver's owned disposable stage cleanup is unchanged when no backup
or preservation marker needs attention. Staging, reload validation, user-file
conflict checks and rollback are unchanged. The UI and copier do not replay the
lost in-memory move journal or strengthen the underlying filesystem's power-loss
durability guarantees.

## Validation

The project-save test exercises real failed-rollbacks with split user files,
backup-only restart, a published current plus a distinct backup, and a regular
file obstructing the backup path. It checks clone hashes, reloads both project
versions, preserves empty/custom folders, saves a copied backup independently,
and covers Mosaic pattern assets and EQ source round-trips.

Failure cases cover destination collisions, an injected output-open failure,
retained incomplete workspaces, path/nesting limits, original links, guarded
transaction/nested saves, and legitimate suffix-like project names. Windows
adds sharing-lock and representable/unrepresentable Unicode filename cases.
Existing validation, rollback, ambiguous-rename and process-interruption tests
continue to run.

The native controller test interposes only the SDL dialogs. It drives the real
pending file-operation path and checks Cancel, Save As, Copy All, Copy Paths,
dialog failure, browser navigation, unchanged live project/dirty state and
cancelled quit/new continuations. These are control/state tests; native dialog
appearance varies with the desktop and was not visually verified headlessly.
It also copies a folder with a damaged project-name manifest field unchanged;
ordinary project validation then refuses that copied TSR without replacing the
live session. Copying does not depend on trusting the manifest's project name.

Build and run the relevant cases in an ordinary SDL/FFmpeg environment:

```sh
cmake --build build --target tapesister_project_save_tests tapesister_new_project_tests
ctest --test-dir build --output-on-failure -R '^(tapesister_project_save_tests|tapesister_new_project_tests)$'
```

The PR's native Linux and Windows Release jobs also run the broader regression
and packaging suites. Consult the PR checks for their final result. Process and
filesystem-failure fixtures are not physical power-cut or storage-failure tests.
