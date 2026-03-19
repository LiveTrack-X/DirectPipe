# DirectPipe — Claude Code Project Guide v8.0

## Project Description
Cross-platform real-time VST2/VST3 host. Windows (stable), macOS (beta), Linux (experimental). Processes microphone input through a plugin chain. Main output goes to AudioSettings Output device, with optional separate monitor output (headphones) via independent shared-mode device. Focused on external control (hotkeys, MIDI, Stream Deck, HTTP API) and fast preset switching. Similar to Light Host but with remote control capabilities.

크로스 플랫폼 실시간 VST2/VST3 호스트. Windows(안정), macOS(베타), Linux(실험적). 마이크 입력을 플러그인 체인으로 처리. 메인 출력은 AudioSettings Output 장치로 직접 전송, 별도 공유 모드 모니터 출력(헤드폰) 옵션. 외부 제어(단축키, MIDI, Stream Deck, HTTP API)와 빠른 프리셋 전환에 초점.

> **v4.0.1은 v3.10.3에서 아키텍처 리팩토링 + 크로스플랫폼 확장 + 내장 프로세서를 추가한 정식 릴리즈입니다.**
> MainComponent를 7개 focused module로 분할, Platform/ 추상화 레이어 도입, 테스트 52→295개 확장.

## Documentation Sync Rule (필수)

코드를 수정할 때 아래 문서 동기화를 반드시 수행한다:

1. **파일 추가/삭제/이름 변경** → 해당 모듈 `host/Source/{Module}/README.md`의 File List 업데이트
2. **클래스/메서드 추가/삭제** → 해당 모듈 README.md + 이 파일의 Quick Reference 테이블 업데이트
3. **스레드 모델 변경** (새 스레드, 락 추가, atomic 변경) → 해당 .h 어노테이션 + 모듈 README 스레드 모델 + 이 파일의 스레드/락 테이블 업데이트
4. **새 Action 추가** → 이 파일 하단 "Maintenance Recipes" 체크리스트 따라 모든 파일 업데이트
5. **새 Platform 추가** → 이 파일 하단 "Maintenance Recipes" 체크리스트 따라 모든 파일 업데이트
6. **DANGER ZONE 관련 코드 수정** → README DANGER ZONES 항목 + .h/.cpp WARNING 주석 확인

> 위 규칙을 따르지 않으면 문서와 코드가 불일치하여 이후 유지보수에서 오류가 발생한다.

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
- C++17, JUCE 7.0.12, CMake 3.22+, project version 4.0.1
- **Windows**: WASAPI Shared + Low Latency + Exclusive Mode + ASIO (Steinberg ASIO SDK)
- **macOS**: CoreAudio + AU hosting. Universal binary (arm64+x86_64). Deployment target 10.15
- **Linux**: ALSA + JACK (PipeWire via JACK compat)
- VST2 SDK 2.4 + VST3 (all platforms), AU (macOS only)
- IPC: Windows CreateFileMapping/CreateEvent, POSIX shm_open/sem_open
- WebSocket: JUCE StreamingSocket + RFC 6455 (handshake, framing, custom SHA-1)
- HTTP: JUCE StreamingSocket manual parsing
- Stream Deck: @elgato/streamdeck SDK v2.0.1, SDKVersion 3, Node.js 20

## Quick Reference

### Thread Ownership Table

| Class | Method/Area | Thread | Notes |
|-------|------------|--------|-------|
| AudioEngine | audioDeviceIOCallbackWithContext | RT (audio) | No alloc, no mutex, no logging |
| AudioEngine | checkReconnection | Message (30Hz timer) | Timer callback; also drains chainCrashed_ notification (deferred from RT) |
| AudioEngine | audioDeviceAboutToStart | Device thread | SpinLock for desiredDevice_ reads; non-atomic writes deferred via callAsync; BS=0/SR=0 guard skips prepare |
| AudioEngine | setInputDevice/setOutputDevice | Message | May restart device |
| VSTChain | processBlock | RT (audio) | NO chainLock_ |
| VSTChain | addPlugin/removePlugin/movePlugin | Message | Holds chainLock_ |
| VSTChain | replaceChainAsync | Message → BG → callAsync → Message | alive_ guard + generation counter |
| OutputRouter | routeAudio | RT (audio) | Atomics only |
| MonitorOutput | audioDeviceIOCallbackWithContext | RT (monitor device) | Separate RT thread from main |
| MonitorOutput | setMonitorDevice | Message | Device config change |
| ActionDispatcher | dispatch | Any → Message | callAsync if off-thread |
| StateBroadcaster | updateState | Any → Message | callAsync if off-thread |
| WebSocketServer | serverThread/clientThread | Own threads | dispatch via ActionDispatcher |
| HttpApiServer | serverThread | Own thread | dispatch via ActionDispatcher |
| HotkeyHandler | WM_HOTKEY / CGEventTap | Platform message thread | dispatch via ActionDispatcher |
| MidiHandler | handleIncomingMidiMessage | JUCE MIDI callback | dispatch OUTSIDE bindingsMutex_ |
| StatusUpdater | tick | Message (30Hz) | Caches values for efficiency |
| PluginPreloadCache | preloadAllSlots | BG thread | COM init on Windows, generation counter |
| UpdateChecker | checkForUpdate | BG thread | alive_ flag guard |
| SharedMemWriter | writeAudio | RT (audio) | Lock-free, pre-allocated buffers |
| AudioRecorder | writeAudio | RT (audio) | SpinLock for writer lifecycle |
| SafetyLimiter | process | RT (audio) | Atomics only, no alloc |
| SafetyLimiter | setEnabled/setCeiling | Any | Atomic writes |
| BuiltinFilter | processBlock | RT (audio) | IIR filters, atomics only |
| BuiltinFilter | prepareToPlay | Message | Coefficient calculation |
| BuiltinNoiseRemoval | processBlock | RT (audio) | RNNoise FIFO, no alloc |
| BuiltinNoiseRemoval | prepareToPlay/releaseResources | Message | rnnoise_create/destroy |
| BuiltinAutoGain | processBlock | RT (audio) | K-weighting sidechain, incremental LUFS |
| BuiltinAutoGain | prepareToPlay | Message | Ring buffer allocation |
| SettingsAutosaver | tick/saveNow | Message (30Hz) | Dirty-flag + debounce |

### Lock Hierarchy (이 순서로만 acquire)

```
1. chainLock_ (VSTChain) — chain_ 벡터 + graph 노드 보호
2. cacheMutex_ (PluginPreloadCache) — cache_ map 보호
3. bindingsMutex_ (MidiHandler) — bindings_ 보호
4. clientsMutex_ (WebSocketServer) — clients_ 벡터 보호
   4a. sendMutex (WebSocketServer, per-connection) — 소켓 동시 쓰기 방지
   4b. handlersMutex_ (HttpApiServer) — handlerThreads_ 벡터 보호
5. stateMutex_ (StateBroadcaster) — state_ 보호
6. listenerMutex_ (ActionDispatcher/StateBroadcaster) — listeners_ 보호
7. writeMutex_ (DirectPipeLogger) — 로그 쓰기 보호
S. desiredDeviceLock_ (AudioEngine, SpinLock) — desiredInputDevice_/desiredOutputDevice_ 보호 (RT-safe, 짧은 임계 구간)

⛔ 금지: chainLock_ 내에서 writeToLog 호출 (교착 위험: chainLock_ → writeMutex_)
⛔ 금지: clientsMutex_ 내에서 thread.join() (교착 위험)
⛔ 금지: handlersMutex_ 내에서 thread.join() (교착 위험)
```

### Reusable Patterns

| Pattern | Description | Used By |
|---------|-------------|---------|
| `alive_` flag | `shared_ptr<atomic<bool>>`, destructor에서 false 설정, callAsync 람다에서 값 캡처 | ActionDispatcher, StateBroadcaster, VSTChain, UpdateChecker, AudioEngine, MonitorOutput, PluginScanner, HttpApiServer, PresetManager, MidiHandler, SettingsAutosaver |
| SafePointer | JUCE Component 포인터, 삭제 시 null, callAsync에서 UI 안전 | MainComponent, PluginChainEditor, HotkeyTab, MidiTab, LogPanel, OutputPanel, PresetSlotBar, PluginScanner |
| copy-before-iterate | 리스너 벡터 복사 후 락 밖에서 순회 (재진입 안전) | ActionDispatcher, StateBroadcaster |
| ActionResult | `[[nodiscard]]` ok()/fail(msg) 구조화 에러 처리. 반환값 무시 시 컴파일 경고 | AudioEngine 디바이스 메서드, MonitorOutput |
| atomicWriteFile | `[[nodiscard]]` .tmp → .bak → rename (crash-safe 파일 저장). 반환값 무시 시 컴파일 경고 | PresetManager, SettingsExporter, ControlMapping, OutputPanel, MacAutoStart, LinuxAutoStart |
| generation counter | uint32_t atomic 카운터, 오래된 콜백 폐기 | VSTChain asyncGeneration_, PluginPreloadCache preloadGeneration_ |
| AtomicGuard/BoolGuard | RAII 스코프 가드 — 생성 시 true, 소멸 시 false. early return 안전 (`Util/ScopedGuard.h`) | AudioEngine intentionalChange_/attemptingReconnection_, ActionHandler loadingSlot_ |
| PluginIndex/SlotIndex | 강타입 인덱스 래퍼 — 서로 다른 인덱스 타입 혼동 시 컴파일 에러 (`Util/StrongIndex.h`) | VSTChain movePlugin |
| DeviceState enum | 디바이스 상태 머신 — 4개 atomic bool 대신 명시적 enum 상태. switch문 누락 경고 (`Audio/DeviceState.h`) | AudioEngine getDeviceState() |
| ThreadOwned | 스레드 소유권 래퍼 — 디버그 빌드에서 잘못된 스레드 접근 감지 (`Util/ThreadOwned.h`) | (점진 적용 예정) |

> **동기화 대상**: Audio/README.md, Control/README.md, UI/README.md, AudioEngine.h, VSTChain.h, WebSocketServer.h 등의 Thread Ownership 어노테이션

## Build
```bash
# cmake is NOT in PATH on this machine — use full path:
# "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Run tests independently:**
```bash
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe   # Host tests (244)
./build/bin/Release/directpipe-tests.exe                                           # Core/IPC tests (51)
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
- **ActionHandler**: Consolidated action routing from ActionDispatcher to AudioEngine/UI. `doPanicMute(bool)` consolidates panic mute logic (single implementation, was scattered in v3 MainComponent): saves pre-mute state (monitor, output, IPC), mutes everything, **stops active recording**. On unmute, restores previous state (recording does not auto-restart). **All action cases** in `handle()` check `engine_.isMuted()` to block during panic — PluginBypass, MasterBypass, InputGainAdjust, SetVolume, SetPluginParameter, LoadPreset, SwitchPresetSlot, NextPreset, PreviousPreset, RecordingToggle, MonitorToggle, IpcToggle. Only PanicMute/InputMuteToggle/XRunReset/SafetyLimiterToggle/SetSafetyLimiterCeiling/AutoProcessorsAdd bypass the check (they ARE the panic toggle or safe to run anytime). Callback-based decoupling from MainComponent (`onDirty`, `onNotification`, `onPanicStateChanged`, `onRecordingStopped`, etc.). Owns pre-mute state (`preMuteMonitorEnabled_`, `preMuteOutputMuted_`, `preMuteVstEnabled_`).
- **ActionResult**: `ActionResult.h` — ok/fail pattern with `onError` callback. Replaces v3's bare bool/void returns for structured error handling.
- **SettingsAutosaver**: Dirty-flag pattern with 1-second debounce timer (~30Hz tick). `markDirty()` sets flag, `tick()` counts down cooldown, `saveNow()` for immediate save. Callback-based decoupling (`onPostLoad`, `onShowWindow`, `onRestorePanicMute`, `onFlushLogs`). `loadFromFile()` wraps `triggerPreload()` in `callAsync` (audio device must be fully started before preload).
- **StatusUpdater**: Periodic UI status updates at ~30Hz. Caches all status values (mute states, latency, CPU, format, gain, outputVolume, xrunCount) to avoid redundant repaints. Updates level meters, mute button colours, status bar labels, input gain slider sync, and broadcasts full state to WebSocket clients. State broadcast includes `volumes.output` and `xrun_count` fields. Pointer-based UI binding via `setUI()`.
- **PresetSlotBar**: A-E quick preset slot buttons. Slot naming with pipe delimiter (`A|게임`, max 8 chars). Right-click context menu (Rename/Copy/Delete/Export/Import). Callback-based (`onSlotSwitch`, `onDirty`, `onNotification`). Shared atomics `loadingSlot_`/`partialLoad_` by reference.
- **UpdateChecker**: Separate class (was inline in v3 MainComponent). Background GitHub API polling for new releases. Semver comparison. Updates credit label text on newer version found. Click triggers update dialog with [Update Now] (Windows only) / [View on GitHub] (macOS/Linux) / [Later]. Uses `alive_` flag for async safety. **Platform-aware asset selection**: 1st pass matches platform tag (`Windows`+`.zip`, `macOS`+`.dmg`, `Linux`+`.tar.gz`), 2nd pass falls back to any `DirectPipe*.zip` (legacy releases), 3rd pass falls back to `DirectPipe*.exe`. **SHA-256 verification**: downloads `checksums.sha256` from release, verifies downloaded file integrity (skipped if checksums file not found). **`escapePSQuote`**: escapes single quotes in PowerShell commands for paths with special characters. See workspace `CLAUDE.md` Section 2 for full release asset naming rules.
- **HotkeyTab / MidiTab / StreamDeckTab**: Each Controls sub-tab is a separate .h/.cpp file (was all inline in v3 ControlSettingsPanel). HotkeyTab: BindingRow drag-and-drop, key recording. MidiTab: device selector, Learn mode, 3-step param popup. StreamDeckTab: WS/HTTP server status. ControlSettingsPanel is now a slim tabbed container (~75 lines).
- **PluginLoadHelper**: `PluginLoadHelper.h` — helper for cross-platform VST loading.
- **Platform/ abstraction layer**: Header-only interfaces with per-OS implementations.
  - `PlatformAudio.h` (inline): `getDefaultSharedDeviceType()` abstracts driver names across OS.
  - `AutoStart.h` → `setAutoStartEnabled()` returns `bool` (false on failure). Windows: Registry `HKCU\...\Run`. macOS: `~/Library/LaunchAgents/com.directpipe.host.plist`. Linux: `~/.config/autostart/directpipe.desktop` (XDG).
  - `ProcessPriority.h` → Windows: HIGH_PRIORITY_CLASS + timeBeginPeriod(1) + Power Throttling disable. macOS: setpriority(-20) + pthread QoS. Linux: setpriority(-10).
  - `MultiInstanceLock.h` → Windows: Named Mutexes. macOS/Linux: JUCE InterProcessLock (file-based flock).

### Audio
- **Built-in Processors**: BuiltinFilter (HPF+LPF), BuiltinNoiseRemoval (RNNoise+FIFO+VAD), BuiltinAutoGain (LUFS+Leveler). All inherit AudioProcessor, inserted into AudioProcessorGraph alongside VSTs. [Auto] button (green when active) = special preset slot (index 5, `PresetSlotBar::kAutoSlotIndex`, non-renameable, rotation-excluded) next to input gain slider; first click creates default (Filter+NR+AGC), subsequent clicks load saved state; right-click for Reset to Defaults. Factory Reset includes Auto slot (deletes `slot_Auto.dppreset` via wildcard, message says "(A-E + Auto)"). **BuiltinFilter**: HPF default ON 60Hz, LPF default OFF 16kHz. `isBusesLayoutSupported`: mono + stereo. `getLatencySamples()` = 0. **RNNoise**: requires x32767 scaling before, /32767 after. 2-pass FIFO for in-place buffer safety. Ring buffer output FIFO uses `(write - read) > 0u` for uint32_t modular-safe drain. 48kHz only (non-48kHz = passthrough, TODO: resampling). Dual-mono (2 RNNoise instances). `getLatencySamples()` = 480 via `setLatencySamples()`. VAD gate hold time: 300ms (`kHoldSamples` = 14400 at 48kHz). Gate smoothing: 20ms (`kGateSmooth` = 0.9990). Gate starts CLOSED (0.0) with 5-frame warmup before VAD active. **AGC**: WebRTC-inspired dual-envelope level detection (fast 10ms/200ms + slow 0.4s LUFS, max selection) + direct gain computation (no IIR gain envelope). Target LUFS -15.0 default (range -24 to -6, internal -6dB offset for overshoot compensation). Low Correct 0.50 default = hold↔full correction blend (boost). High Correct 0.90 default = hold↔full correction blend (cut). Max Gain 22 dB default. Freeze Level -45 dBFS (per-block RMS gate, NOT LUFS): holds current gain during silence (NOT reset to 0dB), bypassed when < -65 dBFS. LUFS window 0.4s (EBU Momentary). lowCorr/hiCorr = blend between hold and full correction per block (NOT envelope speed). K-weighting: ITU-R BS.1770 sidechain (copy, not applied to actual audio). Incremental `runningSquareSum_` (O(blockSize) not O(windowSize)). **VAD thresholds**: Light 0.50, Standard 0.70 (default), Aggressive 0.90.
- **Audio drivers**: Windows: 5 types — DirectSound (legacy), Windows Audio (WASAPI Shared, recommended), Windows Audio (Low Latency), Windows Audio (Exclusive Mode), ASIO. macOS: CoreAudio. Linux: ALSA, JACK. `PlatformAudio::getDefaultSharedDeviceType()` abstracts driver names. Runtime switching. ASIO: single device, dynamic SR/BS, channel routing.
- **Audio optimizations**: `timeBeginPeriod(1)` for 1ms timer resolution (matched by `timeEndPeriod` in shutdown). Power Throttling disabled for Intel hybrid CPUs. `ScopedNoDenormals` in audio callback. Muted fast-path (skip VST processing). RMS decimation (every 4th callback). Buffer page pre-touch in `audioDeviceAboutToStart`. **DPC latency countermeasures** (Windows): MMCSS "Pro Audio" thread registration at AVRT_PRIORITY_HIGH on audio callback thread (covers both WASAPI and ASIO; JUCE only registers WASAPI at AVRT_PRIORITY_NORMAL, and does NO MMCSS for ASIO). IPC `SetEvent` optimization (signal only when data written). LatencyMonitor callback overrun detection (`getCallbackOverrunCount()`).
- **Device auto-reconnection**: Dual mechanism — `ChangeListener` on `deviceManager_` for immediate detection + 3s timer polling fallback (`checkReconnection`). Tracks `desiredInputDevice_`/`desiredOutputDevice_`. `deviceLost_` set by `audioDeviceError`, cleared by `audioDeviceAboutToStart`. `attemptReconnection()` preserves SR/BS/channel routing (ASIO safe). Re-entrancy guard (`attemptingReconnection_`). NotificationBar: Warning on disconnect, Info on reconnect. Monitor device uses same pattern independently (`monitorLost_` + own cooldown). **Per-direction loss**: `inputDeviceLost_` silences input in audio callback. `outputAutoMuted_` auto-mutes on device loss, auto-unmutes on restore. `reconnectMissCount_` after 5 failed attempts. AudioSettings combos show "DeviceName (Disconnected)" for lost devices.
- **Driver type snapshot/restore**: `DriverTypeSnapshot` struct stores per-driver-type settings (input/output device, SR, BS, `outputNone`) in `driverSnapshots_` map. ASIO switch builds ordered `tryOrder` list (preferred → lastAsio → all remaining) and iterates all devices before reverting. `AudioSettings::onDriverTypeChanged` checks return value and syncs driver combo on failure.
- **Device fallback protection**: `intentionalChange_` flag in AudioEngine prevents `audioDeviceAboutToStart` from treating JUCE auto-fallback as successful reconnection. MonitorOutput rejects JUCE auto-fallback devices. OutputPanel shows "Fallback: DeviceName" in orange. Settings export uses desired device names.
- **audioDeviceAboutToStart thread safety**: Called on device thread. `desiredInputDevice_`/`desiredOutputDevice_` protected by `desiredDeviceLock_` SpinLock (reads on device thread, writes deferred via callAsync to message thread). Other non-atomic writes (`reconnectMissCount_`, etc.) deferred to message thread via `callAsync` with `alive_` guard. **BS=0/SR=0 guard**: if device reports invalid SR/BS, logs warning and skips prepareToPlay (prevents division-by-zero). MMCSS function pointers cached here (write-once, then read from RT callback). `mmcssRegistered_` reset to false for re-registration on new audio thread. **chainCrashed_ NOT reset here** — device events (WASAPI session changes, ASIO BS change) fire without chain change; instead cleared by `clearChainCrash()` on chain structure change (plugin add/remove/slot switch).
- **processBlockSEH (Windows SEH crash guard)**: `processBlockSEH()` helper wraps `VSTChain::processBlock` in `__try/__except` to catch Windows SEH exceptions (access violations) that `try/catch(...)` cannot. Extracted into a separate function because MSVC forbids `__try` in functions with C++ destructors on stack. On non-Windows: `try/catch(...)` fallback. On exception: buffer cleared + `chainCrashed_` set atomically.
- **chainCrashed_ deferred notification**: `chainCrashed_` is set on RT thread (no logging/alloc). `chainCrashNotified_` one-shot flag prevents repeated notifications. `checkReconnection()` (30Hz timer, message thread) detects the flag and pushes user notification. `clearChainCrash()` resets both flags — called on chain structure change (not device restart).
- **Main output**: Processed audio written directly to outputChannelData. Works with WASAPI, ASIO, CoreAudio, ALSA, JACK.
- **Output "None" mode**: `setOutputNone(bool)` / `isOutputNone()` — `outputNone_` atomic flag mutes output and locks OUT button. Cleared on driver type switch. `DriverTypeSnapshot` saves/restores per driver type.
- **Monitor output**: Separate shared-mode AudioDeviceManager (WASAPI/CoreAudio/ALSA) + lock-free AudioRingBuffer for headphone monitoring. Independent of main driver type. Status indicator (Active/SampleRateMismatch/Error/No device). Auto-reconnection via `monitorLost_` atomic + 3s timer polling. Monitor underrun tracking via `underrunCount_` atomic counter. `getUnderrunCount()` accessor for diagnostics.
- **XRun monitoring**: Rolling 60-second window. `xrunResetRequested_` atomic flag pattern. Status bar shows count with red highlight.
- **Safety Limiter**: `SafetyLimiter.h/cpp` — RT-safe feed-forward limiter (0.1ms attack, 50ms release). Inserted after VSTChain, before Recording/IPC/Monitor/Output. Atomic params: `enabled` (default true), `ceilingdB` (-0.3 dBFS default, range -6.0~0.0). GR feedback via atomic for UI. No look-ahead, no PDC contribution. PluginChainEditor: ceiling slider (above Add Plugin) + Safety Limiter toggle button. Status bar [LIM] indicator.
- **Plugin Latency Data**: `VSTChain::getPluginLatencies()` + `getTotalChainPDC()` query plugin PDC values under chainLock_. Per-plugin latency display and chain PDC summary were removed from the UI (UX feedback), but the data is still available in the state broadcast: `plugins[].latency_samples`, `chain_pdc_samples`, `chain_pdc_ms`.

### Preset & Plugin
- **Quick Preset Slots (A-E)**: Chain-only. Plugin state via getStateInformation/base64. Async loading (replaceChainAsync with `alive_` flag). **Keep-Old-Until-Ready**: old chain continues during background loading. `partialLoad_` prevents auto-save after incomplete loads. `loadingSlot_` prevents concurrent switches. **Slot naming**: `.dppreset` JSON `"name"` field, displayed as `A|게임`. Right-click → Rename/Copy/Delete/Export/Import.
- **Out-of-process VST scanner**: `--scan` child process. Auto-retry (10x), dead man's pedal. Blacklist for crashed plugins.
- **Plugin chain editor**: Drag-and-drop, bypass toggle, native GUI edit. **Plugin name for async callbacks MUST come from `slot->name`, NOT from `nameLabel_.getText()`**.
- **Plugin scanner search/sort**: Real-time text filter + column sorting (name/vendor/format).

### External Control
- **Global hotkeys**: Windows: `RegisterHotKey` + message-only window. macOS: `CGEventTap` + `AXIsProcessTrustedWithOptions` auto-prompt (accessibility permission required). Linux: stub (not supported, use MIDI/WS/HTTP). `keyToString()` uses macOS symbols (⌃⌥⇧⌘) on Mac. `onError` callback fires on macOS when accessibility permission is missing (wired to ControlManager `onNotification`).
- **Panic Mute**: Remembers pre-mute state (monitor, output mute, IPC), restores on unmute. Stops active recording on engage (does not auto-restart). During panic mute, all actions and external controls locked — only PanicMute/unmute can change state. Logic consolidated in `ActionHandler::doPanicMute()`. `input_muted` state field mirrors `muted` (no independent input mute).
- **WebSocket server**: RFC 6455 with custom SHA-1. Case-insensitive HTTP header matching (RFC 7230). Dead client cleanup. Port 8765. UDP discovery broadcast (port 8767). `broadcastToClients` releases `clientsMutex_` before socket writes (prevents shutdown hang); dead thread join outside lock.
- **IPC Toggle**: `Action::IpcToggle` toggles IPC output. WebSocket/HTTP/MIDI mappable.
- **Default hotkeys**: 11 defaults from `createDefaults()` — Panic Mute (Ctrl+Shift+M), Master Bypass (Ctrl+Shift+0), Plugin 1-3 Bypass (Ctrl+Shift+1~3), Input Mute (Ctrl+Shift+F6), Monitor Toggle (Ctrl+Shift+H), Preset Slot A-E (Ctrl+Shift+F1~F5). Users can change/add more via Controls > Hotkeys tab. Existing users keep their saved config (createDefaults() only runs on first launch).
- **HTTP server**: GET-only REST API. CORS enabled with OPTIONS preflight support for browser clients. Port 8766. Proper HTTP status codes (404/405/400). Input validation: `parseFloat()` strict validation rejects strings like "abc0.5" that `getFloatValue()` silently converts. `escapeJsonString()` prevents response injection from URL segments. Gain delta endpoint scales by 10x (compensates ActionHandler's `*0.1f` hotkey step design). **Handler socket tracking**: `HandlerThread` struct tracks per-connection thread + socket + done flag. `handlersMutex_` protects `handlerThreads_` vector. `stop()` closes all tracked sockets to unblock read(), then joins threads outside lock (same pattern as WebSocketServer). Endpoints include recording toggle, plugin parameter control, IPC toggle, output volume (`/api/volume/output/:value`), plugin list (`/api/plugins`), plugin parameters (`/api/plugin/:idx/params`), performance stats (`/api/perf`), safety limiter toggle/ceiling (`/api/limiter/toggle`, `/api/limiter/ceiling/:value`), auto processors add (`/api/auto/add`).
- **MidiHandler `alive_` flag**: Uses `shared_ptr<atomic<bool>>` pattern for callAsync lifetime guard. **Duplicate binding overwrite**: `addBinding()` checks for existing binding with same CC/channel/device/type — overwrites action instead of creating duplicate entry.
- **MIDI Learn cancel**: Cancel button in MidiTab. `stopLearn()` called on cancel. Learn timeout (30s): timer callback clears learn state directly instead of calling `stopLearn()` — avoids destroying the timer from its own callback (use-after-free).
- **MIDI HTTP API test endpoints**: `/api/midi/cc/:ch/:num/:val` and `/api/midi/note/:ch/:num/:vel`.
- **MIDI plugin parameter mapping**: MidiTab 3-step popup flow.

### UI & Settings
- **Tabbed UI**: Audio/Monitor/Controls/Settings tabs in right column. Controls has 3 sub-tabs (each in separate file): HotkeyTab, MidiTab, StreamDeckTab. ControlSettingsPanel is the slim tabbed container (~75 lines).
- **Auto-start**: Platform::AutoStart abstraction. Windows: Registry. macOS: LaunchAgent. Linux: XDG autostart. Toggle in tray menu + Settings tab.
- **System tray**: Close -> tray (macOS/Windows) or minimize to taskbar or system tray (Linux, DE 지원 시 트레이, 미지원 시 태스크바). Right-click -> Show/Quit/auto-start toggle.
- **Settings export/import**: Two tiers: `.dpbackup` (settings only) and `.dpfullbackup` (everything). **Cross-OS protection**: Backup files include `platform` field (windows/macos/linux). `getCurrentPlatform()`/`getBackupPlatform()`/`isPlatformCompatible()` block cross-OS restore. Legacy backups without platform field accepted.
- **In-app auto-updater**: Background thread fetches latest release from GitHub API. **Platform-aware asset selection** — prefers platform-tagged asset (e.g. `DirectPipe-...-Windows.zip`), falls back to legacy naming. Update Now (Windows): downloads ZIP/exe, batch script auto-restart. macOS/Linux: "View on GitHub" only (manual download). Post-update notification via `_updated.flag`. **릴리스 asset 네이밍 필수**: Windows=`.zip`, macOS=`.dmg`, Linux=`.tar.gz` — macOS/Linux를 `.zip`으로 배포하면 v3 구 업데이터가 잘못된 바이너리를 다운로드함.
- **NotificationBar**: Color-coded (red/orange/purple). Auto-fades 3-8 seconds.
- **LogPanel**: Settings tab. Real-time log viewer. Maintenance: Full Backup/Restore, Clear Plugin Cache, Clear All Presets, Factory Reset.
- **DirectPipeLogger**: Centralized logging with category tags: `[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`. High-frequency actions excluded.

### Receiver & Stream Deck
- **Receiver VST plugin**: VST2/VST3/AU plugin for OBS and DAWs. Reads shared memory IPC. Configurable buffer size (5 presets). Formats: .dll (Win VST2), .vst3 (all), .vst (macOS VST2 bundle), .component (macOS AU), .so (Linux VST2).
- **IPC drift compensation**: Bidirectional clock drift handling in Receiver VST. High-fill: excess frames skipped when buffer > highThreshold. Low-fill: read amount halved when buffer < lowThreshold, creating micro-gaps instead of hard clicks. Buffer presets include targetFill/highThreshold/lowThreshold per preset. Warmup period (50 blocks) before drift checks activate. **Drift hysteresis dead-band**: between lowThreshold/2 and lowThreshold, normal reading without throttling — prevents oscillation. **Fade-out improvement**: saves last 64 samples of output buffer (`saveLastOutput`); on underrun, plays fade-out ramp using saved data (not just last sample) for smoother transition. `isBusesLayoutSupported`: mono + stereo output. `getLatencySamples()` = targetFillFrames (updated dynamically when buffer preset changes).
- **Stream Deck plugin**: SDKVersion 3, 10 SingletonAction subclasses (7 original + Performance Monitor, Plugin Parameter [SD+], Preset Bar [SD+]), Property Inspector HTML (sdpi-components v4), event-driven reconnection, SVG icons + @2x. Pending message queue (cap 50). Packaged via `streamdeck pack` CLI. Plugin Parameter and Preset Bar PI use `GET /api/plugins` and `GET /api/plugin/:idx/params` for dynamic dropdowns. `autoReconnect: true` in plugin.js DirectPipeClient constructor. Handles `activeSlot === -1` (no slot active) and `auto_slot_active` (Auto slot selected) in preset-switch and preset-bar actions.

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
- `loadingSlot_` (std::atomic<bool>) guard prevents recursive auto-save during bulk chain operations (slot loading, Auto slot setup, MasterBypass toggle, Clear All Presets, Factory Reset)
- **VSTChain `chainLock_`**: mutable `CriticalSection` protects ALL chain access. Never held in `processBlock`. **Never call `writeToLog` inside `chainLock_`** — capture log string under lock, log after releasing (lock-ordering hazard with DirectPipeLogger `writeMutex_`).
- **VSTChain `rebuildGraph(bool suspend = true)`**: `suspend=true` for node add/remove, `suspend=false` for bypass toggle. `getConnections()` copied before removal loop (iterator safety). `setPluginBypassed` syncs `node->setBypassed()` + `getBypassParameter()->setValueNotifyingHost()` + `rebuildGraph(false)`.
- onChainChanged callback outside chainLock_ scope (deadlock prevention)
- MidiBuffer pre-allocated in prepareToPlay, cleared after processBlock
- VSTChain removes old I/O nodes before adding new ones in prepareToPlay
- ActionDispatcher/StateBroadcaster: copy-before-iterate for reentrant safety
- WebSocket broadcast on dedicated thread (non-blocking). `broadcastToClients` sweeps dead clients under `clientsMutex_`, releases lock, then writes to live sockets. Dead thread join outside lock (deadlock prevention).
- **VSTChain `asyncGeneration_`**: `uint32_t` atomic counter. Discards stale callbacks from superseded loads.
- Update check thread properly joined (no detached threads)
- AudioRingBuffer capacity must be power-of-2 (assertion enforced). `reset()` zeroes all channel data.
- AudioEngine `setBufferSize`/`setSampleRate` log errors from `setAudioDeviceSetup`
- AudioRecorder: `outputStream` deleted on writer creation failure (no leak)
- **OutputRouter**: `routeAudio()` clamps `numSamples` to `scaledBuffer_` capacity (buffer overrun prevention)
- **AudioEngine**: notification queue indices use `uint32_t` (overflow-safe)
- **HTTP API**: proper status codes (404/405/400) — `processRequest` returns `pair<int, string>`
- **LogPanel DirectPipeLogger**: ring buffer indices use `uint32_t` (overflow-safe)
- **Tray tooltip**: `activeSlot` clamped to 0-4 range in state broadcast, with separate `autoSlotActive` field for Auto slot (index 5)
- **SettingsExporter**: Two tiers — `exportAll/importAll` strips `plugins` key; `exportFullBackup/importFullBackup` includes everything. `getCurrentPlatform()`/`isPlatformCompatible()` for cross-OS protection.
- **PluginPreloadCache `invalidateAll()`**: Non-blocking — bumps all `slotVersions_` + sets `cancelPreload_` + bumps `preloadGeneration_`. Does NOT join thread (COM STA deadlock). `cancelAndWait()` uses heap-allocated `shared_ptr` for join state.
- **PluginPreloadCache `slotVersions_`**: Per-slot `atomic<uint32_t>` version counter. Bumped by `invalidateSlot`/`invalidateAll`. `preloadAllSlots` captures version at file-read time; cache store checks version match — discards stale data if slot was invalidated mid-preload.
- **PluginPreloadCache `isCachedWithStructure`**: Compares cached plugin names/paths/order. `saveSlot` invalidates only on structure change (add/remove/replace/reorder), not on parameter-only saves.
- **`[[nodiscard]]` policy**: `ActionResult`, `atomicWriteFile()`, `VSTChain::addPlugin/removePlugin/movePlugin`, `AudioRecorder::startRecording`, `SharedMemWriter::initialize`, `AudioEngine` device methods — all return values MUST be checked. Compiler warns on ignored return values.
- **jassert thread contracts**: `processBlock`/`writeAudio`/`writeBlock` assert NOT message thread (RT only). `addPlugin`/`removePlugin`/`movePlugin`/`stopLearn`/`checkReconnection` assert IS message thread. Debug builds crash on violations.
- **ScopedGuard for flag pairs**: Use `AtomicGuard` (for `std::atomic<bool>`) or `BoolGuard` (for plain `bool`) from `Util/ScopedGuard.h` instead of manual `flag = true` ... `flag = false` pairs. Guards cleanup on any exit path (return, exception). See `intentionalChange_` in AudioEngine, `attemptingReconnection_` in attemptReconnection, `loadingSlot_` in ActionHandler.
- **Strong index types**: Use `PluginIndex`/`SlotIndex`/`MappingIndex` from `Util/StrongIndex.h` for function parameters that accept indices. Prevents accidental argument swapping (e.g., `movePlugin(from, to)` with swapped args).
- **DeviceState enum**: Use `AudioEngine::getDeviceState()` for switch-based state handling instead of checking individual boolean flags. Compiler warns on missing cases. See `Audio/DeviceState.h`.
- **Atomic file write pattern**: All preset/config saves use `atomicWriteFile()`: write .tmp → rename original to .bak → rename .tmp to target. Load functions use `loadFileWithBackupFallback()`: try main file → .bak → legacy .backup. See `host/Source/Util/AtomicFileIO.h`.
- **Sample rate propagation**: Audio tab SR applies globally — VST chain, monitor output, IPC all follow main device SR.
- **MonitorOutput shutdown()**: Resets `actualSampleRate_`/`actualBufferSize_` to 0. `monitorLost_` set by `audioDeviceError`/`audioDeviceStopped`, cleared ONLY by `audioDeviceAboutToStart`.
- **AudioEngine reconnection**: `desiredInputDevice_`/`desiredOutputDevice_` saved in `audioDeviceAboutToStart` and `setInputDevice`/`setOutputDevice`. `attemptReconnection` only updates device names in setup.
- **Device combo click-to-refresh**: `addMouseListener(this, true)` on combos in constructor, `removeMouseListener` in destructor.
- **No LoadLibraryA in RT callback**: Cache function pointers (e.g., MMCSS avrt.dll) in `audioDeviceAboutToStart` — never call LoadLibraryA from the audio callback.
- **Don't destroy juce::Timer from its own timerCallback**: Use `callAsync` to defer `reset()` or destruction. Direct destruction causes use-after-free (JUCE Timer internals reference destroyed object after callback returns).
- **ASIO SR/BS policy**: ASIO devices own SR/BS globally (shared across all apps). DirectPipe must NOT force saved SR/BS on ASIO — doing so restarts the driver and disrupts other audio sources. On startup: `importFromJSON` calls `syncDesiredFromDevice()` for ASIO (accepts device's current values) instead of `setSampleRate`/`setBufferSize`. On ASIO control panel changes: `audioDeviceAboutToStart` callAsync always syncs `desiredSR/BS` from device. Non-ASIO drivers (WASAPI/CoreAudio/ALSA) use per-app SR/BS, so saved values are safely forced.
- **Startup flow**: Always open WASAPI first (safe fallback), then switch to saved driver type (ASIO etc.) via `importFromJSON`. This ensures the app starts even if the saved ASIO driver is unavailable. The WASAPI→ASIO transition is invisible (~100ms, before window shown).
- **broadcastToClients lock discipline**: Release `clientsMutex_` before socket writes — holding the lock during potentially-blocking I/O causes shutdown hangs.
- **processBlockSEH pattern (Windows)**: Wrap VST processBlock in `__try/__except` via a separate helper function (MSVC restriction). On non-Windows: `try/catch(...)`. On exception: clear buffer + set `chainCrashed_` atomic flag. Never log or allocate in the exception handler (RT thread).
- **chainCrashed_ deferred notification**: Set `chainCrashed_` atomically on RT thread (no alloc/log). Push user notification from `checkReconnection()` on message thread via `chainCrashNotified_` one-shot flag. Reset both flags via `clearChainCrash()` on chain structure change only (NOT device restart).
- **BS=0/SR=0 guard in audioDeviceAboutToStart**: If device reports invalid SR (<=0) or BS (<=0), log warning and return early — skip prepareToPlay to prevent division-by-zero and invalid buffer allocation.
- **desiredDeviceLock_ SpinLock**: Protects `desiredInputDevice_`/`desiredOutputDevice_` (juce::String) for cross-thread reads from device thread. Short critical sections only. Writes deferred to message thread via callAsync.

## Modules
- `core/` -> IPC library (RingBuffer, SharedMemory, Protocol, Constants). POSIX: shm_open + sem_open (macOS sem_trywait polling, Linux sem_timedwait). Windows: CreateFileMapping + CreateEvent.
- `host/` -> JUCE app
  - `Audio/` -> AudioEngine, VSTChain, OutputRouter, MonitorOutput, AudioRingBuffer, LatencyMonitor, AudioRecorder, PluginPreloadCache, PluginLoadHelper, **SafetyLimiter**, **DeviceState** (enum), **BuiltinFilter**, **BuiltinNoiseRemoval**, **BuiltinAutoGain**
  - `Control/` -> ActionDispatcher, **ActionHandler**, ControlManager, ControlMapping, **SettingsAutosaver**, WebSocketServer, HttpApiServer, HotkeyHandler, MidiHandler, StateBroadcaster, DirectPipeLogger (Log.h/cpp)
  - `IPC/` -> SharedMemWriter
  - `UI/` -> AudioSettings, OutputPanel, ControlSettingsPanel (slim tabbed container), **HotkeyTab**, **MidiTab**, **StreamDeckTab**, PluginChainEditor, PluginScanner, PresetManager, **PresetSlotBar**, LevelMeter, LogPanel, NotificationBar, DirectPipeLookAndFeel, SettingsExporter, **StatusUpdater**, **UpdateChecker**, DeviceSelector, **FilterEditPanel**, **NoiseRemovalEditPanel**, **AGCEditPanel**
  - `Platform/` -> PlatformAudio.h (inline), AutoStart.h, ProcessPriority.h, MultiInstanceLock.h
    - `Windows/` -> WindowsAutoStart, WindowsProcessPriority, WindowsMultiInstanceLock (Registry, Named Mutex, Win32)
    - `macOS/` -> MacAutoStart, MacProcessPriority, MacMultiInstanceLock (LaunchAgent, setpriority, InterProcessLock)
    - `Linux/` -> LinuxAutoStart, LinuxProcessPriority, LinuxMultiInstanceLock (XDG .desktop, setpriority, InterProcessLock)
  - `Util/AtomicFileIO.h` — Atomic file write + backup recovery (header-only)
  - `Util/ScopedGuard.h` — AtomicGuard/BoolGuard RAII scope guards (header-only)
  - `Util/StrongIndex.h` — PluginIndex/SlotIndex/MappingIndex strong types (header-only)
  - `Util/ThreadOwned.h` — Thread ownership wrapper with debug assertions (header-only)
  - Root: MainComponent, Main, **ActionResult.h**
- `plugins/receiver/` -> Receiver VST2/VST3/AU plugin for OBS and DAWs (shared memory IPC consumer, configurable buffer size)
- `com.directpipe.directpipe.sdPlugin/` -> Stream Deck plugin (Node.js, @elgato/streamdeck SDK v3)
- `tests/` -> Google Test (core tests + host tests, **295 tests: 51 core (6 suites) + 244 host (18 suites)**)
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
- Stream Deck: SDKVersion 3 (SDK v2.0.1 npm), Version 4.0.0.0, 10 actions (Bypass Toggle, Panic Mute, Volume Control, Preset Switch, Monitor Toggle, Recording Toggle, IPC Toggle, Performance Monitor, Plugin Parameter [SD+], Preset Bar [SD+]), 5 PI HTMLs, SVG-based icons + @2x, packaged via `streamdeck pack` CLI
- Auto-save: dirty-flag + 1s debounce (not periodic timer), managed by SettingsAutosaver
- License: GPL v3 (JUCE GPL compatibility). JUCE_DISPLAY_SPLASH_SCREEN=0
- Credit label "Created by LiveTrack" at bottom-right. Shows "NEW vX.Y.Z" in orange.
- **AudioEngine `setBufferSize` auto-fallback**: If driver rejects requested size, finds closest device-supported size.
- **XRun tracking thread safety**: Two separate atomic flags — `xrunResetRequested_` (user action, full clear) and `xrunBaselineResync_` (device restart, baseline resync only). Non-atomic xrun history vars only on message thread.
- **RMS decimation**: `rmsDecimationCounter_` (RT thread only, no atomic needed). Every 4th callback.
- Portable mode: `portable.flag` next to exe -> config stored in `./config/`
- **Multi-instance external control priority**: Platform::MultiInstanceLock abstraction. `acquireExternalControlPriority()` returns 1/0/-1.
- **LogPanel Quit button**: Red "Quit" button in Settings tab.
- Safety Limiter: default enabled, ceiling -0.3 dBFS. Settings persist in dppreset. Actions: SafetyLimiterToggle, SetSafetyLimiterCeiling (no panic mute guard).
- RNNoise: BSD-3 (thirdparty/rnnoise/), 48kHz only (non-48kHz = passthrough, TODO: resampling), 480-frame FIFO (~10ms latency), dual-mono (2 instances). x32767 scaling before, /32767 after. 2-pass FIFO for in-place buffer safety. Ring buffer output FIFO uses `(write - read) > 0u` for uint32_t modular-safe drain. Gate starts CLOSED (0.0) with 5-frame warmup. VAD gate hold time 300ms (kHoldSamples=14400 at 48kHz). Gate smoothing 20ms (kGateSmooth=0.9990). `getLatencySamples()` = 480 via `setLatencySamples()`
- AGC architecture: WebRTC-inspired dual-envelope level detection (fast 10ms/200ms + slow 0.4s LUFS, max selection) + direct gain computation (no IIR gain envelope). Defaults: target -15 LUFS (internal -6dB offset for overshoot compensation), low correct 0.50 (boost blend), high correct 0.90 (cut blend), max gain 22 dB, freeze level -45 dBFS (per-block RMS hold, NOT LUFS). LUFS window 0.4s (EBU Momentary). K-weighting ITU-R BS.1770 sidechain (copy, not applied to audio). Incremental runningSquareSum_ (O(blockSize)). Freeze holds current gain (NOT reset to 0dB)
- BuiltinFilter: HPF default ON 60Hz, LPF default OFF 16kHz. isBusesLayoutSupported: mono + stereo. getLatencySamples() = 0
- VAD thresholds: Light 0.50, Standard 0.70 (default), Aggressive 0.90
- Auto button: green when active. First click creates default (Filter+NR+AGC), subsequent loads saved. Right-click: Reset to Defaults. Excluded from Next/Previous cycling
- Factory Reset: includes Auto slot (slot_Auto.dppreset deleted via wildcard, message says "(A-E + Auto)")
- Output Volume slider is in AudioSettings (Audio tab), not OutputPanel
- Safety Limiter ceiling slider is in PluginChainEditor (above Add Plugin), not OutputPanel
- MUTE buttons (OUT/MON/VST): height 30-32px
- Receiver: `isBusesLayoutSupported` accepts mono + stereo output. `getLatencySamples()` = targetFillFrames (dynamic). Drift dead-band hysteresis between lowThreshold/2 and lowThreshold. Fade-out uses saved 64-sample buffer tail (not last sample value)
- Stream Deck: `autoReconnect: true`, handles `activeSlot === -1` (no slot active) and `auto_slot_active` (Auto slot). Clears stale state on reconnect
- Build: RNNoise x86 sources guarded for ARM (`CMAKE_SYSTEM_PROCESSOR` check). VST2 target conditional on SDK presence (`DIRECTPIPE_HAS_VST2`). `desiredDeviceLock_` SpinLock protects device name strings across threads
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

**v4.0.x는 정식 릴리즈(latest)입니다.** v3 사용자의 자동 업데이터가 v4를 감지합니다. 릴리스 asset 이름에 플랫폼 태그(-Windows.zip, -macOS.dmg, -Linux.tar.gz)가 포함되어 있어 올바른 바이너리를 다운로드합니다.

**문서 업데이트도 필수**: 코드 변경 시 README.md, docs/ 내 관련 문서, 이 CLAUDE.md도 현재 상태에 맞게 업데이트해야 합니다.

## Common Bug Patterns

코드 수정 시 자주 발생하는 버그 패턴과 진단 방법.

### RT 스레드 위반
- **증상**: 오디오 글리치, 드롭아웃, XRun 증가
- **원인**: `audioDeviceIOCallbackWithContext` 내에서 heap 할당(`new`, `std::vector::push_back`), mutex 잠금, `writeToLog` 호출
- **진단**: RT 콜백 코드 경로에서 allocation/lock 검색. `LatencyMonitor::getCpuUsagePercent()` 확인
- **참조**: Audio/README.md DANGER ZONE 1

### callAsync use-after-free
- **증상**: 랜덤 크래시 (특히 앱 종료 시)
- **원인**: BG 스레드의 callAsync 람다가 이미 파괴된 객체의 `this` 접근
- **진단**: 해당 클래스에 `alive_` 플래그 있는지 확인. 소멸자에서 `alive_->store(false)` 호출되는지. 람다에서 `aliveFlag->load()` 체크 있는지
- **참조**: Quick Reference > Reusable Patterns > `alive_` flag

### Lock ordering 교착
- **증상**: 앱 행 (UI 멈춤, 셧다운 안 됨)
- **원인**: Lock Hierarchy 순서 위반 (예: `chainLock_` 안에서 `writeToLog` → `writeMutex_`)
- **진단**: Quick Reference > Lock Hierarchy 확인. 새 lock 추가 시 순서 결정 필수
- **참조**: CLAUDE.md Lock Hierarchy

### 크로스스레드 non-atomic 접근
- **증상**: 간헐적 잘못된 값, ARM(macOS)에서만 발생하는 버그
- **원인**: 두 스레드에서 동시에 읽기/쓰기하는 변수가 `std::atomic<>` 아님
- **진단**: 변수의 Thread Ownership 어노테이션 확인. `[RT write, Message read]` 패턴이면 반드시 atomic
- **참조**: 각 .h 파일의 Thread Ownership 섹션

### 프리셋 저장 데이터 손실
- **증상**: 앱 재시작 시 설정/프리셋이 초기화됨
- **원인**: `atomicWriteFile()` 대신 `replaceWithText()` 사용 → 전원 중단 시 파일 손상
- **진단**: 저장 코드에서 `atomicWriteFile` 사용 확인. `.bak` 파일 존재 여부
- **참조**: Util/README.md, Quick Reference > atomicWriteFile

### WebSocket 클라이언트 끊김
- **증상**: Stream Deck 연결이 간헐적으로 끊김
- **원인**: clientThread 종료 시 소켓 미닫음 → broadcastToClients sweep 미감지 → 실패한 write
- **진단**: clientThread에서 conn->socket->close() 호출 확인. sendFrame 반환값 확인
- **참조**: Control/README.md DANGER ZONE 4, 4a

### suspendProcessing 카운터 불균형
- **증상**: 프리셋 로드 후 오디오가 영구 mute (1-2초가 아닌 영구)
- **원인**: `rebuildGraph(suspend=true)` 호출 전에 `suspendProcessing(true)` 별도 호출 → 카운터 2로 증가, `rebuildGraph` 내부에서 1만 감소 → 카운터 1 유지 → 영구 suspend
- **진단**: `replaceChainAsync`/`replaceChainWithPreloaded`에서 `suspendProcessing(true)` 호출 위치 확인. `rebuildGraph(true)` 직전에 호출되면 버그
- **참조**: Audio/README.md DANGER ZONE 11

### JUCE Timer 스레드 위반
- **증상**: 앱 종료 시 크래시, 타이머 콜백이 이상한 시점에 호출
- **원인**: `juce::Timer` 또는 `Timer` 파생 객체를 Message thread 아닌 곳에서 파괴 (`unique_ptr.reset()` 등)
- **진단**: Timer 소유 객체의 소멸자/shutdown 코드에서 `isThisTheMessageThread()` 확인. `stopTimer()`는 안전, `reset()`은 위험
- **참조**: Audio/README.md DANGER ZONE 14

### SafePointer BG 스레드 생성
- **증상**: 간헐적 크래시, Component 삭제 시 corruption
- **원인**: `Component::SafePointer`를 BG 스레드에서 생성 → WeakReference 등록이 thread-safe 아님
- **진단**: callAsync 람다에서 SafePointer 사용하는 코드 검색. BG 스레드의 `run()` 내부면 `alive_` 패턴으로 교체 필요
- **참조**: Audio/README.md DANGER ZONE 15, Quick Reference > Reusable Patterns

### LoadLibraryA in RT callback (RT 위반)
- **증상**: 오디오 글리치, 높은 DPC 레이턴시 스파이크
- **원인**: RT 오디오 콜백에서 `LoadLibraryA` 호출 (커널 호출, 힙 할당, 파일 I/O 가능)
- **진단**: 오디오 콜백 코드 경로에서 `LoadLibrary` 호출 검색
- **해결**: `audioDeviceAboutToStart`에서 함수 포인터 캐시, RT 콜백에서는 캐시된 포인터만 사용

### Timer 자기 파괴 (use-after-free)
- **증상**: 타이머 콜백 후 크래시, 특히 학습 타임아웃 시
- **원인**: `juce::Timer`의 `timerCallback` 내부에서 타이머 소유 `unique_ptr`을 `reset()` → JUCE 내부가 파괴된 객체 참조
- **진단**: Timer 콜백에서 `reset()`, `= nullptr`, 소멸자 호출 패턴 검색
- **해결**: `callAsync`로 destruction 지연, 또는 Timer 콜백에서 상태만 변경하고 소유자가 나중에 정리

### loadingSlot_ 누락으로 중간 상태 자동 저장
- **증상**: 프리셋 전환/Master Bypass 중 중간 상태가 저장됨 (일부 플러그인만 bypass된 상태 등)
- **원인**: 여러 플러그인을 순차 변경하는 bulk operation에서 `loadingSlot_` 가드 없이 실행 → SettingsAutosaver가 각 변경마다 dirty flag 설정
- **진단**: `setPluginBypassed` 루프, `removePlugin` 루프 등에서 `AtomicGuard loadGuard(loadingSlot_)` 존재 확인
- **해결**: `AtomicGuard loadGuard(loadingSlot_)` RAII 가드로 bulk operation 감싸기

### Plugin processBlock 크래시 (chainCrashed_)
- **증상**: 갑자기 오디오 출력이 무음이 되고 "Plugin crash detected" 알림 표시
- **원인**: 써드파티 VST 플러그인이 processBlock에서 access violation (Windows SEH) 또는 예외 발생
- **진단**: `chainCrashed_` atomic flag 확인. `processBlockSEH` 로그 확인. 문제 플러그인 식별 후 제거
- **해결**: `clearChainCrash()` 호출 (chain structure 변경 시 자동). 문제 플러그인은 체인에서 제거
- **참조**: Audio/AudioEngine.cpp `processBlockSEH` helper, `chainCrashed_`/`chainCrashNotified_` flags

### broadcastToClients 락 보유 중 소켓 쓰기 (셧다운 행)
- **증상**: 앱 종료 시 행 (hang), 특히 클라이언트 연결 중 종료
- **원인**: `clientsMutex_` 보유 상태에서 `sendFrame` 호출 → 소켓 쓰기가 블로킹될 수 있음 → 다른 스레드가 lock 대기 → 교착
- **진단**: `broadcastToClients`에서 락 범위 확인 — 소켓 쓰기가 락 안에 있으면 버그
- **해결**: 락 안에서 live 클라이언트 목록만 복사, 락 해제 후 소켓 쓰기

## Maintenance Recipes

### 새 Action 추가하기

1. `Control/ActionDispatcher.h` — `Action` enum에 새 값 추가
2. `Control/ActionDispatcher.h` — `actionToString()` **AND** `actionToDisplayName()` 양쪽에 case 추가 (둘 다 같은 헤더 파일의 inline 함수)
   - ⚠️ `actionToDisplayName()` 누락 시 HotkeyTab/MidiTab UI에서 "Unknown"으로 표시됨
3. `Control/ActionHandler.cpp` — `handle()` switch에 새 case 추가
   - `engine_.isMuted()` 체크 필요 여부 결정 (panic mute 중 차단할 액션인지)
4. `Control/ControlMapping.cpp` — ⚠️ **`varToActionEvent()`의 상한 가드 업데이트 필수**
   - `if (actionVal >= 0 && actionVal <= static_cast<int>(Action::마지막_Action))` — 새 enum이 마지막이면 이 값을 변경
   - 이 상한을 업데이트하지 않으면 저장된 핫키/MIDI 바인딩이 **자동 폐기**되어 바인딩이 작동하지 않음
5. `Control/StateBroadcaster.h` — `AppState`에 상태 필드 추가 (필요 시)
   - 필드 추가 시 `quickStateHash()`에도 반드시 포함 (누락 시 해당 필드 변경이 브로드캐스트 안 됨)
6. `Control/StateBroadcaster.cpp` — `toJSON()`에 새 필드 직렬화 추가
7. `Control/HttpApiServer.cpp` — HTTP 엔드포인트 추가 (`processRequest` 내 분기)
8. `Control/WebSocketServer.cpp` — WebSocket 메시지 파싱 추가 (`processMessage` 내 분기)
9. `UI/StatusUpdater.cpp` — `AppState` 필드 매핑 확인 (새 필드가 AudioEngine에서 올바르게 복사되는지)
10. **문서 업데이트**:
    - `docs/CONTROL_API.md` — API 레퍼런스
    - `docs/API_EXAMPLES.md` — 사용 예제
    - `Control/README.md` — Data Flow의 액션 수 업데이트, DANGER ZONE 5의 차단 목록
    - 이 파일의 Quick Reference Thread Table (스레드 모델 변경 시)

### 새 Platform 추가하기

1. `host/Source/Platform/{PlatformName}/` 디렉토리 생성
2. 인터페이스 구현:
   - `{Platform}AutoStart.cpp` — AutoStart.h 인터페이스 구현
   - `{Platform}ProcessPriority.cpp` — ProcessPriority.h 구현
   - `{Platform}MultiInstanceLock.cpp` — MultiInstanceLock.h 구현
3. `host/Source/Platform/PlatformAudio.h` — `getDefaultSharedDeviceType()` 분기 추가
4. `CMakeLists.txt` — 플랫폼 조건부 소스 파일 추가
5. **문서 업데이트**:
   - `Platform/README.md` — File Mapping 테이블 + 파일 목록
   - `docs/PLATFORM_GUIDE.md` — 플랫폼 기능 비교표
   - `docs/BUILDING.md` — 빌드 지침
   - 이 파일의 Platform/ 모듈 설명

### 새 Platform 기능 추가하기 (인터페이스 + OS별 구현)

1. `host/Source/Platform/NewFeature.h` — 인터페이스 헤더 생성 (`namespace directpipe::Platform` 내 free function)
   - 기존 `ProcessPriority.h` 또는 `AutoStart.h`를 템플릿으로 복사
2. 각 OS 구현 생성:
   - `Platform/Windows/WindowsNewFeature.cpp` — `#if defined(_WIN32)` 가드
   - `Platform/macOS/MacNewFeature.cpp` — `#if defined(__APPLE__)` 가드
   - `Platform/Linux/LinuxNewFeature.cpp` — `#if defined(__linux__)` 가드
3. `CMakeLists.txt` — 새 `.cpp` 파일을 플랫폼별 조건 블록에 추가
   - ⚠️ macOS: 시스템 프레임워크 필요 시 `target_link_libraries`에 추가 (예: `IOKit.framework`)
4. **호출 지점 연결** — 새 기능을 실제로 호출하는 코드 추가
   - 어떤 앱 수명주기 이벤트에서 호출할지 결정 (예: `audioDeviceAboutToStart` → inhibit, `audioDeviceStopped` → release)
   - ⚠️ RT 오디오 콜백에서 호출 금지 — Message thread 또는 디바이스 콜백에서만
   - 리소스 쌍 관리 (acquire/release, inhibit/release) — 앱 비정상 종료 시 자동 해제 보장 확인
5. **문서 업데이트**:
   - `Platform/README.md` — 아키텍처 트리 + 매핑 테이블에 새 행 추가
   - `CLAUDE.md` — Key Implementations의 Platform/ 항목 + Modules 섹션
   - `docs/PLATFORM_GUIDE.md` — 플랫폼별 기능 비교표 (해당 시)

### 새 UI 탭 추가하기

1. `host/Source/UI/{TabName}Tab.h/.cpp` 생성 (juce::Component 상속)
2. `UI/ControlSettingsPanel.cpp` — 탭 추가 (`tabbedComponent_->addTab(...)`)
   - 또는 `MainComponent.cpp` — `rightTabs_`에 추가 (최상위 탭인 경우)
3. 콜백 연결: `onDirty`, `onNotification` 등
4. `UI/SettingsExporter.cpp` — 탭 설정 직렬화/역직렬화 추가 (영속 상태 있는 경우)
5. **문서 업데이트**:
   - `UI/README.md` — File List + Component Hierarchy
   - 이 파일의 Modules 섹션

### 새 외부 제어 서버/핸들러 추가하기 (OSC, gRPC 등)

백엔드(Control/ 서버) + 프론트엔드(UI/ 탭) 양쪽을 모두 구현해야 하는 경우.

**백엔드 (Control/):**
1. `Control/{Name}Handler.h/.cpp` 생성 — 서버 클래스 구현
   - 기존 `WebSocketServer` 또는 `HttpApiServer`를 템플릿으로 참조
   - 필수 인터페이스: `start(int port)`, `stop()`, `isRunning()`, `getPort()`
   - `ActionDispatcher&` 참조 보유 — 수신된 메시지를 `dispatcher_.dispatch(event)`로 전달
   - ⚠️ 서버 스레드에서 직접 UI 접근 금지 — ActionDispatcher가 Message thread 전달 보장
   - ⚠️ `alive_` 플래그 패턴 적용 (callAsync 사용 시)
2. `Control/ControlManager.h/.cpp` — 핸들러 등록
   - 멤버 추가: `std::unique_ptr<{Name}Handler> {name}Handler_`
   - `initialize()`에서 생성 + `start()`, `shutdown()`에서 `stop()`
   - 접근자 추가: `{Name}Handler& get{Name}Handler()`
3. `Control/ControlMapping.h/.cpp` — 설정 영속화
   - `ServerConfig` 구조체에 `{name}Port`, `{name}Enabled` 필드 추가
   - JSON 직렬화/역직렬화에 새 필드 추가
4. `Control/Log.h` — 로그 카테고리 태그 추가 (예: `[OSC]`)

**프론트엔드 (UI/):**
5. `UI/{Name}Tab.h/.cpp` 생성 — `StreamDeckTab`을 템플릿으로 복사
   - 서버 상태 표시 (포트, 연결 수, running 여부)
   - Start/Stop 토글 버튼
   - 내부 Timer (2Hz)로 상태 폴링
6. `UI/ControlSettingsPanel.cpp` — 서브탭 추가
   - `tabbedComponent_->addTab("{Name}", tabBg, {name}Tab_.get(), false)`

**빌드 + 문서:**
7. `CMakeLists.txt` — 4개 소스 파일 추가 (.h/.cpp × 2)
   - ⚠️ 외부 라이브러리 필요 시 `target_link_libraries` 추가 (예: `juce::juce_osc`)
8. **문서 업데이트**:
   - `Control/README.md` — Data Flow, File List, Thread Model, DANGER ZONES (서버 스레드 join 규칙)
   - `UI/README.md` — File List, Component Hierarchy, Thread Model
   - 이 파일의 Quick Reference Thread Table + Modules 섹션
   - `CLAUDE.md` DirectPipeLogger 카테고리 목록에 새 태그 추가
   - `docs/CONTROL_API.md` — 프로토콜/엔드포인트 레퍼런스 (해당 시)

### 새 Lock/Mutex 추가하기

1. 헤더 파일에 선언 + Thread Ownership 어노테이션 추가
2. **이 파일의 Lock Hierarchy에 추가** (순서 결정 필수)
3. 해당 모듈 README의 Thread Model 테이블 업데이트
4. DANGER ZONES에 교착 위험 경로 추가 (해당 시)
