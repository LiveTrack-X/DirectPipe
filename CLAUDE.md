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
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build && ctest --config Release
```

## 아키텍처
```
USB Mic → WASAPI Shared → [Mono/Stereo] → VSTChain → OutputRouter
  → SharedMem → [Virtual Loop Mic Driver]
  → WASAPI Out → [Monitor]

외부 제어:
Hotkey/MIDI/WebSocket/HTTP → ControlManager → ActionDispatcher
  → VSTChain (bypass), OutputRouter (volume), PresetManager
```

## 코딩 규칙
- 오디오 콜백: 힙 할당 금지, 뮤텍스 금지
- 제어 → 오디오 통신: atomic flags 또는 lock-free queue
- GUI/제어는 같은 ActionDispatcher 사용 (통일)
- WebSocket/HTTP는 별도 스레드, 오디오와 독립

## 모듈
- `core/` → IPC 라이브러리
- `host/` → JUCE 앱
  - `Audio/` → 오디오 엔진, VST, 출력
  - `Control/` → Hotkey, MIDI, WebSocket, HTTP, Action
  - `UI/` → GUI
- `driver/` → Virtual Loop Mic 커널 드라이버
- `streamdeck-plugin/` → Stream Deck 플러그인 (선택)

## 개발 순서
Phase 0 (환경) → 1 (Core IPC) → 2 (오디오+VST) →
3 (드라이버) → 4 (외부 제어) → 5 (GUI) → 6 (안정화)
