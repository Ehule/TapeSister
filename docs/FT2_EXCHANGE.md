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

- **One Instrument**: occupied tiles retain their tile numbers as FT2 sample slots in
  one destination instrument.
- **Separate Instr**: each occupied tile becomes sample 1 of a relative FT2 instrument.
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

The reciprocal implementation must use this exact version-1 format and safety model.
It should poll or scan only the configured exchange folder, ignore its own outgoing
transfers, stage the newest complete unacknowledged TapeSister transfer, and show a
confirmation dialog before changing tracker state.

For `instrument_samples`, the dialog chooses one destination instrument and previews
the exact sample-slot mapping. For `separate_instruments`, it chooses a starting
instrument and previews each relative instrument destination. It must never silently
clear tracker memory or instruments, must reject out-of-range/conflicting destinations,
and should commit the accepted batch as one undoable transaction. On success it writes
`tapehead.received` beside the manifest.

When sending to TapeSister, FT2 writes the same manifest with `sender=tapehead`,
`recipient=tapesister`, maps each exported WAV to a unique TapeSister tile 1 through 16,
publishes the folder atomically, and launches/focuses TapeSister only after publication.
