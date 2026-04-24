# Audio 모듈

마이크 입력부터 VST 체인 처리, 출력 라우팅까지 전체 오디오 파이프라인을 담당하는 핵심 모듈.
AudioEngine이 WASAPI/ASIO/CoreAudio/ALSA 디바이스를 관리하고, RT 콜백에서 모든 처리를 수행한다.

---

## Data Flow

```
Mic (WASAPI/ASIO/CoreAudio/ALSA)
 |
 v
AudioEngine RT callback (audioDeviceIOCallbackWithContext)
 |
 |  Fast path: panic mute active (`muted_`) -> zero output and return immediately
 |
 |  1. inputChannelData -> workBuffer_ (pre-allocated 8ch)
 |     - Mono: sum all input channels -> ch0, duplicate to ch1
 |     - Stereo: copy channels as-is
 |     - inputDeviceLost_: skip copy (use silence)
 |
|  2. Input gain (SIMD via JUCE FloatVectorOperations)
|     - inputMuted_: clear buffer to silence (input-only mute, chain keeps running)
|
|  3. RMS input level (decimated: every 4th callback)
|
v
VSTChain.processBlock(workBuffer_)
|  - AudioProcessorGraph inline processing
|  - Plugin bypass via atomic flags
|  - Inline processing (체인/플러그인 PDC 설정이 전체 지연에 반영됨)
|
+---> SafetyLimiter.process()            [RT-safe global Safety Guard (legacy name, zero-latency sample-peak guard + hard clamp), applied before ALL outputs]
|
+---> Safety Volume trim                [Final global output trim (default -0.3 dB) applied after Safety Guard to ALL outputs]
|
+---> AudioRecorder.writeBlock()         [RT try-lock/drop -> ThreadedWriter FIFO -> BG writer thread]
|
+---> SharedMemWriter.writeAudio()       [if ipcEnabled_, lock-free ring buffer -> Receiver VST]
|
+---> OutputRouter.routeAudio()
|      |
|      +---> MonitorOutput.writeAudio()  [lock-free AudioRingBuffer -> separate WASAPI callback]
|
+---> outputChannelData (main output)    [apply output volume, or zero if outputMuted_]
 |
 v
RMS output level (decimated: every 4th callback)
LatencyMonitor.markCallbackEnd()
```

---

## File List

| 파일 | 설명 |
|------|------|
| `AudioEngine.h/cpp` | 핵심 오디오 엔진. 디바이스 관리, RT 콜백, 입출력 채널 라우팅, 디바이스 재연결, XRun 추적 |
| `VSTChain.h/cpp` | VST2/VST3 플러그인 체인. AudioProcessorGraph 기반 직렬 체인, 비동기 로딩, 에디터 창 관리 |
| `OutputRouter.h/cpp` | 처리된 오디오를 모니터(헤드폰) 출력으로 라우팅. 볼륨/활성화 제어, RMS 레벨 측정 |
| `MonitorOutput.h/cpp` | 별도 WASAPI 공유 모드 디바이스를 통한 헤드폰 모니터링. AudioRingBuffer로 RT<->모니터 스레드 브릿징 |
| `AudioRingBuffer.h` | SPSC lock-free 링 버퍼 (header-only). 메인 RT 콜백(producer) <-> 모니터 WASAPI 콜백(consumer) |
| `AudioRecorder.h/cpp` | WAV 파일 녹음. RT write path는 try-lock/drop, ThreadedWriter FIFO로 BG 스레드에서 디스크 flush |
| `LatencyMonitor.h/cpp` | 오디오 경로 레이턴시 측정 (입력/처리/출력 버퍼). CPU 사용률 계산 |
| `PluginPreloadCache.h/cpp` | 프리셋 슬롯 전환용 플러그인 인스턴스 백그라운드 프리로딩. 캐시 hit 시 DLL 로딩 건너뜀 |
| `PluginLoadHelper.h` | 크로스플랫폼 플러그인 인스턴스 생성 헬퍼 (header-only). macOS에서 AppKit 메인 스레드 디스패치 |
| `SafetyLimiter.h/cpp` | RT-safe global Safety Guard (legacy class name). Atomic params (enabled, ceiling). Zero-latency stereo-linked sample-peak guard, instant attack, 50ms release smoothing, hard ceiling clamp. GR feedback for UI. Final `Safety Volume` trim (enable + dB) is applied in `AudioEngine` after guard processing |
| `DeviceState.h` | 디바이스 연결 상태 열거형 (header-only). DeviceState enum + transition() + deviceStateToString() |
| `BuiltinFilter.h/cpp` | 내장 HPF + LPF 필터 (AudioProcessor 상속). IIR 2차 버터워스. RT-safe. PDC 0 |
| `BuiltinNoiseRemoval.h/cpp` | 내장 RNNoise 노이즈 제거 (AudioProcessor 상속). FIFO 480프레임, VAD 게이팅, dual-mono. PDC 480 samples |
| `BuiltinAutoGain.h/cpp` | 내장 LUFS AGC (AudioProcessor 상속). ITU-R BS.1770 K-weighting, 비대칭 보정 (Luveler Mode 2) + 고정 post limiter(ceiling 노출, 내부 lookahead/release 고정). 고정 지연 경로 사용 (PDC = lookahead samples) |

---

## Thread Model

| 클래스 | 메서드/영역 | 스레드 | 비고 |
|--------|-------------|--------|------|
| AudioEngine | `audioDeviceIOCallbackWithContext` | `[RT thread]` | heap alloc 금지, mutex 금지. ScopedNoDenormals 사용 |
| AudioEngine | `initialize`, `shutdown`, `set*Device` | `[Message thread]` | 디바이스 매니저 조작 |
| AudioEngine | `checkReconnection`, `updateXRunTracking` | `[Message thread]` | 30Hz 타이머에서 호출 |
| AudioEngine | `audioDeviceError`, `audioDeviceStopped` | `[Device thread]` | JUCE 디바이스 스레드에서 호출 |
| AudioEngine | `popNotification` (read) | `[Message thread]` | lock-free queue에서 소비 |
| AudioEngine | `pushNotification` (write) | `[Device thread]` / `[Message thread]` | MPSC-safe queue에 생산 (RT 콜백에서는 호출하지 않음) |
| VSTChain | `processBlock` | `[RT thread]` | chainLock_ 사용 안 함. 현재 render sequence를 lock-free로 처리 |
| VSTChain | `addPlugin`, `removePlugin`, `movePlugin` | `[Message thread]` | `chainLock_` 보호. `rebuildGraph(true)` |
| VSTChain | `setPluginBypassed` | `[Message thread]` | `chainLock_` + `rebuildGraph(false)` (suspend 없음) |
| VSTChain | `replaceChainAsync` | `[Message thread]` -> `[BG thread]` -> `[Message thread]` | DLL 로딩은 BG, graph 삽입은 callAsync |
| VSTChain | `replaceChainWithPreloaded` | `[Message thread]` | 프리로드 캐시 사용 시 동기 swap |
| OutputRouter | `routeAudio` | `[RT thread]` | atomic 볼륨/활성화. scaledBuffer_ 용량 클램프 |
| MonitorOutput | `writeAudio` | `[RT thread]` | AudioRingBuffer producer (lock-free) |
| MonitorOutput | `audioDeviceIOCallbackWithContext` | `[Monitor RT thread]` | AudioRingBuffer consumer (lock-free) |
| MonitorOutput | `initialize`, `setDevice`, `checkReconnection` | `[Message thread]` | 별도 AudioDeviceManager 조작 |
| AudioRingBuffer | `write` (producer) | `[RT thread]` | SPSC. capacity는 power-of-2 필수 |
| AudioRingBuffer | `read` (consumer) | `[Monitor RT thread]` | SPSC 단일 소비자 |
| AudioRecorder | `writeBlock` | `[RT thread]` | try-lock 후 ThreadedWriter FIFO에 push, teardown 경합 시 drop. jassert: NOT message thread |
| AudioRecorder | `startRecording`, `stopRecording` | `[Message thread]` | `writerLock_` (SpinLock) |
| LatencyMonitor | `markCallbackStart/End` | `[RT thread]` | `sampleRate_`, `bufferSize_`, `callbackStartTicks_`, `avgProcessingTime_` 모두 atomic (reset()과의 cross-thread 안전) |
| LatencyMonitor | `reset` | `[Message thread]` | audioDeviceAboutToStart에서 호출. atomic store(relaxed) |
| LatencyMonitor | `get*Ms`, `getCpuUsagePercent` | `[Message thread]` | atomic read |
| PluginPreloadCache | `preloadAllSlots` | `[Message thread]` -> `[BG thread]` | BG 스레드에서 DLL 로딩. `cacheMutex_`로 캐시 보호 |
| PluginPreloadCache | `take`, `isCached` | `[Message thread]` | `cacheMutex_` 보호 |
| PluginPreloadCache | `invalidateAll` | `[Message thread]` | non-blocking: `slotVersions_` bump + `cancelPreload_` |
| SafetyLimiter | `process()` | `[RT audio]` | Atomics only, no alloc/mutex/logging |
| SafetyLimiter | `set*/get*` | `[Any thread]` | Atomic reads/writes |
| `BuiltinFilter` | `processBlock()` | `[RT audio]` | IIR 필터 적용. atomic freq 읽기, setCoefficients (스택 연산) |
| `BuiltinFilter` | `setters` | `[Any thread]` | atomic 쓰기 |
| `BuiltinNoiseRemoval` | `processBlock()` | `[RT audio]` | FIFO + rnnoise_process_frame. 힙 할당 없음 |
| `BuiltinNoiseRemoval` | `prepareToPlay/release` | `[Message]` | rnnoise_create (malloc) / rnnoise_destroy |
| `BuiltinAutoGain` | `processBlock()` | `[RT audio]` | K-weighting sidechain + 증분 LUFS + 게인 적용 |
| `BuiltinAutoGain` | `prepareToPlay` | `[Message]` | 링버퍼 할당, K-weighting 계수 계산 |
| PluginLoadHelper | `createPluginOnCorrectThread` | `[BG thread]` / `[Message thread]` | macOS: BG->메시지 스레드 디스패치. Windows/Linux: 호출 스레드에서 직접 |

---

## Resource Lifecycle (리소스 수명 관리)

| 리소스 | 생성 | 소유 | 파괴 | 비고 |
|--------|------|------|------|------|
| `AudioDeviceManager` (deviceManager_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | 메인 오디오 디바이스 |
| `VSTChain` (vstChain_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | |
| `AudioProcessorGraph` (graph_) | VSTChain::prepareToPlay | VSTChain (unique_ptr) | VSTChain::releaseResources / 소멸자 | RT 스레드에서 processBlock 호출 |
| `PluginSlot.instance` | VSTChain::addPlugin / replaceChainAsync | graph_ 노드가 소유 | graph_->removeNode() | 에디터 열려있으면 먼저 닫을 것 |
| `DocumentWindow` (editorWindows_) | VSTChain::openPluginEditor | VSTChain (unique_ptr 벡터) | closePluginEditor / 소멸자 | Message thread only |
| `OutputRouter` | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | scaledBuffer_ 사전 할당 |
| `MonitorOutput` (monitorOutput_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | 별도 AudioDeviceManager 소유 (unique_ptr) |
| `AudioRingBuffer` | MonitorOutput 생성자 | MonitorOutput (stack) | MonitorOutput 소멸자 | capacity는 power-of-2 |
| `AudioRecorder` (recorder_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | ThreadedWriter는 startRecording에서 생성 |
| `SharedMemWriter` (sharedMemWriter_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | connected_ atomic으로 상태 관리 |
| `workBuffer_` | audioDeviceAboutToStart | AudioEngine | audioDeviceAboutToStart에서 setSize + clear | 8ch 사전 할당, RT 스레드 전용 |
| `PluginPreloadCache` | MainComponent에서 생성 | MainComponent | MainComponent 소멸자 | BG 스레드 프리로드, cacheMutex_ 보호 |
| `loadThread_` (VSTChain) | replaceChainAsync | VSTChain (unique_ptr) | 다음 replaceChainAsync 또는 소멸자 | asyncGeneration_으로 stale 폐기 |

---

## State Flow Diagrams (상태 흐름)

### Device Reconnection Flow (디바이스 재연결)

```
[Running] --audioDeviceError/audioDeviceStopped--> [DeviceLost]
    |                                                    |
    |  intentionalChange_=true                          | deviceLost_=true
    |  (사용자 의도 변경)                                 | inputDeviceLost_=true / outputAutoMuted_=true
    v                                                    |
[Restarting]                                             v
    |                                            [Reconnecting]
    |  audioDeviceAboutToStart                           |
    |  (새 디바이스 확인)                                 | checkReconnection (3초 타이머, reconnectCooldown_=90)
    v                                                    | attemptReconnection()
[Running]                                                |
                                                         | 성공 → audioDeviceAboutToStart → [Running]
                                                         | 실패 5회 → reconnectMissCount_ >= kMaxReconnectMisses
                                                         |   → 입력만 복구 가능하면 부분 복구
                                                         |   → 출력 lost → 계속 재시도 (outputAutoMuted_ 유지)
```

- `intentionalChange_` 플래그가 true이면 `audioDeviceStopped`에서 `deviceLost_`를 설정하지 않음
- `ChangeListener` (즉시 감지) + 3초 타이머 폴링 (ChangeListener 누락 시 폴백)의 이중 메커니즘
- MonitorOutput도 독립적으로 동일한 패턴 사용 (`monitorLost_` + 자체 cooldown)

### Preset Loading Flow (replaceChainAsync)

```
[Idle] --loadPreset(slotIndex)--> [Loading]
    |                                |
    |  loadingSlot_=true             | 1. asyncGeneration_++ (stale 보호)
    |  (MainComponent 소유)          | 2. BG 스레드: DLL 로딩 (COM init on Windows)
    |                                | 3. alive_ 체크 + generation 비교 → callAsync
    |                                | 4. Message 스레드: chainLock_ 획득
    |                                |    - graph_ suspendProcessing(true)
    |                                |    - 구 chain 노드 제거 (async UpdateKind)
    |                                |    - 새 노드 추가 + 상태 복원
    |                                |    - rebuildGraph → suspendProcessing(false)
    |                                | 5. loadingSlot_=false
    |                                v
    |                           [Complete]
    |                                |
    |  partialLoad_=true             | onChainChanged 콜백
    |  (일부 플러그인 실패)            | settingsAutosaver->markDirty()
    v                                v
[PartialLoad]                   [Idle] (정상)
```

- `loadThread_`가 이미 존재하면 join 후 새 스레드 생성
- `asyncGeneration_` 카운터로 이전 로딩의 stale callAsync 폐기
- `onPluginLoadFailed` 콜백으로 실패한 플러그인 이름/에러 전달

---

## Troubleshooting (트러블슈팅)

| 증상 | 진단 경로 | 핵심 파일:위치 |
|------|----------|--------------|
| 오디오 글리치/드롭아웃 | RT 콜백에서 할당/락 확인 → `audioDeviceIOCallbackWithContext` 내부 검사 | `AudioEngine.cpp` RT 콜백 |
| 디바이스 연결 끊김 반복 | `desiredInputDevice_` vs 실제 디바이스 비교 → `intentionalChange_` 플래그 확인 → fallback 감지 로직 | `AudioEngine.cpp:checkReconnection` |
| 모니터 출력 안 됨 | `monitorLost_` atomic 확인 → AudioRingBuffer overflow 확인 → 별도 WASAPI 디바이스 상태 | `MonitorOutput.cpp` |
| 프리셋 로딩 중 오디오 끊김 | replaceChainAsync의 Keep-Old-Until-Ready 패턴 확인 → graph swap 시점 | `VSTChain.cpp:replaceChainAsync` |
| 프리셋 로딩 실패 | `partialLoad_` 플래그 → 어떤 플러그인이 실패했는지 로그 확인 → DLL 경로/포맷 | `VSTChain.cpp:replaceChainAsync` |
| XRun 카운트 증가 | 버퍼 크기 확인 → processBlock 처리 시간 → CPU 우선순위 | `LatencyMonitor.cpp` |
| IPC 데이터 안 감 | `ipcEnabled_` → `connected_` → SharedMem 매핑 상태 → Receiver VST 버퍼 설정 | `SharedMemWriter.cpp` |

---

## DANGER ZONES

> 이 섹션을 수정할 때 CLAUDE.md의 Coding Rules와 해당 .h 파일의 Thread Ownership 어노테이션도 함께 업데이트할 것

1. **RT 콜백에서 heap 할당/mutex 금지**: `audioDeviceIOCallbackWithContext`에서 `new`, `malloc`, `std::mutex::lock` 등 사용 시 glitch 발생. `workBuffer_`는 `audioDeviceAboutToStart`에서 사전 할당. `emptyMidi_`도 `prepareToPlay`에서 사전 할당.

2. **VSTChain `chainLock_`를 `processBlock`에서 잡지 말 것**: RT 스레드에서 `chainLock_`를 잡으면 메시지 스레드의 플러그인 로딩과 데드락. `processBlock`은 lock-free 경로를 유지해야 함.

3. **VSTChain `chainLock_` 안에서 `writeToLog` 금지**: DirectPipeLogger의 `writeMutex_`와 lock ordering 충돌로 데드락. 로그 문자열은 lock 안에서 캡처하고, lock 해제 후 로깅.

4. **AudioRingBuffer capacity는 반드시 power-of-2**: `initialize()` 시 assertion으로 강제. 위반 시 mask 연산이 깨져서 데이터 손상.

5. **OutputRouter `routeAudio`에서 `numSamples` 클램프 필수**: `scaledBuffer_` 용량을 초과하면 buffer overrun. `bufferTruncated_` 플래그로 진단.

6. **SharedMemWriter `shutdown()` 순서**: `producer_active=false` -> `ringBuffer_.detach()` -> `dataEvent_.close()` -> `sharedMemory_.close()`. 순서 뒤바뀌면 consumer가 dangling pointer 접근.

7. **IPC 토글 race window**: `setIpcEnabled(false)` 후에도 RT 스레드가 `ipcEnabled_=true`를 읽을 수 있음. `interleaveBuffer_`를 `shutdown()`에서 해제하면 안 되는 이유.

8. **MonitorOutput 재연결**: `monitorLost_`는 `audioDeviceError`/`audioDeviceStopped`에서 설정, `audioDeviceAboutToStart`에서만 해제. JUCE auto-fallback 디바이스는 거부.

9. **PluginPreloadCache `invalidateAll()`은 thread join 하지 않음**: COM STA 데드락 방지. `cancelPreload_` + `slotVersions_` bump로 non-blocking 무효화.

10. **RMS decimation counter**: `rmsDecimationCounter_`는 RT 스레드 전용 변수 (atomic 불필요). 다른 스레드에서 접근하면 data race.

11. **`suspendProcessing` 카운터 메커니즘**: JUCE `AudioProcessorGraph::suspendProcessing(bool)`은 bool 토글이 아닌 **카운터 기반**. `true` 호출마다 카운터 증가, `false` 호출마다 감소. 카운터가 0일 때만 오디오 처리 활성. `rebuildGraph(suspend=true)`는 내부에서 `suspendProcessing(true/false)` 쌍을 호출하므로, 호출 전에 `suspendProcessing(true)`를 별도로 호출하면 카운터 불균형 → 그래프가 영구 mute. **절대 `rebuildGraph(true)` 앞에 `suspendProcessing(true)` 호출하지 말 것.**

12. **`UpdateKind::async` vs 기본(sync)**: JUCE `AudioProcessorGraph`의 `addNode`/`removeNode`에 `UpdateKind::async`를 전달하면 내부 렌더 시퀀스 재빌드가 지연됨. 마지막 변경에서만 `UpdateKind::sync` (기본)를 사용해 한 번에 재빌드. 중간 단계에서 sync를 사용하면 불완전한 그래프 상태에서 RT 스레드가 processBlock을 실행할 수 있음.

13. **`AudioProcessorGraph` 렌더 시퀀스**: 그래프는 내부적으로 토폴로지 정렬된 "렌더 시퀀스"를 유지. `rebuildGraph`에서 연결을 변경하면 이 시퀀스가 재계산됨. RT 스레드의 `processBlock`은 현재 시퀀스를 atomic하게 참조하므로, 시퀀스 swap 자체는 lock-free. 단, `suspendProcessing(true)` 상태에서는 RT 콜백이 silence를 출력.

14. **JUCE `Timer` 스레딩 규칙**: `juce::Timer`는 Message thread에서만 생성/파괴 가능. `stopTimer()`는 어느 스레드에서나 안전하지만, `Timer` 객체를 소유한 `unique_ptr.reset()`은 반드시 Message thread에서만 호출. 위반 시 타이머 내부 리스트 손상 → 크래시.

15. **JUCE `SafePointer` 스레딩**: `Component::SafePointer` 생성(WeakReference 등록)은 스레드 안전하지 않음. **반드시 Message thread에서만 생성**. BG 스레드에서 생성하면 Component의 master reference와 data race. BG 스레드에서 callAsync 보호가 필요하면 `shared_ptr<atomic<bool>> alive_` 패턴 사용.

16. **JUCE `String` 스레딩**: `juce::String`은 내부적으로 COW(Copy-On-Write) 참조 카운팅 사용. 두 스레드에서 동시에 같은 String 객체를 읽기/쓰기하면 참조 카운트 data race. `desiredInputDevice_` 같은 String 멤버는 한 스레드에서만 접근하거나, 접근 시 mutex 보호 필요.

17. **JUCE `File::moveFileTo` 동작**: 대상 파일이 이미 존재하면 먼저 `deleteFile()` 후 이동. POSIX `rename()`과 달리 atomic하지 않음 (delete + move 두 단계). `atomicWriteFile`의 .bak 경로가 동작하는 이유.

---

## When to Update This README

- Audio/ 디렉토리에 파일을 추가하거나 삭제할 때 File List 업데이트
- RT 콜백의 데이터 흐름이 변경될 때 (새 출력 대상 추가, 처리 순서 변경 등) Data Flow 다이어그램 업데이트
- 스레드 모델이 변경될 때 (새 lock 추가, atomic 변수 추가, 스레드 소유권 변경) Thread Model 테이블 업데이트
- 새로운 안전 규칙이 발견되거나 기존 DANGER ZONE이 변경될 때 해당 항목 업데이트
