# DirectPipe v4.0.6

This release focuses on Stream Deck responsiveness and Panic Mute state consistency.

## Highlights

- Stream Deck Panic Mute now sends explicit set-mode commands, avoiding accidental double toggles.
- Panic Mute restore now tracks pending restore state separately from the engine mute flag, so unmute can restore the previous output/monitor/IPC state on the first command.
- Stream Deck key visuals now force the matching Panic Mute image/title to avoid inverted muted/unmuted displays after profile cache refreshes.
- DirectPipe now throttles high-frequency telemetry broadcasts while still sending control-state changes immediately, reducing Stream Deck UI update load.
- The tray/menu bar icon now shows a red slash overlay while Panic Mute is active.
- README is refreshed for v4.0.6 with the DeepWiki badge and Windows downloaded-file unblock instructions.

## Upgrade Notes

- No breaking control API or state schema change.
- Windows users: if the downloaded executable is blocked, right-click `DirectPipe.exe` -> **Properties** -> **General** -> **Security** -> **Unblock** -> **Apply/OK**.

## Assets

- `DirectPipe-v4.0.6-Windows.zip`
- `DirectPipe-v4.0.6-macOS.dmg`
- `DirectPipe-v4.0.6-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`
