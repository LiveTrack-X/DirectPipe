# DirectPipe Architecture / 아키텍처

## System Overview / 시스템 개요

DirectPipe is a real-time VST2/VST3 host that processes microphone input through a plugin chain. It provides three independent output paths: main output to the AudioSettings device (WASAPI/ASIO), optional monitor output to headphones (separate WASAPI), and IPC output to shared memory for the Receiver VST2 plugin (e.g., OBS). Processed audio can also be recorded to WAV. All external control sources (hotkeys, MIDI, WebSocket, HTTP, Stream Deck) funnel through a unified ActionDispatcher.

DirectPipe는 마이크 입력을 VST 플러그인 체인으로 실시간 처리하는 VST 호스트다. 3가지 독립 출력 경로 제공: 메인 출력(AudioSettings 장치, WASAPI/ASIO), 모니터 출력(헤드폰, 별도 WASAPI), IPC 출력(공유 메모리 → Receiver VST2, 예: OBS). 처리된 오디오의 WAV 녹음도 지원. 모든 외부 제어(단축키, MIDI, WebSocket, HTTP, Stream Deck)는 ActionDispatcher를 통해 통합된다.

```
Mic ─→ WASAPI Shared / ASIO ─→ Input Gain ─→ VST2/VST3 Plugin Chain ─┐
                                                                      │
                 ┌────────────────────────────────────────────────────┼────────────────────┐
                 │                                                    │                    │
           Main Output                                         Monitor Output         IPC Output
        (outputChannelData)                                  (OutputRouter →         (SharedMemWriter →
     Audio tab Output device                                MonitorOutput,       Shared Memory →
     e.g. VB-Cable → Discord                               separate WASAPI →       OBS [Receiver VST2])
                 │                                            Headphones)
           AudioRecorder → WAV File

External Control:
  Hotkey / MIDI / Stream Deck / WebSocket (:8765) / HTTP (:8766)
    → ControlManager → ActionDispatcher
      → VSTChain (bypass/params), OutputRouter (volume), PresetManager (slots),
        AudioRecorder (recording), SharedMemWriter (IPC toggle)
```

**Design choices / 설계 원칙:**
- **WASAPI Shared** — Non-exclusive mic access. Other apps can use the mic simultaneously. / 비독점 마이크 접근. 다른 앱과 동시 사용 가능.
- **ASIO** — Lower latency for ASIO-compatible interfaces. / ASIO 호환 인터페이스로 저지연.
- Runtime switching between driver types. UI adapts dynamically. / 드라이버 타입 런타임 전환. UI 자동 적응.
- **3 output paths** — Main output directly to AudioSettings device (e.g., VB-Audio for Discord). Monitor uses a separate WASAPI device for headphones. IPC output sends to shared memory for Receiver VST2 plugin (e.g., OBS — no virtual cable needed). / 3가지 출력: 메인(AudioSettings 장치), 모니터(별도 WASAPI 헤드폰), IPC(공유 메모리 → Receiver VST2, 가상 케이블 불필요).
- **15 actions** — Unified action system: PluginBypass, MasterBypass, SetVolume, ToggleMute, LoadPreset, PanicMute, InputGainAdjust, NextPreset, PreviousPreset, InputMuteToggle, SwitchPresetSlot, MonitorToggle, RecordingToggle, SetPluginParameter, IpcToggle. / 15개 통합 액션 시스템.

## Components / 컴포넌트

### 1. Host Application (`host/`) / 호스트 앱

JUCE 7.0.12-based desktop application. Main audio processing engine.

JUCE 7.0.12 기반 데스크톱 앱. 메인 오디오 처리 엔진.

#### Audio Module (`host/Source/Audio/`) / 오디오 모듈

- **AudioEngine** — Dual driver support (WASAPI Shared + ASIO). Manages the audio device callback. Pre-allocated work buffers (8ch). Mono mixing or stereo passthrough. Runtime device type switching, sample rate/buffer size queries. Input gain (atomic), master mute, RMS level measurement. `setBufferSize`/`setSampleRate` log errors from `setAudioDeviceSetup`. / 듀얼 드라이버. 오디오 콜백 관리. 사전 할당 버퍼. Mono/Stereo 처리. 입력 게인, 마스터 뮤트, RMS 레벨 측정. `setBufferSize`/`setSampleRate`가 `setAudioDeviceSetup` 오류를 로깅.
- **VSTChain** — `AudioProcessorGraph`-based VST2/VST3 plugin chain. `suspendProcessing()` during graph rebuild. Async chain replacement (`replaceChainAsync`) loads plugins on background thread with `alive_` flag (`shared_ptr<atomic<bool>>`) to guard `callAsync` completion callbacks against object destruction. Editor windows tracked per-plugin. Pre-allocated MidiBuffer. `chainLock_` (mutable `CriticalSection`) protects ALL reader methods (`getPluginSlot`, `getPluginCount`, `setPluginBypassed`, parameter access, editor open/close) — not just writers. `prepared_` is `std::atomic<bool>` for RT-safe access. `processBlock` uses capacity guard instead of misleading buffer size check. `movePlugin` resizes `editorWindows_` before move to prevent out-of-bounds access. / VST2/VST3 플러그인 체인. 비동기 체인 교체로 UI 프리즈 방지. `alive_` 플래그(`shared_ptr<atomic<bool>>`)로 callAsync 콜백의 수명 안전 보장. MidiBuffer 사전 할당. `chainLock_` (mutable `CriticalSection`)이 모든 리더 메서드도 보호. `prepared_`는 `std::atomic<bool>`. `processBlock`은 용량 가드 사용. `movePlugin`은 이동 전 `editorWindows_` 크기 조정.
- **OutputRouter** — Routes processed audio to the monitor output (separate WASAPI device). Independent atomic volume and enable controls. Pre-allocated scaled buffer. `routeAudio()` clamps `numSamples` to `scaledBuffer_` capacity (prevents buffer overrun). Main output goes directly through outputChannelData. / 모니터 출력(별도 WASAPI 장치)으로 오디오 라우팅. `routeAudio()`가 `numSamples`를 `scaledBuffer_` 용량에 클램프 (버퍼 오버런 방지). 메인 출력은 outputChannelData로 직접 전송.
- **MonitorOutput** — Second WASAPI AudioDeviceManager used for the monitor output. Lock-free `AudioRingBuffer` bridge between two audio callback threads. Configured in Output tab. Status tracking (Active/Error/NotConfigured). / 모니터 출력용 별도 WASAPI AudioDeviceManager. 락프리 링버퍼 브리지. Output 탭에서 구성. 상태 추적.
- **AudioRingBuffer** — Header-only SPSC lock-free ring buffer for inter-device audio transfer. `reset()` zeroes all channel data. / 디바이스 간 오디오 전송용 헤더 전용 SPSC 락프리 링 버퍼. `reset()`은 모든 채널 데이터를 0으로 초기화.
- **LatencyMonitor** — High-resolution timer-based latency measurement. / 고해상도 타이머 기반 레이턴시 측정.
- **AudioRecorder** — Lock-free audio recording to WAV via `AudioFormatWriter::ThreadedWriter`. SpinLock-protected writer teardown for RT-safety. Timer-based duration tracking. Auto-stop on device change. `outputStream` properly deleted on writer creation failure (leak fix). / 락프리 WAV 녹음. SpinLock으로 RT 안전한 writer 해제. 장치 변경 시 자동 중지. writer 생성 실패 시 `outputStream` 올바르게 삭제 (누수 수정).

#### Control Module (`host/Source/Control/`) / 제어 모듈

All external inputs funnel through a unified ActionDispatcher. / 모든 외부 입력은 통합된 ActionDispatcher를 거친다.

- **ActionDispatcher** — Central action routing. 15 actions: `PluginBypass`, `MasterBypass`, `SetVolume`, `ToggleMute`, `LoadPreset`, `PanicMute`, `InputGainAdjust`, `NextPreset`, `PreviousPreset`, `InputMuteToggle`, `SwitchPresetSlot`, `MonitorToggle`, `RecordingToggle`, `SetPluginParameter`, `IpcToggle`. Thread-safe dispatch via `callAsync` with `alive_` flag (`shared_ptr<atomic<bool>>`) lifetime guard. Copy-before-iterate for reentrant safety. `actionToString()` helper for enum-to-string conversion. Dispatched actions logged as `[ACTION]` (high-frequency excluded). / 중앙 액션 라우팅. 15개 액션. `alive_` 플래그로 수명 보호된 callAsync 디스패치. 재진입 안전을 위한 copy-before-iterate. `actionToString()` 헬퍼로 enum→문자열 변환. 디스패치된 액션 `[ACTION]` 로그 (고빈도 제외).
- **ControlManager** — Aggregates all control sources (Hotkey, MIDI, WebSocket, HTTP). Initialize/shutdown lifecycle. / 모든 제어 소스 통합 관리.
- **HotkeyHandler** — Windows `RegisterHotKey` API for global keyboard shortcuts. Recording mode for key capture. / 글로벌 키보드 단축키. 키 녹화 모드.
- **MidiHandler** — JUCE `MidiInput` for MIDI CC/note mapping with Learn mode. LED feedback via MidiOutput. Hot-plug detection. `bindingsMutex_` protects all access to `bindings_`; `getBindings()` returns a copy for safe iteration. `processCC`/`processNote` collect matching actions into a local vector, then dispatch OUTSIDE `bindingsMutex_` (deadlock prevention). / MIDI CC 매핑 + Learn 모드. LED 피드백. 핫플러그 감지. `bindingsMutex_`로 `bindings_` 접근 보호; `getBindings()`는 안전한 반복을 위해 복사본 반환. `processCC`/`processNote`는 매칭 액션을 로컬 벡터에 수집 후 `bindingsMutex_` 밖에서 디스패치 (교착 방지).
- **WebSocketServer** — RFC 6455 WebSocket server (port 8765). Custom SHA-1 implementation for handshake. Case-insensitive HTTP header matching (RFC 7230 compliance). JUCE `StreamingSocket` with frame encoding/decoding, ping/pong. Dead client cleanup sweep on broadcast. UDP discovery broadcast on port 8767 at startup for instant Stream Deck connection. `broadcastToClients` joins broadcast thread outside `clientsMutex_` lock to prevent deadlock. / RFC 6455 WebSocket 서버. 커스텀 SHA-1 핸드셰이크. 대소문자 무시 HTTP 헤더 매칭 (RFC 7230 준수). 죽은 클라이언트 자동 정리. 시작 시 UDP 8767 디스커버리 브로드캐스트로 Stream Deck 즉시 연결. `broadcastToClients`는 교착 방지를 위해 `clientsMutex_` 잠금 밖에서 스레드 join.
- **HttpApiServer** — HTTP REST API (port 8766) for one-shot GET commands. CORS enabled. 3-second read timeout. Volume range validation (0.0-1.0). Proper HTTP status codes (404 Not Found, 405 Method Not Allowed, 400 Bad Request) — `processRequest` returns `pair<int, string>`. Recording toggle, plugin parameter, and IPC toggle endpoints. / HTTP REST API. CORS 활성화. 3초 읽기 타임아웃. 볼륨 범위 검증. 적절한 HTTP 상태 코드 (404/405/400) — `processRequest`가 `pair<int, string>` 반환. 녹음 토글, 플러그인 파라미터, IPC 토글 엔드포인트.
- **StateBroadcaster** — Pushes AppState changes to all connected StateListeners as JSON. Includes `ipc_enabled` field in state object. Thread-safe: state updates from any thread, listeners always notified on message thread via `callAsync` with `alive_` flag lifetime guard. Copy-before-iterate for reentrant safety. WebSocket broadcast on dedicated thread (non-blocking). Notification queue: `uint32_t` indices (overflow-safe), capacity guard before write. / 상태 변경을 JSON으로 모든 리스너에 푸시. 상태 객체에 `ipc_enabled` 필드 포함. 스레드 안전: 어느 스레드에서든 상태 업데이트 가능, `alive_` 플래그로 수명 보호된 callAsync 리스너 알림. 재진입 안전을 위한 copy-before-iterate. WebSocket 브로드캐스트 전용 스레드 (비차단). 알림 큐: `uint32_t` 인덱스 (오버플로 안전), 쓰기 전 용량 가드.
- **DirectPipeLogger** — Centralized logging system. Captures logs from all subsystems (audio engine, plugins, WebSocket, HTTP, etc.). Feeds LogPanel and NotificationBar. Ring buffer indices use `uint32_t` (overflow-safe). All log messages use category tags: `[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`. High-frequency actions (SetVolume, InputGainAdjust, SetPluginParameter) excluded from logging. / 중앙 집중 로깅 시스템. 모든 서브시스템 로그 캡처. LogPanel과 NotificationBar로 전달. 링 버퍼 인덱스 `uint32_t` 사용 (오버플로 안전). 모든 로그에 카테고리 태그 사용. 고빈도 액션(SetVolume, InputGainAdjust, SetPluginParameter) 로그 제외.
- **ControlMapping** — JSON-based persistence for hotkey/MIDI/server config. Portable mode support (`portable.flag` next to exe). / JSON 기반 설정 저장. 포터블 모드 지원.

#### IPC Module (`host/Source/IPC/`) / IPC 모듈

- **SharedMemWriter** — Writes processed audio to shared memory using the core library. / core 라이브러리를 사용해 공유 메모리에 오디오 기록.

#### UI Module (`host/Source/UI/`) / UI 모듈

- **AudioSettings** — Driver type selector (WASAPI/ASIO), device selection, ASIO channel routing (input/output pair), sample rate, buffer size, channel mode (Mono/Stereo), latency display, ASIO Control Panel button. / 오디오 설정 패널.
- **PluginChainEditor** — Drag-and-drop reordering, bypass toggle, edit button (native GUI), remove button. Safe deletion via `callAsync`. `addPluginFromDescription` uses `SafePointer` in `callAsync` lambda. / 드래그 앤 드롭 플러그인 체인 편집. callAsync를 통한 안전 삭제. `addPluginFromDescription`은 callAsync 람다에서 `SafePointer` 사용.
- **PluginScanner** — Out-of-process VST scanner with auto-retry (10x) and dead man's pedal. Blacklist for crashed plugins. Real-time text search and column sorting (name/vendor/format). All 3 `callAsync` lambdas from background scan thread use `SafePointer`. / 별도 프로세스 VST 스캐너. 자동 재시도 10회. 블랙리스트. 실시간 검색 및 정렬. 백그라운드 스캔 스레드의 모든 callAsync 람다가 `SafePointer` 사용.
- **OutputPanel** — Three sections: (1) Monitor output: device selector, volume slider, enable toggle, device status indicator (Active/Error/No device). (2) VST Receiver (IPC): enable toggle for shared memory output. (3) Recording: REC/STOP button, elapsed time, Play last recording, Open Folder, folder chooser. Default folder: `Documents\DirectPipe Recordings`. Recording folder persisted to `recording-config.json`. / 3개 섹션: (1) 모니터 출력(장치/볼륨/상태), (2) VST Receiver IPC 토글, (3) 녹음(REC/STOP/재생/폴더). 기본 폴더: `Documents\DirectPipe Recordings`.
- **PresetManager** — Full preset save/load (JSON, `.dppreset`) + Quick Preset Slots A-E. Plugin state via `getStateInformation()`/base64. Async slot loading. / 프리셋 관리 + 퀵 슬롯 A-E. 비동기 슬롯 로딩.
- **ControlSettingsPanel** — 3 sub-tabs: Hotkeys, MIDI, Stream Deck (server status). MIDI tab includes plugin parameter mapping (3-step popup: plugin → parameter → Learn). / 3개 서브탭: 단축키, MIDI, Stream Deck. MIDI 탭에 플러그인 파라미터 매핑 (3단계 팝업).
- **SettingsExporter** — Export/import full settings as `.dpbackup` files via native file chooser. Located in Settings tab (LogPanel). Uses `controlManager_->getConfigStore()` for live config access instead of temporary stores. / 설정 내보내기/가져오기. Settings 탭(LogPanel)에 위치. 임시 저장소 대신 `controlManager_->getConfigStore()`로 라이브 설정 접근.
- **LevelMeter** — Real-time RMS level display with peak hold, clipping indicator. dB log scale. / 실시간 RMS 레벨 미터. 피크 홀드. dB 로그 스케일.
- **LogPanel** — 4th tab "Settings" in right panel. Four sections: (1) Application: "Start with Windows" toggle (HKCU registry). (2) Settings: Save/Load Settings buttons (`.dpbackup` via SettingsExporter). (3) Log: real-time log viewer with timestamped monospaced entries, Export Log / Clear Log buttons. Batch flush optimization: drains all pending lines, single TextEditor insert, single trim pass. (4) Maintenance: Clear Plugin Cache, Clear All Presets, Reset Settings (all with confirmation dialogs). / 우측 패널 4번째 탭 "Settings". 4개 섹션: (1) 시작 프로그램 등록, (2) 설정 저장/불러오기, (3) 실시간 로그 뷰어 + Export/Clear. 배치 플러시 최적화: 모든 대기 줄 drain, 단일 TextEditor 삽입, 단일 트림 패스. (4) 유지보수 도구 (확인 대화상자).
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

- Process priority set to HIGH_PRIORITY_CLASS at startup / 시작 시 프로세스 우선순위 HIGH
- Close button -> minimize to tray / X 버튼 -> 트레이 최소화
- Double-click or left-click tray icon -> show window / 트레이 아이콘 클릭 -> 창 복원
- Right-click -> popup menu: "Show Window", "Start with Windows" (toggle), "Quit DirectPipe" / 우클릭 -> 메뉴
- Start with Windows via `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run` / 레지스트리를 통한 시작 프로그램 등록
- Out-of-process scanner mode via `--scan` argument / --scan 인자로 스캐너 모드

### 2. Core Library (`core/`) / 코어 라이브러리

Shared static library for IPC. No JUCE dependency. / IPC용 정적 라이브러리. JUCE 의존성 없음.

- **RingBuffer** — SPSC lock-free ring buffer. `std::atomic` with acquire/release. Cache-line aligned (`alignas(64)`). Power-of-2 capacity. / SPSC 락프리 링 버퍼.
- **SharedMemory** — Windows `CreateFileMapping`/`MapViewOfFile` wrapper with named events. / 공유 메모리 래퍼.
- **Protocol** — Shared header structure for IPC communication. / IPC 헤더 구조체.
- **Constants** — Buffer names, sizes, sample rates. / 상수.

### 3. Receiver VST Plugin (`plugins/receiver/`) / 리시버 VST 플러그인

VST2 plugin for OBS and other hosts. Reads processed audio from DirectPipe via shared memory IPC (core library). / OBS 등에서 사용하는 VST2 플러그인. 공유 메모리 IPC(core 라이브러리)를 통해 DirectPipe의 처리된 오디오를 읽음.

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
1. WASAPI Shared / ASIO callback fires / WASAPI Shared / ASIO 콜백 발생
2. Input PCM float32 copied to pre-allocated work buffer (no heap alloc) / 입력 PCM float32를 사전 할당된 버퍼에 복사 (힙 할당 없음)
3. Channel processing: Mono (average L+R) or Stereo (passthrough) / 채널 처리: Mono (좌우 평균) 또는 Stereo (패스스루)
4. Apply input gain (atomic float) / 입력 게인 적용 (atomic float)
5. Measure input RMS level / 입력 RMS 레벨 측정
6. Process through VST chain (graph->processBlock, inline, pre-allocated MidiBuffer) / VST 체인 처리 (인라인, 사전 할당된 MidiBuffer)
7. Copy processed audio to main output (outputChannelData) / 처리된 오디오를 메인 출력(outputChannelData)에 복사
8. Write to AudioRecorder (if recording, lock-free) / AudioRecorder에 기록 (녹음 중이면, 락프리)
9. Write to SharedMemWriter (if IPC enabled) / SharedMemWriter에 기록 (IPC 활성화 시)
10. OutputRouter routes to monitor (if enabled) / OutputRouter가 모니터로 라우팅 (활성화 시):
    Monitor -> volume scale -> lock-free AudioRingBuffer -> MonitorOutput (separate WASAPI)
11. Measure output RMS level / 출력 RMS 레벨 측정
```

## Preset System / 프리셋 시스템

### Full Presets (Save/Load) / 풀 프리셋

- Audio settings (driver type, devices, sample rate, buffer size, input gain) / 오디오 설정
- VST chain (plugins, order, bypass, internal state) / VST 체인
- Output settings (volumes, enables) / 출력 설정
- Active slot index / 활성 슬롯

### Quick Preset Slots (A-E) / 퀵 프리셋 슬롯

- Chain-only: plugins, order, bypass state, plugin internal parameters / 체인 전용
- Audio/output settings are NOT affected by slot switching / 오디오/출력 설정 영향 없음
- Same-chain fast path: bypass + state update only (instant) / 동일 체인: 즉시 전환
- Different-chain slow path: async background loading / 다른 체인: 비동기 로딩
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
