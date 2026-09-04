# Windows audio validation

Run this checklist on physical Windows x64 hardware before release sign-off. Automated
tests cover configuration, fallback policy, logical-to-real ID translation, idempotent
removal, capture/output independence, and pre-initialization ordering; they cannot
prove driver behavior, unplug handling, sleep/wake behavior, or ASIO coexistence.

## Preparation

Record the TapeSister commit, Windows version, SDL version, interface/driver version,
and VB-CABLE version. Use the supported MSYS2 UCRT64 build:

```bash
bash build.sh
```

For each run, retain stderr and `tapesister-diagnostic.log`. Confirm that diagnostics
show the configured and active backend, configured and active device names, real SDL
IDs, sample rate, format, channel count, buffer size, fallback status, connection
state, and exact failure/reopen error.

Use 44.1, 48, and 96 kHz where the devices support them. Repeat representative cases
with 256, 512, and 1024 frame buffers and with matching and mismatched application
rates/buffers.

## Matrix

- [ ] TapeSister alone with backend WASAPI.
- [ ] TapeSister alone with backend DirectSound.
- [ ] TapeSister alone with backend Auto.
- [ ] Tapehead and TapeSister both using WASAPI; test both launch orders.
- [ ] Tapehead routed through VB-CABLE into TapeSister.
- [ ] Tapehead → VB-CABLE → TapeSister; test both launch orders.
- [ ] TapeSister while REAPER uses WASAPI shared mode; test both launch orders.
- [ ] TapeSister while REAPER uses ASIO; test both launch orders and record any
  hardware-driver exclusivity rather than describing ASIO as a TapeSister backend.
- [ ] Remove capture during EXT playback, then reconnect it. Internal tiles, FM,
  audition, and Sister sources must continue; stale ring audio must not replay.
- [ ] Remove output during playback, then reconnect it. The UI must remain responsive,
  the configured device must be retried, and no other output may open silently.
- [ ] Start with a named output unavailable. Verify the exact error and all four choices:
  Retry, approved temporary default, continue without output, and Exit. Verify the INI
  still contains the named device after temporary fallback.
- [ ] Start with a named capture unavailable. EXT/record/monitor must be unavailable,
  internal playback must work, and no default capture device may open silently.
- [ ] Restart TapeSister while Tapehead, REAPER, and/or VB-CABLE remain running.
- [ ] Repeat coexistence cases with matching and mismatched sample-rate/buffer settings.
- [ ] Disconnect/reconnect each device repeatedly. Confirm there is no double close,
  duplicate stream, crash, callback stall, or misleading success message.
- [ ] Sleep and wake Windows with both endpoints active, then with capture closed.
  Confirm deterministic state and recovery.

## Pass criteria

- A named device is never replaced without explicit approval.
- Temporary output fallback is visible and does not overwrite configuration.
- Output loss is recoverable and never freezes the interface.
- Capture loss affects only capture-dependent features.
- Reconnection targets the configured device and resets capture ring/resampling state.
- Device events recover even while another window or workspace is active.
- Healthy-device audio, smoothing, pedalboard behavior, Sister processing, recording,
  limiter behavior, and MIDI remain unchanged.

WASAPI shared mode is the recommended coexistence baseline. Native ASIO is not
implemented by TapeSister, and an ASIO driver's exclusive or single-client limitation
cannot be repaired inside TapeSister.
