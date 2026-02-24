# DirectPipe Architecture / 아키텍처

## System Overview / 시스템 개요

DirectPipe is a real-time VST2/VST3 host that processes microphone input through a plugin chain and routes the output to a monitor device. External control sources (hotkeys, MIDI, WebSocket, HTTP) all funnel through a unified ActionDispatcher.

DirectPipe는 마이크 입력을 VST 플러그인 체인으로 실시간 처리하고 모니터 출력으로 라우팅하는 VST 호스트다. 모든 외부 제어(단축키, MIDI, WebSocket, HTTP)는 ActionDispatcher를 통해 통합된다.

```
Mic → WASAPI Shared / ASIO → VST2/VST3 Chain → Monitor Output (Headphones)

External Control:
  Hotkey / MIDI / WebSocket / HTTP → ControlManager → ActionDispatcher
    → VSTChain (bypass), OutputRouter (volume), PresetManager (slots)
```

**Design choices / 설계 원칙:**
- **WASAPI Shared** — Non-exclusive mic access. Other apps can use the mic simultaneously. / 비독점 마이크 접근. 다른 앱과 동시 사용 가능.
- **ASIO** — Lower latency for ASIO-compatible interfaces. / ASIO 호환 인터페이스로 저지연.
- Runtime switching between driver types. UI adapts dynamically. / 드라이버 타입 런타임 전환. UI 자동 적응.

## Components / 컴포넌트

### 1. Host Application (`host/`) / 호스트 앱

JUCE 7.0.12-based desktop application. Main audio processing engine.

JUCE 7.0.12 기반 데스크톱 앱. 메인 오디오 처리 엔진.

#### Audio Module (`host/Source/Audio/`) / 오디오 모듈

- **AudioEngine** — Dual driver support (WASAPI Shared + ASIO). Manages the audio device callback. Pre-allocated work buffers. Mono mixing or stereo passthrough. Runtime device type switching, sample rate/buffer size queries. / 듀얼 드라이버. 오디오 콜백 관리. 사전 할당 버퍼. Mono/Stereo 처리.
- **VSTChain** — `AudioProcessorGraph`-based VST2/VST3 plugin chain. `suspendProcessing()` during graph rebuild. Async chain replacement (`replaceChainAsync`) loads plugins on background thread. Editor windows tracked per-plugin. / VST2/VST3 플러그인 체인. 비동기 체인 교체로 UI 프리즈 방지.
- **OutputRouter** — Distributes processed audio to monitor output. Independent atomic volume and enable controls. Virtual Cable always ON (forced at load, save, and Panic Mute unmute). / 모니터 출력으로 오디오 분배. 독립적 볼륨/활성화 제어. Virtual Cable 항상 ON.
- **LatencyMonitor** — High-resolution timer-based latency measurement. / 고해상도 타이머 기반 레이턴시 측정.

#### Control Module (`host/Source/Control/`) / 제어 모듈

All external inputs funnel through a unified ActionDispatcher. / 모든 외부 입력은 통합된 ActionDispatcher를 거친다.

- **ActionDispatcher** — Central action routing. Actions: `PluginBypass`, `MasterBypass`, `SetVolume`, `ToggleMute`, `LoadPreset`, `PanicMute`, `InputGainAdjust`, `NextPreset`, `PreviousPreset`, `InputMuteToggle`, `SwitchPresetSlot`. / 중앙 액션 라우팅.
- **ControlManager** — Aggregates all control sources (Hotkey, MIDI, WebSocket, HTTP). / 모든 제어 소스 통합.
- **HotkeyHandler** — Windows `RegisterHotKey` API for global keyboard shortcuts. / 글로벌 키보드 단축키.
- **MidiHandler** — JUCE `MidiInput` for MIDI CC mapping with Learn mode. / MIDI CC 매핑 + Learn 모드.
- **WebSocketServer** — RFC 6455 WebSocket server (port 8765). JUCE `StreamingSocket` with handshake, frame encoding/decoding, ping/pong. / RFC 6455 WebSocket 서버.
- **HttpApiServer** — HTTP REST API (port 8766) for one-shot commands. / HTTP REST API.
- **StateBroadcaster** — Pushes state changes to all connected clients. / 상태 변경 시 모든 클라이언트에 푸시.
- **ControlMapping** — JSON-based persistence for hotkey/MIDI/server config. Portable mode support. / JSON 기반 설정 저장.

#### UI Module (`host/Source/UI/`) / UI 모듈

- **AudioSettings** — Driver type selector (WASAPI/ASIO), device selection, sample rate, buffer size, channel mode, ASIO Control Panel button. / 오디오 설정 패널.
- **PluginChainEditor** — Drag-and-drop reordering, bypass toggle, edit button, remove button. / 드래그 앤 드롭 플러그인 체인 편집.
- **PluginScanner** — Out-of-process VST scanner with auto-retry and dead man's pedal. / 별도 프로세스 VST 스캐너.
- **OutputPanel** — Monitor output controls (device selector, volume, enable). / 모니터 출력 제어.
- **PresetManager** — Full preset save/load (JSON) + Quick Preset Slots A-E. Plugin state saved via `getStateInformation()` as base64. Async slot loading. / 프리셋 관리 + 퀵 슬롯 A-E.
- **ControlSettingsPanel** — Hotkey, MIDI, server, general settings (4 sub-tabs). / 단축키/MIDI/서버/일반 설정.
- **GeneralTab** — App-level settings: Start with Windows (registry toggle). / 앱 설정: 시작 프로그램 등록.
- **LevelMeter** — Real-time input/output RMS level display. / 실시간 레벨 미터.
- **DirectPipeLookAndFeel** — Custom dark theme. / 다크 테마.

#### Main Application (`host/Source/MainComponent.cpp`)

- Two-column layout: left (input + VST chain + slot buttons), right (tabbed panel: Audio/Output/Controls) / 2컬럼 레이아웃
- Quick Preset Slot buttons A-E with visual active state / 퀵 프리셋 슬롯 버튼
- Auto-save via dirty-flag + 1-second debounce (not periodic timer). `onSettingsChanged` callbacks from AudioSettings and OutputPanel trigger `markSettingsDirty()`. / dirty-flag + 1초 디바운스 자동 저장
- Panic mute remembers pre-mute monitor enable state, restores on unmute. Virtual Cable always forced ON. / Panic Mute 모니터 상태 기억/복원, Virtual Cable 항상 ON

### 2. Core Library (`core/`) / 코어 라이브러리

Shared static library for IPC. / IPC용 정적 라이브러리.

- **RingBuffer** — SPSC lock-free ring buffer. `std::atomic` with acquire/release. Cache-line aligned (`alignas(64)`). Power-of-2 capacity. / SPSC 락프리 링 버퍼.
- **SharedMemory** — Windows `CreateFileMapping`/`MapViewOfFile` wrapper. / 공유 메모리 래퍼.
- **Protocol** — Shared header structure for IPC communication. / IPC 헤더 구조체.
- **Constants** — Buffer names, sizes, sample rates. / 상수.

### 3. Stream Deck Plugin (`streamdeck-plugin/`) / 스트림 덱 플러그인

Optional Elgato Stream Deck plugin (Node.js, `@elgato/streamdeck` SDK v2). / 선택적 Stream Deck 플러그인 (SDK v2).

- Connects via WebSocket (`ws://localhost:8765`) / WebSocket으로 연결
- 4 SingletonAction subclasses: Bypass Toggle, Panic Mute, Volume Control, Preset Switch / 4개 SingletonAction 액션
- Volume Control supports 3 modes: Mute Toggle, Volume Up (+), Volume Down (-) with configurable step size / 볼륨 제어: 뮤트 토글, 볼륨 +/- 모드 (스텝 사이즈 설정)
- SD+ dial support for smooth volume adjustment / SD+ 다이얼 지원
- Auto-reconnect with exponential backoff (2s → 30s) / 지수 백오프 자동 재연결
- Property Inspector HTML (sdpi-components v4) for each action / 각 액션별 설정 UI
- SVG icon sources in `icons-src/`, PNG generation via `scripts/generate-icons.mjs` / SVG 원본 + PNG 생성 스크립트
- Packaged as `.streamDeckPlugin` in `dist/` / dist/에 패키징

## Data Flow (Audio Thread) / 데이터 흐름 (오디오 스레드)

```
1. WASAPI Shared / ASIO callback fires
   WASAPI Shared / ASIO 콜백 발생
2. Input PCM float32 copied to pre-allocated work buffer (no heap alloc)
   입력 PCM float32를 사전 할당된 버퍼에 복사 (힙 할당 없음)
3. Channel processing: Mono (average) or Stereo (passthrough)
   채널 처리: Mono (평균) 또는 Stereo (패스스루)
4. Apply input gain (atomic float)
   입력 게인 적용 (atomic float)
5. Measure input RMS level
   입력 RMS 레벨 측정
6. Process through VST chain (graph->processBlock, inline)
   VST 체인 처리 (인라인)
7. Copy to output channels → monitor (volume applied)
   출력 채널에 복사 → 모니터 (볼륨 적용)
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
- Auto-save on any change (dirty-flag + 1s debounce), editor close, slot switch / 변경 시 자동 저장 (dirty-flag + 1초 디바운스)

### Plugin State Serialization / 플러그인 상태 직렬화

- `PluginDescription` saved as XML via `createXml()` / XML 저장
- Plugin internal state via `getStateInformation()` → base64 / 내부 상태 base64 인코딩
- Restored via `setStateInformation()` after plugin load / 로드 후 복원

## Thread Safety Rules / 스레드 안전 규칙

1. **Audio callback (RT thread)** — No heap allocation, no mutexes, no I/O / 힙 할당, 뮤텍스, I/O 금지
2. **Control → Audio** — `std::atomic` flags only / atomic 플래그만 사용
3. **Graph modification** — `suspendProcessing(true)` before, `false` after / 그래프 수정 시 처리 일시 중지
4. **Chain modification** — `juce::CriticalSection` on non-RT thread, never in `processBlock` / 비RT 스레드에서만
5. **Async chain loading** — Plugins loaded on `std::thread`, wired on message thread / 백그라운드 로드, 메시지 스레드에서 연결
6. **onChainChanged callback** — Called OUTSIDE `chainLock_` scope (deadlock prevention) / chainLock_ 범위 밖에서 호출
7. **WebSocket/HTTP** — Separate threads, communicate via ActionDispatcher / 별도 스레드
8. **UI component self-deletion** — Use `juce::MessageManager::callAsync` / callAsync 사용
9. **Slot auto-save guard** — `loadingSlot_` flag prevents recursive saves / 재귀 저장 방지
10. **Dirty-flag auto-save** — `settingsDirty_` + `dirtyCooldown_` debounce (1s). `onSettingsChanged` callbacks trigger `markSettingsDirty()` / dirty-flag 디바운스 자동 저장
