# DirectPipe Architecture / 아키텍처

> **Version 4.0.0 / 버전 4.0.0**

## System Overview / 시스템 개요

DirectPipe is a real-time VST2/VST3 host that processes microphone input through a plugin chain. It provides three independent output paths: main output to the AudioSettings device (WASAPI/ASIO on Windows, CoreAudio on macOS, ALSA/JACK on Linux), optional monitor output to headphones (separate audio device), and IPC output to shared memory for the Receiver VST2 plugin (e.g., OBS). Processed audio can also be recorded to WAV. All external control sources (hotkeys, MIDI, WebSocket, HTTP, Stream Deck) funnel through a unified ActionDispatcher. **Platform support**: Windows (stable), macOS (beta), Linux (experimental).

DirectPipe는 마이크 입력을 VST 플러그인 체인으로 실시간 처리하는 VST 호스트다. 3가지 독립 출력 경로 제공: 메인 출력(AudioSettings 장치, Windows: WASAPI/ASIO, macOS: CoreAudio, Linux: ALSA/JACK), 모니터 출력(헤드폰, 별도 오디오 장치), IPC 출력(공유 메모리 → Receiver VST2, 예: OBS). 처리된 오디오의 WAV 녹음도 지원. 모든 외부 제어(단축키, MIDI, WebSocket, HTTP, Stream Deck)는 ActionDispatcher를 통해 통합된다. **플랫폼 지원**: Windows (안정), macOS (베타), Linux (실험적).

```
Mic ─→ Audio Driver ─→ Input Gain ─→ VST2/VST3 Plugin Chain ─┐
        (WASAPI/ASIO [Win],                                    │
         CoreAudio [macOS],                                    │
         ALSA/JACK [Linux])                                    │
                 ┌────────────────────────────────────────────┼────────────────────┐
                 │                                            │                    │
           Main Output                                  Monitor Output         IPC Output
        (outputChannelData)                           (OutputRouter →         (SharedMemWriter →
     Audio tab Output device                         MonitorOutput,       Shared Memory →
     e.g. VB-Cable → Discord                        separate audio       OBS [Receiver VST2])
                 │                                    device → Headphones)
           AudioRecorder → WAV File

External Control:
  Hotkey / MIDI / Stream Deck / WebSocket (:8765) / HTTP (:8766)
    → ControlManager → ActionDispatcher
      → VSTChain (bypass/params), OutputRouter (volume), PresetManager (slots),
        AudioRecorder (recording), SharedMemWriter (IPC toggle)
```

**Design choices / 설계 원칙:**
- **WASAPI Shared** (Windows) — Non-exclusive mic access. Other apps can use the mic simultaneously. / (Windows) 비독점 마이크 접근. 다른 앱과 동시 사용 가능.
- **ASIO** (Windows) — Lower latency for ASIO-compatible interfaces. / (Windows) ASIO 호환 인터페이스로 저지연.
- **CoreAudio** (macOS) — Native low-latency audio driver. / (macOS) 네이티브 저지연 오디오 드라이버.
- **ALSA/JACK** (Linux) — ALSA for direct hardware access, JACK for pro audio routing. / (Linux) ALSA: 직접 하드웨어 접근, JACK: 프로 오디오 라우팅.
- Runtime switching between driver types. UI adapts dynamically. / 드라이버 타입 런타임 전환. UI 자동 적응.
- **3 output paths (independently controlled)** — Main output directly to AudioSettings device (e.g., VB-Audio for Discord). Monitor uses a separate audio device for headphones. IPC output sends to shared memory for Receiver VST2 plugin (e.g., OBS — no virtual cable needed). Each path can be independently toggled and volume-adjusted via OUT/MON/VST buttons or external controls (hotkey, MIDI, Stream Deck, HTTP). This enables scenarios like muting the OBS stream mic (VST OFF) while keeping Discord active (OUT ON), or vice versa. Panic Mute (Ctrl+Shift+M) kills all outputs instantly; previous states auto-restore on unmute. / 3가지 독립 출력: 메인(AudioSettings 장치), 모니터(별도 오디오 장치, 헤드폰), IPC(공유 메모리 → Receiver VST2, 가상 케이블 불필요). 각 경로는 OUT/MON/VST 버튼 또는 외부 제어로 개별 ON/OFF 및 볼륨 조절 가능. OBS 마이크만 끄고 Discord 유지, 또는 반대도 가능. 패닉 뮤트로 전체 즉시 차단 + 해제 시 이전 상태 복원.
- **15 actions** — Unified action system: PluginBypass, MasterBypass, SetVolume, ToggleMute, LoadPreset, PanicMute, InputGainAdjust, NextPreset, PreviousPreset, InputMuteToggle, SwitchPresetSlot, MonitorToggle, RecordingToggle, SetPluginParameter, IpcToggle. / 15개 통합 액션 시스템.

## Components / 컴포넌트

### 1. Host Application (`host/`) / 호스트 앱

JUCE 7.0.12-based desktop application. Main audio processing engine.

JUCE 7.0.12 기반 데스크톱 앱. 메인 오디오 처리 엔진.

#### Audio Module (`host/Source/Audio/`) / 오디오 모듈

- **AudioEngine** — **Windows**: 5 driver types — DirectSound (legacy), Windows Audio (WASAPI Shared, recommended), Windows Audio (Low Latency) (IAudioClient3), Windows Audio (Exclusive Mode), ASIO. **macOS**: CoreAudio. **Linux**: ALSA, JACK. Manages the audio device callback. Pre-allocated work buffers (8ch). Mono mixing or stereo passthrough. Runtime device type switching, sample rate/buffer size queries. Input gain (atomic), master mute. Audio optimizations: `ScopedNoDenormals` (prevents CPU spikes from denormals in VST plugins), muted fast-path (skips VST chain when muted), RMS decimation (every 4th callback). Rolling 60-second XRun monitoring with atomic reset flag (`xrunResetRequested_`) for thread-safe device→message thread communication. `setBufferSize` auto-fallback to closest device-supported size with notification. **Device auto-reconnection**: Dual mechanism — `ChangeListener` on `deviceManager_` for immediate detection + 3s timer polling fallback. Tracks `desiredInputDevice_`/`desiredOutputDevice_`. Preserves SR/BS/channel routing on reconnect. Per-direction loss: `inputDeviceLost_` zeroes input in audio callback, `outputAutoMuted_` auto-mutes/unmutes output. `reconnectMissCount_` accepts current devices after 5 failed attempts only for cross-driver stale name scenarios; when `outputAutoMuted_` is true (genuine device loss / physical unplug), the counter resets and keeps waiting indefinitely for the desired device. `setInputDevice`/`setOutputDevice` clear `deviceLost_`, `inputDeviceLost_`, `outputAutoMuted_`, and reconnection counters — allows users to manually select a different device during device loss without waiting for reconnection. **Driver type snapshot**: `DriverTypeSnapshot` saves per-driver settings (input/output device, SR, BS, `outputNone`) before type switch, restores when switching back. `outputNone_` cleared on driver type switch (prevents OUT mute lock after WASAPI "None" -> ASIO), restored from snapshot if the target driver had it saved. `ipcAllowed_` blocks IPC in audio-only multi-instance mode. Audio optimizations (`timeBeginPeriod`, Power Throttling disable) are Windows-specific; macOS/Linux rely on JUCE defaults. **Output "None" mode**: `setOutputNone(bool)` / `isOutputNone()` — `outputNone_` atomic flag mutes output and locks OUT button (intentional "no output device" state, similar to panic mute lockout but for deliberate use). Cleared on driver type switch to prevent OUT button lock persisting across drivers. `DriverTypeSnapshot` saves/restores `outputNone` per driver type. / Windows 5종 드라이버, macOS CoreAudio, Linux ALSA/JACK. 오디오 콜백 관리. 사전 할당 버퍼. Mono/Stereo 처리. 입력 게인, 마스터 뮤트, RMS 레벨 측정. **장치 자동 재연결**: 듀얼 감지 + 방향별 감지 (입력/출력 분리). `reconnectMissCount_`는 교차 드라이버 이름 불일치에만 폴백 적용; `outputAutoMuted_` true(물리적 분리)시 원하는 장치를 무기한 대기. `setInputDevice`/`setOutputDevice`는 장치 손실 중 수동 선택을 허용하기 위해 `deviceLost_` 및 재연결 카운터를 초기화. **드라이버 타입 스냅샷**: 타입 전환 시 설정 저장/복원 (`outputNone` 포함). `outputNone_`는 드라이버 전환 시 초기화, 스냅샷에서 복원. `ipcAllowed_`로 audio-only 모드에서 IPC 차단. **Output "None" 모드**: `setOutputNone(bool)` / `isOutputNone()` — `outputNone_` atomic 플래그로 출력 뮤트 + OUT 버튼 잠금 (의도적 "출력 장치 없음" 상태). 드라이버 전환 시 초기화, `DriverTypeSnapshot`으로 드라이버별 저장/복원.
- **VSTChain** — `AudioProcessorGraph`-based VST2/VST3 plugin chain. `suspendProcessing()` during graph rebuild. Async chain replacement (`replaceChainAsync`) loads plugins on background thread with `alive_` flag (`shared_ptr<atomic<bool>>`) to guard `callAsync` completion callbacks against object destruction. **Keep-Old-Until-Ready**: old chain continues processing audio during background plugin loading; new chain swapped atomically on message thread when ready (~10-50ms suspend vs previous 1-3s mute gap). `asyncGeneration_` counter discards stale callAsync callbacks from superseded loads. Batch graph rebuild via `UpdateKind::async` for intermediate addNode/removeNode calls (N² → O(1) rebuild count). Editor windows tracked per-plugin. Pre-allocated MidiBuffer. `chainLock_` (mutable `CriticalSection`) protects ALL reader methods (`getPluginSlot`, `getPluginCount`, `setPluginBypassed`, parameter access, editor open/close) — not just writers. `prepared_` is `std::atomic<bool>` for RT-safe access. `processBlock` uses capacity guard instead of misleading buffer size check. `movePlugin` resizes `editorWindows_` before move to prevent out-of-bounds access. / VST2/VST3 플러그인 체인. **Keep-Old-Until-Ready**: 백그라운드 플러그인 로딩 중 이전 체인이 오디오 처리를 유지, 메시지 스레드에서 원자적 스왑 (~10-50ms 일시정지 vs 이전 1-3초 무음). `asyncGeneration_` 카운터로 대체된 로드의 stale callAsync 콜백 폐기. `UpdateKind::async`로 배치 그래프 리빌드. `alive_` 플래그(`shared_ptr<atomic<bool>>`)로 callAsync 콜백의 수명 안전 보장. MidiBuffer 사전 할당. `chainLock_` (mutable `CriticalSection`)이 모든 리더 메서드도 보호. `prepared_`는 `std::atomic<bool>`. `processBlock`은 용량 가드 사용. `movePlugin`은 이동 전 `editorWindows_` 크기 조정.
- **OutputRouter** — Routes processed audio to the monitor output (separate audio device). Independent atomic volume and enable controls. Pre-allocated scaled buffer. `routeAudio()` clamps `numSamples` to `scaledBuffer_` capacity (prevents buffer overrun). Main output goes directly through outputChannelData. / 모니터 출력(별도 오디오 장치)으로 오디오 라우팅. `routeAudio()`가 `numSamples`를 `scaledBuffer_` 용량에 클램프 (버퍼 오버런 방지). 메인 출력은 outputChannelData로 직접 전송.
- **MonitorOutput** — Second AudioDeviceManager used for the monitor output (WASAPI on Windows, CoreAudio on macOS, ALSA on Linux). Lock-free `AudioRingBuffer` bridge between two audio callback threads. Configured in Output tab. Status tracking (Active/Error/NotConfigured/SampleRateMismatch). Independent auto-reconnection via `monitorLost_` atomic + 3s timer polling. / 모니터 출력용 별도 AudioDeviceManager (Windows: WASAPI, macOS: CoreAudio, Linux: ALSA). 락프리 링버퍼 브리지. Output 탭에서 구성. 상태 추적. `monitorLost_` + 3초 타이머로 독립 자동 재연결.
- **PluginPreloadCache** — Background pre-loads other slots' plugin instances after slot switch. Cache hit = instant swap (~10-50ms vs 200-500ms DLL loading). Invalidated on SR/BS change, slot save/delete/copy. Max 5 slots × ~4 plugins cached. / 슬롯 전환 후 다른 슬롯의 플러그인 인스턴스를 백그라운드 프리로드. 캐시 hit = 즉시 스왑. SR/BS 변경, 슬롯 저장/삭제/복사 시 무효화.
- **AudioRingBuffer** — Header-only SPSC lock-free ring buffer for inter-device audio transfer. `reset()` zeroes all channel data. / 디바이스 간 오디오 전송용 헤더 전용 SPSC 락프리 링 버퍼. `reset()`은 모든 채널 데이터를 0으로 초기화.
- **LatencyMonitor** — High-resolution timer-based latency measurement. / 고해상도 타이머 기반 레이턴시 측정.
- **AudioRecorder** — Lock-free audio recording to WAV via `AudioFormatWriter::ThreadedWriter`. SpinLock-protected writer teardown for RT-safety. Timer-based duration tracking. Auto-stop on device change. `outputStream` properly deleted on writer creation failure (leak fix). / 락프리 WAV 녹음. SpinLock으로 RT 안전한 writer 해제. 장치 변경 시 자동 중지. writer 생성 실패 시 `outputStream` 올바르게 삭제 (누수 수정).

#### Control Module (`host/Source/Control/`) / 제어 모듈

All external inputs funnel through a unified ActionDispatcher. / 모든 외부 입력은 통합된 ActionDispatcher를 거친다.

- **ActionDispatcher** — Central action routing. 15 actions: `PluginBypass`, `MasterBypass`, `SetVolume`, `ToggleMute`, `LoadPreset`, `PanicMute`, `InputGainAdjust`, `NextPreset`, `PreviousPreset`, `InputMuteToggle`, `SwitchPresetSlot`, `MonitorToggle`, `RecordingToggle`, `SetPluginParameter`, `IpcToggle`. Thread-safe dispatch via `callAsync` with `alive_` flag (`shared_ptr<atomic<bool>>`) lifetime guard. Copy-before-iterate for reentrant safety. `actionToString()` helper for enum-to-string conversion. Dispatched actions logged as `[ACTION]` (high-frequency excluded). / 중앙 액션 라우팅. 15개 액션. `alive_` 플래그로 수명 보호된 callAsync 디스패치. 재진입 안전을 위한 copy-before-iterate. `actionToString()` 헬퍼로 enum→문자열 변환. 디스패치된 액션 `[ACTION]` 로그 (고빈도 제외).
- **ControlManager** — Aggregates all control sources (Hotkey, MIDI, WebSocket, HTTP). Initialize/shutdown lifecycle. / 모든 제어 소스 통합 관리.
- **HotkeyHandler** — Global keyboard shortcuts. Windows: `RegisterHotKey` API. macOS: `CGEventTap`. Linux: stub (not yet supported). Recording mode for key capture. / 글로벌 키보드 단축키. Windows: `RegisterHotKey` API. macOS: `CGEventTap`. Linux: 스텁 (미지원). 키 녹화 모드.
- **MidiHandler** — JUCE `MidiInput` for MIDI CC/note mapping with Learn mode. LED feedback via MidiOutput. Hot-plug detection. `bindingsMutex_` protects all access to `bindings_`; `getBindings()` returns a copy for safe iteration. `processCC`/`processNote` collect matching actions into a local vector, then dispatch OUTSIDE `bindingsMutex_` (deadlock prevention). / MIDI CC 매핑 + Learn 모드. LED 피드백. 핫플러그 감지. `bindingsMutex_`로 `bindings_` 접근 보호; `getBindings()`는 안전한 반복을 위해 복사본 반환. `processCC`/`processNote`는 매칭 액션을 로컬 벡터에 수집 후 `bindingsMutex_` 밖에서 디스패치 (교착 방지).
- **WebSocketServer** — RFC 6455 WebSocket server (port 8765). Custom SHA-1 implementation for handshake. Case-insensitive HTTP header matching (RFC 7230 compliance). JUCE `StreamingSocket` with frame encoding/decoding, ping/pong. Dead client cleanup — `sendFrame` returns `bool`, `broadcastToClients` closes socket immediately on write failure (prevents repeated write-error log spam from stale clients). UDP discovery broadcast on port 8767 at startup for instant Stream Deck connection. `broadcastToClients` joins broadcast thread outside `clientsMutex_` lock to prevent deadlock. / RFC 6455 WebSocket 서버. 커스텀 SHA-1 핸드셰이크. 대소문자 무시 HTTP 헤더 매칭 (RFC 7230 준수). `sendFrame`이 `bool` 반환, `broadcastToClients`가 쓰기 실패 시 소켓 즉시 닫음 (stale 클라이언트 반복 오류 로그 방지). 시작 시 UDP 8767 디스커버리 브로드캐스트로 Stream Deck 즉시 연결. `broadcastToClients`는 교착 방지를 위해 `clientsMutex_` 잠금 밖에서 스레드 join.
- **HttpApiServer** — HTTP REST API (port 8766) for one-shot GET commands. CORS enabled. 3-second read timeout. Volume range validation (0.0-1.0). Proper HTTP status codes (404 Not Found, 405 Method Not Allowed, 400 Bad Request) — `processRequest` returns `pair<int, string>`. Recording toggle, plugin parameter, and IPC toggle endpoints. / HTTP REST API. CORS 활성화. 3초 읽기 타임아웃. 볼륨 범위 검증. 적절한 HTTP 상태 코드 (404/405/400) — `processRequest`가 `pair<int, string>` 반환. 녹음 토글, 플러그인 파라미터, IPC 토글 엔드포인트.
- **StateBroadcaster** — Pushes AppState changes to all connected StateListeners as JSON. Includes `ipc_enabled`, `device_lost`, `monitor_lost`, `slot_names`, `output_muted` fields in state object. `quickStateHash` dirty-flag skips JSON serialization + broadcast when state unchanged. Thread-safe: state updates from any thread, listeners always notified on message thread via `callAsync` with `alive_` flag lifetime guard. Copy-before-iterate for reentrant safety. WebSocket broadcast on dedicated thread (non-blocking). Notification queue: `uint32_t` indices (overflow-safe), capacity guard before write. / 상태 변경을 JSON으로 모든 리스너에 푸시. 상태 객체에 `ipc_enabled` 필드 포함. 스레드 안전: 어느 스레드에서든 상태 업데이트 가능, `alive_` 플래그로 수명 보호된 callAsync 리스너 알림. 재진입 안전을 위한 copy-before-iterate. WebSocket 브로드캐스트 전용 스레드 (비차단). 알림 큐: `uint32_t` 인덱스 (오버플로 안전), 쓰기 전 용량 가드.
- **DirectPipeLogger** — Centralized logging system. Captures logs from all subsystems (audio engine, plugins, WebSocket, HTTP, etc.). Feeds LogPanel and NotificationBar. Ring buffer indices use `uint32_t` (overflow-safe). All log messages use category tags: `[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`. High-frequency actions (SetVolume, InputGainAdjust, SetPluginParameter) excluded from logging. / 중앙 집중 로깅 시스템. 모든 서브시스템 로그 캡처. LogPanel과 NotificationBar로 전달. 링 버퍼 인덱스 `uint32_t` 사용 (오버플로 안전). 모든 로그에 카테고리 태그 사용. 고빈도 액션(SetVolume, InputGainAdjust, SetPluginParameter) 로그 제외.
- **ControlMapping** — JSON-based persistence for hotkey/MIDI/server config. Portable mode support (`portable.flag` next to exe). / JSON 기반 설정 저장. 포터블 모드 지원.
- **ControlManager** — Owns and manages all external control subsystems (HotkeyHandler, MidiHandler, WebSocketServer, HttpApiServer). `initialize(bool enableExternalControls)` conditionally skips external control init for multi-instance audio-only mode. `externalControlsActive_` flag preserved across `reloadConfig()`/`applyConfig()`. `getConfigStore()` provides live config access for SettingsExporter. / 모든 외부 제어 서브시스템 소유 및 관리. 다중 인스턴스 audio-only 모드를 위한 조건부 초기화. `getConfigStore()`로 SettingsExporter에 라이브 설정 접근 제공.

#### Platform Module (`host/Source/Platform/`) / 플랫폼 모듈

Cross-platform abstraction layer. Each sub-directory provides platform-specific implementations of a common interface. CMake conditionally compiles the correct source files per platform. / 크로스 플랫폼 추상화 계층. 각 하위 디렉토리가 공통 인터페이스의 플랫폼별 구현 제공. CMake가 플랫폼별로 올바른 소스 파일을 조건부 컴파일.

- **PlatformAudio** (`PlatformAudio.h`, header-only) — Audio device type helpers: `getDefaultSharedDeviceType()` (Windows: "Windows Audio", macOS: "CoreAudio", Linux: "ALSA"), `getSharedModeOutputDevices()`, `isExclusiveDriverType()`. Replaces hardcoded WASAPI strings. / 오디오 디바이스 타입 헬퍼 (헤더 온리): 하드코딩된 WASAPI 문자열 대체.
- **AutoStart** (`AutoStart.h`) — Auto-start interface: `isAutoStartEnabled()`, `setAutoStartEnabled(bool)`. Windows: Registry (`HKCU\...\Run`). macOS: LaunchAgent plist. Linux: XDG `.desktop` file. / 자동 시작 인터페이스. Windows: 레지스트리, macOS: LaunchAgent, Linux: XDG autostart.
- **ProcessPriority** (`ProcessPriority.h`) — Process priority: `setHighPriority()`, `restoreNormalPriority()`. Windows: `SetPriorityClass` + `timeBeginPeriod` + Power Throttling. macOS: `setpriority`. Linux: `nice`. / 프로세스 우선순위 설정.
- **MultiInstanceLock** (`MultiInstanceLock.h`) — Multi-instance coordination: `acquireExternalControlPriority()`, `releaseExternalControlPriority()`. Windows: Named Mutex. macOS/Linux: POSIX file locks. / 다중 인스턴스 외부 제어 우선순위 조정.

#### IPC Module (`host/Source/IPC/`) / IPC 모듈

- **SharedMemWriter** — Writes processed audio to shared memory using the core library. / core 라이브러리를 사용해 공유 메모리에 오디오 기록.

#### UI Module (`host/Source/UI/`) / UI 모듈

- **AudioSettings** — Driver type selector (platform-dependent: WASAPI/ASIO on Windows, CoreAudio on macOS, ALSA/JACK on Linux), device selection, ASIO channel routing (input/output pair, Windows only), sample rate, buffer size, channel mode (Mono/Stereo), latency display, ASIO Control Panel button (Windows only). Shows "DeviceName (Disconnected)" in input/output combos when the corresponding device is lost (input uses `isInputDeviceLost()`, output uses `isOutputAutoMuted()`). Selecting a different device during loss clears the loss state. / 오디오 설정 패널. 드라이버 타입은 플랫폼별 (Windows: WASAPI/ASIO, macOS: CoreAudio, Linux: ALSA/JACK). 장치 손실 시 입력/출력 콤보에 "DeviceName (Disconnected)" 표시. 손실 중 다른 장치를 선택하면 손실 상태가 해제됨.
- **PluginChainEditor** — Drag-and-drop reordering, bypass toggle, edit button (native GUI), remove button. Safe deletion via `callAsync`. `addPluginFromDescription` uses `SafePointer` in `callAsync` lambda. / 드래그 앤 드롭 플러그인 체인 편집. callAsync를 통한 안전 삭제. `addPluginFromDescription`은 callAsync 람다에서 `SafePointer` 사용.
- **PluginScanner** — Out-of-process VST scanner with auto-retry (10x) and dead man's pedal. Blacklist for crashed plugins. Real-time text search and column sorting (name/vendor/format). All 3 `callAsync` lambdas from background scan thread use `SafePointer`. / 별도 프로세스 VST 스캐너. 자동 재시도 10회. 블랙리스트. 실시간 검색 및 정렬. 백그라운드 스캔 스레드의 모든 callAsync 람다가 `SafePointer` 사용.
- **OutputPanel** — Three sections: (1) Monitor output: device selector, volume slider, enable toggle, device status indicator (Active/Error/No device). (2) VST Receiver (IPC): enable toggle for shared memory output. (3) Recording: REC/STOP button, elapsed time, Play last recording, Open Folder, folder chooser. Default folder: `Documents\DirectPipe Recordings`. Recording folder persisted to `recording-config.json`. / 3개 섹션: (1) 모니터 출력(장치/볼륨/상태), (2) VST Receiver IPC 토글, (3) 녹음(REC/STOP/재생/폴더). 기본 폴더: `Documents\DirectPipe Recordings`.
- **PresetManager** — Full preset save/load (JSON, `.dppreset`) + Quick Preset Slots A-E with custom naming (e.g., `A|게임`), slot copy/delete, individual slot export/import (`.dppreset`). Plugin state via `getStateInformation()`/base64. Async slot loading. / 프리셋 관리 + 퀵 슬롯 A-E (이름 지정, 복사/삭제, 개별 내보내기/가져오기). 비동기 슬롯 로딩.
- **ControlSettingsPanel** — 3 sub-tabs: Hotkeys, MIDI, Stream Deck (server status). MIDI tab includes plugin parameter mapping (3-step popup: plugin → parameter → Learn). / 3개 서브탭: 단축키, MIDI, Stream Deck. MIDI 탭에 플러그인 파라미터 매핑 (3단계 팝업).
- **SettingsExporter** — Two export tiers: (1) **Save/Load Settings** (`.dpbackup`) — audio, output, control settings only (no VST chain or slots). (2) **Full Backup/Restore** (`.dpfullbackup`) — everything including VST chain + preset slots A-E. Located in Settings tab (LogPanel). Uses `controlManager_->getConfigStore()` for live config access. **Cross-OS protection**: Backup files include a `platform` field (windows/macos/linux). Import checks platform compatibility and blocks cross-OS restore with an alert dialog. Legacy backups without platform field are accepted. / 2단계 내보내기: (1) 설정 저장/불러오기(`.dpbackup`) — 오디오, 출력, 컨트롤 설정만. (2) 전체 백업/복원(`.dpfullbackup`) — VST 체인 + 프리셋 슬롯 포함 전체. **크로스 OS 보호**: 백업 파일에 `platform` 필드 포함. 다른 OS에서 복원 시 차단. 플랫폼 필드 없는 레거시 백업은 허용.
- **LevelMeter** — Real-time RMS level display with peak hold, clipping indicator. dB log scale. / 실시간 RMS 레벨 미터. 피크 홀드. dB 로그 스케일.
- **LogPanel** — 4th tab "Settings" in right panel. Four sections: (1) Application: Auto-start toggle (platform-adaptive label: "Start with Windows" / "Start at Login" (macOS) / "Start on Login" (Linux); Windows: HKCU registry, macOS: LaunchAgent, Linux: XDG autostart) + red "Quit" button (closes individual instance, helps distinguish multiple portables). (2) Settings: Save/Load Settings buttons (`.dpbackup` via SettingsExporter, settings only). (3) Log: real-time log viewer with timestamped monospaced entries, Export Log / Clear Log buttons. Batch flush optimization: drains all pending lines, single TextEditor insert, single trim pass. (4) Maintenance: Full Backup/Restore (`.dpfullbackup`), Clear Plugin Cache, Clear All Presets (deletes slots + backups + temps, clears active chain), Factory Reset (deletes ALL data). All with confirmation dialogs. / 우측 패널 4번째 탭 "Settings". 4개 섹션: (1) 시작 프로그램 등록 + 빨간색 "Quit" 버튼 (개별 인스턴스 종료), (2) 설정 저장/불러오기 (설정만), (3) 실시간 로그 뷰어. (4) 유지보수: 전체 백업/복원, 플러그인 캐시 삭제, 전체 프리셋 삭제, 공장 초기화.
- **NotificationBar** — Non-intrusive status bar notifications. Temporarily replaces latency/CPU labels. Color-coded: red (errors), orange (warnings), purple (info). Auto-fades after 3-8 seconds depending on severity. / 비침습적 상태 바 알림. 레이턴시/CPU 레이블 임시 대체. 색상: 빨강(오류), 주황(경고), 보라(정보). 3-8초 자동 페이드.
- **DirectPipeLookAndFeel** — Custom dark theme (#1E1E2E bg, #6C63FF purple accent, #4CAF50 green). / 다크 테마.

#### Main Application (`host/Source/MainComponent.cpp`) / 메인 앱

- Two-column layout: left (input meter + gain + VST chain + slot buttons), right (tabbed panel: Audio/Output/Controls/Settings + output meter) / 2컬럼 레이아웃, 좌우 대칭 미터
- Quick Preset Slot buttons A-E with visual active/occupied state. Loading feedback (dimmed buttons). / 퀵 프리셋 슬롯 버튼 (활성/사용중 시각 구분, 로딩 중 피드백)
- Auto-save via dirty-flag + 1-second debounce. `onSettingsChanged` callbacks trigger `markSettingsDirty()`. / dirty-flag + 1초 디바운스 자동 저장
- Panic mute remembers pre-mute monitor enable state, restores on unmute. During panic mute, OUT/MON/VST buttons and external controls (hotkey/MIDI/Stream Deck/HTTP) are locked -- only PanicMute/unmute can change state. / Panic Mute 모니터 상태 기억/복원. 패닉 뮤트 중 OUT/MON/VST 버튼과 외부 제어(단축키/MIDI/Stream Deck/HTTP) 잠금 -- PanicMute/해제만 상태 변경 가능.
- Status bar: latency, CPU, format, portable mode, "Created by LiveTrack". Shows "NEW vX.Y.Z" in orange when newer GitHub release exists (background update check on startup, uses `SafePointer` to guard `callAsync` UI update). NotificationBar temporarily replaces status labels with color-coded error/warning/info messages (auto-fade 3-8s). / 상태 바. 새 릴리즈 시 주황색 "NEW" 표시 (백그라운드 업데이트 체크, `SafePointer`로 callAsync UI 업데이트 수명 보호). NotificationBar가 상태 레이블을 색상 코드 오류/경고/정보 메시지로 임시 대체 (3-8초 자동 페이드).
- System tray tooltip: shows current state (preset, plugins, volumes). Atomic dirty-flag for cross-thread safety. / 시스템 트레이 툴팁: 현재 상태 표시. atomic dirty-flag로 스레드 안전.

#### System Tray (`host/Source/Main.cpp`)

- Process priority set to HIGH_PRIORITY_CLASS at startup (Windows). `timeBeginPeriod(1)` for 1ms timer resolution (Windows). Power Throttling disabled for Intel hybrid CPUs (Windows). macOS/Linux: standard JUCE process defaults. / 시작 시 프로세스 우선순위 HIGH (Windows). 1ms 타이머 해상도 (Windows). Intel 하이브리드 CPU Power Throttling 비활성화 (Windows). macOS/Linux: JUCE 기본값.
- Close button -> minimize to tray / X 버튼 -> 트레이 최소화
- Double-click or left-click tray icon -> show window / 트레이 아이콘 클릭 -> 창 복원
- Right-click -> popup menu: "Show Window", auto-start toggle (Windows: "Start with Windows", macOS: "Start at Login", Linux: "Start on Login"), "Quit DirectPipe" / 우클릭 -> 메뉴 (자동 시작 라벨은 플랫폼별 표시)
- Auto-start: Windows via `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`, macOS via LaunchAgent (`~/Library/LaunchAgents/`), Linux via XDG autostart (`~/.config/autostart/`) / 자동 시작: Windows 레지스트리, macOS LaunchAgent, Linux XDG autostart
- Out-of-process scanner mode via `--scan` argument / --scan 인자로 스캐너 모드
- **Multi-instance external control priority** — Named Mutexes coordinate which instance owns hotkeys/MIDI/WebSocket/HTTP. Windows: `CreateMutex` (`DirectPipe_NormalRunning`, `DirectPipe_ExternalControl`), TOCTOU-safe (`ERROR_ALREADY_EXISTS`, no `OpenMutex` probe for normal mode). macOS/Linux: POSIX file locks (`/tmp/`). `acquireExternalControlPriority()` returns: 1 (full control), 0 (audio only), -1 (blocked/quit). Normal mode blocks if portable has control. Portable runs audio-only if normal is running or another portable already has control. Audio-only mode skips HotkeyHandler, MidiHandler, WebSocketServer, HttpApiServer, UDP discovery. UI: title bar "(Audio Only)", status bar orange "Portable", tray tooltip "(Portable/Audio Only)". / **다중 인스턴스 외부 제어 우선순위** — Windows: Named Mutex, macOS/Linux: POSIX 파일 잠금으로 단축키/MIDI/WS/HTTP 소유권 조정. `acquireExternalControlPriority()` 반환값: 1(전체 제어), 0(오디오만), -1(차단/종료). 일반 모드는 포터블이 제어 중이면 차단. 포터블은 일반이 실행 중이거나 다른 포터블이 제어 중이면 오디오만. Audio-only 모드는 외부 제어 스킵. UI: 타이틀 바 "(Audio Only)", 상태 바 주황색 "Portable", 트레이 "(Portable/Audio Only)".
- **Portable mode** — `portable.flag` file next to exe → config stored in `./config/` instead of `%AppData%/DirectPipe/`. Enables multi-instance from different folders. / **포터블 모드** — exe 옆 `portable.flag` → `%AppData%` 대신 `./config/`에 설정 저장. 다른 폴더에서 다중 인스턴스 가능.

### 2. Core Library (`core/`) / 코어 라이브러리

Shared static library for IPC. No JUCE dependency. / IPC용 정적 라이브러리. JUCE 의존성 없음.

- **RingBuffer** — SPSC lock-free ring buffer. `std::atomic` with acquire/release. Cache-line aligned (`alignas(64)`). Power-of-2 capacity. / SPSC 락프리 링 버퍼.
- **SharedMemory** — Shared memory wrapper. Windows: `CreateFileMapping`/`MapViewOfFile` with named events. macOS/Linux: POSIX `shm_open`/`mmap` with named semaphores. / 공유 메모리 래퍼. Windows: `CreateFileMapping`/`MapViewOfFile`. macOS/Linux: POSIX `shm_open`/`mmap`.
- **Protocol** — Shared header structure for IPC communication. / IPC 헤더 구조체.
- **Constants** — Buffer names, sizes, sample rates. / 상수.

### 3. DirectPipe Receiver Plugin (VST2/VST3/AU) (`plugins/receiver/`) / DirectPipe Receiver 플러그인 (VST2/VST3/AU)

Plugin for OBS and other hosts. Reads processed audio from DirectPipe via shared memory IPC (core library). Output-only plugin (no input bus) — host audio upstream of the plugin is completely replaced by IPC data. Available as VST2, VST3, and AU (macOS). OBS only supports VST2 on all platforms. / OBS 등에서 사용하는 플러그인. 공유 메모리 IPC(core 라이브러리)를 통해 DirectPipe의 처리된 오디오를 읽음. 입력 버스가 없는 출력 전용 플러그인 — 호스트에서 플러그인 앞단의 오디오는 IPC 데이터로 완전히 대체됨. VST2, VST3, AU(macOS) 포맷 제공. OBS는 모든 플랫폼에서 VST2만 지원.

- Consumes shared memory IPC written by `SharedMemWriter` / `SharedMemWriter`가 기록한 공유 메모리 IPC를 소비
- Configurable buffer size (5 presets): Ultra Low (~5ms), Low (~10ms), Medium (~21ms), High (~42ms), Safe (~85ms) / 버퍼 크기 설정 가능 (5단계 프리셋)
- IPC output can be toggled on/off via `IpcToggle` action / IPC 출력은 `IpcToggle` 액션으로 켜기/끄기 가능

### 4. Stream Deck Plugin (`com.directpipe.directpipe.sdPlugin/`) / 스트림 덱 플러그인

Elgato Stream Deck plugin (Node.js, `@elgato/streamdeck` SDKVersion 3). / Stream Deck 플러그인 (SDKVersion 3).

- Connects via WebSocket (`ws://localhost:8765`) / WebSocket으로 연결
- 7 SingletonAction subclasses: Bypass Toggle, Panic Mute, Volume Control, Preset Switch, Monitor Toggle, Recording Toggle, IPC Toggle / 7개 액션
- Volume Control supports 3 modes: Mute Toggle, Volume Up (+), Volume Down (-) with configurable step size / 볼륨 제어: 뮤트 토글, 볼륨 +/- 모드
- SD+ dial support for volume adjustment / SD+ 다이얼 지원
- Event-driven reconnection: UDP discovery (port 8767) + user-action trigger (no polling) / 이벤트 기반 재연결: UDP 디스커버리 + 사용자 조작 트리거 (폴링 없음)
- Pending message queue (cap 50) while disconnected / 연결 해제 중 대기 큐 (최대 50)
- Property Inspector HTML (sdpi-components v4) for each action / 각 액션별 설정 UI
- SVG icon sources in `icons-src/`, PNG generation via `scripts/generate-icons.mjs` / SVG 원본 + PNG 생성 스크립트
- Packaged as `.streamDeckPlugin` in `dist/` / dist/에 패키징

## Data Flow (Audio Thread) / 데이터 흐름 (오디오 스레드)

```
1. Audio driver callback fires (WASAPI/ASIO on Win, CoreAudio on macOS, ALSA/JACK on Linux). ScopedNoDenormals set. / 오디오 드라이버 콜백 발생 (Win: WASAPI/ASIO, macOS: CoreAudio, Linux: ALSA/JACK). ScopedNoDenormals 설정.
2. If muted: zero output, reset levels, return early (fast path) / 뮤트 시: 출력 0, 레벨 리셋, 즉시 반환 (고속 경로)
3. Input PCM float32 copied to pre-allocated work buffer (no heap alloc) / 입력 PCM float32를 사전 할당된 버퍼에 복사 (힙 할당 없음)
4. Channel processing: Mono (average L+R) or Stereo (passthrough) / 채널 처리: Mono (좌우 평균) 또는 Stereo (패스스루)
5. Apply input gain (atomic float) / 입력 게인 적용 (atomic float)
6. Measure input RMS level (every 4th callback — decimation) / 입력 RMS 레벨 측정 (4번째 콜백마다 — 데시메이션)
7. Process through VST chain (graph->processBlock, inline, pre-allocated MidiBuffer) / VST 체인 처리 (인라인, 사전 할당된 MidiBuffer)
8. Copy processed audio to main output (outputChannelData) / 처리된 오디오를 메인 출력(outputChannelData)에 복사
9. Write to AudioRecorder (if recording, lock-free) / AudioRecorder에 기록 (녹음 중이면, 락프리)
10. Write to SharedMemWriter (if IPC enabled) / SharedMemWriter에 기록 (IPC 활성화 시)
11. OutputRouter routes to monitor (if enabled) / OutputRouter가 모니터로 라우팅 (활성화 시):
    Monitor -> volume scale -> lock-free AudioRingBuffer -> MonitorOutput (separate audio device)
12. Measure output RMS level (every 4th callback) / 출력 RMS 레벨 측정 (4번째 콜백마다)
```

## Preset System / 프리셋 시스템

### Auto-save (settings.dppreset) / 자동 저장

- Audio settings (driver type, devices, sample rate, buffer size, input gain) / 오디오 설정
- VST chain (plugins, order, bypass, internal state) / VST 체인
- Output settings (volumes, enables) / 출력 설정
- Active slot index / 활성 슬롯
- Used for app restart restoration only / 앱 재시작 복원용

### Settings Save/Load (.dpbackup) / 설정 저장/불러오기

- Audio/output settings + control mappings (hotkeys, MIDI) / 오디오/출력 설정 + 컨트롤 매핑
- Does NOT include VST chain or preset slots / VST 체인, 프리셋 슬롯 미포함

### Full Backup/Restore (.dpfullbackup) / 전체 백업/복원

- Everything: audio settings + VST chain + control mappings + all preset slots A-E / 전체 포함
- Located in Settings tab > Maintenance section / Settings 탭 > Maintenance 섹션
- **Same-OS only**: Backup files include platform info; cross-OS restore is blocked (device types, plugin paths, hotkey codes are OS-specific) / **같은 OS끼리만**: 백업 파일에 플랫폼 정보 포함; 다른 OS에서 복원 시 차단 (디바이스 타입, 플러그인 경로, 핫키 코드가 OS 종속적)

### Quick Preset Slots (A-E) / 퀵 프리셋 슬롯

- Chain-only: plugins, order, bypass state, plugin internal parameters / 체인 전용
- Audio/output settings are NOT affected by slot switching / 오디오/출력 설정 영향 없음
- Same-chain fast path: bypass + state update only (instant) / 동일 체인: 즉시 전환
- Different-chain: async background loading with **Keep-Old-Until-Ready** (old chain processes audio during load, atomic swap when ready — ~10-50ms suspend vs 1-3s mute gap). `asyncGeneration_` counter discards stale load callbacks. / 다른 체인: 비동기 로딩 + **Keep-Old-Until-Ready** (로딩 중 이전 체인이 오디오 처리 유지, 완료 시 원자적 스왑)
- Auto-save on any change (dirty-flag + 1s debounce), editor close, slot switch / 변경 시 자동 저장

### Plugin State Serialization / 플러그인 상태 직렬화

- `PluginDescription` saved as XML via `createXml()` / XML 저장
- Plugin internal state via `getStateInformation()` -> base64 / 내부 상태 base64 인코딩
- Restored via `setStateInformation()` after plugin load / 로드 후 복원

## Thread Safety Rules / 스레드 안전 규칙

1. **Audio callback (RT thread)** — No heap allocation, no mutexes, no I/O. Pre-allocated buffers only. / 힙 할당, 뮤텍스, I/O 금지. 사전 할당 버퍼만.
2. **Control -> Audio** — `std::atomic` flags only / atomic 플래그만 사용
3. **Graph modification** — `suspendProcessing(true)` before, `false` after / 그래프 수정 시 처리 일시 중지
4. **Chain modification** — `chainLock_` (mutable `CriticalSection`) protects ALL chain access (readers AND writers), never held in `processBlock`. `prepared_` is `std::atomic<bool>`. **Never call `writeToLog` inside `chainLock_`** — capture log string under lock, log after releasing (prevents lock-ordering hazard with DirectPipeLogger `writeMutex_`). / `chainLock_`(mutable `CriticalSection`)이 모든 체인 접근(리더+라이터) 보호, `processBlock`에서는 미사용. `prepared_`는 `std::atomic<bool>`. **`chainLock_` 내부에서 `writeToLog` 호출 금지** — lock 내에서 로그 문자열을 캡처하고 lock 해제 후 로그 기록 (DirectPipeLogger `writeMutex_`와의 lock-ordering 위험 방지).
5. **Async chain loading** — Plugins loaded on `std::thread`, wired on message thread / 백그라운드 로드, 메시지 스레드에서 연결
6. **onChainChanged callback** — Called OUTSIDE `chainLock_` scope (deadlock prevention) / chainLock_ 범위 밖에서 호출
7. **WebSocket/HTTP** — Separate threads, communicate via ActionDispatcher (message-thread delivery guaranteed) / 별도 스레드, ActionDispatcher가 메시지 스레드 전달 보장
8. **ActionDispatcher/StateBroadcaster** — Both guarantee message-thread delivery. Callers from any thread (MIDI, WebSocket, HTTP, hotkey) are deferred via `callAsync`; message-thread callers execute synchronously. / 둘 다 메시지 스레드 전달 보장. 외부 스레드 호출은 callAsync로 지연, 메시지 스레드 호출은 동기 실행.
9. **UI component deletion** — Use `juce::MessageManager::callAsync` for safe self-deletion / callAsync로 안전 삭제
10. **Slot auto-save guard** — `loadingSlot_` (atomic) flag prevents recursive saves / 재귀 저장 방지
11. **Dirty-flag auto-save** — `settingsDirty_` + `dirtyCooldown_` debounce (1s). `onSettingsChanged` callbacks trigger `markSettingsDirty()` / dirty-flag 디바운스 자동 저장
12. **callAsync lifetime guards** — Classes using `callAsync` from background threads protect against use-after-delete. `checkForUpdate` uses `SafePointer`; `VSTChain::replaceChainAsync`, `ActionDispatcher`, `StateBroadcaster` use `shared_ptr<atomic<bool>> alive_` flag pattern (captured by value in lambda, checked before accessing `this`). / `callAsync`를 백그라운드 스레드에서 사용하는 클래스는 소멸 후 접근을 방지. `checkForUpdate`는 `SafePointer` 사용; `VSTChain::replaceChainAsync`, `ActionDispatcher`, `StateBroadcaster`는 `shared_ptr<atomic<bool>> alive_` 플래그 패턴 (람다에서 값 캡처, `this` 접근 전 확인).
13. **MidiHandler bindings mutex** — `std::mutex bindingsMutex_` protects all access to `bindings_`. `getBindings()` returns a copy for safe iteration outside the lock. `processCC`/`processNote` collect matching actions into a local vector, dispatch OUTSIDE `bindingsMutex_` (deadlock prevention). / `std::mutex bindingsMutex_`가 `bindings_` 접근 전체를 보호. `getBindings()`는 잠금 밖에서 안전한 반복을 위해 복사본 반환. `processCC`/`processNote`는 매칭 액션을 로컬 벡터에 수집 후 `bindingsMutex_` 밖에서 디스패치 (교착 방지).
14. **Notification queue overflow guard** — `pushNotification` checks ring buffer capacity before write to prevent overflow. Queue indices use `uint32_t` (overflow-safe). / `pushNotification`이 오버플로 방지를 위해 쓰기 전 링 버퍼 용량 확인. 큐 인덱스는 `uint32_t` 사용 (오버플로 안전).
15. **WebSocket broadcast thread join** — `broadcastToClients` joins the broadcast thread outside `clientsMutex_` lock to prevent potential deadlock. / `broadcastToClients`가 교착 방지를 위해 `clientsMutex_` 잠금 밖에서 브로드캐스트 스레드 join.
16. **OutputRouter bounds check** — `routeAudio()` clamps `numSamples` to `scaledBuffer_` capacity to prevent buffer overrun. / `routeAudio()`가 `numSamples`를 `scaledBuffer_` 용량에 클램프하여 버퍼 오버런 방지.
17. **HTTP proper status codes** — `processRequest` returns `pair<int, string>` with correct status codes (404 Not Found, 405 Method Not Allowed, 400 Bad Request) instead of always 200. / `processRequest`가 올바른 상태 코드(404/405/400)를 `pair<int, string>`으로 반환.
18. **WebSocket RFC 7230 compliance** — Case-insensitive HTTP header matching during handshake. / 핸드셰이크 시 대소문자 무시 HTTP 헤더 매칭.
19. **SafePointer in callAsync** — `PluginChainEditor::addPluginFromDescription`, `PluginScanner` (all 3 background callAsync lambdas), and `Main.cpp` tray tooltip (activeSlot clamped to 0-4) use `SafePointer` for lifetime safety. / `PluginChainEditor::addPluginFromDescription`, `PluginScanner`(백그라운드 callAsync 3개), `Main.cpp` 트레이 툴팁(activeSlot 0-4 클램프)이 수명 안전을 위해 `SafePointer` 사용.
20. **VSTChain movePlugin** — `editorWindows_` resized to match `chain_` size before move to prevent out-of-bounds access. / `movePlugin`에서 이동 전 `editorWindows_`를 `chain_` 크기에 맞춰 조정하여 범위 초과 접근 방지.
21. **VSTChain asyncGeneration_** — `uint32_t` atomic counter incremented per `replaceChainAsync` call. Background thread captures generation at start; `callAsync` callback checks `gen == asyncGeneration_` before modifying graph (discards stale callbacks from superseded loads). / `replaceChainAsync` 호출마다 증가하는 `uint32_t` atomic 카운터. 백그라운드 스레드가 시작 시 generation 캡처; callAsync 콜백이 그래프 수정 전 `gen == asyncGeneration_` 확인 (대체된 로드의 stale 콜백 폐기).
22. **AudioEngine device reconnection** — `deviceLost_` atomic set by `audioDeviceError`, cleared by `audioDeviceAboutToStart` and by `setInputDevice`/`setOutputDevice` (manual device selection during loss). Dual mechanism: ChangeListener (immediate) + 3s timer (fallback). `desiredInputDevice_`/`desiredOutputDevice_` tracked. `attemptReconnection()` preserves SR/BS/channel routing. `attemptingReconnection_` re-entrancy guard (message thread only). `reconnectMissCount_` fallback (accept current devices after 5 misses) only applies to cross-driver stale name scenarios; when `outputAutoMuted_` is true (genuine device loss), counter resets and waits indefinitely. / `deviceLost_` atomic 플래그 — `audioDeviceError`에서 설정, `audioDeviceAboutToStart` 및 `setInputDevice`/`setOutputDevice`(손실 중 수동 선택)에서 해제. 듀얼 감지 메커니즘 + 재진입 가드. `reconnectMissCount_` 폴백은 교차 드라이버 이름 불일치에만 적용; `outputAutoMuted_` true(실제 장치 손실)시 무기한 대기.
23. **MonitorOutput device reconnection** — `monitorLost_` atomic set by `audioDeviceError`/`audioDeviceStopped` (external events only — `shutdown()` removes callback first). Cleared ONLY by `audioDeviceAboutToStart`. Independent 3s cooldown. / 모니터 독립 재연결 + shutdown이 콜백 먼저 제거.
24. **WebSocket sendFrame bool return** — `sendFrame` returns `bool`; on `false`, `broadcastToClients` closes socket for immediate dead client detection (prevents repeated write-error log spam). / `sendFrame`이 `bool` 반환; 실패 시 소켓 즉시 닫아 dead client 감지.
