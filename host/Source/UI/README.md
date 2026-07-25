# UI 모듈 — JUCE GUI 컴포넌트

> DirectPipe 호스트 애플리케이션의 모든 GUI 컴포넌트를 포함한다.
> 특별히 명시된 경우를 제외하고 모든 클래스는 `[Message thread]`에서 동작한다.

---

## 컴포넌트 계층 구조 (Component Hierarchy)

```
MainComponent                           ← 최상위 윈도우 컴포넌트
├── inputMeter_ (LevelMeter)            ← 입력 레벨 미터
├── outputMeter_ (LevelMeter)           ← 출력 레벨 미터
├── inputGainSlider_ (Slider)           ← 입력 게인 슬라이더
├── pluginChainEditor_ (PluginChainEditor)  ← VST 플러그인 체인 에디터
│   └── PluginRowComponent (내부)           ← 드래그 앤 드롭 행
├── presetSlotBar_ (PresetSlotBar)      ← A-E 프리셋 슬롯 버튼 (Auto는 별도 INPUT 영역 버튼)
├── notificationBar_ (NotificationBar)  ← 상태바 알림 (에러/경고/정보)
├── rightTabs_ (TabbedComponent)        ← 우측 탭 패널
│   ├── [Audio] audioSettings_ (AudioSettings)          ← 오디오 I/O 설정
│   ├── [Output] outputPanel_ (OutputPanel)             ← 모니터/IPC/녹음
│   ├── [Controls] controlSettingsPanel_ (ControlSettingsPanel)  ← 외부 제어 탭 컨테이너
│   │   ├── [Hotkeys] HotkeyTab         ← 단축키 바인딩
│   │   ├── [MIDI] MidiTab              ← MIDI 디바이스/매핑
│   │   └── [Stream Deck] StreamDeckTab ← WS/HTTP 서버 상태
│   └── [Settings] LogPanel             ← 로그 뷰어 + 유지보수
├── inputMuteBtn_                         ← 입력 전용 뮤트 버튼 (체인/출력 유지)
├── outputMuteBtn_ / monitorMuteBtn_ / vstMuteBtn_  ← 출력 경로 뮤트 인디케이터
├── panicMuteBtn_                       ← 패닉 뮤트 버튼
├── statusUpdater_ (StatusUpdater)      ← 30Hz 주기 상태 업데이트 (비-컴포넌트)
├── updateChecker_ (UpdateChecker)      ← 업데이트 확인 (비-컴포넌트)
└── 상태바 라벨 (latency/cpu/format)
```

---

## 파일 목록 (File List)

| 파일 | 설명 |
|------|------|
| `AudioSettings.h/cpp` | 통합 오디오 I/O 설정 패널 (드라이버/디바이스/SR/BS/채널 모드) |
| `ControlSettingsPanel.h/cpp` | Hotkey/MIDI/StreamDeck 서브탭을 감싸는 슬림 탭 컨테이너 (~75줄) |
| `DirectPipeLookAndFeel.h/cpp` | 다크 테마 룩앤필 + CJK 폰트 렌더링 (플랫폼별 폰트 선택) |
| `HotkeyTab.h/cpp` | 단축키 바인딩 탭 — 액션-키 매핑, [Set] 녹음, 드래그 앤 드롭 순서 변경 |
| `LevelMeter.h/cpp` | 실시간 오디오 레벨 미터 (RMS + 피크 홀드 + 클리핑) |
| `LogPanel.h/cpp` | 실시간 로그 뷰어 + 유지보수 기능 (백업/복원/팩토리 리셋) + DirectPipeLogger |
| `MidiTab.h/cpp` | MIDI 디바이스 선택 + CC/Note 매핑 + [Learn] 모드 + 3단계 플러그인 파라미터 팝업 |
| `NotificationBar.h/cpp` | 비침입 상태바 알림 (빨강/주황/보라), 자동 페이드 3-8초 |
| `OutputPanel.h/cpp` | 모니터/IPC/녹음 제어. 완료 파일 Play는 현재 선택 폴더로 한정하고 녹음 폴더 저장 실패 시 이전 폴더로 롤백 |
| `PluginChainEditor.h/cpp` | VST 플러그인 체인 에디터 — 추가/삭제/드래그 순서 변경/바이패스/네이티브 GUI |
| `PluginScanner.h/cpp` | Out-of-process VST 스캐너 다이얼로그 (디렉토리 관리, 프로그레스, 검색/정렬) |
| `PresetManager.h/cpp` | 프리셋 저장/로드 + 퀵 슬롯 A-E. 활성 슬롯 import는 파일군·runtime snapshot을 보존하고 성공 뒤 커밋, 실패 시 롤백 |
| `PresetSlotBar.h/cpp` | A-E 프리셋 슬롯 버튼 (5개). Auto 슬롯(index 5)은 별도 Auto 버튼과 연동되며 A-E 순환에서 제외. 우클릭 컨텍스트 메뉴 |
| `SettingsExporter.h/cpp` | 설정 내보내기/가져오기 — `.dpbackup` / `.dpfullbackup`, recording-folder 포함, 크로스-OS 및 부분 복원 오류 보고 |
| `StatusUpdater.h/cpp` | 30Hz 타이머 틱에서 UI 상태 업데이트 (뮤트/레이턴시/CPU/레벨/게인 동기화). 색상 체계: INPUT(녹색/빨강), OUT/MON/VST(녹색/사용자뮤트빨강/패닉잠금진빨강), PANIC(대기=빨강, 활성=녹색 `UNMUTE`) |
| `StreamDeckTab.h/cpp` | WebSocket/HTTP 서버 상태 표시 + Start/Stop 토글 |
| `UpdateChecker.h/cpp` | 백그라운드 GitHub 릴리스 확인 + Windows 인앱 업데이트. 반복 worker를 안전하게 reap하고 v4.2.0+ asset은 exact-name SHA-256을 fail-closed 검증 |
| `UpdateScript.h/cpp` | Windows 업데이트 설치 배치의 staging/교체/rollback 구문을 생성하는 상태 없는 테스트 가능 헬퍼 |
| `FilterEditPanel.h/cpp` | 내장 Filter 설정 패널 (AudioProcessorEditor). HPF/LPF 프리셋 + 커스텀 슬라이더 |
| `NoiseRemovalEditPanel.h/cpp` | 내장 Noise Removal 설정 패널. 강도 프리셋 (약/중/강) + VAD 임계값 |
| `AGCEditPanel.h/cpp` | 내장 Auto Gain 설정 패널. LUFS 타겟 슬라이더 + 실시간 측정 + 고급 설정 |

---

## 스레드 모델 (Thread Model)

| 클래스 | 스레드 | 비고 |
|--------|--------|------|
| `MainComponent` | `[Message thread]` | 30Hz Timer로 StatusUpdater/NotificationBar/LogPanel 틱 호출 |
| `AudioSettings` | `[Message thread]` | ChangeListener로 디바이스 변경 감지 |
| `OutputPanel` | `[Message thread]` | 내부 Timer로 모니터 상태 폴링 |
| `PluginChainEditor` | `[Message thread]` | 플러그인 추가는 callAsync + SafePointer |
| `PluginScanner` | `[BG thread]` | `juce::Thread` 상속. 스캔은 별도 스레드에서 실행, UI 업데이트는 callAsync + `alive_` 플래그 |
| `PresetManager` | `[Message thread]` | `loadSlotAsync`는 `VSTChain::replaceChainAsync` → BG 스레드 로드 후 callAsync 완료 |
| `PresetSlotBar` | `[Message thread]` | — |
| `StatusUpdater` | `[Message thread]` | MainComponent의 timerCallback에서 tick() 호출 |
| `UpdateChecker` | `[BG thread]` | `checkForUpdate()`는 GitHub API 폴링, `downloadThread_`는 다운로드/검증/설치 스크립트 준비. 결과는 callAsync로 메시지 스레드 전달. running/finished atomic과 `alive_` 수명 플래그에 더해 요청 generation을 확인하여 반복 검사 전에 큐에 남은 이전 결과를 폐기 |
| `LevelMeter` | `[RT thread]` → `[Message thread]` | `setLevel()`은 RT 스레드에서 호출 가능 (atomic write). `tick()/paint()`는 메시지 스레드 |
| `DirectPipeLogger` | 모든 스레드 | `logMessage()`는 어떤 스레드에서든 호출 가능 (mutex 보호). `drain()`은 메시지 스레드 전용 |
| `NotificationBar` | `[Message thread]` | — |
| `LogPanel` | `[Message thread]` | — |
| `HotkeyTab` | `[Message thread]` | 내부 Timer로 녹음 상태 폴링 |
| `MidiTab` | `[Message thread]` | 내부 Timer로 Learn 모드 폴링 |
| `StreamDeckTab` | `[Message thread]` | 내부 Timer로 서버 상태 폴링 |
| `SettingsExporter` | `[Message thread]` | 정적 메서드만 제공, async FileChooser 사용 |

---

## Resource Lifecycle

| 리소스 | 생성 | 소유 | 파괴 | 비고 |
|--------|------|------|------|------|
| `audioEngine_` | MainComponent 생성자 | MainComponent (stack) | MainComponent 소멸자 | 가장 먼저 초기화, 가장 나중에 파괴 |
| `dispatcher_` / `broadcaster_` | MainComponent 생성자 | MainComponent (stack) | MainComponent 소멸자 | alive_ 패턴으로 callAsync 보호 |
| `controlManager_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | HotkeyHandler, MidiHandler, WS, HTTP 소유 |
| `presetManager_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | 프리셋 JSON 파일 관리 |
| `presetSlotBar_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | loadingSlot_/partialLoad_ 공유 atomic 참조 |
| `actionHandler_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | engine_, presetMgr_, slotBar_ 참조 |
| `settingsAutosaver_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | 콜백: onPostLoad, onShowWindow 등 |
| `statusUpdater_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | 30Hz tick, UI 포인터 바인딩 |
| `updateChecker_` | MainComponent 생성자 | MainComponent (stack) | MainComponent 소멸자 | BG 스레드 GitHub API, alive_ 보호 |
| 탭 UI 패널들 (audioSettings_, outputPanel_, controlSettingsPanel_) | MainComponent 생성자 | rightTabs_ (release 후) | rightTabs_ 소멸자 | unique_ptr.release()로 TabbedComponent에 이전 |
| `pluginChainEditor_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | 탭 소유 아님 (좌측 체인 영역) |
| `outputPanelPtr_` | MainComponent 생성자 | SafePointer (비소유) | 자동 null (Component 삭제 시) | rightTabs_가 실제 소유 |

---

## Troubleshooting

| 증상 | 진단 경로 | 핵심 파일 |
|------|----------|----------|
| UI 업데이트 안 됨 | StatusUpdater::tick 호출 확인 → 캐시된 값 변경 확인 → setUI() 바인딩 확인 | `StatusUpdater.cpp` |
| 프리셋 슬롯 버튼 반응 없음 | loadingSlot_ 상태 → engine_.isMuted() (panic) → presetManager_ null 체크 | `PresetSlotBar.cpp` |
| 업데이트 알림 안 뜸 | updateChecker_ BG 스레드 → GitHub API 응답 → alive_ 체크 → callAsync | `UpdateChecker.cpp` |
| 플러그인 스캔 크래시 | Out-of-process 자식 프로세스 확인 → dead man's pedal → blacklist | `PluginScanner.cpp` |
| 설정 저장 안 됨 | settingsAutosaver_ dirty 플래그 → cooldown 카운터 → atomicWriteFile 성공 여부 | `SettingsAutosaver.cpp` |
| 백업 복원 실패 | 플랫폼 호환성 체크 → isPlatformCompatible() → JSON 파싱 에러 | `SettingsExporter.cpp` |

---

## DANGER ZONES

> 이 섹션을 수정할 때 CLAUDE.md의 Coding Rules와 해당 .h 파일의 Thread Ownership 어노테이션도 함께 업데이트할 것

### 1. UpdateChecker 백그라운드 스레드 수명 관리
- `updateCheckThread_`와 `downloadThread_`(Windows)는 `std::thread`로 실행된다.
- 소멸자에서 반드시 join해야 한다 (detach 금지).
- `std::thread::joinable()`은 worker 종료 후에도 true이므로 진행 상태로 사용하지 않는다. `downloadInProgress_`/`downloadThreadFinished_`로 구분하고 다음 시도 전에 완료 thread를 reap한다.
- callAsync 람다에서 `alive_` 플래그 (`shared_ptr<atomic<bool>>`)를 캡처하여 소멸 후 접근을 방지한다.
- **위반 시**: use-after-delete 크래시.
- v4.2.0 이상 릴리즈는 `checksums.sha256` 다운로드·exact asset entry·64자리
  SHA-256 중 하나라도 실패하면 설치를 진행하지 않는다. 이전 릴리즈에만
  checksum 없는 호환 경로를 허용한다.

### 2. PluginChainEditor 플러그인 추가/삭제 시 callAsync
- `addPluginFromDescription`은 `SafePointer`로 보호된 callAsync를 사용한다.
- Remove 버튼 콜백에서 자기 자신을 삭제할 수 있으므로 반드시 `callAsync`로 지연 실행해야 한다.
- **플러그인 이름은 반드시 `slot->name`에서 가져와야 한다** — `nameLabel_.getText()` 사용 금지 (비동기 콜백에서 UI 상태와 불일치 가능).

### 3. PresetManager 비동기 슬롯 로드
- `loadSlotAsync`는 `VSTChain::replaceChainAsync`를 사용한다.
- `loadingSlot_` atomic으로 동시 슬롯 전환을 방지한다.
- `partialLoad_` atomic은 불완전 로드 후 자동 저장을 방지한다.
- `asyncGeneration_` 카운터로 이전(stale) 콜백을 폐기한다.
- reset/clear/import 전에 `VSTChain::cancelPendingAsyncLoad()`로 진행 중 결과를
  무효화한다. 활성 슬롯 import는 원본 파일군과 runtime JSON을 유지하고 새
  chain load가 성공한 뒤에만 교체를 확정한다.

### 3a. OutputPanel 녹음 폴더와 Play 대상
- `lastCompletedFile_`은 현재 선택한 recording folder의 자식일 때만 Play
  대상으로 사용할 수 있다. 폴더 A에서 녹음 후 B로 바꿨다면 A 파일을 다시
  선택하지 않는다.
- recording folder 설정 파일 저장 실패 시 UI와 live folder 모두 이전 값으로
  롤백하고 사용자에게 알린다.

### 4. LevelMeter RT→Message 스레드 경계
- `setLevel()`은 RT 스레드에서 호출되며 `atomic<float>`에 쓴다.
- `tick()`/`paint()`는 메시지 스레드에서 읽는다.
- **atomic이 아닌 멤버(`displayLevel_`, `peakLevel_` 등)는 메시지 스레드 전용** — RT 스레드에서 접근 금지.

### 5. DirectPipeLogger 다중 프로듀서 안전성
- `logMessage()`는 모든 스레드에서 호출될 수 있다 (WS, HTTP, Audio 등).
- `writeMutex_`로 MPSC(Multi-Producer Single-Consumer) 링 버퍼를 보호한다.
- **VSTChain `chainLock_` 내부에서 `writeToLog` 호출 금지** — 락 순서 위반으로 데드락 발생.

### 6. PluginScanner Out-of-Process 실행
- `juce::Thread`를 상속하여 별도 스레드에서 스캔을 실행한다.
- 크래시 안전: 자식 프로세스(`--scan`)가 크래시해도 호스트는 영향받지 않는다.
- 스캔 결과는 캐시 파일에 저장되며, UI 업데이트는 callAsync로 전달한다.

---

## When to Update This README

- UI 컴포넌트 파일을 추가/삭제/이름 변경할 때 파일 목록(File List) 업데이트
- 컴포넌트 계층 구조가 변경될 때 (탭 추가/삭제, 부모-자식 관계 변경) 계층 트리 업데이트
- 스레드 모델이 변경될 때 (새 스레드 도입, atomic 추가, 락 변경) 스레드 모델 테이블 업데이트
- DANGER ZONE 관련 코드 수정 시 해당 항목 업데이트 + .h/.cpp WARNING 주석 확인
