# DirectPipe Logging Rules / 로깅 규칙

이 문서는 DirectPipe 로그 파일의 형식, 원칙, 규칙을 정의한다. 목표: **로그 파일만으로 사용자 환경과 문제를 재현할 수 있어야 한다.**

---

## Log File Location / 로그 파일 위치

| Platform | File | Description |
|----------|------|-------------|
| Windows | `%LocalAppData%/DirectPipe/directpipe.log` | Current session log / 현재 세션 로그 |
| Windows | `%LocalAppData%/DirectPipe/directpipe.log.prev` | Previous session log / 이전 세션 로그 |
| macOS | `~/Library/Application Support/DirectPipe/directpipe.log` | Current session log |
| Linux | `~/.local/share/DirectPipe/directpipe.log` | Current session log |

Portable mode (all platforms): `./config/directpipe.log`

- 앱 시작 시 이전 로그를 `.prev`로 이동, 새 로그 시작
- 매 로그 라인 즉시 flush — 크래시 시에도 마지막 로그까지 보존

---

## Log Format / 로그 형식

```
[HH:MM:SS.mmm] SEV [CATEGORY] message
```

- **Timestamp**: `DirectPipeLogger`가 자동 추가 (`[HH:MM:SS.mmm]`)
- **Severity** (`SEV`): 3글자, 메시지 앞에 포함
- **Category**: 대문자 태그 (`[APP]`, `[AUDIO]`, etc.)
- **Message**: 사람이 읽을 수 있는 설명

### Example / 예시

```
[11:23:45.123] INF [APP] DirectPipe v4.0.0 started
[11:23:45.124] INF [APP] OS: Windows 11 Pro 10.0.26200          (Windows)
                                 macOS 15.2 (Sequoia)            (macOS)
                                 Ubuntu 24.04 LTS (6.8.0-45)     (Linux)
[11:23:45.200] INF [AUDIO] Driver: Windows Audio | Input: Microphone (USB) | Output: Speakers | SR: 48000 | BS: 256       (Windows)
                   Driver: CoreAudio | Input: MacBook Pro Microphone | Output: MacBook Pro Speakers | SR: 48000 | BS: 256  (macOS)
                   Driver: ALSA | Input: USB Audio Device | Output: default | SR: 48000 | BS: 256                          (Linux)
[11:23:45.210] INF [VST] Chain: ReaComp, ReaEQ, ReaXcomp (3 plugins)
[11:23:45.220] INF [PRESET] Active slot: E
[11:23:50.100] INF [PRESET] Slot A: cache hit (3 plugins)
[11:23:50.115] INF [VST] Cached chain swap: 3 plugins (15ms)
[11:24:01.300] WRN [AUDIO] Device lost: Microphone (USB)
[11:24:04.500] INF [AUDIO] Device reconnected: Microphone (USB)
[11:24:10.000] ERR [VST] Failed to load: BadPlugin - DLL init crashed
```

---

## Severity Levels / 심각도 레벨

| Level | Tag | When to use / 사용 시점 |
|-------|-----|------------------------|
| **Error** | `ERR` | Operation failed, feature unavailable / 작업 실패, 기능 사용 불가 |
| **Warning** | `WRN` | Recoverable problem, degraded state / 복구 가능한 문제, 품질 저하 |
| **Info** | `INF` | Normal operation, state changes / 정상 동작, 상태 변경 |
| **Audit** | `AUD` | Verbose internal details (audit mode only) / 상세 내부 정보 (감사 모드 전용) |

### Audit Mode / 감사 모드

Audit 모드는 UI의 Log 탭에서 "Audit Mode" 체크박스로 활성화한다.
기본 OFF. 문제 진단 시에만 ON으로 전환하여 상세 로그를 수집한다.

```cpp
Log::audit("AUDIO", "Available SR: 44100, 48000, 96000");  // audit OFF → no-op (atomic check only)
```

**Audit 로그 원칙:**
- 일반 모드에서 오버헤드 제로 (atomic bool 체크만)
- 디바이스 상태, 플러그인 내부값, 서버 통신 상세 등 재현에 필요한 모든 정보
- INF/WRN/ERR과 중복 가능 — INF는 "무엇이 발생했나", AUD는 "왜, 어떤 상태에서"

### Severity Guidelines / 심각도 가이드라인

**ERR — 즉시 조치 필요:**
- Plugin load failure / 플러그인 로드 실패
- Audio device init failure / 오디오 장치 초기화 실패
- Server start failure / 서버 시작 실패
- File I/O failure / 파일 읽기/쓰기 실패
- Shared memory failure / 공유 메모리 실패

**WRN — 주의 필요:**
- Device disconnected (auto-reconnect will try) / 장치 연결 끊김
- Sample rate mismatch / 샘플레이트 불일치
- Partial preset load / 프리셋 부분 로드
- Hotkey registration failure / 단축키 등록 실패
- Plugin preload cancelled / 프리로드 취소
- Cache miss / 캐시 미스

**INF — 정상 동작:**
- Session start/end / 세션 시작/종료
- Device started/stopped / 장치 시작/중지
- Preset loaded/saved / 프리셋 로드/저장
- Plugin added/removed / 플러그인 추가/제거
- Server started / 서버 시작
- State transitions / 상태 전환

**AUD — 상세 진단 (audit mode only):**
- 디바이스 풀 capabilities (available SR/BS, channel names, latency)
- 사용 가능한 디바이스 목록
- 플러그인 내부 파라미터 전체 값 (name=value 쌍)
- 플러그인 체인 순서 + bypass + params count
- 에디터 열기/닫기 + 열 때 파라미터 스냅샷
- WebSocket 메시지 payload, state broadcast 크기
- HTTP request/response 상세
- 녹음 설정 (SR, ch, bits, FIFO)
- 재연결 시 available devices scan 결과
- 프리셋 캐시 상태 (hit/miss, SR/BS match, slot index)
- IPC 상태 변경 상세
- 채널 라우팅 변경

---

## Categories / 카테고리

| Tag | Subsystem | Description |
|-----|-----------|-------------|
| `[APP]` | Application | Startup, shutdown, version, OS info, update check |
| `[AUDIO]` | AudioEngine | Driver, device, SR, BS, reconnection, XRun |
| `[VST]` | VSTChain | Plugin load/unload, bypass, chain swap, preload cache |
| `[PRESET]` | PresetManager | Slot save/load, fast path, cache hit/miss |
| `[MONITOR]` | MonitorOutput | Monitor device, ring buffer, reconnection |
| `[IPC]` | SharedMemWriter | Shared memory, Receiver VST communication |
| `[REC]` | AudioRecorder | Recording start/stop, file path |
| `[WS]` | WebSocketServer | WebSocket server, client connect/disconnect |
| `[HTTP]` | HttpApiServer | HTTP server, request handling |
| `[ACTION]` | ActionDispatcher | Action dispatch, enum-to-string conversion |
| `[HOTKEY]` | HotkeyHandler | Hotkey registration, trigger |
| `[MIDI]` | MidiHandler | MIDI device, learn, CC/Note trigger |
| `[CONTROL]` | ControlManager | Control subsystem init/shutdown |

**Rule**: 모든 로그는 `Log::info/warn/error/audit` API를 사용한다. 직접 `writeToLog` 호출 금지.

---

## Audit Logging Rules per Subsystem / 서브시스템별 Audit 규칙

### [AUDIO] AudioEngine

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 초기화 성공 | 드라이버, 장치, SR, BS | **전체** available device types, 각 type별 input/output 장치 목록 |
| 디바이스 시작 | 장치명, type, SR, BS, available BS | **전체** available SR, available BS, input/output 채널명, active 채널 비트, input/output latency, desired device names |
| 드라이버 전환 | 전환 결과, 새 type, SR, BS | 전환 전후 디바이스 capabilities (available SR/BS, 채널명) |
| 디바이스 변경 | 새 장치명 | 변경 전 setup (SR, BS), 사용 가능 장치 목록 |
| 재연결 시도 | 대기 중 / 성공 / 실패 | scan 결과 (available inputs/outputs), desired device names, setup 파라미터 |
| 채널 변경 | 결과 | first channel, num channels |
| 버퍼 변경 | 요청 → 실제 | available buffer sizes 전체 |
| SR 변경 | 결과 | 요청값 |
| 디바이스 중지 | "Device stopped" | IPC 상태 (ipcWasEnabled) |
| 에러 | 에러 메시지 | — |

### [VST] VSTChain

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 플러그인 추가 | name, format, index, I/O count | **체인 순서**: `[0] ReaComp(bypass=N, params=24) -> [1] ReaEQ(bypass=N, params=16)` |
| 플러그인 제거 | name, index, 전/후 count | 체인 순서 |
| 플러그인 이동 | name, from→to | 체인 순서 |
| 에디터 열기 | name, index | **전체 파라미터 값**: `name1=0.5000, name2=0.7500, ...` |
| 에디터 닫기 | — | name, index |
| 체인 교체 (async) | plugin count | 체인 순서 + 각 플러그인 전체 파라미터 값 |
| 체인 교체 (cached) | plugin count, 소요시간 | 체인 순서 + 각 플러그인 전체 파라미터 값 |

### [MONITOR] MonitorOutput

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 초기화 | 장치명, SR, BS | ring buffer 설정 (frames, channels) |
| 활성화 | 장치명, 실제 SR/BS | device type, output channels, latency, available BS |
| 장치 분실 | 장치명 | — |
| 에러 | 에러 메시지, 장치명 | — |
| 재연결 | 성공/실패 | **available devices 전체 목록** |

### [WS] WebSocketServer

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 서버 시작 | 포트 | 시도한 포트 범위 |
| 클라이언트 연결 | 총 클라이언트 수 | 초기 state JSON 크기 |
| 핸드셰이크 실패 | — | — |
| 명령 수신 | action 이름 | **전체 메시지 payload** |
| 클라이언트 해제 | 남은 클라이언트 수 | — |

### [HTTP] HttpApiServer

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 서버 시작 | 포트 | 시도한 포트 범위 |
| 요청 처리 | method, path, status code | **response body** (최대 200자) |

### [REC] AudioRecorder

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 녹음 시작 | 파일 경로 | SR, channels, bits, FIFO 크기 |
| 녹음 중지 | 파일 경로, 초 | duration, file size, total samples |

### [PRESET] PresetManager

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 슬롯 로드 시작 | slot index | cache check 파라미터 (SR, BS) |
| 캐시 히트 | slot, plugin count | 캐시된 플러그인 목록 |
| 캐시 미스 | slot | — |
| 프리로드 | slot count | 프리로드 대상 슬롯 목록 |

### [IPC] SharedMemWriter

| 이벤트 | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 활성화 | — | SR |
| 비활성화 | — | — |
| 재초기화 실패 | — | SR |

---

## Required Log Points / 필수 로그 지점

### 1. Session Header (앱 시작 시)

앱 시작 직후 아래 정보를 순서대로 기록:

```
INF [APP] DirectPipe v4.0.0 started
INF [APP] OS: Windows 11 Pro 10.0.26200       (or macOS 15.2 Sequoia / Ubuntu 24.04 LTS)
INF [APP] Process priority: HIGH_PRIORITY_CLASS  (Windows; macOS/Linux use equivalent scheduling)
INF [APP] Timer resolution: 1ms                  (Windows; macOS/Linux use platform timers)
INF [AUDIO] Driver: Windows Audio | Input: Microphone (USB) | Output: Speakers (Realtek) | SR: 48000 | BS: 256
            (macOS: CoreAudio | Linux: ALSA)
INF [MONITOR] Device: Headphones (Realtek) | SR: 48000 | BS: 256
INF [VST] Chain: ReaComp, ReaEQ, ReaXcomp (3 plugins)
INF [PRESET] Active slot: E | Preset: Streaming Vocal
INF [WS] Server started on port 8765
INF [HTTP] Server started on port 8766
INF [CONTROL] Hotkeys: 12 registered | MIDI: USB MIDI (3 bindings)
```

**Purpose**: 이 헤더만으로 사용자 환경을 완전히 파악 가능.

### 2. State Changes (상태 변경)

| Event | Required Info |
|-------|---------------|
| Device change | Old → New device name, driver type |
| Sample rate change | Old → New SR |
| Buffer size change | Old → New BS |
| Preset switch | Slot label, path (fast/cache/async), plugin count, duration ms |
| Plugin add/remove | Plugin name, chain position |
| Plugin bypass toggle | Plugin name, new state |
| Mute toggle | Target (input/output/monitor), new state |
| Recording start/stop | File path (start), duration (stop) |
| IPC toggle | New state |

### 3. Errors (에러)

에러 로그는 반드시 **컨텍스트**를 포함해야 한다:

```
BAD:  ERR [AUDIO] Setup error: -1
GOOD: ERR [AUDIO] Failed to set output device 'Speakers (Realtek)' with SR=48000 BS=256: error code -1
```

**Rule**: 에러 메시지에 반드시 포함할 것:
- **What**: 무엇을 하려 했는지 (동사 + 대상)
- **With**: 어떤 파라미터로 (장치명, SR, BS 등)
- **Result**: 결과/에러코드

### 4. Performance Timing (성능 타이밍)

성능에 영향을 주는 작업은 소요 시간을 기록:

```
INF [PRESET] Slot B: cache hit (3 plugins)
INF [VST] Cached chain swap: 3 plugins (15ms)
INF [PRESET] Slot C: full reload (4 plugins)
INF [VST] Async chain load complete: 4 plugins (342ms)
INF [VST] Preload complete: 3 slots cached (1250ms)
```

**Timing 필수 대상:**
- Preset switch (모든 경로: fast/cache/async)
- Plugin instance creation
- Audio device switch
- Preload completion

### 5. Session End (세션 종료)

```
INF [APP] DirectPipe shutting down
INF [APP] Session duration: 2h 15m 30s
```

크래시 시 이 메시지가 없으면 = 비정상 종료로 판단.

---

## Logging API / 로깅 API

### Helper Functions (`Log.h`)

```cpp
#include "Log.h"

// Severity-tagged logging
Log::info("AUDIO", "Device started: " + deviceName);
Log::warn("AUDIO", "Device lost: " + deviceName);
Log::error("AUDIO", "Failed to set output device '" + name + "': " + error);

// Audit mode — zero overhead when OFF (atomic check only)
Log::audit("AUDIO", "Available SR: " + srList);
Log::audit("VST", "Editor params [0] \"ReaComp\": Thresh=-18.0, Ratio=4.0, ...");

// Conditional audit (avoid expensive string building when OFF)
if (Log::isAuditMode()) {
    juce::String info = buildExpensiveDebugString();
    Log::audit("VST", info);
}

// Toggle audit mode (UI checkbox callback)
Log::setAuditMode(true);   // enable verbose logging
Log::setAuditMode(false);  // disable (default)

// Scoped timer (logs duration on destruction)
{
    Log::Timer timer("VST", "Cached chain swap: " + String(count) + " plugins");
    // ... operation ...
}  // → "INF [VST] Cached chain swap: 3 plugins (15ms)"

// Session header (call once at startup)
Log::sessionStart("4.0.0");
Log::audioConfig(driverType, inputDevice, outputDevice, sr, bs);
```

### Migration Guide / 마이그레이션 가이드

기존:
```cpp
juce::Logger::writeToLog("[AUDIO] Device started: " + name);
juce::Logger::writeToLog("[AUDIO] Setup error: " + result);
```

변경:
```cpp
Log::info("AUDIO", "Device started: " + name);
Log::error("AUDIO", "Failed to init device '" + name + "': " + result);
```

**Rule**: 새 코드는 반드시 `Log::` API 사용. `juce::Logger::writeToLog` 직접 호출 금지.

---

## Rules Summary / 규칙 요약

1. **`Log::` API 사용 필수** — `juce::Logger::writeToLog` 직접 호출 금지
2. **모든 로그에 카테고리 태그 필수** — `[APP]`, `[AUDIO]`, etc.
3. **모든 로그에 심각도 필수** — `ERR`, `WRN`, `INF`, `AUD`
4. **세션 헤더 필수** — 앱 시작 시 환경 정보 전부 기록
5. **에러에 컨텍스트 필수** — What + With + Result
6. **성능 작업에 타이밍 필수** — ms 단위 소요 시간
7. **세션 종료 기록 필수** — 없으면 크래시로 판단
8. **파일 즉시 flush** — 크래시 직전 로그까지 보존
9. **이전 세션 보관** — `.prev` 파일로 1세션분 보관
10. **Rate limiting** — 고빈도 작업(SetVolume, InputGainAdjust, SetPluginParameter)과 Continuous MIDI 바인딩은 로깅 제외
11. **Lock ordering** — `writeMutex_` 안에서 다른 mutex 획득 금지. `chainLock_` 안에서 `writeToLog` 호출 금지
12. **Audit는 진단 전용** — 기본 OFF, 문제 재현 시에만 ON
13. **Audit 값 캡처 규칙** — lock 안에서 문자열 캡처, lock 해제 후 `Log::audit()` 호출
14. **비싼 문자열 생성 보호** — `if (Log::isAuditMode())` 가드로 감싸서 OFF 시 문자열 생성 비용 제거

---

## Audit Mode Example Output / Audit 모드 출력 예시

Audit ON 상태에서 디바이스 시작:
```
[11:23:45.200] INF [AUDIO] Device started: Windows Audio | SR=48000 | BS=256 | Available BS: [64, 128, 256, 512, 1024]
                           (macOS: CoreAudio | Linux: ALSA)
[11:23:45.201] AUD [AUDIO] Device name: Speakers (Realtek High Definition Audio)
[11:23:45.201] AUD [AUDIO] Device type: Windows Audio  (or CoreAudio / ALSA)
[11:23:45.201] AUD [AUDIO] Available SR: 44100, 48000, 96000, 192000
[11:23:45.201] AUD [AUDIO] Available BS: 64, 128, 256, 512, 1024
[11:23:45.201] AUD [AUDIO] Input channels: Left, Right
[11:23:45.201] AUD [AUDIO] Output channels: Left, Right
[11:23:45.201] AUD [AUDIO] Active input bits: 11
[11:23:45.201] AUD [AUDIO] Active output bits: 11
[11:23:45.201] AUD [AUDIO] Input latency: 480 samples
[11:23:45.201] AUD [AUDIO] Output latency: 480 samples
[11:23:45.201] AUD [AUDIO] Desired devices: in='Microphone (USB)' out='Speakers (Realtek)'
```

Audit ON 상태에서 플러그인 체인 교체:
```
[11:24:00.100] INF [VST] Cached chain swap: 3 plugins (12ms)
[11:24:00.101] AUD [VST] [0] ReaComp(bypass=N, params=24) -> [1] ReaEQ(bypass=N, params=16) -> [2] ReaXcomp(bypass=Y, params=18)
[11:24:00.101] AUD [VST]   [0] ReaComp: Thresh=-18.0000, Ratio=4.0000, Attack=3.0000, Release=100.0000, ...
[11:24:00.101] AUD [VST]   [1] ReaEQ: Band1Freq=100.0000, Band1Gain=3.5000, Band1Q=1.0000, ...
[11:24:00.101] AUD [VST]   [2] ReaXcomp: Thresh1=-24.0000, Ratio1=2.0000, ...
```

Audit ON 상태에서 재연결:
```
[11:25:10.000] INF [AUDIO] Reconnection: waiting for devices (input: Microphone (USB))
[11:25:10.001] AUD [AUDIO] Reconnection scan: inputs=[마이크 (Realtek)] outputs=[Speakers (Realtek)]
[11:25:10.001] AUD [AUDIO] Reconnection desired: in='Microphone (USB)' out='Speakers (Realtek)'
```

---

## Crash Diagnosis Checklist / 크래시 진단 체크리스트

로그 파일을 받았을 때 확인 순서:

1. **세션 종료 메시지 있는가?** — 없으면 크래시
2. **마지막 로그 라인은?** — 크래시 직전 동작 파악
3. **세션 헤더 확인** — OS, 드라이버, 장치, SR/BS, 플러그인 목록
4. **ERR/WRN 검색** — 문제 발생 지점 빠르게 찾기
5. **타이밍 이상 확인** — 비정상적으로 긴 작업 (>1000ms)
6. **장치 상태 변화** — 연결 끊김/재연결 패턴
7. **프리셋 전환 패턴** — cache hit/miss, 소요 시간
8. **Audit 로그 요청** — 재현 불가 시 사용자에게 Audit Mode ON 후 재현 요청
