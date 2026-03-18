# DirectPipe v4 — Bug Audit Round 2: Fix Plan

> **Date**: 2026-03-18
> **Scope**: 2차 코드 진단 — 5개 기능별 에이전트가 실행 경로 추적으로 발견한 이슈 18건
> **Target Version**: v4.0.1 패치 릴리스 (1차 수정분에 추가)

---

## 요약

5개 전문 에이전트(Audio Pipeline / Preset·Plugin / External Control / UI State Machine / Cross-platform·IPC)가 실제 실행 루프를 추적하여 발견한 이슈 중, 오탐 1건을 제거하고 **CRITICAL 3건 + HIGH 8건 + MEDIUM 7건** 총 18건을 확인했다.

### 확인된 오탐

| 보고 | 기각 사유 |
|---|---|
| VSTChain::prepareToPlay suspendProcessing 카운터 불균형 | JUCE 7.0.12의 `suspendProcessing`은 **bool**이지 카운터가 아님. `true→true→false` = 최종 `false` (정상) |

---

## Phase 1: CRITICAL

### C1 — LoadLibraryA를 RT 오디오 콜백에서 호출

**파일**: `AudioEngine.cpp:904-919`
**카테고리**: RT 안전 위반

`LoadLibraryA("avrt.dll")`는 힙 할당 + OS 로더 락을 수반한다. `mmcssRegistered_` 플래그로 1회만 실행되지만, 장치 시작 후 **매번 첫 콜백**에서 발생한다 (`audioDeviceAboutToStart`에서 플래그 리셋).

**수정**: `audioDeviceAboutToStart`에서 DLL 로드 + 함수 포인터 캐싱. RT 콜백에서는 캐싱된 포인터만 사용.

**영향**: `AudioEngine.h` (캐시 멤버 추가) + `AudioEngine.cpp` (로직 이동)

---

### C2 — MidiHandler LearnTimeout 자기 파괴 (use-after-free)

**파일**: `MidiHandler.cpp:139-142`
**카테고리**: 메모리 안전

`LearnTimeout::timerCallback()`이 `handler.stopLearn()`을 호출 → `learnTimer_.reset()`이 현재 실행 중인 `LearnTimeout` 객체를 파괴 → 돌아와서 `stopTimer()` 호출 시 `this`가 이미 해제됨.

**수정**: timerCallback 내에서 `stopLearn()` 대신 직접 상태를 정리하고, `callAsync`로 `learnTimer_.reset()` 예약.

**영향**: `MidiHandler.cpp` (timerCallback 본문만)

---

### C3 — Reset Auto에 loadingSlot_ 가드 없음

**파일**: `MainComponent.cpp:795-826`
**카테고리**: 상태 관리

우클릭 → "Reset Auto to Defaults"에서 `loadingSlot_`을 설정하지 않고 removePlugin 루프 실행. 각 removePlugin이 `onChainChanged` → `onChainModified` → `saveSlot(activeSlot)` 트리거. 중간 상태(플러그인 1-2개만 제거됨)가 auto-save로 디스크에 저장됨.

**수정**: 루프 전에 `loadingSlot_ = true`, 완료/실패 후 해제.

**영향**: `MainComponent.cpp` (Reset Auto 핸들러만)

---

## Phase 2: HIGH

### H1 — audioDeviceStopped가 recorder를 중지하지 않음

**파일**: `AudioEngine.cpp:1253-1273`

`audioDeviceStopped`에서 VSTChain, OutputRouter, SharedMemWriter는 shutdown하지만 `recorder_.stopRecording()` 누락. 장치 에러 후 녹음이 잘못된 SR로 계속됨.

**수정**: `recorder_.stopRecording()` 추가.

---

### H2 — addAutoProcessors 부분 실패 시 partialLoad_ 미설정

**파일**: `MainComponent.cpp:184-190`

Auto 첫 클릭에서 `addAutoProcessors()`가 실패하면 `loadingSlot_ = false`만 하고 `partialLoad_`은 설정하지 않음. auto-saver가 불완전한 체인을 이전 슬롯에 저장.

**수정**: 실패 시 `partialLoad_ = true` 설정.

---

### H3 — pendingSlot_ 리셋 안 됨

**파일**: `PresetSlotBar.h`, `MainComponent.cpp:271-303`

Factory Reset이나 Clear Presets 후 `pendingSlot_`이 -1로 리셋되지 않음. 다음 슬롯 로드 완료 시 stale 슬롯이 자동 발동.

**수정**: `onPresetsCleared`/`onResetSettings`에서 `presetSlotBar_->resetPendingSlot()` 호출.

---

### H4 — quickStateHash에 plugin name 미포함

**파일**: `StateBroadcaster.cpp:77-83`

`quickStateHash`가 `p.name`을 해시에 포함하지 않음. 플러그인 이름 변경이 WebSocket 브로드캐스트를 트리거하지 않음.

**수정**: 해시 루프에 `p.name` 추가.

---

### H5 — broadcastToClients가 clientsMutex_ 보유 중 socket write

**파일**: `WebSocketServer.cpp:521-543`

`clientsMutex_`를 잡은 채로 모든 클라이언트에 `sendFrame`. 느린 클라이언트가 전체 브로드캐스트를 블로킹하고, `stop()`도 `clientsMutex_` 대기 → 셧다운 행.

**수정**: 락 안에서 클라이언트 목록 스냅샷만 복사, 락 해제 후 per-connection `sendMutex`로 전송.

---

### H6 — audioDeviceAboutToStart가 Message-thread-only 변수 직접 쓰기

**파일**: `AudioEngine.cpp:1070,1141,1150-1157`

device thread에서 `desiredInputDevice_`, `desiredOutputDevice_`, `reconnectMissCount_` 등 `[Message thread only]` 변수를 직접 쓰기. ARM(macOS M-시리즈)에서 torn read/stale cache 위험.

**수정**: `callAsync` 내에서 변수 할당하거나, `std::atomic`/mutex 보호 추가. 성능 영향 없는 접근이 필요.

---

### H7 — Receiver tryConnect에서 consumer_active 플래그 누수

**파일**: `plugins/receiver/Source/PluginProcessor.cpp:194-204`

`attachAsConsumer()` 후 producer 비활성 확인 → `sharedMemory_.close()` 호출하지만 `ringBuffer_.detach()` 누락. `consumer_active`가 true로 남아 다음 Receiver 연결 시 허위 다중연결 경고.

**수정**: close() 전에 `ringBuffer_.detach()` 호출.

---

### H8 — UpdateChecker 배치 스크립트 경로에 작은따옴표

**파일**: `UpdateChecker.cpp:402-421`

PowerShell 명령에서 경로를 작은따옴표로 감싸지만, 사용자명에 `'` 포함 시 (`O'Brien`) 파싱 실패. 이전 exe는 `_backup`으로 이동된 상태에서 업데이트 중단 → 실행 불가.

**수정**: PowerShell 작은따옴표 내 `'`를 `''`로 이스케이프.

---

## Phase 3: MEDIUM

### M1 — outputChannelData nullptr 미체크

**파일**: `AudioEngine.cpp:937-939`

ASIO 레거시 드라이버에서 비활성 채널에 nullptr 전달 가능. input은 null 체크하지만 output은 안 함.

**수정**: `if (outputChannelData[ch]) std::memset(...)` 가드 추가.

---

### M2 — parseSlotFile이 .bak 폴백 없이 직접 읽기

**파일**: `PresetManager.cpp:378-388`

캐시 히트 경로에서 `loadFileAsString()` 직접 사용. 파일 손상 시 유효 캐시를 폐기하고 느린 경로로 폴백.

**수정**: `loadFileWithBackupFallback()` 사용.

---

### M3 — 플러그인 제거 시 이름 기반 검색

**파일**: `PluginChainEditor.cpp:66-77`

동일 이름 플러그인 2개 시 항상 첫 번째가 제거됨.

**수정**: rowIndex + name 조합으로 식별.

---

### M4 — WebSocket >1MB 프레임에 close frame 미전송

**파일**: `WebSocketServer.cpp:245`

RFC 6455 위반 — 클라이언트가 재연결 루프에 빠짐.

**수정**: 1009 상태 코드로 close frame 전송 후 종료.

---

### M5 — importAll/importFullBackup 플랫폼 호환성 체크 누락

**파일**: `SettingsExporter.cpp:95,180`

다이얼로그에서만 체크하고, 직접 호출 시 누락.

**수정**: import 함수 내부에 `isPlatformCompatible()` 체크 추가.

---

### M6 — pushNotification에서 juce::String 복사 (힙 할당)

**파일**: `AudioEngine.cpp:1460`

`notifQueue_[slot] = {msg, level}` — juce::String 복사 = 힙 할당. `audioDeviceError`(device thread)에서 호출.

**수정**: 고정 크기 char 배열 또는 pre-allocated 문자열 풀 사용. 또는 현재 사용 패턴(device thread, 드물게 호출)에서는 수용 가능으로 문서화.

---

### M7 — macOS/Linux AutoStart에 atomicWriteFile 미사용

**파일**: `MacAutoStart.cpp:74`, `LinuxAutoStart.cpp:69`

`replaceWithText` 사용 — 전원 중단 시 파일 손상.

**수정**: `atomicWriteFile()` 사용. (core/Util에 이미 존재)

---

## 구현 순서 및 의존성

```
Phase 1 (CRITICAL — 즉시):
  [C1] LoadLibraryA RT 콜백 → audioDeviceAboutToStart로 이동
  [C2] MidiHandler LearnTimeout use-after-free
  [C3] Reset Auto loadingSlot_ 가드
  ↑ 세 이슈 모두 독립적, 병렬 가능

Phase 2 (HIGH — 다음):
  [H1] audioDeviceStopped recorder 중지 ← 1줄
  [H2] Auto 부분 실패 partialLoad_ ← 1줄
  [H3] pendingSlot_ 리셋 ← 소규모
  [H4] quickStateHash plugin name ← 2줄
  [H5] broadcastToClients 락 범위 축소 ← 리팩토링
  [H6] audioDeviceAboutToStart 스레드 안전 ← 설계 검토 필요
  [H7] Receiver consumer_active 누수 ← 1줄
  [H8] UpdateChecker 배치 스크립트 따옴표 ← 문자열 처리
  ↑ H5, H6은 설계 검토 후 구현. 나머지는 독립적

Phase 3 (MEDIUM — 시간 여유 시):
  [M1-M7] 각각 독립적
  ↑ M6은 현재 사용 패턴에서 수용 가능 — 문서화만으로도 충분
```
