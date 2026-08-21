# TapeSister / FT2 Tapehead exchange protocol

TapeSister and FT2 Tapehead exchange audio through self-contained folders under the
configured **FT2 Exchange Path**. Neither program links against the other's memory or
project state. A receiver polls the shared folder, stages a complete transfer, shows a
confirmation dialog, and changes its working set only after explicit acceptance.

## Atomic folder publication

The sender writes WAV files and the manifest into a folder whose name ends in
`.partial`. It writes the manifest last and then renames the whole folder to its final
name. Receivers ignore partial folders and folders without a valid manifest. Existing
transfer folders are never overwritten.

Version 1 remains the single-page reciprocal format. Version 2 is used only for a
TapeSister **All Pages** send and adds the `page_instruments` layout:

```text
TAPESISTER_EXCHANGE 2
sender=tapesister
recipient=tapehead
layout=page_instruments
count=2
item=1,1,1,P001_01_Kick.wav
item=1,2,1,P002_01_Bass.wav
```

Each page maps to one relative FT2 instrument and each TapeSister tile maps to the
same-numbered sample within that instrument. The repeated tile field is valid across
different relative instruments. Version 2 allows 1–255 pages and up to 16 items per
page. TapeSister does not accept version-2 manifests on import because its receiving
destination remains one 16-tile page.

The manifest file is named `exchange.tsexchange`. It is UTF-8-compatible ASCII with
this versioned line format:

```text
TAPESISTER_EXCHANGE 1
sender=tapesister
recipient=tapehead
layout=instrument_samples
count=2
item=1,0,1,01_Kick.wav
item=4,0,4,04_Noise.wav
```

Fields are:

- `sender`: `tapesister` or `tapehead`.
- `recipient`: the other application.
- `layout`: `instrument_samples` when the files belong to one FT2 instrument, or
  `separate_instruments` when each file belongs to a separate FT2 instrument.
- `count`: number of `item` rows, from 1 through 16.
- `item`: `tapesister_tile,ft2_instrument,ft2_sample,wav_filename`.

Tile, instrument, and sample numbers are one-based except that TapeSister uses FT2
instrument `0` in an outgoing `instrument_samples` transfer to mean "the receiving
dialog's selected destination instrument." A `separate_instruments` transfer uses
relative FT2 instrument positions 1 through 16; the FT2 receiver chooses the starting
instrument and validates the available range before import.

WAV filenames are basenames only and contain ASCII letters, digits, period, dash, or
underscore. Receivers reject absolute paths, separators, `..`, duplicate TapeSister
tile targets, missing WAVs, unsupported versions/layouts, and more than 16 items.

## TapeSister sending

The top **FT2 Link** button opens a choice instead of immediately launching FT2:

- **Page -> One**: occupied tiles retain their tile numbers as FT2 sample slots in
  one destination instrument.
- **Page -> Split**: each occupied tile becomes sample 1 of a relative FT2 instrument.
- **All Pages**: each Sample page becomes one relative FT2 instrument and each occupied
  tile keeps its number as that instrument's sample slot; this publishes version 2.
- **Check Inbox**: immediately scans for a Tapehead-to-TapeSister transfer.
- **Cancel**: closes the dialog without writing files or launching FT2.

Both programs refresh a small `.tapehead.running` or `.tapesister.running` marker in
the shared directory once per second. After a successful atomic publish, TapeSister
reuses a live Tapehead and lets its inbox poll discover the transfer. If no live marker
is present, it launches the configured FastTracker executable when that path is set.
Toggle **New Instance** before choosing a layout to deliberately launch another
Tapehead. A blank executable path leaves the completed transfer ready for manual import.

Every WAV carries TapeSister's standard `smpl` tuning and loop metadata. FT2 remains
responsible for validating destination instruments/sample slots and for applying the
chosen import as one undoable operation.

## TapeSister receiving

TapeSister scans the exchange path at startup and once per second while no dialog or
editing gesture owns input. A valid `sender=tapehead`, `recipient=tapesister` transfer
opens a staged confirmation showing the sample count, source layout, and folder name.

**Import** validates and decodes every WAV into temporary memory first. Only after the
entire batch and acknowledgement are ready does TapeSister replace its current bank.
Manifest tile numbers determine the 16 Tile destinations; both FT2 layouts become
ordinary independent editable TapeSister tiles. Tuning and supported loop metadata are
restored from the WAVs. **Later** leaves the transfer untouched and suppresses repeat
prompts for that same folder during the current session; **Check Inbox** can reopen it.

After a successful import TapeSister writes `tapesister.received` into the transfer
folder. Pending scans ignore acknowledged folders. An import failure leaves the current
TapeSister bank unchanged and does not acknowledge the transfer.

## FT2 Tapehead receiver requirements

The reciprocal implementation must use the version-1 format and safety model for
single-page transfers and version 2 for `page_instruments`.
It should poll or scan only the configured exchange folder, ignore its own outgoing
transfers, stage the newest complete unacknowledged TapeSister transfer, and show a
confirmation dialog before changing tracker state.

For `instrument_samples`, the dialog chooses one destination instrument and previews
the exact sample-slot mapping. For `separate_instruments`, it chooses a starting
instrument and previews each relative instrument destination. It must never silently
clear tracker memory or instruments, must reject out-of-range/conflicting destinations,
and should commit the accepted batch as one undoable transaction. On success it writes
`tapehead.received` beside the manifest.

For version-2 `page_instruments`, the dialog chooses a starting instrument, validates
that the complete page range and every sample slot fit, previews the page-to-instrument
mapping, and imports the whole offer as one undoable transaction. A Tapehead build that
does not implement version 2 must reject it explicitly without importing a partial page.

When sending to TapeSister, FT2 writes the same manifest with `sender=tapehead`,
publishes the folder atomically, and launches/focuses TapeSister only after publication.
