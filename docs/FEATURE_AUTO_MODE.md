# Feature Spec: Auto Mode (자동 프로세싱 모드)

> **DirectPipe v5.0.0 로드맵** — 기획 문서 (Claude Code 핸드오프용)
>
> 이 문서는 DirectPipe에 "Auto Mode"를 추가하기 위한 설계 명세입니다.
> VST 플러그인 없이 내장 프로세싱만으로 마이크 오디오를 처리하는 제로 설정 경로를 제공합니다.
> 기존 코드베이스의 아키텍처(`CLAUDE.md` Key Implementations 섹션)와 스레드 안전 규칙을 준수해야 합니다.
>
> **의존성**: 이 기능은 `FEATURE_SAFETY_LIMITER.md`의 Safety Limiter가 선행 구현되어야 합니다.
> Auto 모드의 AGC가 과도 증폭 시 Safety Limiter가 최종 보호 역할을 담당합니다.

---

## 1. 목적 / Purpose

VST 플러그인이 뭔지 모르거나, 플러그인 설치 없이 깨끗한 마이크 소리만 원하는 사용자를 위한 **제로 설정 내장 프로세싱 모드**.

### 핵심 컨셉

- **Auto 버튼 하나**로 내장 노이즈 제거 + 자동 볼륨 조절 활성화
- VST 플러그인 설치, 스캔, 체인 구성 **일체 불필요**
- 디스코드의 "입력 감도 자동 조정 + 잡음 제거 + 자동 증폭 조절"에 해당하는 기능을 DirectPipe 안에서 구현
- **기존 VST 체인과 공존** — Auto 모드와 VST 모드는 토글로 전환, 동시 동작 아님 (이중 처리 불가)

### 타겟 사용자

| 사용자 | 시나리오 |
|--------|---------|
| VST를 모르는 사용자 | "설치했는데 뭘 해야 하지?" → Auto 누르면 끝 |
| 빠른 설정이 필요한 사용자 | 플러그인 스캔/선택 없이 즉시 사용 |
| 세팅 도우미가 세팅해주는 대상 | "일단 Auto 켜놓으세요" 한마디로 해결 |
| VST 체인 구성 전 임시 사용 | 플러그인 고르는 동안 Auto로 임시 사용 |

### 디자인 원칙

1. **제로 설정** — Auto 버튼 하나로 동작. 추가 설정 없이도 즉시 사용 가능
2. **기존 UI 재활용** — VST 체인 에디터와 동일한 행 UI(Edit, Bypass) 사용. 새 UI 컴포넌트 최소화
3. **크로스 플랫폼** — RNNoise(순수 C), Filter/AGC(순수 C++) 모두 플랫폼 의존성 없음. Windows/macOS/Linux 전부 동작
4. **VST 체인과 충돌 없음** — Auto ON이면 VST 체인 비활성화. 이중 처리 불가능한 구조
5. **RT-safe** — 오디오 콜백에서 힙 할당, 뮤텍스, I/O 없음

---

## 2. 신호 흐름 / Signal Flow

### Auto OFF (기존 VST 모드)

```
Mic → Input Gain (수동) → VST Plugin Chain → Safety Limiter → 출력
```

### Auto ON

```
Auto ON:
Mic → Input Gain (수동) → [Filter] → [RNNoise] → [AGC] → Safety Limiter → Recording/IPC/Monitor/Output
                           on/off     on/off       on/off
```

- **Input Gain**: Auto 모드에서도 수동 조절 가능. 대략적인 레벨을 잡는 용도
- **Filter**: HPF(Low Cut) + LPF(High Cut) 통합. 불필요 주파수 제거. 개별 Bypass 가능
- **RNNoise**: AI 노이즈 제거. 프리셋별 EQ + dry/wet. 개별 Bypass 가능
- **AGC**: 자동 볼륨 조절 (느린 컴프레서 스타일). 개별 Bypass 가능
- **Safety Limiter**: Auto/VST 모드 무관하게 항상 동작 (별도 Feature Spec 참조)

### 삽입 위치 (AudioEngine 오디오 콜백)

```
1. Audio driver callback
2. Mute check (fast path)
3. Input → workBuffer 복사
4. Mono/Stereo 처리
5. Input gain 적용 (수동)
6. Input RMS 측정
7. ★ Auto 모드 분기:
     Auto ON  → Filter → RNNoise → AGC
     Auto OFF → VST chain processBlock()
8. Safety Limiter
9. AudioRecorder 쓰기
10. SharedMemWriter (IPC) 쓰기
11. OutputRouter → Monitor (자체 볼륨)
12. Output Volume + Main output 복사 (자체 볼륨 0-100%)
13. Output RMS 측정
```

---

## 3. Auto 모드 진입/전환 / Mode Switching

### UI 위치

**입력 게인 슬라이더 옆에 [Auto] 토글 버튼** 배치.

```
┌─INPUT──────────────────────────────────┐
│ 레벨미터 │ 게인 슬라이더  │ [Auto] 버튼  │
│  (40px)  │ (0.0x~2.0x)   │              │
└────────────────────────────────────────┘
```

### 전환 동작

**Auto ON:**
1. VST 체인 비활성화 (processBlock 스킵)
2. 프리셋 슬롯 A-E 비활성화 (회색 처리, 클릭 불가)
3. VST 체인 에디터 영역이 Auto 프로세싱 패널로 교체
4. Add Plugin / Scan 버튼 숨김
5. 내장 프로세싱(Filter + RNNoise + AGC) 활성화

**Auto OFF:**
1. 내장 프로세싱 비활성화
2. VST 체인 복귀 (이전 상태 그대로)
3. 프리셋 슬롯 A-E 복귀
4. VST 체인 에디터 영역 복귀

### 상태 저장

- Auto 모드 on/off 상태: `settings.dppreset`에 저장
- Auto 모드 설정(Filter, RNNoise 프리셋, AGC 타겟 등): `settings.dppreset`에 저장
- VST 체인은 Auto 모드 전환 시 건드리지 않음 (그대로 보존)

---

## 4. UI 설계 / UI Design

### 4.1 Auto ON 상태의 체인 에디터 영역

기존 PluginChainEditor의 행 UI를 재활용. Auto 모드 내장 프로세서를 VST 플러그인과 **동일한 형태**로 표시:

```
Auto ON:
┌─Auto 프로세싱────────────────────────────────┐
│ 1. Filter                  [Edit][Bp]        │  ← Edit: HPF+LPF 설정 패널
│ 2. Noise Removal           [Edit][Bp]        │  ← Edit: RNNoise 프리셋/강도 설정
│ 3. Auto Gain               [Edit][Bp]        │  ← Edit: AGC 타겟 레벨 설정
└──────────────────────────────────────────────┘
┌─프리셋 슬롯──────────────────────────────────┐
│ [A] [B] [C] [D] [E]                         │  ← 비활성화 (회색, 클릭 불가)
└──────────────────────────────────────────────┘
```

### VST 체인 에디터와의 차이점

| 요소 | VST 모드 | Auto 모드 |
|------|---------|----------|
| Remove (X) 버튼 | 있음 | **없음** — 내장 프로세서 삭제 불가 |
| 드래그앤드롭 순서 변경 | 가능 | **불가** — Filter → RNNoise → AGC 순서 고정 |
| Add Plugin / Scan 버튼 | 표시 | **숨김** |
| Edit 버튼 | 플러그인 네이티브 GUI | **DirectPipe 자체 설정 패널** |
| Bypass 버튼 | 개별 플러그인 bypass | 개별 프로세서 bypass (동일 동작) |

### 4.2 Filter Edit 패널

HPF(Low Cut)와 LPF(High Cut)를 하나의 패널에서 제어.

```
┌──────────────────────────────────────┐
│ Filter                               │
│                                      │
│ Low Cut (HPF):  [✓ on/off]           │
│   [▼ 프리셋 드롭다운]                 │
│     60Hz  — 극저음만 제거 (기본)       │
│     80Hz  — 에어컨/환풍기 제거         │
│     120Hz — 강한 저역 차단             │
│     커스텀                            │
│   커스텀: [████████░░] 20~300Hz       │
│                                      │
│ High Cut (LPF): [✓ on/off]           │
│   [▼ 프리셋 드롭다운]                 │
│     16kHz — 가벼운 고역 제거 (기본)     │
│     12kHz — 표준 고역 제거             │
│     8kHz  — 강한 고역 차단             │
│     커스텀                            │
│   커스텀: [████████░░] 4~20kHz        │
│                                      │
│ Status: Active                       │
└──────────────────────────────────────┘
```

**파라미터:**

| 파라미터 | 타입 | 기본값 | 범위 | 설명 |
|---------|------|--------|------|------|
| `hpf_enabled` | bool | true | on/off | HPF 활성화 |
| `hpf_frequency` | float | 60.0 | 20~300 Hz | HPF 컷오프 주파수 |
| `lpf_enabled` | bool | false | on/off | LPF 활성화 |
| `lpf_frequency` | float | 16000.0 | 4000~20000 Hz | LPF 컷오프 주파수 |

**구현**: JUCE `IIRFilter` — 2차 버터워스. RT-safe, CPU 무시 가능.

### 4.3 Noise Removal (RNNoise) Edit 패널

기술적인 억제 강도 대신 **사용 시나리오 기반 프리셋**으로 표시.

```
┌──────────────────────────────────────┐
│ Noise Removal                        │
│                                      │
│ Mode:  [▼ 드롭다운]                   │
│   ┌──────────────────────────────┐   │
│   │ 🎙 토크/팟캐스트 (남성 저음)   │   │
│   │ 🎤 보컬/노래 (여성 고음)      │   │
│   │ 🎮 게이밍 (키보드/마우스)      │   │
│   │ 💬 회의/통화 (일반)           │   │
│   │ 🔧 커스텀                     │   │
│   └──────────────────────────────┘   │
│                                      │
│ [커스텀 선택 시]                       │
│ Suppression: ████████░░ [슬라이더]    │
│                                      │
│ Status: Active                       │
└──────────────────────────────────────┘
```

**프리셋 동작 (EQ + dry/wet 조합):**

| 프리셋 | Dry/Wet | 후처리 EQ | 설명 |
|--------|---------|-----------|------|
| 🎙 토크/팟캐스트 | 100% wet (풀 억제) | 없음 | 남성 음성에 RNNoise 오류 거의 없으므로 풀 억제 |
| 🎤 보컬/노래 | 60-70% wet | 고역(2kHz+) 보존 부스트 | 여성 고음/웃음/노래에서 찢어지는 문제 완화 |
| 🎮 게이밍 | 90% wet | 1-4kHz 대역 타겟 억제 | 키보드/마우스 소음 강한 억제, 음성 대역 보존 |
| 💬 회의/통화 | 80% wet | 없음 | 범용 표준 설정 |
| 🔧 커스텀 | 슬라이더 0-100% | 없음 | 사용자 직접 조절 |

**억제 강도 구현 방식: 프리셋별 EQ + dry/wet 믹스**
- RNNoise 출력(클린)과 원본(노이지)을 dry/wet 비율로 블렌딩
- 프리셋별로 RNNoise 후단에 경량 EQ 필터 적용하여 주파수 대역 보정
- 커스텀 모드에서는 dry/wet 슬라이더만 노출 (EQ 없음)

**파라미터:**

| 파라미터 | 타입 | 기본값 | 범위 | 설명 |
|---------|------|--------|------|------|
| `rnn_preset` | enum | Talk | Talk/Vocal/Gaming/Meeting/Custom | 프리셋 선택 |
| `rnn_wet` | float | 1.0 | 0.0~1.0 | 커스텀 모드 dry/wet (프리셋 시 자동 설정) |

**RNNoise 기술 참고:**

- 라이브러리: RNNoise (BSD-3 라이선스, 순수 C, 직접 링크)
- 참조 구현: [werman/noise-suppression-for-voice](https://github.com/werman/noise-suppression-for-voice)
- **48kHz 고정 제한**: RNNoise 내부 모델이 48kHz로 학습됨
- **리샘플링 전략 (방법 A)**: 48kHz가 아닌 샘플레이트에서는 JUCE `LagrangeInterpolator`로 입력 SR → 48kHz → RNNoise → 48kHz → 원래 SR 왕복 리샘플링
  - CPU 비용: RNNoise 자체 연산의 1% 미만 (무시 가능)
  - 레이턴시: 왕복 ~4 샘플 (44.1kHz 기준 ~0.09ms, 체감 불가)
  - 품질: 음성 대역(100Hz~8kHz)에서 무손실
  - 검증: werman/noise-suppression-for-voice VST 플러그인이 동일 방식 사용 중
- **RNNoise의 알려진 한계**: 남성 음성 위주 학습 데이터로 인해 여성 고음역 + 고에너지(웃음, 노래)에서 음성을 노이즈로 오인하는 경우 있음. "보컬/노래" 프리셋에서 dry/wet 60-70% + 고역 보존 EQ로 완화

### 4.4 Auto Gain (AGC) Edit 패널

```
┌──────────────────────────────────────┐
│ Auto Gain Control                    │
│                                      │
│ Target Level:                        │
│ [-18 dBFS] ████████████░░░░ 슬라이더  │
│            -24        -6             │
│                                      │
│ Current Gain: +3.2 dB                │  ← 실시간 표시
│ Input Level: -24.5 dBFS              │  ← 실시간 표시
│ Output Level: -18.1 dBFS             │  ← 실시간 표시
│                                      │
│ [▶ 고급 설정]                         │  ← 접힌 상태 (기본)
│ ┌─고급 설정──────────────────────┐    │
│ │ Max Gain: [+18 dB] ██████░░   │    │  ← 숨겨진 상한선 설정
│ └────────────────────────────────┘    │
│                                      │
│ Status: Active                       │
└──────────────────────────────────────┘
```

**파라미터:**

| 파라미터 | 타입 | 기본값 | 범위 | UI | 설명 |
|---------|------|--------|------|-----|------|
| `agc_target_dB` | float | -18.0 | -24.0 ~ -6.0 dBFS | 슬라이더 | 타겟 출력 레벨 |
| `agc_max_gain_dB` | float | 18.0 | 6.0 ~ 30.0 dB | 고급 설정 (숨김) | 최대 게인 상한선 |

**AGC 알고리즘:**

| 항목 | 사양 |
|------|------|
| 방식 | 느린 컴프레서 스타일 (feed-forward) |
| 윈도우 | 2-3초 RMS 윈도우 |
| 반응 속도 | 느린 어택/릴리즈 (자연스러운 볼륨 변화) |
| 타겟 | 사용자 설정 가능 (-24 ~ -6 dBFS, 기본 -18) |
| 최대 게인 | 상한선 있음 (고급 설정, 기본 +18dB) |
| Safety Limiter 관계 | AGC가 게인을 올려 클리핑 영역 진입 시 Safety Limiter가 잡아줌. AGC max gain 상한선으로 과도한 증폭 방지 |

**AGC 동작 원리:**

```
매 프레임:
  1. RMS 측정 (2-3초 슬라이딩 윈도우)
  2. targetGain_dB = target_dB - measured_RMS_dB
  3. targetGain_dB = clamp(targetGain_dB, 0, maxGain_dB)  // 줄이기만 하거나, 최대 maxGain까지 증폭
  4. smoothedGain = envelope follow (느린 attack + 느린 release)
  5. 오디오에 smoothedGain 적용
```

**엔벨로프 계수:**

| 상수 | 값 | 설명 |
|------|-----|------|
| `kAttackMs` | 2000ms | 느린 어택 (2초) |
| `kReleaseMs` | 3000ms | 느린 릴리즈 (3초) |

`prepareToPlay`에서 샘플레이트 기반 계수 계산.

---

## 5. 클래스 설계 / Class Design

### 5.1 AutoModeProcessor

Auto 모드 전체를 관리하는 최상위 클래스.

**위치**: `host/Source/Audio/AutoModeProcessor.h/cpp`

```
class AutoModeProcessor
{
public:
    AutoModeProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock);

    // 오디오 콜백에서 호출 — RT-safe
    void process(juce::AudioBuffer<float>& buffer);

    // 모드 전환
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // 개별 프로세서 접근
    AutoFilter& getFilter();
    AutoNoiseRemoval& getNoiseRemoval();
    AutoGainControl& getAGC();

    // 설정 직렬화
    void saveState(juce::var& obj) const;
    void loadState(const juce::var& obj);

private:
    std::atomic<bool> enabled_ { false };
    AutoFilter filter_;
    AutoNoiseRemoval noiseRemoval_;
    AutoGainControl agc_;
};
```

### 5.2 AutoFilter (HPF + LPF)

**위치**: `host/Source/Audio/AutoFilter.h/cpp`

```
class AutoFilter
{
public:
    void prepareToPlay(double sampleRate);
    void process(juce::AudioBuffer<float>& buffer);  // RT-safe

    void setHPFEnabled(bool enabled);
    void setHPFFrequency(float hz);       // 20~300 Hz
    void setLPFEnabled(bool enabled);
    void setLPFFrequency(float hz);       // 4000~20000 Hz
    void setBypassed(bool bypassed);

    // 상태 조회
    bool isBypassed() const;
    float getHPFFrequency() const;
    float getLPFFrequency() const;

    void saveState(juce::var& obj) const;
    void loadState(const juce::var& obj);

private:
    std::atomic<bool> bypassed_ { false };
    std::atomic<bool> hpfEnabled_ { true };
    std::atomic<bool> lpfEnabled_ { false };
    std::atomic<float> hpfFreq_ { 60.0f };
    std::atomic<float> lpfFreq_ { 16000.0f };

    // JUCE IIRFilter (2차 버터워스, 채널별)
    juce::IIRFilter hpfL_, hpfR_;
    juce::IIRFilter lpfL_, lpfR_;
    double currentSampleRate_ = 48000.0;
};
```

### 5.3 AutoNoiseRemoval (RNNoise 래퍼)

**위치**: `host/Source/Audio/AutoNoiseRemoval.h/cpp`

```
class AutoNoiseRemoval
{
public:
    AutoNoiseRemoval();
    ~AutoNoiseRemoval();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void process(juce::AudioBuffer<float>& buffer);  // RT-safe

    void setPreset(NoisePreset preset);  // Talk, Vocal, Gaming, Meeting, Custom
    void setCustomWet(float wet);         // 0.0~1.0 (커스텀 모드용)
    void setBypassed(bool bypassed);

    bool isBypassed() const;
    NoisePreset getPreset() const;

    void saveState(juce::var& obj) const;
    void loadState(const juce::var& obj);

    enum class NoisePreset { Talk, Vocal, Gaming, Meeting, Custom };

private:
    std::atomic<bool> bypassed_ { false };
    std::atomic<int> preset_ { 0 };       // NoisePreset enum
    std::atomic<float> customWet_ { 1.0f };

    // RNNoise 인스턴스
    DenoiseState* rnnState_ = nullptr;    // RNNoise C API

    // 리샘플러 (48kHz 변환용)
    juce::LagrangeInterpolator resamplerIn_;   // 입력SR → 48kHz
    juce::LagrangeInterpolator resamplerOut_;  // 48kHz → 입력SR
    double hostSampleRate_ = 48000.0;
    bool needsResampling_ = false;

    // 리샘플링 버퍼 (사전 할당)
    std::vector<float> resampleInBuffer_;
    std::vector<float> resampleOutBuffer_;
    std::vector<float> rnnInputBuffer_;    // RNNoise 480 프레임 단위
    std::vector<float> rnnOutputBuffer_;

    // 프리셋별 EQ (후처리)
    juce::IIRFilter presetEqL_, presetEqR_;

    // dry/wet 믹싱
    float currentWet_ = 1.0f;             // 프리셋에서 자동 설정

    void updatePresetParameters();
};
```

**RNNoise 통합 상세:**

- RNNoise API: `rnnoise_create()`, `rnnoise_destroy()`, `rnnoise_process_frame(state, out, in)`
- 프레임 크기: 고정 480 샘플 (10ms @ 48kHz)
- 입력/출력: float[], 모노
- 스테레오 처리: 채널별 독립 처리 (RNNoise 인스턴스 2개) 또는 모노 다운믹스 → 처리 → 양쪽 적용

**리샘플링 흐름 (호스트 SR ≠ 48kHz 일 때):**

```
1. 호스트 버퍼 (hostSR, N samples)
2. resamplerIn_: hostSR → 48kHz (출력 크기 = N * 48000 / hostSR)
3. 480 프레임 단위로 rnnoise_process_frame() 호출
4. resamplerOut_: 48kHz → hostSR
5. dry/wet 믹싱
6. 프리셋 EQ 적용
```

### 5.4 AutoGainControl (AGC)

**위치**: `host/Source/Audio/AutoGainControl.h/cpp`

```
class AutoGainControl
{
public:
    AutoGainControl();

    void prepareToPlay(double sampleRate);
    void process(juce::AudioBuffer<float>& buffer);  // RT-safe

    void setTargetLevel(float dB);        // -24.0 ~ -6.0 dBFS
    void setMaxGain(float dB);            // 6.0 ~ 30.0 dB (고급 설정)
    void setBypassed(bool bypassed);

    bool isBypassed() const;
    float getTargetLevel() const;
    float getCurrentGain() const;          // UI 피드백용 (dB)

    void saveState(juce::var& obj) const;
    void loadState(const juce::var& obj);

private:
    std::atomic<bool> bypassed_ { false };
    std::atomic<float> targetdB_ { -18.0f };
    std::atomic<float> maxGaindB_ { 18.0f };

    // 엔벨로프 상태 (오디오 스레드 전용)
    float currentGain_ = 1.0f;
    float rmsAccumulator_ = 0.0f;
    int rmsSampleCount_ = 0;
    float smoothedRMS_ = 0.0f;

    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    double currentSampleRate_ = 48000.0;

    std::atomic<float> currentGaindB_ { 0.0f };  // UI 피드백용
};
```

---

## 6. AudioEngine 통합 / Integration

### AudioEngine.h 변경

```cpp
#include "AutoModeProcessor.h"

class AudioEngine {
    // ... 기존 멤버 ...
    AutoModeProcessor autoMode_;

public:
    AutoModeProcessor& getAutoMode() { return autoMode_; }
};
```

### AudioEngine.cpp 오디오 콜백 변경

기존 VST 체인 처리 부분에 Auto 모드 분기 추가:

```cpp
// Step 5: Input gain (기존)
// Step 6: Input RMS (기존)

// Step 7: Auto/VST 분기
if (autoMode_.isEnabled())
    autoMode_.process(workBuffer);
else
    vstChain_.processBlock(workBuffer, midiBuffer);

// Step 8: Safety Limiter
safetyLimiter_.process(workBuffer);

// Step 9-10: Recording + IPC (both get limiter-processed audio)
recorder_.writeBlock(workBuffer, numSamples);
if (ipcEnabled_.load(std::memory_order_acquire))
    sharedMemWriter_.writeAudio(workBuffer, numSamples);

// Step 11-12: Monitor + Main Output (each with own volume)
outputRouter_.routeAudio(workBuffer, numSamples);
// ... output volume + memcpy ...
```

### prepareToPlay 연결

```cpp
autoMode_.prepareToPlay(sampleRate, samplesPerBlock);
```

### 설정 저장/불러오기

`settings.dppreset` JSON에 추가:

```json
{
  "version": 5,
  // ... 기존 필드 ...
  "autoMode": {
    "enabled": false,
    "filter": {
      "bypassed": false,
      "hpfEnabled": true,
      "hpfFrequency": 60.0,
      "lpfEnabled": false,
      "lpfFrequency": 16000.0
    },
    "noiseRemoval": {
      "bypassed": false,
      "preset": "talk",
      "customWet": 1.0
    },
    "agc": {
      "bypassed": false,
      "targetdB": -18.0,
      "maxGaindB": 18.0
    }
  }
}
```

기존 version 4 파일 로드 시 `autoMode` 키가 없으면 기본값 적용 (하위 호환).

---

## 7. 외부 제어 연동 / External Control

### 7.1 ActionDispatcher 액션 추가

| 액션 | 파라미터 | 설명 |
|------|---------|------|
| `AutoModeToggle` | — | Auto 모드 on/off 토글 |

> Filter/RNNoise/AGC 개별 bypass는 Auto 모드 내부 UI에서만 제어 (외부 제어 불필요).
> 필요 시 추후 추가 가능.

### 7.2 StateBroadcaster 상태 추가

```json
{
  "data": {
    // ... 기존 필드 ...
    "auto_mode": {
      "enabled": true,
      "filter_bypassed": false,
      "noise_removal_bypassed": false,
      "noise_removal_preset": "talk",
      "agc_bypassed": false,
      "agc_target_dB": -18.0,
      "agc_current_gain_dB": 3.2
    }
  }
}
```

### 7.3 HTTP API 엔드포인트 추가

| 엔드포인트 | 설명 |
|-----------|------|
| `GET /api/auto/toggle` | Auto 모드 on/off 토글 |

### 7.4 핫키

| 핫키 | 액션 |
|------|------|
| (없음 — 사용자가 Controls > Hotkeys에서 수동 추가) | Auto 모드 토글 |

> **참고**: v4.0.0에서 기본 핫키를 최소화하는 방향으로 변경됨 (Panic Mute + Plugin 1 Bypass + 몇 개만 기본 등록).
> Auto Mode 핫키는 사용자가 필요 시 수동 추가하도록 함.

### 7.5 Stream Deck 연동 참고

- Auto Mode ON 상태에서 Stream Deck Bypass Toggle 액션: VST 체인이 비활성이므로 동작하지 않아야 함. `ActionHandler`에서 Auto 모드 시 PluginBypass/MasterBypass를 차단하거나, 무시하고 알림 표시 필요
- Auto Mode 상태는 AppState 브로드캐스트에 포함되어 Stream Deck Performance Monitor 등에서 표시 가능

---

## 8. RNNoise 빌드 통합 / RNNoise Build Integration

### CMake 설정

RNNoise를 서브모듈 또는 FetchContent로 통합:

```cmake
# Option A: FetchContent
FetchContent_Declare(
  rnnoise
  GIT_REPOSITORY https://github.com/xiph/rnnoise.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(rnnoise)

# Option B: 소스 직접 포함
add_library(rnnoise STATIC
  thirdparty/rnnoise/src/denoise.c
  thirdparty/rnnoise/src/rnn.c
  thirdparty/rnnoise/src/rnn_data.c
  thirdparty/rnnoise/src/pitch.c
  thirdparty/rnnoise/src/celt_lpc.c
)
target_include_directories(rnnoise PUBLIC thirdparty/rnnoise/include)
```

### 라이선스

- RNNoise: **BSD-3-Clause** — DirectPipe(GPL-3.0)와 호환. 재배포 가능.
- 라이선스 고지를 LICENSE 파일 또는 About 다이얼로그에 추가 필요.

### 크로스 플랫폼

- 순수 C (libm만 의존) → Windows/macOS/Linux 전부 빌드 가능
- 플랫폼 종속 API 없음
- CMake에서 플랫폼별 조건부 컴파일 불필요

---

## 9. 로깅 / Logging

기존 LOGRULE.md 규칙을 따름:

```
INF [AUDIO] Auto mode enabled
INF [AUDIO] Auto mode disabled
INF [AUDIO] Auto mode: noise removal preset changed to "Vocal"
INF [AUDIO] Auto mode: AGC target changed to -16.0 dBFS
INF [AUDIO] Auto mode: RNNoise resampling active (44100Hz → 48000Hz → 44100Hz)
WRN [AUDIO] Auto mode: AGC at max gain (+18.0 dB) — consider increasing input gain
AUD [AUDIO] Auto mode state: filter=ON(HPF:80Hz,LPF:OFF) noise=ON(talk,wet=1.0) agc=ON(target:-18dB,gain:+3.2dB)
```

---

## 10. 테스트 / Tests

### Unit Tests (directpipe-host-tests)

`test_auto_mode.cpp`:

| 테스트 | 설명 |
|--------|------|
| `DefaultDisabled` | 기본값 Auto 모드 OFF |
| `EnableDisableToggle` | ON/OFF 전환 시 상태 정확히 변경 |
| `FilterHPF` | HPF 활성 시 저역 감쇠 확인 |
| `FilterLPF` | LPF 활성 시 고역 감쇠 확인 |
| `FilterBypass` | Filter bypass 시 신호 변경 없음 |
| `AGCBelowTarget` | 입력 < 타겟 → 게인 증가 |
| `AGCAboveTarget` | 입력 > 타겟 → 게인 감소 |
| `AGCMaxGainClamp` | 최대 게인 상한선 동작 확인 |
| `AGCBypass` | AGC bypass 시 신호 변경 없음 |
| `AGCSlowResponse` | 2-3초 윈도우 반응 속도 확인 |
| `AutoModeVSTexclusive` | Auto ON 시 VST 체인 processBlock 스킵 확인 |
| `SerializationRoundtrip` | saveState → loadState → 동일값 |
| `LegacyPresetCompat` | autoMode 키 없는 JSON → 기본값 |
| `ResamplerActivation` | 44.1kHz에서 리샘플러 활성화 확인 |
| `ResamplerPassthrough` | 48kHz에서 리샘플러 비활성화 확인 |

> **참고**: RNNoise 자체의 노이즈 제거 품질은 유닛 테스트로 검증하기 어려움.
> pre-release dashboard의 수동 테스트로 커버.

### Pre-Release Dashboard

`tools/pre-release-dashboard.html`에 수동 테스트 항목 추가:

- [ ] Auto 버튼 → 체인 에디터가 Auto 패널로 전환되는지
- [ ] Auto ON → 프리셋 슬롯 A-E 비활성화(회색)되는지
- [ ] Auto OFF → VST 체인 + 슬롯 복귀되는지
- [ ] Filter Edit → HPF/LPF on/off 및 주파수 조절 동작
- [ ] Noise Removal Edit → 프리셋 전환 동작
- [ ] Noise Removal → "보컬/노래" 프리셋에서 여성 고음 찢어짐 완화 확인
- [ ] AGC Edit → 타겟 레벨 슬라이더 동작, 실시간 게인 표시
- [ ] AGC → 작게 말했다가 크게 말할 때 자연스러운 볼륨 변화
- [ ] Auto 모드 설정이 앱 재시작 후 유지되는지
- [ ] 44.1kHz 장치에서 Auto 모드 정상 동작 (리샘플링)
- [ ] 48kHz 장치에서 Auto 모드 정상 동작 (리샘플링 없음)
- [ ] 사용자 지정 핫키로 Auto 모드 토글 (Controls > Hotkeys에서 수동 추가 후)
- [ ] /api/auto/toggle HTTP API 동작

---

## 11. 파일 변경 요약 / File Changes

### 새 파일

| 파일 | 설명 |
|------|------|
| `host/Source/Audio/AutoModeProcessor.h/cpp` | Auto 모드 최상위 프로세서 |
| `host/Source/Audio/AutoFilter.h/cpp` | 내장 HPF + LPF |
| `host/Source/Audio/AutoNoiseRemoval.h/cpp` | RNNoise 래퍼 + 리샘플러 + 프리셋 EQ |
| `host/Source/Audio/AutoGainControl.h/cpp` | 내장 AGC |
| `host/Source/UI/AutoModePanel.h/cpp` | Auto 모드 UI 패널 (체인 에디터 영역 교체) |
| `host/Source/UI/FilterEditPanel.h/cpp` | Filter Edit 팝업 패널 |
| `host/Source/UI/NoiseRemovalEditPanel.h/cpp` | RNNoise Edit 팝업 패널 |
| `host/Source/UI/AGCEditPanel.h/cpp` | AGC Edit 팝업 패널 |
| `tests/test_auto_mode.cpp` | 유닛 테스트 |
| `thirdparty/rnnoise/` | RNNoise 소스 (또는 CMake FetchContent) |

### 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `host/Source/Audio/AudioEngine.h/cpp` | `AutoModeProcessor` 멤버 추가, 오디오 콜백 분기, prepareToPlay 연결 |
| `host/Source/UI/MainComponent.h/cpp` | Auto 버튼 추가 (입력 게인 옆), Auto/VST 패널 전환 로직, 프리셋 슬롯 비활성화 |
| `host/Source/UI/PluginChainEditor.h/cpp` | Auto 모드 시 숨김 처리 |
| `host/Source/UI/PresetSlotBar.h/cpp` | Auto 모드 시 비활성화(회색) 처리 |
| `host/Source/Control/ActionDispatcher.h` | `AutoModeToggle` 액션 추가 |
| `host/Source/Control/ActionHandler.h/cpp` | AutoModeToggle 핸들링 |
| `host/Source/Control/StateBroadcaster.cpp` | state JSON에 auto_mode 필드 추가 |
| `host/Source/Control/HttpApiServer.cpp` | `/api/auto/toggle` 엔드포인트 추가 |
| `host/Source/Control/WebSocketServer.cpp` | `auto_mode_toggle` 명령 추가 |
| `host/Source/Control/ControlMapping.cpp` | AutoModeToggle 액션 매핑 추가 (기본 핫키 없음 — 사용자 수동 추가) |
| `CMakeLists.txt` | RNNoise 라이브러리 + 새 소스 파일 등록 |
| `CLAUDE.md` | Key Implementations 섹션에 Auto 모드 컴포넌트 + 신호 흐름 분기 설명 추가 |
| `docs/QUICKSTART.md` | "Auto 모드로 즉시 시작" 옵션 추가 |
| `LICENSE` 또는 `THIRD_PARTY.md` | RNNoise BSD-3 라이선스 고지 추가 |

---

## 12. 구현 순서 / Implementation Order

```
Phase 1 (기반):
  1. RNNoise 빌드 통합 (CMake + thirdparty/)
  2. AutoFilter 구현 (HPF + LPF, IIRFilter)
  3. AutoNoiseRemoval 구현 (RNNoise 래퍼 + 리샘플러)
  4. AutoGainControl 구현 (느린 컴프레서 AGC)
  5. AutoModeProcessor 통합 (Filter → RNNoise → AGC 체인)
  6. AudioEngine 분기 로직

Phase 2 (UI):
  7. Auto 버튼 (입력 게인 슬라이더 옆)
  8. AutoModePanel (체인 에디터 영역 교체)
  9. FilterEditPanel, NoiseRemovalEditPanel, AGCEditPanel
  10. 프리셋 슬롯 비활성화 처리
  11. settings.dppreset 직렬화

Phase 3 (연동):
  12. StateBroadcaster에 auto_mode 상태 추가
  13. ActionDispatcher + ActionHandler (AutoModeToggle)
  14. HTTP API + WebSocket 명령
  15. 핫키 (Ctrl+Shift+A)
  16. 테스트 작성
  17. 문서 업데이트

Phase 4 (RNNoise 프리셋 튜닝):
  18. 프리셋별 EQ 커브 실제 튜닝 (실기기 테스트)
  19. dry/wet 비율 미세 조정
  20. 여성 고음 테스트 (보컬/노래 프리셋 검증)
```

---

## 13. 참고 사항 / Notes

- Auto 모드와 VST 체인은 **동시 동작하지 않음**. Auto ON이면 VST chain processBlock은 스킵됨. 이중 처리로 인한 음질 문제 원천 차단
- Auto 모드는 **VST 체인을 건드리지 않음**. Auto OFF 시 이전 VST 체인 상태 그대로 복귀
- RNNoise 480 프레임 단위 처리와 호스트 버퍼 크기 불일치 시 내부 FIFO 버퍼 필요 (예: 호스트 256 samples → 480까지 모은 후 처리 → 나머지 보관)
- 패닉 뮤트 중에도 Auto 모드 설정 변경 가능 (패닉 뮤트 잠금 대상 아님)
- Safety Limiter는 Auto/VST 모드 무관하게 항상 동작 — Auto 모드의 AGC가 과도하게 증폭해도 Safety Limiter가 최종 보호
- `ScopedNoDenormals`는 AudioEngine 콜백 최상단에서 이미 적용됨 → Auto 프로세서 내부에서 별도 처리 불필요
- Mono 모드에서도 workBuffer는 스테레오(채널 0 = 채널 1)로 처리됨 → RNNoise는 채널 0만 처리 후 채널 1에 복제, 또는 양 채널 독립 처리
- RNNoise 프레임 크기(480)와 호스트 버퍼 크기의 불일치는 `AutoNoiseRemoval` 내부에서 FIFO로 해결. 외부 인터페이스는 임의 버퍼 크기를 받을 수 있어야 함
