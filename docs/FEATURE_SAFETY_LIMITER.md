# Feature Spec: Safety Limiter (안전 리미터)

> **DirectPipe v4.1.0 구현 명세** — Claude Code 핸드오프용
> 
> 이 문서는 DirectPipe에 "Safety Limiter" 기능을 추가하기 위한 설계 명세입니다.
> 기존 코드베이스의 아키텍처(`CLAUDE.md` Key Implementations 섹션)와 스레드 안전 규칙을 준수해야 합니다.

---

## 1. 목적 / Purpose

VST 플러그인 체인 이후, 모든 출력 경로(Main/Monitor/IPC/Recording) 앞에 위치하는 **글로벌 안전 리미터**.

- 슬롯 전환, 플러그인 파라미터 변경 등으로 예상치 못한 클리핑이 발생할 때 **최후의 안전장치** 역할
- 슬롯(A-E) 전환과 무관하게 **항상 동작** (글로벌 설정)
- on/off 토글 가능

### 핵심 원칙

1. **RT-safe** — 오디오 콜백에서 힙 할당, 뮤텍스, I/O 절대 없음
2. **최소 복잡도** — ceiling 파라미터 하나. 사용자가 만질 게 거의 없어야 함
3. **투명한 동작** — 리미팅이 걸리고 있을 때만 시각적 피드백. 안 걸리면 존재감 없음
4. **기존 아키텍처 비침습** — AudioProcessorGraph 밖에서 독립 처리. VSTChain 코드 변경 최소화

---

## 2. 신호 흐름 삽입 위치 / Signal Chain Position

현재 AudioEngine 오디오 콜백 흐름 (`CLAUDE.md` Key Implementations 참조):

```
1. Audio driver callback
2. Mute check (fast path)
3. Input → workBuffer 복사
4. Mono/Stereo 처리
5. Input gain 적용
6. Input RMS 측정
7. VST chain processBlock()
8. ★ Safety Limiter 처리 ★         ← 여기에 삽입 (4개 경로 모두 적용)
9. AudioRecorder 쓰기               ← 리미팅된 신호 녹음
10. SharedMemWriter (IPC) 쓰기       ← 리미팅된 신호를 Receiver에 전달
11. OutputRouter → Monitor           ← 모니터 자체 볼륨 적용
12. Output Volume + Main output 복사  ← 메인 출력 자체 볼륨 적용 (0-100%)
13. Output RMS 측정
```

**핵심**: Step 7(VST 체인) 바로 뒤, Step 9(녹음) 바로 앞에 삽입.
이 위치에서 처리하면 Recording/IPC/Monitor/Main Output 4개 경로 모두에 리미터가 적용됨.

**Output Volume과의 관계**: Output Volume (Step 12)은 Safety Limiter 이후에 적용됨.
따라서 Safety Limiter의 ceiling은 Output Volume 감쇠 전 기준임.

---

## 3. 알고리즘 / Algorithm

### True Peak Limiter (Feed-Forward, Look-Ahead 없음)

Look-ahead 없는 간단한 feed-forward 리미터. RT-safe 보장.

```
입력 → Peak 감지 → Gain Reduction 계산 → Gain 적용 → 출력

For each sample:
  1. peak = max(abs(L), abs(R))
  2. if peak > ceiling:
       targetGain = ceiling / peak
     else:
       targetGain = 1.0
  3. smoothedGain = envelope follow (attack/release)
  4. L *= smoothedGain
  5. R *= smoothedGain
  6. gainReduction_dB = 20 * log10(smoothedGain)  // UI 피드백용
```

### 파라미터

| 파라미터 | 타입 | 기본값 | 범위 | 저장 | 설명 |
|---------|------|--------|------|------|------|
| `enabled` | bool | true | on/off | settings.dppreset | 리미터 활성화 |
| `ceiling_dB` | float | -0.3 | -6.0 ~ 0.0 dBFS | settings.dppreset | 최대 출력 레벨. 이 값을 넘으면 리미팅 |

### 내부 상수 (하드코딩, 사용자 노출 없음)

| 상수 | 값 | 설명 |
|------|-----|------|
| `kAttackMs` | 0.1ms | 빠른 어택 (클리핑 방지 목적이므로 거의 즉시) |
| `kReleaseMs` | 50ms | 자연스러운 릴리즈 |
| `kAttackCoeff` | `exp(-1.0 / (sampleRate * 0.0001))` | 어택 계수 (prepareToPlay에서 계산) |
| `kReleaseCoeff` | `exp(-1.0 / (sampleRate * 0.05))` | 릴리즈 계수 (prepareToPlay에서 계산) |

### 엔벨로프 팔로워

```cpp
if (targetGain < currentGain)
    currentGain = attackCoeff * currentGain + (1 - attackCoeff) * targetGain;   // 빠르게 줄임
else
    currentGain = releaseCoeff * currentGain + (1 - releaseCoeff) * targetGain; // 천천히 복귀
```

---

## 4. 클래스 설계 / Class Design

### `SafetyLimiter` 클래스

**위치**: `host/Source/Audio/SafetyLimiter.h` (헤더 온리 또는 .h/.cpp 분리)

```
class SafetyLimiter
{
public:
    SafetyLimiter();

    // 오디오 설정 변경 시 호출 (SR 변경 → 계수 재계산)
    void prepareToPlay(double sampleRate);

    // 오디오 콜백에서 호출 — RT-safe, 인라인
    // buffer: JUCE AudioBuffer<float>& (또는 float** + numChannels + numSamples)
    void process(juce::AudioBuffer<float>& buffer);

    // 파라미터 설정 (atomic, 어느 스레드에서든 호출 가능)
    void setEnabled(bool enabled);
    void setCeiling(float dB);       // -6.0 ~ 0.0

    // 상태 조회 (UI 스레드에서 호출)
    bool isEnabled() const;
    float getCeilingdB() const;
    float getCurrentGainReduction() const;  // 0.0 ~ -inf dB (UI 표시용)
    bool isLimiting() const;                // GR > 0.1dB 이면 true

    // 설정 직렬화
    void saveState(juce::var& obj) const;
    void loadState(const juce::var& obj);

private:
    std::atomic<bool> enabled_ { true };
    std::atomic<float> ceilingLinear_ { /* dBtoLinear(-0.3f) */ };
    std::atomic<float> ceilingdB_ { -0.3f };

    float currentGain_ = 1.0f;         // 엔벨로프 상태 (오디오 스레드 전용)
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    std::atomic<float> gainReduction_dB_ { 0.0f };  // UI 피드백용
};
```

### 스레드 안전 규칙

- `enabled_`, `ceilingLinear_`, `ceilingdB_`: `std::atomic` — UI 스레드에서 쓰기, 오디오 스레드에서 읽기
- `currentGain_`, `attackCoeff_`, `releaseCoeff_`: 오디오 스레드 전용 (non-atomic)
- `gainReduction_dB_`: `std::atomic` — 오디오 스레드에서 쓰기, UI 스레드에서 읽기
- 기존 스레드 안전 규칙 #1 준수: 오디오 콜백에서 힙 할당, 뮤텍스, I/O 금지
- `prepareToPlay`는 오디오 스레드 밖에서 호출됨 (JUCE 규약)

---

## 5. AudioEngine 통합 / Integration

### AudioEngine.h 변경

```cpp
#include "SafetyLimiter.h"

class AudioEngine {
    // ... 기존 멤버 ...
    SafetyLimiter safetyLimiter_;

public:
    SafetyLimiter& getSafetyLimiter() { return safetyLimiter_; }
};
```

### AudioEngine.cpp 오디오 콜백 변경

기존 processBlock/audioDeviceIOCallback에서 VST 체인 처리 후, 출력 복사 전에 1줄 추가:

```cpp
// Step 7: VST chain
vstChain_.processBlock(workBuffer, midiBuffer);

// Step 8: Safety Limiter (NEW)
safetyLimiter_.process(workBuffer);

// Step 8.5: Recording (records limiter-processed audio)
recorder_.writeBlock(workBuffer, numSamples);

// Step 8.6: IPC (sends limiter-processed audio)
if (ipcEnabled_.load(std::memory_order_acquire))
    sharedMemWriter_.writeAudio(workBuffer, numSamples);

// Step 9: Monitor
outputRouter_.routeAudio(workBuffer, numSamples);

// Step 10: Output volume + main output copy
// ... existing code ...
```

### prepareToPlay 연결

AudioEngine의 `audioDeviceAboutToStart` 또는 SR 변경 시:

```cpp
safetyLimiter_.prepareToPlay(sampleRate);
```

### 설정 저장/불러오기

`settings.dppreset` JSON에 추가:

```json
{
  "version": 5,
  // ... 기존 필드 ...
  "safetyLimiter": {
    "enabled": true,
    "ceiling_dB": -0.3
  }
}
```

기존 version 4 파일 로드 시 `safetyLimiter` 키가 없으면 기본값 적용 (하위 호환).

---

## 6. UI 설계 / UI Design

### 6.1 출력 레벨 미터 옆 GR 미터

**위치**: 메인 윈도우 우측, 기존 OUTPUT 레벨 미터 옆에 얇은 GR(Gain Reduction) 미터 추가.

```
기존:                         변경 후:
┌──────────┐                  ┌──────────┬──┐
│ OUTPUT   │                  │ OUTPUT   │GR│
│ ██████   │                  │ ██████   │░░│  ← GR 미터 (위에서 아래로, 빨간색)
│ ██████   │                  │ ██████   │░░│
│ ██████   │                  │ ██████   │██│  ← 리미팅 중일 때 채워짐
└──────────┘                  └──────────┴──┘
```

- GR 미터 폭: 6-8px (좁게)
- 색상: 리미팅 시 빨간색/주황색 계열
- 리미팅 안 될 때: 비어있음 (투명)
- `SafetyLimiter::getCurrentGainReduction()` 값을 30Hz 타이머에서 폴링

### 6.2 상태 바 클리핑 인디케이터

**위치**: 하단 상태 바, CPU% 옆 또는 레이턴시 옆.

- 리미터 OFF: `[LIM OFF]` 회색 텍스트
- 리미터 ON + 리미팅 아님: 표시 없음 (투명하게 동작)
- 리미터 ON + 리미팅 중: `[LIM]` 빨간색 깜빡임 또는 주황색 표시
- 클릭 시: ceiling 값 조절 팝업 또는 Output 탭으로 이동

### 6.3 Output 탭 설정

**위치**: Output 탭 상단 또는 하단에 "Safety Limiter" 섹션 추가.

```
┌─ Safety Limiter ──────────────────────────┐
│ [✓ Enable]    Ceiling: [-0.3] dBFS  [슬라이더] │
│                                            │
│ GR: -2.3 dB  ████████░░░░  (실시간 표시)    │
└────────────────────────────────────────────┘
```

| UI 요소 | 타입 | 설명 |
|---------|------|------|
| Enable 토글 | ToggleButton | 리미터 on/off |
| Ceiling 슬라이더 | Slider | -6.0 ~ 0.0 dBFS, 기본 -0.3 |
| Ceiling 값 라벨 | Label | "-0.3 dBFS" 숫자 표시 |
| GR 미터 | 커스텀 컴포넌트 | 수평 바, 실시간 gain reduction 표시 |
| GR 값 라벨 | Label | "-2.3 dB" 또는 "0.0 dB" |

### 6.4 OutputPanel 레이아웃 참고

OutputPanel에는 이미 Output Volume 슬라이더(0-100%)가 추가되어 있음 (v4.0.0 Stream Deck 개선).
Safety Limiter UI 섹션은 Output Volume 슬라이더 아래에 배치.

---

## 7. 외부 제어 연동 / External Control Integration

### 7.1 StateBroadcaster 상태 추가

`state` JSON에 필드 추가:

```json
{
  "data": {
    // ... 기존 필드 ...
    "safety_limiter": {
      "enabled": true,
      "ceiling_dB": -0.3,
      "gain_reduction_dB": -2.3,
      "is_limiting": true
    }
  }
}
```

### 7.2 ActionDispatcher 액션 추가 (선택)

필요하다고 판단되면 액션 추가:

| 액션 | 파라미터 | 설명 |
|------|---------|------|
| `SafetyLimiterToggle` | — | 리미터 on/off 토글 |
| `SetSafetyLimiterCeiling` | floatParam = dB 값 | ceiling 변경 |

이 액션들은 HTTP API, WebSocket, MIDI, 핫키에서 모두 접근 가능하게 됨.

### 7.3 HTTP API 엔드포인트 추가 (선택)

| 엔드포인트 | 설명 |
|-----------|------|
| `GET /api/limiter/toggle` | 리미터 on/off 토글 |
| `GET /api/limiter/ceiling/:value` | ceiling 값 설정 (-6.0 ~ 0.0) |

### 7.4 WebSocket 명령 추가 (선택)

```json
{ "type": "action", "action": "safety_limiter_toggle", "params": {} }
{ "type": "action", "action": "set_safety_limiter_ceiling", "params": { "value": -0.5 } }
```

> **참고**: 외부 제어 연동(7.2~7.4)은 코어 기능(리미터 자체) 구현 후 2차로 추가해도 됨.
> 우선순위: 코어 알고리즘 + UI > 외부 제어 연동

---

## 8. 로깅 / Logging

기존 LOGRULE.md 규칙을 따름:

```
INF [AUDIO] Safety limiter enabled (ceiling: -0.3 dBFS)
INF [AUDIO] Safety limiter disabled
INF [AUDIO] Safety limiter ceiling changed: -0.3 → -1.0 dBFS
WRN [AUDIO] Safety limiter active: peak -2.3 dB gain reduction
```

- 활성화/비활성화/ceiling 변경은 `INF`
- 지속적 리미팅(3초 이상)은 `WRN` (rate limited, 10초에 1번)
- `AUD` 모드에서는 per-callback GR 값 (데시메이션 적용)

---

## 9. 테스트 / Tests

### Unit Tests (directpipe-host-tests)

`test_safety_limiter.cpp`:

| 테스트 | 설명 |
|--------|------|
| `DefaultState` | 기본값 enabled=true, ceiling=-0.3dBFS |
| `DisabledPassthrough` | disabled 시 신호 변경 없음 |
| `BelowCeiling` | ceiling 미만 신호 → 변경 없음 |
| `AboveCeiling` | ceiling 초과 신호 → ceiling 이하로 제한됨 |
| `CeilingRange` | -6.0 ~ 0.0 범위 검증 |
| `GainReductionFeedback` | 리미팅 시 getCurrentGainReduction() < 0 |
| `IsLimiting` | ceiling 초과 시 isLimiting() == true |
| `SampleRateChange` | prepareToPlay 후 계수 재계산 확인 |
| `AtomicThreadSafety` | 멀티스레드 읽기/쓰기 레이스 없음 |
| `SerializationRoundtrip` | saveState → loadState → 동일값 |
| `LegacyPresetCompat` | safetyLimiter 키 없는 JSON → 기본값 |

### Pre-Release Dashboard

`tools/pre-release-dashboard.html`에 수동 테스트 항목 추가:

- [ ] 리미터 ON 상태에서 큰 입력 → 출력이 ceiling 이하인지 확인
- [ ] 리미터 OFF 상태에서 큰 입력 → 출력이 그대로 나오는지 확인
- [ ] GR 미터가 리미팅 시 움직이는지 확인
- [ ] 슬롯 전환해도 리미터 설정 유지되는지 확인
- [ ] ceiling 값 변경 → 즉시 적용되는지 확인
- [ ] 앱 재시작 후 리미터 설정 유지되는지 확인

---

## 10. 파일 변경 요약 / File Changes

### 새 파일

| 파일 | 설명 |
|------|------|
| `host/Source/Audio/SafetyLimiter.h` | 리미터 클래스 헤더 |
| `host/Source/Audio/SafetyLimiter.cpp` | 리미터 구현 (또는 헤더 온리) |
| `tests/test_safety_limiter.cpp` | 유닛 테스트 |

### 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `host/Source/Audio/AudioEngine.h` | `SafetyLimiter safetyLimiter_` 멤버 추가, getter |
| `host/Source/Audio/AudioEngine.cpp` | 오디오 콜백에 `safetyLimiter_.process()` 삽입, prepareToPlay 연결 |
| `host/Source/UI/OutputPanel.h/cpp` | Safety Limiter 섹션 UI 추가 |
| `host/Source/UI/LevelMeter.h/cpp` | GR 미터 추가 (또는 별도 `GainReductionMeter` 컴포넌트) |
| `host/Source/Control/StateBroadcaster.cpp` | state JSON에 safety_limiter 필드 추가 |
| `host/Source/MainComponent.cpp` | GR 미터 배치, 상태 바 인디케이터 |
| `CMakeLists.txt` | 새 소스 파일 등록 |
| `CLAUDE.md` | Key Implementations 섹션에 SafetyLimiter 컴포넌트 + Data Flow Step 8 설명 추가 |
| `docs/STREAMDECK_GUIDE.md` | Stream Deck 연동 시 Safety Limiter 상태 표시 (해당 시) |
| `docs/CONTROL_API.md` | (외부 제어 연동 시) 엔드포인트 추가 |

---

## 11. 구현 순서 / Implementation Order

```
Phase 1 (코어):
  1. SafetyLimiter.h/cpp 작성
  2. AudioEngine에 통합 (process 호출 삽입)
  3. prepareToPlay 연결
  4. settings.dppreset 직렬화 (version 5)
  5. 유닛 테스트 작성

Phase 2 (UI):
  6. Output 탭에 Safety Limiter 섹션 추가
  7. GR 미터 컴포넌트 (출력 미터 옆)
  8. 상태 바 클리핑 인디케이터
  9. StateBroadcaster에 상태 추가

Phase 3 (외부 제어, 선택):
  10. ActionDispatcher에 액션 추가
  11. HTTP API 엔드포인트
  12. WebSocket 명령
  13. Stream Deck 액션 (추후)
```

---

## 12. 참고 사항 / Notes

- `ScopedNoDenormals`는 AudioEngine 콜백 최상단에서 이미 적용됨 → SafetyLimiter 내부에서 별도 처리 불필요
- Mono 모드에서도 workBuffer는 스테레오(채널 0 = 채널 1)로 처리됨 → 리미터는 항상 2채널 처리
- 뮤트 fast-path (Step 2)에서 이미 리턴하므로, 뮤트 상태에서 리미터가 호출되지 않음
- `outputNone_` 모드에서도 리미터는 동작해야 함 (모니터/IPC/녹음 경로에 영향)
- 패닉 뮤트 중에는 리미터 설정 변경이 가능해야 함 (잠금 대상 아님)
