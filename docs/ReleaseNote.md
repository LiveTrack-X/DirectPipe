# DirectPipe Release Notes

> 이 문서는 **사용자 대상** 릴리스 요약입니다. 개발자 상세 변경 이력은 [CHANGELOG.md](../CHANGELOG.md) 참조.
> This is a **user-facing** release summary. For detailed developer change history, see [CHANGELOG.md](../CHANGELOG.md).

## v4.0.2

### Highlights / 주요 변경

- **Independent Input Mute / 독립 입력 뮤트**: INPUT 전용 뮤트를 추가/정리하여 입력만 무음 처리하고 체인/출력 경로는 유지합니다. Added dedicated INPUT mute behavior that silences mic input only while keeping chain/output paths running.
- **Panic Mute Clarification / 패닉 동작 명확화**: Panic은 OUT/MON/VST 차단 + 녹음 자동 중지를 유지하고, 해제 시 사용자 뮤트 상태를 복원합니다. Panic still blocks OUT/MON/VST and stops active recording, then restores user mute states on unmute.
- **State Model Update / 상태 모델 갱신**: `active_slot`을 `0-5`(`5=Auto`, `-1`=none)로 통합했고 `auto_slot_active`는 deprecated입니다. Unified `active_slot` to `0-5` with `-1` as none; `auto_slot_active` is now deprecated.
- **XRun QoL / XRun 운용성 개선**: 60초 윈도우 드리프트를 수정하고 CPU/XRun 라벨 클릭 리셋을 추가했습니다. Fixed 60-second drift and added click-to-reset on CPU/XRun label.
- **SR-safe NR timing / SR 안전 NR 타이밍**: NR hold/smoothing이 런타임 SR 기준으로 재계산됩니다. NR hold/smoothing now recalculates from runtime sample rate.

### Upgrade Notes / 업그레이드 안내

- **API clients / API 연동**: `active_slot` (`0-5` / `-1`)을 기준으로 사용하고 `auto_slot_active`는 하위 호환용으로만 유지하세요. Prefer `active_slot` as source of truth and keep `auto_slot_active` only for backward compatibility.
- **Control integrations / 제어 연동**: `input_muted`와 panic `muted`는 독립 상태입니다. `input_muted` is independent from panic `muted`; do not assume mirrored state.

---

## v4.0.1

### Bugfixes

- **NoiseRemoval**: Ring buffer uint32_t overflow causing permanent silence after ~25h continuous use
- **HTTP API**: Strict numeric validation — reject mixed alpha-numeric input (e.g., "abc0.5")
- **UI**: Plugin chain editor negative height on very small window
- **State Broadcast**: activeSlot clamped to 0-4, added `auto_slot_active` field for WebSocket/SD clients
- **Linux**: Complete XDG Desktop Entry Exec key character escaping per spec
- **macOS**: Notify user when hotkey accessibility permission not granted
- **Linux**: Show "unsupported" message in Hotkeys tab instead of non-functional UI
- **HTTP API**: Escape JSON special characters in API responses
- **Platform**: AutoStart setters return bool, notify user on failure
- **IPC**: Restrict POSIX semaphore/shm permissions to owner-only (0600)
- **XRun Tracking**: Device restart no longer clears XRun history — display persists for full 60s window
- **Audio RT**: Moved MMCSS LoadLibraryA call from RT callback to audioDeviceAboutToStart
- **MIDI**: Fixed LearnTimeout use-after-free when timer callback destroyed itself
- **Preset**: Added loadingSlot_ guard to Reset Auto to prevent intermediate state auto-save
- **Audio**: Recorder now stops on device loss (audioDeviceStopped) to prevent WAV corruption
- **Preset**: Auto first-click failure now sets partialLoad_ to prevent saving incomplete chain
- **Preset**: pendingSlot_ cleared on Factory Reset / Clear All Presets
- **State**: Plugin name now included in quickStateHash — name changes trigger WebSocket broadcast
- **WebSocket**: broadcastToClients releases clientsMutex_ before socket writes — prevents slow client blocking shutdown
- **IPC**: Receiver VST now calls detach() before close() on failed producer check — fixes spurious multi-consumer warning
- **Thread Safety**: audioDeviceAboutToStart non-atomic variable writes deferred to message thread via callAsync
- **Update**: PowerShell batch script now escapes single quotes in paths
- **Audio**: Added null guard on outputChannelData for ASIO legacy drivers
- **Preset**: parseSlotFile now uses .bak fallback for crash resilience
- **UI**: Plugin removal now identifies by index+name, not name-only (fixes duplicate-name removal)
- **WebSocket**: Sends RFC 6455 close frame (1009) before disconnecting oversized-message clients
- **Settings**: importAll/importFullBackup now check platform compatibility internally
- **Platform**: macOS/Linux AutoStart uses atomicWriteFile for crash-safe writes

---

## v4.0.0

### Code Architecture Refactoring

- **MainComponent split**: Reduced `MainComponent.cpp` from ~1835 lines to ~729 lines by extracting 7 focused classes:
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
- **CI/CD**: macOS build artifact changed from `.zip` to `.dmg` — prevents v3 auto-updater from downloading wrong binary (updater looks for `.zip` → `.dmg` is invisible to it).

### Bugfix Ports from v3.10.1

- **VST Bypass fix**: `setPluginBypassed` now syncs `node->setBypassed()` + `getBypassParameter()->setValueNotifyingHost()` and calls `rebuildGraph(false)` to disconnect bypassed plugins in the signal chain. Fixes bypass not working for plugins with internal bypass parameter (Clear, RNNoise, etc.). `rebuildGraph` gains `suspend` parameter (default `true`) — bypass toggle uses `false` for glitch-free connection-only rebuild. Iterator safety fix (copy `getConnections()` before removal).
- **Preload cache race condition fix**: Per-slot version counter (`slotVersions_`) prevents background preload thread from storing stale plugin instances after `invalidateSlot` was called mid-preload. Version captured at file-read time, checked before cache store.
- **Cache structure validation**: `saveSlot` now checks plugin structure (names/paths/order) via `isCachedWithStructure` before deciding to invalidate cache. Parameter-only saves (bypass, state) skip invalidation. Prevents stale cache when plugins are added/removed/replaced/reordered.

### Bugfix Ports from v3.10.2

- **ASIO device iteration fix**: `setAudioDeviceType` now builds an ordered try-list (`tryOrder`: preferred → lastAsio → all remaining devices) and iterates through all available ASIO devices. Previously only tried `devices[0]` — if that device was disconnected, no other ASIO devices were attempted. Individual device failures log `warn` and `continue` to next device; full revert to previous driver only when all devices fail.
- **Driver combo sync on ASIO failure**: `AudioSettings::onDriverTypeChanged` now checks the `bool` return value of `setAudioDeviceType`. On failure (engine reverted to previous driver), syncs the driver combo to the actual current driver type via `getCurrentDeviceType()`. Fixes UI showing "ASIO" while engine was already back on WASAPI.

### Bugfixes

- **Panic mute comprehensive blocking**: All action cases in `ActionHandler::handle()` now check `engine_.isMuted()` — PluginBypass, MasterBypass, InputGainAdjust, SetVolume (input), SetPluginParameter, LoadPreset, SwitchPresetSlot, NextPreset, PreviousPreset, RecordingToggle. Previously some actions (bypass, gain, presets) could execute during panic mute.
- **Panic mute stops recording**: `doPanicMute(true)` now stops active recording (recording is mic output → must stop). Recording does not auto-restart on unmute. RecordingToggle is also blocked during panic.
- **HTTP CORS preflight**: Added `OPTIONS` request handler with `Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS` for browser-based clients (e.g., pre-release dashboard). Without this, browser PUT/DELETE requests got connection reset before reaching the 405 handler.
- **HTTP gain delta scaling**: `/api/gain/:delta` now multiplies delta by 10× to compensate for `ActionHandler`'s `*0.1f` scaling (designed for hotkey steps of ±1.0 meaning ±1 dB = ±0.1 gain). Previously `GET /api/gain/1.0` only applied +0.1 gain instead of +1.0.
- **HTTP volume/parameter validation**: Volume and parameter endpoints validate numeric input — `indexOfAnyOf("0123456789.")` check rejects non-numeric strings like "abc" which `getFloatValue()` silently converts to 0.0.
- **UpdateChecker lifetime fix**: `alive_` promoted from local variable to class member. Destructor sets `false` before joining threads, preventing use-after-free in pending `callAsync` lambdas.
- **IPC lock-free assertion**: `Protocol.h` now has `static_assert(std::atomic<uint64_t/bool>::is_always_lock_free)` — compile-time guarantee that shared memory atomics never use hidden mutexes.
- **MidiTab Learn race fix**: `startLearn` callback captures `manager_` reference directly instead of raw `this`, eliminating potential use-after-free when MIDI thread fires callback during tab destruction.
- **VSTChain addPlugin double-suspend fix**: Removed orphaned `suspendProcessing(true)` around `addNode()`. JUCE uses a counter-based suspend mechanism — the extra suspend without matching resume left the audio graph muted after plugin load.
- **RingBuffer availableWrite clamp**: Added `std::min` overflow guard matching `availableRead()`, preventing underflow if positions are transiently inconsistent.
- **Receiver VST RT-safety**: Replaced `std::vector::resize()` in `saveLastOutput` (called from `processBlock`) with `jassert` — buffer is pre-allocated in `prepareToPlay`.
- **Code deduplication**: Extracted identical `actionToDisplayName` (~40 lines) from `HotkeyTab.cpp` and `MidiTab.cpp` to shared `ActionDispatcher.h`.
- **`input_muted` clarification**: `input_muted` is independent from `muted` (panic mute). `InputMuteToggle` mutes microphone input only while keeping the VST chain and output paths running.

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

- Slot naming (right-click Rename, `A|게임` display) / Slot naming (right-click Rename, `A|Game` display)
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
