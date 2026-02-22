# DirectPipe — USB 마이크 VST 호스트 + 초저지연 루프백 시스템

## 개발 계획서 v1.0

> **목표:** USB 마이크 사용자가 하드웨어 오디오 인터페이스 없이도 VST 플러그인을 적용하고,  
> 방송/통화 앱에 초저지연(3~12ms)으로 오디오를 전달하는 올인원 소프트웨어

---

## 1. 프로젝트 개요

### 1.1 해결하는 문제

| 기존 방식 | 문제점 |
|-----------|--------|
| VB-Cable + Light Host | WASAPI 버퍼 4~5단 경유 → 40~80ms 딜레이 |
| VoiceMeeter | 설정 복잡, 리소스 과다, 여전히 20~40ms |
| 하드웨어 오인페 | 비용 발생, 물리 장비 필요 |

### 1.2 DirectPipe 해결책

```
USB Mic → WASAPI Exclusive (~2.7ms) → VST Chain (0ms 추가) 
  ├──→ [OBS Plugin] 공유 메모리 직결 (3~6ms 총 레이턴시)
  └──→ [Virtual Mic] 번들 드라이버 경유 (8~12ms, Discord/Zoom용)
```

### 1.3 기술 스택

| 영역 | 기술 | 이유 |
|------|------|------|
| 오디오 엔진 | JUCE 7+ (C++) | WASAPI Exclusive 지원, VST 호스팅 내장 |
| VST 호스트 | JUCE AudioPluginHost 모듈 | VST2/VST3 로딩, 체인 관리 |
| IPC | Windows Shared Memory + Named Event | Lock-free SPSC ring buffer |
| OBS 플러그인 | OBS Plugin SDK (C) | obs-studio 소스 플러그인 |
| 가상 마이크 | 번들 드라이버 (기존 서명된 것 활용) | 커널 드라이버 직접 개발 회피 |
| UI | JUCE GUI (LookAndFeel 커스텀) | 오디오 앱과 UI 통합 |
| 빌드 | CMake + JUCE CMake API | 크로스 모듈 빌드 관리 |

---

## 2. 시스템 아키텍처

### 2.1 전체 구조도

```
┌─────────────────────────────────────────────────────────────────┐
│                      DirectPipe Application                      │
│                                                                   │
│  ┌───────────┐    ┌──────────────┐    ┌───────────────────────┐  │
│  │  Audio     │    │  VST Plugin  │    │    Output Router      │  │
│  │  Input     │───→│  Chain       │───→│                       │  │
│  │  Manager   │    │  Engine      │    │  ┌─────────────────┐  │  │
│  │            │    │              │    │  │ SharedMemory     │  │  │
│  │ WASAPI     │    │ Plugin 1     │    │  │ Ring Buffer      │──│──│──→ OBS Plugin
│  │ Exclusive  │    │ Plugin 2     │    │  │ (SPSC Lock-free) │  │  │
│  │            │    │ Plugin N     │    │  └─────────────────┘  │  │
│  │ USB Mic    │    │              │    │                       │  │
│  │ 선택/설정  │    │ Dry/Wet Mix  │    │  ┌─────────────────┐  │  │
│  └───────────┘    │ Gain Control │    │  │ WASAPI Output    │──│──│──→ Virtual Mic
│                    └──────────────┘    │  │ (번들 드라이버)  │  │  │    (Discord 등)
│                                        │  └─────────────────┘  │  │
│  ┌──────────────────────────────────┐  │                       │  │
│  │          GUI Layer               │  │  ┌─────────────────┐  │  │
│  │  - 입력 장치 선택                │  │  │ Local Monitor    │──│──│──→ 헤드폰
│  │  - VST 체인 관리 (추가/삭제/순서)│  │  │ (Direct Out)     │  │  │    (본인 모니터링)
│  │  - 입력/출력 볼륨 미터           │  │  └─────────────────┘  │  │
│  │  - 레이턴시 모니터               │  └───────────────────────┘  │
│  │  - 프리셋 관리                   │                             │
│  └──────────────────────────────────┘                             │
└───────────────────────────────────────────────────────────────────┘

          ┌──────────────────────────────┐
          │  OBS Source Plugin (DLL)     │
          │  obs-directpipe-source.dll   │
          │                              │
          │  공유 메모리에서 PCM 직접    │
          │  읽어서 OBS에 Audio Source   │
          │  로 전달                     │
          └──────────────────────────────┘
```

### 2.2 데이터 플로우 상세

```
[오디오 콜백 스레드 - 실시간, 최고 우선순위]

1. WASAPI Exclusive 콜백 발생 (128 samples @48kHz = 2.67ms 주기)
2. USB 마이크에서 PCM float32 데이터 수신
3. VST 플러그인 체인 순차 처리 (processBlock 인라인 호출)
4. 처리된 PCM을 3개 출력으로 동시 분배:
   a. Shared Memory Ring Buffer에 write → OBS 플러그인이 read
   b. WASAPI 출력 → 번들 가상 마이크 드라이버
   c. WASAPI 출력 → 로컬 모니터링 (헤드폰)
5. Named Event 시그널 → OBS 플러그인에 새 데이터 알림
```

### 2.3 공유 메모리 Ring Buffer 설계

```
구조: SPSC (Single Producer Single Consumer) Lock-free Ring Buffer

메모리 레이아웃:
┌──────────────────────────────────────────────┐
│ Header (64 bytes, cache-line aligned)        │
│  - write_pos: atomic<uint64_t>               │
│  - read_pos:  atomic<uint64_t>               │
│  - sample_rate: uint32_t                     │
│  - channels: uint32_t                        │
│  - buffer_frames: uint32_t                   │
│  - version: uint32_t (프로토콜 버전)         │
│  - active: atomic<bool> (연결 상태)          │
├──────────────────────────────────────────────┤
│ Ring Buffer Data                             │
│  - float32 PCM samples                       │
│  - 크기: 4096 frames × 2ch × 4bytes = 32KB  │
│  - ~85ms 분량 버퍼 (충분한 여유)             │
└──────────────────────────────────────────────┘

동기화:
- Named Event "DirectPipeDataReady" → 새 데이터 시그널
- Producer(JUCE앱): write 후 SetEvent()
- Consumer(OBS): WaitForSingleObject() 후 read
- Timeout 500ms → 연결 끊김 감지
```

---

## 3. 디렉토리 구조

```
directpipe/
├── CMakeLists.txt                    # 루트 CMake (전체 빌드 오케스트레이션)
├── CLAUDE.md                         # Claude Code 프로젝트 컨텍스트
├── README.md                         # 사용자 문서
│
├── core/                             # 핵심 공유 라이브러리
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── directpipe/
│   │       ├── RingBuffer.h          # SPSC lock-free ring buffer
│   │       ├── SharedMemory.h        # Windows shared memory 래퍼
│   │       ├── Protocol.h            # IPC 프로토콜 정의 (헤더 구조체 등)
│   │       └── Constants.h           # 공유 상수 (버퍼 크기, 이름 등)
│   └── src/
│       ├── RingBuffer.cpp
│       └── SharedMemory.cpp
│
├── host/                             # JUCE VST Host 앱 (메인 애플리케이션)
│   ├── CMakeLists.txt
│   ├── Source/
│   │   ├── Main.cpp                  # 앱 진입점
│   │   ├── MainComponent.h/cpp       # 메인 오디오 + UI 컴포넌트
│   │   ├── Audio/
│   │   │   ├── AudioEngine.h/cpp     # WASAPI 입력 → VST 체인 → 출력 라우팅
│   │   │   ├── VSTChain.h/cpp        # VST 플러그인 로딩/체인 관리
│   │   │   ├── OutputRouter.h/cpp    # 3개 출력 분배 (SharedMem, WASAPI, Monitor)
│   │   │   └── LatencyMonitor.h/cpp  # 실시간 레이턴시 측정/표시
│   │   ├── IPC/
│   │   │   └── SharedMemWriter.h/cpp # 공유 메모리에 PCM write (Producer)
│   │   └── UI/
│   │       ├── DeviceSelector.h/cpp  # 입력 장치 선택 UI
│   │       ├── PluginChainEditor.h/cpp # VST 체인 편집 UI
│   │       ├── LevelMeter.h/cpp      # 입출력 볼륨 미터
│   │       ├── PresetManager.h/cpp   # 프리셋 저장/로드
│   │       └── LookAndFeel.h/cpp     # 커스텀 UI 테마
│   └── Resources/
│       └── icon.png
│
├── obs-plugin/                       # OBS Studio 소스 플러그인
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── plugin-main.c             # OBS 플러그인 진입점
│   │   ├── directpipe-source.c/h     # Audio Source 구현
│   │   └── shm-reader.c/h           # 공유 메모리에서 PCM read (Consumer)
│   └── data/
│       └── locale/
│           ├── en-US.ini
│           └── ko-KR.ini
│
├── installer/                        # 설치 프로그램
│   ├── directpipe.iss                # Inno Setup 스크립트
│   └── assets/
│       ├── banner.bmp
│       └── license.txt
│
├── tests/                            # 테스트
│   ├── CMakeLists.txt
│   ├── test_ring_buffer.cpp          # Ring buffer 단위 테스트
│   ├── test_shared_memory.cpp        # 공유 메모리 IPC 테스트
│   ├── test_latency.cpp             # 레이턴시 벤치마크
│   └── test_audio_engine.cpp        # 오디오 엔진 통합 테스트
│
└── docs/
    ├── ARCHITECTURE.md               # 상세 아키텍처 문서
    ├── BUILDING.md                   # 빌드 가이드
    └── USER_GUIDE.md                # 사용자 가이드
```

---

## 4. 개발 Phase 및 태스크

### Phase 0: 환경 설정 (Day 1)

```
목표: 빌드 환경 구축, 프로젝트 스캐폴딩

태스크:
□ P0-1: CMake 프로젝트 구조 생성 (루트 + 3개 서브 프로젝트)
□ P0-2: JUCE를 CPM 또는 git submodule로 추가
□ P0-3: OBS Plugin SDK 연동 (obs-plugintemplate 참고)
□ P0-4: CLAUDE.md 작성 (Claude Code 프로젝트 컨텍스트)
□ P0-5: 빌드 검증 (빈 JUCE 앱 + 빈 OBS 플러그인 빌드 확인)

산출물:
- 빌드 가능한 빈 프로젝트 스캐폴딩
- CLAUDE.md (Claude Code가 참조할 프로젝트 맥락)

의존성:
- JUCE 7.x (https://github.com/juce-framework/JUCE)
- CMake 3.22+
- Visual Studio 2022 (MSVC v143)
- OBS Studio Plugin SDK / libobs
```

### Phase 1: Core IPC 라이브러리 (Day 2~3)

```
목표: SPSC Lock-free Ring Buffer + Windows Shared Memory 구현

태스크:
□ P1-1: Protocol.h — 공유 메모리 헤더 구조체 정의
         struct DirectPipeHeader {
           atomic<uint64_t> write_pos;
           atomic<uint64_t> read_pos;
           uint32_t sample_rate;    // 48000
           uint32_t channels;       // 1 or 2
           uint32_t buffer_frames;  // 4096
           uint32_t version;        // 1
           atomic<bool> active;
           uint8_t padding[...];    // 64 byte alignment
         };

□ P1-2: RingBuffer.h/cpp — SPSC lock-free ring buffer
         - write(const float* data, uint32_t frames) → uint32_t written
         - read(float* data, uint32_t frames) → uint32_t read
         - available_read() / available_write()
         - memory_order_acquire / release 기반 동기화
         - cache-line padding으로 false sharing 방지

□ P1-3: SharedMemory.h/cpp — Windows shared memory 래퍼
         - create(name, size) — CreateFileMapping + MapViewOfFile
         - open(name) — OpenFileMapping + MapViewOfFile
         - close() — UnmapViewOfFile + CloseHandle
         - Named Event 생성/시그널/대기

□ P1-4: Constants.h — 공유 상수
         - SHM_NAME = "Local\\DirectPipeAudio"
         - EVENT_NAME = "Local\\DirectPipeDataReady"
         - DEFAULT_BUFFER_FRAMES = 4096
         - DEFAULT_SAMPLE_RATE = 48000

□ P1-5: 단위 테스트 — ring buffer 정합성 테스트
         - 단일 스레드 write/read 검증
         - 멀티 스레드 producer/consumer 동시 실행
         - 오버플로우/언더플로우 처리 확인
         - 레이턴시 측정 (rdtsc 기반)

기술 핵심:
- std::atomic<uint64_t> + memory_order_release/acquire
- alignas(64) 로 cache-line 정렬
- power-of-2 버퍼 크기 (비트마스크 모듈로 연산)

산출물:
- directpipe-core 정적 라이브러리
- 통과하는 단위 테스트
```

### Phase 2: JUCE 오디오 엔진 (Day 4~7)

```
목표: USB 마이크 입력 → VST 처리 → 출력 라우팅 파이프라인

태스크:
□ P2-1: AudioEngine 기본 구조
         - JUCE AudioDeviceManager 초기화
         - WASAPI Exclusive 모드 활성화
         - 버퍼 사이즈 128 samples 설정
         - 입력 장치 열거 및 선택 기능

□ P2-2: VSTChain 구현
         - JUCE AudioPluginFormatManager로 VST2/VST3 스캔
         - 플러그인 로드/언로드
         - 체인 순서 변경 (insert, remove, reorder)
         - 각 플러그인별 bypass 토글
         - processBlock() 인라인 호출 체인

□ P2-3: OutputRouter 구현
         - 3개 출력 동시 분배 로직:
           1. SharedMemWriter → 공유 메모리 (OBS용)
           2. WASAPI Output → 가상 마이크 (Discord용)
           3. WASAPI Output → 로컬 모니터 (헤드폰)
         - 각 출력별 독립 볼륨 컨트롤
         - 각 출력별 on/off 토글

□ P2-4: SharedMemWriter 구현
         - Phase 1의 core 라이브러리 사용
         - 오디오 콜백 내에서 ring buffer write
         - Named Event 시그널
         - OBS 연결 상태 모니터링

□ P2-5: LatencyMonitor 구현
         - 입력→출력 총 레이턴시 실시간 측정
         - 각 구간별 레이턴시 표시
           (입력 버퍼 + VST 처리 + 출력 버퍼)
         - UI에 ms 단위로 표시

□ P2-6: 콘솔 기반 통합 테스트
         - 마이크 입력 → VST (게인 플러그인) → 공유 메모리 write
         - 별도 프로세스에서 공유 메모리 read → WAV 저장
         - 레이턴시 측정 결과 출력

기술 핵심:
- juce::AudioDeviceManager::AudioDeviceSetup
  - bufferSize = 128
  - sampleRate = 48000.0
  - useDefaultInputChannels = false (수동 선택)
- juce::AudioProcessorGraph 사용 가능 (VST 체인 관리)
- 오디오 스레드에서 메모리 할당/해제 절대 금지
- 모든 VST 파라미터 변경은 lock-free queue로

산출물:
- GUI 없이 동작하는 오디오 엔진
- 마이크 → VST → 공유 메모리 파이프라인 검증
```

### Phase 3: OBS 소스 플러그인 (Day 8~10)

```
목표: OBS에서 "DirectPipe Audio" 소스를 추가하면 처리된 오디오가 들리는 것

태스크:
□ P3-1: OBS 플러그인 스캐폴딩
         - obs_module_load / obs_module_unload
         - obs_register_source 등록
         - 빌드 → DLL 생성 확인

□ P3-2: DirectPipe Audio Source 구현
         - obs_source_info 정의:
           .id = "directpipe_audio_source"
           .type = OBS_SOURCE_TYPE_INPUT
           .output_flags = OBS_SOURCE_AUDIO
         - create: 공유 메모리 열기 + Named Event 핸들 획득
         - activate: 읽기 스레드 시작
         - deactivate: 읽기 스레드 정지
         - destroy: 리소스 해제

□ P3-3: 오디오 읽기 스레드 구현
         - WaitForSingleObject(event, timeout) 기반 대기
         - Ring buffer에서 PCM read
         - obs_source_output_audio()로 OBS에 전달
         - 언더런 시 무음 삽입 (글리치 방지)
         - 연결 끊김 감지 및 자동 재연결

□ P3-4: OBS 설정 UI (Properties)
         - 연결 상태 표시 (Connected / Waiting / Error)
         - 레이턴시 표시
         - 버퍼 크기 조절 옵션

□ P3-5: 통합 테스트
         - DirectPipe Host 실행 → OBS에서 소스 추가 → 오디오 확인
         - Host 종료 후 OBS 안정성 확인
         - Host 재시작 시 자동 재연결 확인
         - CPU 사용률 측정

기술 핵심:
- OBS 오디오 타이밍: obs_source_output_audio의 timestamp 관리
- 스레드 안전: OBS 메인 스레드 vs 읽기 스레드 분리
- 연결 복구: Host가 재시작되면 공유 메모리 핸들 재획득

산출물:
- obs-directpipe-source.dll
- OBS에서 DirectPipe 오디오 소스 동작 확인
```

### Phase 4: GUI 개발 (Day 11~14)

```
목표: 사용자 친화적인 원클릭 설정 가능한 인터페이스

태스크:
□ P4-1: 메인 윈도우 레이아웃
         ┌──────────────────────────────────────────┐
         │  DirectPipe                    ─ □ ✕     │
         ├──────────────────────────────────────────┤
         │  INPUT                                    │
         │  [마이크 드롭다운 ▼] [48kHz] [128 smp]   │
         │  ■■■■■■■■■□□□□  -12dB                    │
         ├──────────────────────────────────────────┤
         │  VST CHAIN                                │
         │  ┌─────────────────────────────────────┐ │
         │  │ 1. ReaEQ          [Edit] [⏻] [✕]   │ │
         │  │ 2. RX Voice De-noise [Edit] [⏻] [✕]│ │
         │  │ 3. TDR Nova       [Edit] [⏻] [✕]   │ │
         │  │         [+ 플러그인 추가]            │ │
         │  └─────────────────────────────────────┘ │
         ├──────────────────────────────────────────┤
         │  OUTPUT                                   │
         │  🟢 OBS Plugin    ■■■■■■□□ Vol [====]    │
         │  🟡 Virtual Mic   ■■■■□□□□ Vol [====]    │
         │  🟢 Monitor       ■■■■■□□□ Vol [====]    │
         ├──────────────────────────────────────────┤
         │  Latency: 5.3ms │ CPU: 2.1% │ 48kHz/32b │
         └──────────────────────────────────────────┘

□ P4-2: DeviceSelector 컴포넌트
         - 시스템 오디오 입력 장치 열거
         - 선택 시 실시간 전환
         - 샘플레이트/버퍼 사이즈 설정
         - 장치 연결/분리 감지

□ P4-3: PluginChainEditor 컴포넌트
         - VST 플러그인 브라우저 (파일 선택)
         - 드래그앤드롭 순서 변경
         - 플러그인 GUI 팝업 (에디터 윈도우)
         - Bypass 토글
         - 프리셋 저장/로드

□ P4-4: LevelMeter 컴포넌트
         - 입력/각 출력별 실시간 레벨 미터
         - Peak hold 표시
         - 클리핑 표시

□ P4-5: 시스템 트레이 최소화
         - 트레이 아이콘
         - 우클릭 메뉴 (열기/종료)
         - 시작 시 트레이로 자동 시작 옵션

□ P4-6: 프리셋 관리
         - JSON 기반 프리셋 파일
         - 전체 설정 저장: 입력장치, VST체인, 볼륨, 버퍼 설정
         - 프리셋 내보내기/가져오기

산출물:
- 완전한 GUI를 갖춘 DirectPipe 앱
```

### Phase 5: 가상 마이크 통합 (Day 15~17)

```
목표: Discord/Zoom 등에서 사용 가능한 가상 마이크 출력

태스크:
□ P5-1: 가상 오디오 드라이버 전략 결정 및 번들
         옵션 A: VB-Cable 자동 감지 (이미 설치된 경우)
         옵션 B: 오픈소스 가상 오디오 드라이버 번들
         옵션 C: Windows Audio Session API (WASAPI) loopback 활용

□ P5-2: WASAPI 출력으로 가상 마이크에 오디오 전달
         - 가상 마이크를 WASAPI 출력 장치로 열기
         - VST 처리된 PCM을 출력
         - Exclusive 모드 시도 → 실패 시 Shared 모드 폴백

□ P5-3: 자동 설정 가이드
         - 가상 마이크 미설치 시 안내 다이얼로그
         - 원클릭 VB-Cable 설치 유도 (다운로드 링크)
         - Discord/Zoom에서 입력 장치 설정 가이드

산출물:
- Discord/Zoom에서 DirectPipe 오디오 사용 가능
```

### Phase 6: 안정화 및 최적화 (Day 18~21)

```
목표: 프로덕션 수준 안정성, CPU 최적화

태스크:
□ P6-1: 에러 핸들링 강화
         - 마이크 분리/재연결 시 graceful recovery
         - VST 플러그인 크래시 시 체인에서 자동 제거
         - OBS 플러그인 연결 끊김 시 자동 재연결
         - 공유 메모리 경합 상황 처리

□ P6-2: CPU 최적화
         - SIMD(SSE2/AVX) 활용한 버퍼 복사/믹싱
         - 불필요한 버퍼 복사 제거 (zero-copy where possible)
         - 스레드 우선순위: 오디오 콜백 → THREAD_PRIORITY_TIME_CRITICAL
         - 타이머 해상도: timeBeginPeriod(1)

□ P6-3: 레이턴시 벤치마크
         - 입력→OBS 도달 총 레이턴시 자동 측정
         - 다양한 버퍼 사이즈(64/128/256/512)별 비교
         - 다양한 USB 마이크별 호환성 테스트

□ P6-4: 메모리 누수 검사
         - VST 로드/언로드 반복 테스트
         - 장시간(8시간+) 연속 동작 테스트

□ P6-5: 설치 프로그램 제작
         - Inno Setup 기반
         - DirectPipe 앱 + OBS 플러그인 DLL 자동 배치
         - OBS 플러그인 경로 자동 감지
         - 시작 프로그램 등록 옵션

산출물:
- 안정적인 릴리즈 빌드
- 벤치마크 결과
- 설치 프로그램 (.exe)
```

---

## 5. CLAUDE.md (Claude Code 프로젝트 컨텍스트)

아래 내용을 프로젝트 루트의 `CLAUDE.md`에 배치:

```markdown
# DirectPipe — Claude Code 프로젝트 가이드

## 프로젝트 설명
USB 마이크용 VST 호스트 + 초저지연 루프백 시스템.
하드웨어 오디오 인터페이스 없이 VST 이펙트를 적용하고
OBS/Discord에 초저지연(3~12ms)으로 전달하는 올인원 앱.

## 기술 스택
- C++17, JUCE 7+, CMake 3.22+
- Windows WASAPI Exclusive Mode
- OBS Plugin SDK (C)
- SPSC Lock-free Ring Buffer + Windows Shared Memory

## 빌드 방법
```bash
# 전체 빌드
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 테스트
cd build && ctest --config Release
```

## 아키텍처 핵심
1. **오디오 스레드는 실시간** — malloc, lock, I/O 절대 금지
2. **IPC는 lock-free** — std::atomic + memory_order만 사용
3. **출력 3개 동시** — 공유메모리(OBS), WASAPI(가상마이크), WASAPI(모니터)

## 코딩 규칙
- 오디오 콜백 내: 힙 할당 금지, 뮤텍스 금지, 시스템 콜 최소화
- 모든 public API에 Doxygen 주석
- 에러 시 예외 대신 반환값/optional 사용
- VST 관련: juce::AudioProcessorGraph 기반
- IPC 관련: Windows API (CreateFileMapping, MapViewOfFile, CreateEvent)

## 주요 파일 역할
- `core/` — 공유 라이브러리 (ring buffer, shared memory)
- `host/` — JUCE 앱 (VST 호스트, 오디오 엔진, GUI)
- `obs-plugin/` — OBS 소스 플러그인 (DLL)

## 개발 순서
Phase 0 → 1 → 2 → 3 → 4 → 5 → 6
각 Phase는 이전 Phase에 의존하므로 순서대로 개발.
```

---

## 6. 핵심 코드 설계 스니펫

### 6.1 Ring Buffer (핵심 IPC)

```cpp
// core/include/directpipe/RingBuffer.h
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace directpipe {

class RingBuffer {
public:
    // 공유 메모리에 직접 배치되는 구조이므로 생성자 호출 불가
    // placement new 또는 init() 사용
    struct Header {
        alignas(64) std::atomic<uint64_t> write_pos{0};
        alignas(64) std::atomic<uint64_t> read_pos{0};
        uint32_t capacity_frames = 0;  // must be power of 2
        uint32_t channels = 0;
        uint32_t sample_rate = 0;
        uint32_t version = 1;
        std::atomic<bool> producer_active{false};
    };

    void init(uint32_t capacity_frames, uint32_t channels, uint32_t sample_rate);

    // Producer (JUCE app)
    uint32_t write(const float* data, uint32_t frames);

    // Consumer (OBS plugin)
    uint32_t read(float* data, uint32_t frames);

    uint32_t available_read() const;
    uint32_t available_write() const;

private:
    Header* header_ = nullptr;
    float*  data_   = nullptr;    // header 바로 뒤에 위치
    uint32_t mask_  = 0;          // capacity - 1 (power of 2 마스크)
};

} // namespace directpipe
```

### 6.2 Audio Engine 콜백 (실시간 처리 핵심)

```cpp
// host/Source/Audio/AudioEngine.cpp (핵심 콜백)
void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    // 1. 입력 데이터를 작업 버퍼에 복사
    juce::AudioBuffer<float> buffer(numInputChannels, numSamples);
    for (int ch = 0; ch < numInputChannels; ++ch)
        buffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);

    // 2. VST 체인 처리 (인라인, 추가 레이턴시 0)
    vstChain_.processBlock(buffer);

    // 3. 출력 분배 (3개 동시)
    outputRouter_.routeAudio(buffer);
    // 내부:
    //   a. sharedMemWriter_.write(buffer)  → OBS용
    //   b. WASAPI output → 가상 마이크
    //   c. monitor output → 헤드폰

    // 4. 모니터 출력 (이 콜백의 output에 직접 쓰기)
    if (monitorEnabled_) {
        for (int ch = 0; ch < numOutputChannels && ch < buffer.getNumChannels(); ++ch)
            std::memcpy(outputChannelData[ch],
                       buffer.getReadPointer(ch),
                       sizeof(float) * numSamples);
    } else {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            std::memset(outputChannelData[ch], 0, sizeof(float) * numSamples);
    }
}
```

### 6.3 OBS 플러그인 소스 (전체 구조)

```c
// obs-plugin/src/directpipe-source.c
#include <obs-module.h>
#include <windows.h>
#include "shm-reader.h"

struct directpipe_source {
    obs_source_t *source;
    shm_reader_t *reader;           // 공유 메모리 리더
    HANDLE        data_event;       // Named Event
    HANDLE        read_thread;
    volatile bool active;
    float         read_buf[8192];   // 최대 4096 frames × 2ch
};

static const char *dp_get_name(void *unused) {
    UNUSED_PARAMETER(unused);
    return "DirectPipe Audio";
}

static void *dp_create(obs_data_t *settings, obs_source_t *source) {
    struct directpipe_source *ctx = bzalloc(sizeof(*ctx));
    ctx->source = source;
    ctx->reader = shm_reader_create("Local\\DirectPipeAudio");
    ctx->data_event = OpenEventA(SYNCHRONIZE, FALSE,
                                  "Local\\DirectPipeDataReady");
    return ctx;
}

static DWORD WINAPI dp_read_thread(LPVOID param) {
    struct directpipe_source *ctx = param;
    while (ctx->active) {
        DWORD wait = WaitForSingleObject(ctx->data_event, 500);
        if (wait == WAIT_OBJECT_0 && ctx->active) {
            uint32_t frames = shm_reader_read(ctx->reader,
                                               ctx->read_buf, 128);
            if (frames > 0) {
                struct obs_source_audio audio = {
                    .data[0] = (uint8_t*)ctx->read_buf,
                    .frames = frames,
                    .speakers = SPEAKERS_STEREO,
                    .format = AUDIO_FORMAT_FLOAT_PLANAR,
                    .samples_per_sec = 48000,
                    .timestamp = os_gettime_ns(),
                };
                obs_source_output_audio(ctx->source, &audio);
            }
        }
    }
    return 0;
}

static void dp_activate(void *data) {
    struct directpipe_source *ctx = data;
    ctx->active = true;
    ctx->read_thread = CreateThread(NULL, 0, dp_read_thread, ctx, 0, NULL);
    SetThreadPriority(ctx->read_thread, THREAD_PRIORITY_ABOVE_NORMAL);
}

static void dp_deactivate(void *data) {
    struct directpipe_source *ctx = data;
    ctx->active = false;
    WaitForSingleObject(ctx->read_thread, 1000);
    CloseHandle(ctx->read_thread);
}

static void dp_destroy(void *data) {
    struct directpipe_source *ctx = data;
    shm_reader_destroy(ctx->reader);
    if (ctx->data_event) CloseHandle(ctx->data_event);
    bfree(ctx);
}

struct obs_source_info directpipe_source_info = {
    .id             = "directpipe_audio_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_AUDIO,
    .get_name       = dp_get_name,
    .create         = dp_create,
    .destroy        = dp_destroy,
    .activate       = dp_activate,
    .deactivate     = dp_deactivate,
};

OBS_DECLARE_MODULE()
bool obs_module_load(void) {
    obs_register_source(&directpipe_source_info);
    return true;
}
```

---

## 7. 리스크 및 대응 방안

| 리스크 | 영향 | 대응 |
|--------|------|------|
| USB 마이크가 WASAPI Exclusive 미지원 | 입력 레이턴시 증가 (10~20ms) | Shared 모드 폴백 + 경고 표시 |
| VST 플러그인 크래시 | 전체 앱 중단 | 플러그인을 try-catch 래핑, 크래시 시 체인에서 자동 제거 |
| OBS 버전 업데이트로 API 변경 | 플러그인 호환성 깨짐 | OBS 28+ 안정 API만 사용, CI에서 다중 버전 테스트 |
| 공유 메모리 이름 충돌 | IPC 실패 | GUID 기반 고유 이름 + 레지스트리 등록 |
| 가상 마이크 드라이버 없음 | Discord/Zoom 출력 불가 | VB-Cable 설치 안내 팝업, OBS 출력은 정상 동작 |
| Windows 보안 업데이트로 Exclusive 제한 | 입력 레이턴시 증가 | Shared 모드 자동 폴백 |

---

## 8. 성능 목표

| 지표 | 목표값 | 측정 방법 |
|------|--------|-----------|
| 총 레이턴시 (OBS) | ≤ 8ms @128 samples | loopback 측정 (톤 입력→OBS 녹화 비교) |
| 총 레이턴시 (가상 마이크) | ≤ 15ms | 동일 |
| CPU 사용률 | ≤ 3% (VST 0개 기준) | Task Manager |
| 메모리 사용 | ≤ 50MB (VST 0개 기준) | Task Manager |
| 글리치 없는 연속 동작 | 8시간+ | 장시간 테스트 |
| 앱 시작→오디오 출력 | ≤ 3초 | 스톱워치 |

---

## 9. 타임라인 요약

```
Week 1:  Phase 0 (환경) + Phase 1 (Core IPC) + Phase 2 시작 (오디오 엔진)
Week 2:  Phase 2 완료 + Phase 3 (OBS 플러그인)
Week 3:  Phase 4 (GUI)
Week 4:  Phase 5 (가상 마이크) + Phase 6 (안정화/최적화)
```

---

## 10. 개발 환경 요구사항

```
필수:
- Windows 10/11 (64-bit)
- Visual Studio 2022 (C++ 데스크톱 개발 워크로드)
- CMake 3.22+
- Git
- USB 마이크 1개 (테스트용)
- OBS Studio 28+ (플러그인 테스트용)

권장:
- 무료 VST 플러그인 (테스트용):
  - ReaPlugs (ReaEQ, ReaComp 등) — Cockos
  - TDR Nova — Tokyo Dawn Records
  - MeldaProduction MFreeFXBundle
```

---

*이 문서는 Claude Code로 개발 시 각 Phase별 태스크를 순서대로 진행하는 데 사용됩니다.*
*CLAUDE.md와 함께 프로젝트 루트에 배치하여 AI 어시스턴트가 프로젝트 맥락을 파악하도록 합니다.*
