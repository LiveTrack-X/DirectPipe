# DirectPipe 제품 기획서 / Product Specification

> 현재 구현된 모든 기능의 상세 동작 방식을 기술한 역기획서입니다. 사용법은 [User Guide](USER_GUIDE.md), 아키텍처 개요는 [Architecture](ARCHITECTURE.md) 참조.
>
> A reverse-engineered specification documenting the detailed behavior of all currently implemented features. For usage see [User Guide](USER_GUIDE.md), for architecture overview see [Architecture](ARCHITECTURE.md).

> 역기획서 — 현재 구현된 기능을 기반으로 작성 (v4.0.1 기준)
>
> Reverse spec — written based on currently implemented features (as of v4.0.1)

---

## 1. 제품 개요 / Product Overview

### 제품명 / Product Name
DirectPipe

### 한 줄 설명 / One-Line Description
DAW 없이, 설치 없이, 마이크에 VST 이펙트를 거는 가장 가벼운 방법 (Windows / macOS / Linux)

The lightest way to apply VST effects to a microphone — no DAW, no installation (Windows / macOS / Linux)

### 버전 / Version
4.0.1

### 개발 배경 / Background
- DAW(Reaper, Ableton 등)를 마이크 이펙트 용도로 구동하는 것은 자원 낭비 / Running a DAW (Reaper, Ableton, etc.) just for mic effects is a waste of resources
- LightHost를 대안으로 사용했으나 2016년 이후 업데이트 중단 / LightHost was used as an alternative but hasn't been updated since 2016
- 스트리밍 및 방송 마이크 세팅 작업 중 겪은 불편함을 직접 해결 / Solved pain points encountered while setting up streaming/broadcasting mics
  - 방송 중 마우스 조작 불가 → 핫키/스트림덱 필요 / Can't use mouse during broadcast → need hotkeys/Stream Deck
  - 방송 사고 → 패닉 뮤트 필요 / On-air accidents → need panic mute
  - 상황별 세팅 전환 → 프리셋 슬롯 필요 / Situation-based setting changes → need preset slots
  - OBS에 깨끗한 오디오 전달 → DirectPipe Receiver 필요 / Clean audio to OBS → need DirectPipe Receiver
  - 자기 목소리 확인 → 독립 모니터 출력 필요 / Hearing your own voice → need independent monitor output

### 라이선스 / License
GPL v3 (오픈소스 / open source)

### 플랫폼 / Platform
- **Windows** 10/11 (64-bit) — fully supported / 정식 지원
- **macOS** 10.15+ (Apple Silicon & Intel universal binary) — beta / 베타 (빌드 최소 10.15, 권장 13+ / build min 10.15, recommended 13+)
- **Linux** (Ubuntu 22.04+, x86_64) — experimental / 실험적

### 배포 형태 / Distribution
- `DirectPipe.exe` — 메인 호스트 (단일 실행 파일) / Main host (single executable)
- `DirectPipe Receiver.dll` — Receiver 플러그인 (VST2/VST3/AU) / Receiver plugin (VST2/VST3/AU)
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 플러그인 패키지 / Stream Deck plugin package
- 릴리즈: Windows `DirectPipe-vX.Y.Z-Windows.zip` (exe + dll + vst3), macOS `DirectPipe-vX.Y.Z-macOS.dmg`, Linux `DirectPipe-vX.Y.Z-Linux.tar.gz` / Release: Windows `.zip`, macOS `.dmg`, Linux `.tar.gz`

---

## 2. 타겟 사용자 / Target Users

| 사용자 유형 / User Type | 설명 / Description | 핵심 니즈 / Core Needs |
|------------|------|----------|
| **스트리머 / Streamers** | Twitch, YouTube 등 실시간 방송자 / Live broadcasters on Twitch, YouTube, etc. | 방송 중 원클릭 마이크 제어, 이펙트 전환 / One-click mic control and effect switching during broadcast |
| **팟캐스터 / Podcasters** | 녹음/실시간 팟캐스트 진행자 / Recording/live podcast hosts | 노이즈 제거, EQ, 컴프레서 상시 적용 / Always-on noise removal, EQ, compressor |
| **게이머 / Gamers** | Discord, 팀스피크 음성채팅 사용자 / Discord, TeamSpeak voice chat users | 백그라운드에서 저자원으로 마이크 처리 / Low-resource mic processing in the background |
| **세팅 도우미/전문가 / Setup Helpers/Experts** | 타인의 마이크 세팅을 해주는 사람 / People who configure mic settings for others | 포터블 실행, 프리셋 전달, 빠른 설정 / Portable execution, preset sharing, quick setup |
| **보이스챗 사용자 / Voice Chat Users** | Zoom, Teams 등 화상회의 참가자 / Zoom, Teams video conference participants | 깨끗한 음성, 간편한 설정 / Clean voice, easy setup |

---

## 3. 핵심 가치 / Core Values

### 3.1 가벼움 / Lightweight
- 단일 exe 파일, 설치 불필요 / Single exe file, no installation required
- 시스템에 드라이버/가상 장치를 설치하지 않음 / No drivers or virtual devices installed on the system
- 포터블 모드 지원 (exe 옆에 `portable.flag` 파일 → 설정을 `./config/`에 저장) / Portable mode supported (`portable.flag` file next to exe -> config stored in `./config/`)
- `HIGH_PRIORITY_CLASS`로 실행, 최소 자원으로 최대 성능 / Runs as `HIGH_PRIORITY_CLASS`, maximum performance with minimal resources

### 3.2 단순한 구조 / Simple Architecture
```
마이크 입력 → VST 플러그인 체인 → 출력
Mic Input  → VST Plugin Chain   → Output
```
- 이 한 줄이 전부. 멀티채널 믹서가 아님 / That one line is everything. Not a multi-channel mixer
- 열면 바로 이해되는 2열 레이아웃 / Two-column layout that's immediately understandable upon opening

### 3.3 방송 중 제어 / Live Broadcast Control
- 5종 외부 제어: 핫키, MIDI, Stream Deck, HTTP API, WebSocket / 5 types of external control: Hotkeys, MIDI, Stream Deck, HTTP API, WebSocket
- 방송 중 마우스에 손대지 않고 모든 것을 조작 가능 / Control everything without touching the mouse during broadcast

### 3.4 하드웨어 독립 / Hardware Independent
- 어떤 USB 마이크, 오디오 인터페이스든 사용 가능 / Works with any USB microphone or audio interface
- 특정 하드웨어에 종속되지 않음 / Not locked to any specific hardware

---

## 4. 기능 명세 / Feature Specification

### 4.1 오디오 엔진 / Audio Engine

#### 4.1.1 입력 / Input
| 항목 / Item | 상세 / Details |
|------|------|
| 드라이버 타입 / Driver Type | **Windows**: 5종 — DirectSound (레거시 / legacy), WASAPI Shared (권장 / recommended), WASAPI Low Latency (IAudioClient3), WASAPI Exclusive, ASIO. **macOS**: CoreAudio. **Linux**: ALSA, JACK |
| 드라이버 전환 / Driver Switching | 런타임 전환 가능 / Runtime switching supported. ASIO (Windows): 단일 장치, 동적 SR/BS, 채널 라우팅 / single device, dynamic SR/BS, channel routing. WASAPI: 입출력 분리 가능 / separate input/output possible |
| ASIO SR/BS 정책 / ASIO SR/BS Policy | ASIO 장치는 SR/BS를 전역으로 소유 (장치를 공유하는 모든 앱에 영향). 시작 시 저장된 SR/BS를 ASIO에 강제하지 않고, 장치가 보고하는 현재 값을 수용 (`syncDesiredFromDevice()`). 이유: SR/BS 강제 시 ASIO 드라이버 재시작 → 다른 앱의 오디오 끊김. ASIO 컨트롤 패널에서 변경한 BS도 설정에 자동 반영. WASAPI/CoreAudio/ALSA는 앱별 SR/BS이므로 저장된 값 강제 적용 (다른 앱에 영향 없음) / ASIO devices own SR/BS globally (affects all apps sharing the device). On startup, DirectPipe does NOT force saved SR/BS on ASIO — instead accepts whatever the device currently reports (`syncDesiredFromDevice()`). Reason: forcing SR/BS would restart the ASIO driver, disrupting audio in DAWs, media players, and other apps. ASIO control panel BS changes are automatically saved to settings. WASAPI/CoreAudio/ALSA use per-app SR/BS, so saved values are safely forced (no impact on other apps) |
| 시작 흐름 / Startup Flow | WASAPI로 먼저 시작 (안전한 폴백) → 설정 파일에서 드라이버 타입 로드 → ASIO 설정 시 전환 시도 (~100ms, 창 표시 전 완료). ASIO 실패 시 WASAPI에 남아있음 / Opens WASAPI first (safe fallback) → loads saved driver type from settings → switches to ASIO if configured (~100ms, before window shown). Falls back to WASAPI if ASIO fails |
| 채널 모드 / Channel Mode | Mono / Stereo (기본값 / default: Stereo, `channelMode_` = 2) |
| 입력 게인 / Input Gain | 0.0x ~ 2.0x (기본 / default 1.0x). `|gain - 1.0f| > 0.001f`일 때만 SIMD 적용 / SIMD applied only when gain differs from 1.0 |
| 샘플레이트 / Sample Rate | 장치 지원 범위 (일반적 44.1kHz ~ 192kHz) / Device-supported range (typically 44.1kHz ~ 192kHz) |
| 버퍼 크기 / Buffer Size | 장치 지원 범위. 자동 폴백: 장치가 요청 크기를 거부하면 가장 가까운 지원 크기 선택 후 NotificationBar 알림 / Device-supported range. Auto-fallback: if device rejects requested size, selects closest supported size and shows NotificationBar alert |

#### 4.1.2 오디오 콜백 처리 흐름 / Audio Callback Processing Flow
```
1. ScopedNoDenormals 적용 (비정규 부동소수점 방지) / Apply ScopedNoDenormals (prevent denormal floats)
2. 입력 → 사전 할당된 8채널 workBuffer에 복사 (힙 할당 없음) / Input → copy to pre-allocated 8-ch workBuffer (no heap alloc)
3. Mono 모드: 모든 입력을 채널 0에 합산 → 채널 1에 복제 / Mono mode: sum all inputs to ch0 → duplicate to ch1
   Stereo 모드: 그대로 복사 / Stereo mode: copy as-is
4. 입력 게인 적용 (변경 시에만) / Apply input gain (only when changed)
5. RMS 레벨 계산 (4회 콜백마다 1회 = ~23Hz @48kHz/512) / RMS level calculation (every 4th callback = ~23Hz @48kHz/512)
6. VST 체인 processBlock() 통과 (SEH crash guard: Windows `__try/__except`, 기타 `try/catch`)
   / Pass through VST chain processBlock() (SEH crash guard: Windows `__try/__except`, others `try/catch`)
   - 예외 발생 시: 버퍼 클리어, `chainCrashed_` 플래그 설정, 이후 모든 콜백 무음 출력
     / On exception: clear buffer, set `chainCrashed_` flag, silent output for all subsequent callbacks
   - 메시지 스레드에서 지연 알림 (NotificationBar) / Deferred notification from message thread (NotificationBar)
7. Safety Limiter 적용 (feed-forward, 0.1ms attack / 50ms release) / Apply Safety Limiter (feed-forward, 0.1ms attack / 50ms release)
8. AudioRecorder에 lock-free 쓰기 (녹음 중일 때) / Lock-free write to AudioRecorder (when recording)
9. SharedMemWriter에 IPC 쓰기 (IPC 활성화 시) / IPC write to SharedMemWriter (when IPC enabled)
10. OutputRouter → 모니터 출력 (별도 WASAPI 장치) / OutputRouter → monitor output (separate WASAPI device)
11. 메인 출력: outputChannelData에 직접 memcpy / Main output: direct memcpy to outputChannelData
12. 출력 RMS 레벨 계산 (데시메이션) / Output RMS level calculation (decimated)
```

#### 4.1.3 출력 경로 (3가지 + 녹음) / Output Paths (3 + Recording)

3가지 출력 경로는 모두 **독립적으로 켜기/끄기 및 볼륨 조절**이 가능하다. OUT/MON/VST 버튼 또는 외부 제어(핫키, MIDI, Stream Deck, HTTP API)로 각 경로를 개별 제어하여, 예를 들어 OBS 마이크만 끄고 Discord는 유지하거나 그 반대도 가능하다. Panic Mute(Ctrl+Shift+M)로 전체를 즉시 차단할 수 있으며, 해제 시 이전 ON/OFF 상태가 자동 복원된다.

All 3 output paths can be **independently toggled and volume-adjusted**. Use OUT/MON/VST buttons or external controls (hotkeys, MIDI, Stream Deck, HTTP API) to individually control each path -- e.g., mute OBS mic while keeping Discord active, or vice versa. Panic Mute (Ctrl+Shift+M) instantly kills all outputs; previous ON/OFF states auto-restore on unmute.

All 3 output paths can be **independently toggled and volume-adjusted**. Use OUT/MON/VST buttons or external controls (hotkeys, MIDI, Stream Deck, HTTP API) to independently control each path — e.g., mute OBS mic while keeping Discord active, or vice versa. Panic Mute (Ctrl+Shift+M) kills all outputs instantly and stops active recording; previous ON/OFF states auto-restore on unmute (recording does not auto-restart). During panic, all actions (bypass, volume, preset, gain, recording, plugin parameters) are blocked.

| 경로 / Path | 설명 / Description | 기술 / Technology | 제어 / Control |
|------|------|------|------|
| **Main Output** | 메인 출력 (스피커/가상 케이블) / Main output (speakers/virtual cable) | AudioSettings의 Output 장치에 직접 쓰기. WASAPI/ASIO 모두 지원 / Direct write to AudioSettings Output device. Both WASAPI/ASIO supported | OUT 버튼, ToggleMute, SetVolume |
| **Monitor Output** | 헤드폰 모니터링 (자기 목소리 확인) / Headphone monitoring (hear your own voice) | 별도 WASAPI AudioDeviceManager + lock-free AudioRingBuffer (4096 프레임, 스테레오, power-of-2) / Separate WASAPI AudioDeviceManager + lock-free AudioRingBuffer (4096 frames, stereo, power-of-2) | MON 버튼, MonitorToggle, SetVolume |
| **IPC Output** | OBS용 DirectPipe Receiver / DirectPipe Receiver for OBS | SharedMemory 기반 IPC. 공유 메모리 이름: `Local\\DirectPipeAudio`. 인터리브 float 형식. POSIX sem/shm 퍼미션 0600 (owner-only) / SharedMemory-based IPC. Shared memory name: `Local\\DirectPipeAudio`. Interleaved float format. POSIX sem/shm permissions 0600 (owner-only) | VST 버튼, IpcToggle |
| **Recording** | WAV 녹음 (VST 체인 이후) / WAV recording (after VST chain) | AudioRecorder, lock-free ThreadedWriter | REC 버튼, RecordingToggle |

#### 4.1.4 오디오 최적화 / Audio Optimizations
| 최적화 / Optimization | 상세 / Details |
|--------|------|
| 타이머 해상도 / Timer Resolution | Windows: `timeBeginPeriod(1)` — 1ms (종료 시 `timeEndPeriod(1)` 복원 / restored on shutdown). macOS/Linux: platform timer APIs |
| 비정규 방지 / Denormal Prevention | `ScopedNoDenormals` — 매 콜백마다 적용 / applied every callback |
| Power Throttling | Windows: Intel 하이브리드 CPU E-코어 할당 방지 (시작 시 비활성화) / Prevents Intel hybrid CPU E-core assignment (disabled at startup). macOS/Linux: N/A |
| RMS 데시메이션 / RMS Decimation | `rmsDecimationCounter_` — 4콜백마다 1회 계산 (CPU 절약) / calculated every 4th callback (CPU savings) |
| 뮤트 fast-path / Mute Fast-Path | 뮤트 상태에서 VST 처리 스킵 / Skip VST processing when muted |
| 워크버퍼 / Work Buffer | 8채널 사전 할당 (`workBuffer_`). 콜백에서 힙 할당 없음 / 8-channel pre-allocated (`workBuffer_`). No heap allocation in callback |
| 프로세스 우선순위 / Process Priority | `HIGH_PRIORITY_CLASS` |
| MMCSS 스레드 등록 / MMCSS Thread Registration | Windows: 오디오 콜백 스레드를 "Pro Audio" MMCSS에 AVRT_PRIORITY_HIGH로 등록 (WASAPI + ASIO 모두). DPC 지연 간섭 방지 / Registers audio callback thread to "Pro Audio" MMCSS at AVRT_PRIORITY_HIGH (both WASAPI + ASIO). Prevents DPC latency interference. MMCSS 함수 포인터는 `audioDeviceAboutToStart`에서 1회 캐시 (release/acquire 순서), RT 콜백에서 읽기만 / MMCSS function pointers cached once in `audioDeviceAboutToStart` (release/acquire order), read-only in RT callback. `audioDeviceStopped`에서 핸들 적절히 revert / Handle properly reverted in `audioDeviceStopped` |
| IPC 이벤트 최적화 / IPC Event Optimization | `SetEvent` 시그널을 데이터 기록 시에만 발생 (불필요한 커널 호출 감소) / `SetEvent` signal only fired on data write (reduces unnecessary kernel calls) |
| 콜백 오버런 감지 / Callback Overrun Detection | LatencyMonitor: 처리 시간이 버퍼 주기를 초과하면 카운트 / counts when processing time exceeds buffer period (`getCallbackOverrunCount()`) |

#### 4.1.5 장치 자동 재연결 / Device Auto-Reconnection
| 항목 / Item | 상세 / Details |
|------|------|
| 감지 방식 / Detection Method | 이중 메커니즘: (1) `ChangeListener` 즉시 감지 + (2) 3초 타이머 폴링 (`checkReconnection`) / Dual mechanism: (1) `ChangeListener` immediate detection + (2) 3s timer polling (`checkReconnection`) |
| 의도 추적 / Intent Tracking | `desiredInputDevice_` / `desiredOutputDevice_` (SpinLock 보호 / SpinLock protected) / `desiredDeviceType_` / `desiredSampleRate_` / `desiredBufferSize_` 저장 / saved |
| 폴백 보호 / Fallback Protection | `intentionalChange_` 플래그: JUCE 자동 폴백을 성공적 재연결로 오인하는 것 방지 / `intentionalChange_` flag: prevents mistaking JUCE auto-fallback as successful reconnection |
| 재연결 로직 / Reconnection Logic | `attemptReconnection()`: 장치 재스캔 → 가용성 확인 → desired 설정(SR/BS/채널) 복원 / device rescan → availability check → restore desired settings (SR/BS/channels). `attemptingReconnection_` 재진입 가드 / re-entrancy guard |
| 쿨다운 / Cooldown | `reconnectCooldown_` (30Hz 타이머 틱 / 30Hz timer ticks) |
| 알림 / Notification | 연결 끊김 시 경고(주황), 재연결 시 정보(보라) NotificationBar 표시 / Warning (orange) on disconnect, Info (purple) on reconnect in NotificationBar |
| 방향별 감지 / Per-Direction Detection | `inputDeviceLost_`: 입력 장치 분실 시 오디오 콜백에서 입력 무음 처리 (폴백 마이크 방지) / silences input in audio callback on input device loss (prevents fallback mic). `outputAutoMuted_`: 출력 장치 분실 시 자동 뮤트, 복원 시 자동 해제 / auto-mutes on output device loss, auto-unmutes on restore |
| 재연결 실패 / Reconnection Failure | `reconnectMissCount_`: 5회 연속 실패(~15초) 후 현재 드라이버 장치를 수락하여 크로스 드라이버 stale name 무한 루프 방지 / after 5 consecutive failures (~15s), accepts current driver device to prevent cross-driver stale name infinite loop |
| 드라이버 전환 복원 / Driver Switch Restore | `DriverTypeSnapshot`: 드라이버 타입별 설정(입출력 장치, SR, BS) 저장/복원 / saves/restores per-driver-type settings (input/output device, SR, BS). WASAPI↔ASIO 전환 시 이전 설정 자동 복원 / auto-restores previous settings on WASAPI↔ASIO switch |
| 모니터 독립 / Monitor Independence | 모니터 장치는 별도 패턴으로 독립 재연결 / Monitor device reconnects independently with its own pattern (`monitorLost_` + 자체 쿨다운 / own cooldown) |

#### 4.1.6 XRun 모니터링 / XRun Monitoring
| 항목 / Item | 상세 / Details |
|------|------|
| 윈도우 / Window | 60초 롤링 / 60-second rolling. 순환 버퍼 / circular buffer `xrunHistory_[60]` (1초당 1슬롯 / 1 slot per second) |
| 스레드 안전 / Thread Safety | `xrunResetRequested_` atomic 플래그로 장치 스레드 → 메시지 스레드 신호 / atomic flag signals from device thread → message thread |
| 표시 / Display | 상태 바에 카운트 표시. > 0이면 빨간색 하이라이트 / Count shown in status bar. Red highlight when > 0 |
| 비지원 / Unsupported | 장치가 XRun 지원 안 하면 -1 반환 / Returns -1 if device doesn't support XRun |

#### 4.1.7 레이턴시 모니터 / Latency Monitor
| 측정값 / Metric | 설명 / Description |
|--------|------|
| `inputLatencyMs_` | 입력 버퍼 레이턴시 / Input buffer latency |
| `processingTimeMs_` | VST 체인 처리 시간 (고해상도 타임스탬프 델타) / VST chain processing time (high-resolution timestamp delta) |
| `outputLatencyMs_` | 출력 버퍼 레이턴시 / Output buffer latency |
| `cpuUsage_` | 콜백 시간 대비 처리 비율 (%) / Processing ratio relative to callback time (%) |
| 스무딩 / Smoothing | 지수 이동 평균 / Exponential moving average, `kSmoothingFactor = 0.1` |

#### 4.1.8 Safety Limiter
| 항목 / Item | 상세 / Details |
|------|------|
| 타입 / Type | 피드포워드 리미터 (look-ahead 없음, PDC 기여 없음) / Feed-forward limiter (no look-ahead, no PDC contribution) |
| 위치 / Position | VST 체인 이후, Recording/IPC/Monitor/Output 이전 / After VST chain, before Recording/IPC/Monitor/Output |
| Attack | 0.1ms |
| Release | 50ms |
| Ceiling 범위 / Ceiling Range | -6.0 ~ 0.0 dBFS (기본값 / default: -0.3 dBFS) |
| 파라미터 / Parameters | `enabled` (atomic, 기본 / default true), `ceilingdB` (atomic) |
| UI | PluginChainEditor: ceiling 슬라이더 (Add Plugin 위) + Safety Limiter 토글 버튼. 상태 바 [LIM] 표시 / ceiling slider (above Add Plugin) + Safety Limiter toggle button. Status bar [LIM] indicator |
| GR 피드백 / GR Feedback | atomic으로 UI에 전달 / Delivered to UI via atomic |

#### 4.1.9 Plugin Latency Data (PDC)
| 항목 / Item | 상세 / Details |
|------|------|
| 소스 / Source | `VSTChain::getPluginLatencies()` + `getTotalChainPDC()` — chainLock_ 하에서 각 플러그인의 PDC 조회 / queries each plugin's PDC under chainLock_ |
| UI | Per-plugin latency display와 chain PDC summary는 UX 피드백으로 UI에서 제거됨 / Per-plugin latency display and chain PDC summary removed from UI (UX feedback) |
| 보상 / Compensation | AudioProcessorGraph가 PDC 보상을 자동 처리 / AudioProcessorGraph handles PDC compensation automatically |
| 상태 전파 / State Propagation | StateBroadcaster: `plugins[].latency_samples`, `chain_pdc_samples`, `chain_pdc_ms` (API에서 여전히 사용 가능 / still available via API) |

#### 4.1.10 Built-in Processors
VST 플러그인과 동일하게 AudioProcessorGraph에 삽입 가능한 내장 프로세서 3종.

3 built-in processors that can be inserted into the AudioProcessorGraph just like VST plugins.

| 프로세서 / Processor | 클래스 / Class | 상세 / Details |
|---------|--------|------|
| **Filter** | `BuiltinFilter` | HPF (기본 ON, 60Hz / default ON, 60Hz) + LPF (기본 OFF, 16kHz / default OFF, 16kHz). 범위 / Range: HPF 20-300Hz, LPF 4k-20kHz. IIR 필터 / IIR filters, atomic 파라미터 / atomic parameters. `isBusesLayoutSupported`: mono + stereo. `getLatencySamples()` = 0 |
| **Noise Removal** | `BuiltinNoiseRemoval` | RNNoise AI 기반 노이즈 제거 / RNNoise AI-based noise removal. 480-frame FIFO (~10ms 레이턴시 / ~10ms latency). 48kHz only (비-48kHz = 패스스루 / non-48kHz = passthrough, TODO: 리샘플링 / resampling). 듀얼 모노 / Dual mono (2 RNNoise 인스턴스 / instances). x32767 스케일링 전처리, /32767 후처리 / x32767 scaling before, /32767 after. 2-pass FIFO (in-place 버퍼 안전 / in-place buffer safety). 링 버퍼 출력 FIFO (modulo wrapping). 게이트 초기 / Gate starts CLOSED (0.0), 5프레임 워밍업 / 5-frame warmup. VAD 게이트 홀드 타임 / VAD gate hold time 300ms (`kHoldSamples`=14400 @48kHz). 게이트 스무딩 / Gate smoothing 20ms (`kGateSmooth`=0.9990). `getLatencySamples()` = 480 via `setLatencySamples()`. VAD 임계값 / VAD thresholds: Light 0.50, Standard 0.70 (기본값 / default), Aggressive 0.90 |
| **Auto Gain** | `BuiltinAutoGain` | LUFS 기반 AGC / LUFS-based AGC (WebRTC-inspired dual-envelope). Target LUFS -15.0 기본 / default (범위 / range -24~-6, 내부적으로 -6dB 오프셋 적용하여 오픈루프 오버슈트 보정 / internal -6dB offset for open-loop overshoot compensation). Low Correct 0.50 기본 / default (hold↔full correction 블렌드, 부스트 / blend, boost). High Correct 0.90 기본 / default (hold↔full correction 블렌드, 컷 / blend, cut). Max Gain 22 dB 기본 / default. ITU-R BS.1770 K-weighting 사이드체인 / sidechain (copy, 실제 오디오 미적용 / not applied to actual audio). Dual-envelope level detection: fast envelope (~10ms attack, ~200ms release) + slow LUFS window (0.4s EBU Momentary), effective = max(fast, slow). Direct gain computation (IIR gain envelope 없음 / none), per-block linear ramp으로 click-free 전환 / for click-free transitions. Freeze Level -45 dBFS (per-block RMS, NOT LUFS): freeze 시 현재 게인 유지 / holds current gain on freeze (0dB 리셋 아님 / NOT reset to 0dB), -65 dBFS 미만 시 바이패스 / bypassed below -65 dBFS. Incremental `runningSquareSum_` (O(blockSize)). lowCorr/hiCorr = hold↔full correction 블렌드 비율 (엔벨로프 속도 아님) / blend ratio between hold and full correction (NOT envelope speed) |

**[Auto] 버튼 / [Auto] Button**: 입력 게인 슬라이더 옆 특수 프리셋 슬롯 (A-E 바와 별도 위치, 인덱스 5 `PresetSlotBar::kAutoSlotIndex`). 활성 시 초록색 (green when active). 첫 클릭 시 Filter + Noise Removal + Auto Gain 기본 체인 생성, 이후 마지막 저장 상태 로드. 우클릭 → Reset to Defaults.

Special preset slot next to input gain slider (separate from A-E bar, index 5 `PresetSlotBar::kAutoSlotIndex`). Green when active. First click creates default chain (Filter + Noise Removal + Auto Gain), subsequent clicks load last saved state. Right-click → Reset to Defaults.

**Auto 프리셋 슬롯 / Auto Preset Slot**: 6번째 프리셋 슬롯 (인덱스 5). 이름 변경 불가, Next/Previous 사이클에서 제외. Reset 시 Filter + NoiseRemoval + AutoGain 기본값 복원. Factory Reset 시 Auto 슬롯도 포함 삭제 (`slot_Auto.dppreset` 와일드카드 삭제, 메시지 "(A-E + Auto)").

6th preset slot (index 5). Cannot be renamed, excluded from Next/Previous cycling. Reset restores Filter + NoiseRemoval + AutoGain defaults. Factory Reset also deletes the Auto slot (`slot_Auto.dppreset` wildcard deletion, message "(A-E + Auto)").

---

### 4.2 VST 호스팅 / VST Hosting

#### 4.2.1 플러그인 지원 / Plugin Support
| 항목 / Item | 상세 / Details |
|------|------|
| 포맷 / Format | VST2 (SDK 2.4) + VST3 |
| 스캐너 / Scanner | Out-of-process 자식 프로세스 / child process (`--scan` 인자 / argument). Dead Man's Pedal 패턴 / pattern. 10회 자동 재시도 / 10 auto-retries |
| 블랙리스트 / Blacklist | 크래시한 플러그인 자동 블랙리스트 등록. 재스캔 시 건너뜀 / Crashed plugins auto-blacklisted. Skipped on rescan |
| 스캔 UI / Scan UI | 실시간 텍스트 필터 + 칼럼 정렬 (이름/벤더/포맷) / Real-time text filter + column sorting (name/vendor/format). 프로그레스 바 / Progress bar |
| 기본 스캔 경로 / Default Scan Paths | Windows: VST2 `C:\Program Files\Common Files\VST2`, VST3 `C:\Program Files\Common Files\VST3`. macOS: `/Library/Audio/Plug-Ins/VST`, `/Library/Audio/Plug-Ins/VST3`. Linux: `~/.vst`, `~/.vst3` |
| 스캔 캐시 / Scan Cache | XML 파일로 캐시. 재스캔 불필요 / Cached as XML file. No rescan needed |
| 스캔 로그 / Scan Log | Windows: `%AppData%/DirectPipe/scanner-log.txt`, macOS: `~/Library/Application Support/DirectPipe/`, Linux: `~/.config/DirectPipe/` |
| 스캔 타임아웃 / Scan Timeout | 플러그인당 300초 (5분) / 300 seconds (5 min) per plugin |

#### 4.2.2 플러그인 체인 에디터 / Plugin Chain Editor
| 기능 / Feature | 상세 / Details |
|------|------|
| 순서 변경 / Reorder | 드래그앤드롭 (자동 그래프 재빌드) / Drag-and-drop (automatic graph rebuild) |
| 바이패스 / Bypass | 개별 플러그인 토글 / Individual plugin toggle |
| GUI 편집 / GUI Edit | 네이티브 플러그인 GUI 윈도우 열기/닫기 / Open/close native plugin GUI window |
| 삭제 / Delete | `callAsync`로 안전한 자기삭제 (UI 스레드 보호) / Safe self-deletion via `callAsync` (UI thread protection) |
| 추가 / Add | 메뉴: 스캐너에서 선택 / 파일에서 직접 추가 / Menu: select from scanner / add directly from file |
| 우클릭 메뉴 / Right-Click Menu | Edit, Bypass, Remove |

#### 4.2.3 플러그인 체인 내부 구조 / Plugin Chain Internal Structure
```
AudioProcessorGraph:
  InputNode → Plugin[0] → Plugin[1] → ... → Plugin[N-1] → OutputNode
```
- `PluginSlot` 구조 / structure: name, path, PluginDescription, bypassed, nodeId, instance 포인터 / pointer
- `chainLock_` (CriticalSection): 모든 체인 접근(읽기+쓰기) 보호 / Protects all chain access (read+write)
- `processBlock`에서는 lock 없이 capacity guard만 사용 / Only capacity guard in `processBlock`, no lock
- `chainLock_` 내부에서 절대 로그 쓰기 금지 (DirectPipeLogger `writeMutex_`와 데드락 방지) / Never write logs inside `chainLock_` (deadlock prevention with DirectPipeLogger `writeMutex_`)
- `onChainChanged` 콜백은 lock 범위 밖에서 호출 / `onChainChanged` callback invoked outside lock scope

#### 4.2.4 프리셋 슬롯 (A-E + Auto) / Preset Slots (A-E + Auto)
| 항목 / Item | 상세 / Details |
|------|------|
| 슬롯 수 / Slot Count | 5개 사용자 슬롯 / 5 user slots (A=0, B=1, C=2, D=3, E=4) + 1개 Auto 슬롯 / 1 Auto slot (인덱스 / index 5, 이름 변경 불가 / cannot rename, Next/Previous 사이클 제외 / excluded from Next/Previous cycling, Reset 시 Filter+NoiseRemoval+AutoGain 기본값 복원 / Reset restores Filter+NoiseRemoval+AutoGain defaults) |
| 저장 내용 / Stored Content | VST 체인만 / VST chain only — 플러그인 이름, 경로 / plugin name, path, PluginDescription, 바이패스 상태 / bypass state, 내부 상태(base64 인코딩) / internal state (base64 encoded) |
| 저장 위치 / Storage Location | 앱 데이터 디렉토리 / App data directory `Slots/slot_A.dppreset` ~ `slot_E.dppreset` (Windows: `%AppData%/DirectPipe/`, macOS/Linux: see platform paths above) |
| 전환 속도 / Switch Speed | 캐시 히트 / Cache hit: 10-50ms, DLL 로딩 / DLL loading: 200-500ms |
| 전환 방식 / Switch Method | **Keep-Old-Until-Ready**: 이전 체인이 오디오 처리 계속 → 새 체인 준비 완료 시 메시지 스레드에서 원자적 교체 / Old chain continues audio processing → atomic swap on message thread when new chain is ready |
| 프리로드 / Preload | `PluginPreloadCache`: 슬롯 로드 후 백그라운드에서 다른 슬롯 플러그인 미리 로드 / After slot load, preloads other slot plugins in background. SR/BS 변경, 구조 변경(이름/경로/순서) 시 무효화 / Invalidated on SR/BS change or structure change (name/path/order). Per-slot 버전 카운터로 프리로드 중 무효화 경합 방지 / Per-slot version counter prevents invalidation race during preload |
| 자동 저장 / Auto-Save | 플러그인 에디터 닫을 때 (`onEditorClosed`) 활성 슬롯에 자동 저장 / Auto-saves to active slot when plugin editor closes (`onEditorClosed`) |
| 슬롯 이름 / Slot Naming | 커스텀 이름 지원 / Custom names supported. 표시 / Display: `A\|게임` (파이프 구분자, 최대 8자 + ".." 자동 잘림 / pipe delimiter, max 8 chars + ".." auto-truncation). `.dppreset` JSON `"name"` 필드에 저장 / stored in `"name"` field. StateBroadcaster `slot_names` 배열로 외부 전달 / externally delivered via `slot_names` array |
| 관리 / Management | 우클릭 메뉴 / Right-click menu: Rename, Copy A→B/C/D/E, Delete, Export (.dppreset), Import (.dppreset). 활성 슬롯 Copy 시 라이브 상태 캡처 / Active slot Copy captures live state |
| 보호 / Protection | 빈 체인 저장 방지 / Prevents saving empty chains. `partialLoad_` 부분 로드 감지 / partial load detection. `loadingSlot_` 동시 전환 방지 / concurrent switch prevention |
| 점유 캐시 / Occupancy Cache | `slotOccupiedCache_[5]` — 파일시스템 조회 없이 빠른 점유 확인 / Fast occupancy check without filesystem queries (NextPreset 사이클링용 / for NextPreset cycling) |

#### 4.2.5 비동기 체인 로딩 / Async Chain Loading (replaceChainAsync)
| 항목 / Item | 상세 / Details |
|------|------|
| 생성 카운터 / Generation Counter | `asyncGeneration_` (uint32_t atomic) — 호출마다 증가. 이전 요청의 콜백 폐기 / Incremented per call. Discards callbacks from previous requests |
| 백그라운드 스레드 / Background Thread | COM 초기화 (`CoInitializeEx`) 후 플러그인 로드 / Plugin loading after COM initialization (`CoInitializeEx`) |
| 메시지 스레드 교체 / Message Thread Swap | `callAsync`로 그래프 교체. 생성 카운터 확인 후 진행 / Graph swap via `callAsync`. Proceeds after generation counter check |
| 수명 보호 / Lifetime Protection | `alive_` 플래그 (`shared_ptr<atomic<bool>>`) — callAsync 람다에서 this 접근 전 확인 / `alive_` flag checked before accessing `this` in callAsync lambda |
| 프리로드 경로 / Preload Path | `replaceChainWithPreloaded`: 미리 로드된 인스턴스로 동기 교체 (DLL 로딩 없음) / Synchronous swap with pre-loaded instances (no DLL loading) |

#### 4.2.6 그래프 재빌드 / Graph Rebuild (rebuildGraph)
```
rebuildGraph(bool suspend = true)
1. suspend=true일 때만 suspendProcessing(true) (노드 추가/제거 시)
   / suspendProcessing(true) only when suspend=true (on node add/remove)
2. 연결 목록 복사 후 모든 연결 제거 (async → 개별 리빌드 지연)
   / Copy connection list then remove all connections (async → deferred per-connection rebuild)
3. 입력 → plugin[0] → ... → plugin[N-1] → 출력 연결 (bypassed 플러그인 스킵)
   / Input → plugin[0] → ... → plugin[N-1] → output connections (bypassed plugins skipped)
4. 마지막 연결만 sync (단일 리빌드 트리거)
   / Only the last connection is sync (single rebuild trigger)
5. suspend=true일 때만 suspendProcessing(false)
   / suspendProcessing(false) only when suspend=true
```
- `suspend=false`: bypass 토글 시 사용 — 연결만 변경되므로 suspend 불필요, 오디오 갭 없음 / Used for bypass toggle — only connections change, no suspend needed, no audio gap
- bypassed 플러그인은 그래프 연결에서 제외 (오디오가 우회) / Bypassed plugins excluded from graph connections (audio bypasses them). `node->setBypassed()` + `getBypassParameter()` 동기화와 함께 이중 보호 / dual protection with synchronization

---

### 4.3 모니터 출력 / Monitor Output (MonitorOutput)

#### 구성 / Configuration
| 항목 / Item | 상세 / Details |
|------|------|
| 장치 / Device | 별도 AudioDeviceManager / Separate AudioDeviceManager (Windows: WASAPI, macOS: CoreAudio, Linux: ALSA) (메인 드라이버와 독립 / independent from main driver) |
| 링 버퍼 / Ring Buffer | AudioRingBuffer: 4096 프레임 / frames, 스테레오 / stereo, power-of-2 |
| 상태 / Status | `VirtualCableStatus` enum: NotConfigured, Active, Error, SampleRateMismatch |

#### 이중 스레드 브릿지 / Dual-Thread Bridge
| 역할 / Role | 스레드 / Thread | 동작 / Behavior |
|------|--------|------|
| 프로듀서 / Producer | 메인 오디오 콜백 (RT) / Main audio callback (RT) | `writeAudio()` → 링 버퍼에 RT-safe 쓰기 / RT-safe write to ring buffer |
| 컨슈머 / Consumer | 모니터 장치 콜백 / Monitor device callback | 링 버퍼에서 읽기 → WASAPI 출력 / Read from ring buffer → WASAPI output |

#### 폴백 보호 / Fallback Protection
- `audioDeviceAboutToStart`에서 실제 장치명 vs desired 비교 / Compares actual device name vs desired in `audioDeviceAboutToStart`
- JUCE 자동 폴백 감지 시: Error 상태 설정, callAsync로 장치 닫기 / On JUCE auto-fallback detection: set Error state, close device via callAsync
- SR 불일치: `SampleRateMismatch` 상태, 링 버퍼 리셋, one-shot NotificationBar 경고 (클리어 시 리셋) / SR mismatch: `SampleRateMismatch` state, ring buffer reset, one-shot NotificationBar warning (reset on clear)

#### 재연결 / Reconnection
- `monitorLost_` atomic 플래그 (audioDeviceError/audioDeviceStopped에서 설정) / `monitorLost_` atomic flag (set by audioDeviceError/audioDeviceStopped)
- `shutdown()`은 콜백 제거 후 실행 → 외부 이벤트에서만 monitorLost_ 설정 / `shutdown()` runs after callback removal → monitorLost_ only set by external events
- 3초 타이머 폴링으로 재연결 시도 / Reconnection attempted via 3-second timer polling
- `actualSampleRate_` / `actualBufferSize_` 표시용 (shutdown 시 0으로 리셋) / For display purposes (reset to 0 on shutdown)

#### 레이턴시 계산 / Latency Calculation
```
레이턴시(ms) = (버퍼크기 / 샘플레이트) × 1000
Latency(ms) = (bufferSize / sampleRate) × 1000
```

---

### 4.4 AudioRingBuffer (Lock-Free 스레드 브릿지 / Lock-Free Thread Bridge)

| 항목 / Item | 상세 / Details |
|------|------|
| 구조 / Structure | 채널별 float 벡터 + atomic uint64 read/write 포지션 / Per-channel float vectors + atomic uint64 read/write positions |
| 정렬 / Alignment | `alignas(64)` — 캐시 라인 정렬로 false sharing 방지 / Cache-line aligned to prevent false sharing |
| 용량 / Capacity | 반드시 power-of-2 (assertion 강제) / Must be power-of-2 (assertion enforced). mask = capacity - 1 |
| 쓰기 / Write | relaxed load writePos, acquire load readPos → 채널별 복사 + wrap-around / per-channel copy + wrap-around → release store writePos |
| 읽기 / Read | relaxed load readPos, acquire load writePos → 채널별 복사 + wrap-around / per-channel copy + wrap-around → release store readPos |
| Mono→Stereo | 채널 0을 채널 1에 자동 복제 / Automatically duplicates channel 0 to channel 1 |
| reset() | read/write 포지션 0, 모든 채널 데이터 0.0f 초기화 / read/write positions to 0, all channel data initialized to 0.0f |

---

### 4.5 외부 제어 시스템 / External Control System

#### 4.5.1 통합 아키텍처 / Unified Architecture
```
핫키 / MIDI / WebSocket / HTTP → ControlManager → ActionDispatcher
Hotkey / MIDI / WebSocket / HTTP → ControlManager → ActionDispatcher
  → VSTChain (바이패스/bypass), OutputRouter (볼륨/volume), PresetManager (슬롯 전환/slot switching)
  → StateBroadcaster → WebSocket/HTTP 클라이언트에 상태 전파 / state propagation to WebSocket/HTTP clients
```
- **ActionDispatcher**: 메시지 스레드 전달 보장 / Guarantees message-thread delivery. 오프스레드 호출 시 `callAsync` / `callAsync` for off-thread calls. `alive_` 플래그 수명 보호 / `alive_` flag lifetime protection. 리스너 copy-before-iterate (재진입 안전) / Listener copy-before-iterate (reentrant-safe)
- **StateBroadcaster**: `quickStateHash()` 더티 체크로 무변경 시 브로드캐스트 생략 / `quickStateHash()` dirty check skips broadcast when unchanged. float 0.05 정밀도 양자화 / float 0.05 precision quantization

#### 4.5.2 액션 목록 (19종) / Action List (19 Actions)

| # | Action | 설명 / Description | 파라미터 / Parameters |
|---|--------|------|---------|
| 1 | `PluginBypass` | 특정 플러그인 바이패스 토글 / Toggle bypass for a specific plugin | intParam = 플러그인 인덱스 / plugin index |
| 2 | `MasterBypass` | 전체 VST 체인 바이패스 토글 / Toggle bypass for entire VST chain | — |
| 3 | `SetVolume` | 볼륨 설정 / Set volume | stringParam = "monitor"/"input"/"output", floatParam = 값 / value |
| 4 | `ToggleMute` | 뮤트 토글 / Toggle mute | stringParam = 타겟명 / target name |
| 5 | `LoadPreset` | 프리셋 로드 / Load preset | intParam = 인덱스 / index |
| 6 | `PanicMute` | 패닉 뮤트 (모든 출력 즉시 차단) / Panic mute (instantly kill all outputs) | — |
| 7 | `InputGainAdjust` | 입력 게인 조정 / Adjust input gain | floatParam = 델타 / delta (±1.0) |
| 8 | `NextPreset` | 다음 슬롯으로 순환 / Cycle to next slot | — |
| 9 | `PreviousPreset` | 이전 슬롯으로 순환 / Cycle to previous slot | — |
| 10 | `InputMuteToggle` | 마이크 입력 뮤트 토글 / Toggle mic input mute | — |
| 11 | `SwitchPresetSlot` | 특정 슬롯 전환 / Switch to specific slot | intParam = 0~4 (A~E) |
| 12 | `MonitorToggle` | 모니터 출력 토글 / Toggle monitor output | — |
| 13 | `RecordingToggle` | 녹음 토글 / Toggle recording | — |
| 14 | `SetPluginParameter` | 플러그인 파라미터 설정 / Set plugin parameter | intParam=플러그인 / plugin, intParam2=파라미터 / parameter, floatParam=0.0~1.0 |
| 15 | `IpcToggle` | IPC 출력(DirectPipe Receiver) 토글 / Toggle IPC output (DirectPipe Receiver) | — |
| 16 | `XRunReset` | XRun 카운터 리셋 / Reset XRun counter | — |
| 17 | `SafetyLimiterToggle` | Safety Limiter on/off 토글 / Toggle Safety Limiter on/off | — (패닉 뮤트 가드 없음 / no panic mute guard) |
| 18 | `SetSafetyLimiterCeiling` | Safety Limiter ceiling 설정 / Set Safety Limiter ceiling | floatParam = dB 값 / dB value (-6.0~0.0) (패닉 뮤트 가드 없음 / no panic mute guard) |
| 19 | `AutoProcessorsAdd` | Auto 프로세서 추가 / Add Auto processors (Filter+NR+AGC) | — |

#### 4.5.3 키보드 핫키 / Keyboard Hotkeys

**기본 매핑 (11개) / Default Mappings (11):**
| 핫키 / Hotkey | 액션 / Action | 파라미터 / Parameters |
|------|------|---------|
| Ctrl+Shift+M | PanicMute | — |
| Ctrl+Shift+0 | MasterBypass | — |
| Ctrl+Shift+1~3 | PluginBypass | 인덱스 / Index 0~2 |
| Ctrl+Shift+F6 | InputMuteToggle | — |
| Ctrl+Shift+H | MonitorToggle | — |
| Ctrl+Shift+F1~F5 | SwitchPresetSlot | 0~4 |

**구현 / Implementation:**
- Windows: `RegisterHotKey()` via 숨겨진 메시지 전용 윈도우 / hidden message-only window (`HWND_MESSAGE`). macOS: `CGEventTap` (접근성 권한 필요 / accessibility permission required — 미허용 시 `onError` 콜백으로 사용자 알림 / user notified via `onError` callback if denied). Linux: 스텁 (미지원 / stub, not supported — HotkeyTab에 "unsupported" 메시지 표시 / shows "unsupported" message in HotkeyTab, MIDI/WebSocket/HTTP 대안 안내 / suggests MIDI/WebSocket/HTTP alternatives)
- 앱 최소화/트레이 상태에서도 동작 / Works even when app is minimized/in tray
- 드래그앤드롭으로 순서 재정렬 / Reorder via drag-and-drop (`moveBinding`)
- 인플레이스 키 재할당 / In-place key reassignment (`updateHotkey`)
- 키 녹음: ~60Hz 폴링 (16ms 타이머). 수정자 키 1개 이상 필수. [Cancel] 취소 가능 / Key recording: ~60Hz polling (16ms timer). At least one modifier key required. [Cancel] to abort

#### 4.5.4 MIDI

**바인딩 타입 / Binding Types:**
| 타입 / Type | 동작 / Behavior |
|------|------|
| `Toggle` | CC ≥ 64 → ON, < 64 → OFF (모멘터리 버튼 시뮬레이션 / momentary button simulation) |
| `Momentary` | CC ≥ 64 → 누르는 동안 ON / ON while pressed |
| `Continuous` | CC 0~127 → 파라미터 0.0~1.0 매핑 (페이더, 노브) / parameter 0.0~1.0 mapping (faders, knobs) |
| `NoteOnOff` | Note ON → 토글 / toggle |

**바인딩 구조 / Binding Structure:**
- CC 번호 (-1이면 Note 모드) / CC number (-1 for Note mode)
- Note 번호 (-1이면 CC 모드) / Note number (-1 for CC mode)
- 채널 (0 = 모든 채널, 1~16 = 특정 채널) / Channel (0 = all channels, 1~16 = specific channel)
- 장치명 ("" = 모든 장치) / Device name ("" = all devices)
- 마지막 상태 (토글 메모리) / Last state (toggle memory)

**MIDI Learn:**
- [Learn] 클릭 → 다음 CC 또는 Note 메시지 캡처 / Click [Learn] → captures next CC or Note message
- 플러그인 파라미터 매핑: 3단계 팝업 (플러그인 선택 → 파라미터 선택 → MIDI Learn) / Plugin parameter mapping: 3-step popup (select plugin → select parameter → MIDI Learn)
- [Cancel] 취소 가능 / [Cancel] to abort
- 기본 매핑: 없음 (사용자가 직접 학습) / Default mappings: none (user learns manually)

**바인딩 관리 / Binding Management:**
- `addBinding()` returns `bool` — 동일 CC/채널/장치의 기존 바인딩이 있으면 덮어쓰기(overwrite), 중복 추가 안 함 / overwrites existing binding with same CC/channel/device, no duplicate entries
- `alive_` 플래그 (`shared_ptr<atomic<bool>>`) — 소멸 후 callAsync 콜백 안전 보호 / lifetime safety for callAsync callbacks after destruction
- LearnTimeout (30초) — 타이머 콜백에서 자기 자신을 파괴하지 않도록 callAsync로 지연 정리 / 30-second timeout — deferred cleanup via callAsync to prevent self-destruction from timer callback

**스레드 안전 / Thread Safety:**
- `bindingsMutex_` (std::mutex)로 바인딩 접근 보호 / protects binding access
- `getBindings()`는 복사본 반환 / returns a copy
- `processCC`/`processNote`: lock 내에서 액션 수집 → lock 해제 후 dispatch (데드락 방지) / collect actions under lock → dispatch after lock release (deadlock prevention)

**테스트 API / Test API:**
- `injectTestMessage(MidiMessage)` — HTTP API를 통한 MIDI 하드웨어 없이 테스트 / Test without MIDI hardware via HTTP API

#### 4.5.5 Stream Deck 플러그인 / Stream Deck Plugin

**기본 정보 / Basic Info:**
| 항목 / Item | 상세 / Details |
|------|------|
| SDK | Elgato SDKVersion 3 (@elgato/streamdeck v2.0.1) |
| Node.js | v20+ |
| UUID | `com.directpipe.directpipe` |
| 최소 SD 버전 / Min SD Version | 6.9 |
| 빌드 / Build | Rollup → `bin/plugin.js` |
| 패키징 / Packaging | `streamdeck pack` CLI |

**액션 (10개, 모두 SingletonAction) / Actions (10, all SingletonAction):**

| # | 액션 / Action | UUID | 컨트롤러 / Controller | 상태수 / States | Property Inspector |
|---|------|------|---------|--------|-------------------|
| 1 | Bypass Toggle | `...bypass-toggle` | Keypad | 2 (Bypassed/Active) | bypass-pi.html |
| 2 | Volume Control | `...volume-control` | Keypad + Encoder | 2 (Unmuted/Muted) | volume-pi.html |
| 3 | Preset Switch | `...preset-switch` | Keypad | 1 | preset-pi.html |
| 4 | Monitor Toggle | `...monitor-toggle` | Keypad | 2 (ON/OFF) | 없음 / None |
| 5 | Panic Mute | `...panic-mute` | Keypad | 2 (MUTE/MUTED) | 없음 / None |
| 6 | Recording Toggle | `...recording-toggle` | Keypad | 2 (REC/REC) | 없음 / None |
| 7 | IPC Toggle | `...ipc-toggle` | Keypad | 2 (ON/OFF) | 없음 / None |
| 8 | Performance Monitor | `...performance-monitor` | Keypad + Encoder | 1 | performance-pi.html |
| 9 | Plugin Parameter | `...plugin-param` | Encoder (SD+) | 1 | plugin-param-pi.html |
| 10 | Preset Bar | `...preset-bar` | Encoder (SD+) | 1 | 없음 / None |

**Bypass Toggle 상세 / Details:**
- 숏프레스: 개별 플러그인 바이패스 / Short press: individual plugin bypass (`plugin_bypass`)
- 롱프레스 (500ms): 마스터 바이패스 / Long press (500ms): master bypass (`master_bypass`)
- 설정 / Settings: Plugin # (1-indexed)
- 타이틀 / Title: "{PluginName}\nBypassed" 또는 / or "Active", 빈 슬롯은 / empty slot shows "Slot {N}\nEmpty"

**Volume Control 상세 / Details:**
- 타겟 / Target: input (0~2.0), output (0~1.0), monitor (0~1.0)
- 모드 / Mode: mute, volume_up, volume_down
- 스텝 / Step: 1~25% (기본 / default 5%)
- SD+ 다이얼 / SD+ Dial: 회전=볼륨 조정 / rotation=volume adjust (틱당 / per tick ±0.05), 누르기=뮤트 / press=mute, 롱터치=100% 리셋 / long touch=100% reset
- LCD 표시 / LCD Display: input "x{gain}", output/monitor "{percent}%"
- 쓰로틀 / Throttle: 다이얼 전송 50ms / dial send 50ms, 로컬 오버라이드 억제 300ms / local override suppression 300ms (optimistic local update)

**Preset Switch 상세 / Details:**
- 설정 / Settings: cycle / A / B / C / D / E
- cycle 모드 / cycle mode: `next_preset` 명령 / command
- 슬롯 모드 / slot mode: `switch_preset_slot { slot: 0-4 }`

**Recording Toggle 상세 / Details:**
- 녹음 중: 타이틀에 "REC\n{MM}:{SS}" 표시 / During recording: title shows "REC\n{MM}:{SS}"

**연결 / Connection:**
| 항목 / Item | 상세 / Details |
|------|------|
| WebSocket | `ws://localhost:8765` |
| UDP 디스커버리 / UDP Discovery | 포트 / Port 8767, `127.0.0.1`. 메시지 / Message: `"DIRECTPIPE_READY:8765"` |
| 재연결 / Reconnection | 이벤트 드리븐 (UDP 감지 + 사용자 액션). 폴링 없음 / Event-driven (UDP detection + user action). No polling |
| 백오프 / Backoff | 초기 / Initial 2초 / 2s, 최대 / Max 10초 / 10s, 배율 / Multiplier 1.5x |
| 대기열 / Queue | 연결 끊김 중 최대 50개 메시지 큐잉 / Up to 50 messages queued while disconnected |
| 킵얼라이브 / Keepalive | 15초 간격 ping/pong / 15-second interval ping/pong |

#### 4.5.6 HTTP REST API

| 항목 / Item | 상세 / Details |
|------|------|
| 포트 / Port | 8766 (폴백 / fallback: 8767~8771) |
| 방식 / Method | GET-only |
| CORS | `Access-Control-Allow-Origin: *` |
| 타임아웃 / Timeout | 3초 읽기 타임아웃 / 3-second read timeout |
| 응답 / Response | JSON. 상태 코드 / Status codes: 200/400/404/405 |

**엔드포인트 (23개) / Endpoints (23):**
| 엔드포인트 / Endpoint | 액션 / Action | 파라미터 검증 / Parameter Validation |
|-----------|------|-------------|
| `GET /api/status` | 전체 상태 JSON / Full state JSON | — |
| `GET /api/bypass/master` | 마스터 바이패스 토글 / Master bypass toggle | — |
| `GET /api/bypass/{index}/toggle` | 플러그인 바이패스 토글 / Plugin bypass toggle | index 범위 검증 / index range validation |
| `GET /api/mute/panic` | 패닉 뮤트 / Panic mute | — |
| `GET /api/mute/toggle` | 마스터 뮤트 토글 / Master mute toggle | — |
| `GET /api/volume/{target}/{value}` | 볼륨 설정 / Set volume | target: monitor(0~1)/input(0~2)/output(0~1). 범위 초과 시 400 / 400 on out-of-range |
| `GET /api/volume/output/{value}` | 출력 볼륨 설정 / Set output volume | 0~1 범위 / range |
| `GET /api/preset/{index}` | 프리셋 로드 / Load preset | 0~5 범위 / range (0-4=A-E, 5=Auto) |
| `GET /api/slot/{index}` | 슬롯 전환 / Switch slot | 0~5 범위 / range (0-4=A-E, 5=Auto) |
| `GET /api/gain/{delta}` | 입력 게인 조정 / Adjust input gain | float 델타 / delta |
| `GET /api/input-mute/toggle` | 입력 뮤트 토글 / Input mute toggle | — |
| `GET /api/monitor/toggle` | 모니터 출력 토글 / Monitor output toggle | — |
| `GET /api/recording/toggle` | 녹음 토글 / Recording toggle | — |
| `GET /api/ipc/toggle` | IPC 출력 토글 / IPC output toggle | — |
| `GET /api/plugins` | 플러그인 목록 조회 / List plugins | — |
| `GET /api/plugin/{idx}/params` | 플러그인 파라미터 목록 조회 / List plugin parameters | index 범위 검증 / index range validation |
| `GET /api/plugin/{pIdx}/param/{paramIdx}/{value}` | 플러그인 파라미터 설정 / Set plugin parameter | 인덱스 + 값(0~1) 범위 검증 / index + value (0~1) range validation |
| `GET /api/perf` | 성능 통계 조회 / Get performance stats | — |
| `GET /api/limiter/toggle` | Safety Limiter on/off 토글 / Safety Limiter on/off toggle | — |
| `GET /api/limiter/ceiling/{value}` | Safety Limiter ceiling 설정 / Set Safety Limiter ceiling | -6.0~0.0 범위 / range |
| `GET /api/auto/add` | Auto 프로세서 추가 / Add Auto processors (Filter+NR+AGC) | — |
| `GET /api/midi/cc/{ch}/{num}/{val}` | MIDI CC 테스트 주입 / MIDI CC test injection | ch:1~16, num:0~127, val:0~127 |
| `GET /api/midi/note/{ch}/{num}/{vel}` | MIDI Note 테스트 주입 / MIDI Note test injection | ch:1~16, num:0~127, vel:0~127 |

**응답 형식 / Response Format:**
```json
// 성공 / Success
{"ok": true, "action": "...", /* 액션별 추가 필드 / additional fields per action */}
// 실패 / Failure
{"error": "설명 / description"}
```

#### 4.5.7 WebSocket 서버 / WebSocket Server

| 항목 / Item | 상세 / Details |
|------|------|
| 포트 / Port | 8765 (폴백 / fallback: 8766~8770) |
| 프로토콜 / Protocol | RFC 6455 (커스텀 SHA-1 핸드셰이크 / custom SHA-1 handshake) |
| 헤더 / Headers | 대소문자 무시 / Case-insensitive (RFC 7230) |
| 프레임 제한 / Frame Limit | 1MB |
| 최대 클라이언트 / Max Clients | 32개 / 32 |
| 데드 클라이언트 / Dead Clients | `sendFrame` 실패 시 즉시 소켓 종료 / Immediate socket close on `sendFrame` failure |
| 브로드캐스트 / Broadcast | 전용 스레드 (논블로킹) / Dedicated thread (non-blocking). `clientsMutex_` 밖에서 thread join (데드락 방지) / thread join outside `clientsMutex_` (deadlock prevention) |

**수신 명령 (19개) / Incoming Commands (19):**
```json
{"type": "action", "action": "command_name", "params": {/* optional */}}
```

| 명령 | params |
|------|--------|
| `plugin_bypass` | `{"index": 0}` |
| `master_bypass` | — |
| `set_volume` | `{"target": "monitor", "value": 0.75}` |
| `toggle_mute` | `{"target": ""}` |
| `load_preset` | `{"index": 0}` |
| `panic_mute` | — |
| `input_gain` | `{"delta": 1.0}` |
| `switch_preset_slot` | `{"slot": 0}` |
| `input_mute_toggle` | — |
| `next_preset` | — |
| `previous_preset` | — |
| `monitor_toggle` | — |
| `recording_toggle` | — |
| `ipc_toggle` | — |
| `set_plugin_parameter` | `{"pluginIndex": 0, "paramIndex": 2, "value": 0.5}` |
| `xrun_reset` | — |
| `safety_limiter_toggle` | — |
| `set_safety_limiter_ceiling` | `{"value": -0.5}` |
| `auto_processors_add` | — |

**상태 브로드캐스트 (서버 → 클라이언트) / State Broadcast (Server → Client):**
```json
{
  "type": "state",
  "data": {
    "plugins": [{"name": "Plugin 1", "bypass": false, "loaded": true, "type": "vst", "latency_samples": 128}],
    "volumes": {"input": 1.0, "monitor": 1.0, "output": 1.0},
    "master_bypassed": false,
    "muted": false,
    "output_muted": false,
    "input_muted": false,
    "preset": "Default",
    "latency_ms": 10.5,
    "monitor_latency_ms": 12.0,
    "level_db": -25.5,
    "cpu_percent": 5.2,
    "sample_rate": 48000.0,
    "buffer_size": 480,
    "channel_mode": 2,
    "monitor_enabled": true,
    "active_slot": 0,
    "auto_slot_active": false,
    "slot_names": ["게임", "토크", "", "", "", "Auto"],
    "recording": false,
    "recording_seconds": 0.0,
    "ipc_enabled": true,
    "device_lost": false,
    "monitor_lost": false,
    "xrun_count": 0,
    "chain_pdc_samples": 128,
    "chain_pdc_ms": 2.67,
    "safety_limiter": {"enabled": true, "ceiling_dB": -0.3, "gain_reduction_dB": 0.0, "is_limiting": false}
  }
}
```

---

### 4.6 사용자 인터페이스 / User Interface

#### 4.6.1 메인 윈도우 레이아웃 (800×800 기본) / Main Window Layout (800×800 default)
```
┌─────────────────────────────────────────────────────────┐
│ [좌측 열]                        │ [우측 열]              │
│                                  │                       │
│ ┌─INPUT─────────────────────┐    │ ┌─탭 패널─────────────┐│
│ │ 레벨미터 │ 게인 슬라이더   │    │ │ Audio│Output│       ││
│ │  (40px)  │ (0.0x~2.0x)    │    │ │ Controls│Settings   ││
│ └───────────────────────────┘    │ │                     ││
│                                  │ │ (탭 내용)            ││
│ ┌─VST 체인 에디터───────────┐    │ │                     ││
│ │ [Plugin 1] [Edit][Bp][X]  │    │ │                     ││
│ │ [Plugin 2] [Edit][Bp][X]  │    │ │                     ││
│ │ (드래그앤드롭 순서 변경)    │    │ │                     ││
│ │ [+ Add Plugin] [Scan...]  │    │ │                     ││
│ └───────────────────────────┘    │ │                     ││
│                                  │ │                     ││
│ ┌─프리셋 슬롯───────────────┐    │ │                     ││
│ │ [A] [B] [C] [D] [E]      │    │ │                     ││
│ │ (우클릭: Rename/Copy/Del/  │    │ │                     ││
│ Export/Import)             │    │ │                     ││
│ └───────────────────────────┘    │ │                     ││
│                                  │ │                     ││
│ ┌─뮤트 컨트롤──────────────┐    │ │                     ││
│ │ [OUT] [MON] [VST]        │    │ │                     ││
│ │ [  PANIC MUTE  ]         │    │ │                     ││
│ └───────────────────────────┘    │ └─────────────────────┘│
│                                  │                       │
│ 출력 레벨미터                     │                       │
├─────────────────────────────────────────────────────────┤
│ [레이턴시] [CPU%] [SR/BS/Ch] [포터블] [Created by LiveTrack]│
└─────────────────────────────────────────────────────────┘
```

**프리셋 슬롯 버튼 / Preset Slot Buttons:**
- 5개 TextButton, 4px 간격 / 5 TextButtons, 4px spacing
- 색상: 활성=하이라이트, 점유=채움, 빈=흐림 / Colors: active=highlighted, occupied=filled, empty=dimmed
- 싱글 클릭으로 슬롯 로드 (디바운스 적용) / Single click to load slot (debounced)
- 우클릭 / Right-click: Copy Slot X→Y, Delete Slot

**뮤트 버튼 / Mute Buttons:**
- OUT: 메인 출력 뮤트 / Main output mute
- MON: 모니터 뮤트 / Monitor mute
- VST: IPC 출력 토글 / IPC output toggle
- PANIC MUTE: 패닉 뮤트 (모든 출력 차단 + 컨트롤 잠금) / Panic mute (kill all outputs + lock controls)

#### 4.6.2 Audio 탭 / Audio Tab (AudioSettings)
| UI 요소 / UI Element | 설명 / Description |
|---------|------|
| 드라이버 타입 ComboBox / Driver Type ComboBox | Windows: DirectSound / WASAPI Shared / WASAPI Low Latency / WASAPI Exclusive / ASIO. macOS: CoreAudio. Linux: ALSA / JACK |
| 입력 장치 ComboBox / Input Device ComboBox | 클릭 시 `scanForDevices()` + 목록 재빌드 / `scanForDevices()` + list rebuild on click |
| 출력 장치 ComboBox / Output Device ComboBox | 클릭 시 `scanForDevices()` + 목록 재빌드 / `scanForDevices()` + list rebuild on click |
| ASIO 입력 채널 ComboBox / ASIO Input Channel ComboBox | ASIO 모드에서만 표시 / Shown only in ASIO mode |
| ASIO 출력 채널 ComboBox / ASIO Output Channel ComboBox | ASIO 모드에서만 표시 / Shown only in ASIO mode |
| 샘플레이트 ComboBox / Sample Rate ComboBox | 장치에서 동적 팝업 / Dynamic popup from device |
| 버퍼 크기 ComboBox / Buffer Size ComboBox | 장치에서 동적 팝업 / Dynamic popup from device |
| Mono/Stereo 토글 버튼 / Mono/Stereo Toggle Button | 채널 모드 전환 / Channel mode switching |
| 레이턴시 라벨 / Latency Label | "-- ms" 또는 / or "10.5 ms" 실시간 표시 / real-time display |
| 출력 볼륨 슬라이더 / Output Volume Slider | Output Volume (0.0~1.0) — Audio 탭에서 출력 볼륨 조절 / output volume control in Audio tab |
| ASIO Control Panel 버튼 / ASIO Control Panel Button | ASIO 모드에서만 표시 / Shown only in ASIO mode (Windows only). 드라이버 설정 패널 열기 / Opens driver settings panel |

#### 4.6.3 Output 탭 / Output Tab (OutputPanel)

**모니터 섹션 / Monitor Section:**
| UI 요소 / UI Element | 설명 / Description |
|---------|------|
| 장치 ComboBox / Device ComboBox | 클릭 시 목록 새로고침 / List refreshes on click |
| 볼륨 슬라이더 / Volume Slider | 0.0 ~ 1.0 |
| 버퍼 크기 ComboBox / Buffer Size ComboBox | 모니터 장치용 / For monitor device |
| 레이턴시 라벨 / Latency Label | Active 시 실시간 ms 표시 / Real-time ms display when Active |
| Enable 토글 / Enable Toggle | 모니터 출력 활성화/비활성화 / Enable/disable monitor output |
| 상태 라벨 / Status Label | Active / SampleRateMismatch / Error / No device |

**VST Receiver (IPC) 섹션 / VST Receiver (IPC) Section:**
| UI 요소 / UI Element | 설명 / Description |
|---------|------|
| Enable 토글 / Enable Toggle | "Enable VST Receiver Output" |
| 설명 라벨 / Description Label | "Send processed audio to DirectPipe Receiver plugin." |

**녹음 섹션 / Recording Section:**
| UI 요소 / UI Element | 설명 / Description |
|---------|------|
| REC 버튼 / REC Button | 녹음 토글 (녹음 중 눌린 상태) / Recording toggle (pressed state during recording) |
| 타이머 라벨 / Timer Label | "0m 15s" 형식 / format |
| Play 버튼 / Play Button | 마지막 녹음 파일 기본 플레이어로 재생 / Play last recorded file with default player |
| Open Folder 버튼 / Open Folder Button | Windows: `explorer.exe /select,{lastFile}`, macOS: `open -R`, Linux: `xdg-open` |
| 폴더 변경 버튼 / Change Folder Button (...) | 녹음 폴더 선택 / Select recording folder |
| 폴더 경로 라벨 / Folder Path Label | 말줄임표로 축약 표시 / Truncated with ellipsis |

녹음 설정은 앱 데이터 디렉토리(Windows: `%AppData%/DirectPipe/`, macOS: `~/Library/Application Support/DirectPipe/`, Linux: `~/.config/DirectPipe/`)의 `recording-config.json`에 영속 저장

Recording settings are persisted in `recording-config.json` in the app data directory (Windows: `%AppData%/DirectPipe/`, macOS: `~/Library/Application Support/DirectPipe/`, Linux: `~/.config/DirectPipe/`)

#### 4.6.4 Controls 탭 / Controls Tab (ControlSettingsPanel) — 3개 서브탭 / 3 Sub-Tabs

**Hotkeys 서브탭 / Hotkeys Sub-Tab:**
- 스크롤 가능한 바인딩 리스트 / Scrollable binding list
- 행별: 액션명 라벨, 단축키 라벨, [Set] 버튼, [Remove] 버튼 / Per row: action name label, hotkey label, [Set] button, [Remove] button
- 드래그앤드롭 행 순서 변경 / Drag-and-drop row reordering
- 교대 행 배경색 / Alternating row background colors
- [+ Add Shortcut] 버튼 / button
- 녹음 중: 상태 라벨 ("Press a key...") + [Cancel] 버튼 / During recording: status label ("Press a key...") + [Cancel] button

**MIDI 서브탭 / MIDI Sub-Tab:**
- MIDI 장치 ComboBox + [Rescan] 버튼 / MIDI device ComboBox + [Rescan] button
- 스크롤 가능한 매핑 리스트 / Scrollable mapping list
- 행별: CC/Note 라벨, 액션 라벨, [Learn] 버튼, [Remove] 버튼 / Per row: CC/Note label, action label, [Learn] button, [Remove] button
- [+ Add Mapping] 버튼 / button
- [+ Plugin Param] 버튼 (3단계 팝업: 플러그인 → 파라미터 → Learn) / button (3-step popup: plugin → parameter → Learn)
- Learn 중: 상태 라벨 ("Waiting for MIDI...") + [Cancel] 버튼 / During Learn: status label ("Waiting for MIDI...") + [Cancel] button

**Stream Deck 서브탭 / Stream Deck Sub-Tab:**
- WebSocket 서버 상태 (Running/Stopped + 포트) / WebSocket server status (Running/Stopped + port)
- HTTP API 서버 상태 (Running/Stopped + 포트) / HTTP API server status (Running/Stopped + port)
- 연결된 클라이언트 수 / Connected client count

#### 4.6.5 Settings 탭 / Settings Tab (LogPanel)

**애플리케이션 섹션 / Application Section:**
- 자동 시작 토글 / Auto-start toggle (Windows: "Start with Windows", macOS: "Start at Login", Linux: "Start on Login")

**설정 섹션 / Settings Section:**
- [Save Settings] 버튼 / button (`.dpbackup`)
- [Load Settings] 버튼 / button

**로그 섹션 / Log Section:**
- Audit Mode 토글 / toggle
- 로그 뷰어 (TextEditor, 읽기 전용, 모노스페이스, 타임스탬프) / Log viewer (TextEditor, read-only, monospace, timestamped)
- 최대 1000줄 인메모리 히스토리 / Max 1000 lines in-memory history
- DirectPipeLogger 링 버퍼에서 실시간 drain / Real-time drain from DirectPipeLogger ring buffer
- [Export Log] 버튼 (.txt 저장) / button (saves .txt)
- [Clear Log] 버튼 / button

**유지보수 섹션 / Maintenance Section:**
| 버튼 / Button | 동작 / Action | 확인 다이얼로그 / Confirmation Dialog |
|------|------|---------------|
| Full Backup | `.dpfullbackup`으로 전체 백업 / Full backup to `.dpfullbackup` | 없음 (파일 선택) / None (file chooser) |
| Full Restore | `.dpfullbackup`에서 전체 복원 / Full restore from `.dpfullbackup` | 있음 / Yes |
| Clear Plugin Cache | 플러그인 스캔 캐시 삭제 / Delete plugin scan cache | 있음 / Yes |
| Clear All Presets | 슬롯 A-E + Auto + 백업 + 임시 파일 삭제, 활성 체인 초기화 / Delete slots A-E + Auto + backups + temp files, reset active chain | 있음 / Yes |
| Factory Reset | 모든 데이터 삭제 (설정, 컨트롤, 프리셋(A-E + Auto), 캐시, 녹음 설정) / Delete all data (settings, controls, presets (A-E + Auto), cache, recording config) | 있음 / Yes |

#### 4.6.6 상태 바 / Status Bar (30px)
| 요소 / Element | 설명 / Description |
|------|------|
| 레이턴시 라벨 / Latency Label | 메인 + 모니터 레이턴시. 녹음 중 자동 숨김 / Main + monitor latency. Auto-hidden during recording |
| CPU% 라벨 / CPU% Label | 오디오 콜백 오버로드 시 빨간 하이라이트 / Red highlight on audio callback overload |
| 포맷 라벨 / Format Label | "48kHz 512 Stereo" 형식 / format |
| 포터블 라벨 / Portable Label | 포터블 모드에서만 표시 (노란 배경) / Shown only in portable mode (yellow background) |
| Credit 링크 / Credit Link | "Created by LiveTrack". 업데이트 가용 시 "NEW vX.Y.Z" 주황색 / "NEW vX.Y.Z" in orange when update available |

#### 4.6.7 NotificationBar (알림 시스템 / Notification System)
| 항목 / Item | 상세 / Details |
|------|------|
| 위치 / Position | 상태 바 영역 위에 일시적 오버레이 (레이턴시/CPU 라벨 대체) / Temporary overlay above status bar area (replaces latency/CPU labels) |
| 색상 / Colors | Info=보라 / purple (0xFF6C63FF), Warning=주황 / orange (0xFFCC8844), Error=빨강 / red (0xFFE05050), Critical=밝은빨강 / bright red (0xFFFF0000) |
| 자동 페이드 / Auto-Fade | 심각도에 따라 3~8초 / 3~8 seconds depending on severity |
| 트리거 / Triggers | 오디오 장치 에러, 플러그인 로드 실패, 녹음 실패, 장치 재연결 / Audio device error, plugin load failure, recording failure, device reconnection |
| 큐 오버플로 / Queue Overflow | 링 버퍼 용량 확인 후 쓰기 (오버플로 가드) / Checks ring buffer capacity before write (overflow guard) |

#### 4.6.8 시스템 트레이 / System Tray
| 동작 / Action | 결과 / Result |
|------|------|
| 창 닫기 (X) / Close Window (X) | 트레이로 최소화 (종료 아님) / Minimize to tray (not quit) |
| 트레이 좌클릭/더블클릭 / Tray Left/Double Click | 창 복원 + 포커스 / Restore window + focus |
| 트레이 우클릭 / Tray Right Click | 메뉴 / Menu: Show/Hide, 자동 시작 토글 / auto-start toggle, Quit |
| 마우스 호버 / Mouse Hover | 동적 툴팁: 프리셋명, 플러그인 수, 볼륨, 뮤트 상태 / Dynamic tooltip: preset name, plugin count, volume, mute state |
| 아이콘 / Icon | 16px/32px 듀얼 사이즈 / dual-size PNG |

#### 4.6.9 테마 / Theme (DirectPipeLookAndFeel)
| 색상 / Color | 값 / Value | 용도 / Usage |
|------|-----|------|
| 배경 / Background | `#1E1E2E` | 메인 배경 (다크 블루그레이) / Main background (dark blue-grey) |
| 표면 / Surface | `#2A2A40` | 컴포넌트 배경 / Component background |
| 액센트 / Accent | `#6C63FF` | 보라 (버튼, 하이라이트) / Purple (buttons, highlights) |
| 액센트2 / Accent2 | `#4CAF50` | 초록 (활성 상태) / Green (active state) |
| 텍스트 / Text | `#E0E0E0` | 기본 텍스트 (밝은 회색) / Default text (light grey) |
| 흐린 텍스트 / Dim Text | `#8888AA` | 보조 텍스트 (흐린 보라) / Secondary text (dim purple) |
| 빨강 / Red | `#E05050` | 에러, 뮤트 상태 / Error, mute state |

**CJK 폰트 지원 / CJK Font Support:**
- `getTypefaceForFont()` 오버라이드로 전역 적용 / Applied globally via `getTypefaceForFont()` override
- Bold Malgun Gothic — ComboBox, PopupMenu, ToggleButton
- 한국어 장치명 □□□ 깨짐 방지 / Prevents Korean device name □□□ rendering issues

#### 4.6.10 레벨 미터 / Level Meter (LevelMeter)
| 항목 / Item | 상세 / Details |
|------|------|
| 표시 / Display | 현재 RMS 레벨 (초록→노랑→빨강) / Current RMS level (green→yellow→red) |
| 피크 홀드 / Peak Hold | 피크 인디케이터 (30틱 ≈ 1초 @30Hz) / Peak indicator (30 ticks ≈ 1 second @30Hz) |
| 클리핑 / Clipping | 0dBFS 클리핑 인디케이터 / 0dBFS clipping indicator |
| 스무딩 / Smoothing | Attack: 0.3, Release: 0.05 |
| 갱신 / Update | 부모 타이머에서 `tick()` 호출 (30Hz). 조건부 repaint / `tick()` called from parent timer (30Hz). Conditional repaint |

---

### 4.7 패닉 뮤트 / Panic Mute

| 항목 / Item | 상세 / Details |
|------|------|
| 동작 / Behavior | 즉시 모든 오디오 출력 뮤트 + 녹음 자동 중지 / Instantly mute all audio outputs + auto-stop recording |
| 잠금 / Lock | 패닉 뮤트 중 OUT/MON/VST 버튼 잠금 / OUT/MON/VST buttons locked during panic mute |
| 외부 제어 잠금 / External Control Lock | 핫키/MIDI/Stream Deck/HTTP 모두 잠금 — PanicMute 액션만 허용 / All locked — only PanicMute action allowed |
| 액션 차단 / Action Blocking | 바이패스, 볼륨, 게인, 프리셋, 녹음, 플러그인 파라미터 등 모든 액션 차단 / All actions blocked: bypass, volume, gain, presets, recording, plugin parameters, etc. |
| 녹음 / Recording | 녹음 중이면 자동 중지 (해제 시 자동 재시작 안 함) / Auto-stops if recording (does not auto-restart on unmute) |
| 모니터 상태 / Monitor State | 뮤트 전 모니터/출력/IPC 활성 상태 기억, 언뮤트 시 복원 / Remembers pre-mute monitor/output/IPC active state, restores on unmute |
| 상태 지속 / State Persistence | 패닉 뮤트 상태가 재시작 후에도 유지됨 / Panic mute state persists across restarts |
| 해제 / Release | PanicMute 액션으로만 해제 가능 / Can only be released by PanicMute action |
| input_muted | `muted`와 동일 (독립 입력 뮤트 없음, InputMuteToggle = PanicMute) / Same as `muted` (no independent input mute, InputMuteToggle = PanicMute) |

---

### 4.8 DirectPipe Receiver 플러그인 / DirectPipe Receiver Plugin (VST2/VST3/AU)

#### 기본 정보 / Basic Info
| 항목 / Item | 상세 / Details |
|------|------|
| 이름 / Name | DirectPipe Receiver |
| 포맷 / Format | VST2 / VST3 / AU (macOS) |
| 입력 / Input | **없음 (입력 버스 없음)** / **None (no input bus)** — `BusesProperties().withOutput(...)` only, no `.withInput()` |
| 출력 / Output | 스테레오 (2채널) 또는 모노 (1채널) / Stereo (2ch) or Mono (1ch). `isBusesLayoutSupported`가 mono와 stereo 모두 허용 / accepts both mono and stereo |
| 레이턴시 보고 / Latency Reporting | `setLatencySamples(targetFillFrames)` — 버퍼 프리셋 크기를 호스트 DAW에 보고 / Reports buffer preset size to host DAW. 프리셋 변경 시 processBlock 내에서 동적 갱신 / Dynamically updated in processBlock on preset change |
| MIDI | 없음 / None |
| 프로그램 / Programs | 1개 (전환 없음) / 1 (no switching) |

#### 핵심 동작 원리 / Core Operating Principle
DirectPipe Receiver는 **입력 버스가 없는 출력 전용 플러그인**이다. `processBlock()`에서 호스트(OBS)가 전달하는 입력 오디오 버퍼를 IPC 공유 메모리에서 읽은 데이터로 **완전히 덮어쓴다**. 따라서:

DirectPipe Receiver is an **output-only plugin with no input bus**. In `processBlock()`, it completely **overwrites** the host (OBS) input audio buffer with data read from IPC shared memory. Therefore:

- OBS 오디오 소스(마이크 캡처 등)의 원본 오디오는 **무시됨** / Original audio from OBS audio sources (mic capture, etc.) is **ignored**
- OBS 필터 체인에서 Receiver 앞에 있는 다른 필터의 출력도 **무시됨** / Output from other filters preceding Receiver in the OBS filter chain is also **ignored**
- DirectPipe에서 VST 체인을 거쳐 IPC로 전송된 **최종 처리 오디오만 출력** / Only the **final processed audio** sent via IPC through DirectPipe's VST chain is output

이 설계 덕분에 OBS 마이크 소스의 자체 오디오와 DirectPipe 오디오가 중복되지 않으며, IPC 토글(VST 버튼)로 OBS 마이크를 독립적으로 ON/OFF 할 수 있다.

This design prevents audio duplication between OBS mic source audio and DirectPipe audio, and enables independent OBS mic ON/OFF control via IPC toggle (VST button).

DirectPipe Receiver is an **output-only plugin with no input bus**. In `processBlock()`, it completely replaces the host (OBS) buffer with data read from IPC shared memory. Therefore: OBS source audio is ignored, preceding filters are ignored, only DirectPipe's final processed audio is output. This design prevents audio duplication and enables independent OBS mic control via IPC toggle (VST button).

#### 파라미터 / Parameters (AudioProcessorValueTreeState)
| ID | 타입 / Type | 기본값 / Default | 설명 / Description |
|----|------|--------|------|
| `mute` | Boolean | false | 오디오 뮤트 / Audio mute |
| `buffer` | Choice (0-4) | 1 (Low) | 버퍼 프리셋 선택 / Buffer preset selection |

#### 버퍼 프리셋 / Buffer Presets
| # | 이름 / Name | targetFillFrames | highFillThreshold | lowFillThreshold | 레이턴시 / Latency @48kHz |
|---|------|-----------------|-------------------|------------------|----------------|
| 0 | Ultra Low | 256 | 768 | 64 | ~5ms |
| 1 | Low | 512 | 1536 | 128 | ~10ms |
| 2 | Medium | 1024 | 3072 | 256 | ~21ms |
| 3 | High | 2048 | 6144 | 512 | ~42ms |
| 4 | Safe | 4096 | 12288 | 1024 | ~85ms |

#### IPC 연결 / IPC Connection
| 항목 / Item | 상세 / Details |
|------|------|
| 공유 메모리 이름 / Shared Memory Name | `Local\\DirectPipeAudio` |
| 프로토콜 / Protocol | SPSC 링 버퍼 / SPSC ring buffer, atomic read/write 포지션 / positions. RingBuffer에 atomic `detached_` 플래그 / flag (detach 시 읽기/쓰기 즉시 차단 / immediately blocks read/write on detach) |
| 연결 확인 / Connection Check | `producer_active` 플래그 / flag (acquire) |
| 재연결 간격 / Reconnection Interval | 100 블록마다 / Every 100 blocks |
| 드리프트 워밍업 / Drift Warmup | 50 블록 후 클록 드리프트 체크 시작 / Clock drift checks start after 50 blocks |

#### 오디오 처리 / Audio Processing
0. 인터리브 버퍼 empty 가드 → prepareToPlay 전 호출 시 즉시 무음 반환 / Interleaved buffer empty guard → immediate silence return if called before prepareToPlay
1. Mute 확인 → 뮤트면 버퍼 클리어 / Check mute → clear buffer if muted
2. 미연결: 페이드아웃 또는 무음 / Not connected: fade-out or silence
3. 연결 시: producer 활성 확인 / On connection: check producer active. **Stale consumer_active 감지 / detection**: detach() 후 close() 호출하여 OBS 크래시 등으로 남은 spurious multi-consumer 경고 방지 / call detach() then close() to prevent spurious multi-consumer warnings left by OBS crash, etc.
4. 클록 드리프트 보상: 버퍼 > highThreshold이면 초과 프레임 스킵 / Clock drift compensation: skip excess frames when buffer > highThreshold
5. 링 버퍼에서 프레임 읽기 / Read frames from ring buffer
6. 인터리브 → JUCE planar 변환 / Interleaved → JUCE planar conversion
7. 부분 읽기 시 패딩 (무음) / Padding with silence on partial read

#### Clock Drift Compensation

호스트와 DAW/OBS의 오디오 클록이 미세하게 다를 때 발생하는 버퍼 드리프트를 자동 보상.

Automatically compensates buffer drift caused by slight differences between the host and DAW/OBS audio clocks.

| 상태 / State | 조건 / Condition | 동작 / Behavior |
|------|------|------|
| 정상 / Normal | lowThreshold/2 ≤ fill ≤ highThreshold | 그대로 읽기 / Read as-is |
| 버퍼 과다 (호스트 빠름) / Buffer Overflow (host faster) | fill > highThreshold | excess 프레임 스킵 → targetFill로 복귀 / Skip excess frames → return to targetFill |
| 데드 밴드 (히스테리시스) / Dead Band (hysteresis) | lowThreshold/2 ≤ fill < lowThreshold | 정상 읽기 — 스로틀/정상 모드 간 진동 방지 / Normal read — prevents oscillation between throttle/normal mode |
| 버퍼 부족 (호스트 느림) / Buffer Underflow (host slower) | fill < lowThreshold/2 | 읽기량 절반으로 쿠션 확보 → 하드 클릭 대신 미세 갭 / Halve read amount for cushion → micro-gaps instead of hard clicks |

버퍼 프리셋별 임계값 / Thresholds per buffer preset:

| 프리셋 / Preset | Target | High Threshold | Low Threshold |
|--------|--------|----------------|---------------|
| Ultra Low (256) | 256 | 768 | 64 |
| Low (512) | 512 | 1536 | 128 |
| Medium (1024) | 1024 | 3072 | 256 |
| High (2048) | 2048 | 6144 | 512 |
| Safe (4096) | 4096 | 12288 | 1024 |

#### 페이드아웃 로직 / Fade-Out Logic
- 마지막 출력 버퍼: 64 샘플 (planar 형식) / Last output buffer: 64 samples (planar format)
- 페이드 게인: 1.0 → 0.0, 샘플당 0.05 감소 / Fade gain: 1.0 → 0.0, decreasing by 0.05 per sample
- 언더런 시 마지막 샘플을 페이딩하며 재생 / On underrun, plays last samples while fading

#### 에디터 UI / Editor UI (240×200px)
| 요소 / Element | 설명 / Description |
|------|------|
| 타이틀 / Title | "DirectPipe Receiver" (볼드 화이트 / bold white, 16pt) |
| 상태 원 / Status Circle | 초록(연결) / 빨강(미연결) / Green (connected) / Red (disconnected) |
| 상태 텍스트 / Status Text | "Connected" / "Disconnected" |
| 오디오 정보 / Audio Info | 연결 시 / When connected: "{SR}Hz {Channels}ch" |
| Mute 버튼 / Mute Button | 빨강(ON) / 어두운(OFF) / Red (ON) / Dark (OFF), 40px 높이 / height |
| Buffer ComboBox | 5개 프리셋 / 5 presets |
| 레이턴시 라벨 / Latency Label | "X.XX ms (YYYY samples @ ZZZZ Hz)" |
| SR 경고 / SR Warning | "SR mismatch: {source} vs {host}" (주황 / orange, 10pt) |
| 버전 / Version | "v4.0.1" (우하단 / bottom-right, 10pt) |
| 갱신 / Update | 10Hz 타이머 콜백 / 10Hz timer callback |

---

### 4.9 IPC 코어 라이브러리 / IPC Core Library

#### 프로토콜 헤더 / Protocol Header (DirectPipeHeader)
```
alignas(64) atomic<uint64_t> write_pos    — 프로듀서 증가 / producer increments
alignas(64) atomic<uint64_t> read_pos     — 컨슈머 증가 / consumer increments
uint32_t sample_rate                       — 샘플레이트 / sample rate
uint32_t channels                          — 채널 수 / channel count
uint32_t buffer_frames                     — 버퍼 프레임 수 / buffer frame count
uint32_t version                           — 프로토콜 버전 / protocol version (1)
atomic<bool> producer_active               — 프로듀서 활성 플래그 / producer active flag
```
64바이트 정렬 (false sharing 방지) / 64-byte alignment (prevents false sharing)

#### 공유 메모리 레이아웃 / Shared Memory Layout
```
[Header (64+ bytes)] [Ring Buffer PCM (interleaved floats)]
```
크기 / Size = sizeof(Header) + (buffer_frames × channels × sizeof(float))

#### 상수 / Constants
| 상수 / Constant | 값 / Value | 설명 / Description |
|------|-----|------|
| SHM_NAME | Windows: `Local\\DirectPipeAudio`, POSIX: `/DirectPipeAudio` | 공유 메모리 이름 / Shared memory name |
| EVENT_NAME | `Local\\DirectPipeDataReady` | 이벤트 이름 / Event name |
| DEFAULT_BUFFER_FRAMES | 16384 | ~341ms @48kHz |
| DEFAULT_SAMPLE_RATE | 48000 | 기본 SR / Default SR |
| DEFAULT_CHANNELS | 2 | 스테레오 / Stereo |
| PROTOCOL_VERSION | 1 | 프로토콜 버전 / Protocol version |

#### SharedMemWriter (호스트 측 / Host Side)
- `initialize(sampleRate, channels, bufferFrames)` — 공유 메모리 생성 / Creates shared memory
- `writeAudio(buffer, numSamples)` — RT-safe. 사전 할당된 인터리브 버퍼로 변환 후 쓰기 / Converts to pre-allocated interleaved buffer then writes
- `shutdown()` — `producer_active` false 설정 → 5ms 대기 → 메모리/이벤트 해제 / Sets `producer_active` false → 5ms wait → releases memory/event

---

### 4.10 오디오 녹음 / Audio Recording (AudioRecorder)

| 항목 / Item | 상세 / Details |
|------|------|
| 포맷 / Format | WAV, 24-bit |
| FIFO | 32768 샘플 / samples (~0.68초 / seconds @48kHz) |
| 락 / Lock | `juce::SpinLock` (RT-safe) — writer teardown 보호 / writer teardown protection |
| 스레드 / Thread | `juce::TimeSliceThread "Audio Writer"` — FIFO → 디스크 / disk |
| 시작 / Start | 부모 디렉토리 생성, samplesWritten 리셋, ThreadedWriter 생성 / Create parent directory, reset samplesWritten, create ThreadedWriter |
| 정지 / Stop | recording_ false (seq_cst) → SpinLock 획득 / acquire → writer 파괴 (FIFO 플러시) / destroy writer (FIFO flush) |
| 쓰기 / Write | recording_ 확인 / check (acquire) → SpinLock → ThreadedWriter 쓰기 / write → samplesWritten 증가 / increment |
| 자동 정지 / Auto-Stop | 오디오 장치 변경 시 / On audio device change |
| 시간 표시 / Time Display | Timer 기반 / Timer-based. `getRecordedSeconds() = samplesWritten / sampleRate` |

---

### 4.11 설정 관리 / Settings Management

#### 4.11.1 자동 저장 / Auto-Save
- Dirty-flag 패턴 + 1초 디바운스 (30Hz 타이머에서 30틱 카운트다운) / Dirty-flag pattern + 1-second debounce (30-tick countdown at 30Hz timer)
- `onSettingsChanged` 콜백 → `markSettingsDirty()` → 카운터 리셋 → 0 도달 시 `saveSettings()` / callback → `markSettingsDirty()` → counter reset → `saveSettings()` on reaching 0
- `loadingSlot_` 또는 `isLoading()` true면 저장 스킵 (부분 상태 손상 방지) / Saves skipped when `loadingSlot_` or `isLoading()` is true (prevents partial state corruption)
- 저장 위치 / Storage location: Windows `%AppData%/DirectPipe/settings.dppreset`, macOS `~/Library/Application Support/DirectPipe/`, Linux `~/.config/DirectPipe/` (JSON, 버전 / version 4)

#### 4.11.2 프리셋 파일 형식 / Preset File Format (JSON, version 4)
```json
{
  "version": 4,
  "activeSlot": 0,
  "sampleRate": 48000,
  "bufferSize": 256,
  "inputGain": 1.0,
  "deviceType": "Windows Audio",
  "inputDevice": "USB Audio Device",
  "outputDevice": "Headphones",
  "outputNone": false,
  "channelMode": 2,
  "ipcEnabled": false,
  "outputMuted": false,
  "plugins": [
    {
      "name": "FabFilter Pro-Q",
      "path": "C:\\Program Files\\...",
      "descXml": "...",
      "bypassed": false,
      "state": "base64-encoded-plugin-state"
    }
  ],
  "outputs": {
    "monitorVolume": 0.8,
    "monitorEnabled": true,
    "monitorDevice": "Headphones",
    "monitorBufferSize": 256
  }
}
```

#### 4.11.3 백업/복원 (2단계) / Backup/Restore (2 Tiers)

**플랫폼 호환성 / Platform Compatibility**: `importAll()` / `importFullBackup()` 함수가 내부적으로 플랫폼 호환성을 검사하여, 다른 OS에서 만든 백업의 복원을 차단한다 (UI 레벨이 아닌 API 레벨에서 보호).

`importAll()` / `importFullBackup()` functions internally check platform compatibility, blocking restoration of backups created on a different OS (protected at API level, not UI level).

**Tier 1: Settings Only (`.dpbackup`, v2)**
| 포함 / Included | 제외 / Excluded |
|------|------|
| 오디오 설정 (드라이버, 장치, SR, BS, 채널) / Audio settings (driver, device, SR, BS, channels) | VST 플러그인 체인 / VST plugin chain (`plugins` 키 제거 / key removed) |
| 출력 설정 (모니터 장치, 볼륨, 버퍼) / Output settings (monitor device, volume, buffer) | 프리셋 슬롯 A-E / Preset slots A-E |
| 컨트롤 매핑 (핫키, MIDI, 서버 설정) / Control mappings (hotkeys, MIDI, server settings) | |

```json
{
  "version": 2,
  "platform": "windows",
  "exportDate": "2025-03-06T14:30:00Z",
  "appVersion": "4.0.1",
  "audioSettings": { /* plugins 키 제거됨 */ },
  "controlConfig": {
    "hotkeys": [...],
    "midi": [...],
    "server": {"websocketPort": 8765, "websocketEnabled": true}
  }
}
```

**Tier 2: Full Backup (`.dpfullbackup`, v2)**
| 포함 / Included |
|------|
| Tier 1 전체 내용 / All Tier 1 content |
| VST 플러그인 체인 (전체 상태) / VST plugin chain (full state) |
| 프리셋 슬롯 A-E (각 슬롯 JSON) / Preset slots A-E (each slot JSON) |

```json
{
  "version": 2,
  "type": "full",
  "platform": "windows",
  "exportDate": "...",
  "appVersion": "4.0.1",
  "audioSettings": { /* plugins 포함 */ },
  "controlConfig": { /* ... */ },
  "presetSlots": {
    "A": { /* slot A JSON */ },
    "B": { /* slot B JSON */ },
    "C": null,
    "D": null,
    "E": null
  }
}
```

#### 4.11.4 유지보수 / Maintenance (Settings > Maintenance)
| 기능 / Feature | 동작 / Action | 확인 / Confirmation |
|------|------|------|
| Full Backup | `.dpfullbackup`으로 전체 백업 / Full backup to `.dpfullbackup` | 파일 선택 / File chooser |
| Full Restore | `.dpfullbackup`에서 전체 복원 (같은 OS만 — 크로스 OS 복원 차단) / Full restore from `.dpfullbackup` (same OS only — cross-OS restore blocked) | 확인 다이얼로그 / Confirmation dialog |
| Clear Plugin Cache | 스캔 캐시 XML 삭제 / Delete scan cache XML | 확인 다이얼로그 / Confirmation dialog |
| Clear All Presets | 슬롯 A-E + Auto 파일 + 백업 + 임시 파일 삭제, 활성 체인 초기화 / Delete slots A-E + Auto files + backups + temp files, reset active chain | 확인 다이얼로그 / Confirmation dialog |
| Factory Reset | 모든 데이터 삭제 (설정, 컨트롤, 프리셋(A-E + Auto), 캐시, 녹음 설정) / Delete all data (settings, controls, presets (A-E + Auto), cache, recording config) | 확인 다이얼로그 / Confirmation dialog |

---

### 4.12 로깅 시스템 / Logging System

#### DirectPipeLogger
| 항목 / Item | 상세 / Details |
|------|------|
| 링 버퍼 / Ring Buffer | 최대 512개 대기 항목 / Max 512 pending entries |
| 인덱스 / Indices | `writeIdx_`, `readIdx_` (atomic<uint32_t>, 오버플로 안전 / overflow-safe) |
| 뮤텍스 / Mutex | `std::mutex writeMutex_` (MPSC 안전 / MPSC safe) |
| 로그 파일 / Log File | `%AppData%/DirectPipe/directpipe.log` |
| drain | 메시지 스레드에서만 호출. 모든 대기 라인 일괄 읽기 → 단일 TextEditor 삽입 → 단일 trim / Called only from message thread. Batch read all pending lines → single TextEditor insert → single trim |

#### 로그 카테고리 태그 / Log Category Tags
`[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`

#### 로그 레벨 / Log Levels
- `info(cat, msg)` → `"INF [CAT] {msg}"`
- `warn(cat, msg)` → `"WRN [CAT] {msg}"`
- `error(cat, msg)` → `"ERR [CAT] {msg}"`
- `audit(cat, msg)` → `"AUD [CAT] {msg}"` (Audit Mode 활성 시에만)

#### 스코프드 타이머 / Scoped Timer
```cpp
{ Log::Timer t("VST", "Chain swap"); ... }
// 스코프 종료 시 / On scope exit: "INF [VST] Chain swap (15ms)"
```

#### 로깅 제외 (스팸 방지) / Logging Exclusions (Spam Prevention)
- 고빈도 액션 / High-frequency actions: SetVolume, InputGainAdjust, SetPluginParameter
- Continuous MIDI 바인딩 / bindings

---

### 4.13 자동 업데이트 / Auto-Update

| 항목 / Item | 상세 / Details |
|------|------|
| 체크 / Check | 시작 시 백그라운드 스레드에서 GitHub API (최신 릴리즈) / Background thread checks GitHub API (latest release) on startup |
| 비교 / Comparison | Semver 비교 / Semver comparison |
| 표시 / Display | Credit 라벨에 "NEW vX.Y.Z" (주황색) / "NEW vX.Y.Z" (orange) in credit label |
| 다이얼로그 / Dialog | [Update Now] (Windows only) / [View on GitHub] / [Later] |
| 업데이트 / Update | Windows: GitHub 릴리즈에서 ZIP 다운로드 → batch 스크립트로 바이너리 교체 → 자동 재시작 / Downloads ZIP from GitHub release → batch script replaces binary → auto-restart. macOS/Linux: "View on GitHub" 버튼으로 릴리즈 페이지 안내 (수동 다운로드) / "View on GitHub" button opens release page (manual download) |
| 완료 확인 / Completion Verification | `_updated.flag` 파일 → 다음 시작 시 "Updated successfully" 알림 / `_updated.flag` file → "Updated successfully" notification on next startup |
| 스레드 안전 / Thread Safety | `alive_` 플래그 (`shared_ptr<atomic<bool>>`) 사용 / Uses `alive_` flag (`shared_ptr<atomic<bool>>`) (`checkForUpdate` callAsync) |

---

### 4.14 시스템 기능 / System Features

| 기능 / Feature | 상세 / Details |
|------|------|
| **시스템 시작 / System Startup** | Windows: 레지스트리 / Registry `HKCU\...\Run\DirectPipe`. macOS: LaunchAgent (`~/Library/LaunchAgents/com.directpipe.host.plist`). Linux: `~/.config/autostart/directpipe.desktop`. 트레이 메뉴 + Settings 탭에서 토글 / Toggle in tray menu + Settings tab |
| **단일 인스턴스 / Single Instance** | `moreThanOneInstanceAllowed()` = false. `--scan` 인자 시에만 복수 허용 / Multiple instances only with `--scan` argument. 포터블 모드에서는 true (다중 인스턴스 허용, 폴더별 InterProcessLock으로 같은 폴더 중복만 차단) / true in portable mode (multiple instances allowed, per-folder InterProcessLock blocks same-folder duplicates only) |
| **프로세스 우선순위 / Process Priority** | `HIGH_PRIORITY_CLASS` |
| **포터블 모드 / Portable Mode** | exe 옆 `portable.flag` → 설정을 `./config/`에 저장 / `portable.flag` next to exe → config stored in `./config/`. `ControlMappingStore::getConfigDirectory()` 사용 / used |
| **다중 인스턴스 외부 제어 / Multi-Instance External Control** | Windows: Named Mutex. macOS/Linux: file-based lock. 외부 제어(핫키/MIDI/WS/HTTP) 충돌 방지 / Prevents external control (hotkey/MIDI/WS/HTTP) conflicts. 일반 모드 실행 중 → 포터블 Audio Only / Normal mode running → portable Audio Only. 포터블 제어 중 → 일반 모드 차단 / Portable controlling → normal mode blocked. 포터블 2개+ → 첫 번째만 제어 / 2+ portables → only first gets control. 타이틀 바 / Title bar "(Audio Only)", 상태 바 주황색 / status bar orange "Portable", 트레이 / tray "(Portable/Audio Only)" 표시 / display |
| **콤보박스 새로고침 / ComboBox Refresh** | `addMouseListener(this, true)` → `mouseDown`에서 `scanForDevices()` + 목록 재빌드 / `scanForDevices()` + list rebuild in `mouseDown`. `eventComponent` 체크로 배경 클릭 방지 / `eventComponent` check prevents background click. 소멸자에서 `removeMouseListener` / `removeMouseListener` in destructor |

---

## 5. 기술 스택 / Tech Stack

| 항목 / Item | 기술 / Technology | 버전 / Version |
|------|------|------|
| 언어 / Language | C++17 | — |
| 프레임워크 / Framework | JUCE | 7.0.12 |
| 빌드 / Build | CMake | 3.22+ |
| 오디오 / Audio (Windows) | WASAPI Shared + Low Latency (IAudioClient3) + Exclusive | Windows 내장 / built-in |
| 오디오 / Audio (Windows) | ASIO — Steinberg ASIO SDK | `thirdparty/asiosdk/common` |
| 오디오 / Audio (macOS) | CoreAudio | macOS 내장 / built-in |
| 오디오 / Audio (Linux) | ALSA, JACK | Linux 내장 / built-in |
| VST2 | VST2 SDK | 2.4 |
| VST3 | JUCE 내장 VST3 지원 / JUCE built-in VST3 support | — |
| WebSocket | JUCE StreamingSocket + RFC 6455 | 커스텀 SHA-1 / Custom SHA-1 |
| HTTP | JUCE StreamingSocket 수동 파싱 / manual parsing | — |
| Stream Deck | @elgato/streamdeck SDK | v2.0.1 (SDKVersion 3) |
| Stream Deck WebSocket | ws (npm) | v8.16.0 |
| 번들러 / Bundler | Rollup | v4.59.0 |
| 테스트 / Testing | Google Test | — |
| IPC | SharedMemory + lock-free RingBuffer | Windows: CreateFileMapping, macOS: `shm_open`, Linux: POSIX shm |

---

## 6. 파일 구조 / File Structure

```
DirectPipe/
├── core/                           → IPC 라이브러리 / IPC library
│   └── include/directpipe/
│       ├── Constants.h             → SHM_NAME, DEFAULT_BUFFER_FRAMES 등 / etc.
│       ├── Protocol.h              → DirectPipeHeader (64바이트 정렬 / 64-byte aligned)
│       ├── RingBuffer.h            → SPSC lock-free 링 버퍼 / ring buffer
│       └── SharedMemory.h          → Windows 공유 메모리 / shared memory + NamedEvent
│
├── host/                           → JUCE 메인 앱 / JUCE main app
│   ├── Resources/                  → 아이콘 / Icons (16~512px PNG + SVG)
│   └── Source/
│       ├── Main.cpp                → 앱 진입점, 트레이, 스캐너 모드 / App entry point, tray, scanner mode
│       ├── MainComponent.h/cpp     → 메인 UI 레이아웃 / Main UI layout (~729줄/lines, 헬퍼 클래스에 위임 / delegates to helper classes)
│       ├── ActionResult.h          → 성공/실패 반환 타입 / Success/failure return type (ok/fail/bool 변환 / conversion)
│       ├── Audio/
│       │   ├── AudioEngine.h/cpp       → 오디오 콜백, 장치 관리, 재연결, XRun / Audio callback, device management, reconnection, XRun
│       │   ├── VSTChain.h/cpp          → 플러그인 체인, 그래프, 비동기 로딩 / Plugin chain, graph, async loading
│       │   ├── OutputRouter.h/cpp      → 모니터 출력 라우팅 / Monitor output routing
│       │   ├── MonitorOutput.h/cpp     → 별도 WASAPI 모니터 장치 / Separate WASAPI monitor device
│       │   ├── AudioRingBuffer.h       → Lock-free 스테레오 링 버퍼 / Lock-free stereo ring buffer
│       │   ├── AudioRecorder.h/cpp     → WAV 녹음 / WAV recording (ThreadedWriter)
│       │   ├── PluginPreloadCache.h/cpp → 슬롯 백그라운드 프리로드 / Slot background preloading
│       │   ├── LatencyMonitor.h        → 실시간 레이턴시/CPU 측정 / Real-time latency/CPU measurement
│       │   ├── SafetyLimiter.h/cpp     → RT-safe 피드포워드 리미터 / RT-safe feed-forward limiter
│       │   ├── DeviceState.h           → 장치 연결 상태 enum 상태 머신 / Device connection state enum state machine
│       │   ├── BuiltinFilter.h/cpp     → HPF+LPF 오디오 프로세서 / HPF+LPF audio processor
│       │   ├── BuiltinNoiseRemoval.h/cpp → RNNoise 기반 노이즈 제거 / RNNoise-based noise removal
│       │   ├── BuiltinAutoGain.h/cpp   → LUFS 기반 자동 게인 제어 / LUFS-based auto gain control
│       │   └── PluginLoadHelper.h      → 크로스 플랫폼 VST 로딩 헬퍼 / Cross-platform VST loading helper
│       ├── Control/
│       │   ├── ActionDispatcher.h      → 19개 Action enum, 메시지 스레드 디스패치 / 19 Action enums, message thread dispatch
│       │   ├── ActionHandler.h/cpp     → 중앙 액션 이벤트 처리 / Central action event handling (MainComponent에서 추출 / extracted from MainComponent)
│       │   ├── SettingsAutosaver.h/cpp → dirty-flag + 디바운스 자동 저장 / dirty-flag + debounce auto-save (MainComponent에서 추출 / extracted from MainComponent)
│       │   ├── ControlManager.h        → 컨트롤 핸들러 소유 / Control handler ownership, configStore_
│       │   ├── ControlMapping.cpp      → 기본 핫키 11개 / 11 default hotkeys
│       │   ├── WebSocketServer.h/cpp   → RFC 6455, UDP 디스커버리 / discovery, 19 명령 / commands
│       │   ├── HttpApiServer.cpp       → REST API, 23 엔드포인트 / endpoints, CORS
│       │   ├── HotkeyHandler.h/cpp     → RegisterHotKey, 글로벌 핫키 / global hotkeys
│       │   ├── MidiHandler.h/cpp       → CC/Note, Learn, 4 바인딩 타입 / 4 binding types
│       │   ├── StateBroadcaster.h/cpp  → 26필드 상태 JSON / 26-field state JSON, 해시 기반 더티 체크 / hash-based dirty check
│       │   ├── DirectPipeLogger.h/cpp  → 링 버퍼 로깅 / Ring buffer logging, 13 카테고리 / categories
│       │   └── Log.h/cpp              → 로그 유틸리티 / Log utilities, 스코프드 타이머 / scoped timer
│       ├── IPC/
│       │   └── SharedMemWriter.h/cpp   → RT-safe IPC 쓰기, 인터리브 변환 / RT-safe IPC write, interleaved conversion
│       └── UI/
│           ├── AudioSettings.h/cpp     → 5종 드라이버 / 5 driver types, ASIO 채널 / channels, SR/BS
│           ├── OutputPanel.h/cpp       → 모니터 + IPC + 녹음 UI / Monitor + IPC + recording UI
│           ├── ControlSettingsPanel.h/cpp → 3 서브탭 컨테이너 / 3 sub-tab container (탭별 별도 클래스 / separate class per tab)
│           ├── HotkeyTab.h/cpp         → 핫키 설정 탭 / Hotkey settings tab (ControlSettingsPanel에서 추출 / extracted from ControlSettingsPanel)
│           ├── MidiTab.h/cpp           → MIDI 설정 탭 / MIDI settings tab (ControlSettingsPanel에서 추출 / extracted from ControlSettingsPanel)
│           ├── StreamDeckTab.h/cpp     → Stream Deck 설정 탭 / Stream Deck settings tab (ControlSettingsPanel에서 추출 / extracted from ControlSettingsPanel)
│           ├── PresetSlotBar.h/cpp     → 프리셋 슬롯 A-E 버튼 바 / Preset slot A-E button bar (MainComponent에서 추출 / extracted from MainComponent)
│           ├── StatusUpdater.h/cpp     → 상태 바 업데이트 / Status bar updates (MainComponent에서 추출 / extracted from MainComponent)
│           ├── UpdateChecker.h/cpp     → 백그라운드 업데이트 체크 / Background update checker (MainComponent에서 추출 / extracted from MainComponent)
│           ├── PluginChainEditor.h/cpp → 드래그드롭, 바이패스, GUI 편집 / Drag-drop, bypass, GUI editing
│           ├── PluginScanner.h/cpp     → Out-of-process 스캔, 검색/정렬 / Out-of-process scan, search/sort
│           ├── PresetManager.h/cpp     → 슬롯 A-E, 자동 저장 / Slots A-E, auto-save, Copy/Delete
│           ├── LevelMeter.h/cpp        → RMS 미터, 피크 홀드, 스무딩 / RMS meter, peak hold, smoothing
│           ├── LogPanel.h/cpp          → 로그 뷰어, 유지보수, 팩토리 리셋 / Log viewer, maintenance, factory reset
│           ├── NotificationBar.h/cpp   → 컬러코딩 알림, 자동 페이드 / Color-coded notifications, auto-fade
│           ├── DirectPipeLookAndFeel.h/cpp → 다크 테마, CJK 폰트 / Dark theme, CJK fonts
│           ├── SettingsExporter.h/cpp  → 2단계 백업 / 2-tier backup (.dpbackup / .dpfullbackup)
│           ├── FilterEditPanel.h/cpp   → 내장 필터 편집 패널 / Built-in filter edit panel
│           ├── NoiseRemovalEditPanel.h/cpp → 노이즈 제거 편집 패널 / Noise removal edit panel
│           ├── AGCEditPanel.h/cpp      → 자동 게인 제어 편집 패널 / Auto gain control edit panel
│           └── DeviceSelector.h/cpp    → 오디오 장치 선택 위젯 / Audio device selection widget
│
├── plugins/receiver/               → DirectPipe Receiver (VST2/VST3/AU)
│   └── Source/
│       ├── PluginProcessor.h/cpp   → IPC 소비자, 5 버퍼 프리셋, 페이드아웃 / IPC consumer, 5 buffer presets, fade-out
│       └── PluginEditor.h/cpp      → 240×200 UI, 상태/SR 경고 / 240×200 UI, status/SR warnings
│
├── com.directpipe.directpipe.sdPlugin/ → Stream Deck 플러그인 / Stream Deck plugin
│   ├── manifest.json               → SDKVersion 3, 10 액션 / actions, v4.0.1.0
│   ├── package.json                → ws v8.16, @elgato/streamdeck v2.0.1
│   └── src/
│       ├── plugin.js               → 진입점, UDP 디스커버리, 상태 관리 / Entry point, UDP discovery, state management
│       ├── websocket-client.js     → WS 클라이언트, 재연결, 큐잉 / WS client, reconnection, queuing
│       └── actions/                → 10개 SingletonAction 클래스 / 10 SingletonAction classes
│
├── tests/                          → Google Test (core + host, 295 tests)
├── tools/                          → midi-test.py, pre-release-test.sh, pre-release-dashboard.html
├── docs/                           → USER_GUIDE, CONTROL_API, STREAMDECK_GUIDE 등 / etc.
└── dist/                           → 빌드 산출물 / Build artifacts + .streamDeckPlugin
```

---

## 7. 경쟁 포지셔닝 / Competitive Positioning

### DirectPipe는 이것이다 / DirectPipe IS
- 마이크 전용 경량 VST 호스트 / A lightweight VST host dedicated to microphones
- 방송자를 위한 마이크 리모컨 / A mic remote control for broadcasters
- LightHost의 정통 후계자 / The legitimate successor to LightHost

### DirectPipe는 이것이 아니다 / DirectPipe IS NOT
- 멀티채널 소프트웨어 믹서 / A multi-channel software mixer (Wave Link)
- 가상 오디오 라우터 / A virtual audio router (VoiceMeeter)
- DAW (Reaper, Ableton)
- 하드웨어 번들 소프트웨어 / Hardware-bundled software (GoXLR)

### 핵심 차별점 / Key Differentiators
1. **5종 외부 제어 통합 / 5 External Control Types Unified** — 핫키/MIDI/Stream Deck/HTTP/WebSocket을 한 프로그램에서 / Hotkeys/MIDI/Stream Deck/HTTP/WebSocket all in one program
2. **프리셋 즉시 전환 / Instant Preset Switching** — A-E 슬롯, Keep-Old-Until-Ready로 10-50ms 교체 / A-E slots, 10-50ms swap with Keep-Old-Until-Ready
3. **DirectPipe Receiver** — 가상 케이블 없이 OBS 직접 연결 / Direct OBS connection without virtual cables (SharedMemory IPC, VST2/VST3/AU)
4. **포터블 / Portable** — 설치 없이, 시스템 변경 없이 단일 exe 실행 / Single exe, no installation, no system changes
5. **오픈소스 / Open Source** — GPL v3, 누구나 기여 가능 / GPL v3, anyone can contribute
