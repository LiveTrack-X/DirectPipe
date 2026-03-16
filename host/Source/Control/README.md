# Control 모듈

외부 제어 입력(단축키, MIDI, WebSocket, HTTP)을 통합 관리하고, ActionDispatcher를 통해 오디오 엔진/프리셋/UI에 전달하는 모듈.
모든 제어 액션은 ActionDispatcher의 메시지 스레드 보장을 통해 일관되게 처리된다.

---

## Data Flow

```
External Input Sources
 |
 +-- HotkeyHandler     [Message thread]    RegisterHotKey(Win) / CGEventTap(Mac)
 |     WM_HOTKEY / CGEvent
 |
 +-- MidiHandler        [MIDI callback thread]  JUCE MidiInputCallback
 |     CC / Note On/Off
 |
 +-- WebSocketServer    [Server thread]    RFC 6455, port 8765
 |     JSON text frames
 |
 +-- HttpApiServer      [Server thread]    REST GET, port 8766
 |     HTTP GET /api/*
 |
 v
ControlManager (orchestrator)
 |  - initialize/shutdown lifecycle
 |  - ControlMappingStore (JSON persistence)
 |  - ControlConfig (hotkeys + MIDI + server)
 |
 v
ActionDispatcher.dispatch(ActionEvent)
 |  - Any thread -> Message thread delivery guarantee
 |  - Message thread: synchronous delivery (zero latency)
 |  - Other threads: callAsync deferred delivery
 |
 v
ActionHandler.handle(ActionEvent)
 |  - Panic mute check (blocks most actions during panic)
 |  - Routes to:
 |
 +---> AudioEngine       (bypass, gain, mute, IPC, monitor, recording)
 +---> PresetManager      (load preset, switch slot, next/prev)
 +---> PresetSlotBar      (slot switching coordination)
 +---> UI callbacks       (onDirty, onNotification, onPanicStateChanged, ...)
 |
 v
StateBroadcaster.updateState()
 |  - AppState snapshot (plugins, gain, mute, preset, latency, ...)
 |  - Message thread notification guarantee
 |
 +---> WebSocketServer    (JSON state push to Stream Deck)
 +---> GUI                (repaint)
 +---> MIDI feedback      (LED controllers)
```

---

## File List

| 파일 | 설명 |
|------|------|
| `ControlManager.h/cpp` | 전체 제어 서브시스템의 최상위 관리자. Hotkey/MIDI/WS/HTTP 핸들러 소유 및 수명 관리 |
| `ActionDispatcher.h/cpp` | 통합 액션 인터페이스. 모든 제어 소스 -> 메시지 스레드 리스너 전달. `Action` enum 및 `ActionEvent` 정의 |
| `ActionHandler.h/cpp` | ActionEvent 라우팅. Panic mute 로직 통합 (`doPanicMute`). AudioEngine/PresetManager/UI 콜백 연결 |
| `ControlMapping.h/cpp` | 단축키/MIDI/서버 매핑의 JSON 직렬화/역직렬화. `ControlConfig` 구조체. Portable 모드 지원 |
| `SettingsAutosaver.h/cpp` | Dirty-flag 패턴 + 1초 디바운스 자동 저장. `markDirty()` / `tick()` / `saveNow()` |
| `HotkeyHandler.h/cpp` | 글로벌 키보드 단축키. Windows: `RegisterHotKey` + 메시지 창. macOS: `CGEventTap`. Linux: stub |
| `MidiHandler.h/cpp` | MIDI CC/Note 매핑 및 Learn 모드. 핫플러그 감지. LED 피드백. `bindingsMutex_`로 바인딩 보호 |
| `WebSocketServer.h/cpp` | RFC 6455 WebSocket 서버 (port 8765). Stream Deck 연동. UDP 디스커버리 (port 8767). 전용 broadcast 스레드 |
| `HttpApiServer.h/cpp` | REST API 서버 (port 8766). GET-only. CORS/OPTIONS 지원. 상태 코드 404/405/400. 엔드포인트: volume, preset, gain, bypass, recording, ipc, panic, plugin param, **plugins (목록)**, **plugin/:idx/params**, **perf (성능)**, **xrun/reset**, **auto/add** |
| `StateBroadcaster.h/cpp` | 앱 상태 스냅샷 (`AppState`) 관리 및 브로드캐스트. 메시지 스레드 리스너 전달 보장 |
| `Log.h/cpp` | 구조화 로깅 헬퍼. severity 레벨 (info/warn/error/audit), 카테고리 태그, RAII 타이머, 세션 헤더 |

---

## Thread Model

| 클래스 | 메서드/영역 | 스레드 | 비고 |
|--------|-------------|--------|------|
| ControlManager | `initialize`, `shutdown`, `applyConfig` | `[Message thread]` | 모든 핸들러의 수명 관리 |
| ActionDispatcher | `dispatch` | Any thread | 메시지 스레드면 동기, 아니면 callAsync |
| ActionDispatcher | listener notification | `[Message thread]` | callAsync 사용 시 `alive_` 플래그 체크 |
| ActionHandler | `handle` | `[Message thread]` | ActionDispatcher가 메시지 스레드 전달 보장 |
| ActionHandler | `doPanicMute` | `[Message thread]` | pre-mute state 저장/복원 |
| HotkeyHandler | `timerCallback` (WM_HOTKEY polling) | `[Message thread]` | JUCE Timer. Windows: PeekMessage 루프 |
| HotkeyHandler | `registerHotkey`, `unregisterAll` | `[Message thread]` | OS API 호출 |
| HotkeyHandler | `macHandleKeyDown` | `[Message thread]` | macOS CGEventTap 콜백 (메인 런루프) |
| MidiHandler | `handleIncomingMidiMessage` | `[MIDI callback thread]` | JUCE MidiInput 콜백. `bindingsMutex_` 보호 |
| MidiHandler | Learn callback (learnCallback_) | `[MIDI callback thread]` → **반드시 Message thread로 디스패치** | Learn 콜백 내에서 addBinding/removeBinding 호출 시 Message thread에서만 실행. MIDI callback에서 직접 호출 시 UI의 removeBinding과 bindings_ 동시 변경 → 크래시 |
| MidiHandler | `processCC`, `processNote` | `[MIDI callback thread]` | dispatch는 lock 밖에서 (데드락 방지) |
| MidiHandler | `addBinding`, `removeBinding`, `loadFromMappings` | `[Message thread]` | `bindingsMutex_` 보호. **MIDI callback thread에서 직접 호출 금지** |
| MidiHandler | `getBindings` | Any thread | `bindingsMutex_` 잠금 후 복사본 반환 |
| WebSocketServer | `serverThread` | `[Server thread]` | accept 루프 |
| WebSocketServer | `clientThread` | `[Server thread]` (per-client) | 각 클라이언트 전용 스레드. `sendMutex`로 송신 보호 |
| WebSocketServer | `processMessage` | `[Server thread]` | JSON 파싱 -> ActionDispatcher.dispatch |
| WebSocketServer | `broadcastThreadFunc` | `[BG thread]` | 전용 broadcast 스레드. `broadcastMutex_` + CV |
| WebSocketServer | `onStateChanged` | `[Message thread]` | StateBroadcaster 콜백 -> broadcast 큐에 push |
| HttpApiServer | `serverThread` | `[Server thread]` | accept 루프 |
| HttpApiServer | `handleClient` | `[Server thread]` (per-client) | 요청 파싱 -> ActionDispatcher.dispatch |
| StateBroadcaster | `updateState` | Any thread | `stateMutex_` 보호. 메시지 스레드면 동기 통지, 아니면 callAsync |
| StateBroadcaster | `notifyListeners` | `[Message thread]` | copy-before-iterate (재진입 안전) |
| SettingsAutosaver | `markDirty`, `tick`, `saveNow`, `loadFromFile` | `[Message thread]` | MainComponent 타이머에서 호출 |
| Log (all methods) | `info`, `warn`, `error`, `audit` | Any thread | JUCE Logger 내부 `writeMutex_` |

---

## Resource Lifecycle

| 리소스 | 생성 | 소유 | 파괴 | 비고 |
|--------|------|------|------|------|
| `ControlManager` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | 모든 제어 핸들러의 최상위 소유자 |
| `ActionDispatcher` | MainComponent 생성자 | MainComponent (stack) | MainComponent 소멸자 | alive_ 패턴으로 callAsync 보호 |
| `StateBroadcaster` | MainComponent 생성자 | MainComponent (stack) | MainComponent 소멸자 | alive_ 패턴 |
| `HotkeyHandler` | ControlManager::initialize | ControlManager (stack) | ControlManager::shutdown | Windows: message-only HWND. macOS: CGEventTap |
| `MidiHandler` | ControlManager::initialize | ControlManager (stack) | ControlManager::shutdown | MIDI 디바이스 핸들, learnTimer_ |
| `learnTimer_` (MidiHandler) | MidiHandler::startLearn | MidiHandler (unique_ptr) | stopLearn() / 소멸자 | JUCE Timer — Message thread에서만 파괴 가능 |
| `WebSocketServer` | ControlManager::initialize | ControlManager (unique_ptr) | ControlManager::shutdown | serverThread_, broadcastThread_, clientThreads |
| `ClientConnection` (WS) | serverThread accept | clients_ 벡터 (unique_ptr) | broadcastToClients sweep 또는 stop() | socket + thread + sendMutex. clientThread 종료 시 socket->close() 필수 |
| `HttpApiServer` | ControlManager::initialize | ControlManager (unique_ptr) | ControlManager::shutdown | serverThread_, handlerThreads_ |
| `HandlerThread` (HTTP) | serverThread accept | handlerThreads_ 벡터 | stop() 또는 done sweep | running_ 체크 후에만 삽입 (TOCTOU 방지) |
| `ActionHandler` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | engine_, presetMgr_ 참조 보유 |
| `SettingsAutosaver` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | dirty flag + cooldown 타이머 |

---

## State Flow Diagrams

### WebSocket Connection Lifecycle

```
[Accept] --serverThread--> [Handshake]
    |                          |
    |  clients_ 벡터에 추가      | SHA-1 challenge/response
    |  (clientsMutex_ 보호)     | Sec-WebSocket-Accept 헤더
    |                          v
    |                     [Connected]
    |                          |
    |  clientThread 시작        | readFrame 루프
    |                          | - Text(0x1): processMessage -> ActionDispatcher
    |                          | - Ping(0x9): sendMutex 잡고 Pong 응답
    |                          | - Close(0x8): sendMutex 잡고 Close 응답
    |                          | - Error(0xFF): 즉시 종료
    |                          v
    |                     [Disconnecting]
    |                          |
    |  conn->socket->close()   | clientCount_--
    |  (broadcastToClients     |
    |   sweep에서 감지)         v
    |                     [Dead -> Join]
    |                          |
    v                          | thread.join() (clientsMutex_ 바깥에서!)
[Cleaned Up]  <-----------------
```

### Action Dispatch Flow

```
[External Input]
    |
    |  Hotkey: WM_HOTKEY / CGEventTap
    |  MIDI: handleIncomingMidiMessage (MIDI callback thread)
    |  WebSocket: processMessage (server thread)
    |  HTTP: handleClient (server thread)
    |
    v
ActionDispatcher::dispatch(event)
    |
    |  isThisTheMessageThread?
    |  +-- YES -> dispatchOnMessageThread(event) [동기]
    |  +-- NO  -> callAsync + alive_ 체크 [비동기]
    |
    v
dispatchOnMessageThread(event)
    |
    |  listeners_ 복사 (copy-before-iterate)
    |  listenerMutex_ 해제 후 순회
    |
    v
MainComponent::onAction(event)
    |
    v
ActionHandler::handle(event)
    |
    |  engine_.isMuted()? -> 차단 (PanicMute/InputMuteToggle 제외)
    |
    +-- PluginBypass -> VSTChain::togglePluginBypassed
    +-- SetVolume -> OutputRouter::setVolume
    +-- LoadPreset -> PresetSlotBar::onSlotClicked
    +-- PanicMute -> doPanicMute(toggle)
    +-- RecordingToggle -> AudioRecorder::start/stop
    +-- ... (15개 액션)
```

---

## Troubleshooting

| 증상 | 진단 경로 | 핵심 파일:위치 |
|------|----------|--------------|
| WS 클라이언트 간헐적 끊김 | clientThread 소켓 close 여부 -> broadcastToClients sweep 로직 -> sendFrame 실패 | `WebSocketServer.cpp:clientThread`, `broadcastToClients` |
| WS 브로드캐스트 지연/행 | clientsMutex_ 보유 중 blocking write 확인 -> 느린 클라이언트 감지 | `WebSocketServer.cpp:broadcastToClients` |
| 셧다운 시 행 | stop()이 clientsMutex_ 대기 -> broadcastToClients가 blocking write 중 | `WebSocketServer.cpp:stop()` |
| 단축키 작동 안 함 | HotkeyHandler 등록 확인 -> Panic mute 상태 -> ActionHandler isMuted() 체크 | `HotkeyHandler.cpp`, `ActionHandler.cpp:handle` |
| MIDI Learn 반응 없음 | learning_ atomic 상태 -> bindingsMutex_ 교착 -> learnTimer_ 타임아웃 | `MidiHandler.cpp:handleIncomingMidiMessage` |
| HTTP API 404 | 엔드포인트 매칭 -> processRequest URL 파싱 | `HttpApiServer.cpp:processRequest` |
| 액션이 무시됨 | Panic mute 활성화 확인 -> engine_.isMuted() -> ActionHandler의 차단 로직 | `ActionHandler.cpp:handle` |
| 상태 브로드캐스트 누락 | StateBroadcaster hash 비교 -> updateState 호출 여부 -> WebSocket 연결 상태 | `StateBroadcaster.cpp:updateState` |

---

## DANGER ZONES

> 이 섹션을 수정할 때 CLAUDE.md의 Coding Rules와 해당 .h 파일의 Thread Ownership 어노테이션도 함께 업데이트할 것

1. **MidiHandler `bindingsMutex_` 안에서 dispatch 금지**: `processCC`/`processNote`에서 바인딩 검색은 lock 안에서, `dispatcher_.dispatch()`는 lock 밖에서 호출해야 함. lock 안에서 dispatch하면 ActionHandler -> MidiHandler 역참조로 데드락.

2. **ActionDispatcher/StateBroadcaster의 `alive_` 패턴 필수**: callAsync 람다에서 `this` 접근 전에 `alive_` 체크. 소멸자에서 `alive_->store(false)` 호출. 체크 없으면 use-after-free.

3. **ActionDispatcher/StateBroadcaster listener 순회 시 copy-before-iterate**: 리스너 콜백이 add/remove를 호출할 수 있으므로, 순회 전 리스너 목록을 복사. iterator invalidation 방지.

4. **WebSocket `broadcastToClients` 스레드 join은 `clientsMutex_` 밖에서**: `clientsMutex_` 잡은 상태에서 broadcast 스레드 join하면, broadcast 스레드가 `clientsMutex_`를 기다리며 데드락. 또한 per-connection `sendMutex`로 동시 쓰기 방지 (broadcastToClients + clientThread pong/close 동시 전송 시 프레임 손상). `broadcastToClients`는 `clientsMutex_` 보유 중 blocking `sendFrame`을 호출하므로, 느린 클라이언트가 전체 브로드캐스트와 셧다운을 지연시킬 수 있음.

4a. **WebSocket 소켓 수명 관리**: `clientThread`가 종료될 때 `conn->socket->close()`를 명시적 호출해야 `broadcastToClients`의 dead-client sweep(`isConnected()` 체크)이 즉시 감지함. 미호출 시 sweep이 dead client를 놓치고 실패한 write가 간헐적 연결 끊김으로 나타남. `clientCount_`는 소켓 close 후 decrement.

5. **Panic mute 중 액션 차단**: `ActionHandler::handle()`에서 `engine_.isMuted()` 체크. PanicMute/InputMuteToggle만 통과, 나머지 모든 액션(PluginBypass, LoadPreset, RecordingToggle 등) 차단. 새 Action 추가 시 이 체크 누락하면 panic mute 우회.

6. **SettingsAutosaver `loadFromFile`에서 `triggerPreload` callAsync 래핑**: 오디오 디바이스가 완전히 시작된 후에 프리로드 시작. callAsync 없이 직접 호출하면 prepareToPlay 전에 플러그인 로딩 시도.

7. **HttpApiServer CORS preflight**: OPTIONS 요청에 적절한 헤더 없이 응답하면 브라우저 클라이언트(Stream Deck Property Inspector 등)에서 요청 차단.

8. **HotkeyHandler recording 모드**: 녹음 중 다음 키 입력을 캡처. `stopRecording()` 호출로 취소. 취소 없이 모드가 남으면 실제 단축키가 무시됨.

9. **Log의 `writeMutex_`와 다른 lock 순서**: VSTChain `chainLock_` 안에서 `writeToLog` 호출 금지 (lock ordering). 다른 모듈에서도 자체 lock 안에서 로깅 시 같은 문제 주의.

10. **HttpApiServer gain delta 10x 스케일링**: `/api/gain/:delta` 엔드포인트는 delta에 10을 곱함. ActionHandler의 `InputGainAdjust`가 `*0.1f` 스텝 설계이므로, HTTP에서 직접 전달하면 1/10 크기로 적용됨.

11. **JUCE `StreamingSocket::write` 블로킹**: `write()`는 커널 send 버퍼가 가득 차면 무한 블로킹 가능. 타임아웃 없음. `broadcastToClients`에서 `clientsMutex_` 보유 중 write 호출 시, 느린 클라이언트가 전체 서버를 블로킹. 셧다운 시 `stop()`이 `clientsMutex_` 대기 → 무한 행.

12. **JUCE `StreamingSocket::isConnected` 한계**: TCP 연결의 OS 레벨 상태만 반영. 상대방이 `close()` 후에도 로컬에서 `isConnected()=true` 잠시 유지 가능 (TCP TIME_WAIT). `clientThread` 종료 시 명시적 `socket->close()` 호출 필수 — 이것이 `broadcastToClients` sweep의 dead client 감지 트리거.

13. **JUCE `InterProcessLock` POSIX 동작**: POSIX에서는 `fcntl`/`flock` 기반 파일 락 사용. 같은 프로세스 내에서 재진입(re-entrant) 동작은 OS/파일시스템에 따라 다름. `acquireExternalControlPriority()` 재호출 시 이전 락과 충돌 가능.

---

## When to Update This README

- Control/ 디렉토리에 파일을 추가하거나 삭제할 때 File List 업데이트
- 새 Action enum 값을 추가할 때 Data Flow 다이어그램과 ActionHandler 관련 설명 업데이트
- 스레드 모델이 변경될 때 (새 lock 추가, 콜백 스레드 변경, atomic 추가) Thread Model 테이블 업데이트
- 새로운 안전 규칙이 발견되거나 기존 DANGER ZONE이 변경될 때 해당 항목 업데이트
- 새 외부 제어 서버 추가 시 → CLAUDE.md "새 외부 제어 서버/핸들러 추가하기" Recipe 참조. ControlManager 등록 + 이 README의 Data Flow/Thread Model/DANGER ZONES 모두 업데이트 필수
