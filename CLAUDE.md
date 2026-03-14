# DirectPipe — Claude Code Project Guide v8.0

## Project Description
Cross-platform real-time VST2/VST3 host. Windows (stable), macOS (beta), Linux (experimental). Processes microphone input through a plugin chain. Main output goes to AudioSettings Output device, with optional separate monitor output (headphones) via independent shared-mode device. Focused on external control (hotkeys, MIDI, Stream Deck, HTTP API) and fast preset switching. Similar to Light Host but with remote control capabilities.

크로스 플랫폼 실시간 VST2/VST3 호스트. Windows(안정), macOS(베타), Linux(실험적). 마이크 입력을 플러그인 체인으로 처리. 메인 출력은 AudioSettings Output 장치로 직접 전송, 별도 공유 모드 모니터 출력(헤드폰) 옵션. 외부 제어(단축키, MIDI, Stream Deck, HTTP API)와 빠른 프리셋 전환에 초점.

> **v4.0.0은 v3.10.3에서 아키텍처 리팩토링 + 크로스플랫폼 확장한 알파 버전입니다.**
> MainComponent를 7개 focused module로 분할, Platform/ 추상화 레이어 도입, 테스트 52→233+개 확장.

## Core Principles
1. Platform-abstracted audio: WASAPI/ASIO (Windows), CoreAudio (macOS), ALSA/JACK (Linux)
2. VST2 + VST3 hosting with drag-and-drop chain editing (+ AU on macOS)
3. Main output via AudioSettings Output device + optional Monitor (headphones) via separate shared-mode device
4. Quick Preset Slots A-E (chain-only, includes plugin internal state)
5. External control: Hotkey, MIDI, WebSocket, HTTP -> unified ActionDispatcher
6. System tray resident (close = minimize to tray; Linux: minimize to taskbar)
7. Out-of-process VST scanner (crash-safe)
8. Platform abstraction layer: `host/Source/Platform/` with per-OS implementations

## Tech Stack
- C++17, JUCE 7.0.12, CMake 3.22+, project version 4.0.0
- **Windows**: WASAPI Shared + Low Latency + Exclusive Mode + ASIO (Steinberg ASIO SDK)
- **macOS**: CoreAudio + AU hosting. Universal binary (arm64+x86_64). Deployment target 10.15
- **Linux**: ALSA + JACK (PipeWire via JACK compat)
- VST2 SDK 2.4 + VST3 (all platforms), AU (macOS only)
- IPC: Windows CreateFileMapping/CreateEvent, POSIX shm_open/sem_open
- WebSocket: JUCE StreamingSocket + RFC 6455 (handshake, framing, custom SHA-1)
- HTTP: JUCE StreamingSocket manual parsing
- Stream Deck: @elgato/streamdeck SDK v2.0.1, SDKVersion 3, Node.js 20

## Build
```bash
# cmake is NOT in PATH on this machine — use full path:
# "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- **Portable test build**: Copy exe to `C:\Users\livet\Desktop\DirectPipe-v4-test\` (has `portable.flag` for isolation from v3)
- **Stale build artifacts**: If HTTP API or other features behave unexpectedly after code changes, do a clean rebuild — incremental builds can silently link stale .obj files

## Architecture
```
Mic -> [WASAPI/ASIO | CoreAudio | ALSA/JACK] -> [Mono/Stereo] -> VSTChain -> Main Output (outputChannelData)
                                                                    \
                                                               OutputRouter -> Monitor (Headphones, separate shared-mode device)

External Control:
Hotkey/MIDI/WebSocket/HTTP -> ControlManager -> ActionDispatcher -> ActionHandler
  -> VSTChain (bypass), OutputRouter (volume), PresetManager

Platform Abstraction:
host/Source/Platform/{PlatformAudio.h, AutoStart.h, ProcessPriority.h, MultiInstanceLock.h}
  -> Windows/  (Registry, Named Mutex, Win32 API)
  -> macOS/    (LaunchAgent, InterProcessLock, setpriority)
  -> Linux/    (XDG .desktop, InterProcessLock, setpriority)

MainComponent Split (v3→v4):
  MainComponent (~729 lines) — UI layout, component wiring
  + ActionHandler           — action routing, panic mute consolidation
  + SettingsAutosaver       — dirty-flag auto-save with debounce
  + StatusUpdater           — periodic UI status updates (~30Hz)
  + PresetSlotBar           — A-E slot buttons + context menu
  + UpdateChecker           — GitHub API polling + update dialog
  + HotkeyTab / MidiTab / StreamDeckTab — Controls sub-tabs (each separate file)
```

## Key Implementations

### v4 Architecture (v3에서 변경된 부분)
- **ActionHandler**: Consolidated action routing from ActionDispatcher to AudioEngine/UI. `doPanicMute(bool)` consolidates panic mute logic (single implementation, was scattered in v3 MainComponent): saves pre-mute state (monitor, output, IPC), mutes everything, **stops active recording**. On unmute, restores previous state (recording does not auto-restart). **All action cases** in `handle()` check `engine_.isMuted()` to block during panic — PluginBypass, MasterBypass, InputGainAdjust, SetVolume, SetPluginParameter, LoadPreset, SwitchPresetSlot, NextPreset, PreviousPreset, RecordingToggle, MonitorToggle, IpcToggle. Only PanicMute/InputMuteToggle bypass the check (they ARE the panic toggle). Callback-based decoupling from MainComponent (`onDirty`, `onNotification`, `onPanicStateChanged`, `onRecordingStopped`, etc.). Owns pre-mute state (`preMuteMonitorEnabled_`, `preMuteOutputMuted_`, `preMuteVstEnabled_`).
- **ActionResult**: `ActionResult.h` — ok/fail pattern with `onError` callback. Replaces v3's bare bool/void returns for structured error handling.
- **SettingsAutosaver**: Dirty-flag pattern with 1-second debounce timer (~30Hz tick). `markDirty()` sets flag, `tick()` counts down cooldown, `saveNow()` for immediate save. Callback-based decoupling (`onPostLoad`, `onShowWindow`, `onRestorePanicMute`, `onFlushLogs`). `loadFromFile()` wraps `triggerPreload()` in `callAsync` (audio device must be fully started before preload).
- **StatusUpdater**: Periodic UI status updates at ~30Hz. Caches all status values (mute states, latency, CPU, format, gain) to avoid redundant repaints. Updates level meters, mute button colours, status bar labels, input gain slider sync, and broadcasts full state to WebSocket clients. Pointer-based UI binding via `setUI()`.
- **PresetSlotBar**: A-E quick preset slot buttons. Slot naming with pipe delimiter (`A|게임`, max 8 chars). Right-click context menu (Rename/Copy/Delete/Export/Import). Callback-based (`onSlotSwitch`, `onDirty`, `onNotification`). Shared atomics `loadingSlot_`/`partialLoad_` by reference.
- **UpdateChecker**: Separate class (was inline in v3 MainComponent). Background GitHub API polling for new releases. Semver comparison. Updates credit label text on newer version found. Click triggers update dialog with [Update Now] (Windows only) / [View on GitHub] (macOS/Linux) / [Later]. Uses `alive_` flag for async safety. **Platform-aware asset selection**: 1st pass matches platform tag (`Windows`+`.zip`, `macOS`+`.dmg`, `Linux`+`.tar.gz`), 2nd pass falls back to any `DirectPipe*.zip` (legacy releases), 3rd pass falls back to `DirectPipe*.exe`. See workspace `CLAUDE.md` Section 2 for full release asset naming rules.
- **HotkeyTab / MidiTab / StreamDeckTab**: Each Controls sub-tab is a separate .h/.cpp file (was all inline in v3 ControlSettingsPanel). HotkeyTab: BindingRow drag-and-drop, key recording. MidiTab: device selector, Learn mode, 3-step param popup. StreamDeckTab: WS/HTTP server status. ControlSettingsPanel is now a slim tabbed container (~75 lines).
- **PluginLoadHelper**: `PluginLoadHelper.h` — helper for cross-platform VST loading.
- **Platform/ abstraction layer**: Header-only interfaces with per-OS implementations.
  - `PlatformAudio.h` (inline): `getDefaultSharedDeviceType()` abstracts driver names across OS.
  - `AutoStart.h` → Windows: Registry `HKCU\...\Run`. macOS: `~/Library/LaunchAgents/com.directpipe.host.plist`. Linux: `~/.config/autostart/directpipe.desktop` (XDG).
  - `ProcessPriority.h` → Windows: HIGH_PRIORITY_CLASS + timeBeginPeriod(1) + Power Throttling disable. macOS: setpriority(-20) + pthread QoS. Linux: setpriority(-10).
  - `MultiInstanceLock.h` → Windows: Named Mutexes. macOS/Linux: JUCE InterProcessLock (file-based flock).

### Audio
- **Audio drivers**: Windows: 5 types — DirectSound (legacy), Windows Audio (WASAPI Shared, recommended), Windows Audio (Low Latency), Windows Audio (Exclusive Mode), ASIO. macOS: CoreAudio. Linux: ALSA, JACK. `PlatformAudio::getDefaultSharedDeviceType()` abstracts driver names. Runtime switching. ASIO: single device, dynamic SR/BS, channel routing.
- **Audio optimizations**: `timeBeginPeriod(1)` for 1ms timer resolution (matched by `timeEndPeriod` in shutdown). Power Throttling disabled for Intel hybrid CPUs. `ScopedNoDenormals` in audio callback. Muted fast-path (skip VST processing). RMS decimation (every 4th callback). Buffer page pre-touch in `audioDeviceAboutToStart`.
- **Device auto-reconnection**: Dual mechanism — `ChangeListener` on `deviceManager_` for immediate detection + 3s timer polling fallback (`checkReconnection`). Tracks `desiredInputDevice_`/`desiredOutputDevice_`. `deviceLost_` set by `audioDeviceError`, cleared by `audioDeviceAboutToStart`. `attemptReconnection()` preserves SR/BS/channel routing (ASIO safe). Re-entrancy guard (`attemptingReconnection_`). NotificationBar: Warning on disconnect, Info on reconnect. Monitor device uses same pattern independently (`monitorLost_` + own cooldown). **Per-direction loss**: `inputDeviceLost_` silences input in audio callback. `outputAutoMuted_` auto-mutes on device loss, auto-unmutes on restore. `reconnectMissCount_` after 5 failed attempts. AudioSettings combos show "DeviceName (Disconnected)" for lost devices.
- **Driver type snapshot/restore**: `DriverTypeSnapshot` struct stores per-driver-type settings (input/output device, SR, BS, `outputNone`) in `driverSnapshots_` map. ASIO switch builds ordered `tryOrder` list (preferred → lastAsio → all remaining) and iterates all devices before reverting. `AudioSettings::onDriverTypeChanged` checks return value and syncs driver combo on failure.
- **Device fallback protection**: `intentionalChange_` flag in AudioEngine prevents `audioDeviceAboutToStart` from treating JUCE auto-fallback as successful reconnection. MonitorOutput rejects JUCE auto-fallback devices. OutputPanel shows "Fallback: DeviceName" in orange. Settings export uses desired device names.
- **Main output**: Processed audio written directly to outputChannelData. Works with WASAPI, ASIO, CoreAudio, ALSA, JACK.
- **Output "None" mode**: `setOutputNone(bool)` / `isOutputNone()` — `outputNone_` atomic flag mutes output and locks OUT button. Cleared on driver type switch. `DriverTypeSnapshot` saves/restores per driver type.
- **Monitor output**: Separate shared-mode AudioDeviceManager (WASAPI/CoreAudio/ALSA) + lock-free AudioRingBuffer for headphone monitoring. Independent of main driver type. Status indicator (Active/SampleRateMismatch/Error/No device). Auto-reconnection via `monitorLost_` atomic + 3s timer polling.
- **XRun monitoring**: Rolling 60-second window. `xrunResetRequested_` atomic flag pattern. Status bar shows count with red highlight.

### Preset & Plugin
- **Quick Preset Slots (A-E)**: Chain-only. Plugin state via getStateInformation/base64. Async loading (replaceChainAsync with `alive_` flag). **Keep-Old-Until-Ready**: old chain continues during background loading. `partialLoad_` prevents auto-save after incomplete loads. `loadingSlot_` prevents concurrent switches. **Slot naming**: `.dppreset` JSON `"name"` field, displayed as `A|게임`. Right-click → Rename/Copy/Delete/Export/Import.
- **Out-of-process VST scanner**: `--scan` child process. Auto-retry (10x), dead man's pedal. Blacklist for crashed plugins.
- **Plugin chain editor**: Drag-and-drop, bypass toggle, native GUI edit. **Plugin name for async callbacks MUST come from `slot->name`, NOT from `nameLabel_.getText()`**.
- **Plugin scanner search/sort**: Real-time text filter + column sorting (name/vendor/format).

### External Control
- **Global hotkeys**: Windows: `RegisterHotKey` + message-only window. macOS: `CGEventTap` + `AXIsProcessTrustedWithOptions` auto-prompt (accessibility permission required). Linux: stub (not supported, use MIDI/WS/HTTP). `keyToString()` uses macOS symbols (⌃⌥⇧⌘) on Mac.
- **Panic Mute**: Remembers pre-mute state (monitor, output mute, IPC), restores on unmute. Stops active recording on engage (does not auto-restart). During panic mute, all actions and external controls locked — only PanicMute/unmute can change state. Logic consolidated in `ActionHandler::doPanicMute()`. `input_muted` state field mirrors `muted` (no independent input mute).
- **WebSocket server**: RFC 6455 with custom SHA-1. Case-insensitive HTTP header matching (RFC 7230). Dead client cleanup. Port 8765. UDP discovery broadcast (port 8767). `broadcastToClients` thread join outside `clientsMutex_` lock.
- **IPC Toggle**: `Action::IpcToggle` toggles IPC output. Default hotkey: Ctrl+Shift+I. WebSocket/HTTP/MIDI mappable.
- **HTTP server**: GET-only REST API. CORS enabled with OPTIONS preflight support for browser clients. Port 8766. Proper HTTP status codes (404/405/400). Input validation: volume/parameter values validated as numeric. Gain delta endpoint scales by 10× (compensates ActionHandler's `*0.1f` hotkey step design). Endpoints include recording toggle, plugin parameter control, IPC toggle.
- **MIDI Learn cancel**: Cancel button in MidiTab. `stopLearn()` called on cancel.
- **MIDI HTTP API test endpoints**: `/api/midi/cc/:ch/:num/:val` and `/api/midi/note/:ch/:num/:vel`.
- **MIDI plugin parameter mapping**: MidiTab 3-step popup flow.

### UI & Settings
- **Tabbed UI**: Audio/Monitor/Controls/Settings tabs in right column. Controls has sub-tabs (each in separate file): HotkeyTab, MidiTab, StreamDeckTab, General. ControlSettingsPanel is the slim tabbed container (~75 lines).
- **Auto-start**: Platform::AutoStart abstraction. Windows: Registry. macOS: LaunchAgent. Linux: XDG autostart. Toggle in tray menu + Controls > General tab.
- **System tray**: Close -> tray (macOS/Windows) or minimize to taskbar (Linux, GNOME 42+ tray limitation). Right-click -> Show/Quit/auto-start toggle.
- **Settings export/import**: Two tiers: `.dpbackup` (settings only) and `.dpfullbackup` (everything). **Cross-OS protection**: Backup files include `platform` field (windows/macos/linux). `getCurrentPlatform()`/`getBackupPlatform()`/`isPlatformCompatible()` block cross-OS restore. Legacy backups without platform field accepted.
- **In-app auto-updater**: Background thread fetches latest release from GitHub API. **Platform-aware asset selection** — prefers platform-tagged asset (e.g. `DirectPipe-...-Windows.zip`), falls back to legacy naming. Update Now (Windows): downloads ZIP/exe, batch script auto-restart. macOS/Linux: "View on GitHub" only (manual download). Post-update notification via `_updated.flag`. **릴리스 asset 네이밍 필수**: Windows=`.zip`, macOS=`.dmg`, Linux=`.tar.gz` — macOS/Linux를 `.zip`으로 배포하면 v3 구 업데이터가 잘못된 바이너리를 다운로드함.
- **NotificationBar**: Color-coded (red/orange/purple). Auto-fades 3-8 seconds.
- **LogPanel**: Settings tab. Real-time log viewer. Maintenance: Full Backup/Restore, Clear Plugin Cache, Clear All Presets, Factory Reset.
- **DirectPipeLogger**: Centralized logging with category tags: `[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`. High-frequency actions excluded.

### Receiver & Stream Deck
- **Receiver VST plugin**: VST2/VST3/AU plugin for OBS and DAWs. Reads shared memory IPC. Configurable buffer size (5 presets). Formats: .dll (Win VST2), .vst3 (all), .vst (macOS VST2 bundle), .component (macOS AU), .so (Linux VST2).
- **Stream Deck plugin**: SDKVersion 3, 7 SingletonAction subclasses, Property Inspector HTML (sdpi-components v4), event-driven reconnection, SVG icons + @2x. Pending message queue (cap 50). Packaged via `streamdeck pack` CLI.

### Platform-Specific Notes
- **CJK font rendering**: Platform-specific fonts — Windows: Bold Malgun Gothic. macOS: Apple SD Gothic Neo. Linux: Noto Sans CJK KR.
- **macOS Entitlements**: `host/Resources/Entitlements.plist` — audio-input, allow-unsigned-executable-memory (VST JIT), disable-library-validation (third-party plugins). `MacOSXBundleInfo.plist` — NSMicrophoneUsageDescription, NSAccessibilityUsageDescription, NSLocalNetworkUsageDescription.
- **POSIX signal handling**: SIGPIPE ignored. SIGTERM triggers `systemRequestedQuit()` (clean shutdown for systemd/kill). JUCE only handles SIGINT on Linux.
- **macOS config directory**: `~/Library/Application Support/DirectPipe/` (not `~/Library/DirectPipe/`).
- **POSIX IPC**: `shm_open` + `sem_open`. macOS: `sem_trywait` polling (no `sem_timedwait`). Linux: `sem_timedwait`.

## Coding Rules
- Audio callback: no heap alloc, no mutex. Pre-allocated 8-channel work buffer.
- Control -> audio: atomic flags or lock-free queue
- GUI and control share ActionDispatcher (message-thread delivery guaranteed)
- ActionDispatcher/StateBroadcaster guarantee message-thread listener delivery (callAsync for off-thread callers, synchronous for message-thread callers)
- WebSocket/HTTP on separate threads (actions routed via ActionDispatcher's message-thread guarantee)
- `juce::MessageManager::callAsync` for UI self-deletion safety (PluginChainEditor remove button etc.)
- **callAsync lifetime guards**: `checkForUpdate` uses `SafePointer`; `VSTChain::replaceChainAsync`, `ActionDispatcher`, `StateBroadcaster` use `shared_ptr<atomic<bool>> alive_` flag pattern (captured by value in callAsync lambda, checked before accessing `this`). `PluginChainEditor::addPluginFromDescription` and `PluginScanner` use `SafePointer`. Prevents use-after-delete when background thread's callAsync fires after object destruction.
- **MidiHandler `bindingsMutex_`**: `std::mutex` protects all access to `bindings_`. `getBindings()` returns a copy. Never hold across callbacks. dispatch OUTSIDE lock (deadlock prevention).
- **Notification queue overflow guard**: `pushNotification` checks ring buffer capacity before write.
- `loadingSlot_` (std::atomic<bool>) guard prevents recursive auto-save during slot loading
- **VSTChain `chainLock_`**: mutable `CriticalSection` protects ALL chain access. Never held in `processBlock`. **Never call `writeToLog` inside `chainLock_`** — capture log string under lock, log after releasing (lock-ordering hazard with DirectPipeLogger `writeMutex_`).
- **VSTChain `rebuildGraph(bool suspend = true)`**: `suspend=true` for node add/remove, `suspend=false` for bypass toggle. `getConnections()` copied before removal loop (iterator safety). `setPluginBypassed` syncs `node->setBypassed()` + `getBypassParameter()->setValueNotifyingHost()` + `rebuildGraph(false)`.
- onChainChanged callback outside chainLock_ scope (deadlock prevention)
- MidiBuffer pre-allocated in prepareToPlay, cleared after processBlock
- VSTChain removes old I/O nodes before adding new ones in prepareToPlay
- ActionDispatcher/StateBroadcaster: copy-before-iterate for reentrant safety
- WebSocket broadcast on dedicated thread (non-blocking). `broadcastToClients` joins thread outside `clientsMutex_` lock (deadlock prevention).
- **VSTChain `asyncGeneration_`**: `uint32_t` atomic counter. Discards stale callbacks from superseded loads.
- Update check thread properly joined (no detached threads)
- AudioRingBuffer capacity must be power-of-2 (assertion enforced). `reset()` zeroes all channel data.
- AudioEngine `setBufferSize`/`setSampleRate` log errors from `setAudioDeviceSetup`
- AudioRecorder: `outputStream` deleted on writer creation failure (no leak)
- **OutputRouter**: `routeAudio()` clamps `numSamples` to `scaledBuffer_` capacity (buffer overrun prevention)
- **AudioEngine**: notification queue indices use `uint32_t` (overflow-safe)
- **HTTP API**: proper status codes (404/405/400) — `processRequest` returns `pair<int, string>`
- **LogPanel DirectPipeLogger**: ring buffer indices use `uint32_t` (overflow-safe)
- **Tray tooltip**: `activeSlot` clamped to 0-4 range
- **SettingsExporter**: Two tiers — `exportAll/importAll` strips `plugins` key; `exportFullBackup/importFullBackup` includes everything. `getCurrentPlatform()`/`isPlatformCompatible()` for cross-OS protection.
- **PluginPreloadCache `invalidateAll()`**: Non-blocking — bumps all `slotVersions_` + sets `cancelPreload_` + bumps `preloadGeneration_`. Does NOT join thread (COM STA deadlock). `cancelAndWait()` uses heap-allocated `shared_ptr` for join state.
- **PluginPreloadCache `slotVersions_`**: Per-slot `atomic<uint32_t>` version counter. Bumped by `invalidateSlot`/`invalidateAll`. `preloadAllSlots` captures version at file-read time; cache store checks version match — discards stale data if slot was invalidated mid-preload.
- **PluginPreloadCache `isCachedWithStructure`**: Compares cached plugin names/paths/order. `saveSlot` invalidates only on structure change (add/remove/replace/reorder), not on parameter-only saves.
- **Atomic file write pattern**: All preset/config saves use `atomicWriteFile()`: write .tmp → rename original to .bak → rename .tmp to target. Load functions use `loadFileWithBackupFallback()`: try main file → .bak → legacy .backup. See `host/Source/Util/AtomicFileIO.h`.
- **Sample rate propagation**: Audio tab SR applies globally — VST chain, monitor output, IPC all follow main device SR.
- **MonitorOutput shutdown()**: Resets `actualSampleRate_`/`actualBufferSize_` to 0. `monitorLost_` set by `audioDeviceError`/`audioDeviceStopped`, cleared ONLY by `audioDeviceAboutToStart`.
- **AudioEngine reconnection**: `desiredInputDevice_`/`desiredOutputDevice_` saved in `audioDeviceAboutToStart` and `setInputDevice`/`setOutputDevice`. `attemptReconnection` only updates device names in setup.
- **Device combo click-to-refresh**: `addMouseListener(this, true)` on combos in constructor, `removeMouseListener` in destructor.

## Modules
- `core/` -> IPC library (RingBuffer, SharedMemory, Protocol, Constants). POSIX: shm_open + sem_open (macOS sem_trywait polling, Linux sem_timedwait). Windows: CreateFileMapping + CreateEvent.
- `host/` -> JUCE app
  - `Audio/` -> AudioEngine, VSTChain, OutputRouter, MonitorOutput, AudioRingBuffer, LatencyMonitor, AudioRecorder, PluginPreloadCache, PluginLoadHelper
  - `Control/` -> ActionDispatcher, **ActionHandler**, ControlManager, ControlMapping, **SettingsAutosaver**, WebSocketServer, HttpApiServer, HotkeyHandler, MidiHandler, StateBroadcaster, DirectPipeLogger (Log.h/cpp)
  - `IPC/` -> SharedMemWriter
  - `UI/` -> AudioSettings, OutputPanel, ControlSettingsPanel (slim tabbed container), **HotkeyTab**, **MidiTab**, **StreamDeckTab**, PluginChainEditor, PluginScanner, PresetManager, **PresetSlotBar**, LevelMeter, LogPanel, NotificationBar, DirectPipeLookAndFeel, SettingsExporter, **StatusUpdater**, **UpdateChecker**, DeviceSelector
  - `Platform/` -> PlatformAudio.h (inline), AutoStart.h, ProcessPriority.h, MultiInstanceLock.h
    - `Windows/` -> WindowsAutoStart, WindowsProcessPriority, WindowsMultiInstanceLock (Registry, Named Mutex, Win32)
    - `macOS/` -> MacAutoStart, MacProcessPriority, MacMultiInstanceLock (LaunchAgent, setpriority, InterProcessLock)
    - `Linux/` -> LinuxAutoStart, LinuxProcessPriority, LinuxMultiInstanceLock (XDG .desktop, setpriority, InterProcessLock)
  - `Util/AtomicFileIO.h` — Atomic file write + backup recovery (header-only)
  - Root: MainComponent, Main, **ActionResult.h**
- `plugins/receiver/` -> Receiver VST2/VST3/AU plugin for OBS and DAWs (shared memory IPC consumer, configurable buffer size)
- `com.directpipe.directpipe.sdPlugin/` -> Stream Deck plugin (Node.js, @elgato/streamdeck SDK v3)
- `tests/` -> Google Test (core tests + host tests, **233+ tests, 14 host suites**)
- `dist/` -> Packaged .streamDeckPlugin + marketplace assets

**(bold = v3에 없고 v4에서 추가된 모듈)**

## Known Notes
- `ChildProcess::start()`: quote paths with spaces
- VST scan timeout: 300s (5min)
- `channelMode_` default: 2 (Stereo)
- Plugin deletion: auto-close editor windows + cleanup editorWindows_ vector
- ASIO SDK path: `thirdparty/asiosdk/common`
- Preset version 4 (deviceType, activeSlot, plugin state)
- SHA-1: custom implementation for WebSocket handshake only
- Stream Deck: SDKVersion 3 (SDK v2.0.1 npm), Version 3.9.10.0, 7 actions, 3 PI HTMLs, SVG-based icons + @2x, packaged via `streamdeck pack` CLI
- Auto-save: dirty-flag + 1s debounce (not periodic timer), managed by SettingsAutosaver
- License: GPL v3 (JUCE GPL compatibility). JUCE_DISPLAY_SPLASH_SCREEN=0
- Credit label "Created by LiveTrack" at bottom-right. Shows "NEW vX.Y.Z" in orange.
- **AudioEngine `setBufferSize` auto-fallback**: If driver rejects requested size, finds closest device-supported size.
- **XRun tracking thread safety**: `xrunResetRequested_` atomic flag. Non-atomic xrun history vars only on message thread.
- **RMS decimation**: `rmsDecimationCounter_` (RT thread only, no atomic needed). Every 4th callback.
- Portable mode: `portable.flag` next to exe -> config stored in `./config/`
- **Multi-instance external control priority**: Platform::MultiInstanceLock abstraction. `acquireExternalControlPriority()` returns 1/0/-1.
- **LogPanel Quit button**: Red "Quit" button in Settings tab.
- **v3 핫픽스 동기화**: v3에서 긴급 버그를 수정하면 반드시 이 v4에도 동기화해야 합니다. 단, v3 전용 코드(version ceiling 등)는 동기화 제외. 상세 절차는 `../HOTFIX_RELEASE_GUIDE.md` Phase 10 참조.

## Pre-Release Workflow (MANDATORY)
When the user asks to "release", "make a release", "commit for release", "push release", "create release", "릴리스", "릴리즈", "배포", or any release-related action, you MUST ask and verify the following BEFORE proceeding:

1. **Version bumped?** — All 7 locations updated? Run: `bash tools/pre-release-test.sh --version-only`
2. **Code review done?** — Run "pre-release review" skill (9 bug categories) on changed files. Any Critical issues must be fixed first.
3. **Auto tests passed?** — Run: `bash tools/pre-release-test.sh` (build + unit tests + API tests)
4. **GUI dashboard checked?** — Ask user to open `tools/pre-release-dashboard.html` in browser, run auto tests, check manual items, and export report.
5. **Dashboard report reviewed?** — If user provides the exported JSON report, analyze it for any FAIL items and suggest fixes.

Do NOT proceed with release packaging (ZIP, gh release, git push) until all 5 steps are confirmed.
If any step fails, help fix the issue first, then re-run that step.

**v4 릴리스 시 주의**: GitHub Release에 반드시 `--prerelease` 플래그를 사용해야 합니다. 그래야 v3 사용자의 자동 업데이터가 v4를 감지하지 않습니다. stable이 확정된 후에만 latest로 전환.

**문서 업데이트도 필수**: 코드 변경 시 README.md, docs/ 내 관련 문서, 이 CLAUDE.md도 현재 상태에 맞게 업데이트해야 합니다.
