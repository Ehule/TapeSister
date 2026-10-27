# Project-save file preservation

## Verified starting point

Inspected `main` at `47d38395d89f83a3930495263a5fd7cfb8ab4c53` and the open
PR #156 (`52d176bc5143a9ff7b6940026aee7d8c23ad6f59`). Its save-related change is
in project loading, not bundle publication. This fix is based independently on main.

The previous saver validated a fresh sibling bundle, renamed the existing bundle
to `.tapesister-backup`, moved only `logs`, published staging, and recursively
deleted the backup. A standalone reproduction using the original main-branch
saver, linked against the same core, reported:

```
User-added file survives second save: NO
```

The same reproduction with this saver reports `YES`.

## Behavior

- User-added files and folders keep their relative locations, including entries
  inside `samples/` and `project-data/`, hidden files, empty directories, and logs.
  Entire unrelated folders are renamed without copying or decoding their contents.
- TapeSister owns individual generated files, not entire directories. Unchanged
  obsolete generated files are removed when pages, samples, REC BANK or Mosaic
  sources disappear. Unrecognized empty folders are conservatively retained.
- The existing version-2 manifest now records the exact generated file paths and
  FNV-1a 64-bit content checksums. Carried user files are never added to this list.
  Existing readers ignore the new fields; this does not change TSR, tracker, or
  Mosaic formats. The manifest itself remains application-managed.
- A generated file changed outside TapeSister is treated as user material on the
  next save. If the new project needs that path, Save fails with the conflicting
  relative filename. If it no longer needs the path, the changed file survives.
- A user file, folder, symlink or Windows reparse point that conflicts with a new
  output also stops Save before the old project is renamed. Move or rename that
  entry, or choose a different project destination, then retry. Overwrite
  confirmation does not override this protection.
- Old version-2 bundles without checksums migrate by inferring only exact
  generated files declared in their old manifest and Mosaic metadata, including
  EQ-dry WAVs and pattern score/cell/asset files. There is no directory wildcard.
  **Earlier external edits to those legacy generated files cannot be detected
  retroactively.** Back up such edits before the first save with this version.

## Transaction and recovery

The original staging writers and reload validation remain. Only after generation,
validation, inventory and conflict preflight does the old folder become the backup.
User entries move into staging with a reverse-order rollback journal. Renaming
preserves the moved entries' contents and filesystem identity, including links;
links and reparse points are never traversed. Shared generated directories may
be recreated, so their directory permissions/ACLs are not promised to survive.

If a user move or final publication fails, moved entries return to the backup and
the old folder is restored. If any rollback step fails, both recovery folders
remain, and Save reports that recovery is required. Any existing backup entry
(including a file, dangling link, reparse point, or inspection error) blocks the
next save before automatic staging cleanup. An unowned staging folder, a linked
staging root, or an invalid/symlink staging marker is also refused.

After a recovery-required message:

1. Keep both sibling `.tapesister-backup` and `.tapesister-partial` folders.
2. Copy both folders to a safe location before editing either. The backup holds
   the previous generated project; user entries may be split between them.
3. Recombine user entries with the previous project in a separate recovery copy,
   resolve duplicate paths explicitly, and open that project to check it.
4. Remove or relocate the two transaction folders only after checking the recovered
   copy. Merely deleting staging can delete the only copy of a user entry.

The journal is in memory; restart does not automatically merge an interrupted
transaction. Existing backup-cleanup failures likewise retain the backup and block
the next save. This preserves the prior manual-recovery policy.

## Regression coverage

`tapesister_project_save_tests` runs the actual public saver with failures injected
at filesystem rename boundaries. It covers:

- Repeated saves, reopened audio hashes, nested extras, hidden files, empty folders,
  user WAVs inside generated sample folders, and session logs.
- New generated filenames colliding with user files/folders, a user file replacing
  a required directory, and modified generated TSR/WAV content.
- Retirement of unchanged generated files while preserving edited files and extras
  inside a removed page's folder.
- Legacy manifest adoption, Mosaic PCM and EQ-dry sources, pattern score/cell/asset
  persistence, normal round trips, and stale legacy source cleanup.
- Staged reload validation failure, old-folder rename failure, failure after the
  first user entry moved, final publication failure, and successful retry.
- Failed entry rollback and failed root rollback, followed by another save attempt
  that must preserve both recovery folders and the user material.
- An actual POSIX child-process exit immediately after a user move, followed by a
  restart-equivalent save attempt and checks of all user entries across both folders.
- Unknown/invalid staging markers, ordinary files at the backup path, reserved-name
  conflicts, malformed ownership data, links, and dangling transaction links.
- POSIX open-log continuation and read-only-file permissions; a Windows file opened
  without delete sharing must fail safely and save successfully after it is closed.
  Windows symlink checks report a skip if the runner lacks creation privilege.

Existing Sample page tests retain multi-page/REC BANK/Sister-state round trips,
stale generated file removal, moved bundles, legacy layouts, and unrelated-folder
refusal. SisterTracker corruption tests now require Save to refuse external changes
before those tests explicitly delete their own corrupt fixtures.

Linux Debug core, TSR, Sample page, Bank, Sister-state, SisterTracker, Mosaic and
tracker persistence/playback suites are run from fresh fixture directories. The
focused saver and Sample page suites are also built with AddressSanitizer and
UndefinedBehaviorSanitizer (`detect_leaks=0`; LeakSanitizer cannot inspect processes
under this host's ptrace sandbox). Native release CI builds the application and runs the
new suite on Linux and Windows; the Windows fixture uses the application's 16 MiB
stack reserve.

## Limits

- Do not save the same bundle simultaneously from multiple instances or edit its
  files during a save. There is no interprocess filesystem lock or filesystem
  snapshot; checks and renames cannot prevent every concurrent external mutation.
- Content fingerprints detect accidental changes; they are not cryptographic
  authenticity checks. Keep regular independent backups.
- This retains the existing rename-based durability model, not a new guarantee
  against disk failure or power loss. No new filesystem fsync protocol is added.
- Existing Windows ANSI filesystem APIs and path-length limits remain. Inspection,
  naming, sharing or permission failures stop Save rather than authorizing deletion.
- Saving again with an older TapeSister build restores that build's destructive
  folder-replacement behavior, despite its ability to read the extended manifest.
