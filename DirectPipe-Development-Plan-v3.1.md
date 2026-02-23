# DirectPipe — USB 마이크 VST 호스트 + 초저지연 루프백 시스템

## 개발 계획서 v3.1

> **목표:** USB 마이크 사용자가 하드웨어 오인페 없이 VST2/VST3 플러그인을 적용하고,  
> "Virtual Loop Mic" 가상 마이크를 통해 모든 앱에서 사용할 수 있는 올인원 소프트웨어  
> Light Host 수준의 VST 호환성 + 기존 대비 50~60% 레이턴시 감소  
> 키보드 단축키, MIDI, Stream Deck으로 실시간 제어 가능

---

## 1. 핵심 컨셉

### 1.1 사용자 관점

```
[Before]
USB Mic → Light Host → VB-Cable → Discord/OBS  (40~60ms 딜레이)

[After]
USB Mic → DirectPipe → "Virtual Loop Mic" → Discord/OBS  (13~23ms 딜레이)
                ↑
     키보드/MIDI/StreamDeck으로 실시간 제어
```

### 1.2 Light Host와의 비교

| 항목 | Light Host + VB-Cable | DirectPipe |
|------|----------------------|------------|
| VST2 지원 | ✅ | ✅ |
| VST3 지원 | ✅ | ✅ |
| 가상 마이크 | VB-Cable 별도 설치 | **내장 (Virtual Loop Mic)** |
| 레이턴시 | 40~60ms | **13~23ms** |
| WASAPI 버퍼 경유 | 4~5단 | **2단** |
| 설정 복잡도 | VB-Cable + Light Host 별도 설정 | **원클릭** |
| 모노/스테레오 | 자동 | **선택 가능** |
| 샘플레이트 | 고정 | **선택 가능** |
| 버퍼 사이즈 | 고정 | **선택 가능** |
| 외부 제어 | ❌ | **단축키 + MIDI + Stream Deck** |

### 1.3 레이턴시 상세

```
[기존: Light Host + VB-Cable — WASAPI 4~5단]
USB Mic → WASAPI Shared (~10ms) → Light Host 내부 (~10ms)
  → WASAPI Out → VB-Cable → WASAPI In (~20ms) → 앱
총: ~40~60ms

[DirectPipe — WASAPI 2단 + 공유 메모리 직결]
USB Mic → WASAPI Shared (~10ms) → VST 인라인 (~0ms)
  → 공유 메모리 (~0.05ms) → 드라이버 (~2ms) → 앱 WASAPI In (~10ms)
총: ~21~23ms
```

**버퍼 사이즈별:**

| 버퍼 (samples @48kHz) | ms | 기존 | DirectPipe | 절감 |
|----------------------|-----|------|-----------|------|
| 480 (기본) | 10ms | ~50ms | ~23ms | **54%↓** |
| 256 | 5.3ms | ~35ms | ~13ms | **63%↓** |
| 128 | 2.7ms | ~28ms | ~8ms | **71%↓** |
| 64 | 1.3ms | ~22ms | ~5ms | **77%↓** |

---

## 2. 외부 제어 시스템 (v3.1 추가)

### 2.1 제어 가능한 액션 목록

```
[플러그인 제어]
- Plugin 1 Bypass 토글
- Plugin 2 Bypass 토글
- Plugin N Bypass 토글
- 전체 체인 Bypass (마스터 바이패스)
- 전체 체인 Mute

[볼륨 제어]
- 입력 게인 Up/Down (1dB 단위)
- Virtual Loop Mic 볼륨 Up/Down
- Monitor 볼륨 Up/Down
- Monitor Mute 토글

[프리셋]
- 프리셋 1~8 즉시 전환
- 다음 프리셋 / 이전 프리셋

[유틸리티]
- 전체 뮤트 (패닉 버튼, 모든 출력 즉시 무음)
- 마이크 입력 Mute 토글
```

### 2.2 제어 방식 1: 글로벌 키보드 단축키

```
구현: Windows RegisterHotKey API
→ DirectPipe가 백그라운드/최소화 상태에서도 동작

기본 단축키 (사용자 변경 가능):
- Ctrl+Shift+1     → Plugin 1 Bypass 토글
- Ctrl+Shift+2     → Plugin 2 Bypass 토글
- Ctrl+Shift+3     → Plugin 3 Bypass 토글
- ...
- Ctrl+Shift+0     → 전체 체인 Bypass
- Ctrl+Shift+M     → 전체 뮤트 (패닉 버튼)
- Ctrl+Shift+↑/↓   → 입력 게인 Up/Down
- Ctrl+Shift+F1~F8 → 프리셋 1~8 전환

커스텀 설정:
- UI에서 "단축키 설정" 버튼 → 단축키 녹화 방식
- 충돌 감지 (다른 앱과 겹치는 경우 경고)
- JSON으로 저장/복원
```

### 2.3 제어 방식 2: MIDI CC 매핑

```
구현: JUCE MidiInput (Windows MME / WinRT MIDI)
→ MIDI 컨트롤러, MIDI 키보드, nanoKONTROL 등

동작 방식:
- MIDI Learn: 버튼 클릭 → "MIDI 신호를 보내세요" → 수신된 CC를 매핑
- CC 값 0~63 = OFF, 64~127 = ON (토글)
- CC 값 0~127 = 연속 (볼륨 등에 매핑 시)

매핑 예시:
┌─────────────────────────────────────────────┐
│ MIDI Mapping                                │
│                                              │
│ Action              MIDI        Status       │
│ ─────────────────────────────────────────── │
│ Plugin 1 Bypass     CC#21 Ch1   [Learn] [✕] │
│ Plugin 2 Bypass     CC#22 Ch1   [Learn] [✕] │
│ Master Bypass       CC#30 Ch1   [Learn] [✕] │
│ Input Gain          CC#7  Ch1   [Learn] [✕] │
│ Monitor Volume      CC#11 Ch1   [Learn] [✕] │
│ Panic Mute          Note C1     [Learn] [✕] │
│                                              │
│ MIDI Device: [nanoKONTROL2 ▼]              │
│                     [+ Add Mapping]          │
└─────────────────────────────────────────────┘

MIDI 매핑 타입:
- CC Toggle: CC 값 64+ → ON, <64 → OFF (버튼용)
- CC Momentary: CC 값 64+ 누른 동안 ON (풋스위치용)
- CC Continuous: 0~127 → 0%~100% (페이더/노브용)
- Note On/Off: 노트 키로 토글 (MIDI 키보드용)
```

### 2.4 제어 방식 3: Stream Deck (Elgato)

```
구현: WebSocket 서버 (DirectPipe 내장)
→ Stream Deck 플러그인이 WebSocket으로 연결
→ 또는 Stream Deck의 "Website" 액션으로 HTTP 호출

구조:
┌──────────────┐     WebSocket      ┌─────────────────────┐
│ Stream Deck  │ ←── :8765 ──────→ │ DirectPipe          │
│ Plugin       │                     │ WebSocket Server    │
│              │ {"action":"bypass", │ (내장, 자동 시작)   │
│              │  "plugin":1}        │                     │
└──────────────┘                     └─────────────────────┘

WebSocket 프로토콜 (JSON):

요청 (Stream Deck → DirectPipe):
{
  "type": "action",
  "action": "plugin_bypass",
  "params": { "index": 1 }
}
{
  "type": "action",
  "action": "set_volume",
  "params": { "target": "monitor", "value": 0.5 }
}
{
  "type": "action",
  "action": "load_preset",
  "params": { "index": 3 }
}
{
  "type": "action",
  "action": "panic_mute"
}

응답 (DirectPipe → Stream Deck):
{
  "type": "state",
  "data": {
    "plugins": [
      {"name": "ReaEQ", "bypass": false},
      {"name": "W1 Limiter", "bypass": true}
    ],
    "volumes": {"input": 0.8, "virtual_mic": 1.0, "monitor": 0.7},
    "muted": false,
    "preset": "방송용",
    "latency_ms": 23.1,
    "level_db": -12.3
  }
}

상태 Push (DirectPipe → Stream Deck, 변경 시 자동):
→ bypass 상태 변경 시 즉시 push
→ Stream Deck 버튼 아이콘 자동 업데이트 (ON/OFF 표시)

Stream Deck 플러그인 (별도 개발):
- Elgato SDK (Node.js 기반)로 제작
- 버튼 타입:
  - Plugin Bypass (토글, 상태 표시)
  - Volume Control (길게 누르면 Up, 짧게 누르면 토글)
  - Preset Switch (프리셋 번호 표시)
  - Panic Mute (빨간 버튼)
  - Level Meter (실시간 레벨 표시)

대안 (플러그인 없이):
- Stream Deck의 "Open Website" 액션으로 HTTP endpoint 호출
- DirectPipe가 간단한 HTTP 서버도 제공:
  GET http://localhost:8766/api/bypass/1/toggle
  GET http://localhost:8766/api/mute/toggle
  GET http://localhost:8766/api/preset/3
```

### 2.5 외부 제어 통합 아키텍처

```
┌─────────────────────────────────────────────────────────┐
│                DirectPipe Application                     │
│                                                           │
│  ┌────────────────────────────────────────────────────┐  │
│  │              Control Manager                        │  │
│  │                                                      │  │
│  │  ┌──────────────┐                                   │  │
│  │  │ Hotkey       │──┐                                │  │
│  │  │ Handler      │  │                                │  │
│  │  │ (WinAPI)     │  │    ┌─────────────────────┐    │  │
│  │  └──────────────┘  ├───→│  Action Dispatcher   │    │  │
│  │                     │    │                       │    │  │
│  │  ┌──────────────┐  │    │  모든 소스에서 온     │    │  │
│  │  │ MIDI         │──┤    │  액션을 통일 처리     │───→│──│──→ AudioEngine
│  │  │ Handler      │  │    │                       │    │  │    VSTChain
│  │  │ (JUCE MIDI)  │  │    │  액션 종류:           │    │  │    OutputRouter
│  │  └──────────────┘  │    │  - PluginBypass(n)    │    │  │
│  │                     │    │  - MasterBypass       │    │  │
│  │  ┌──────────────┐  │    │  - SetVolume(t,v)     │    │  │
│  │  │ WebSocket    │──┤    │  - ToggleMute(t)      │    │  │
│  │  │ Server       │  │    │  - LoadPreset(n)      │    │  │
│  │  │ (Stream Deck)│  │    │  - PanicMute          │    │  │
│  │  └──────────────┘  │    └─────────────────────┘    │  │
│  │                     │                                │  │
│  │  ┌──────────────┐  │                                │  │
│  │  │ HTTP API     │──┘                                │  │
│  │  │ (간단 REST)  │                                   │  │
│  │  └──────────────┘                                   │  │
│  │                                                      │  │
│  └────────────────────────────────────────────────────┘  │
│                                                           │
│  ┌────────────────────────────────────────────────────┐  │
│  │              State Broadcaster                      │  │
│  │                                                      │  │
│  │  상태 변경 시 모든 연결된 클라이언트에 push:         │  │
│  │  → WebSocket 클라이언트 (Stream Deck)               │  │
│  │  → GUI 업데이트                                      │  │
│  │  → MIDI 피드백 (LED 있는 컨트롤러)                  │  │
│  └────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### 2.6 제어 설정 UI

```
┌───────────────────────────────────────────────────────┐
│  ⚙ Control Settings                           ✕      │
├───────────────────────────────────────────────────────┤
│                                                        │
│  [Hotkeys] [MIDI] [Stream Deck]                       │
│  ─────────────────────────────────────────────────── │
│                                                        │
│  HOTKEYS                                               │
│  ┌──────────────────────────────────────────────────┐ │
│  │ Action              Shortcut         [Set] [✕]   │ │
│  │ ────────────────────────────────────────────────  │ │
│  │ Plugin 1 Bypass     Ctrl+Shift+1     [Set] [✕]   │ │
│  │ Plugin 2 Bypass     Ctrl+Shift+2     [Set] [✕]   │ │
│  │ Plugin 3 Bypass     (없음)           [Set]        │ │
│  │ Master Bypass       Ctrl+Shift+0     [Set] [✕]   │ │
│  │ Panic Mute          Ctrl+Shift+M     [Set] [✕]   │ │
│  │ Input Gain Up       Ctrl+Shift+↑     [Set] [✕]   │ │
│  │ Input Gain Down     Ctrl+Shift+↓     [Set] [✕]   │ │
│  │ Preset 1            Ctrl+Shift+F1    [Set] [✕]   │ │
│  │ Preset 2            Ctrl+Shift+F2    [Set] [✕]   │ │
│  │                                                    │ │
│  │ [Set] 클릭 → "단축키를 누르세요..." → 녹화       │ │
│  └──────────────────────────────────────────────────┘ │
│                                                        │
│  MIDI                                                  │
│  ┌──────────────────────────────────────────────────┐ │
│  │ MIDI Device: [nanoKONTROL2 ▼]  🟢 Connected     │ │
│  │                                                    │ │
│  │ Action              MIDI          Type             │ │
│  │ ──────────────────────────────────────────────── │ │
│  │ Plugin 1 Bypass     CC#21 Ch1     Toggle  [Learn] │ │
│  │ Plugin 2 Bypass     CC#22 Ch1     Toggle  [Learn] │ │
│  │ Input Gain          CC#7  Ch1     Continuous       │ │
│  │ Monitor Volume      CC#11 Ch1     Continuous       │ │
│  │                                                    │ │
│  │ [Learn] 클릭 → "MIDI 신호를 보내세요..." → 매핑  │ │
│  │                     [+ Add Mapping]                │ │
│  └──────────────────────────────────────────────────┘ │
│                                                        │
│  STREAM DECK                                           │
│  ┌──────────────────────────────────────────────────┐ │
│  │ WebSocket: ws://localhost:8765  🟢 Running        │ │
│  │ HTTP API:  http://localhost:8766 🟢 Running       │ │
│  │                                                    │ │
│  │ Connected Clients: 1                               │ │
│  │                                                    │ │
│  │ [Stream Deck 플러그인 설치 가이드...]             │ │
│  │ [API 문서 열기...]                                │ │
│  └──────────────────────────────────────────────────┘ │
│                                                        │
│                              [Save] [Cancel]          │
└───────────────────────────────────────────────────────┘
```

---

## 3. 시스템 아키텍처 (v3.1)

### 3.1 전체 구조

```
                    외부 제어
          ┌──── 키보드 단축키 (Global Hotkey)
          ├──── MIDI CC (nanoKONTROL, 키보드 등)
          ├──── Stream Deck (WebSocket)
          ├──── HTTP API (범용)
          ▼
┌───────────────────────────────────────────────────────────────────┐
│                     DirectPipe Application                         │
│                                                                     │
│  ┌──────────────┐                                                  │
│  │ Control      │   ┌───────────────┐   ┌───────────────────────┐  │
│  │ Manager      │──→│ Action        │──→│ AudioEngine           │  │
│  │              │   │ Dispatcher    │   │ VSTChain              │  │
│  │ Hotkey       │   │               │   │ OutputRouter          │  │
│  │ MIDI         │   │ 통일된 액션   │   │                       │  │
│  │ WebSocket    │   │ 인터페이스    │   │ ┌─────────────────┐   │  │
│  │ HTTP         │   └───────────────┘   │ │ SharedMem Writer│───│──│──┐
│  └──────────────┘           │            │ └─────────────────┘   │  │  │
│                              │            │                       │  │  │
│  ┌──────────────┐           │            │ ┌─────────────────┐   │  │  │
│  │ State        │◄──────────┘            │ │ Monitor Output  │───│──│──│──→ 헤드폰
│  │ Broadcaster  │                         │ └─────────────────┘   │  │  │
│  │              │──→ GUI 업데이트        └───────────────────────┘  │  │
│  │              │──→ WebSocket push                                  │  │
│  │              │──→ MIDI LED 피드백                                 │  │
│  └──────────────┘                                                    │  │
│                                                                      │  │
│  ┌──────────────┐   ┌───────────────┐                               │  │
│  │ Audio Input  │──→│ VST Chain     │                               │  │
│  │ WASAPI Shared│   │ VST2 / VST3  │──→ OutputRouter               │  │
│  │ SR/Buffer/Ch │   │ Bypass 개별   │                               │  │
│  │ 선택 가능    │   │ 외부제어 가능 │                               │  │
│  └──────────────┘   └───────────────┘                               │  │
└──────────────────────────────────────────────────────────────────────┘  │
                                                                          │
  ┌───────────────────────────────────────────────────────────────────┐   │
  │              Virtual Loop Mic Driver (Kernel, WDM)                 │   │
  │  공유 메모리에서 PCM 읽기 → "Virtual Loop Mic" 으로 노출          │◄──┘
  │  모든 앱에서 일반 마이크로 선택 가능                               │
  └───────────────────────────────────────────────────────────────────┘
```

### 3.2 데이터 플로우

```
[오디오 콜백 — 실시간 스레드]

1. WASAPI Shared 콜백 (버퍼: 사용자 선택, 기본 480 samples)
2. 채널 처리 (Mono 또는 Stereo, 사용자 선택)
3. VST2/VST3 체인 processBlock() 인라인
   - 각 플러그인 bypass 상태는 외부에서 실시간 변경 가능
   - bypass 토글은 atomic flag로 lock-free 처리
4. 출력 분배:
   a. 공유 메모리 → Virtual Loop Mic
   b. WASAPI 출력 → 헤드폰 모니터
5. Named Event 시그널

[제어 이벤트 — 비동기, 메인 스레드]
- Hotkey/MIDI/WebSocket → Action Dispatcher → atomic flag 변경
- 오디오 스레드는 다음 콜백에서 변경된 flag 반영
- lock-free: 제어 이벤트가 오디오를 블로킹하지 않음
```

---

## 4. 디렉토리 구조 (v3.1)

```
directpipe/
├── CMakeLists.txt
├── CLAUDE.md
├── README.md
│
├── core/                              # 공유 IPC 라이브러리
│   ├── CMakeLists.txt
│   ├── include/directpipe/
│   │   ├── RingBuffer.h
│   │   ├── SharedMemory.h
│   │   ├── Protocol.h
│   │   └── Constants.h
│   └── src/
│       ├── RingBuffer.cpp
│       └── SharedMemory.cpp
│
├── host/                              # JUCE VST Host 앱
│   ├── CMakeLists.txt
│   ├── Source/
│   │   ├── Main.cpp
│   │   ├── MainComponent.h/cpp
│   │   │
│   │   ├── Audio/
│   │   │   ├── AudioEngine.h/cpp
│   │   │   ├── VSTChain.h/cpp
│   │   │   ├── OutputRouter.h/cpp
│   │   │   └── LatencyMonitor.h/cpp
│   │   │
│   │   ├── IPC/
│   │   │   └── SharedMemWriter.h/cpp
│   │   │
│   │   ├── Control/                   # ← v3.1 추가
│   │   │   ├── ControlManager.h/cpp   # 모든 외부 입력 통합 관리
│   │   │   ├── ActionDispatcher.h/cpp # 액션 → 오디오 엔진 전달
│   │   │   ├── StateBroadcaster.h/cpp # 상태 변경 → 모든 클라이언트 push
│   │   │   ├── HotkeyHandler.h/cpp    # Windows Global Hotkey
│   │   │   ├── MidiHandler.h/cpp      # MIDI CC 매핑 + Learn
│   │   │   ├── WebSocketServer.h/cpp  # Stream Deck WebSocket
│   │   │   ├── HttpApiServer.h/cpp    # 간단 REST API
│   │   │   └── ControlMapping.h/cpp   # 매핑 저장/로드 (JSON)
│   │   │
│   │   └── UI/
│   │       ├── DeviceSelector.h/cpp
│   │       ├── AudioSettings.h/cpp
│   │       ├── PluginChainEditor.h/cpp
│   │       ├── PluginScanner.h/cpp
│   │       ├── LevelMeter.h/cpp
│   │       ├── OutputPanel.h/cpp
│   │       ├── PresetManager.h/cpp
│   │       ├── ControlSettingsPanel.h/cpp  # ← v3.1 추가
│   │       └── LookAndFeel.h/cpp
│   └── Resources/
│
├── streamdeck-plugin/                 # ← v3.1 추가 (선택적)
│   ├── package.json
│   ├── manifest.json                  # Stream Deck 플러그인 매니페스트
│   ├── src/
│   │   ├── plugin.js                  # 메인 플러그인 로직
│   │   ├── actions/
│   │   │   ├── bypass-toggle.js       # 바이패스 토글 액션
│   │   │   ├── volume-control.js      # 볼륨 제어 액션
│   │   │   ├── preset-switch.js       # 프리셋 전환 액션
│   │   │   └── panic-mute.js          # 패닉 뮤트 액션
│   │   └── websocket-client.js        # DirectPipe 연결
│   └── images/                        # Stream Deck 버튼 아이콘
│       ├── bypass-on.png
│       ├── bypass-off.png
│       ├── mute.png
│       └── preset.png
│
├── driver/                            # Virtual Loop Mic 드라이버
│   ├── README.md
│   ├── virtualloop.vcxproj
│   ├── inf/virtualloop.inf
│   ├── src/
│   │   ├── adapter.cpp
│   │   ├── miniport.cpp
│   │   ├── stream.cpp
│   │   └── shm_kernel_reader.cpp
│   └── test/driver_test.cpp
│
├── installer/
│   ├── directpipe.iss
│   └── install-driver.ps1
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_ring_buffer.cpp
│   ├── test_shared_memory.cpp
│   ├── test_latency.cpp
│   ├── test_ipc_integration.cpp
│   ├── test_action_dispatcher.cpp     # ← v3.1 추가
│   └── test_websocket_protocol.cpp    # ← v3.1 추가
│
└── docs/
    ├── ARCHITECTURE.md
    ├── BUILDING.md
    ├── USER_GUIDE.md
    ├── CONTROL_API.md                 # ← v3.1 추가 (WebSocket/HTTP API 문서)
    └── STREAMDECK_GUIDE.md            # ← v3.1 추가
```

---

## 5. 개발 Phase (v3.1) — 진행 상황

### Phase 0: 환경 설정 — DONE

```
완료:
✅ P0-1: CMake 프로젝트 구조 생성
✅ P0-2: JUCE 7.0.12 FetchContent 추가
✅ P0-3: VST2 SDK 설정 (thirdparty/VST2_SDK 인터페이스 헤더)
✅ P0-4: WDK 설치
✅ P0-5: CLAUDE.md 작성
✅ P0-6: 빌드 검증 (Linux CI + Windows 대상)
```

### Phase 1: Core IPC — DONE

```
완료:
✅ P1-1: Protocol.h — SharedHeader 구조체 (cache-line aligned)
✅ P1-2: RingBuffer.h/cpp — SPSC lock-free (atomic acquire/release)
✅ P1-3: SharedMemory.h/cpp — Windows shared memory + POSIX fallback + Named Event
✅ P1-4: Constants.h
✅ P1-5: 단위 테스트 (18개: RingBuffer 11 + SharedMemory 7)
✅ P1-6: 레이턴시 벤치마크 테스트 3개
✅ P1-7: IPC 통합 테스트 12개

산출물: directpipe-core 정적 라이브러리, 33개 테스트 통과
```

### Phase 2: 오디오 엔진 + VST 호스트 — DONE

```
완료:
✅ P2-1: AudioEngine — WASAPI Shared, SR/Buffer/Channel 선택 가능
         - 사전할당 workBuffer (RT 힙할당 금지)
         - 모노 믹싱: 0.5*L + 0.5*R 평균
✅ P2-2: 채널 처리 — Mono/Stereo atomic 전환
✅ P2-3: VSTChain — VST2 + VST3, AudioProcessorGraph 기반
         - 사전할당 MidiBuffer (RT 안전)
         - suspendProcessing으로 그래프 재구성 스레드 안전
         - graph rebuild 시 nodeId 정상 갱신
✅ P2-4: OutputRouter — SharedMem + Monitor, 독립 볼륨/이네이블
         - 미사용 채널 클리어 (모노 입력 대응)
✅ P2-5: LatencyMonitor — high_resolution_clock 기반
✅ P2-6: SharedMemWriter — producer-side IPC

산출물: GUI 없이 동작하는 오디오 엔진 (코드 완성, Windows 빌드 필요)
```

### Phase 3: Virtual Loop Mic 드라이버 — DONE (코드)

```
완료:
✅ P3-1: Sysvad 포크 및 최소화 (Capture만)
✅ P3-2: 공유 메모리 Reader (커널 모드, ZwOpenSection)
✅ P3-3: WDM 스트림 — SR/Channel 자동 매칭
✅ P3-4: INF + 설치/제거 스크립트
⬜ P3-5: 테스트 서명 + Discord/Zoom 테스트 (Windows 환경 필요)
⬜ P3-6: 다중 클라이언트 + 안정성 테스트 (Windows 환경 필요)

산출물: virtualloop.sys 소스 코드 완성 (WDK 빌드 + 서명 필요)
```

### Phase 4: 외부 제어 시스템 — DONE

```
목표: 키보드/MIDI/Stream Deck으로 VST bypass 등 실시간 제어

태스크:
□ P4-1: ActionDispatcher — 통일된 액션 인터페이스

  enum class Action {
      PluginBypass,      // params: plugin_index
      MasterBypass,
      SetVolume,         // params: target, value (0.0~1.0)
      ToggleMute,        // params: target
      LoadPreset,        // params: preset_index
      PanicMute,         // 모든 출력 즉시 무음
      InputGainAdjust,   // params: delta_db (+1 or -1)
  };

  struct ActionEvent {
      Action action;
      int intParam = 0;
      float floatParam = 0.0f;
      std::string stringParam;
  };

  - dispatch(ActionEvent) → AudioEngine/VSTChain에 전달
  - 오디오 스레드와의 통신: lock-free queue 또는 atomic flags
  - GUI에서도 같은 ActionDispatcher 사용 (통일)

□ P4-2: StateBroadcaster — 상태 변경 알림

  struct AppState {
      struct PluginState {
          std::string name;
          bool bypassed;
          bool loaded;
      };
      std::vector<PluginState> plugins;
      float inputGain;
      float virtualMicVolume;
      float monitorVolume;
      bool masterBypassed;
      bool muted;
      std::string currentPreset;
      float latencyMs;
      float inputLevelDb;
  };

  - 상태 변경 시 리스너들에게 push
  - 리스너: GUI, WebSocket 클라이언트, MIDI LED

□ P4-3: HotkeyHandler — 글로벌 키보드 단축키

  구현:
  - RegisterHotKey(hwnd, id, modifiers, vk) — Win32 API
  - 메시지 루프에서 WM_HOTKEY 수신
  - JUCE의 경우: Timer나 AsyncUpdater로 메인 스레드 처리
  - DirectPipe 최소화/트레이 상태에서도 동작

  기능:
  - 사용자 정의 단축키 등록/해제
  - 충돌 감지 (RegisterHotKey 실패 시 알림)
  - 설정 저장/로드 (JSON)

  테스트:
  - 앱 포커스 없을 때 hotkey 동작 확인
  - 여러 hotkey 동시 등록/해제
  - 앱 종료 시 hotkey 정리 확인

□ P4-4: MidiHandler — MIDI CC 매핑

  구현:
  - juce::MidiInput으로 MIDI 장치 열기
  - handleIncomingMidiMessage 콜백
  - MIDI Learn 모드:
    1. UI에서 [Learn] 클릭 → 리스닝 모드
    2. 사용자가 MIDI 컨트롤러 조작
    3. 수신된 CC#/Channel 자동 매핑
    4. 매핑 저장

  매핑 타입:
  - Toggle: CC ≥64 → ON, CC <64 → OFF
  - Momentary: CC ≥64 → ON (누르는 동안), CC <64 → OFF
  - Continuous: CC 0~127 → 0.0~1.0 (볼륨/게인)
  - NoteOnOff: Note ON → 토글

  기능:
  - 여러 MIDI 장치 동시 사용
  - 장치 핫플러그 (연결/분리 감지)
  - MIDI 피드백 (LED 있는 컨트롤러: bypass ON → LED ON)
  - 매핑 저장/로드 (JSON)

□ P4-5: WebSocketServer — Stream Deck 연결

  구현:
  - 경량 WebSocket 서버 (포트 8765)
  - 라이브러리: beast (Boost) 또는 IXWebSocket 또는 직접 구현
  - JUCE에 내장된 WebSocket은 없으므로 외부 라이브러리 필요
  - 추천: cpp-httplib (HTTP) + IXWebSocket (WebSocket)
    또는 uWebSockets (경량, 고성능)

  프로토콜:
  - JSON 기반 요청/응답
  - 액션 요청 → ActionDispatcher
  - 상태 push → StateBroadcaster에서 자동

  자동 시작:
  - DirectPipe 실행 시 WebSocket 서버 자동 시작
  - 실패 시 포트 변경 시도 (8765 → 8766 → ...)

□ P4-6: HttpApiServer — 간단 REST API (Stream Deck 대안)

  구현:
  - cpp-httplib 기반 간단 HTTP 서버 (포트 8766)
  - Stream Deck에서 "Open Website" 액션으로 호출 가능

  엔드포인트:
  GET /api/status                    → 전체 상태 JSON
  GET /api/bypass/:index/toggle      → 플러그인 bypass 토글
  GET /api/bypass/master/toggle      → 마스터 bypass
  GET /api/mute/toggle               → 전체 뮤트
  GET /api/mute/panic                → 패닉 뮤트
  GET /api/volume/:target/:value     → 볼륨 설정
  GET /api/preset/:index             → 프리셋 로드
  GET /api/gain/:delta               → 입력 게인 조절

  장점: Stream Deck 플러그인 없이도 바로 사용 가능

□ P4-7: ControlMapping — 매핑 저장/로드

  JSON 파일: directpipe-controls.json
  {
    "hotkeys": [
      {"action": "plugin_bypass", "params": {"index": 1},
       "key": "Ctrl+Shift+1"},
      {"action": "panic_mute", "key": "Ctrl+Shift+M"}
    ],
    "midi": [
      {"action": "plugin_bypass", "params": {"index": 1},
       "device": "nanoKONTROL2", "type": "toggle",
       "cc": 21, "channel": 1},
      {"action": "set_volume", "params": {"target": "monitor"},
       "device": "nanoKONTROL2", "type": "continuous",
       "cc": 7, "channel": 1}
    ],
    "websocket": {"port": 8765, "enabled": true},
    "http": {"port": 8766, "enabled": true}
  }

□ P4-8: ControlSettingsPanel UI
  - Hotkeys 탭: 단축키 목록 + [Set] + [✕]
  - MIDI 탭: MIDI 장치 선택 + 매핑 목록 + [Learn]
  - Stream Deck 탭: 연결 상태 + 포트 + 가이드 링크

□ P4-9: 테스트
  - Hotkey: 백그라운드에서 bypass 토글 확인
  - MIDI: nanoKONTROL (또는 가상 MIDI) 테스트
  - WebSocket: 테스트 클라이언트로 액션/상태 확인
  - HTTP: curl/브라우저로 API 호출 확인
  - 동시 제어: Hotkey + MIDI 동시에 같은 액션 → 충돌 없음 확인

완료:
✅ P4-1: ActionDispatcher — 통일된 액션 인터페이스 (23개 테스트 통과)
✅ P4-2: StateBroadcaster — 상태 변경 알림
✅ P4-3: HotkeyHandler — 글로벌 키보드 단축키
✅ P4-4: MidiHandler — MIDI CC 매핑 + Learn
✅ P4-5: WebSocketServer — Stream Deck 연결 (unique_ptr 소유권 버그 수정 완료)
✅ P4-6: HttpApiServer — 간단 REST API
✅ P4-7: ControlMapping — 매핑 저장/로드 (JSON)
✅ P4-8: ControlSettingsPanel UI
⬜ P4-9: 통합 테스트 (Windows 환경 필요)

산출물: 전체 외부 제어 코드 완성
```

### Phase 5: GUI — DONE (코드)

```
태스크:
□ P5-1: 메인 윈도우 레이아웃

  ┌───────────────────────────────────────────────────┐
  │  🎙 DirectPipe                     [⚙] ─ □ ✕    │
  ├───────────────────────────────────────────────────┤
  │                                                    │
  │  INPUT                                             │
  │  ┌─────────────────────────────────────────────┐  │
  │  │ 🎤 [Blue Yeti ▼]                            │  │
  │  │ ■■■■■■■■■□□□□□□  -12.3 dB   Gain [====]    │  │
  │  │                                              │  │
  │  │ SR: [48000 ▼] Buffer: [480 ▼] Ch: [●M][○S] │  │
  │  │ Estimated Latency: ~23ms                     │  │
  │  └─────────────────────────────────────────────┘  │
  │                                                    │
  │  VST CHAIN                                         │
  │  ┌─────────────────────────────────────────────┐  │
  │  │ 1. ▶ ReaEQ           [Edit] [Bypass] [✕]   │  │
  │  │    Ctrl+Shift+1 │ MIDI CC#21               │  │
  │  │                                              │  │
  │  │ 2. ▶ W1 Limiter      [Edit] [Bypass] [✕]   │  │
  │  │    Ctrl+Shift+2 │ MIDI CC#22               │  │
  │  │                                              │  │
  │  │ 3. ▶ OrilRiver       [Edit] [Bypass] [✕]   │  │
  │  │    Ctrl+Shift+3 │ (미설정)                  │  │
  │  │                                              │  │
  │  │          [+ Add VST Plugin]                  │  │
  │  │ Master Bypass: [OFF]  Ctrl+Shift+0          │  │
  │  └─────────────────────────────────────────────┘  │
  │                                                    │
  │  OUTPUT                                            │
  │  ┌─────────────────────────────────────────────┐  │
  │  │ 🟢 Virtual Loop Mic  ■■■■■■□□ Vol [=====]  │  │
  │  │ 🔵 Monitor (헤드폰)  ■■■■□□□□ Vol [=====]  │  │
  │  │                                              │  │
  │  │ Monitor: [Realtek HD Audio ▼]               │  │
  │  └─────────────────────────────────────────────┘  │
  │                                                    │
  ├───────────────────────────────────────────────────┤
  │ ⏱ 23ms │ CPU 1.8% │ 48k/Mono │ 🟢 Active │ MIDI🟢│
  └───────────────────────────────────────────────────┘

  [⚙] → Control Settings 패널 열기

□ P5-2: DeviceSelector — 입력/모니터 장치 선택
□ P5-3: AudioSettings — SR, 버퍼, 채널 모드
□ P5-4: PluginChainEditor — VST 추가/삭제/순서/에디터
         - 각 플러그인 행에 할당된 단축키/MIDI 표시
□ P5-5: OutputPanel — Virtual Loop Mic 상태 + 볼륨
□ P5-6: LevelMeter — 입출력 레벨
□ P5-7: PresetManager — JSON 프리셋
□ P5-8: 시스템 트레이

완료:
✅ P5-1: 메인 윈도우 레이아웃
✅ P5-2: DeviceSelector — 입력/모니터 장치 선택
✅ P5-3: AudioSettings — SR, 버퍼, 채널 모드
✅ P5-4: PluginChainEditor — VST 추가/삭제/순서/에디터
✅ P5-5: OutputPanel — Virtual Loop Mic 상태 + 볼륨
✅ P5-6: LevelMeter — 입출력 레벨
✅ P5-7: PresetManager — JSON 프리셋
⬜ P5-8: 시스템 트레이 (Windows 환경 필요)

산출물: GUI 코드 완성 (Windows JUCE 빌드 필요)
```

### Phase 6: 안정화 + 설치 프로그램 — IN PROGRESS

```
태스크:
□ P6-1: 에러 핸들링 (USB 분리, VST 크래시, 드라이버 미설치)
□ P6-2: CPU 최적화
□ P6-3: 호환성 테스트 (마이크, VST, Discord/Zoom/OBS)
□ P6-4: 장시간 안정성 (8시간+)
□ P6-5: 설치 프로그램 (Inno Setup)
□ P6-6: Stream Deck 플러그인 패키징 (선택적)

완료:
✅ P6-1 (일부): 오디오 RT-safety 버그 수정 완료
  - VSTChain graph rebuild nodeId 갱신
  - RT 콜백 힙 할당 제거 (AudioEngine workBuffer, VSTChain MidiBuffer)
  - 모노 믹싱 게인 정규화 (0.5+0.5 평균)
  - VSTChain suspendProcessing 스레드 안전
  - OutputRouter 미사용 채널 클리어
  - WebSocket unique_ptr 소유권 수정
  - ActionDispatcher 오타 수정
✅ P6-1 (일부): JUCE 7.0.12 컴파일 에러 수정 (FontOptions, lParam)
✅ P6-1 (일부): 누락 헤더/종속성 수정
⬜ P6-2: CPU 최적화 (SIMD, zero-copy)
⬜ P6-3: Windows 통합 테스트 (USB 마이크, VST, Discord/Zoom/OBS)
⬜ P6-4: 장시간 안정성 (8시간+)
⬜ P6-5: Inno Setup 설치 프로그램
⬜ P6-6: Stream Deck 플러그인 패키징

산출물: 릴리즈 빌드 + 설치 프로그램 (Windows 환경에서 계속)
```

---

## 6. CLAUDE.md (v3.1)

```markdown
# DirectPipe — Claude Code 프로젝트 가이드 v3.1

## 프로젝트 설명
USB 마이크용 VST2/VST3 호스트 + 초저지연 루프백.
"Virtual Loop Mic" 가상 장치로 출력. Light Host 수준 호환성.
기존 대비 50~60% 레이턴시 감소.
키보드 단축키, MIDI CC, Stream Deck, HTTP API로 실시간 제어.

## 핵심 원칙
1. WASAPI Shared 기본 (원본 마이크 비독점)
2. VST2 + VST3 호스팅
3. 공유 메모리 → Virtual Loop Mic 드라이버 직결
4. SR/Buffer/Channel 사용자 선택
5. 외부 제어: Hotkey, MIDI, WebSocket, HTTP (모두 ActionDispatcher 경유)

## 기술 스택
- C++17, JUCE 7+, CMake 3.22+
- WASAPI Shared Mode
- VST2 SDK 2.4 + VST3
- WDK — Virtual Loop Mic 드라이버
- SPSC Lock-free Ring Buffer + Windows Shared Memory
- WebSocket (IXWebSocket 또는 uWebSockets)
- HTTP (cpp-httplib)

## 빌드
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVST2_SDK_DIR=/path/to/vstsdk2.4
cmake --build build --config Release

## 아키텍처
USB Mic → WASAPI Shared → [Mono/Stereo] → VSTChain → OutputRouter
  → SharedMem → [Virtual Loop Mic Driver]
  → WASAPI Out → [Monitor]

외부 제어:
Hotkey/MIDI/WebSocket/HTTP → ControlManager → ActionDispatcher
  → VSTChain (bypass), OutputRouter (volume), PresetManager

## 코딩 규칙
- 오디오 콜백: 힙 할당 금지, 뮤텍스 금지
- 제어 → 오디오 통신: atomic flags 또는 lock-free queue
- GUI/제어는 같은 ActionDispatcher 사용 (통일)
- WebSocket/HTTP는 별도 스레드, 오디오와 독립

## 모듈
- core/ → IPC 라이브러리
- host/ → JUCE 앱
  - Audio/ → 오디오 엔진, VST, 출력
  - Control/ → Hotkey, MIDI, WebSocket, HTTP, Action
  - UI/ → GUI
- driver/ → Virtual Loop Mic 커널 드라이버
- streamdeck-plugin/ → Stream Deck 플러그인 (선택)

## 개발 순서
Phase 0 (환경) → 1 (Core IPC) → 2 (오디오+VST) →
3 (드라이버) → 4 (외부 제어) → 5 (GUI) → 6 (안정화)
```

---

## 7. 성능 목표

| 지표 | 목표 |
|------|------|
| 레이턴시 (버퍼 480) | ≤ 25ms |
| 레이턴시 (버퍼 128) | ≤ 10ms |
| CPU (VST 0개) | ≤ 2% |
| CPU (VST 3개) | ≤ 8% |
| 메모리 (VST 0개) | ≤ 30MB |
| 연속 동작 | 8시간+ 글리치 없음 |
| Hotkey 반응 | ≤ 10ms (다음 오디오 콜백에 반영) |
| MIDI 반응 | ≤ 10ms |
| WebSocket 반응 | ≤ 50ms (네트워크 포함) |

---

## 8. 타임라인 및 현재 상태

```
Week 1:  Phase 0 (환경) + Phase 1 (Core IPC)              ✅ 완료
Week 2:  Phase 2 (오디오 엔진 + VST 호스트)                ✅ 완료
Week 3:  Phase 3 (Virtual Loop Mic 드라이버)               ✅ 코드 완료
Week 4:  Phase 4 (외부 제어: Hotkey + MIDI + WS + HTTP)    ✅ 완료
Week 5:  Phase 5 (GUI)                                      ✅ 코드 완료
Week 6:  Phase 6 (안정화 + 설치 프로그램)                   🔄 진행 중

마일스톤:
- Week 1 끝: IPC 동작 ✅ (33개 테스트 통과)
- Week 2 끝: 마이크→VST→공유메모리 ✅ (코드 완성)
- Week 3 끝: Virtual Loop Mic 장치 등록 ✅ (코드 완성, WDK 빌드 필요)
- Week 4 끝: Hotkey/MIDI/StreamDeck 제어 동작 ✅ (23개 테스트 통과)
- Week 5 끝: 완전한 GUI ✅ (코드 완성, Windows JUCE 빌드 필요)
- Week 6 끝: 릴리즈 가능 🔄 (RT-safety 버그 수정 완료, Windows 통합 테스트 필요)
```

### 다음 단계 (Windows 환경에서)

1. Windows에서 전체 JUCE 앱 빌드 (Visual Studio 2022)
2. USB 마이크 연결 후 통합 테스트
3. Virtual Loop Mic 드라이버 WDK 빌드 + 테스트 서명
4. Discord/Zoom/OBS 실제 동작 테스트
5. CPU/메모리 최적화
6. Inno Setup 설치 프로그램 제작

---

## 9. 리스크 및 대응

| 리스크 | 심각도 | 대응 |
|--------|--------|------|
| USB 마이크 작은 버퍼 미지원 | 중 | 자동 조정 + 경고 |
| VST2 SDK 라이선스 | 중 | 기존 보유 SDK 사용 |
| VST2 크래시 | 중 | SEH 보호 + 자동 bypass |
| 커널 드라이버 BSOD | 높 | 가상머신 + Driver Verifier |
| 드라이버 서명 | 중 | 테스트 서명 → Attestation |
| WebSocket 포트 충돌 | 낮 | 자동 포트 폴백 |
| MIDI 장치 핫플러그 | 낮 | JUCE MidiInput 재스캔 |
| Hotkey 다른 앱과 충돌 | 낮 | 등록 실패 시 경고 + 대체 키 제안 |
| Stream Deck SDK 변경 | 낮 | HTTP API를 대안으로 유지 |
