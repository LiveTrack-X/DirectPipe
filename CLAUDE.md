# DirectPipe — Claude Code 프로젝트 가이드 v3.2

## 프로젝트 설명
USB 마이크용 VST2/VST3 호스트 + 초저지연 루프백.
"Virtual Loop Mic" 가상 장치로 출력. Light Host 수준 호환성.
기존 대비 50~60% 레이턴시 감소.
키보드 단축키, MIDI CC, Stream Deck, HTTP API로 실시간 제어.

## 핵심 원칙
1. WASAPI Shared 기본 (원본 마이크 비독점)
2. VST2 + VST3 호스팅
3. 공유 메모리 → Virtual Loop Mic 드라이버 직결
4. SR/Buffer/Channel 사용자 선택 (기본 Stereo)
5. 외부 제어: Hotkey, MIDI, WebSocket, HTTP (모두 ActionDispatcher 경유)
6. 시스템 트레이 상주 (닫기 = 트레이로 최소화)

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

## 주요 구현 사항
- **Out-of-process VST 스캔**: `--scan` 모드로 자식 프로세스에서 스캔. 크래시 시 자동 재시도 (최대 5회), dead man's pedal로 불량 플러그인 자동 건너뜀. 하위 폴더 명시적 확장.
- **플러그인 체인 에디터**: 드래그 앤 드롭 순서 변경, Bypass 토글, Edit 버튼으로 네이티브 GUI 열기/닫기. DragAndDropContainer + DragAndDropTarget 구현.
- **시스템 트레이**: 닫기 → 트레이 최소화, 더블클릭 → 복원, 우클릭 → Show/Quit 메뉴.
- **모니터 출력**: Mono 모드에서 양쪽 출력 채널에 복사, OutputRouter 볼륨 적용.
- **단축키**: 기본 핫키에 읽기 쉬운 액션 이름 표시. Panic Mute (Ctrl+Shift+M), Input Mute (Ctrl+Shift+N).

## 코딩 규칙
- 오디오 콜백: 힙 할당 금지, 뮤텍스 금지
- 제어 → 오디오 통신: atomic flags 또는 lock-free queue
- GUI/제어는 같은 ActionDispatcher 사용 (통일)
- WebSocket/HTTP는 별도 스레드, 오디오와 독립
- UI 콜백 안에서 자기 자신 컴포넌트를 삭제할 수 있는 작업은 `juce::MessageManager::callAsync` 사용 (use-after-free 방지)
- KnownPluginList 등 non-copyable JUCE 객체는 XML 직렬화로 스레드 간 전달

## 모듈
- `core/` → IPC 라이브러리
- `host/` → JUCE 앱
  - `Audio/` → AudioEngine, VSTChain, OutputRouter, LatencyMonitor
  - `Control/` → Hotkey, MIDI, WebSocket, HTTP, ActionDispatcher
  - `IPC/` → SharedMemWriter
  - `UI/` → PluginChainEditor, PluginScanner, AudioSettings, OutputPanel, ControlSettingsPanel, LevelMeter, DirectPipeLookAndFeel
- `driver/` → Virtual Loop Mic 커널 드라이버
- `streamdeck-plugin/` → Stream Deck 플러그인 (선택)

## 개발 순서
Phase 0 (환경) → 1 (Core IPC) → 2 (오디오+VST) →
3 (드라이버) → 4 (외부 제어) → 5 (GUI) → 6 (안정화)

## 알려진 주의사항
- `ChildProcess::start()` 사용 시 커맨드라인 내 경로에 공백이 있으면 따옴표 필수
- VST 스캔 타임아웃 300초 (5분)
- `monitorEnabled_`는 `std::atomic<bool>` (스레드 안전)
- `channelMode_` 기본값 2 (Stereo)
- 플러그인 삭제 시 에디터 윈도우 자동 닫기 + editorWindows_ 벡터 정리
