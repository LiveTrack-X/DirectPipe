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
├── updateChecker_ (UpdateChecker)      ← 릴리스 확인 + 명시적 Receiver 유지보수 (비-컴포넌트)
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
| `LogPanel.h/cpp` | 실시간 로그 뷰어 + 유지보수 (백업/복원/팩토리 리셋, Windows `Update Receiver...`) + DirectPipeLogger |
| `MidiTab.h/cpp` | MIDI 디바이스 선택 + CC/Note 매핑 + [Learn] 모드 + 3단계 플러그인 파라미터 팝업 |
| `NotificationBar.h/cpp` | 비침입 상태바 알림 (빨강/주황/보라), 자동 페이드 3-8초 |
| `OutputPanel.h/cpp` | 모니터/IPC/녹음 제어. 완료 파일 Play는 현재 선택 폴더로 한정하고 녹음 폴더 저장 실패 시 이전 폴더로 롤백 |
| `PluginChainEditor.h/cpp` | VST 플러그인 체인 에디터 — 추가/삭제/드래그 순서 변경/바이패스/네이티브 GUI |
| `PluginScanner.h/cpp` | Out-of-process VST 스캐너 다이얼로그 (디렉토리 관리, 프로그레스, 검색/정렬) |
| `PresetManager.h/cpp` | 프리셋 저장/로드 + 퀵 슬롯 A-E. 활성 슬롯 import는 파일군·runtime snapshot을 보존하고 성공 뒤 커밋, 실패 시 롤백 |
| `PresetSlotBar.h/cpp` | A-E 프리셋 슬롯 버튼 (5개). Auto 슬롯(index 5)은 별도 Auto 버튼과 연동되며 A-E 순환에서 제외. 우클릭 컨텍스트 메뉴 |
| `SettingsExporter.h/cpp` | 설정 내보내기/가져오기 — `.dpbackup` / `.dpfullbackup`, recording-folder 포함, 크로스-OS 및 부분 복원 오류 보고 |
| `StatusUpdater.h/cpp` | 30Hz UI/WebSocket 상태 업데이트. main은 드라이버 입력·출력 지연(미보고 방향은 한 버퍼) + 활성 chain PDC. monitor는 입력 지연 + PDC + adaptive queue target + monitor 출력 장치 지연이며 main 출력 지연을 더하지 않음. 콜백 실행시간은 별도 CPU/XRun 지표 |
| `StreamDeckTab.h/cpp` | WebSocket/HTTP 서버 상태 표시 + Start/Stop 토글 |
| `UpdateChecker.h/cpp` | GitHub 공개 릴리스 확인, Windows Receiver 설치 경로 미리보기/사용 중 확인, 다운로드·설치 진행. 시작 시 같거나 낮은 host 릴리스는 알리지 않음. v4.2.0+ asset은 exact-name SHA-256을 fail-closed 검증 |
| `ReceiverUpdateSupport.h/cpp` | 표준/등록/선택한 VST 경로의 기존 Receiver 탐색, DLL 실행 없는 PE/제품/버전 검사, exact-file Restart Manager·접근 검사. 같은/상위 버전은 교체 대상에서 제외 |
| `UpdateScript.h/cpp` | Receiver 대상이 있는 PowerShell companion transaction 및 기존 host-only batch 경로 생성. staging, identity/hash/lock 재확인, 백업과 실패 시 rollback 지원 |
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
| `UpdateChecker` | `[Message]` + `[BG workers]` | 메시지 스레드에서 UI·유지보수 flow·installer launch/결과 polling. worker는 API 요청, Receiver 탐색/사용 확인, 다운로드/검증/스크립트 준비를 담당. 결과는 `alive_`로 보호한 callAsync, 릴리스 검사 결과는 요청 generation도 검증 |
| `LevelMeter` | `[Message thread]` (RT 입력도 지원) | 현재 StatusUpdater가 engine atomic level을 읽어 `setLevel()`에 전달. setter 자체는 atomic write여서 RT 호출 가능. `tick()/paint()`는 메시지 스레드 |
| `DirectPipeLogger` | `[Non-RT threads]` | `logMessage()`는 mutex 보호와 파일 쓰기를 사용하므로 audio RT hot path에서는 호출하지 않음. `drain()`은 메시지 스레드 전용 |
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
| `updateChecker_` | MainComponent 생성자 | MainComponent (stack) | MainComponent 소멸자 | alive_ 무효화, timer 중지, 검사/탐색/다운로드 worker join. Receiver-only child handle은 종료 시 닫지만 installer 자체를 kill하지 않음 |
| 탭 UI 패널들 (audioSettings_, outputPanel_, controlSettingsPanel_) | MainComponent 생성자 | rightTabs_ (release 후) | rightTabs_ 소멸자 | unique_ptr.release()로 TabbedComponent에 이전 |
| `pluginChainEditor_` | MainComponent 생성자 | MainComponent (unique_ptr) | MainComponent 소멸자 | 탭 소유 아님 (좌측 체인 영역) |
| `outputPanelPtr_` | MainComponent 생성자 | SafePointer (비소유) | 자동 null (Component 삭제 시) | rightTabs_가 실제 소유 |

---

## Troubleshooting

| 증상 | 진단 경로 | 핵심 파일 |
|------|----------|----------|
| UI 업데이트 안 됨 | StatusUpdater::tick 호출 확인 → 캐시된 값 변경 확인 → setUI() 바인딩 확인 | `StatusUpdater.cpp` |
| 프리셋 슬롯 버튼 반응 없음 | loadingSlot_ 상태 → engine_.isMuted() (panic) → presetManager_ null 체크 | `PresetSlotBar.cpp` |
| 업데이트 알림 안 뜸 | host가 공개 릴리스와 같거나 앞서면 정상적으로 조용함. 그 외 API 상태 → alive_/generation → callAsync 확인 | `UpdateChecker.cpp` |
| 오래된 Receiver가 검색 안 됨 | Settings → Update Receiver... → Choose Folder... → 정확한 DLL/bundle 이름·제품·버전·경로 권한 확인. 탐색 경고도 확인 | `ReceiverUpdateSupport.cpp` |
| Receiver 사용 중으로 업데이트 중단 | 표시된 OBS/오디오 앱을 직접 종료 → Retry. Later는 보류. 확인 불가 경로도 자동 덮어쓰지 않음 | `UpdateChecker.cpp` |
| 플러그인 스캔 크래시 | Out-of-process 자식 프로세스 확인 → dead man's pedal → blacklist | `PluginScanner.cpp` |
| 설정 저장 안 됨 | settingsAutosaver_ dirty 플래그 → cooldown 카운터 → atomicWriteFile 성공 여부 | `SettingsAutosaver.cpp` |
| 백업 복원 실패 | 플랫폼 호환성 체크 → isPlatformCompatible() → JSON 파싱 에러 | `SettingsExporter.cpp` |

---

## DANGER ZONES

> 이 섹션을 수정할 때 CLAUDE.md의 Coding Rules와 해당 .h 파일의 Thread Ownership 어노테이션도 함께 업데이트할 것

### 1. UpdateChecker 백그라운드 스레드 수명 관리
- `updateCheckThread_`, Windows의 `receiverInspectionThread_`와 `downloadThread_`는 `std::thread`로 실행된다.
- 소멸자에서 반드시 join해야 한다 (detach 금지).
- `std::thread::joinable()`은 worker 종료 후에도 true이므로 진행 상태로 사용하지 않는다. `downloadInProgress_`/`downloadThreadFinished_`로 구분하고 다음 시도 전에 완료 thread를 reap한다.
- callAsync 람다에서 `alive_` 플래그 (`shared_ptr<atomic<bool>>`)를 캡처하여 소멸 후 접근을 방지한다.
- Receiver 유지보수의 UI/flow 상태는 message thread 전용이다. 탐색 및 Restart Manager 검사는
  worker에서 수행하고, 앞선 inspection의 I/O 완료 후 결과를 받은 경로에서 thread를 reap한다.
- **위반 시**: use-after-delete 크래시.
- v4.2.0 이상 릴리즈는 `checksums.sha256` 다운로드·exact asset entry·64자리
  SHA-256 중 하나라도 실패하면 설치를 진행하지 않는다. 이전 릴리즈에만
  checksum 없는 호환 경로를 허용한다.

### 1a. Receiver 업데이트 계약 (4.4.0에도 유지)
- 시작 시 host 버전만 비교한다. `Settings > Update Receiver...`를 명시적으로 눌러야
  host가 최신이거나 공개 릴리스보다 앞선 경우에도 오래된 Receiver를 별도 조사한다.
- 기본 VST2/VST3, 사용자 VST3, HKCU/HKLM의 32/64-bit VST2 등록 경로를 조사한다.
  `Choose Folder...`는 추가 탐색 루트이며 새 설치 위치를 만드는 기능이 아니다.
  depth 6/20,000 visit 한도, linked 경로 및 build/dist/backup 제외를 적용한다.
- 정확한 이름과 Windows x64 PE export, `DirectPipe Receiver` 제품명, 엄격한 동일
  file/product version이 확인된 기존 파일만 대상으로 한다. DLL을 실행하지 않으며,
  이 검사는 코드 서명이나 모든 사용자 설치 사본의 발견을 보장하지 않는다.
- 미리보기는 발견한 실제 경로와 버전을 표시한다. 같은/상위 버전은 보존한다. 사용 중인
  앱은 사용자가 직접 닫고 `Retry`하거나 `Later`로 보류한다. 자동 종료/kill은 하지 않는다.
- Receiver 포함 업데이트는 UUID 임시 폴더의 검증된 ZIP과 PowerShell helper를 사용한다.
  helper가 package hash, staged 제품/버전/포맷 및 기존 대상의 identity/hash/use를 재확인한다.
  Receiver와 host 어느 목적지든 권한이 필요하면 UAC를 요청하며, 실행 취소 시 host는 유지된다.
- VST3는 bundle 전체를 교체한다. Windows 폴더 rename에는 해당 bundle handle을 잠시
  해제한 뒤 재확보·hash 검증하는 구간이 있다. 오류 시 역순 rollback을 시도하고 백업을
  유지하며, `result.json`이 자동 복구 실패/수동 복구 필요를 구분한다. 여러 파일의 교체가
  원자적으로 완료된다는 보장은 아니다.
- Receiver-only는 DirectPipe 실행·설정·host 바이너리를 유지하고 결과를 polling한다.
  host 동반 업데이트는 helper 실행에 성공한 뒤 host 종료를 요청하고, helper가 해당 PID
  종료를 기다려 교체 후 desktop shell을 통해 재실행한다. 재실행 실패는 수동 시작 안내다.
  eligible Receiver가 없는 host-only 업데이트는 기존 batch 경로를 쓴다.
- 설치 성공은 파일 교체 결과다. OBS 재로딩, 실제 UAC 대화상자, 오디오 장치·청취 검증은 별도다.

### 2. PluginChainEditor 플러그인 추가/삭제 시 callAsync
- `addPluginFromDescription`은 `SafePointer`로 보호된 callAsync를 사용한다.
- Remove 버튼 콜백에서 자기 자신을 삭제할 수 있으므로 반드시 `callAsync`로 지연 실행해야 한다.
- **플러그인 이름은 반드시 `slot->name`에서 가져와야 한다** — `nameLabel_.getText()` 사용 금지 (비동기 콜백에서 UI 상태와 불일치 가능).

### 3. PresetManager 비동기 슬롯 로드
- `loadSlotAsync`는 동일 구조의 동기 state restore, preload cache, BG prepare/commit을 선택한다.
- `loadingSlot_` atomic으로 동시 슬롯 전환을 방지한다.
- `partialLoad_` atomic은 불완전 로드 후 자동 저장을 방지한다.
- `asyncGeneration_` 카운터로 이전(stale) 콜백을 폐기한다.
- reset/clear/import 전에 `VSTChain::cancelPendingAsyncLoad()`로 진행 중 결과를
  무효화한다. 활성 슬롯 import는 원본 파일군과 runtime JSON을 유지하고 새
  chain load가 성공한 뒤에만 교체를 확정한다.
- fast restore와 staged swap은 `ScopedProcessingSuspension`으로 admission을 닫고 현재
  render를 drain한다. nested rebuild가 외부 scope를 먼저 재개할 수 없고, 새 RT 콜백은
  기다리지 않고 무음 처리한다. preload가 있어도 state/commit 시간과 청감 연속성은 보장하지 않는다.

### 3a. OutputPanel 녹음 폴더와 Play 대상
- `lastCompletedFile_`은 현재 선택한 recording folder의 자식일 때만 Play
  대상으로 사용할 수 있다. 폴더 A에서 녹음 후 B로 바꿨다면 A 파일을 다시
  선택하지 않는다.
- recording folder 설정 파일 저장 실패 시 UI와 live folder 모두 이전 값으로
  롤백하고 사용자에게 알린다.

### 4. LevelMeter RT→Message 스레드 경계
- `setLevel()`은 `atomic<float>`에 쓰는 RT-safe setter다. 현재 호스트는 StatusUpdater의
  message-thread tick에서 engine level을 읽어 이 setter에 전달한다.
- `tick()`/`paint()`는 메시지 스레드에서 읽는다.
- **atomic이 아닌 멤버(`displayLevel_`, `peakLevel_` 등)는 메시지 스레드 전용** — RT 스레드에서 접근 금지.

### 5. DirectPipeLogger 다중 프로듀서 안전성
- `logMessage()`는 WS/HTTP/MIDI/device/control 등 여러 producer를 받지만 mutex와 파일 I/O를
  포함하므로 audio RT hot path에서 호출하면 안 된다.
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
