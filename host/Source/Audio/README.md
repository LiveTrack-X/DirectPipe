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
 |     - Mono: average first two enabled inputs (one input: copy) -> ch0, duplicate to ch1
 |     - Stereo: compact first two enabled inputs -> internal L/R
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
|  - Plugin bypass via graph routing and synchronized processor state
|  - Inline processing (체인/플러그인 PDC 설정이 전체 지연에 반영됨)
|
+---> SafetyLimiter.process()            [RT-safe global Safety Guard (legacy name, zero-latency sample-peak guard + hard clamp), applied before ALL outputs]
|
+---> Safety Volume trim                [Final global output trim (default -0.3 dB) applied after Safety Guard to ALL outputs]
|
+---> AudioRecorder.writeBlock()         [RT try-lock/drop -> ThreadedWriter FIFO -> BG writer thread]
|
+---> SharedMemWriter.writeAudio()       [if ipcEnabled_, interleave once -> legacy SPSC + 8 independent fan-out queues]
|
+---> OutputRouter.routeAudio()
|      |
|      +---> MonitorOutput.writeAudio()  [lock-free AudioRingBuffer -> separate shared-mode callback + adaptive PLL fractional read + emergency trim fallback]
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
| `AudioEngine.h/cpp` | 핵심 오디오 엔진. 디바이스 관리, RT 콜백, 입출력 채널 라우팅, 3초 제한 재연결, 독점 main open 전 monitor 사전 중지/복원, XRun 추적 |
| `VSTChain.h/cpp` | VST2/VST3 플러그인 체인. AudioProcessorGraph 기반 직렬 체인, 비동기 로딩/취소, control-side graph lifecycle 직렬화, 에디터 창 관리 |
| `OutputRouter.h/cpp` | 처리된 오디오를 모니터(헤드폰) 출력으로 라우팅. 볼륨/활성화 제어, RMS 레벨 측정 |
| `MonitorOutput.h/cpp` | 선택한 별도 shared-mode 디바이스를 기본 장치 bootstrap 없이 직접 여는 헤드폰 모니터링 (Windows: WASAPI, macOS: CoreAudio, Linux: ALSA). AudioRingBuffer로 RT<->모니터 스레드 브릿징, low-watermark priming, adaptive PLL fractional playback, near-overflow emergency trim |
| `AudioRingBuffer.h` | SPSC lock-free 링 버퍼 (header-only). 메인 RT 콜백(producer) <-> 모니터 장치 콜백(consumer), integer read/discard 및 fractional interpolated read 지원 |
| `AudioRecorder.h/cpp` | WAV 파일 녹음. start/stop writer 상태를 SpinLock으로 선형화하고 RT write path는 try-lock/drop, ThreadedWriter FIFO로 BG 스레드에서 디스크 flush |
| `LatencyMonitor.h/cpp` | 드라이버가 보고한 입력·출력 지연(미보고 방향은 한 버퍼로 대체)과 별도 콜백 실행시간/CPU 측정. StatusUpdater/HTTP는 장치 지연에 활성 chain PDC를 추가하며 콜백 실행시간을 중복 합산하지 않음 |
| `PluginPreloadCache.h/cpp` | 프리셋 슬롯 전환용 플러그인 인스턴스 백그라운드 프리로딩. 캐시 hit 시 DLL 로딩 건너뜀 |
| `PluginLoadHelper.h` | 크로스플랫폼 플러그인 인스턴스 생성 헬퍼 (header-only). macOS에서 AppKit 메인 스레드 디스패치 |
| `SafetyLimiter.h/cpp` | RT-safe global Safety Guard (legacy class name). Atomic params (enabled, ceiling). Zero-latency stereo-linked sample-peak guard, instant attack, 50ms release smoothing, hard ceiling clamp. GR feedback for UI. Final `Safety Volume` trim (enable + dB) is applied in `AudioEngine` after guard processing |
| `DeviceState.h` | 디바이스 연결 상태 열거형 (header-only). DeviceState enum + transition() + deviceStateToString() |
| `BuiltinFilter.h/cpp` | 내장 HPF + LPF 필터 (AudioProcessor 상속). IIR 2차 버터워스. RT-safe. PDC 0 |
| `BuiltinNoiseRemoval.h/cpp` | 내장 RNNoise 노이즈 제거 (AudioProcessor 상속). callback-size-aware FIFO, VAD 게이팅, dual-mono. 48kHz PDC 480 samples, unsupported-rate passthrough PDC 0 |
| `BuiltinAutoGain.h/cpp` | 내장 LUFS AGC (AudioProcessor 상속). ITU-R BS.1770 K-weighting, 비대칭 보정 (Luveler Mode 2) + 고정 post limiter(ceiling 노출, 내부 lookahead/release 고정). 고정 지연 경로 사용 (PDC = lookahead samples) |

---

## Thread Model

| 클래스 | 메서드/영역 | 스레드 | 비고 |
|--------|-------------|--------|------|
| AudioEngine | `audioDeviceIOCallbackWithContext` | `[RT thread]` | host hot path는 사전 할당, blocking control lock/로그/파일 I/O 금지. Recorder는 try-lock/drop. 기존 Windows MMCSS 최초 등록과 legacy IPC event signal은 OS 호출 예외이며 외부 플러그인의 내부 동작은 별도 |
| AudioEngine | `initialize`, `shutdown`, `set*Device` | `[Message thread]` | 디바이스 매니저 조작. 독점 main open 전 충돌 가능 monitor를 중지하고 rollback/비충돌 결과에서 복원 |
| AudioEngine | `checkReconnection`, `updateXRunTracking` | `[Message thread]` | 30Hz 타이머에서 호출. 실패한 zero-active 복구는 90 tick(약 3초) 쿨다운 재적용 |
| AudioEngine | `audioDeviceError`, `audioDeviceStopped` | `[Device thread]` | JUCE 디바이스 스레드에서 호출 |
| AudioEngine | `popNotification` (read) | `[Message thread]` | lock-free queue에서 소비 |
| AudioEngine | `pushNotification` (write) | `[Device thread]` / `[Message thread]` | MPSC-safe queue에 생산 (RT 콜백에서는 호출하지 않음) |
| VSTChain | `processBlock` | `[RT thread]` | SC admission + in-flight counter; gate는 control mutex/wait/allocation/I/O 없음. 중단 또는 미준비 상태면 요청 구간 무음. Lease는 C++ 예외 및 Windows SEH에서도 반납; JUCE/플러그인 내부 실행 특성을 보장하지는 않음 |
| VSTChain | `prepareToPlay`, `releaseResources`, `ScopedProcessingSuspension` | `[Non-RT control / Device lifecycle thread]` | `graphControlLock_ -> chainLock_`; close admission and drain active rendering before state/resource changes. Nested scopes resume only after the outer mutation completes |
| VSTChain | `addPlugin`, `removePlugin`, `movePlugin` | `[Message thread]` | `graphControlLock_ -> chainLock_` 보호. `rebuildGraph(true)` |
| VSTChain | `setPluginBypassed` | `[Message thread]` | `graphControlLock_ -> chainLock_` + `rebuildGraph(false)` |
| VSTChain | `audioProcessorChanged`, `refreshPendingPluginLatency` | `[Plugin callback]` -> `[Message thread]` | 지연 변경 콜백은 atomic flag만 설정하고, 30Hz 상태 갱신이 활성 체인 PDC가 달라진 경우에만 graph render sequence를 재빌드 |
| VSTChain | `replaceChainAsync`, `cancelPendingAsyncLoad` | `[Message thread]` -> `[BG thread]` -> `[Message thread]` | DLL 로딩은 BG, graph 삽입은 callAsync; generation으로 취소 결과 폐기 |
| VSTChain | `replaceChainWithPreloaded` | `[Message thread]` | staging 시작 전 admission 차단·render drain. 준비/상태 복원 실패 시 구 chain 유지. 성공 로그/UI 알림은 swap scope 뒤 실행 |
| OutputRouter | `routeAudio` | `[RT thread]` | atomic 볼륨/활성화. scaledBuffer_ 용량 클램프 |
| MonitorOutput | `writeAudio` | `[RT thread]` | AudioRingBuffer producer. SC admission + in-flight counter only; no mutex, wait, or allocation |
| MonitorOutput | `audioDeviceIOCallbackWithContext` | `[Monitor RT thread]` | AudioRingBuffer consumer. JUCE `audioCallbackLock` serializes it against `audioDeviceAboutToStart` and `removeAudioCallback` |
| MonitorOutput | `audioDeviceAboutToStart` | `[Device lifecycle thread]` | Runs under JUCE `audioCallbackLock`; closes producer admission and drains an in-flight write before ring reset |
| MonitorOutput | `initialize`, `shutdown`, `setDevice`, `checkReconnection` | `[Message thread]` | 선택 장치를 직접 open. Close producer admission and drain the bounded in-flight write before ring reset/resize; lifecycle generations reject stale deferred device work; 실패한 zero-active/reconnect는 약 3초 쿨다운 재적용; JUCE callback removal drains the consumer |
| AudioRingBuffer | `write` (producer) | `[RT thread]` | SPSC. capacity는 power-of-2 필수 |
| AudioRingBuffer | `read` / `readInterpolated` (consumer) | `[Monitor RT thread]` | SPSC 단일 소비자 |
| AudioRecorder | `writeBlock` | `[RT thread]` | try-lock 후 ThreadedWriter FIFO에 push, lifecycle/FIFO 경합 시 drop counter 증가. 성공한 samples만 duration 반영 |
| AudioRecorder | `startRecording`, `stopRecording` | `[Message / Device lifecycle thread]` | `writerLock_` (SpinLock) 하나로 writer·generation·recording publication/teardown 선형화 |
| LatencyMonitor | `markCallbackStart/End` | `[RT thread]` | `sampleRate_`, `bufferSize_`, `callbackStartTicks_`, `avgProcessingTime_` 모두 atomic (reset()과의 cross-thread 안전) |
| LatencyMonitor | `reset` | `[Device lifecycle thread]` | audioDeviceAboutToStart에서 호출. atomic store(relaxed) |
| LatencyMonitor | `get*Ms`, `getCpuUsagePercent` | `[Message thread]` | atomic read |
| PluginPreloadCache | `preloadAllSlots` | `[Message thread]` -> `[BG thread]` | BG 스레드에서 DLL 로딩. `cacheMutex_`로 캐시 보호 |
| PluginPreloadCache | `take`, `isCached` | `[Message thread]` | `cacheMutex_` 보호 |
| PluginPreloadCache | `invalidateAll` | `[Message thread]` | non-blocking: `slotVersions_` bump + `cancelPreload_` |
| SafetyLimiter | `process()` | `[RT audio]` | Atomics only, no alloc/mutex/logging |
| SafetyLimiter | `set*/get*` | `[Any thread]` | Atomic reads/writes |
| `BuiltinFilter` | `processBlock()` | `[RT audio]` | IIR 필터 적용. atomic freq 읽기, setCoefficients (스택 연산) |
| `BuiltinFilter` | `setters` | `[Any thread]` | atomic 쓰기 |
| `BuiltinNoiseRemoval` | `processBlock()` | `[RT audio]` | FIFO + rnnoise_process_frame. 힙 할당 없음 |
| `BuiltinNoiseRemoval` | `prepareToPlay/release` | `[Non-RT control / Device lifecycle]` | rendering 제외 상태에서 rnnoise_create (malloc) / rnnoise_destroy |
| `BuiltinAutoGain` | `processBlock()` | `[RT audio]` | K-weighting sidechain + 증분 LUFS + 게인 적용 |
| `BuiltinAutoGain` | `prepareToPlay` | `[Non-RT control / Device lifecycle]` | rendering 제외 상태에서 링버퍼 할당, K-weighting 계수 계산 |
| PluginLoadHelper | `createPluginOnCorrectThread` | `[BG thread]` / `[Message thread]` | macOS: BG->메시지 스레드 디스패치. Windows/Linux: 호출 스레드에서 직접 |

---

## Resource Lifecycle (리소스 수명 관리)

| 리소스 | 생성 | 소유 | 파괴 | 비고 |
|--------|------|------|------|------|
| `AudioDeviceManager` (deviceManager_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | 메인 오디오 디바이스 |
| `VSTChain` (vstChain_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | |
| `AudioProcessorGraph` (graph_) | VSTChain 생성자 | VSTChain (unique_ptr) | VSTChain 소멸자 | prepare/release는 graph의 처리 리소스를 준비/해제하며 graph 객체와 chain 소유권은 유지 |
| `PluginSlot.instance` | VSTChain::addPlugin / replaceChainAsync | graph_ 노드가 소유 | graph_->removeNode() | 에디터 열려있으면 먼저 닫을 것 |
| `DocumentWindow` (editorWindows_) | VSTChain::openPluginEditor | VSTChain (unique_ptr 벡터) | closePluginEditor / 소멸자 | Message thread only |
| `OutputRouter` | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | scaledBuffer_ 사전 할당 |
| `MonitorOutput` (monitorOutput_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | 별도 AudioDeviceManager 소유 (unique_ptr) |
| `AudioRingBuffer` | MonitorOutput 생성자 | MonitorOutput (stack) | MonitorOutput 소멸자 | capacity는 power-of-2 |
| `AudioRecorder` (recorder_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | ThreadedWriter는 startRecording에서 생성 |
| `SharedMemWriter` (sharedMemWriter_) | AudioEngine 생성자 | AudioEngine (stack) | AudioEngine 소멸자 | legacy와 fan-out 매핑을 독립적으로 open; 공통 connected_/in-flight gate로 종료 보호 |
| `workBuffer_` | audioDeviceAboutToStart | AudioEngine | audioDeviceAboutToStart에서 setSize + clear | 8ch 사전 할당, RT 스레드 전용 |
| `PluginPreloadCache` | PresetManager 생성자 | PresetManager (멤버) | PresetManager 소멸 시 | BG 스레드 프리로드, cacheMutex_ 보호 |
| `loadThread_` (VSTChain) | prepareChainAsync | VSTChain (unique_ptr) | 다음 worker가 이전 thread를 join하거나 소멸 시 join | asyncGeneration_으로 stale 폐기; 활성 constructor join을 새 BG worker로 이전 |

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
- MonitorOutput also uses independent reconnect state for retryable hotplug/device errors (`monitorLost_` + own cooldown). Failed reconnect and zero-active recovery re-arm the full three-second cooldown; sample-rate mismatch is treated as a disabled configuration state, not a retry loop.

### Preset Loading Flow

`PresetManager::loadSlotAsync`는 같은 구조이면 기존 인스턴스의 상태를 동기 복원하고,
유효한 캐시가 있으면 외부 DLL 생성을 생략한다. 다른 구조의 캐시 미스는 다음 순서다.

```
MainComponent/slot UI: loadingSlot_ = true
  -> prepareChainAsync: generation 증가, 외부 플러그인 BG 생성
     (macOS 생성은 message thread에 dispatch; 이 단계에서는 구 chain 처리 유지)
  -> alive + generation 확인 후 message-thread 완료 콜백
  -> graphControlLock_ -> admission 차단 -> 실행 중 render drain -> chainLock_
     -> 새 노드 staging + built-in 생성 + state 복원
     -> 실패: staged 노드 제거, 구 chain 유지
     -> 성공: 구 노드 제거, 새 chain 확정, 한 번의 연결 재빌드
  -> scope 종료: outer scope/manual suspension이 없으면 admission 재개
  -> 완료 결과에 따라 슬롯 UI와 loadingSlot_/partialLoad_ 갱신
```

- gate가 닫힌 동안 새 RT 콜백은 기다리지 않고 요청 구간을 무음 처리한다. 프리로드는
  DLL 생성 시간을 줄이지만 state 복원·graph commit의 시간, 클릭/무음 없는 전환을 보장하지 않는다.
- 동기 `applySlowPath`는 개별 remove/add와 마지막 state batch에 각각 suspension을 둔다.
  이 경로 전체가 하나의 원자적 chain 교체인 것은 아니다.
- 새 BG worker가 이전 `loadThread_`를 join한다. 활성 loader가 있으면 직접 캐시 swap은
  실패를 반환해 async 경로로 넘기며 message thread에서 constructor 종료를 기다리지 않는다.
- `asyncGeneration_`으로 취소/이전 결과를 폐기하고 `onPluginLoadFailed`로 외부 플러그인
  생성 오류를 알린다. 실패 후 `partialLoad_`는 원본 슬롯의 자동 덮어쓰기를 막는 보호 상태다.

### Receiver 전송 (4.4.0)

- `Local\DirectPipeAudio`의 기존 v1 매핑/192-byte ABI와 `Local\DirectPipeDataReady`
  event는 유지한다. 구 Receiver는 기존 단일 소비자 경로를 사용한다.
- 새 매핑 `Local\DirectPipeAudioFanOutV1`은 자체 protocol version 1과 최대 8개의
  독립 SPSC queue를 사용한다. 4.4.0 Receiver가 서로 같은 read cursor를 경쟁하지 않는다.
- 한 번 interleave한 블록을 활성 queue에 복사한다. 새 claim은 producer ack 뒤 읽기 가능하며,
  panic/crash fast path 또는 IPC 비활성 상태에서 writer가 실행되지 않으면 ack도 진행되지 않는다.
- writer drop 수는 연결된 각 수신 경로의 손실 합계다. legacy 청취자가 없을 때의 FIFO 포화는
  손실로 집계하지 않으며, 이 합계는 장치 XRun 수나 단일 오디오 경로의 손실량과 다르다.
- 실제 OBS 동시 연결·장시간 청취·장치 clock drift는 별도 검증 영역이다. 자동화된 queue 격리
  검사가 실제 오디오의 무손실을 보장하지 않으며, Receiver 샘플레이트 변환은 추가되지 않았다.

---

## Troubleshooting (트러블슈팅)

| 증상 | 진단 경로 | 핵심 파일:위치 |
|------|----------|--------------|
| 오디오 글리치/드롭아웃 | RT 콜백에서 할당/락 확인 → `audioDeviceIOCallbackWithContext` 내부 검사 | `AudioEngine.cpp` RT 콜백 |
| 디바이스 연결 끊김 반복 | `desiredInputDevice_` vs 실제 디바이스 비교 → `intentionalChange_` 플래그 확인 → fallback 감지 로직 | `AudioEngine.cpp:checkReconnection` |
| 모니터 출력 안 됨 | `monitorLost_` atomic 확인 → AudioRingBuffer fill/trim 상태 확인 → 별도 shared-mode 디바이스 상태 | `MonitorOutput.cpp` |
| 프리셋 로딩 중 오디오 끊김 | replaceChainAsync의 Keep-Old-Until-Ready 패턴 확인 → graph swap 시점 | `VSTChain.cpp:replaceChainAsync` |
| 프리셋 로딩 실패 | `partialLoad_` 플래그 → 어떤 플러그인이 실패했는지 로그 확인 → DLL 경로/포맷 | `VSTChain.cpp:replaceChainAsync` |
| XRun 카운트 증가 | 버퍼 크기 확인 → processBlock 처리 시간 → CPU 우선순위 | `LatencyMonitor.cpp` |
| IPC 데이터 안 감 | `ipcEnabled_` → `connected_` → SharedMem 매핑 상태 → Receiver VST 버퍼 설정 | `SharedMemWriter.cpp` |

---

## DANGER ZONES

> 이 섹션을 수정할 때 CLAUDE.md의 Coding Rules와 해당 .h 파일의 Thread Ownership 어노테이션도 함께 업데이트할 것

1. **RT 콜백에서 heap 할당/mutex 금지**: `audioDeviceIOCallbackWithContext`에서 `new`, `malloc`, `std::mutex::lock` 등 사용 시 glitch 발생. `workBuffer_`는 `audioDeviceAboutToStart`에서 사전 할당. `emptyMidi_`도 `prepareToPlay`에서 사전 할당.

2. **VSTChain control lock을 `processBlock`에서 잡지 말 것**: RT 스레드에서 `graphControlLock_` 또는 `chainLock_`를 잡으면 device/message thread의 lifecycle 및 플러그인 로딩과 교착·glitch가 발생한다. `processBlock`은 lock-free 경로를 유지해야 함.

3. **VSTChain lock 순서와 logging**: non-RT graph 변경은 항상 `graphControlLock_ -> chainLock_` 순서다. 두 lock 안에서 `writeToLog`를 호출하지 말고 문자열만 캡처한 뒤 lock 해제 후 기록한다.

4. **AudioRingBuffer capacity는 반드시 power-of-2**: `initialize()` 시 assertion으로 강제. 위반 시 mask 연산이 깨져서 데이터 손상.

5. **OutputRouter `routeAudio`에서 `numSamples` 클램프 필수**: `scaledBuffer_` 용량을 초과하면 buffer overrun. `bufferTruncated_` 플래그로 진단.

6. **SharedMemWriter `shutdown()` 순서**: 공통 `connected_=false` -> in-flight writer drain 뒤에 양쪽 producer를 비활성화하고 view/event/mapping을 해제한다. legacy와 fan-out 중 한쪽만 초기화됐어도 같은 종료 경계를 지킨다. admission/drain 전에 unmap하면 dangling pointer 접근 위험.

7. **IPC 토글 race window**: `setIpcEnabled(false)` 후에도 RT 스레드가 `ipcEnabled_=true`를 읽을 수 있음. `interleaveBuffer_`를 `shutdown()`에서 해제하면 안 되는 이유.

8. **MonitorOutput reconnection**: retryable device loss is set from `audioDeviceError`/external `audioDeviceStopped` and cleared by a successful `audioDeviceAboutToStart`. Initialization targets the selected shared-mode output directly, failed recovery re-arms the three-second cooldown, intentional monitor teardown is ignored, JUCE auto-fallback devices are rejected, and sample-rate mismatch stays disabled until the main sample rate or selected monitor changes.

9. **PluginPreloadCache `invalidateAll()`은 thread join 하지 않음**: COM STA 데드락 방지. `cancelPreload_` + `slotVersions_` bump로 non-blocking 무효화.

10. **RMS decimation counter**: `rmsDecimationCounter_`는 RT 스레드 전용 변수 (atomic 불필요). 다른 스레드에서 접근하면 data race.

11. **Suspension ownership**: JUCE 7.0.12 `suspendProcessing(bool)` is a boolean flag, and direct `AudioProcessorGraph::processBlock` calls do not enforce the root flag. DirectPipe therefore owns an atomic admission/in-flight handshake. Public `VSTChain::suspendProcessing(bool)` stays idempotent (`true -> true -> false` resumes); `ScopedProcessingSuspension` separately tracks nested non-RT mutations and resumes on scope exit, including C++ exceptions. The control thread drains rendering before touching live plug-in state; RT never waits. A plug-in that never returns can still prevent safe state mutation; this is not plug-in process isolation.

12. **`UpdateKind::async` vs 기본(sync)**: staged node/connection 변경은 `UpdateKind::async`로 재빌드를 모아 마지막 연결에서 한 번 확정한다. 구조 변경은 별도로 admission/drain으로 보호해야 한다. 중간 sync는 불필요한 재빌드를 만들며, admission을 닫지 않는 연결 변경에서는 중간 routing을 발행할 수 있다.

13. **`AudioProcessorGraph` render sequence**: JUCE atomically swaps render sequences for connection changes. That does not serialize plug-in state restoration or lifecycle work against an already running callback. Those mutations require DirectPipe's scoped admission/drain boundary. Callbacks emit silence while it is closed; the duration depends on the plug-in state/graph operation and is not a gap-free switching guarantee. Pure bypass connection changes retain the existing unsuspended path.

14. **JUCE `Timer` 스레딩 규칙**: `juce::Timer`는 Message thread에서만 생성/파괴 가능. `stopTimer()`는 어느 스레드에서나 안전하지만, `Timer` 객체를 소유한 `unique_ptr.reset()`은 반드시 Message thread에서만 호출. 위반 시 타이머 내부 리스트 손상 → 크래시.

15. **JUCE `SafePointer` 스레딩**: `Component::SafePointer` 생성(WeakReference 등록)은 스레드 안전하지 않음. **반드시 Message thread에서만 생성**. BG 스레드에서 생성하면 Component의 master reference와 data race. BG 스레드에서 callAsync 보호가 필요하면 `shared_ptr<atomic<bool>> alive_` 패턴 사용.

16. **JUCE `String` 스레딩**: `juce::String`은 내부적으로 COW(Copy-On-Write) 참조 카운팅 사용. 두 스레드에서 동시에 같은 String 객체를 읽기/쓰기하면 참조 카운트 data race. `desiredInputDevice_` 같은 String 멤버는 한 스레드에서만 접근하거나, 접근 시 mutex 보호 필요.

17. **JUCE `File::moveFileTo` 동작**: 대상 파일이 이미 존재하면 먼저 `deleteFile()` 후 이동. POSIX `rename()`과 달리 atomic하지 않음 (delete + move 두 단계). `atomicWriteFile`의 .bak 경로가 동작하는 이유.

---

## Current Edge Guards

- Monitor adaptive target, PLL ratio limits, and emergency trim fallback live in `MonitorDriftPolicy.h`; normal drift must use fractional playback rather than frame discard, and monitor MMCSS must stay below main OUT priority.
- XRun display history is a 60-second rolling window. If message-thread work stalls the UI timer, `updateXRunTracking()` must advance every elapsed bucket so stale XRuns age out on time.
- Startup settings restore calls `rememberRestoredDeviceTargets()` so saved WASAPI input/output names remain the reconnection target even when Windows has not finished enumerating devices at login.
- `AudioRingBuffer`, `OutputRouter`, `MonitorOutput`, `SharedMemWriter`, and `AudioRecorder` explicitly guard zero-channel, null-channel, and short-source-buffer calls so fallback silence paths do not reuse stale audio or read past source buffers.

## When to Update This README

- Audio/ 디렉토리에 파일을 추가하거나 삭제할 때 File List 업데이트
- RT 콜백의 데이터 흐름이 변경될 때 (새 출력 대상 추가, 처리 순서 변경 등) Data Flow 다이어그램 업데이트
- 스레드 모델이 변경될 때 (새 lock 추가, atomic 변수 추가, 스레드 소유권 변경) Thread Model 테이블 업데이트
- 새로운 안전 규칙이 발견되거나 기존 DANGER ZONE이 변경될 때 해당 항목 업데이트
