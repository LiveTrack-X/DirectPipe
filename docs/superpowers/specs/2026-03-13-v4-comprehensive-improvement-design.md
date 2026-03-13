# DirectPipe v4 Comprehensive Improvement Plan

> Date: 2026-03-13
> Status: Approved (spec review passed)
> Scope: Test coverage, logging, error resilience across 4 module slices

## Background

DirectPipe v4는 v3의 Windows 전용 코드를 크로스플랫폼(Windows/macOS/Linux)으로 전환한 알파 버전이다. 현재 개발자가 Windows 환경에서만 테스트 가능하므로, macOS/Linux에서 발생할 수 있는 버그를 코드 레벨에서 최대한 방어해야 한다.

### 발견된 문제 이력

v3.10.x에서 실제 발생한 설정 저장 관련 버그:
- **v3.10.1**: VST bypass 상태 저장/로드 불일치, 프리셋 슬롯 oscillation, 캐시 corruption
- **v3.9.12**: StateBroadcaster slotNames 해시 충돌
- **v3.9.7**: Settings scope 분리 필요 (Save/Load vs Full Backup)

### 현재 테스트 커버리지

| 영역 | 테스트 수 | 커버리지 |
|------|----------|---------|
| Core IPC (RingBuffer, SharedMemory) | 51 | ~80% |
| Control Dispatch (ActionDispatcher, ControlMapping) | 111 | ~50% |
| Audio Engine (AudioEngine, VSTChain, OutputRouter) | 0 | 0% |
| UI/Preset (PresetManager, SettingsExporter) | 0 | 0% |
| Platform (AutoStart, ProcessPriority, MultiInstanceLock) | 0 | 0% |

### 기존 CI 인프라

GitHub Actions `build.yml`이 이미 3플랫폼 빌드 + 테스트를 수행:
- Windows (windows-latest): CMake → Build → ctest
- macOS (macos-14): CMake → Build → ctest
- Linux (ubuntu-24.04): CMake → Build → ctest
- Stream Deck 플러그인: Node.js 빌드 + pack

## Approach

**모듈별 수직 슬라이스**: 한 모듈 그룹을 잡으면 테스트 + 로깅 + 에러 복원력을 한 번에 완성하고 다음으로 이동. 한 슬라이스가 끝나면 그 영역은 완전히 신뢰할 수 있다.

### Test Dependencies & Mocking Strategy

PresetManager는 AudioEngine에 의존하고, AudioEngine은 VSTChain/OutputRouter 등 전체 오디오 스택을 끌어온다. 이를 해결하기 위해:

- **JSON-level 테스트 우선**: PresetManager의 `exportChainToJSON()` / `importFromJSON()` 등 JSON 직렬화는 AudioEngine 없이 독립 테스트 가능. 파일 I/O + JSON 파싱 로직을 최대한 이 레벨에서 커버.
- **최소 AudioEngine 인스턴스**: 슬롯 전환/bypass 검증처럼 AudioEngine이 필요한 테스트는 오디오 디바이스를 열지 않는 최소 인스턴스 사용 (`initialiseWithDefaultDevices(0, 0)` 또는 디바이스 초기화 생략).
- **JUCE MessageManager**: `callAsync`를 사용하는 테스트는 `juce::ScopedJuceInitialiser_GUI` 또는 `MessageManager::getInstance()` 초기화 후 `runDispatchLoopUntil()`로 처리. 기존 `test_websocket_protocol.cpp`의 `RoundTripPluginBypass` 테스트가 이 패턴 사용 중.
- **타이밍 결정성**: oscillation/debounce 테스트는 실제 타이머가 아닌 `tick()`을 직접 호출하여 시간 경과를 시뮬레이션. 100% 결정적.
- **임시 디렉토리**: 기존 `test_control_mapping.cpp`의 `ControlMappingTest` 픽스처처럼 `juce::File::createTempFile()`로 임시 `.dppreset` 파일 생성/삭제.

### CI Platform Notes

- **Platform 테스트**: AutoStart는 CI에서 실제 레지스트리/plist 경로에 쓰므로, 테스트 후 정리(cleanup) 필수. Linux CI 컨테이너에서 XDG 경로 존재 여부 사전 확인.
- **MultiInstanceLock**: JUCE InterProcessLock은 파일 기반이므로 CI에서 동작. 실제 두 번째 프로세스 테스트는 기존 `test_cross_process_ipc.cpp`의 child process 패턴 재사용 가능.

## Slice 1: Settings Persistence + Bypass State Integrity

**대상 모듈**: `PresetManager`, `SettingsExporter`, `SettingsAutosaver`, `VSTChain` (bypass 상태 부분)

### 1.1 Tests

**신규 파일**: `tests/test_preset_manager.cpp`, `tests/test_settings_exporter.cpp`, `tests/test_settings_autosaver.cpp`

#### test_preset_manager.cpp (~16 tests)

| Test | 검증 내용 |
|------|----------|
| SaveLoadRoundtrip | 프리셋 저장 → 로드 → 모든 JSON 키/값 일치 |
| BypassStateRoundtrip | bypass true/false 저장 → 로드 → 상태 정확히 복원 |
| BypassAcrossSlotSwitch | A(bypass=true) → B → A 복귀 시 bypass 보존 (동기 경로) |
| BypassAcrossSlotSwitchAsync | replaceChainAsync 경로: A(bypass=true) → B → A 완료 콜백 후 bypass 보존 |
| SlotOscillationSafety | 100ms 간격 A→B→A→B 10회 반복 → 상태 corruption 없음 |
| CacheInvalidationDuringAutosave | invalidateAll() 직후 saveNow() 호출 → 안전 |
| SlotNamingKorean | 슬롯 이름 "게임" 저장/로드 → 일치 |
| SlotNamingPipeDelimiter | "A\|게임" 형식 파싱 → label "A", name "게임" |
| SlotNamingMaxLength | 8자 초과 → ".." truncation 검증 |
| SlotNamingEmptyName | 빈 이름 → 기본 레이블 유지 |
| CorruptJsonRecovery | 깨진 JSON 로드 → 크래시 없이 빈 상태 반환 |
| EmptyFileRecovery | 0바이트 파일 로드 → 크래시 없이 기본값 |
| MissingKeysRecovery | 필수 키 누락 JSON → 누락 키만 기본값 적용 |
| SelfHealingFromSlotFile | settings.dppreset 체인 비어있고 슬롯 파일 유효 → 슬롯에서 복구 |
| PluginStateBase64Roundtrip | getStateInformation base64 인코딩/디코딩 무결성 |
| MultiplePluginChainRoundtrip | 3개 플러그인 체인 (각각 다른 bypass) 저장/로드 순서 보존 |

#### test_settings_exporter.cpp (~10 tests, SettingsExporter + ControlMapping recovery)

| Test | 검증 내용 |
|------|----------|
| DpbackupRoundtrip | `.dpbackup` 저장 → 로드 → 설정 일치, plugins 키 없음 |
| DpfullbackupRoundtrip | `.dpfullbackup` 저장 → 로드 → 전체 데이터 포함 plugins 키 |
| CrossPlatformReject | platform="macos" 백업을 Windows에서 로드 시도 → 거부 |
| CrossPlatformAcceptSame | platform="windows" 백업을 Windows에서 → 허용 |
| LegacyNoPlatformField | platform 필드 없는 레거시 백업 → 허용 |
| GetCurrentPlatform | 현재 OS에 맞는 문자열 반환 검증 |
| ExportStripsPluginsKey | exportAll이 plugins 키를 제거하는지 확인 |
| FullBackupIncludesPlugins | exportFullBackup이 plugins 키를 포함하는지 확인 |
| CorruptControlsJsonRecovery | controls.json corrupt → .bak에서 복구 → 성공 시 경고 로그 |
| CorruptControlsJsonNoBackup | controls.json corrupt + .bak 없음 → 빈 기본 바인딩 초기화 |

#### test_settings_autosaver.cpp (~7 tests) — 별도 파일

| Test | 검증 내용 |
|------|----------|
| DirtyFlagReset | markDirty → tick 30회 → dirty=false 확인 |
| CooldownResetOnReDirty | markDirty → tick 15회 → markDirty → 다시 30회 필요 |
| DeferDuringLoading | loadingSlot=true 동안 cooldown 반복 재설정 확인 |
| ForceAfterMaxDefer | 50회 defer 후 강제 저장 실행 확인 |
| SaveNowSkipsDuringLoading | loadingSlot=true일 때 saveNow() 스킵 확인 |
| DebounceTiming | markDirty → 30틱 후 saveNow 호출 검증 |
| DeferAndForce | loading 중 defer → kMaxDeferCount 도달 시 강제 저장 |

### 1.2 Logging Enhancement

| 위치 | 추가 로깅 |
|------|----------|
| `PresetManager::saveSlot()` | `[PRESET] Saved slot X: N plugins, bypass=[T,F,T], size=NNNbytes` |
| `PresetManager::loadSlot()` | `[PRESET] Loaded slot X: N plugins, bypass=[T,F,T]` |
| `PresetManager::savePreset()` | `[PRESET] Settings saved to <path> (size=NNN)` |
| `PresetManager::loadPreset()` | `[PRESET] Settings loaded from <path>` + JSON 검증 결과 |
| JSON 파싱 실패 시 | `[PRESET] JSON parse failed: first 200 chars of content` |
| `SettingsExporter` | `[PRESET] Export/Import: tier=backup/full, platform=<os>` |

### 1.3 Error Resilience

#### Atomic Write (half-write 방지)

3단계 원자적 쓰기:
1. `file.tmp`에 전체 JSON 쓰기
2. 기존 `file`을 `file.bak`으로 rename
3. `file.tmp`를 `file`로 rename

**플랫폼 차이**:
- **POSIX (macOS/Linux)**: `rename()` is atomic within the same filesystem. 단계 3 이후 크래시해도 `file`은 완전한 새 데이터.
- **Windows**: `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`는 원자적이지 않을 수 있지만, NTFS에서 단일 파일 rename은 사실상 원자적. `ReplaceFile()` API가 더 안전하지만, JUCE `File::moveFileTo()`가 내부적으로 `MoveFileEx`를 사용하므로 충분.
- **실패 복구**: 단계 2에서 크래시 → `.tmp` 존재 + `.bak` 존재 + `file` 없음 → 로드 시 `.bak`에서 복구. 단계 1에서 크래시 → 불완전한 `.tmp` + 원본 `file` 건재 → `.tmp` 삭제.

적용 대상:
- `PresetManager::savePreset()` (settings.dppreset)
- `PresetManager::saveSlot()` (slot_X.dppreset)
- `ControlMapping::save()` (controls.json)

#### Corrupt controls.json Recovery

```
로드 시: controls.json 파싱 실패 → controls.json.bak 존재하면 .bak에서 로드 시도
         → .bak도 실패 → 빈 기본 바인딩으로 초기화 + 경고 로그
```

#### Auto-Backup Recovery

```
로드 시: file 파싱 실패 → file.bak 존재하면 .bak에서 로드 시도 → 성공 시 로그 경고
```

#### Bypass State Validation

```
로드 후: 각 플러그인의 slot.bypassed와 node->isBypassed() 비교
불일치 시: node->setBypassed(slot.bypassed) 강제 동기화 + 경고 로그
```

## Slice 2: Audio Engine

**대상 모듈**: `AudioEngine`, `VSTChain`, `OutputRouter`, `MonitorOutput`

### 2.1 Tests

**신규 파일**: `tests/test_audio_engine.cpp` (~12 tests), `tests/test_output_router.cpp` (~6 tests)

#### test_audio_engine.cpp (~12 tests)

| Test | 검증 내용 |
|------|----------|
| DriverSnapshotSaveRestore | 스냅샷 저장 → 드라이버 전환 → 복원 → 설정 일치 |
| DriverSnapshotDeviceNames | 저장된 디바이스 이름 검증 |
| OutputNoneToggle | setOutputNone(true/false) 토글 → isOutputNone() 상태 일치 |
| OutputNoneClearOnDriverSwitch | 드라이버 전환 시 outputNone 클리어 |
| DesiredDeviceSave | 입출력 디바이스 설정 → desiredDevice 저장 검증 |
| ReconnectionAttempt | 장치 유실 시뮬레이션 → attemptReconnection 호출 확인 |
| ReconnectionMaxRetry | 5회 실패 후 reconnectMissCount_ 증가 |
| FallbackProtection | intentionalChange_ false → audioDeviceAboutToStart에서 fallback 감지 |
| XRunWindowRolling | 60초 윈도우 내 카운트 → 이전 건 만료 검증 |
| XRunResetFlag | xrunResetRequested_ → 카운트 클리어 |
| BufferSizeFallback | 지원하지 않는 BS 요청 → 가장 가까운 값 선택 |
| SampleRatePropagation | SR 변경 → VSTChain/Monitor/IPC 전파 검증 |

#### test_output_router.cpp (~6 tests)

| Test | 검증 내용 |
|------|----------|
| VolumeClamp | setVolume(1.5) → getVolume() == 1.0 |
| VolumeZeroSkipsMonitor | volume=0 → monitorOutput_->writeAudio 호출 안 됨 |
| EnableDisableToggle | setEnabled(false) → routeAudio 시 monitor 무시 |
| BufferTruncatedFlag | numSamples > scaledBuffer 용량 → checkAndClearBufferTruncated() true |
| MonoToStereoRouting | mono 버퍼 → 양쪽 채널 동일 데이터 |
| UninitializedEarlyReturn | initialize() 미호출 시 routeAudio 안전 |

### 2.2 Logging Enhancement

| 위치 | 추가 로깅 |
|------|----------|
| 드라이버 전환 | `[AUDIO] Driver switch: <from> → <to>, snapshot saved/restored` |
| 디바이스 재연결 | `[AUDIO] Reconnect attempt #N: desired=<name>, available=[...]` |
| fallback 감지 | `[AUDIO] Fallback detected: desired=<name>, actual=<name>` |
| bufferTruncated | message thread에서 플래그 확인 시 `[AUDIO] Buffer truncation detected` |

### 2.3 Error Resilience

- 재연결 5회 실패 후 사용자 알림 (NotificationBar)
- MonitorOutput 샘플레이트 미스매치 시 UI 상태 표시 개선
- OutputRouter `initialize()` 호출 전 `routeAudio()` 진입 시 early return 방어

## Slice 3: Control Handlers

**대상 모듈**: `MidiHandler`, `HotkeyHandler`, `ActionHandler`

### 3.1 Tests

**신규 파일**: `tests/test_midi_handler.cpp` (~8 tests), `tests/test_action_handler.cpp` (~5 tests)

#### test_midi_handler.cpp (~8 tests)

| Test | 검증 내용 |
|------|----------|
| AddBinding | 바인딩 추가 → getBindings()에 포함 확인 |
| RemoveBinding | 바인딩 삭제 → getBindings()에서 제거 확인 |
| SerializeDeserialize | 바인딩 직렬화 → 역직렬화 → 일치 |
| LearnStartComplete | startLearn → CC 수신 → 바인딩 자동 생성 |
| LearnCancel | startLearn → stopLearn → 바인딩 미생성 |
| LearnTimeout | startLearn → 30초 경과 시뮬레이션 → 자동 취소 확인 |
| DuplicateBindingDetect | 동일 CC 바인딩 시도 → 경고 + 거부/덮어쓰기 검증 |
| DispatchOutsideLock | dispatch 시 bindingsMutex_ 외부에서 콜백 실행 확인 |

#### test_action_handler.cpp (~5 tests)

| Test | 검증 내용 |
|------|----------|
| PanicMuteEngage | doPanicMute(true) → 모든 출력 음소거 확인 |
| PanicMuteRestore | mute → unmute → pre-mute 상태 복원 |
| PanicMutePreserveMonitor | monitor on → mute → unmute → monitor on 복원 |
| CallbackOrder | 액션 실행 시 onDirty/onNotification 콜백 순서 검증 |
| HotkeyPlatformSupport | isHotkeySupported() → Windows true, Linux false |

### 3.2 Logging Enhancement

- MIDI Learn 시작/완료/취소/타임아웃 이벤트
- 핫키 등록 실패 시 키 조합 + 에러 코드
- ActionHandler 패닉뮤트 진입/해제 시 pre-mute 상태 기록

### 3.3 Error Resilience

- MIDI Learn 30초 타임아웃 (무한 대기 방지)
- 바인딩 중복 감지 + 경고 로그
- HotkeyHandler: Linux stub에서 명확한 "not supported" 로그

## Slice 4: Platform Abstraction

**대상 모듈**: `AutoStart`, `ProcessPriority`, `MultiInstanceLock`

### 4.1 Tests

**신규 파일**: `tests/test_platform.cpp` (~7 tests)

| Test | 검증 내용 |
|------|----------|
| AutoStartToggle | setAutoStartEnabled(true) → isAutoStartEnabled() == true |
| AutoStartDisable | setAutoStartEnabled(false) → isAutoStartEnabled() == false |
| AutoStartSupported | isAutoStartSupported() → 현재 OS에 맞는 값 반환 |
| ProcessPriorityHigh | setHighPriority() → 크래시 없이 완료 |
| ProcessPriorityRestore | restoreNormalPriority() → 크래시 없이 완료 |
| MultiInstanceAcquire | acquireExternalControlPriority() 반환값 1 (성공) |
| MultiInstanceAlreadyHeld | 두 번째 acquireExternalControlPriority() → 적절한 반환값 |

### 4.2 Logging Enhancement

- 플랫폼 API 실패 시 일관된 에러 로깅 (macOS는 이번 세션에서 완료)
- Windows Registry 접근 실패 시 에러 코드 포함

### 4.3 Error Resilience

- AutoStart: 레지스트리/plist/desktop 파일 쓰기 실패 시 재시도 1회
- MultiInstanceLock: 락 파일 손상 시 재생성 시도

## Build Integration

### CMake 변경

`tests/CMakeLists.txt`에 신규 테스트 파일 추가:

```cmake
# Host tests (JUCE 의존)
target_sources(directpipe-host-tests PRIVATE
    # 기존
    test_websocket_protocol.cpp
    test_action_dispatcher.cpp
    test_action_result.cpp
    test_control_mapping.cpp
    test_notification_queue.cpp
    # 신규 Slice 1
    test_preset_manager.cpp
    test_settings_exporter.cpp
    test_settings_autosaver.cpp
    # 신규 Slice 2
    test_audio_engine.cpp
    test_output_router.cpp
    # 신규 Slice 3
    test_midi_handler.cpp
    test_action_handler.cpp
    # 신규 Slice 4
    test_platform.cpp
)
```

추가 소스 링크 (테스트 대상 구현체):
- Slice 1: `PresetManager.cpp`, `SettingsExporter.cpp`, `SettingsAutosaver.cpp`
- Slice 2: `AudioEngine.cpp`, `VSTChain.cpp`, `OutputRouter.cpp`, `MonitorOutput.cpp`, `AudioRingBuffer.cpp`, `LatencyMonitor.cpp`
- Slice 3: `MidiHandler.cpp`, `ActionHandler.cpp`, `HotkeyHandler.cpp`
- Slice 4: Platform 구현체는 `#ifdef` 기반이므로 현재 OS 구현만 링크

### CI 영향

기존 `build.yml`의 `ctest` 단계가 자동으로 신규 테스트를 실행하므로 CI 워크플로우 변경 불필요. 3플랫폼 모두에서 새 테스트가 돌아감.

## Success Criteria

- 전체 테스트 수: 162 → ~233 (71개 추가: Slice1=33, Slice2=18, Slice3=13, Slice4=7)
- Settings Persistence 테스트 커버리지: 0% → ~80%
- Audio Engine 테스트 커버리지: 0% → ~40% (디바이스 의존 테스트 한계)
- 프리셋 저장 시 atomic write로 half-write 0건
- corrupt 파일 로드 시 크래시 0건 (자동 복구 또는 기본값 fallback)
- bypass 상태 저장/로드 불일치 0건
- 3플랫폼 CI 빌드 + 테스트 통과
