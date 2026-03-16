# Feature Spec: Built-in Processors (내장 프로세서)

> **DirectPipe v5.0.0 구현 명세** — Claude Code 핸드오프용
>
> 이 문서는 DirectPipe에 "내장 프로세서" (Filter, Noise Removal, Auto Gain)를 추가하기 위한 설계 명세입니다.
> 내장 프로세서는 VST 플러그인과 동일하게 체인에 추가/제거/바이패스/순서 변경이 가능합니다.
> 기존 코드베이스의 아키텍처(`CLAUDE.md` Key Implementations 섹션)와 스레드 안전 규칙을 준수해야 합니다.
>
> **의존성**: Safety Limiter (`FEATURE_SAFETY_LIMITER.md`) 구현 완료 필요. (✅ 완료됨)

---

## 1. 목적 / Purpose

VST 플러그인 없이도 기본적인 마이크 처리가 가능하도록 **내장 오디오 프로세서 3종**을 제공.

### 핵심 컨셉

- Filter, Noise Removal, Auto Gain을 **VST 플러그인처럼** 체인에 추가
- 일반 VST와 동일한 UI (Edit/Bypass/Remove 버튼, 드래그 순서 변경)
- VST 플러그인과 자유롭게 조합 가능
- **[Auto] 버튼**: 3개를 한번에 추가하는 편의 기능
- 프리셋 슬롯 A-E에 내장 프로세서 포함하여 자연스럽게 저장/복원

### 타겟 사용자

| 사용자 | 시나리오 |
|--------|---------|
| VST를 모르는 사용자 | [Auto] 누르면 Filter+RNNoise+AGC 즉시 추가 |
| VST와 조합하는 사용자 | RNNoise → ReaComp → ProQ 같은 혼합 체인 |
| 빠른 설정이 필요한 사용자 | 플러그인 스캔 없이 내장 프로세서로 즉시 사용 |

### 디자인 원칙

1. **VST와 동일한 인터페이스** — AudioProcessor 상속, AudioProcessorGraph에 삽입
2. **기존 UI 100% 재활용** — PluginChainEditor 그대로 사용. 별도 패널 없음
3. **크로스 플랫폼** — RNNoise(순수 C), Filter/AGC(순수 C++) 플랫폼 의존성 없음
4. **RT-safe** — 오디오 콜백에서 힙 할당, 뮤텍스, I/O 없음
5. **프리셋 호환** — A-E 슬롯에 내장 프로세서 포함 저장/복원

---

## 2. 아키텍처 / Architecture

### 내장 프로세서 = AudioProcessor 서브클래스

각 내장 프로세서는 JUCE `AudioProcessor`를 상속하여 AudioProcessorGraph에 일반 VST와 동일하게 삽입.

```
AudioProcessorGraph
  ├── [Input Node]
  ├── BuiltinFilter (AudioProcessor)      ← 내장
  ├── BuiltinNoiseRemoval (AudioProcessor) ← 내장 (RNNoise)
  ├── SomeVSTPlugin (AudioPluginInstance)   ← VST
  ├── BuiltinAutoGain (AudioProcessor)     ← 내장
  ├── AnotherVSTPlugin (AudioPluginInstance) ← VST
  └── [Output Node]
```

### VSTChain PluginSlot 확장

```cpp
struct PluginSlot {
    juce::String name;
    juce::String path;
    juce::PluginDescription desc;
    bool bypassed = false;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::AudioPluginInstance* instance = nullptr;  // VST용

    // 내장 프로세서 지원 추가
    enum class Type { VST, BuiltinFilter, BuiltinNoiseRemoval, BuiltinAutoGain };
    Type type = Type::VST;
    std::unique_ptr<juce::AudioProcessor> builtinProcessor;  // 내장용
};
```

### 신호 흐름

```
Mic → Input Gain → AudioProcessorGraph (내장+VST 혼합 체인) → Safety Limiter → 출력
                   ├── Filter (내장, 선택)
                   ├── RNNoise (내장, 선택)
                   ├── ReaComp (VST, 선택)
                   ├── AutoGain (내장, 선택)
                   └── ProQ (VST, 선택)
```

AudioEngine 오디오 콜백은 변경 없음 — AudioProcessorGraph가 내부적으로 순서대로 처리.

---

## 3. [Auto] 프리셋 슬롯 / Auto Preset Slot

### 개념

**[Auto] = 특수 프리셋 슬롯 (6번째 슬롯)**. A-E와 동일한 매커니즘으로 동작하지만:
- **이름 변경 불가** — 항상 "Auto"
- **초기화 기능** — Reset 시 Filter→Noise Removal→Auto Gain + 기본 파라미터로 복원
- 그 외에는 A-E와 완전히 동일 (VST 추가, 순서 변경, 바이패스, 저장/복원)

### UI 위치

```
┌─INPUT──────────────────────────────────┐
│ 레벨미터 │ 게인 슬라이더  │ [Auto] 버튼  │  ← 입력 게인 옆
└────────────────────────────────────────┘

┌─프리셋 슬롯──────────────────────────────┐
│ [A] [B] [C] [D] [E]                     │  ← 기존 슬롯
└──────────────────────────────────────────┘
```

[Auto]는 프리셋 바(A-E)와 별도 위치에 배치되지만, 내부적으로는 프리셋 슬롯 인덱스 5로 동작.

### 동작

**[Auto] 클릭**: 현재 체인을 Auto 슬롯의 저장된 체인으로 전환 (A→B 전환과 동일)
- 첫 사용 시 (Auto 슬롯이 비어있을 때): Filter + Noise Removal + Auto Gain 기본 체인 생성
- 이후: 마지막으로 저장된 Auto 슬롯 상태 로드 (VST 포함 가능)

**우클릭 컨텍스트 메뉴**:
- Reset to Defaults — Filter→Noise Removal→Auto Gain + 기본 파라미터로 초기화
- Copy / Paste — 다른 슬롯과 체인 복사
- ~~Rename~~ — 비활성화 (이름 변경 불가)

### PresetSlotBar 변경

```cpp
// 기존: kNumPresetSlots = 5 (A-E)
// 변경: kNumPresetSlots = 6 (A-E + Auto)
static constexpr int kAutoSlotIndex = 5;

// Auto 슬롯은 이름 변경 불가
bool isRenameable(int slotIndex) { return slotIndex != kAutoSlotIndex; }

// Auto 슬롯 초기화 (Reset to Defaults)
void resetAutoSlot();  // Filter + NR + AGC 기본 체인 생성 + 저장

// ⚠️ 프리셋 순환(Next/Previous Preset)에서 Auto 슬롯 제외
// A→B→C→D→E→A 로테이션에 Auto가 포함되지 않음
// Auto는 명시적으로 [Auto] 버튼을 눌러야만 활성화
bool isInRotation(int slotIndex) { return slotIndex < kAutoSlotIndex; }
```

### "Add Plugin" 메뉴 확장

어떤 슬롯에서든 내장 프로세서를 개별적으로 추가할 수 있음:

```
[+ Add Plugin]
  ├── Built-in
  │   ├── Filter (HPF + LPF)
  │   ├── Noise Removal (RNNoise)
  │   └── Auto Gain (LUFS AGC)
  ├── VST3
  │   └── (스캔된 플러그인 목록)
  ├── VST2
  │   └── (스캔된 플러그인 목록)
  └── Add from file...
```

> **참고**: 내장 프로세서는 Auto 슬롯뿐 아니라 A-E 슬롯에서도 추가 가능. "Add Plugin" 메뉴는 슬롯에 무관하게 동일.

---

## 4. 체인 에디터 표시 / Chain Editor Display

내장 프로세서는 VST와 동일한 행 UI. 차이점은 Edit 클릭 시 네이티브 GUI 대신 DirectPipe 자체 설정 패널이 열림.

```
┌─플러그인 체인──────────────────────────────────┐
│ 1. Filter (내장)           [Edit][Bp][X]      │
│ 2. Noise Removal (내장)    [Edit][Bp][X]      │
│ 3. ReaComp                 [Edit][Bp][X]      │  ← VST
│ 4. Auto Gain (내장)        [Edit][Bp][X]      │
│ 5. FabFilter ProQ          [Edit][Bp][X]      │  ← VST
├────────────────────────────────────────────────┤
│ Chain PDC: 512 samples (10.7ms @ 48kHz)        │
│ [Safety Limiter ✓]                             │
│ [+ Add Plugin]  [Scan...]  [Remove]            │
└────────────────────────────────────────────────┘
```

| 동작 | VST | 내장 프로세서 |
|------|-----|-------------|
| Edit 버튼 | 네이티브 플러그인 GUI | DirectPipe 자체 설정 패널 |
| Bypass 버튼 | 동일 | 동일 |
| Remove (X) 버튼 | 동일 | 동일 (삭제 가능) |
| 드래그 순서 변경 | 동일 | 동일 |
| 프리셋 저장/복원 | 동일 | 동일 (getStateInformation/setStateInformation) |

### 내장 프로세서 이름 표시

체인 에디터에서 "(내장)" 접미사로 구분:
- `Filter (내장)` 또는 아이콘으로 구분
- VST 이름은 플러그인이 리포트하는 이름 그대로

---

## 5. 내장 프로세서 설계 / Built-in Processor Design

### 5.1 BuiltinFilter (HPF + LPF)

**파일**: `host/Source/Audio/BuiltinFilter.h/cpp`

`juce::AudioProcessor` 상속. Edit 패널에서 HPF/LPF 제어.

```
┌──────────────────────────────────────┐
│ Filter                               │
│                                      │
│ Low Cut (HPF):  [✓ on]               │
│   [▼ 프리셋 드롭다운]                 │
│     60Hz  — 극저음만 제거 (기본)       │
│     80Hz  — 에어컨/환풍기 제거         │
│     120Hz — 강한 저역 차단             │
│     커스텀                            │
│   커스텀: [████████░░] 20~300Hz       │
│                                      │
│ High Cut (LPF): [□ off]              │
│   [▼ 프리셋 드롭다운]                 │
│     16kHz — 가벼운 고역 제거           │
│     12kHz — 표준 고역 제거             │
│     8kHz  — 강한 고역 차단             │
│     커스텀                            │
│   커스텀: [████████░░] 4~20kHz        │
└──────────────────────────────────────┘
```

**파라미터:**

| 파라미터 | 타입 | 기본값 | 범위 | 설명 |
|---------|------|--------|------|------|
| `hpf_enabled` | bool | true | on/off | HPF 활성화 (기본 ON) |
| `hpf_frequency` | float | 60.0 | 20~300 Hz | HPF 컷오프 주파수 |
| `lpf_enabled` | bool | false | on/off | LPF 활성화 (기본 OFF) |
| `lpf_frequency` | float | 16000.0 | 4000~20000 Hz | LPF 컷오프 주파수 |

**구현**: JUCE `IIRFilter` — 2차 버터워스. RT-safe. `getLatencySamples()` = 0.

### 5.2 BuiltinNoiseRemoval (RNNoise)

**파일**: `host/Source/Audio/BuiltinNoiseRemoval.h/cpp`

`juce::AudioProcessor` 상속. RNNoise 래퍼 + VAD 기반 노이즈 게이팅.

```
┌──────────────────────────────────────┐
│ Noise Removal                        │
│                                      │
│ Strength:  [▼ 드롭다운]              │
│   약 (Light)      — 최소 게이팅       │
│   중 (Standard)   — 표준 (기본)       │
│   강 (Aggressive) — 강한 게이팅       │
│                                      │
│ [▶ 고급 설정]                         │
│ ┌─고급 설정──────────────────────┐    │
│ │ VAD Threshold:                │    │
│ │ [0.60] ████████░░ 슬라이더     │    │
│ └────────────────────────────────┘    │
│                                      │
│ Status: Active                       │
└──────────────────────────────────────┘
```

**프리셋 (VAD 기반 노이즈 게이팅):**

RNNoise는 항상 100% 처리 (dry/wet 믹싱 없음 — 위상 왜곡 방지).

| 프리셋 | VAD 임계값 | 설명 |
|--------|-----------|------|
| 약 (Light) | 0.35 | 게이팅 느슨. 잔여 노이즈 약간 남지만 음성 손실 최소 |
| 중 (Standard) | 0.60 | 업계 표준 (werman VST 기본값). 대부분의 환경에 적합 |
| 강 (Aggressive) | 0.85 | 강한 게이팅. 매우 깨끗하지만 작은 소리 잘릴 수 있음 |

**파라미터:**

| 파라미터 | 타입 | 기본값 | 범위 | 설명 |
|---------|------|--------|------|------|
| `rnn_strength` | enum | Standard | Light/Standard/Aggressive | 노이즈 제거 강도 |
| `rnn_vad_threshold` | float | 0.60 | 0.0~1.0 | VAD 임계값 (고급 설정에서 직접 조절) |

**RNNoise 기술:**
- 48kHz 고정. 다른 SR에서는 JUCE `LagrangeInterpolator`로 리샘플링
- 480 프레임 단위 처리 → 내부 FIFO 버퍼 필요 (사전 할당, RT-safe)
- Dual-mono (채널별 독립 처리, RNNoise 인스턴스 2개)
- `getLatencySamples()` = FIFO에 의한 추가 레이턴시 리포트 (PDC 표시에 반영됨)
- `rnnoise_create()`는 `prepareToPlay`에서만 호출 (힙 할당이므로 RT 콜백 금지)
- `rnnoise_process_frame()` 반환값 = VAD 확률 → 게이팅에 사용

**왜 dry/wet 믹싱을 사용하지 않는가:**
- FIFO로 인한 원본/처리 신호 위상 불일치 → 빗살 필터(comb filtering) → 목소리 왜곡
- 100% wet 사용으로 위상 문제 완전 회피

### 5.3 BuiltinAutoGain (LUFS AGC)

**파일**: `host/Source/Audio/BuiltinAutoGain.h/cpp`

`juce::AudioProcessor` 상속. LUFS 기반 비대칭 보정 AGC (Luveler Mode 2 스타일).

```
┌──────────────────────────────────────┐
│ Auto Gain Control                    │
│                                      │
│ Target:                              │
│ [-15 LUFS] ████████████░░░░ 슬라이더  │
│            -24        -6             │
│                                      │
│ Current: -14.2 LUFS                  │
│ Gain: +3.2 dB                        │
│                                      │
│ [▶ 고급 설정]                         │
│ ┌─고급 설정──────────────────────┐    │
│ │ Low Correct:  [50%] ██████░░  │    │
│ │ High Correct: [75%] ████████░░│    │
│ │ Max Gain: [+18 dB] ██████░░   │    │
│ └────────────────────────────────┘    │
│                                      │
│ Status: Active                       │
└──────────────────────────────────────┘
```

**파라미터:**

| 파라미터 | 타입 | 기본값 | 범위 | UI | 설명 |
|---------|------|--------|------|-----|------|
| `agc_target_LUFS` | float | -15.0 | -24 ~ -6 LUFS | 슬라이더 | 타겟 라우드니스 |
| `agc_low_correct` | float | 0.50 | 0.0 ~ 1.0 | 고급 설정 | 조용할 때 보정 비율 |
| `agc_high_correct` | float | 0.75 | 0.0 ~ 1.0 | 고급 설정 | 클 때 보정 비율 |
| `agc_max_gain_dB` | float | 18.0 | 6 ~ 30 dB | 고급 설정 | 최대 증폭 상한 |

**LUFS 측정 (ITU-R BS.1770):**
- K-weighting 프리필터 (sidechain — 측정용, 실제 오디오에 적용 안 함)
- Short-term LUFS (3초 슬라이딩 윈도우)
- `getLatencySamples()` = 0 (look-ahead 없음)

**비대칭 보정 (Luveler Mode 2):**
```
correction = target_LUFS - measured_LUFS
if correction > 0:  // 조용함
    gain = correction × lowCorrectRatio (0.50)
else:               // 큼
    gain = correction × highCorrectRatio (0.75)
gain = clamp(gain, -maxGain, +maxGain)
```

---

## 6. AudioProcessor 인터페이스 구현 / AudioProcessor Interface

모든 내장 프로세서가 공통으로 구현해야 하는 JUCE AudioProcessor 메서드:

```cpp
class BuiltinFilter : public juce::AudioProcessor {
public:
    // 필수 구현
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void releaseResources() override;

    // 에디터 (DirectPipe 자체 설정 패널)
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // 상태 직렬화 (프리셋 저장/복원에 사용)
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // 메타데이터
    const juce::String getName() const override { return "Filter"; }
    int getLatencySamples() const override { return 0; }

    // 기타 필수 (기본 구현)
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() const override { return 1; }
    // ... etc
};
```

**getStateInformation / setStateInformation**:
- JSON 직렬화로 파라미터 저장/복원
- 프리셋 슬롯 A-E의 기존 메커니즘 (getStateInformation → base64) 그대로 활용
- 내장 프로세서도 VST와 동일하게 슬롯에 저장됨

---

## 7. VSTChain 변경 / VSTChain Changes

### PluginSlot 확장

```cpp
struct PluginSlot {
    // ... 기존 필드 ...

    enum class Type { VST, BuiltinFilter, BuiltinNoiseRemoval, BuiltinAutoGain };
    Type type = Type::VST;

    // VST: instance != nullptr, builtinProcessor == nullptr
    // 내장: instance == nullptr, builtinProcessor != nullptr
    std::unique_ptr<juce::AudioProcessor> builtinProcessor;

    // 공통 접근자
    juce::AudioProcessor* getProcessor() const {
        return (type == Type::VST)
            ? static_cast<juce::AudioProcessor*>(instance)
            : builtinProcessor.get();
    }
};
```

### 내장 프로세서 추가 메서드

```cpp
// 내장 프로세서를 체인에 추가
ActionResult addBuiltinProcessor(PluginSlot::Type type, int insertIndex = -1);

// Auto 버튼: Filter + NoiseRemoval + AutoGain 한번에 추가
ActionResult addAutoProcessors();
```

### 프리셋 직렬화

기존 프리셋 저장 흐름:
```
각 PluginSlot → getStateInformation() → base64 인코딩 → JSON 저장
```

내장 프로세서도 동일:
```json
{
  "plugins": [
    {
      "type": "builtin_filter",
      "name": "Filter",
      "bypassed": false,
      "state": "base64_encoded_state..."
    },
    {
      "type": "builtin_noise_removal",
      "name": "Noise Removal",
      "bypassed": false,
      "state": "base64_encoded_state..."
    },
    {
      "type": "vst3",
      "name": "ReaComp",
      "path": "C:/VST3/ReaComp.vst3",
      "bypassed": false,
      "state": "base64_encoded_state..."
    },
    {
      "type": "builtin_auto_gain",
      "name": "Auto Gain",
      "bypassed": false,
      "state": "base64_encoded_state..."
    }
  ]
}
```

---

## 8. 외부 제어 연동 / External Control

### StateBroadcaster

기존 `plugins[]` 배열에 내장 프로세서도 포함됨 (type 필드 추가):
```json
{
  "plugins": [
    { "name": "Filter", "type": "builtin_filter", "bypass": false, "loaded": true, "latency_samples": 0 },
    { "name": "Noise Removal", "type": "builtin_noise_removal", "bypass": false, "loaded": true, "latency_samples": 480 },
    { "name": "ReaComp", "type": "vst3", "bypass": false, "loaded": true, "latency_samples": 0 }
  ]
}
```

### ActionDispatcher

| 액션 | 파라미터 | 설명 |
|------|---------|------|
| `AutoProcessorsAdd` | — | [Auto] 버튼 — Filter+RNNoise+AGC 추가 |

기존 `PluginBypass` 액션은 내장 프로세서에도 동일하게 동작 (인덱스 기반).

### HTTP API

| 엔드포인트 | 설명 |
|-----------|------|
| `GET /api/auto/add` | Filter+RNNoise+AGC 체인에 추가 |

기존 `/api/plugins` 응답에 `type` 필드 추가.

---

## 9. RNNoise 빌드 통합 / RNNoise Build Integration

### CMake — 소스 직접 포함 (권장)

```cmake
# thirdparty/rnnoise/ 에 소스 직접 포함 (오프라인 빌드 보장, 버전 고정)
add_library(rnnoise STATIC
    thirdparty/rnnoise/src/denoise.c
    thirdparty/rnnoise/src/rnn.c
    thirdparty/rnnoise/src/rnn_data.c
    thirdparty/rnnoise/src/pitch.c
    thirdparty/rnnoise/src/celt_lpc.c
)
target_include_directories(rnnoise PUBLIC thirdparty/rnnoise/include)
target_link_libraries(DirectPipe PRIVATE rnnoise)
```

- RNNoise: **BSD-3-Clause** — GPL-3.0 호환
- 순수 C (libm만 의존) → Windows/macOS/Linux 전부 빌드
- 라이선스 고지: `THIRD_PARTY.md` 또는 About 다이얼로그에 추가

---

## 10. 로깅 / Logging

```
INF [VST] Built-in processor added: "Filter" at index 0
INF [VST] Built-in processor added: "Noise Removal" at index 1
INF [VST] Auto processors added: Filter + Noise Removal + Auto Gain
INF [AUDIO] RNNoise resampling active (44100Hz → 48000Hz → 44100Hz)
WRN [AUDIO] Auto Gain at max gain (+18.0 dB) — consider increasing input gain
```

내장 프로세서는 기존 `[VST]` 카테고리로 로깅 (별도 카테고리 불필요).

---

## 11. 테스트 / Tests

### Unit Tests

`test_builtin_processors.cpp`:

| 테스트 | 설명 |
|--------|------|
| `FilterHPF` | HPF 활성 시 저역 감쇠 확인 |
| `FilterLPF` | LPF 활성 시 고역 감쇠 확인 |
| `FilterBypass` | bypass 시 신호 변경 없음 |
| `FilterStateRoundtrip` | getStateInformation → setStateInformation 동일값 |
| `NoiseRemovalVADGatingLight` | VAD 0.35에서 잔여 노이즈 통과 확인 |
| `NoiseRemovalVADGatingAggressive` | VAD 0.85에서 조용한 구간 감쇠 확인 |
| `NoiseRemovalBypass` | bypass 시 신호 변경 없음 |
| `NoiseRemovalStateRoundtrip` | 상태 직렬화 왕복 확인 |
| `AGCBelowTargetLUFS` | 입력 < 타겟 → Low Correct 비율로 게인 증가 |
| `AGCAboveTargetLUFS` | 입력 > 타겟 → High Correct 비율로 게인 감소 |
| `AGCMaxGainClamp` | 최대 게인 상한선 동작 확인 |
| `AGCBypass` | bypass 시 신호 변경 없음 |
| `LUFSMeasurement` | K-weighting + 3초 윈도우 LUFS 측정 정확도 |
| `AsymmetricCorrection` | Low/High Correct 비대칭 보정 확인 |
| `ChainWithBuiltinAndVST` | 내장+VST 혼합 체인에서 순서대로 처리 확인 |
| `PresetSaveLoadBuiltin` | 내장 프로세서 포함 프리셋 저장/복원 확인 |
| `AutoButtonAddsThree` | Auto 버튼 클릭 → 3개 추가 확인 |
| `ResamplerActivation` | 44.1kHz에서 RNNoise 리샘플러 활성화 확인 |

### Pre-Release Dashboard

- [ ] [Auto] 버튼 → Filter + Noise Removal + Auto Gain 3개 추가되는지
- [ ] 내장 프로세서 Edit → 자체 설정 패널 열리는지
- [ ] 내장 프로세서 Bypass → 신호 통과 확인
- [ ] 내장 프로세서 Remove → 체인에서 제거 확인
- [ ] 내장 + VST 혼합 체인 → 순서대로 처리 확인
- [ ] 드래그로 내장 프로세서 순서 변경 가능
- [ ] 프리셋 슬롯에 내장 프로세서 포함 저장/복원
- [ ] Noise Removal 약/중/강 프리셋 전환 동작
- [ ] AGC 타겟 LUFS 슬라이더 동작, 실시간 게인 표시
- [ ] 44.1kHz 장치에서 RNNoise 정상 동작 (리샘플링)
- [ ] 48kHz 장치에서 RNNoise 정상 동작 (리샘플링 없음)

---

## 12. 파일 변경 요약 / File Changes

### 새 파일

| 파일 | 설명 |
|------|------|
| `host/Source/Audio/BuiltinFilter.h/cpp` | 내장 HPF + LPF (AudioProcessor 상속) |
| `host/Source/Audio/BuiltinNoiseRemoval.h/cpp` | RNNoise 래퍼 + VAD 게이팅 (AudioProcessor 상속) |
| `host/Source/Audio/BuiltinAutoGain.h/cpp` | LUFS 기반 AGC (AudioProcessor 상속) |
| `host/Source/UI/FilterEditPanel.h/cpp` | Filter Edit 패널 (AudioProcessorEditor 상속) |
| `host/Source/UI/NoiseRemovalEditPanel.h/cpp` | Noise Removal Edit 패널 |
| `host/Source/UI/AGCEditPanel.h/cpp` | AGC Edit 패널 |
| `tests/test_builtin_processors.cpp` | 유닛 테스트 |
| `thirdparty/rnnoise/` | RNNoise 소스 |

### 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `host/Source/Audio/VSTChain.h/cpp` | PluginSlot::Type 추가, addBuiltinProcessor(), addAutoProcessors() |
| `host/Source/UI/PluginChainEditor.h/cpp` | "Add Plugin" 메뉴에 Built-in 서브메뉴 추가 |
| `host/Source/UI/MainComponent.h/cpp` | [Auto] 버튼 추가, 클릭 시 addAutoProcessors() 호출 |
| `host/Source/UI/PresetManager.cpp` | 내장 프로세서 타입 직렬화/역직렬화 |
| `host/Source/Control/ActionDispatcher.h` | AutoProcessorsAdd 액션 추가 |
| `host/Source/Control/ActionHandler.cpp` | AutoProcessorsAdd 핸들링 |
| `host/Source/Control/HttpApiServer.cpp` | /api/auto/add 엔드포인트 |
| `host/Source/Control/WebSocketServer.cpp` | auto_add 명령 |
| `host/Source/Control/StateBroadcaster.cpp` | plugins[] 배열에 type 필드 추가 |
| `CMakeLists.txt` | RNNoise 라이브러리 + 새 소스 파일 등록 |
| `CLAUDE.md` | Built-in Processors 설명 추가 |

---

## 13. 구현 순서 / Implementation Order

```
Phase 1 (기반):
  1. RNNoise 빌드 통합 (CMake + thirdparty/)
  2. BuiltinFilter 구현 (AudioProcessor 상속, HPF+LPF)
  3. BuiltinNoiseRemoval 구현 (RNNoise 래퍼 + FIFO + VAD 게이팅)
  4. BuiltinAutoGain 구현 (LUFS 측정 + 비대칭 보정)

Phase 2 (체인 통합):
  5. VSTChain PluginSlot 확장 (Type, builtinProcessor)
  6. addBuiltinProcessor() / addAutoProcessors()
  7. 프리셋 직렬화 (type 필드 추가, 하위 호환)

Phase 3 (UI):
  8. FilterEditPanel, NoiseRemovalEditPanel, AGCEditPanel
  9. "Add Plugin" 메뉴에 Built-in 서브메뉴
  10. [Auto] 버튼 (MainComponent)

Phase 4 (연동 + 테스트):
  11. StateBroadcaster type 필드
  12. ActionDispatcher + HTTP/WS
  13. 테스트 작성
  14. 문서 업데이트

Phase 5 (튜닝):
  15. VAD 임계값 프리셋 미세 조정 (실기기 테스트)
  16. LUFS K-weighting 계수 검증
  17. 다양한 마이크/환경 테스트
```

---

## 14. 참고 사항 / Notes

- 내장 프로세서는 **일반 VST와 동일하게 AudioProcessorGraph에 삽입** — VSTChain의 processBlock, rebuildGraph 등 기존 코드를 최대한 재활용
- **RNNoise FIFO 레이턴시**: 480 프레임 고정으로 ~5-10ms 추가 레이턴시 발생. `getLatencySamples()`로 리포트하여 Per-Plugin Latency Display에 자동 표시
- **`rnnoise_create()`는 prepareToPlay에서만 호출** — 힙 할당이므로 RT 콜백 금지
- **K-weighting 필터 계수**: 48kHz 기준 ITU-R BS.1770 정의. 다른 SR에서는 bilinear transform으로 재계산 필요
- Safety Limiter는 AudioProcessorGraph 밖에서 동작 (이미 구현됨) — 내장 프로세서와 무관하게 항상 적용
- `ScopedNoDenormals`는 AudioEngine 콜백 최상단에서 적용됨 → 내장 프로세서 내부에서 별도 처리 불필요
- 내장 프로세서의 `getStateInformation/setStateInformation`은 JSON 직렬화 사용 (VST의 바이너리 상태와 달리 사람이 읽을 수 있음)
- RNNoise 프레임 크기(480)와 호스트 버퍼 크기 불일치 → BuiltinNoiseRemoval 내부 FIFO로 해결
- 내장 프로세서를 여러 개 추가하는 것은 허용 (예: Filter 2개) — 금지할 필요 없음
