# DirectPipe 제품 기획서 (Product Specification)

> 이 문서는 **내부 개발 참조용** 역기획서입니다. 사용자 문서는 [USER_GUIDE.md](USER_GUIDE.md) 참조.

> 역기획서 — 현재 구현된 기능을 기반으로 작성 (v4.0.1 기준)

---

## 1. 제품 개요

### 제품명
DirectPipe

### 한 줄 설명
DAW 없이, 설치 없이, 마이크에 VST 이펙트를 거는 가장 가벼운 방법 (Windows / macOS / Linux)

### 버전
4.0.1

### 개발 배경
- DAW(Reaper, Ableton 등)를 마이크 이펙트 용도로 구동하는 것은 자원 낭비
- LightHost를 대안으로 사용했으나 2016년 이후 업데이트 중단
- 스트리밍 및 방송 마이크 세팅 작업 중 겪은 불편함을 직접 해결
  - 방송 중 마우스 조작 불가 → 핫키/스트림덱 필요
  - 방송 사고 → 패닉 뮤트 필요
  - 상황별 세팅 전환 → 프리셋 슬롯 필요
  - OBS에 깨끗한 오디오 전달 → DirectPipe Receiver 필요
  - 자기 목소리 확인 → 독립 모니터 출력 필요

### 라이선스
GPL v3 (오픈소스)

### 플랫폼 / Platform
- **Windows** 10/11 (64-bit) — fully supported / 정식 지원
- **macOS** 10.15+ (Apple Silicon & Intel universal binary) — beta / 베타 (빌드 최소 10.15, 권장 13+ / build min 10.15, recommended 13+)
- **Linux** (Ubuntu 22.04+, x86_64) — experimental / 실험적

### 배포 형태
- `DirectPipe.exe` — 메인 호스트 (단일 실행 파일)
- `DirectPipe Receiver.dll` — Receiver 플러그인 (VST2/VST3/AU)
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 플러그인 패키지
- 릴리즈: Windows `DirectPipe-vX.Y.Z-Windows.zip` (exe + dll + vst3), macOS `DirectPipe-vX.Y.Z-macOS.dmg`, Linux `DirectPipe-vX.Y.Z-Linux.tar.gz`

---

## 2. 타겟 사용자

| 사용자 유형 | 설명 | 핵심 니즈 |
|------------|------|----------|
| **스트리머** | Twitch, YouTube 등 실시간 방송자 | 방송 중 원클릭 마이크 제어, 이펙트 전환 |
| **팟캐스터** | 녹음/실시간 팟캐스트 진행자 | 노이즈 제거, EQ, 컴프레서 상시 적용 |
| **게이머** | Discord, 팀스피크 음성채팅 사용자 | 백그라운드에서 저자원으로 마이크 처리 |
| **세팅 도우미/전문가** | 타인의 마이크 세팅을 해주는 사람 | 포터블 실행, 프리셋 전달, 빠른 설정 |
| **보이스챗 사용자** | Zoom, Teams 등 화상회의 참가자 | 깨끗한 음성, 간편한 설정 |

---

## 3. 핵심 가치 (Core Values)

### 3.1 가벼움
- 단일 exe 파일, 설치 불필요
- 시스템에 드라이버/가상 장치를 설치하지 않음
- 포터블 모드 지원 (exe 옆에 `portable.flag` 파일 → 설정을 `./config/`에 저장)
- `HIGH_PRIORITY_CLASS`로 실행, 최소 자원으로 최대 성능

### 3.2 단순한 구조
```
마이크 입력 → VST 플러그인 체인 → 출력
```
- 이 한 줄이 전부. 멀티채널 믹서가 아님
- 열면 바로 이해되는 2열 레이아웃

### 3.3 방송 중 제어
- 5종 외부 제어: 핫키, MIDI, Stream Deck, HTTP API, WebSocket
- 방송 중 마우스에 손대지 않고 모든 것을 조작 가능

### 3.4 하드웨어 독립
- 어떤 USB 마이크, 오디오 인터페이스든 사용 가능
- 특정 하드웨어에 종속되지 않음

---

## 4. 기능 명세

### 4.1 오디오 엔진

#### 4.1.1 입력
| 항목 | 상세 |
|------|------|
| 드라이버 타입 | **Windows**: 5종 — DirectSound (레거시), WASAPI Shared (권장), WASAPI Low Latency (IAudioClient3), WASAPI Exclusive, ASIO. **macOS**: CoreAudio. **Linux**: ALSA, JACK |
| 드라이버 전환 | 런타임 전환 가능. ASIO (Windows): 단일 장치, 동적 SR/BS, 채널 라우팅. WASAPI: 입출력 분리 가능 |
| 채널 모드 | Mono / Stereo (기본값: Stereo, `channelMode_` = 2) |
| 입력 게인 | 0.0x ~ 2.0x (기본 1.0x). `|gain - 1.0f| > 0.001f`일 때만 SIMD 적용 |
| 샘플레이트 | 장치 지원 범위 (일반적 44.1kHz ~ 192kHz) |
| 버퍼 크기 | 장치 지원 범위. 자동 폴백: 장치가 요청 크기를 거부하면 가장 가까운 지원 크기 선택 후 NotificationBar 알림 |

#### 4.1.2 오디오 콜백 처리 흐름
```
1. ScopedNoDenormals 적용 (비정규 부동소수점 방지)
2. 입력 → 사전 할당된 8채널 workBuffer에 복사 (힙 할당 없음)
3. Mono 모드: 모든 입력을 채널 0에 합산 → 채널 1에 복제
   Stereo 모드: 그대로 복사
4. 입력 게인 적용 (변경 시에만)
5. RMS 레벨 계산 (4회 콜백마다 1회 = ~23Hz @48kHz/512)
6. VST 체인 processBlock() 통과 (SEH crash guard: Windows `__try/__except`, 기타 `try/catch`)
   - 예외 발생 시: 버퍼 클리어, `chainCrashed_` 플래그 설정, 이후 모든 콜백 무음 출력
   - 메시지 스레드에서 지연 알림 (NotificationBar)
7. Safety Limiter 적용 (feed-forward, 0.1ms attack / 50ms release)
8. AudioRecorder에 lock-free 쓰기 (녹음 중일 때)
9. SharedMemWriter에 IPC 쓰기 (IPC 활성화 시)
10. OutputRouter → 모니터 출력 (별도 WASAPI 장치)
11. 메인 출력: outputChannelData에 직접 memcpy
12. 출력 RMS 레벨 계산 (데시메이션)
```

#### 4.1.3 출력 경로 (3가지 + 녹음)

3가지 출력 경로는 모두 **독립적으로 켜기/끄기 및 볼륨 조절**이 가능하다. OUT/MON/VST 버튼 또는 외부 제어(핫키, MIDI, Stream Deck, HTTP API)로 각 경로를 개별 제어하여, 예를 들어 OBS 마이크만 끄고 Discord는 유지하거나 그 반대도 가능하다. Panic Mute(Ctrl+Shift+M)로 전체를 즉시 차단할 수 있으며, 해제 시 이전 ON/OFF 상태가 자동 복원된다.

All 3 output paths can be **independently toggled and volume-adjusted**. Use OUT/MON/VST buttons or external controls (hotkeys, MIDI, Stream Deck, HTTP API) to independently control each path — e.g., mute OBS mic while keeping Discord active, or vice versa. Panic Mute (Ctrl+Shift+M) kills all outputs instantly and stops active recording; previous ON/OFF states auto-restore on unmute (recording does not auto-restart). During panic, all actions (bypass, volume, preset, gain, recording, plugin parameters) are blocked.

| 경로 | 설명 | 기술 | 제어 |
|------|------|------|------|
| **Main Output** | 메인 출력 (스피커/가상 케이블) | AudioSettings의 Output 장치에 직접 쓰기. WASAPI/ASIO 모두 지원 | OUT 버튼, ToggleMute, SetVolume |
| **Monitor Output** | 헤드폰 모니터링 (자기 목소리 확인) | 별도 WASAPI AudioDeviceManager + lock-free AudioRingBuffer (4096 프레임, 스테레오, power-of-2) | MON 버튼, MonitorToggle, SetVolume |
| **IPC Output** | OBS용 DirectPipe Receiver | SharedMemory 기반 IPC. 공유 메모리 이름: `Local\\DirectPipeAudio`. 인터리브 float 형식. POSIX sem/shm 퍼미션 0600 (owner-only) | VST 버튼, IpcToggle |
| **Recording** | WAV 녹음 (VST 체인 이후) | AudioRecorder, lock-free ThreadedWriter | REC 버튼, RecordingToggle |

#### 4.1.4 오디오 최적화
| 최적화 | 상세 |
|--------|------|
| 타이머 해상도 | Windows: `timeBeginPeriod(1)` — 1ms (종료 시 `timeEndPeriod(1)` 복원). macOS/Linux: platform timer APIs |
| 비정규 방지 | `ScopedNoDenormals` — 매 콜백마다 적용 |
| Power Throttling | Windows: Intel 하이브리드 CPU E-코어 할당 방지 (시작 시 비활성화). macOS/Linux: N/A |
| RMS 데시메이션 | `rmsDecimationCounter_` — 4콜백마다 1회 계산 (CPU 절약) |
| 뮤트 fast-path | 뮤트 상태에서 VST 처리 스킵 |
| 워크버퍼 | 8채널 사전 할당 (`workBuffer_`). 콜백에서 힙 할당 없음 |
| 프로세스 우선순위 | `HIGH_PRIORITY_CLASS` |
| MMCSS 스레드 등록 | Windows: 오디오 콜백 스레드를 "Pro Audio" MMCSS에 AVRT_PRIORITY_HIGH로 등록 (WASAPI + ASIO 모두). DPC 지연 간섭 방지. MMCSS 함수 포인터는 `audioDeviceAboutToStart`에서 1회 캐시 (release/acquire 순서), RT 콜백에서 읽기만. `audioDeviceStopped`에서 핸들 적절히 revert |
| IPC 이벤트 최적화 | `SetEvent` 시그널을 데이터 기록 시에만 발생 (불필요한 커널 호출 감소) |
| 콜백 오버런 감지 | LatencyMonitor: 처리 시간이 버퍼 주기를 초과하면 카운트 (`getCallbackOverrunCount()`) |

#### 4.1.5 장치 자동 재연결
| 항목 | 상세 |
|------|------|
| 감지 방식 | 이중 메커니즘: (1) `ChangeListener` 즉시 감지 + (2) 3초 타이머 폴링 (`checkReconnection`) |
| 의도 추적 | `desiredInputDevice_` / `desiredOutputDevice_` (SpinLock 보호) / `desiredDeviceType_` / `desiredSampleRate_` / `desiredBufferSize_` 저장 |
| 폴백 보호 | `intentionalChange_` 플래그: JUCE 자동 폴백을 성공적 재연결로 오인하는 것 방지 |
| 재연결 로직 | `attemptReconnection()`: 장치 재스캔 → 가용성 확인 → desired 설정(SR/BS/채널) 복원. `attemptingReconnection_` 재진입 가드 |
| 쿨다운 | `reconnectCooldown_` (30Hz 타이머 틱) |
| 알림 | 연결 끊김 시 경고(주황), 재연결 시 정보(보라) NotificationBar 표시 |
| 방향별 감지 | `inputDeviceLost_`: 입력 장치 분실 시 오디오 콜백에서 입력 무음 처리 (폴백 마이크 방지). `outputAutoMuted_`: 출력 장치 분실 시 자동 뮤트, 복원 시 자동 해제 |
| 재연결 실패 | `reconnectMissCount_`: 5회 연속 실패(~15초) 후 현재 드라이버 장치를 수락하여 크로스 드라이버 stale name 무한 루프 방지 |
| 드라이버 전환 복원 | `DriverTypeSnapshot`: 드라이버 타입별 설정(입출력 장치, SR, BS) 저장/복원. WASAPI↔ASIO 전환 시 이전 설정 자동 복원 |
| 모니터 독립 | 모니터 장치는 별도 패턴으로 독립 재연결 (`monitorLost_` + 자체 쿨다운) |

#### 4.1.6 XRun 모니터링
| 항목 | 상세 |
|------|------|
| 윈도우 | 60초 롤링. 순환 버퍼 `xrunHistory_[60]` (1초당 1슬롯) |
| 스레드 안전 | `xrunResetRequested_` atomic 플래그로 장치 스레드 → 메시지 스레드 신호 |
| 표시 | 상태 바에 카운트 표시. > 0이면 빨간색 하이라이트 |
| 비지원 | 장치가 XRun 지원 안 하면 -1 반환 |

#### 4.1.7 레이턴시 모니터
| 측정값 | 설명 |
|--------|------|
| `inputLatencyMs_` | 입력 버퍼 레이턴시 |
| `processingTimeMs_` | VST 체인 처리 시간 (고해상도 타임스탬프 델타) |
| `outputLatencyMs_` | 출력 버퍼 레이턴시 |
| `cpuUsage_` | 콜백 시간 대비 처리 비율 (%) |
| 스무딩 | 지수 이동 평균, `kSmoothingFactor = 0.1` |

#### 4.1.8 Safety Limiter
| 항목 | 상세 |
|------|------|
| 타입 | 피드포워드 리미터 (look-ahead 없음, PDC 기여 없음) |
| 위치 | VST 체인 이후, Recording/IPC/Monitor/Output 이전 |
| Attack | 0.1ms |
| Release | 50ms |
| Ceiling 범위 | -6.0 ~ 0.0 dBFS (기본값: -0.3 dBFS) |
| 파라미터 | `enabled` (atomic, 기본 true), `ceilingdB` (atomic) |
| UI | PluginChainEditor: ceiling 슬라이더 (Add Plugin 위) + Safety Limiter 토글 버튼. 상태 바 [LIM] 표시 |
| GR 피드백 | atomic으로 UI에 전달 |

#### 4.1.9 Plugin Latency Data (PDC)
| 항목 | 상세 |
|------|------|
| 소스 | `VSTChain::getPluginLatencies()` + `getTotalChainPDC()` — chainLock_ 하에서 각 플러그인의 PDC 조회 |
| UI | Per-plugin latency display와 chain PDC summary는 UX 피드백으로 UI에서 제거됨 |
| 보상 | AudioProcessorGraph가 PDC 보상을 자동 처리 |
| 상태 전파 | StateBroadcaster: `plugins[].latency_samples`, `chain_pdc_samples`, `chain_pdc_ms` (API에서 여전히 사용 가능) |

#### 4.1.10 Built-in Processors
VST 플러그인과 동일하게 AudioProcessorGraph에 삽입 가능한 내장 프로세서 3종.

| 프로세서 | 클래스 | 상세 |
|---------|--------|------|
| **Filter** | `BuiltinFilter` | HPF (기본 ON, 60Hz) + LPF (기본 OFF, 16kHz). 범위: HPF 20-300Hz, LPF 4k-20kHz. IIR 필터, atomic 파라미터. `isBusesLayoutSupported`: mono + stereo. `getLatencySamples()` = 0 |
| **Noise Removal** | `BuiltinNoiseRemoval` | RNNoise AI 기반 노이즈 제거. 480-frame FIFO (~10ms 레이턴시). 48kHz only (비-48kHz = 패스스루, TODO: 리샘플링). 듀얼 모노 (2 RNNoise 인스턴스). x32767 스케일링 전처리, /32767 후처리. 2-pass FIFO (in-place 버퍼 안전). 링 버퍼 출력 FIFO (modulo wrapping). 게이트 초기 CLOSED (0.0), 5프레임 워밍업. VAD 게이트 홀드 타임 300ms (`kHoldSamples`=14400 @48kHz). 게이트 스무딩 20ms (`kGateSmooth`=0.9990). `getLatencySamples()` = 480 via `setLatencySamples()`. VAD 임계값: Light 0.50, Standard 0.70 (기본값), Aggressive 0.90 |
| **Auto Gain** | `BuiltinAutoGain` | LUFS 기반 AGC (WebRTC-inspired dual-envelope). Target LUFS -15.0 기본 (범위 -24~-6, 내부적으로 -4dB 오프셋 적용하여 오픈루프 오버슈트 보정). Low Correct 0.50 기본 (hold↔full correction 블렌드, 부스트). High Correct 0.90 기본 (hold↔full correction 블렌드, 컷). Max Gain 22 dB 기본. ITU-R BS.1770 K-weighting 사이드체인 (copy, 실제 오디오 미적용). Dual-envelope level detection: fast envelope (~10ms attack, ~200ms release) + slow LUFS window (0.4s EBU Momentary), effective = max(fast, slow). Direct gain computation (IIR gain envelope 없음), per-block linear ramp으로 click-free 전환. Freeze Level -45 dBFS (per-block RMS, NOT LUFS): freeze 시 현재 게인 유지 (0dB 리셋 아님), -65 dBFS 미만 시 바이패스. Incremental `runningSquareSum_` (O(blockSize)). lowCorr/hiCorr = hold↔full correction 블렌드 비율 (엔벨로프 속도 아님) |

**[Auto] 버튼**: 입력 게인 슬라이더 옆 특수 프리셋 슬롯 (A-E 바와 별도 위치, 인덱스 5 `PresetSlotBar::kAutoSlotIndex`). 활성 시 초록색 (green when active). 첫 클릭 시 Filter + Noise Removal + Auto Gain 기본 체인 생성, 이후 마지막 저장 상태 로드. 우클릭 → Reset to Defaults.

**Auto 프리셋 슬롯**: 6번째 프리셋 슬롯 (인덱스 5). 이름 변경 불가, Next/Previous 사이클에서 제외. Reset 시 Filter + NoiseRemoval + AutoGain 기본값 복원. Factory Reset 시 Auto 슬롯도 포함 삭제 (`slot_Auto.dppreset` 와일드카드 삭제, 메시지 "(A-E + Auto)").

---

### 4.2 VST 호스팅

#### 4.2.1 플러그인 지원
| 항목 | 상세 |
|------|------|
| 포맷 | VST2 (SDK 2.4) + VST3 |
| 스캐너 | Out-of-process 자식 프로세스 (`--scan` 인자). Dead Man's Pedal 패턴. 10회 자동 재시도 |
| 블랙리스트 | 크래시한 플러그인 자동 블랙리스트 등록. 재스캔 시 건너뜀 |
| 스캔 UI | 실시간 텍스트 필터 + 칼럼 정렬 (이름/벤더/포맷). 프로그레스 바 |
| 기본 스캔 경로 | Windows: VST2 `C:\Program Files\Common Files\VST2`, VST3 `C:\Program Files\Common Files\VST3`. macOS: `/Library/Audio/Plug-Ins/VST`, `/Library/Audio/Plug-Ins/VST3`. Linux: `~/.vst`, `~/.vst3` |
| 스캔 캐시 | XML 파일로 캐시. 재스캔 불필요 |
| 스캔 로그 | Windows: `%AppData%/DirectPipe/scanner-log.txt`, macOS: `~/Library/Application Support/DirectPipe/`, Linux: `~/.config/DirectPipe/` |
| 스캔 타임아웃 | 플러그인당 300초 (5분) |

#### 4.2.2 플러그인 체인 에디터
| 기능 | 상세 |
|------|------|
| 순서 변경 | 드래그앤드롭 (자동 그래프 재빌드) |
| 바이패스 | 개별 플러그인 토글 |
| GUI 편집 | 네이티브 플러그인 GUI 윈도우 열기/닫기 |
| 삭제 | `callAsync`로 안전한 자기삭제 (UI 스레드 보호) |
| 추가 | 메뉴: 스캐너에서 선택 / 파일에서 직접 추가 |
| 우클릭 메뉴 | Edit, Bypass, Remove |

#### 4.2.3 플러그인 체인 내부 구조
```
AudioProcessorGraph:
  InputNode → Plugin[0] → Plugin[1] → ... → Plugin[N-1] → OutputNode
```
- `PluginSlot` 구조: name, path, PluginDescription, bypassed, nodeId, instance 포인터
- `chainLock_` (CriticalSection): 모든 체인 접근(읽기+쓰기) 보호
- `processBlock`에서는 lock 없이 capacity guard만 사용
- `chainLock_` 내부에서 절대 로그 쓰기 금지 (DirectPipeLogger `writeMutex_`와 데드락 방지)
- `onChainChanged` 콜백은 lock 범위 밖에서 호출

#### 4.2.4 프리셋 슬롯 (A-E + Auto)
| 항목 | 상세 |
|------|------|
| 슬롯 수 | 5개 사용자 슬롯 (A=0, B=1, C=2, D=3, E=4) + 1개 Auto 슬롯 (인덱스 5, 이름 변경 불가, Next/Previous 사이클 제외, Reset 시 Filter+NoiseRemoval+AutoGain 기본값 복원) |
| 저장 내용 | VST 체인만 — 플러그인 이름, 경로, PluginDescription, 바이패스 상태, 내부 상태(base64 인코딩) |
| 저장 위치 | 앱 데이터 디렉토리 `Slots/slot_A.dppreset` ~ `slot_E.dppreset` (Windows: `%AppData%/DirectPipe/`, macOS/Linux: see platform paths above) |
| 전환 속도 | 캐시 히트: 10-50ms, DLL 로딩: 200-500ms |
| 전환 방식 | **Keep-Old-Until-Ready**: 이전 체인이 오디오 처리 계속 → 새 체인 준비 완료 시 메시지 스레드에서 원자적 교체 |
| 프리로드 | `PluginPreloadCache`: 슬롯 로드 후 백그라운드에서 다른 슬롯 플러그인 미리 로드. SR/BS 변경, 구조 변경(이름/경로/순서) 시 무효화. Per-slot 버전 카운터로 프리로드 중 무효화 경합 방지 |
| 자동 저장 | 플러그인 에디터 닫을 때 (`onEditorClosed`) 활성 슬롯에 자동 저장 |
| 슬롯 이름 | 커스텀 이름 지원. 표시: `A\|게임` (파이프 구분자, 최대 8자 + ".." 자동 잘림). `.dppreset` JSON `"name"` 필드에 저장. StateBroadcaster `slot_names` 배열로 외부 전달 |
| 관리 | 우클릭 메뉴: Rename, Copy A→B/C/D/E, Delete, Export (.dppreset), Import (.dppreset). 활성 슬롯 Copy 시 라이브 상태 캡처 |
| 보호 | 빈 체인 저장 방지. `partialLoad_` 부분 로드 감지. `loadingSlot_` 동시 전환 방지 |
| 점유 캐시 | `slotOccupiedCache_[5]` — 파일시스템 조회 없이 빠른 점유 확인 (NextPreset 사이클링용) |

#### 4.2.5 비동기 체인 로딩 (replaceChainAsync)
| 항목 | 상세 |
|------|------|
| 생성 카운터 | `asyncGeneration_` (uint32_t atomic) — 호출마다 증가. 이전 요청의 콜백 폐기 |
| 백그라운드 스레드 | COM 초기화 (`CoInitializeEx`) 후 플러그인 로드 |
| 메시지 스레드 교체 | `callAsync`로 그래프 교체. 생성 카운터 확인 후 진행 |
| 수명 보호 | `alive_` 플래그 (`shared_ptr<atomic<bool>>`) — callAsync 람다에서 this 접근 전 확인 |
| 프리로드 경로 | `replaceChainWithPreloaded`: 미리 로드된 인스턴스로 동기 교체 (DLL 로딩 없음) |

#### 4.2.6 그래프 재빌드 (rebuildGraph)
```
rebuildGraph(bool suspend = true)
1. suspend=true일 때만 suspendProcessing(true) (노드 추가/제거 시)
2. 연결 목록 복사 후 모든 연결 제거 (async → 개별 리빌드 지연)
3. 입력 → plugin[0] → ... → plugin[N-1] → 출력 연결 (bypassed 플러그인 스킵)
4. 마지막 연결만 sync (단일 리빌드 트리거)
5. suspend=true일 때만 suspendProcessing(false)
```
- `suspend=false`: bypass 토글 시 사용 — 연결만 변경되므로 suspend 불필요, 오디오 갭 없음
- bypassed 플러그인은 그래프 연결에서 제외 (오디오가 우회). `node->setBypassed()` + `getBypassParameter()` 동기화와 함께 이중 보호

---

### 4.3 모니터 출력 (MonitorOutput)

#### 구성
| 항목 | 상세 |
|------|------|
| 장치 | 별도 AudioDeviceManager (Windows: WASAPI, macOS: CoreAudio, Linux: ALSA) (메인 드라이버와 독립) |
| 링 버퍼 | AudioRingBuffer: 4096 프레임, 스테레오, power-of-2 |
| 상태 | `VirtualCableStatus` enum: NotConfigured, Active, Error, SampleRateMismatch |

#### 이중 스레드 브릿지
| 역할 | 스레드 | 동작 |
|------|--------|------|
| 프로듀서 | 메인 오디오 콜백 (RT) | `writeAudio()` → 링 버퍼에 RT-safe 쓰기 |
| 컨슈머 | 모니터 장치 콜백 | 링 버퍼에서 읽기 → WASAPI 출력 |

#### 폴백 보호
- `audioDeviceAboutToStart`에서 실제 장치명 vs desired 비교
- JUCE 자동 폴백 감지 시: Error 상태 설정, callAsync로 장치 닫기
- SR 불일치: `SampleRateMismatch` 상태, 링 버퍼 리셋, one-shot NotificationBar 경고 (클리어 시 리셋)

#### 재연결
- `monitorLost_` atomic 플래그 (audioDeviceError/audioDeviceStopped에서 설정)
- `shutdown()`은 콜백 제거 후 실행 → 외부 이벤트에서만 monitorLost_ 설정
- 3초 타이머 폴링으로 재연결 시도
- `actualSampleRate_` / `actualBufferSize_` 표시용 (shutdown 시 0으로 리셋)

#### 레이턴시 계산
```
레이턴시(ms) = (버퍼크기 / 샘플레이트) × 1000
```

---

### 4.4 AudioRingBuffer (Lock-Free 스레드 브릿지)

| 항목 | 상세 |
|------|------|
| 구조 | 채널별 float 벡터 + atomic uint64 read/write 포지션 |
| 정렬 | `alignas(64)` — 캐시 라인 정렬로 false sharing 방지 |
| 용량 | 반드시 power-of-2 (assertion 강제). mask = capacity - 1 |
| 쓰기 | relaxed load writePos, acquire load readPos → 채널별 복사 + wrap-around → release store writePos |
| 읽기 | relaxed load readPos, acquire load writePos → 채널별 복사 + wrap-around → release store readPos |
| Mono→Stereo | 채널 0을 채널 1에 자동 복제 |
| reset() | read/write 포지션 0, 모든 채널 데이터 0.0f 초기화 |

---

### 4.5 외부 제어 시스템

#### 4.5.1 통합 아키텍처
```
핫키 / MIDI / WebSocket / HTTP → ControlManager → ActionDispatcher
  → VSTChain (바이패스), OutputRouter (볼륨), PresetManager (슬롯 전환)
  → StateBroadcaster → WebSocket/HTTP 클라이언트에 상태 전파
```
- **ActionDispatcher**: 메시지 스레드 전달 보장. 오프스레드 호출 시 `callAsync`. `alive_` 플래그 수명 보호. 리스너 copy-before-iterate (재진입 안전)
- **StateBroadcaster**: `quickStateHash()` 더티 체크로 무변경 시 브로드캐스트 생략. float 0.05 정밀도 양자화

#### 4.5.2 액션 목록 (19종)

| # | Action | 설명 | 파라미터 |
|---|--------|------|---------|
| 1 | `PluginBypass` | 특정 플러그인 바이패스 토글 | intParam = 플러그인 인덱스 |
| 2 | `MasterBypass` | 전체 VST 체인 바이패스 토글 | — |
| 3 | `SetVolume` | 볼륨 설정 | stringParam = "monitor"/"input"/"output", floatParam = 값 |
| 4 | `ToggleMute` | 뮤트 토글 | stringParam = 타겟명 |
| 5 | `LoadPreset` | 프리셋 로드 | intParam = 인덱스 |
| 6 | `PanicMute` | 패닉 뮤트 (모든 출력 즉시 차단) | — |
| 7 | `InputGainAdjust` | 입력 게인 조정 | floatParam = 델타 (±1.0) |
| 8 | `NextPreset` | 다음 슬롯으로 순환 | — |
| 9 | `PreviousPreset` | 이전 슬롯으로 순환 | — |
| 10 | `InputMuteToggle` | 마이크 입력 뮤트 토글 | — |
| 11 | `SwitchPresetSlot` | 특정 슬롯 전환 | intParam = 0~4 (A~E) |
| 12 | `MonitorToggle` | 모니터 출력 토글 | — |
| 13 | `RecordingToggle` | 녹음 토글 | — |
| 14 | `SetPluginParameter` | 플러그인 파라미터 설정 | intParam=플러그인, intParam2=파라미터, floatParam=0.0~1.0 |
| 15 | `IpcToggle` | IPC 출력(DirectPipe Receiver) 토글 | — |
| 16 | `XRunReset` | XRun 카운터 리셋 | — |
| 17 | `SafetyLimiterToggle` | Safety Limiter on/off 토글 | — (패닉 뮤트 가드 없음) |
| 18 | `SetSafetyLimiterCeiling` | Safety Limiter ceiling 설정 | floatParam = dB 값 (-6.0~0.0) (패닉 뮤트 가드 없음) |
| 19 | `AutoProcessorsAdd` | Auto 프로세서 추가 (Filter+NR+AGC) | — |

#### 4.5.3 키보드 핫키

**기본 매핑 (11개):**
| 핫키 | 액션 | 파라미터 |
|------|------|---------|
| Ctrl+Shift+M | PanicMute | — |
| Ctrl+Shift+0 | MasterBypass | — |
| Ctrl+Shift+1~3 | PluginBypass | 인덱스 0~2 |
| Ctrl+Shift+F6 | InputMuteToggle | — |
| Ctrl+Shift+H | MonitorToggle | — |
| Ctrl+Shift+F1~F5 | SwitchPresetSlot | 0~4 |

**구현:**
- Windows: `RegisterHotKey()` via 숨겨진 메시지 전용 윈도우 (`HWND_MESSAGE`). macOS: `CGEventTap` (접근성 권한 필요 — 미허용 시 `onError` 콜백으로 사용자 알림). Linux: 스텁 (미지원 — HotkeyTab에 "unsupported" 메시지 표시, MIDI/WebSocket/HTTP 대안 안내)
- 앱 최소화/트레이 상태에서도 동작
- 드래그앤드롭으로 순서 재정렬 (`moveBinding`)
- 인플레이스 키 재할당 (`updateHotkey`)
- 키 녹음: ~60Hz 폴링 (16ms 타이머). 수정자 키 1개 이상 필수. [Cancel] 취소 가능

#### 4.5.4 MIDI

**바인딩 타입:**
| 타입 | 동작 |
|------|------|
| `Toggle` | CC ≥ 64 → ON, < 64 → OFF (모멘터리 버튼 시뮬레이션) |
| `Momentary` | CC ≥ 64 → 누르는 동안 ON |
| `Continuous` | CC 0~127 → 파라미터 0.0~1.0 매핑 (페이더, 노브) |
| `NoteOnOff` | Note ON → 토글 |

**바인딩 구조:**
- CC 번호 (-1이면 Note 모드)
- Note 번호 (-1이면 CC 모드)
- 채널 (0 = 모든 채널, 1~16 = 특정 채널)
- 장치명 ("" = 모든 장치)
- 마지막 상태 (토글 메모리)

**MIDI Learn:**
- [Learn] 클릭 → 다음 CC 또는 Note 메시지 캡처
- 플러그인 파라미터 매핑: 3단계 팝업 (플러그인 선택 → 파라미터 선택 → MIDI Learn)
- [Cancel] 취소 가능
- 기본 매핑: 없음 (사용자가 직접 학습)

**바인딩 관리:**
- `addBinding()` returns `bool` — 동일 CC/채널/장치의 기존 바인딩이 있으면 덮어쓰기(overwrite), 중복 추가 안 함
- `alive_` 플래그 (`shared_ptr<atomic<bool>>`) — 소멸 후 callAsync 콜백 안전 보호
- LearnTimeout (30초) — 타이머 콜백에서 자기 자신을 파괴하지 않도록 callAsync로 지연 정리

**스레드 안전:**
- `bindingsMutex_` (std::mutex)로 바인딩 접근 보호
- `getBindings()`는 복사본 반환
- `processCC`/`processNote`: lock 내에서 액션 수집 → lock 해제 후 dispatch (데드락 방지)

**테스트 API:**
- `injectTestMessage(MidiMessage)` — HTTP API를 통한 MIDI 하드웨어 없이 테스트

#### 4.5.5 Stream Deck 플러그인

**기본 정보:**
| 항목 | 상세 |
|------|------|
| SDK | Elgato SDKVersion 3 (@elgato/streamdeck v2.0.1) |
| Node.js | v20+ |
| UUID | `com.directpipe.directpipe` |
| 최소 SD 버전 | 6.9 |
| 빌드 | Rollup → `bin/plugin.js` |
| 패키징 | `streamdeck pack` CLI |

**액션 (10개, 모두 SingletonAction):**

| # | 액션 | UUID | 컨트롤러 | 상태수 | Property Inspector |
|---|------|------|---------|--------|-------------------|
| 1 | Bypass Toggle | `...bypass-toggle` | Keypad | 2 (Bypassed/Active) | bypass-pi.html |
| 2 | Volume Control | `...volume-control` | Keypad + Encoder | 2 (Unmuted/Muted) | volume-pi.html |
| 3 | Preset Switch | `...preset-switch` | Keypad | 1 | preset-pi.html |
| 4 | Monitor Toggle | `...monitor-toggle` | Keypad | 2 (ON/OFF) | 없음 |
| 5 | Panic Mute | `...panic-mute` | Keypad | 2 (MUTE/MUTED) | 없음 |
| 6 | Recording Toggle | `...recording-toggle` | Keypad | 2 (REC/REC) | 없음 |
| 7 | IPC Toggle | `...ipc-toggle` | Keypad | 2 (ON/OFF) | 없음 |
| 8 | Performance Monitor | `...performance-monitor` | Keypad + Encoder | 1 | performance-pi.html |
| 9 | Plugin Parameter | `...plugin-param` | Encoder (SD+) | 1 | plugin-param-pi.html |
| 10 | Preset Bar | `...preset-bar` | Encoder (SD+) | 1 | 없음 |

**Bypass Toggle 상세:**
- 숏프레스: 개별 플러그인 바이패스 (`plugin_bypass`)
- 롱프레스 (500ms): 마스터 바이패스 (`master_bypass`)
- 설정: Plugin # (1-indexed)
- 타이틀: "{PluginName}\nBypassed" 또는 "Active", 빈 슬롯은 "Slot {N}\nEmpty"

**Volume Control 상세:**
- 타겟: input (0~2.0), output (0~1.0), monitor (0~1.0)
- 모드: mute, volume_up, volume_down
- 스텝: 1~25% (기본 5%)
- SD+ 다이얼: 회전=볼륨 조정 (틱당 ±0.05), 누르기=뮤트, 롱터치=100% 리셋
- LCD 표시: input "x{gain}", output/monitor "{percent}%"
- 쓰로틀: 다이얼 전송 50ms, 로컬 오버라이드 억제 300ms (optimistic local update)

**Preset Switch 상세:**
- 설정: cycle / A / B / C / D / E
- cycle 모드: `next_preset` 명령
- 슬롯 모드: `switch_preset_slot { slot: 0-4 }`

**Recording Toggle 상세:**
- 녹음 중: 타이틀에 "REC\n{MM}:{SS}" 표시

**연결:**
| 항목 | 상세 |
|------|------|
| WebSocket | `ws://localhost:8765` |
| UDP 디스커버리 | 포트 8767, `127.0.0.1`. 메시지: `"DIRECTPIPE_READY:8765"` |
| 재연결 | 이벤트 드리븐 (UDP 감지 + 사용자 액션). 폴링 없음 |
| 백오프 | 초기 2초, 최대 10초, 배율 1.5x |
| 대기열 | 연결 끊김 중 최대 50개 메시지 큐잉 |
| 킵얼라이브 | 15초 간격 ping/pong |

#### 4.5.6 HTTP REST API

| 항목 | 상세 |
|------|------|
| 포트 | 8766 (폴백: 8767~8770) |
| 방식 | GET-only |
| CORS | `Access-Control-Allow-Origin: *` |
| 타임아웃 | 3초 읽기 타임아웃 |
| 응답 | JSON. 상태 코드: 200/400/404/405 |

**엔드포인트 (23개):**
| 엔드포인트 | 액션 | 파라미터 검증 |
|-----------|------|-------------|
| `GET /api/status` | 전체 상태 JSON | — |
| `GET /api/bypass/master` | 마스터 바이패스 토글 | — |
| `GET /api/bypass/{index}/toggle` | 플러그인 바이패스 토글 | index 범위 검증 |
| `GET /api/mute/panic` | 패닉 뮤트 | — |
| `GET /api/mute/toggle` | 마스터 뮤트 토글 | — |
| `GET /api/volume/{target}/{value}` | 볼륨 설정 | target: monitor(0~1)/input(0~2)/output(0~1). 범위 초과 시 400 |
| `GET /api/volume/output/{value}` | 출력 볼륨 설정 | 0~1 범위 |
| `GET /api/preset/{index}` | 프리셋 로드 | 0~5 범위 (0-4=A-E, 5=Auto) |
| `GET /api/slot/{index}` | 슬롯 전환 | 0~5 범위 (0-4=A-E, 5=Auto) |
| `GET /api/gain/{delta}` | 입력 게인 조정 | float 델타 |
| `GET /api/input-mute/toggle` | 입력 뮤트 토글 | — |
| `GET /api/monitor/toggle` | 모니터 출력 토글 | — |
| `GET /api/recording/toggle` | 녹음 토글 | — |
| `GET /api/ipc/toggle` | IPC 출력 토글 | — |
| `GET /api/plugins` | 플러그인 목록 조회 | — |
| `GET /api/plugin/{idx}/params` | 플러그인 파라미터 목록 조회 | index 범위 검증 |
| `GET /api/plugin/{pIdx}/param/{paramIdx}/{value}` | 플러그인 파라미터 설정 | 인덱스 + 값(0~1) 범위 검증 |
| `GET /api/perf` | 성능 통계 조회 | — |
| `GET /api/limiter/toggle` | Safety Limiter on/off 토글 | — |
| `GET /api/limiter/ceiling/{value}` | Safety Limiter ceiling 설정 | -6.0~0.0 범위 |
| `GET /api/auto/add` | Auto 프로세서 추가 (Filter+NR+AGC) | — |
| `GET /api/midi/cc/{ch}/{num}/{val}` | MIDI CC 테스트 주입 | ch:1~16, num:0~127, val:0~127 |
| `GET /api/midi/note/{ch}/{num}/{vel}` | MIDI Note 테스트 주입 | ch:1~16, num:0~127, vel:0~127 |

**응답 형식:**
```json
// 성공
{"ok": true, "action": "...", /* 액션별 추가 필드 */}
// 실패
{"error": "설명"}
```

#### 4.5.7 WebSocket 서버

| 항목 | 상세 |
|------|------|
| 포트 | 8765 (폴백: 8766~8769) |
| 프로토콜 | RFC 6455 (커스텀 SHA-1 핸드셰이크) |
| 헤더 | 대소문자 무시 (RFC 7230) |
| 프레임 제한 | 1MB |
| 최대 클라이언트 | 16개 |
| 데드 클라이언트 | `sendFrame` 실패 시 즉시 소켓 종료 |
| 브로드캐스트 | 전용 스레드 (논블로킹). `clientsMutex_` 밖에서 thread join (데드락 방지) |

**수신 명령 (19개):**
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

**상태 브로드캐스트 (서버 → 클라이언트):**
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

### 4.6 사용자 인터페이스

#### 4.6.1 메인 윈도우 레이아웃 (800×800 기본)
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

**프리셋 슬롯 버튼:**
- 5개 TextButton, 4px 간격
- 색상: 활성=하이라이트, 점유=채움, 빈=흐림
- 싱글 클릭으로 슬롯 로드 (디바운스 적용)
- 우클릭: Copy Slot X→Y, Delete Slot

**뮤트 버튼:**
- OUT: 메인 출력 뮤트
- MON: 모니터 뮤트
- VST: IPC 출력 토글
- PANIC MUTE: 패닉 뮤트 (모든 출력 차단 + 컨트롤 잠금)

#### 4.6.2 Audio 탭 (AudioSettings)
| UI 요소 | 설명 |
|---------|------|
| 드라이버 타입 ComboBox | Windows: DirectSound / WASAPI Shared / WASAPI Low Latency / WASAPI Exclusive / ASIO. macOS: CoreAudio. Linux: ALSA / JACK |
| 입력 장치 ComboBox | 클릭 시 `scanForDevices()` + 목록 재빌드 |
| 출력 장치 ComboBox | 클릭 시 `scanForDevices()` + 목록 재빌드 |
| ASIO 입력 채널 ComboBox | ASIO 모드에서만 표시 |
| ASIO 출력 채널 ComboBox | ASIO 모드에서만 표시 |
| 샘플레이트 ComboBox | 장치에서 동적 팝업 |
| 버퍼 크기 ComboBox | 장치에서 동적 팝업 |
| Mono/Stereo 토글 버튼 | 채널 모드 전환 |
| 레이턴시 라벨 | "-- ms" 또는 "10.5 ms" 실시간 표시 |
| 출력 볼륨 슬라이더 | Output Volume (0.0~1.0) — Audio 탭에서 출력 볼륨 조절 |
| ASIO Control Panel 버튼 | ASIO 모드에서만 표시 (Windows only). 드라이버 설정 패널 열기 |

#### 4.6.3 Output 탭 (OutputPanel)

**모니터 섹션:**
| UI 요소 | 설명 |
|---------|------|
| 장치 ComboBox | 클릭 시 목록 새로고침 |
| 볼륨 슬라이더 | 0.0 ~ 1.0 |
| 버퍼 크기 ComboBox | 모니터 장치용 |
| 레이턴시 라벨 | Active 시 실시간 ms 표시 |
| Enable 토글 | 모니터 출력 활성화/비활성화 |
| 상태 라벨 | Active / SampleRateMismatch / Error / No device |

**VST Receiver (IPC) 섹션:**
| UI 요소 | 설명 |
|---------|------|
| Enable 토글 | "Enable VST Receiver Output" |
| 설명 라벨 | "Send processed audio to DirectPipe Receiver plugin." |

**녹음 섹션:**
| UI 요소 | 설명 |
|---------|------|
| REC 버튼 | 녹음 토글 (녹음 중 눌린 상태) |
| 타이머 라벨 | "0m 15s" 형식 |
| Play 버튼 | 마지막 녹음 파일 기본 플레이어로 재생 |
| Open Folder 버튼 | Windows: `explorer.exe /select,{lastFile}`, macOS: `open -R`, Linux: `xdg-open` |
| 폴더 변경 버튼 (...) | 녹음 폴더 선택 |
| 폴더 경로 라벨 | 말줄임표로 축약 표시 |

녹음 설정은 앱 데이터 디렉토리(Windows: `%AppData%/DirectPipe/`, macOS: `~/Library/Application Support/DirectPipe/`, Linux: `~/.config/DirectPipe/`)의 `recording-config.json`에 영속 저장

#### 4.6.4 Controls 탭 (ControlSettingsPanel) — 3개 서브탭

**Hotkeys 서브탭:**
- 스크롤 가능한 바인딩 리스트
- 행별: 액션명 라벨, 단축키 라벨, [Set] 버튼, [Remove] 버튼
- 드래그앤드롭 행 순서 변경
- 교대 행 배경색
- [+ Add Shortcut] 버튼
- 녹음 중: 상태 라벨 ("Press a key...") + [Cancel] 버튼

**MIDI 서브탭:**
- MIDI 장치 ComboBox + [Rescan] 버튼
- 스크롤 가능한 매핑 리스트
- 행별: CC/Note 라벨, 액션 라벨, [Learn] 버튼, [Remove] 버튼
- [+ Add Mapping] 버튼
- [+ Plugin Param] 버튼 (3단계 팝업: 플러그인 → 파라미터 → Learn)
- Learn 중: 상태 라벨 ("Waiting for MIDI...") + [Cancel] 버튼

**Stream Deck 서브탭:**
- WebSocket 서버 상태 (Running/Stopped + 포트)
- HTTP API 서버 상태 (Running/Stopped + 포트)
- 연결된 클라이언트 수

#### 4.6.5 Settings 탭 (LogPanel)

**애플리케이션 섹션:**
- 자동 시작 토글 (Windows: "Start with Windows", macOS: "Start at Login", Linux: "Start on Login")

**설정 섹션:**
- [Save Settings] 버튼 (`.dpbackup`)
- [Load Settings] 버튼

**로그 섹션:**
- Audit Mode 토글
- 로그 뷰어 (TextEditor, 읽기 전용, 모노스페이스, 타임스탬프)
- 최대 1000줄 인메모리 히스토리
- DirectPipeLogger 링 버퍼에서 실시간 drain
- [Export Log] 버튼 (.txt 저장)
- [Clear Log] 버튼

**유지보수 섹션:**
| 버튼 | 동작 | 확인 다이얼로그 |
|------|------|---------------|
| Full Backup | `.dpfullbackup`으로 전체 백업 | 없음 (파일 선택) |
| Full Restore | `.dpfullbackup`에서 전체 복원 | 있음 |
| Clear Plugin Cache | 플러그인 스캔 캐시 삭제 | 있음 |
| Clear All Presets | 슬롯 A-E + Auto + 백업 + 임시 파일 삭제, 활성 체인 초기화 | 있음 |
| Factory Reset | 모든 데이터 삭제 (설정, 컨트롤, 프리셋(A-E + Auto), 캐시, 녹음 설정) | 있음 |

#### 4.6.6 상태 바 (30px)
| 요소 | 설명 |
|------|------|
| 레이턴시 라벨 | 메인 + 모니터 레이턴시. 녹음 중 자동 숨김 |
| CPU% 라벨 | 오디오 콜백 오버로드 시 빨간 하이라이트 |
| 포맷 라벨 | "48kHz 512 Stereo" 형식 |
| 포터블 라벨 | 포터블 모드에서만 표시 (노란 배경) |
| Credit 링크 | "Created by LiveTrack". 업데이트 가용 시 "NEW vX.Y.Z" 주황색 |

#### 4.6.7 NotificationBar (알림 시스템)
| 항목 | 상세 |
|------|------|
| 위치 | 상태 바 영역 위에 일시적 오버레이 (레이턴시/CPU 라벨 대체) |
| 색상 | Info=보라(0xFF6C63FF), Warning=주황(0xFFCC8844), Error=빨강(0xFFE05050), Critical=밝은빨강(0xFFFF0000) |
| 자동 페이드 | 심각도에 따라 3~8초 |
| 트리거 | 오디오 장치 에러, 플러그인 로드 실패, 녹음 실패, 장치 재연결 |
| 큐 오버플로 | 링 버퍼 용량 확인 후 쓰기 (오버플로 가드) |

#### 4.6.8 시스템 트레이
| 동작 | 결과 |
|------|------|
| 창 닫기 (X) | 트레이로 최소화 (종료 아님) |
| 트레이 좌클릭/더블클릭 | 창 복원 + 포커스 |
| 트레이 우클릭 | 메뉴: Show/Hide, 자동 시작 토글, Quit |
| 마우스 호버 | 동적 툴팁: 프리셋명, 플러그인 수, 볼륨, 뮤트 상태 |
| 아이콘 | 16px/32px 듀얼 사이즈 PNG |

#### 4.6.9 테마 (DirectPipeLookAndFeel)
| 색상 | 값 | 용도 |
|------|-----|------|
| 배경 | `#1E1E2E` | 메인 배경 (다크 블루그레이) |
| 표면 | `#2A2A40` | 컴포넌트 배경 |
| 액센트 | `#6C63FF` | 보라 (버튼, 하이라이트) |
| 액센트2 | `#4CAF50` | 초록 (활성 상태) |
| 텍스트 | `#E0E0E0` | 기본 텍스트 (밝은 회색) |
| 흐린 텍스트 | `#8888AA` | 보조 텍스트 (흐린 보라) |
| 빨강 | `#E05050` | 에러, 뮤트 상태 |

**CJK 폰트 지원:**
- `getTypefaceForFont()` 오버라이드로 전역 적용
- Bold Malgun Gothic — ComboBox, PopupMenu, ToggleButton
- 한국어 장치명 □□□ 깨짐 방지

#### 4.6.10 레벨 미터 (LevelMeter)
| 항목 | 상세 |
|------|------|
| 표시 | 현재 RMS 레벨 (초록→노랑→빨강) |
| 피크 홀드 | 피크 인디케이터 (30틱 ≈ 1초 @30Hz) |
| 클리핑 | 0dBFS 클리핑 인디케이터 |
| 스무딩 | Attack: 0.3, Release: 0.05 |
| 갱신 | 부모 타이머에서 `tick()` 호출 (30Hz). 조건부 repaint |

---

### 4.7 패닉 뮤트

| 항목 | 상세 |
|------|------|
| 동작 | 즉시 모든 오디오 출력 뮤트 + 녹음 자동 중지 |
| 잠금 | 패닉 뮤트 중 OUT/MON/VST 버튼 잠금 |
| 외부 제어 잠금 | 핫키/MIDI/Stream Deck/HTTP 모두 잠금 — PanicMute 액션만 허용 |
| 액션 차단 | 바이패스, 볼륨, 게인, 프리셋, 녹음, 플러그인 파라미터 등 모든 액션 차단 |
| 녹음 | 녹음 중이면 자동 중지 (해제 시 자동 재시작 안 함) |
| 모니터 상태 | 뮤트 전 모니터/출력/IPC 활성 상태 기억, 언뮤트 시 복원 |
| 상태 지속 | 패닉 뮤트 상태가 재시작 후에도 유지됨 |
| 해제 | PanicMute 액션으로만 해제 가능 |
| input_muted | `muted`와 동일 (독립 입력 뮤트 없음, InputMuteToggle = PanicMute) |

---

### 4.8 DirectPipe Receiver 플러그인 (VST2/VST3/AU)

#### 기본 정보
| 항목 | 상세 |
|------|------|
| 이름 | DirectPipe Receiver |
| 포맷 | VST2 / VST3 / AU (macOS) |
| 입력 | **없음 (입력 버스 없음)** — `BusesProperties().withOutput(...)` only, no `.withInput()` |
| 출력 | 스테레오 (2채널) 또는 모노 (1채널). `isBusesLayoutSupported`가 mono와 stereo 모두 허용 |
| 레이턴시 보고 | `setLatencySamples(targetFillFrames)` — 버퍼 프리셋 크기를 호스트 DAW에 보고. 프리셋 변경 시 processBlock 내에서 동적 갱신 |
| MIDI | 없음 |
| 프로그램 | 1개 (전환 없음) |

#### 핵심 동작 원리
DirectPipe Receiver는 **입력 버스가 없는 출력 전용 플러그인**이다. `processBlock()`에서 호스트(OBS)가 전달하는 입력 오디오 버퍼를 IPC 공유 메모리에서 읽은 데이터로 **완전히 덮어쓴다**. 따라서:

- OBS 오디오 소스(마이크 캡처 등)의 원본 오디오는 **무시됨**
- OBS 필터 체인에서 Receiver 앞에 있는 다른 필터의 출력도 **무시됨**
- DirectPipe에서 VST 체인을 거쳐 IPC로 전송된 **최종 처리 오디오만 출력**

이 설계 덕분에 OBS 마이크 소스의 자체 오디오와 DirectPipe 오디오가 중복되지 않으며, IPC 토글(VST 버튼)로 OBS 마이크를 독립적으로 ON/OFF 할 수 있다.

DirectPipe Receiver is an **output-only plugin with no input bus**. In `processBlock()`, it completely replaces the host (OBS) buffer with data read from IPC shared memory. Therefore: OBS source audio is ignored, preceding filters are ignored, only DirectPipe's final processed audio is output. This design prevents audio duplication and enables independent OBS mic control via IPC toggle (VST button).

#### 파라미터 (AudioProcessorValueTreeState)
| ID | 타입 | 기본값 | 설명 |
|----|------|--------|------|
| `mute` | Boolean | false | 오디오 뮤트 |
| `buffer` | Choice (0-4) | 1 (Low) | 버퍼 프리셋 선택 |

#### 버퍼 프리셋
| # | 이름 | targetFillFrames | highFillThreshold | lowFillThreshold | 레이턴시 @48kHz |
|---|------|-----------------|-------------------|------------------|----------------|
| 0 | Ultra Low | 256 | 768 | 64 | ~5ms |
| 1 | Low | 512 | 1536 | 128 | ~10ms |
| 2 | Medium | 1024 | 3072 | 256 | ~21ms |
| 3 | High | 2048 | 6144 | 512 | ~42ms |
| 4 | Safe | 4096 | 12288 | 1024 | ~85ms |

#### IPC 연결
| 항목 | 상세 |
|------|------|
| 공유 메모리 이름 | `Local\\DirectPipeAudio` |
| 프로토콜 | SPSC 링 버퍼, atomic read/write 포지션. RingBuffer에 atomic `detached_` 플래그 (detach 시 읽기/쓰기 즉시 차단) |
| 연결 확인 | `producer_active` 플래그 (acquire) |
| 재연결 간격 | 100 블록마다 |
| 드리프트 워밍업 | 50 블록 후 클록 드리프트 체크 시작 |

#### 오디오 처리
0. 인터리브 버퍼 empty 가드 → prepareToPlay 전 호출 시 즉시 무음 반환
1. Mute 확인 → 뮤트면 버퍼 클리어
2. 미연결: 페이드아웃 또는 무음
3. 연결 시: producer 활성 확인. **Stale consumer_active 감지**: detach() 후 close() 호출하여 OBS 크래시 등으로 남은 spurious multi-consumer 경고 방지
4. 클록 드리프트 보상: 버퍼 > highThreshold이면 초과 프레임 스킵
5. 링 버퍼에서 프레임 읽기
6. 인터리브 → JUCE planar 변환
7. 부분 읽기 시 패딩 (무음)

#### Clock Drift Compensation

호스트와 DAW/OBS의 오디오 클록이 미세하게 다를 때 발생하는 버퍼 드리프트를 자동 보상.

| 상태 | 조건 | 동작 |
|------|------|------|
| 정상 | lowThreshold/2 ≤ fill ≤ highThreshold | 그대로 읽기 |
| 버퍼 과다 (호스트 빠름) | fill > highThreshold | excess 프레임 스킵 → targetFill로 복귀 |
| 데드 밴드 (히스테리시스) | lowThreshold/2 ≤ fill < lowThreshold | 정상 읽기 — 스로틀/정상 모드 간 진동 방지 |
| 버퍼 부족 (호스트 느림) | fill < lowThreshold/2 | 읽기량 절반으로 쿠션 확보 → 하드 클릭 대신 미세 갭 |

버퍼 프리셋별 임계값:

| 프리셋 | Target | High Threshold | Low Threshold |
|--------|--------|----------------|---------------|
| Ultra Low (256) | 256 | 768 | 64 |
| Low (512) | 512 | 1536 | 128 |
| Medium (1024) | 1024 | 3072 | 256 |
| High (2048) | 2048 | 6144 | 512 |
| Safe (4096) | 4096 | 12288 | 1024 |

#### 페이드아웃 로직
- 마지막 출력 버퍼: 64 샘플 (planar 형식)
- 페이드 게인: 1.0 → 0.0, 샘플당 0.05 감소
- 언더런 시 마지막 샘플을 페이딩하며 재생

#### 에디터 UI (240×200px)
| 요소 | 설명 |
|------|------|
| 타이틀 | "DirectPipe Receiver" (볼드 화이트, 16pt) |
| 상태 원 | 초록(연결) / 빨강(미연결) |
| 상태 텍스트 | "Connected" / "Disconnected" |
| 오디오 정보 | 연결 시: "{SR}Hz {Channels}ch" |
| Mute 버튼 | 빨강(ON) / 어두운(OFF), 40px 높이 |
| Buffer ComboBox | 5개 프리셋 |
| 레이턴시 라벨 | "X.XX ms (YYYY samples @ ZZZZ Hz)" |
| SR 경고 | "SR mismatch: {source} vs {host}" (주황, 10pt) |
| 버전 | "v4.0.1" (우하단, 10pt) |
| 갱신 | 10Hz 타이머 콜백 |

---

### 4.9 IPC 코어 라이브러리

#### 프로토콜 헤더 (DirectPipeHeader)
```
alignas(64) atomic<uint64_t> write_pos    — 프로듀서 증가
alignas(64) atomic<uint64_t> read_pos     — 컨슈머 증가
uint32_t sample_rate                       — 샘플레이트
uint32_t channels                          — 채널 수
uint32_t buffer_frames                     — 버퍼 프레임 수
uint32_t version                           — 프로토콜 버전 (1)
atomic<bool> producer_active               — 프로듀서 활성 플래그
```
64바이트 정렬 (false sharing 방지)

#### 공유 메모리 레이아웃
```
[Header (64+ bytes)] [Ring Buffer PCM (interleaved floats)]
```
크기 = sizeof(Header) + (buffer_frames × channels × sizeof(float))

#### 상수
| 상수 | 값 | 설명 |
|------|-----|------|
| SHM_NAME | Windows: `Local\\DirectPipeAudio`, POSIX: `/DirectPipeAudio` | 공유 메모리 이름 |
| EVENT_NAME | `Local\\DirectPipeDataReady` | 이벤트 이름 |
| DEFAULT_BUFFER_FRAMES | 16384 | ~341ms @48kHz |
| DEFAULT_SAMPLE_RATE | 48000 | 기본 SR |
| DEFAULT_CHANNELS | 2 | 스테레오 |
| PROTOCOL_VERSION | 1 | 프로토콜 버전 |

#### SharedMemWriter (호스트 측)
- `initialize(sampleRate, channels, bufferFrames)` — 공유 메모리 생성
- `writeAudio(buffer, numSamples)` — RT-safe. 사전 할당된 인터리브 버퍼로 변환 후 쓰기
- `shutdown()` — `producer_active` false 설정 → 5ms 대기 → 메모리/이벤트 해제

---

### 4.10 오디오 녹음 (AudioRecorder)

| 항목 | 상세 |
|------|------|
| 포맷 | WAV, 24-bit |
| FIFO | 32768 샘플 (~0.68초 @48kHz) |
| 락 | `juce::SpinLock` (RT-safe) — writer teardown 보호 |
| 스레드 | `juce::TimeSliceThread "Audio Writer"` — FIFO → 디스크 |
| 시작 | 부모 디렉토리 생성, samplesWritten 리셋, ThreadedWriter 생성 |
| 정지 | recording_ false (seq_cst) → SpinLock 획득 → writer 파괴 (FIFO 플러시) |
| 쓰기 | recording_ 확인 (acquire) → SpinLock → ThreadedWriter 쓰기 → samplesWritten 증가 |
| 자동 정지 | 오디오 장치 변경 시 |
| 시간 표시 | Timer 기반. `getRecordedSeconds() = samplesWritten / sampleRate` |

---

### 4.11 설정 관리

#### 4.11.1 자동 저장
- Dirty-flag 패턴 + 1초 디바운스 (30Hz 타이머에서 30틱 카운트다운)
- `onSettingsChanged` 콜백 → `markSettingsDirty()` → 카운터 리셋 → 0 도달 시 `saveSettings()`
- `loadingSlot_` 또는 `isLoading()` true면 저장 스킵 (부분 상태 손상 방지)
- 저장 위치: Windows `%AppData%/DirectPipe/settings.dppreset`, macOS `~/Library/Application Support/DirectPipe/`, Linux `~/.config/DirectPipe/` (JSON, 버전 4)

#### 4.11.2 프리셋 파일 형식 (JSON, version 4)
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

#### 4.11.3 백업/복원 (2단계)

**플랫폼 호환성**: `importAll()` / `importFullBackup()` 함수가 내부적으로 플랫폼 호환성을 검사하여, 다른 OS에서 만든 백업의 복원을 차단한다 (UI 레벨이 아닌 API 레벨에서 보호).

**Tier 1: Settings Only (`.dpbackup`, v2)**
| 포함 | 제외 |
|------|------|
| 오디오 설정 (드라이버, 장치, SR, BS, 채널) | VST 플러그인 체인 (`plugins` 키 제거) |
| 출력 설정 (모니터 장치, 볼륨, 버퍼) | 프리셋 슬롯 A-E |
| 컨트롤 매핑 (핫키, MIDI, 서버 설정) | |

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
| 포함 |
|------|
| Tier 1 전체 내용 |
| VST 플러그인 체인 (전체 상태) |
| 프리셋 슬롯 A-E (각 슬롯 JSON) |

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

#### 4.11.4 유지보수 (Settings > Maintenance)
| 기능 | 동작 | 확인 |
|------|------|------|
| Full Backup | `.dpfullbackup`으로 전체 백업 | 파일 선택 |
| Full Restore | `.dpfullbackup`에서 전체 복원 (같은 OS만 — 크로스 OS 복원 차단) | 확인 다이얼로그 |
| Clear Plugin Cache | 스캔 캐시 XML 삭제 | 확인 다이얼로그 |
| Clear All Presets | 슬롯 A-E + Auto 파일 + 백업 + 임시 파일 삭제, 활성 체인 초기화 | 확인 다이얼로그 |
| Factory Reset | 모든 데이터 삭제 (설정, 컨트롤, 프리셋(A-E + Auto), 캐시, 녹음 설정) | 확인 다이얼로그 |

---

### 4.12 로깅 시스템

#### DirectPipeLogger
| 항목 | 상세 |
|------|------|
| 링 버퍼 | 최대 512개 대기 항목 |
| 인덱스 | `writeIdx_`, `readIdx_` (atomic<uint32_t>, 오버플로 안전) |
| 뮤텍스 | `std::mutex writeMutex_` (MPSC 안전) |
| 로그 파일 | `%AppData%/DirectPipe/directpipe.log` |
| drain | 메시지 스레드에서만 호출. 모든 대기 라인 일괄 읽기 → 단일 TextEditor 삽입 → 단일 trim |

#### 로그 카테고리 태그
`[APP]`, `[AUDIO]`, `[VST]`, `[PRESET]`, `[ACTION]`, `[HOTKEY]`, `[MIDI]`, `[WS]`, `[HTTP]`, `[MONITOR]`, `[IPC]`, `[REC]`, `[CONTROL]`

#### 로그 레벨
- `info(cat, msg)` → `"INF [CAT] {msg}"`
- `warn(cat, msg)` → `"WRN [CAT] {msg}"`
- `error(cat, msg)` → `"ERR [CAT] {msg}"`
- `audit(cat, msg)` → `"AUD [CAT] {msg}"` (Audit Mode 활성 시에만)

#### 스코프드 타이머
```cpp
{ Log::Timer t("VST", "Chain swap"); ... }
// 스코프 종료 시: "INF [VST] Chain swap (15ms)"
```

#### 로깅 제외 (스팸 방지)
- 고빈도 액션: SetVolume, InputGainAdjust, SetPluginParameter
- Continuous MIDI 바인딩

---

### 4.13 자동 업데이트

| 항목 | 상세 |
|------|------|
| 체크 | 시작 시 백그라운드 스레드에서 GitHub API (최신 릴리즈) |
| 비교 | Semver 비교 |
| 표시 | Credit 라벨에 "NEW vX.Y.Z" (주황색) |
| 다이얼로그 | [Update Now] (Windows only) / [View on GitHub] / [Later] |
| 업데이트 | Windows: GitHub 릴리즈에서 ZIP 다운로드 → batch 스크립트로 바이너리 교체 → 자동 재시작. macOS/Linux: "View on GitHub" 버튼으로 릴리즈 페이지 안내 (수동 다운로드) |
| 완료 확인 | `_updated.flag` 파일 → 다음 시작 시 "Updated successfully" 알림 |
| 스레드 안전 | `alive_` 플래그 (`shared_ptr<atomic<bool>>`) 사용 (`checkForUpdate` callAsync) |

---

### 4.14 시스템 기능

| 기능 | 상세 |
|------|------|
| **시스템 시작** | Windows: 레지스트리 `HKCU\...\Run\DirectPipe`. macOS: LaunchAgent (`~/Library/LaunchAgents/com.directpipe.host.plist`). Linux: `~/.config/autostart/directpipe.desktop`. 트레이 메뉴 + Settings 탭에서 토글 |
| **단일 인스턴스** | `moreThanOneInstanceAllowed()` = false. `--scan` 인자 시에만 복수 허용. 포터블 모드에서는 true (다중 인스턴스 허용, 폴더별 InterProcessLock으로 같은 폴더 중복만 차단) |
| **프로세스 우선순위** | `HIGH_PRIORITY_CLASS` |
| **포터블 모드** | exe 옆 `portable.flag` → 설정을 `./config/`에 저장. `ControlMappingStore::getConfigDirectory()` 사용 |
| **다중 인스턴스 외부 제어** | Windows: Named Mutex. macOS/Linux: file-based lock. 외부 제어(핫키/MIDI/WS/HTTP) 충돌 방지. 일반 모드 실행 중 → 포터블 Audio Only. 포터블 제어 중 → 일반 모드 차단. 포터블 2개+ → 첫 번째만 제어. 타이틀 바 "(Audio Only)", 상태 바 주황색 "Portable", 트레이 "(Portable/Audio Only)" 표시 |
| **콤보박스 새로고침** | `addMouseListener(this, true)` → `mouseDown`에서 `scanForDevices()` + 목록 재빌드. `eventComponent` 체크로 배경 클릭 방지. 소멸자에서 `removeMouseListener` |

---

## 5. 기술 스택

| 항목 | 기술 | 버전 |
|------|------|------|
| 언어 | C++17 | — |
| 프레임워크 | JUCE | 7.0.12 |
| 빌드 | CMake | 3.22+ |
| 오디오 (Windows) | WASAPI Shared + Low Latency (IAudioClient3) + Exclusive | Windows 내장 |
| 오디오 (Windows) | ASIO — Steinberg ASIO SDK | `thirdparty/asiosdk/common` |
| 오디오 (macOS) | CoreAudio | macOS 내장 |
| 오디오 (Linux) | ALSA, JACK | Linux 내장 |
| VST2 | VST2 SDK | 2.4 |
| VST3 | JUCE 내장 VST3 지원 | — |
| WebSocket | JUCE StreamingSocket + RFC 6455 | 커스텀 SHA-1 |
| HTTP | JUCE StreamingSocket 수동 파싱 | — |
| Stream Deck | @elgato/streamdeck SDK | v2.0.1 (SDKVersion 3) |
| Stream Deck WebSocket | ws (npm) | v8.16.0 |
| 번들러 | Rollup | v4.59.0 |
| 테스트 | Google Test | — |
| IPC | SharedMemory + lock-free RingBuffer | Windows: CreateFileMapping, macOS: `shm_open`, Linux: POSIX shm |

---

## 6. 파일 구조

```
DirectPipe/
├── core/                           → IPC 라이브러리
│   └── include/directpipe/
│       ├── Constants.h             → SHM_NAME, DEFAULT_BUFFER_FRAMES 등
│       ├── Protocol.h              → DirectPipeHeader (64바이트 정렬)
│       ├── RingBuffer.h            → SPSC lock-free 링 버퍼
│       └── SharedMemory.h          → Windows 공유 메모리 + NamedEvent
│
├── host/                           → JUCE 메인 앱
│   ├── Resources/                  → 아이콘 (16~512px PNG + SVG)
│   └── Source/
│       ├── Main.cpp                → 앱 진입점, 트레이, 스캐너 모드
│       ├── MainComponent.h/cpp     → 메인 UI 레이아웃 (~729줄, 헬퍼 클래스에 위임)
│       ├── ActionResult.h          → 성공/실패 반환 타입 (ok/fail/bool 변환)
│       ├── Audio/
│       │   ├── AudioEngine.h/cpp       → 오디오 콜백, 장치 관리, 재연결, XRun
│       │   ├── VSTChain.h/cpp          → 플러그인 체인, 그래프, 비동기 로딩
│       │   ├── OutputRouter.h/cpp      → 모니터 출력 라우팅
│       │   ├── MonitorOutput.h/cpp     → 별도 WASAPI 모니터 장치
│       │   ├── AudioRingBuffer.h       → Lock-free 스테레오 링 버퍼
│       │   ├── AudioRecorder.h/cpp     → WAV 녹음 (ThreadedWriter)
│       │   ├── PluginPreloadCache.h/cpp → 슬롯 백그라운드 프리로드
│       │   ├── LatencyMonitor.h        → 실시간 레이턴시/CPU 측정
│       │   ├── SafetyLimiter.h/cpp     → RT-safe 피드포워드 리미터
│       │   ├── DeviceState.h           → 장치 연결 상태 enum 상태 머신
│       │   ├── BuiltinFilter.h/cpp     → HPF+LPF 오디오 프로세서
│       │   ├── BuiltinNoiseRemoval.h/cpp → RNNoise 기반 노이즈 제거
│       │   ├── BuiltinAutoGain.h/cpp   → LUFS 기반 자동 게인 제어
│       │   └── PluginLoadHelper.h      → 크로스 플랫폼 VST 로딩 헬퍼
│       ├── Control/
│       │   ├── ActionDispatcher.h      → 19개 Action enum, 메시지 스레드 디스패치
│       │   ├── ActionHandler.h/cpp     → 중앙 액션 이벤트 처리 (MainComponent에서 추출)
│       │   ├── SettingsAutosaver.h/cpp → dirty-flag + 디바운스 자동 저장 (MainComponent에서 추출)
│       │   ├── ControlManager.h        → 컨트롤 핸들러 소유, configStore_
│       │   ├── ControlMapping.cpp      → 기본 핫키 11개
│       │   ├── WebSocketServer.h/cpp   → RFC 6455, UDP 디스커버리, 19 명령
│       │   ├── HttpApiServer.cpp       → REST API, 23 엔드포인트, CORS
│       │   ├── HotkeyHandler.h/cpp     → RegisterHotKey, 글로벌 핫키
│       │   ├── MidiHandler.h/cpp       → CC/Note, Learn, 4 바인딩 타입
│       │   ├── StateBroadcaster.h/cpp  → 26필드 상태 JSON, 해시 기반 더티 체크
│       │   ├── DirectPipeLogger.h/cpp  → 링 버퍼 로깅, 13 카테고리
│       │   └── Log.h/cpp              → 로그 유틸리티, 스코프드 타이머
│       ├── IPC/
│       │   └── SharedMemWriter.h/cpp   → RT-safe IPC 쓰기, 인터리브 변환
│       └── UI/
│           ├── AudioSettings.h/cpp     → 5종 드라이버, ASIO 채널, SR/BS
│           ├── OutputPanel.h/cpp       → 모니터 + IPC + 녹음 UI
│           ├── ControlSettingsPanel.h/cpp → 3 서브탭 컨테이너 (탭별 별도 클래스)
│           ├── HotkeyTab.h/cpp         → 핫키 설정 탭 (ControlSettingsPanel에서 추출)
│           ├── MidiTab.h/cpp           → MIDI 설정 탭 (ControlSettingsPanel에서 추출)
│           ├── StreamDeckTab.h/cpp     → Stream Deck 설정 탭 (ControlSettingsPanel에서 추출)
│           ├── PresetSlotBar.h/cpp     → 프리셋 슬롯 A-E 버튼 바 (MainComponent에서 추출)
│           ├── StatusUpdater.h/cpp     → 상태 바 업데이트 (MainComponent에서 추출)
│           ├── UpdateChecker.h/cpp     → 백그라운드 업데이트 체크 (MainComponent에서 추출)
│           ├── PluginChainEditor.h/cpp → 드래그드롭, 바이패스, GUI 편집
│           ├── PluginScanner.h/cpp     → Out-of-process 스캔, 검색/정렬
│           ├── PresetManager.h/cpp     → 슬롯 A-E, 자동 저장, Copy/Delete
│           ├── LevelMeter.h/cpp        → RMS 미터, 피크 홀드, 스무딩
│           ├── LogPanel.h/cpp          → 로그 뷰어, 유지보수, 팩토리 리셋
│           ├── NotificationBar.h/cpp   → 컬러코딩 알림, 자동 페이드
│           ├── DirectPipeLookAndFeel.h/cpp → 다크 테마, CJK 폰트
│           ├── SettingsExporter.h/cpp  → 2단계 백업 (.dpbackup / .dpfullbackup)
│           ├── FilterEditPanel.h/cpp   → 내장 필터 편집 패널
│           ├── NoiseRemovalEditPanel.h/cpp → 노이즈 제거 편집 패널
│           ├── AGCEditPanel.h/cpp      → 자동 게인 제어 편집 패널
│           └── DeviceSelector.h/cpp    → 오디오 장치 선택 위젯
│
├── plugins/receiver/               → DirectPipe Receiver (VST2/VST3/AU)
│   └── Source/
│       ├── PluginProcessor.h/cpp   → IPC 소비자, 5 버퍼 프리셋, 페이드아웃
│       └── PluginEditor.h/cpp      → 240×200 UI, 상태/SR 경고
│
├── com.directpipe.directpipe.sdPlugin/ → Stream Deck 플러그인
│   ├── manifest.json               → SDKVersion 3, 10 액션, v4.0.1.0
│   ├── package.json                → ws v8.16, @elgato/streamdeck v2.0.1
│   └── src/
│       ├── plugin.js               → 진입점, UDP 디스커버리, 상태 관리
│       ├── websocket-client.js     → WS 클라이언트, 재연결, 큐잉
│       └── actions/                → 10개 SingletonAction 클래스
│
├── tests/                          → Google Test (core + host, 294+ tests)
├── tools/                          → midi-test.py, pre-release-test.sh, pre-release-dashboard.html
├── docs/                           → USER_GUIDE, CONTROL_API, STREAMDECK_GUIDE 등
└── dist/                           → 빌드 산출물 + .streamDeckPlugin
```

---

## 7. 경쟁 포지셔닝

### DirectPipe는 이것이다
- 마이크 전용 경량 VST 호스트
- 방송자를 위한 마이크 리모컨
- LightHost의 정통 후계자

### DirectPipe는 이것이 아니다
- 멀티채널 소프트웨어 믹서 (Wave Link)
- 가상 오디오 라우터 (VoiceMeeter)
- DAW (Reaper, Ableton)
- 하드웨어 번들 소프트웨어 (GoXLR)

### 핵심 차별점
1. **5종 외부 제어 통합** — 핫키/MIDI/Stream Deck/HTTP/WebSocket을 한 프로그램에서
2. **프리셋 즉시 전환** — A-E 슬롯, Keep-Old-Until-Ready로 10-50ms 교체
3. **DirectPipe Receiver** — 가상 케이블 없이 OBS 직접 연결 (SharedMemory IPC, VST2/VST3/AU)
4. **포터블** — 설치 없이, 시스템 변경 없이 단일 exe 실행
5. **오픈소스** — GPL v3, 누구나 기여 가능
