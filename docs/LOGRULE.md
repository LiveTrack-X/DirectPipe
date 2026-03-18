# DirectPipe Logging Rules / 로깅 규칙

이 문서는 DirectPipe 로그 파일의 형식, 원칙, 규칙을 정의한다. 목표: **로그 파일만으로 사용자 환경과 문제를 재현할 수 있어야 한다.**

This document defines the format, principles, and rules for DirectPipe log files. Goal: **The log file alone must be sufficient to reproduce the user's environment and issues.**

---

## Log File Location / 로그 파일 위치

| Platform | File | Description |
|----------|------|-------------|
| Windows | `%AppData%/DirectPipe/directpipe.log` | Current session log / 현재 세션 로그 |
| Windows | `%AppData%/DirectPipe/directpipe.log.prev` | Previous session log / 이전 세션 로그 |
| macOS | `~/Library/Application Support/DirectPipe/directpipe.log` | Current session log |
| Linux | `~/.config/DirectPipe/directpipe.log` | Current session log |

Portable mode (all platforms): `./config/directpipe.log`

- 앱 시작 시 이전 로그를 `.prev`로 이동, 새 로그 시작 / On app start, move previous log to `.prev` and start a new log
- 매 로그 라인 즉시 flush — 크래시 시에도 마지막 로그까지 보존 / Each log line is flushed immediately — preserves logs up to the last line even on crash

---

## Log Format / 로그 형식

```
[HH:MM:SS.mmm] SEV [CATEGORY] message
```

- **Timestamp**: `DirectPipeLogger`가 자동 추가 (`[HH:MM:SS.mmm]`) / Auto-added by `DirectPipeLogger`
- **Severity** (`SEV`): 3글자, 메시지 앞에 포함 / 3-letter tag, included before message
- **Category**: 대문자 태그 (`[APP]`, `[AUDIO]`, etc.) / Uppercase tag
- **Message**: 사람이 읽을 수 있는 설명 / Human-readable description

### Example / 예시

```
[11:23:45.123] INF [APP] DirectPipe v4.0.1 started
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

Audit mode is enabled via the "Audit Mode" checkbox in the UI Log tab.
Default OFF. Switch ON only for diagnostics to collect verbose logs.

```cpp
Log::audit("AUDIO", "Available SR: 44100, 48000, 96000");  // audit OFF → no-op (atomic check only)
```

**Audit 로그 원칙 / Audit Log Principles:**
- 일반 모드에서 오버헤드 제로 (atomic bool 체크만) / Zero overhead in normal mode (atomic bool check only)
- 디바이스 상태, 플러그인 내부값, 서버 통신 상세 등 재현에 필요한 모든 정보 / All information needed for reproduction: device state, plugin internals, server communication details
- INF/WRN/ERR과 중복 가능 — INF는 "무엇이 발생했나", AUD는 "왜, 어떤 상태에서" / May overlap with INF/WRN/ERR — INF = "what happened", AUD = "why, in what state"

### Severity Guidelines / 심각도 가이드라인

**ERR — 즉시 조치 필요 / Immediate action required:**
- Plugin load failure / 플러그인 로드 실패
- Audio device init failure / 오디오 장치 초기화 실패
- Server start failure / 서버 시작 실패
- File I/O failure / 파일 읽기/쓰기 실패
- Shared memory failure / 공유 메모리 실패

**WRN — 주의 필요 / Attention needed:**
- Device disconnected (auto-reconnect will try) / 장치 연결 끊김
- Sample rate mismatch / 샘플레이트 불일치
- Partial preset load / 프리셋 부분 로드
- Hotkey registration failure / 단축키 등록 실패
- Plugin preload cancelled / 프리로드 취소
- Cache miss / 캐시 미스

**INF — 정상 동작 / Normal operation:**
- Session start/end / 세션 시작/종료
- Device started/stopped / 장치 시작/중지
- Preset loaded/saved / 프리셋 로드/저장
- Plugin added/removed / 플러그인 추가/제거
- Server started / 서버 시작
- State transitions / 상태 전환

**AUD — 상세 진단 (audit mode only) / Verbose diagnostics (audit mode only):**
- 디바이스 풀 capabilities (available SR/BS, channel names, latency) / Full device capabilities
- 사용 가능한 디바이스 목록 / Available device list
- 플러그인 내부 파라미터 전체 값 (name=value 쌍) / Full plugin parameter values (name=value pairs)
- 플러그인 체인 순서 + bypass + params count / Plugin chain order + bypass + params count
- 에디터 열기/닫기 + 열 때 파라미터 스냅샷 / Editor open/close + parameter snapshot on open
- WebSocket 메시지 payload, state broadcast 크기 / WebSocket message payload, state broadcast size
- HTTP request/response 상세 / HTTP request/response details
- 녹음 설정 (SR, ch, bits, FIFO) / Recording config (SR, ch, bits, FIFO)
- 재연결 시 available devices scan 결과 / Available devices scan results on reconnection
- 프리셋 캐시 상태 (hit/miss, SR/BS match, slot index) / Preset cache state (hit/miss, SR/BS match, slot index)
- IPC 상태 변경 상세 / IPC state change details
- 채널 라우팅 변경 / Channel routing changes

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

**Rule**: All logs must use the `Log::info/warn/error/audit` API. Direct `writeToLog` calls are prohibited.

---

## Audit Logging Rules per Subsystem / 서브시스템별 Audit 규칙

### [AUDIO] AudioEngine

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 초기화 성공 / Init success | 드라이버, 장치, SR, BS / Driver, device, SR, BS | **전체** available device types, 각 type별 input/output 장치 목록 / **All** available device types, input/output device list per type |
| 디바이스 시작 / Device start | 장치명, type, SR, BS, available BS / Device name, type, SR, BS, available BS | **전체** available SR, available BS, input/output 채널명, active 채널 비트, input/output latency, desired device names / **All** available SR, available BS, input/output channel names, active channel bits, input/output latency, desired device names |
| 드라이버 전환 / Driver switch | 전환 결과, 새 type, SR, BS / Switch result, new type, SR, BS | 전환 전후 디바이스 capabilities (available SR/BS, 채널명) / Pre/post device capabilities (available SR/BS, channel names) |
| 디바이스 변경 / Device change | 새 장치명 / New device name | 변경 전 setup (SR, BS), 사용 가능 장치 목록 / Previous setup (SR, BS), available device list |
| 재연결 시도 / Reconnection attempt | 대기 중 / 성공 / 실패 / Waiting / success / failure | scan 결과 (available inputs/outputs), desired device names, setup 파라미터 / Scan results (available inputs/outputs), desired device names, setup params |
| 채널 변경 / Channel change | 결과 / Result | first channel, num channels |
| 버퍼 변경 / Buffer change | 요청 → 실제 / Requested → actual | available buffer sizes 전체 / All available buffer sizes |
| SR 변경 / SR change | 결과 / Result | 요청값 / Requested value |
| 디바이스 중지 / Device stop | "Device stopped" | IPC 상태 (ipcWasEnabled) / IPC state (ipcWasEnabled) |
| 에러 / Error | 에러 메시지 / Error message | — |

### [VST] VSTChain

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 플러그인 추가 / Plugin add | name, format, index, I/O count | **체인 순서 / Chain order**: `[0] ReaComp(bypass=N, params=24) -> [1] ReaEQ(bypass=N, params=16)` |
| 플러그인 제거 / Plugin remove | name, index, 전/후 count / before/after count | 체인 순서 / Chain order |
| 플러그인 이동 / Plugin move | name, from→to | 체인 순서 / Chain order |
| 에디터 열기 / Editor open | name, index | **전체 파라미터 값 / All parameter values**: `name1=0.5000, name2=0.7500, ...` |
| 에디터 닫기 / Editor close | — | name, index |
| 체인 교체 (async) / Chain swap (async) | plugin count | 체인 순서 + 각 플러그인 전체 파라미터 값 / Chain order + all plugin parameter values |
| 체인 교체 (cached) / Chain swap (cached) | plugin count, 소요시간 / duration | 체인 순서 + 각 플러그인 전체 파라미터 값 / Chain order + all plugin parameter values |

### [MONITOR] MonitorOutput

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 초기화 / Init | 장치명, SR, BS / Device name, SR, BS | ring buffer 설정 (frames, channels) / Ring buffer config (frames, channels) |
| 활성화 / Activate | 장치명, 실제 SR/BS / Device name, actual SR/BS | device type, output channels, latency, available BS |
| 장치 분실 / Device lost | 장치명 / Device name | — |
| 에러 / Error | 에러 메시지, 장치명 / Error message, device name | — |
| 재연결 / Reconnect | 성공/실패 / Success/failure | **available devices 전체 목록 / Full available devices list** |

### [WS] WebSocketServer

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 서버 시작 / Server start | 포트 / Port | 시도한 포트 범위 / Attempted port range |
| 클라이언트 연결 / Client connect | 총 클라이언트 수 / Total client count | 초기 state JSON 크기 / Initial state JSON size |
| 핸드셰이크 실패 / Handshake failure | — | — |
| 명령 수신 / Command received | action 이름 / Action name | **전체 메시지 payload / Full message payload** |
| 클라이언트 해제 / Client disconnect | 남은 클라이언트 수 / Remaining client count | — |

### [HTTP] HttpApiServer

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 서버 시작 / Server start | 포트 / Port | 시도한 포트 범위 / Attempted port range |
| 요청 처리 / Request handling | method, path, status code | **response body** (최대 200자 / max 200 chars) |

### [REC] AudioRecorder

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 녹음 시작 / Recording start | 파일 경로 / File path | SR, channels, bits, FIFO 크기 / FIFO size |
| 녹음 중지 / Recording stop | 파일 경로, 초 / File path, seconds | duration, file size, total samples |

### [PRESET] PresetManager

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 슬롯 로드 시작 / Slot load start | slot index | cache check 파라미터 (SR, BS) / Cache check params (SR, BS) |
| 캐시 히트 / Cache hit | slot, plugin count | 캐시된 플러그인 목록 / Cached plugin list |
| 캐시 미스 / Cache miss | slot | — |
| 프리로드 / Preload | slot count | 프리로드 대상 슬롯 목록 / Target slot list for preload |

### [IPC] SharedMemWriter

| 이벤트 / Event | INF/WRN/ERR | AUD (audit mode) |
|--------|-------------|-------------------|
| 활성화 / Activate | — | SR |
| 비활성화 / Deactivate | — | — |
| 재초기화 실패 / Re-init failure | — | SR |

---

## Required Log Points / 필수 로그 지점

### 1. Session Header (앱 시작 시 / On App Start)

앱 시작 직후 아래 정보를 순서대로 기록:

Log the following information in order immediately after app startup:

```
INF [APP] DirectPipe v4.0.1 started
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

**Purpose**: 이 헤더만으로 사용자 환경을 완전히 파악 가능. / This header alone fully identifies the user's environment.

### 2. State Changes / 상태 변경

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

### 3. Errors / 에러

에러 로그는 반드시 **컨텍스트**를 포함해야 한다:

Error logs must always include **context**:

```
BAD:  ERR [AUDIO] Setup error: -1
GOOD: ERR [AUDIO] Failed to set output device 'Speakers (Realtek)' with SR=48000 BS=256: error code -1
```

**Rule**: 에러 메시지에 반드시 포함할 것 / Error messages must include:
- **What**: 무엇을 하려 했는지 (동사 + 대상) / What was attempted (verb + target)
- **With**: 어떤 파라미터로 (장치명, SR, BS 등) / With what parameters (device name, SR, BS, etc.)
- **Result**: 결과/에러코드 / Result/error code

### 4. Performance Timing / 성능 타이밍

성능에 영향을 주는 작업은 소요 시간을 기록:

Log elapsed time for performance-sensitive operations:

```
INF [PRESET] Slot B: cache hit (3 plugins)
INF [VST] Cached chain swap: 3 plugins (15ms)
INF [PRESET] Slot C: full reload (4 plugins)
INF [VST] Async chain load complete: 4 plugins (342ms)
INF [VST] Preload complete: 3 slots cached (1250ms)
```

**Timing 필수 대상 / Timing required for:**
- Preset switch (모든 경로 / all paths: fast/cache/async)
- Plugin instance creation
- Audio device switch
- Preload completion

### 5. Session End / 세션 종료

```
INF [APP] DirectPipe shutting down
INF [APP] Session duration: 2h 15m 30s
```

크래시 시 이 메시지가 없으면 = 비정상 종료로 판단.

If this message is missing on crash = abnormal termination.

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
Log::sessionStart("4.0.1");
Log::audioConfig(driverType, inputDevice, outputDevice, sr, bs);
```

### Migration Guide / 마이그레이션 가이드

기존 / Before:
```cpp
juce::Logger::writeToLog("[AUDIO] Device started: " + name);
juce::Logger::writeToLog("[AUDIO] Setup error: " + result);
```

변경 / After:
```cpp
Log::info("AUDIO", "Device started: " + name);
Log::error("AUDIO", "Failed to init device '" + name + "': " + result);
```

**Rule**: 새 코드는 반드시 `Log::` API 사용. `juce::Logger::writeToLog` 직접 호출 금지.

**Rule**: New code must use the `Log::` API. Direct `juce::Logger::writeToLog` calls are prohibited.

---

## Rules Summary / 규칙 요약

1. **`Log::` API 사용 필수** — `juce::Logger::writeToLog` 직접 호출 금지 / **`Log::` API required** — direct `writeToLog` calls prohibited
2. **모든 로그에 카테고리 태그 필수** — `[APP]`, `[AUDIO]`, etc. / **Category tag required on all logs**
3. **모든 로그에 심각도 필수** — `ERR`, `WRN`, `INF`, `AUD` / **Severity required on all logs**
4. **세션 헤더 필수** — 앱 시작 시 환경 정보 전부 기록 / **Session header required** — log all environment info at startup
5. **에러에 컨텍스트 필수** — What + With + Result / **Context required in errors** — What + With + Result
6. **성능 작업에 타이밍 필수** — ms 단위 소요 시간 / **Timing required for performance ops** — duration in ms
7. **세션 종료 기록 필수** — 없으면 크래시로 판단 / **Session end log required** — absence = crash
8. **파일 즉시 flush** — 크래시 직전 로그까지 보존 / **Immediate file flush** — preserves logs up to crash
9. **이전 세션 보관** — `.prev` 파일로 1세션분 보관 / **Previous session retained** — 1 session in `.prev` file
10. **Rate limiting** — 고빈도 작업(SetVolume, InputGainAdjust, SetPluginParameter)과 Continuous MIDI 바인딩은 로깅 제외 / **Rate limiting** — high-frequency actions and continuous MIDI bindings excluded from logging
11. **Lock ordering** — `writeMutex_` 안에서 다른 mutex 획득 금지. `chainLock_` 안에서 `writeToLog` 호출 금지 / **Lock ordering** — no acquiring other mutexes inside `writeMutex_`; no `writeToLog` inside `chainLock_`
12. **Audit는 진단 전용** — 기본 OFF, 문제 재현 시에만 ON / **Audit is diagnostic only** — default OFF, enable only for issue reproduction
13. **Audit 값 캡처 규칙** — lock 안에서 문자열 캡처, lock 해제 후 `Log::audit()` 호출 / **Audit value capture rule** — capture strings inside lock, call `Log::audit()` after releasing lock
14. **비싼 문자열 생성 보호** — `if (Log::isAuditMode())` 가드로 감싸서 OFF 시 문자열 생성 비용 제거 / **Expensive string guard** — wrap with `if (Log::isAuditMode())` to eliminate string construction cost when OFF

---

## Audit Mode Example Output / Audit 모드 출력 예시

Audit ON 상태에서 디바이스 시작:

Device start with Audit ON:
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

Plugin chain swap with Audit ON:
```
[11:24:00.100] INF [VST] Cached chain swap: 3 plugins (12ms)
[11:24:00.101] AUD [VST] [0] ReaComp(bypass=N, params=24) -> [1] ReaEQ(bypass=N, params=16) -> [2] ReaXcomp(bypass=Y, params=18)
[11:24:00.101] AUD [VST]   [0] ReaComp: Thresh=-18.0000, Ratio=4.0000, Attack=3.0000, Release=100.0000, ...
[11:24:00.101] AUD [VST]   [1] ReaEQ: Band1Freq=100.0000, Band1Gain=3.5000, Band1Q=1.0000, ...
[11:24:00.101] AUD [VST]   [2] ReaXcomp: Thresh1=-24.0000, Ratio1=2.0000, ...
```

Audit ON 상태에서 재연결:

Reconnection with Audit ON:
```
[11:25:10.000] INF [AUDIO] Reconnection: waiting for devices (input: Microphone (USB))
[11:25:10.001] AUD [AUDIO] Reconnection scan: inputs=[마이크 (Realtek)] outputs=[Speakers (Realtek)]
[11:25:10.001] AUD [AUDIO] Reconnection desired: in='Microphone (USB)' out='Speakers (Realtek)'
```

---

## Crash Diagnosis Checklist / 크래시 진단 체크리스트

로그 파일을 받았을 때 확인 순서:

Checklist when reviewing a log file:

1. **세션 종료 메시지 있는가?** — 없으면 크래시 / **Session end message present?** — if missing, it's a crash
2. **마지막 로그 라인은?** — 크래시 직전 동작 파악 / **Last log line?** — identify the action just before crash
3. **세션 헤더 확인** — OS, 드라이버, 장치, SR/BS, 플러그인 목록 / **Check session header** — OS, driver, device, SR/BS, plugin list
4. **ERR/WRN 검색** — 문제 발생 지점 빠르게 찾기 / **Search ERR/WRN** — quickly locate problem points
5. **타이밍 이상 확인** — 비정상적으로 긴 작업 (>1000ms) / **Check timing anomalies** — abnormally long operations (>1000ms)
6. **장치 상태 변화** — 연결 끊김/재연결 패턴 / **Device state changes** — disconnect/reconnect patterns
7. **프리셋 전환 패턴** — cache hit/miss, 소요 시간 / **Preset switch patterns** — cache hit/miss, duration
8. **Audit 로그 요청** — 재현 불가 시 사용자에게 Audit Mode ON 후 재현 요청 / **Request audit logs** — if not reproducible, ask user to enable Audit Mode and reproduce
