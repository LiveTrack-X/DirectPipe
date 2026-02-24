# DirectPipe Architecture / 아키텍처

## System Overview / 시스템 개요

DirectPipe is a real-time VST2/VST3 host that processes microphone input through a plugin chain and routes the output to a monitor device and virtual cable. External control sources (hotkeys, MIDI, WebSocket, HTTP) all funnel through a unified ActionDispatcher.

DirectPipe는 마이크 입력을 VST 플러그인 체인으로 실시간 처리하고 모니터 출력 및 가상 케이블로 라우팅하는 VST 호스트다. 모든 외부 제어(단축키, MIDI, WebSocket, HTTP)는 ActionDispatcher를 통해 통합된다.

```
Mic -> WASAPI Shared / ASIO -> VST2/VST3 Chain -> OutputRouter
                                                     /      \
                                          Virtual Cable   Monitor Output
                                          (VB-Audio etc.) (Headphones)

External Control:
  Hotkey / MIDI / WebSocket / HTTP -> ControlManager -> ActionDispatcher
    -> VSTChain (bypass), OutputRouter (volume), PresetManager (slots)
```

**Design choices / 설계 원칙:**
- **WASAPI Shared** — Non-exclusive mic access. Other apps can use the mic simultaneously. / 비독점 마이크 접근. 다른 앱과 동시 사용 가능.
- **ASIO** — Lower latency for ASIO-compatible interfaces. / ASIO 호환 인터페이스로 저지연.
- Runtime switching between driver types. UI adapts dynamically. / 드라이버 타입 런타임 전환. UI 자동 적응.
- **Dual output** — Monitor for local listening + Virtual Cable for routing to OBS/Discord/etc. / 모니터 로컬 청취 + 가상 케이블로 OBS/Discord 등에 라우팅.

## Components / 컴포넌트

### 1. Host Application (`host/`) / 호스트 앱

JUCE 7.0.12-based desktop application. Main audio processing engine.

JUCE 7.0.12 기반 데스크톱 앱. 메인 오디오 처리 엔진.

#### Audio Module (`host/Source/Audio/`) / 오디오 모듈

- **AudioEngine** — Dual driver support (WASAPI Shared + ASIO). Manages the audio device callback. Pre-allocated work buffers (8ch). Mono mixing or stereo passthrough. Runtime device type switching, sample rate/buffer size queries. Input gain (atomic), master mute, RMS level measurement. / 듀얼 드라이버. 오디오 콜백 관리. 사전 할당 버퍼. Mono/Stereo 처리. 입력 게인, 마스터 뮤트, RMS 레벨 측정.
- **VSTChain** — `AudioProcessorGraph`-based VST2/VST3 plugin chain. `suspendProcessing()` during graph rebuild. Async chain replacement (`replaceChainAsync`) loads plugins on background thread. Editor windows tracked per-plugin. Pre-allocated MidiBuffer. / VST2/VST3 플러그인 체인. 비동기 체인 교체로 UI 프리즈 방지. MidiBuffer 사전 할당.
- **OutputRouter** — Distributes processed audio to 2 destinations: Virtual Cable and Monitor. Independent atomic volume and enable controls per output. Pre-allocated scaled buffer. / 2개 출력(Virtual Cable, Monitor)으로 오디오 분배. 독립적 볼륨/활성화 제어.
- **VirtualMicOutput** — Second WASAPI AudioDeviceManager for virtual cable output. Lock-free `AudioRingBuffer` bridge between two callback threads. Manually configured in Output settings. / 별도 WASAPI 디바이스로 가상 케이블 출력. 락프리 링버퍼 브리지. Output 설정에서 수동 구성.
- **AudioRingBuffer** — Header-only SPSC lock-free ring buffer for inter-device audio transfer. / 디바이스 간 오디오 전송용 헤더 전용 SPSC 락프리 링 버퍼.
- **LatencyMonitor** — High-resolution timer-based latency measurement. / 고해상도 타이머 기반 레이턴시 측정.

#### Control Module (`host/Source/Control/`) / 제어 모듈

All external inputs funnel through a unified ActionDispatcher. / 모든 외부 입력은 통합된 ActionDispatcher를 거친다.

- **ActionDispatcher** — Central action routing. 11 actions: `PluginBypass`, `MasterBypass`, `SetVolume`, `ToggleMute`, `LoadPreset`, `PanicMute`, `InputGainAdjust`, `NextPreset`, `PreviousPreset`, `InputMuteToggle`, `SwitchPresetSlot`. Thread-safe dispatch via `callAsync`. / 중앙 액션 라우팅. 11개 액션. callAsync를 통한 스레드 안전 디스패치.
- **ControlManager** — Aggregates all control sources (Hotkey, MIDI, WebSocket, HTTP). Initialize/shutdown lifecycle. / 모든 제어 소스 통합 관리.
- **HotkeyHandler** — Windows `RegisterHotKey` API for global keyboard shortcuts. Recording mode for key capture. / 글로벌 키보드 단축키. 키 녹화 모드.
- **MidiHandler** — JUCE `MidiInput` for MIDI CC/note mapping with Learn mode. LED feedback via MidiOutput. Hot-plug detection. / MIDI CC 매핑 + Learn 모드. LED 피드백. 핫플러그 감지.
- **WebSocketServer** — RFC 6455 WebSocket server (port 8765). Custom SHA-1 implementation for handshake. JUCE `StreamingSocket` with frame encoding/decoding, ping/pong. Dead client cleanup sweep on broadcast. / RFC 6455 WebSocket 서버. 커스텀 SHA-1 핸드셰이크. 죽은 클라이언트 자동 정리.
- **HttpApiServer** — HTTP REST API (port 8766) for one-shot GET commands. CORS enabled. 3-second read timeout. / HTTP REST API. CORS 활성화. 3초 읽기 타임아웃.
- **StateBroadcaster** — Pushes AppState changes to all connected StateListeners as JSON. / 상태 변경을 JSON으로 모든 리스너에 푸시.
- **ControlMapping** — JSON-based persistence for hotkey/MIDI/server config. Portable mode support (`portable.flag` next to exe). / JSON 기반 설정 저장. 포터블 모드 지원.

#### IPC Module (`host/Source/IPC/`) / IPC 모듈

- **SharedMemWriter** — Writes processed audio to shared memory using the core library. / core 라이브러리를 사용해 공유 메모리에 오디오 기록.

#### UI Module (`host/Source/UI/`) / UI 모듈

- **AudioSettings** — Driver type selector (WASAPI/ASIO), device selection, ASIO channel routing (input/output pair), sample rate, buffer size, channel mode (Mono/Stereo), latency display, ASIO Control Panel button. / 오디오 설정 패널.
- **PluginChainEditor** — Drag-and-drop reordering, bypass toggle, edit button (native GUI), remove button. Safe deletion via `callAsync`. / 드래그 앤 드롭 플러그인 체인 편집. callAsync를 통한 안전 삭제.
- **PluginScanner** — Out-of-process VST scanner with auto-retry (5x) and dead man's pedal. Blacklist for crashed plugins. / 별도 프로세스 VST 스캐너. 자동 재시도 5회. 블랙리스트.
- **OutputPanel** — Monitor output controls: device selector, volume slider, enable toggle. / 모니터 출력 제어.
- **PresetManager** — Full preset save/load (JSON, `.dppreset`) + Quick Preset Slots A-E. Plugin state via `getStateInformation()`/base64. Async slot loading. / 프리셋 관리 + 퀵 슬롯 A-E. 비동기 슬롯 로딩.
- **ControlSettingsPanel** — 4 sub-tabs: Hotkey, MIDI, StreamDeck (server status), General (Start with Windows). / 4개 서브탭: 단축키, MIDI, StreamDeck, 일반.
- **LevelMeter** — Real-time RMS level display with peak hold, clipping indicator. dB log scale. / 실시간 RMS 레벨 미터. 피크 홀드. dB 로그 스케일.
- **DirectPipeLookAndFeel** — Custom dark theme (#1E1E2E bg, #6C63FF purple accent, #4CAF50 green). / 다크 테마.

#### Main Application (`host/Source/MainComponent.cpp`)

- Two-column layout: left (input section + VST chain + slot buttons), right (tabbed panel: Audio/Output/Controls) / 2컬럼 레이아웃
- Quick Preset Slot buttons A-E with visual active/occupied state / 퀵 프리셋 슬롯 버튼 (활성/사용중 시각 구분)
- Auto-save via dirty-flag + 1-second debounce. `onSettingsChanged` callbacks trigger `markSettingsDirty()`. / dirty-flag + 1초 디바운스 자동 저장
- Panic mute remembers pre-mute monitor enable state, restores on unmute / Panic Mute 모니터 상태 기억/복원
- Status bar: latency, CPU, format, portable mode, "Created by LiveTrack" / 상태 바

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

### 3. Stream Deck Plugin (`streamdeck-plugin/`) / 스트림 덱 플러그인

Elgato Stream Deck plugin (Node.js, `@elgato/streamdeck` SDK v2). / Stream Deck 플러그인 (SDK v2).

- Connects via WebSocket (`ws://localhost:8765`) / WebSocket으로 연결
- 4 SingletonAction subclasses: Bypass Toggle, Panic Mute, Volume Control, Preset Switch / 4개 액션
- Volume Control supports 3 modes: Mute Toggle, Volume Up (+), Volume Down (-) with configurable step size / 볼륨 제어: 뮤트 토글, 볼륨 +/- 모드
- SD+ dial support for volume adjustment / SD+ 다이얼 지원
- Auto-reconnect with exponential backoff (2s -> 30s) / 지수 백오프 자동 재연결 (2초 -> 30초)
- Pending message queue (cap 50) while disconnected / 연결 해제 중 대기 큐 (최대 50)
- Property Inspector HTML (sdpi-components v4) for each action / 각 액션별 설정 UI
- SVG icon sources in `icons-src/`, PNG generation via `scripts/generate-icons.mjs` / SVG 원본 + PNG 생성 스크립트
- Packaged as `.streamDeckPlugin` in `dist/` / dist/에 패키징

## Data Flow (Audio Thread) / 데이터 흐름 (오디오 스레드)

```
1. WASAPI Shared / ASIO callback fires
   WASAPI Shared / ASIO 콜백 발생
2. Input PCM float32 copied to pre-allocated work buffer (no heap alloc)
   입력 PCM float32를 사전 할당된 버퍼에 복사 (힙 할당 없음)
3. Channel processing: Mono (average L+R) or Stereo (passthrough)
   채널 처리: Mono (좌우 평균) 또는 Stereo (패스스루)
4. Apply input gain (atomic float)
   입력 게인 적용 (atomic float)
5. Measure input RMS level
   입력 RMS 레벨 측정
6. Process through VST chain (graph->processBlock, inline, pre-allocated MidiBuffer)
   VST 체인 처리 (인라인, 사전 할당된 MidiBuffer)
7. OutputRouter distributes to:
   OutputRouter가 분배:
   a. Virtual Cable -> lock-free AudioRingBuffer -> VirtualMicOutput (WASAPI)
      가상 케이블 -> 락프리 링버퍼 -> VirtualMicOutput
   b. Monitor output -> scaled copy to device output channels
      모니터 출력 -> 스케일링 후 디바이스 출력 채널에 복사
8. Measure output RMS level
   출력 RMS 레벨 측정
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
4. **Chain modification** — `juce::CriticalSection` on non-RT thread, never in `processBlock` / 비RT 스레드에서만
5. **Async chain loading** — Plugins loaded on `std::thread`, wired on message thread / 백그라운드 로드, 메시지 스레드에서 연결
6. **onChainChanged callback** — Called OUTSIDE `chainLock_` scope (deadlock prevention) / chainLock_ 범위 밖에서 호출
7. **WebSocket/HTTP** — Separate threads, communicate via ActionDispatcher / 별도 스레드
8. **UI action dispatch** — `MainComponent::onAction()` checks thread, uses `callAsync` if not on message thread / onAction()에서 스레드 확인, callAsync 사용
9. **UI component deletion** — Use `juce::MessageManager::callAsync` for safe self-deletion / callAsync로 안전 삭제
10. **Slot auto-save guard** — `loadingSlot_` (atomic) flag prevents recursive saves / 재귀 저장 방지
11. **Dirty-flag auto-save** — `settingsDirty_` + `dirtyCooldown_` debounce (1s). `onSettingsChanged` callbacks trigger `markSettingsDirty()` / dirty-flag 디바운스 자동 저장
