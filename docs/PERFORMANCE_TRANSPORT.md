# Performance Transport

Open **CFG → Transport** or **Ctrl+Shift+Space**. Check ARP, SisterTracker and/or
Mosaic to choose the players controlled together. The choice saves with the
project. Older projects default to ARP only, preserving the previous shortcut.

![Performance Transport](screenshots/performance/performance-transport.png)

| Control | Behavior |
| --- | --- |
| Play | Starts checked players that are stopped |
| Stop | Stops checked players; keeps Tracker and Mosaic positions |
| Return to Start | Stops and rewinds checked players |
| Shift+Space | Stops the group if any checked player is running; otherwise starts it |
| Space | Keeps the active workspace's existing play/stop or Follow/live-edit behavior |

Closing the dialog leaves playback running. Unchecked players are independent.
If ARP lacks notes/playable audio or Mosaic is empty, group Play reports the
problem before starting the other players. Stop remains available. The ARP's
own Play/Stop buttons continue to control only ARP; group Stop releases ARP
voices without releasing manually held notes. ARP keeps its existing restart-from-pattern-start
behavior; it does not resume a partly played note.

The selected players are changed under one shared output exclusion, so they
start rendering at the same next audio callback boundary. Sample preparation
and tracker binding happen before that change. Running participants continue
from their current positions when Play brings another selected participant in.

This coordinates transport, not beat or tempo following. Mosaic retains its
seconds timeline and tape speed; SisterTracker retains its tempo and ARP its
step timing. No follower is selected, and synchronizing transport does not
change Mosaic pitch. Text-entry fields and modal dialogs keep shortcut ownership.
