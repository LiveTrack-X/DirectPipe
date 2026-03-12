# DirectPipe Release Notes

## v4.0.0

### Code Architecture Refactoring

- **MainComponent split**: Reduced `MainComponent.cpp` from ~1835 lines to ~729 lines by extracting 6 focused classes:
  - `ActionHandler` (`Control/`) — Centralized action event handling (moved from MainComponent's 200+ line switch-case)
  - `SettingsAutosaver` (`Control/`) — Dirty-flag + debounce auto-save logic
  - `PresetSlotBar` (`UI/`) — Preset slot A-E buttons, naming, right-click menu
  - `StatusUpdater` (`UI/`) — Status bar updates (latency, CPU, format, notifications)
  - `UpdateChecker` (`UI/`) — Background GitHub release check + update dialog
  - `HotkeyTab`, `MidiTab`, `StreamDeckTab` (`UI/`) — Split from monolithic `ControlSettingsPanel`
- **ActionResult pattern**: New `ActionResult` struct (`ActionResult.h`) for typed success/failure returns with messages. Replaces bare `bool`/`void` returns on AudioEngine device methods. `static ok()` / `static fail(msg)`, `explicit operator bool()`.
- **onError callback pattern**: `std::function<void(const juce::String&)> onError` on `AudioSettings` and `OutputPanel`, wired to `MainComponent::showNotification()` for clean error propagation.

### Test Suite Expansion

- **52 → 110+ tests** across 6 host test suites (was 2):
  - `WebSocketProtocolTest` (30 tests) — expanded with state serialization, error handling, edge cases
  - `ActionDispatcherTest` (31 tests) — expanded with ActionResult integration, error paths
  - `ActionResultTest` (12 tests) — new: ok/fail factory, bool conversion, message propagation
  - `ControlMappingTest` (16 tests) — new: hotkey/MIDI/server serialization roundtrip, defaults, error handling
  - `NotificationQueueTest` (10 tests) — new: lock-free SPSC queue correctness, overflow, cross-thread
- **GTest dashboard integration**: `pre-release-dashboard.html` now loads GTest JSON output files for visual test result inspection.
- **GTest JSON output**: `pre-release-test.sh` generates `--gtest_output=json:` files for both core and host tests.

### Cross-Platform Support

- **Platform abstraction layer**: New `host/Source/Platform/` module with per-platform implementations (PlatformAudio, AutoStart, ProcessPriority, MultiInstanceLock). Replaces hardcoded Windows API calls with cross-platform interfaces.
- **macOS support (Beta)**: CoreAudio driver, LaunchAgent auto-start, CGEventTap hotkeys (requires Accessibility permission), Gatekeeper instructions.
- **Linux support (Experimental)**: ALSA/JACK drivers, XDG autostart, hotkey stub (use MIDI/HTTP/WebSocket instead).
- **Cross-OS backup protection**: Backup files (`.dpbackup`, `.dpfullbackup`) now include a `platform` field. Restoring a backup from a different OS is blocked with a warning dialog.
- **Platform-adaptive UI labels**: "Start with Windows" (Windows), "Start at Login" (macOS), "Start on Login" (Linux).
- **Plugin scanner cross-platform paths**: Scans OS-specific VST directories (Windows: `Program Files\...`, macOS: `/Library/Audio/Plug-Ins/...`, Linux: `/usr/lib/vst3/...`).
- **Plugin file browser**: Platform-specific file filters (`.dll` on Windows, `.vst3/.vst/.component` on macOS, `.vst3/.so` on Linux).
- **Preset deviceType validation**: Skips unavailable device types when importing presets (e.g., WASAPI preset on macOS).
- **CJK font per platform**: Windows (Malgun Gothic), macOS (Apple SD Gothic Neo), Linux (Noto Sans CJK).

### Build System

- **Core library**: Added POSIX library linking for macOS (`-lpthread`) and Linux (`-lrt -lpthread`).
- **Receiver plugin**: Added `JUCE_PLUGINHOST_AU=1` for macOS AudioUnit builds.
- **CMake**: Platform-conditional WASAPI/ASIO/CoreAudio/ALSA/JACK defines.

### Bugfix Ports from v3.10.1

- **VST Bypass fix**: `setPluginBypassed` now syncs `node->setBypassed()` + `getBypassParameter()->setValueNotifyingHost()` and calls `rebuildGraph(false)` to disconnect bypassed plugins in the signal chain. Fixes bypass not working for plugins with internal bypass parameter (Clear, RNNoise, etc.). `rebuildGraph` gains `suspend` parameter (default `true`) — bypass toggle uses `false` for glitch-free connection-only rebuild. Iterator safety fix (copy `getConnections()` before removal).
- **Preload cache race condition fix**: Per-slot version counter (`slotVersions_`) prevents background preload thread from storing stale plugin instances after `invalidateSlot` was called mid-preload. Version captured at file-read time, checked before cache store.
- **Cache structure validation**: `saveSlot` now checks plugin structure (names/paths/order) via `isCachedWithStructure` before deciding to invalidate cache. Parameter-only saves (bypass, state) skip invalidation. Prevents stale cache when plugins are added/removed/replaced/reordered.

### Bugfix Ports from v3.10.2

- **ASIO device iteration fix**: `setAudioDeviceType` now builds an ordered try-list (`tryOrder`: preferred → lastAsio → all remaining devices) and iterates through all available ASIO devices. Previously only tried `devices[0]` — if that device was disconnected, no other ASIO devices were attempted. Individual device failures log `warn` and `continue` to next device; full revert to previous driver only when all devices fail.
- **Driver combo sync on ASIO failure**: `AudioSettings::onDriverTypeChanged` now checks the `bool` return value of `setAudioDeviceType`. On failure (engine reverted to previous driver), syncs the driver combo to the actual current driver type via `getCurrentDeviceType()`. Fixes UI showing "ASIO" while engine was already back on WASAPI.

### Docs

- Platform Guide (new): OS-specific setup, features, and limitations.
- Pre-release dashboard: Platform selector (Windows/macOS/Linux) for OS-specific test items.
- All docs updated with cross-platform information.

---

## v3.10.0

### Multi-Instance & Portable Mode

- **Multi-instance external control priority**: Named Mutex system (`DirectPipe_NormalRunning`, `DirectPipe_ExternalControl`) prevents hotkey/MIDI/WebSocket/HTTP conflicts between instances. Normal mode blocks if portable has control. Portable runs audio-only if another instance already controls.
- **Audio-only mode indicators**: Title bar "(Audio Only)", status bar orange "Portable", tray tooltip "(Portable/Audio Only)".
- **IPC blocked in audio-only mode**: `setIpcEnabled(true)` silently ignored — prevents shared memory name collision between instances.
- **Quit button**: Red "Quit" button in Settings tab (next to auto-start toggle) for closing individual instances.

### Audio Engine

- **Driver type snapshot/restore**: Settings (input/output device, SR, BS) saved per driver type before switching. Automatically restored when switching back (e.g., WASAPI → ASIO → WASAPI preserves original WASAPI settings).
- **Per-direction device loss**: Input and output device loss handled independently.
  - `inputDeviceLost_`: Silences input in audio callback (prevents fallback mic audio leak).
  - `outputAutoMuted_`: Auto-mutes output on device loss, auto-unmutes when restored.
  - Edge-detection notifications for both directions.
- **Reconnection miss counter**: After 5 consecutive failed reconnection attempts (~15s), accepts current driver's devices to break cross-driver stale name infinite loops. When `outputAutoMuted_` is true (genuine device loss), the counter resets and keeps waiting indefinitely — fallback only applies to stale name issues, not physical disconnection.
- **Reconnection state cleanup**: `setAudioDeviceType` clears `deviceLost_`, `inputDeviceLost_`, `outputAutoMuted_`, and reconnect counters to prevent stale state after intentional driver switch.
- **Output "None" state on driver switch**: `outputNone_` cleared when switching driver types (prevents OUT mute button getting stuck after WASAPI "None" → ASIO). `DriverTypeSnapshot` now includes `outputNone` for save/restore across driver switches.
- **Device loss UI**: Input/output combos show "DeviceName (Disconnected)" when device is physically lost (instead of showing fallback device name). Clears when device is reconnected or user manually selects another device.
- **Manual device selection during loss**: `setInputDevice`/`setOutputDevice` now clear `deviceLost_`, `inputDeviceLost_`, reconnection counters — allows user to manually pick a different device during device loss without interference from reconnection logic.

### UI

- **LevelMeter**: Smoother release (kRelease 0.05→0.12, ~230ms half-life at 30Hz). Lower repaint threshold (0.001→0.0004) for finer visual response.
- **PluginChainEditor delete fix**: Delete confirmation now uses `slot->name` instead of `nameLabel_.getText()` (which includes row prefix "1. Clear"), fixing silent delete failure from name mismatch.

### Stability

- **PluginPreloadCache `cancelAndWait`**: Heap-allocated `shared_ptr` for join state — prevents dangling reference if detached joiner thread outlives caller's stack frame.
- **HttpApiServer thread cleanup**: Handler thread join moved outside `handlersMutex_` lock (deadlock prevention, same pattern as WebSocketServer).
- **StateBroadcaster hash**: `quickStateHash` now includes `inputMuted`, `sampleRate`, `bufferSize`, `channelMode`, and plugin `loaded` state for more accurate dirty detection.

---

## v3.9.12

- ASIO startup buffer size bounce fix (wrong device selection during type switch)
- `pushNotification` MPSC ring buffer fix
- MonitorOutput `alive_` flag lifetime guard
- AudioRingBuffer `reset()` ordering fix
- StateBroadcaster `slotNames` hash fix

## v3.9.11

- Output device switching fix (fallback false positive + input channel loss)

## v3.9.10

- ASIO buffer size persistence fix (`desiredBufferSize_` preserves user request)

## v3.9.9

- Slot naming (right-click Rename, `A|게임` display)
- Individual slot export/import (`.dppreset`)
- StateBroadcaster `slot_names` array

## v3.9.8

- Device fallback protection
- Hotkey drag-and-drop reorder
- MIDI HTTP API test endpoints
- MIDI Learn / Hotkey recording cancel buttons

## v3.9.7

- Instant preset switching (keep-old-until-ready)
- Settings scope separation (Save/Load vs Full Backup)
- Full Backup/Restore (`.dpfullbackup`)

## v3.9.6

- Device auto-reconnection (dual mechanism)
- Monitor reconnect
- Click-to-refresh device combos
- StateBroadcaster `device_lost`/`monitor_lost`

## v3.9.5

- WASAPI Exclusive Mode
- Audio optimizations (timeBeginPeriod, ScopedNoDenormals, RMS decimation)
- XRun monitoring
- 5 driver types

## v3.9.4

- Modal dialog fixes
- HTTP API input gain range
- Constructor SafePointer

## v3.9.3

- 25 bug fixes: thread safety, lifetime guards, server fixes, RT-safety

## v3.9.2

- Categorized logging
- LogPanel batch flush
- VSTChain lock-ordering fix

## v3.9.0 – v3.9.1

- Buffer display, SR propagation, monitor SR mismatch, Receiver VST SR warning

## v3.8.0

- Auto-updater UI, CJK font fix, download thread safety

## v3.7.0

- Plugin scanner fix (moreThanOneInstanceAllowed)

## v3.6.0

- IPC Toggle, Receiver VST buffer config, panic mute lockout

## v3.5.0

- NotificationBar, LogPanel, thread-safety audit (4 callAsync fixes)

## v3.4.0

- Tray tooltip, plugin search/sort, audio recording, settings save/load, MIDI param mapping, auto-updater
