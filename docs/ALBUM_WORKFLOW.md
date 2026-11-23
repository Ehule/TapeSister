# Album projects

Open **MOVEMENTS** in the FX Canvas or **Gallery** in Mosaic (`Ctrl+G`). The
compact Gallery window can stay open with **PIN LIST**. A row's **FX** button
focuses its routing without switching the playing canvas. Double-click its name
or choose **ENTER** to use the normal Gallery transition. The sequencer continues
to reference stable canvas IDs.

Renaming a movement in Gallery updates its FX heading, sequencer display name,
and source cards whose names start with the old movement title followed by a
separator. The layer suffix stays intact; independently named cards are kept.
The operation refuses a title that would truncate a card's layer name. `Ctrl+Z`
and `Ctrl+Shift+Z` in Gallery or FX undo/redo the rename without restarting DSP.
Compact card labels omit the repeated movement prefix; selection/hover shows the
full source name. Source names and FX aliases both allow 63 characters.

**SAVE** opens the Project menu; the FX Canvas also has **PROJECT**:

| Action | Result |
| --- | --- |
| Save (`Ctrl+S`) | Confirm saving the active project. |
| Save As (`Ctrl+Shift+S`) | Choose another named project bundle. |
| Export ZIP | Export current edits and audio to a portable ZIP; keep the active project path and unsaved status. |
| Open Project Folder | Open the active project's containing folder. |

`Ctrl+Alt+S` opens the Project menu. ZIP export runs on the save worker and can
be cancelled; an existing destination is replaced only after successful export.
Extract the ZIP before opening its `.tsr`. ZIP64 stored entries avoid an extra
audio compression pass, so archive size is close to the native bundle size.

Right-click **CFG**, press `Ctrl+Alt+U`, or use **VIEW SIZES** in the FX Canvas to
adjust the Mosaic toolbar and source bank independently from 50–100% of their
normal size. Canvas zoom stays unchanged. Preferences persist in the local
configuration, and **Reset sizes** restores the original interface. Dialogs use
standard sizing.

The CPU readout also identifies **PLAYING**, **VOICES**, **LIVE INPUT**, **FX
RUNNING**, **IDLE**, or **NO OUTPUT**. **FX RUNNING** means the engine is enabled
without one of the listed playback sources; effects may still process tails or
other live sources. It is not a detection of audible sound. Click CPU for Health;
the profiler identifies the highest recently measured Matrix effect load.
