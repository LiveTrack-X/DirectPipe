# DirectPipe — Claude Code Project Guide v6.1

## Project Description
Real-time VST2/VST3 host for Windows. Processes microphone input through a plugin chain. Main output goes to AudioSettings Output device (WASAPI/ASIO), with optional separate monitor output (headphones) via independent WASAPI device. Focused on external control (hotkeys, MIDI, Stream Deck, HTTP API) and fast preset switching. Similar to Light Host but with remote control capabilities.

Windows용 실시간 VST2/VST3 호스트. 마이크 입력을 플러그인 체인으로 처리. 메인 출력은 AudioSettings Output 장치(WASAPI/ASIO)로 직접 전송, 별도 WASAPI 모니터 출력(헤드폰) 옵션. 외부 제어(단축키, MIDI, Stream Deck, HTTP API)와 빠른 프리셋 전환에 초점.

## Core Principles
1. WASAPI Shared default + ASIO support (non-exclusive mic access)
2. VST2 + VST3 hosting with drag-and-drop chain editing
3. Main output via AudioSettings Output device + optional Monitor (headphones) via separate WASAPI
4. Quick Preset Slots A-E (chain-only, includes plugin internal state)
5. External control: Hotkey, MIDI, WebSocket, HTTP -> unified ActionDispatcher
6. System tray resident (close = minimize to tray)
7. Out-of-process VST scanner (crash-safe)

## Tech Stack
- C++17, JUCE 7.0.12, CMake 3.22+, project version 3.10.0
- WASAPI Shared + Low Latency + Exclusive Mode + ASIO (Steinberg ASIO SDK)
- VST2 SDK 2.4 + VST3
- WebSocket: JUCE StreamingSocket + RFC 6455 (handshake, framing, custom SHA-1)
- HTTP: JUCE StreamingSocket manual parsing
- Stream Deck: @elgato/streamdeck SDK v2.0.1, SDKVersion 3, Node.js 20

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Architecture
```
Mic -> WASAPI Shared / ASIO -> [Mono/Stereo] -> VSTChain -> Main Output (outputChannelData)
                                                              \
                                                         OutputRouter -> Monitor (Headphones, separate WASAPI)

External Control:
Hotkey/MIDI/WebSocket/HTTP -> ControlManager -> ActionDispatcher
  -> VSTChain (bypass), OutputRouter (volume), PresetManager
```

## Key Implementations
- **5 audio driver types**: DirectSound (legacy, don't use), Windows Audio (WASAPI Shared, recommended), Windows Audio (Low Latency) (IAudioClient3, many devices don't support properly), Windows Audio (Exclusive Mode), ASIO. Runtime switching. ASIO: single device, dynamic SR/BS, channel routing. WASAPI: separate I/O.
- **Audio optimizations**: `timeBeginPeriod(1)` for 1ms timer resolution (matched by `timeEndPeriod` in shutdown). Power Throttling disabled for Intel hybrid CPUs. `ScopedNoDenormals` in audio callback. Muted fast-path (skip VST processing). RMS decimation (every 4th callback). Buffer page pre-touch in `audioDeviceAboutToStart`.
- **Device auto-reconnection**: Dual mechanism — `ChangeListener` on `deviceManager_` for immediate detection + 3s timer polling fallback (`checkReconnection`). Tracks `desiredInputDevice_`/`desiredOutputDevice_`. `deviceLost_` set by `audioDeviceError`, cleared by `audioDeviceAboutToStart`. `attemptReconnection()` preserves SR/BS/channel routing (ASIO safe). Re-entrancy guard (`attemptingReconnection_`). NotificationBar: Warning on disconnect, Info on reconnect. Monitor device uses same pattern independently (`monitorLost_` + own cooldown). **Per-direction loss**: `inputDeviceLost_` silences input in audio callback (prevents fallback mic audio). `outputAutoMuted_` auto-mutes output on device loss, auto-unmutes on restore. Edge-detection notifications for both. `reconnectMissCount_` after 5 failed attempts (~15s): if `outputAutoMuted_` (genuine device loss), resets counter and keeps waiting; otherwise accepts current devices to break cross-driver stale name loops. AudioSettings combos show "DeviceName (Disconnected)" for lost devices.
- **Driver type snapshot/restore**: `DriverTypeSnapshot` struct stores per-driver-type settings (input/output device, SR, BS, `outputNone`) in `driverSnapshots_` map. Saved before `setAudioDeviceType` switch, restored when switching back. Validates saved device names still exist in target driver type. Prevents losing WASAPI settings when switching to ASIO and back. `setAudioDeviceType` clears `outputNone_` unconditionally, then restores from target driver's snapshot if saved. ASIO switch builds ordered `tryOrder` list (preferred → lastAsio → all remaining) and iterates all devices before reverting. `AudioSettings::onDriverTypeChanged` checks return value and syncs driver combo on failure.
- **Device fallback protection**: `intentionalChange_` flag in AudioEngine prevents `audioDeviceAboutToStart` from treating JUCE auto-fallback as successful reconnection. `desiredDeviceType_`/`desiredSampleRate_`/`desiredBufferSize_` track user intent. MonitorOutput rejects JUCE auto-fallback devices (shuts down and waits for reconnection to desired device). OutputPanel shows "Fallback: DeviceName" in orange when monitor falls back. Settings export uses desired device names (not fallback).
- **Hotkey drag-and-drop reorder**: Per-row `BindingRow` as `Component + DragAndDropTarget` (VST chain style). In-place key re-assignment via `updateHotkey()`. `moveBinding()` for reorder. Cancel button during key recording.
- **MIDI Learn cancel**: Cancel button in MidiTab during MIDI Learn mode. `stopLearn()` called on cancel.
- **MIDI HTTP API test endpoints**: `/api/midi/cc/:ch/:num/:val` and `/api/midi/note/:ch/:num/:vel` for testing without MIDI hardware. `MidiHandler::injectTestMessage()` wrapper. CLI tool: `tools/midi-test.py`.
- **Device combo click-to-refresh**: `mouseDown` on input/output/monitor combos triggers `scanForDevices()` + list rebuild before popup opens (mouseUp). `eventComponent` check prevents background click false triggers.
- **StateBroadcaster device state**: `device_lost` and `monitor_lost` bool fields in state JSON. Broadcast via WebSocket + HTTP API. Enables external controllers (Stream Deck) to detect device loss.
- **XRun monitoring**: Rolling 60-second window. `xrunResetRequested_` atomic flag pattern for thread-safe reset between device thread and message thread. Status bar shows count with red highlight when > 0.
- **Main output**: Processed audio written directly to outputChannelData (AudioSettings Output device). Works with both WASAPI and ASIO.
- **Output "None" mode**: `setOutputNone(bool)` / `isOutputNone()` — `outputNone_` atomic flag mutes output and locks OUT button (similar to panic mute lockout but for intentional "no output device" state). Cleared on driver type switch to prevent OUT button lock persisting across drivers. `DriverTypeSnapshot` saves/restores `outputNone` per driver type.
- **Monitor output**: Separate WASAPI AudioDeviceManager + lock-free AudioRingBuffer for headphone monitoring. Independent of main driver type (works even with ASIO). Configured in Monitor tab. Status indicator (Active/SampleRateMismatch/Error/No device). Latency label shows real-time ms calculation when Active. Auto-reconnection via `monitorLost_` atomic + 3s timer polling.
- **Quick Preset Slots (A-E)**: Chain-only. Plugin state via getStateInformation/base64. Async loading (replaceChainAsync with `alive_` flag lifetime guard). Same-chain fast path = instant switch. **Keep-Old-Until-Ready**: old chain continues processing audio during background plugin loading; swap happens atomically on message thread when new chain is ready (~10-50ms suspend vs previous 1-3s mute gap). `partialLoad_` atomic prevents auto-save after incomplete plugin loads. `loadingSlot_` prevents concurrent slot switches. **Slot naming**: custom names stored in `.dppreset` JSON `"name"` field, displayed as `A|게임` (pipe delimiter, max 8 chars with ".." truncation). Right-click → Rename/Copy/Delete/Export/Import. **Slot export/import**: individual `.dppreset` file export/import via FileChooser async. StateBroadcaster includes `slot_names` array in state JSON.
- **Out-of-process VST scanner**: `--scan` child process. Auto-retry (10x), dead man's pedal. Blacklist for crashed plugins. Log: `%AppData%/DirectPipe/scanner-log.txt`.
- **Plugin chain editor**: Drag-and-drop, bypass toggle, native GUI edit. Safe deletion via callAsync. **Plugin name for async callbacks MUST come from `slot->name` (e.g. `"Clear"`), NOT from `nameLabel_.getText()` which includes row prefix (e.g. `"1. Clear"`)** — mismatch causes silent delete failure.
- **Tabbed UI**: Audio/Monitor/Controls/Settings tabs in right column. Monitor tab includes Recording section. Controls has sub-tabs: Hotkeys/MIDI/Stream Deck/General. Settings tab has app settings, save/load, log viewer + maintenance tools (Full Backup/Restore, Clear Plugin Cache, Clear All Presets, Factory Reset).
- **Auto-start**: Platform-adaptive — Windows: Registry (`HKCU\...\Run`, label "Start with Windows"), macOS: LaunchAgent ("Start at Login"), Linux: XDG autostart ("Start on Login"). Toggle in tray menu + Settings tab.
- **System tray**: Close -> tray, double-click/left-click -> restore, right-click -> Show/Quit/Auto-start toggle.
- **Panic Mute**: Remembers pre-mute monitor enable state, restores on unmute. During panic mute, OUT/MON/VST buttons and external controls (hotkey/MIDI/Stream Deck/HTTP) are locked -- only PanicMute/unmute can change state.
- **Auto-save**: Dirty-flag pattern with 1-second debounce. `onSettingsChanged` callbacks from AudioSettings/OutputPanel trigger `markSettingsDirty()`.
- **WebSocket server**: RFC 6455 with custom SHA-1. Case-insensitive HTTP header matching (RFC 7230). Dead client cleanup on broadcast — `sendFrame` returns `bool`, `broadcastToClients` closes socket immediately on write failure (prevents repeated write-error log spam from stale clients). Port 8765. UDP discovery broadcast (port 8767) at startup for instant Stream Deck connection. `broadcastToClients` thread join outside `clientsMutex_` lock.
- **IPC Toggle**: `Action::IpcToggle` toggles IPC output (Receiver VST) on/off. Default hotkey: Ctrl+Shift+I. WebSocket: `ipc_toggle`. HTTP: `GET /api/ipc/toggle`. MIDI mappable. StateBroadcaster includes `ipc_enabled` in state JSON.
- **Receiver VST plugin**: VST2 plugin for OBS. Reads shared memory IPC from DirectPipe. Configurable buffer size (5 presets: Ultra Low 256, Low 512, Medium 1024, High 2048, Safe 4096 samples). SR mismatch warning when source SR != host SR. Latency label shows real-time ms based on host SR. Located at `plugins/receiver/`.
- **HTTP server**: GET-only REST API. CORS enabled. 3-second read timeout. Port 8766. Volume range validated (0.0-1.0). Proper HTTP status codes (404/405/400) — `processRequest` returns `pair<int, string>`. Endpoints include recording toggle, plugin parameter control, and IPC toggle. Handler thread cleanup joins outside `handlersMutex_` (deadlock prevention).
- **Audio recording**: Lock-free recording via `AudioRecorder` + `AudioFormatWriter::ThreadedWriter`. WAV output. RT-safe `juce::SpinLock` protects writer teardown. Timer-based duration tracking. Auto-stop on device change. `outputStream` properly cleaned up on writer creation failure.
- **Settings export/import**: `SettingsExporter` class. Two tiers: (1) **Save/Load Settings** (`.dpbackup`) — audio/output/control settings only, no VST chain or slots. (2) **Full Backup/Restore** (`.dpfullbackup`) — everything including VST chain + preset slots A-E. Located in Settings tab (LogPanel). Uses `controlManager_->getConfigStore()` for live config access. **Cross-OS protection**: Backup files include `platform` field (windows/macos/linux). Import checks compatibility and blocks cross-OS restore. Legacy backups without platform field accepted.
- **Plugin scanner search/sort**: Real-time text filter + column sorting (name/vendor/format) in PluginScanner dialog.
- **MIDI plugin parameter mapping**: MidiTab 3-step popup flow (select plugin → select parameter → MIDI Learn). Creates `Continuous` MidiBinding with `SetPluginParameter` action.
- **System tray tooltip**: Shows current state (preset, plugins, volumes) on hover. Atomic dirty-flag for cross-thread safety.
- **Stream Deck plugin**: SDKVersion 3, 7 SingletonAction subclasses (Bypass/Volume/Preset/Monitor/Panic/Recording/IpcToggle), Property Inspector HTML (sdpi-components v4), event-driven reconnection (UDP discovery + user-action trigger, no polling), SVG icons with @2x. Pending message queue (cap 50). Packaged via official `streamdeck pack` CLI.
- **NotificationBar**: Non-intrusive status bar notifications. Temporarily replaces latency/CPU labels. Color-coded: red (errors), orange (warnings), purple (info). Auto-fades after 3-8 seconds depending on severity. Triggered by audio device errors, plugin load failures, recording failures. Notification queue overflow guard (capacity check before write).
- **LogPanel**: 4th tab "Settings" in right panel. Application section: "Start with Windows" toggle + red "Quit" button. Real-time log viewer with timestamped monospaced entries. Export Log (save to .txt) and Clear Log buttons. Maintenance section: Full Backup/Restore (`.dpfullbackup`), Clear Plugin Cache, Clear All Presets (deletes slots + backups + temps, clears active chain), Factory Reset (deletes ALL data — settings, controls, presets, plugin cache, recordings config). All with confirmation dialogs. Batch flush optimization (drain all pending lines, single TextEditor insert, single trim pass).
- **DirectPipeLogger**: Centralized logging system feeding LogPanel and NotificationBar. Captures logs from all subsystems (audio engine, plugins, WebSocket, HTTP, etc.).
- **Verbose categorized logging**: All log messages use category tags: `[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`. High-frequency actions (SetVolume, InputGainAdjust, SetPluginParameter) and Continuous MIDI bindings excluded from logging to prevent spam. `actionToString()` helper in ActionDispatcher.h for enum-to-string conversion.

## Coding Rules
- Audio callback: no heap alloc, no mutex. Pre-allocated 8-channel work buffer.
- Control -> audio: atomic flags or lock-free queue
- GUI and control share ActionDispatcher (message-thread delivery guaranteed)
- ActionDispatcher/StateBroadcaster guarantee message-thread listener delivery (callAsync for off-thread callers, synchronous for message-thread callers)
- WebSocket/HTTP on separate threads (actions routed via ActionDispatcher's message-thread guarantee)
- `juce::MessageManager::callAsync` for UI self-deletion safety (PluginChainEditor remove button etc.)
- **callAsync lifetime guards**: `checkForUpdate` uses `SafePointer`; `VSTChain::replaceChainAsync`, `ActionDispatcher`, `StateBroadcaster` use `shared_ptr<atomic<bool>> alive_` flag pattern (captured by value in callAsync lambda, checked before accessing `this`). `PluginChainEditor::addPluginFromDescription` and `PluginScanner` (all 3 background callAsync lambdas) use `SafePointer`. Prevents use-after-delete when background thread's callAsync fires after object destruction.
- **MidiHandler `bindingsMutex_`**: `std::mutex` protects all access to `bindings_`. `getBindings()` returns a copy for safe iteration. Never hold the mutex across callbacks. `processCC`/`processNote` collect matching actions into local vector, dispatch OUTSIDE `bindingsMutex_` (deadlock prevention).
- **Notification queue overflow guard**: `pushNotification` checks ring buffer capacity before write.
- `loadingSlot_` (std::atomic<bool>) guard prevents recursive auto-save during slot loading
- **VSTChain `chainLock_`**: mutable `CriticalSection` protects ALL chain access (readers AND writers: `getPluginSlot`, `getPluginCount`, `setPluginBypassed`, parameter access, editor open/close). Never held in `processBlock`. `prepared_` is `std::atomic<bool>`. `movePlugin` resizes `editorWindows_` before move. `processBlock` uses capacity guard. **Never call `writeToLog` inside `chainLock_`** — capture log string under lock, log after releasing (prevents lock-ordering hazard with DirectPipeLogger `writeMutex_`).
- **VSTChain `rebuildGraph(bool suspend = true)`**: `suspend=true` (default) for node add/remove, `suspend=false` for bypass toggle (connection-only, no audio gap). Bypassed plugins skipped in connection graph. `getConnections()` copied before removal loop (iterator safety). `setPluginBypassed` syncs `node->setBypassed()` + `getBypassParameter()->setValueNotifyingHost()` + `rebuildGraph(false)`.
- onChainChanged callback outside chainLock_ scope (deadlock prevention)
- MidiBuffer pre-allocated in prepareToPlay, cleared after processBlock
- VSTChain removes old I/O nodes before adding new ones in prepareToPlay
- ActionDispatcher/StateBroadcaster: copy-before-iterate for reentrant safety
- WebSocket broadcast on dedicated thread (non-blocking). `broadcastToClients` joins thread outside `clientsMutex_` lock (deadlock prevention). Case-insensitive HTTP header matching in handshake (RFC 7230). `sendFrame` returns `bool` — on `false`, caller closes socket for immediate dead client detection.
- **VSTChain `asyncGeneration_`**: `uint32_t` atomic counter incremented per `replaceChainAsync` call. Background thread captures generation at start; `callAsync` callback checks `gen == asyncGeneration_` before modifying graph (discards stale callbacks from superseded loads).
- Update check thread properly joined (no detached threads)
- AudioRingBuffer capacity must be power-of-2 (assertion enforced). `reset()` zeroes all channel data.
- AudioEngine `setBufferSize`/`setSampleRate` log errors from `setAudioDeviceSetup`
- AudioRecorder: `outputStream` deleted on writer creation failure (no leak)
- **OutputRouter**: `routeAudio()` clamps `numSamples` to `scaledBuffer_` capacity (buffer overrun prevention)
- **AudioEngine**: notification queue indices use `uint32_t` (overflow-safe)
- **HTTP API**: proper status codes (404/405/400) — `processRequest` returns `pair<int, string>` instead of always 200
- **LogPanel DirectPipeLogger**: ring buffer indices use `uint32_t` (overflow-safe)
- **Tray tooltip**: `activeSlot` clamped to 0-4 range
- **SettingsExporter**: Two tiers — `exportAll/importAll` (settings only, `.dpbackup`) strips `plugins` key from audioSettings JSON; `exportFullBackup/importFullBackup` (`.dpfullbackup`) includes everything. Uses `controlManager_->getConfigStore()` for live config. `getCurrentPlatform()`/`getBackupPlatform()`/`isPlatformCompatible()` for cross-OS protection.
- **PluginPreloadCache `invalidateAll()`**: Non-blocking — bumps all `slotVersions_` + sets `cancelPreload_` + bumps `preloadGeneration_`. Does NOT join thread (COM STA deadlock) or clear cache (slow synchronous plugin destruction). Stale entries rejected by `take()`'s SR/BS check. `cancelAndWait()` uses heap-allocated `shared_ptr` for join state (prevents dangling reference if detached joiner thread outlives caller's stack frame).
- **PluginPreloadCache `slotVersions_`**: Per-slot `atomic<uint32_t>` version counter. Bumped by `invalidateSlot`/`invalidateAll`. `preloadAllSlots` captures version at file-read time; cache store checks version match — discards stale data if slot was invalidated mid-preload (race condition prevention).
- **PluginPreloadCache `isCachedWithStructure`**: Compares cached plugin names/paths/order against current chain. `saveSlot` uses this to invalidate only on structure change (add/remove/replace/reorder), not on parameter-only saves (bypass, plugin state).
- **Sample rate propagation**: Audio tab SR applies globally — VST chain, monitor output, IPC all follow main device SR. Monitor `VirtualCableStatus::SampleRateMismatch` if device SR differs. Receiver VST shows SR mismatch warning in editor UI.
- **MonitorOutput shutdown()**: Resets `actualSampleRate_`/`actualBufferSize_` to 0 (prevents stale latency display).
- **MonitorOutput `monitorLost_`**: Set by `audioDeviceError`/`audioDeviceStopped` (only fires on external events — `shutdown()` removes callback first). Cleared ONLY by `audioDeviceAboutToStart`. NOT cleared by `initialize()`/`shutdown()` (prevents retry-stop on init failure). `audioDeviceStopped` sets status to `Error` (not `NotConfigured`) for accurate UI display.
- **AudioEngine reconnection**: `desiredInputDevice_`/`desiredOutputDevice_` saved in `audioDeviceAboutToStart` and `setInputDevice`/`setOutputDevice`. `attemptReconnection` only updates device names in setup (preserves SR/BS/channel config from `getAudioDeviceSetup`). `reconnectCooldown_` reset on success. `attemptingReconnection_` re-entrancy guard (message thread only). `setInputDevice`/`setOutputDevice` also clear `deviceLost_`, `inputDeviceLost_`/`outputAutoMuted_`, and reconnect counters (allows manual device selection during device loss).
- **Device combo click-to-refresh**: `addMouseListener(this, true)` on combos in constructor, `removeMouseListener` in destructor. `mouseDown` checks `eventComponent` to avoid background clicks. Component already inherits MouseListener (no duplicate inheritance).

## Modules
- `core/` -> IPC library (RingBuffer, SharedMemory, Protocol, Constants)
- `host/` -> JUCE app
  - `Audio/` -> AudioEngine, VSTChain, OutputRouter, MonitorOutput, AudioRingBuffer, LatencyMonitor, AudioRecorder
  - `Control/` -> ActionDispatcher, ControlManager, ControlMapping, WebSocketServer, HttpApiServer, HotkeyHandler, MidiHandler, StateBroadcaster, DirectPipeLogger
  - `IPC/` -> SharedMemWriter
  - `Platform/` -> Cross-platform abstractions (PlatformAudio, AutoStart, ProcessPriority, MultiInstanceLock) with Windows/macOS/Linux implementations
  - `UI/` -> AudioSettings, OutputPanel, ControlSettingsPanel, PluginChainEditor, PluginScanner, PresetManager, LevelMeter, LogPanel, NotificationBar, DirectPipeLookAndFeel, SettingsExporter
- `plugins/receiver/` -> Receiver VST2/VST3/AU plugin for OBS/DAW (shared memory IPC consumer, configurable buffer size)
- `com.directpipe.directpipe.sdPlugin/` -> Stream Deck plugin (Node.js, @elgato/streamdeck SDK v3)
- `tests/` -> Google Test (core tests + host tests)
- `dist/` -> Packaged .streamDeckPlugin + marketplace assets

## Known Notes
- `ChildProcess::start()`: quote paths with spaces
- VST scan timeout: 300s (5min)
- `channelMode_` default: 2 (Stereo)
- Plugin deletion: auto-close editor windows + cleanup editorWindows_ vector
- ASIO SDK path: `thirdparty/asiosdk/common`
- Preset version 4 (deviceType, activeSlot, plugin state)
- SHA-1: custom implementation for WebSocket handshake only
- Stream Deck: SDKVersion 3 (SDK v2.0.1 npm), Version 3.9.10.0, 7 actions (Bypass/Volume/Preset/Monitor/Panic/Recording/IpcToggle), 3 PI HTMLs, SVG-based icons + @2x, packaged via `streamdeck pack` CLI
- Auto-save: dirty-flag + 1s debounce (not periodic timer), onSettingsChanged callbacks
- License: GPL v3 (JUCE GPL compatibility). JUCE_DISPLAY_SPLASH_SCREEN=0
- Credit label "Created by LiveTrack" at bottom-right of main UI. Shows "NEW vX.Y.Z" in orange when a newer GitHub release exists. Clicking opens update dialog.
- **In-app auto-updater**: Background thread fetches latest release from GitHub API on startup. Compares semver, updates credit label if newer. Click credit label → dialog with [Update Now] / [View on GitHub] / [Later]. Update Now downloads ZIP/exe from GitHub release, replaces exe via batch script, auto-restarts. Post-update "Updated successfully" notification via `_updated.flag` file.
- **CJK font rendering**: `DirectPipeLookAndFeel` sets `LookAndFeel::setDefaultLookAndFeel` so `getTypefaceForFont()` is globally active. Bold Malgun Gothic for ComboBox/PopupMenu/ToggleButton text. Fixes Korean device names rendering as □□□.
- Process priority: HIGH_PRIORITY_CLASS at startup. `timeBeginPeriod(1)` + Power Throttling disable in `initialise()`, `timeEndPeriod(1)` in `shutdown()`.
- **AudioEngine `setBufferSize` auto-fallback**: If driver rejects requested size, finds closest device-supported size and notifies via NotificationBar.
- **XRun tracking thread safety**: `xrunResetRequested_` atomic flag in `audioDeviceAboutToStart`, reset handled on message thread in `updateXRunTracking()`. Non-atomic xrun history vars (`lastDeviceXRunCount_`, `xrunHistory_[]`, etc.) only accessed from message thread.
- **RMS decimation**: `rmsDecimationCounter_` (RT thread only, no atomic needed). RMS computed every 4th callback to reduce CPU.
- Portable mode: `portable.flag` next to exe -> config stored in `./config/`
- **Multi-instance external control priority**: Named Mutexes (`DirectPipe_NormalRunning`, `DirectPipe_ExternalControl`) coordinate which instance owns hotkeys/MIDI/WS/HTTP. Normal mode blocks if portable has control. Portable runs audio-only if normal is running or another portable already has control. `acquireExternalControlPriority()` returns 1 (full control), 0 (audio only), -1 (blocked/quit). `ControlManager::initialize(bool enableExternalControls)` conditionally skips external control init. `reloadConfig()`/`applyConfig()` preserve the flag. Title bar shows "(Audio Only)", status bar shows orange "Portable", tray tooltip shows "(Portable/Audio Only)".
- **LogPanel Quit button**: Red "Quit" button next to auto-start toggle in Settings tab. Calls `systemRequestedQuit()`. Helps distinguish instances when multiple portables are running.

## Pre-Release Workflow (MANDATORY)
When the user asks to "release", "make a release", "commit for release", "push release", "create release", "릴리스", "릴리즈", "배포", or any release-related action, you MUST ask and verify the following BEFORE proceeding:

1. **Version bumped?** — All 7 locations updated? Run: `bash tools/pre-release-test.sh --version-only`
2. **Code review done?** — Run "pre-release review" skill (9 bug categories) on changed files. Any Critical issues must be fixed first.
3. **Auto tests passed?** — Run: `bash tools/pre-release-test.sh` (build + unit tests + API tests)
4. **GUI dashboard checked?** — Ask user to open `tools/pre-release-dashboard.html` in browser, run auto tests, check manual items, and export report.
5. **Dashboard report reviewed?** — If user provides the exported JSON report, analyze it for any FAIL items and suggest fixes.

Do NOT proceed with release packaging (ZIP, gh release, git push) until all 5 steps are confirmed.
If any step fails, help fix the issue first, then re-run that step.
