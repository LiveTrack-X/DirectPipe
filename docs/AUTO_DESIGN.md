# [Auto] 설계 문서 — Design Principles & Parameter Rationale

> DirectPipe의 [Auto] 버튼은 "VST를 모르는 사용자도 한 번에 방송 가능한 마이크 품질"을 목표로 설계되었습니다.
> 이 문서는 Auto 체인의 구성 원칙, 순서의 기술적 근거, 포함하지 않은 처리와 그 이유, 모든 파라미터의 선택 근거를 기록합니다.

> The [Auto] button in DirectPipe is designed with the goal of "broadcast-ready microphone quality in one click, even for users who know nothing about VSTs."
> This document records the design principles of the Auto chain, the technical rationale for processing order, what was intentionally excluded and why, and the reasoning behind every parameter choice.

---

## 1. 설계 원칙 / Design Principles

### Correction vs Shaping

오디오 처리는 두 가지로 나뉩니다:

Audio processing falls into two categories:

| | Correction (보정 / Correction) | Shaping (가공 / Shaping) |
|---|---|---|
| **목적 / Purpose** | 문제 제거 / Remove problems | 취향 반영 / Reflect taste |
| **예시 / Examples** | 소음 제거, 험 필터, 볼륨 안정화 / Noise removal, hum filtering, volume stabilization | EQ 톤 조절, 컴프레서 캐릭터, 리버브 / EQ tone shaping, compressor character, reverb |
| **마이크 의존도 / Mic dependency** | 낮음 (거의 모든 마이크에 유효) / Low (effective on nearly all mics) | 높음 (마이크·목소리마다 다름) / High (varies per mic and voice) |
| **잘못 적용 시 / If misapplied** | 효과 없음 (해 없음) / No effect (no harm) | 음질 악화 가능 / Can degrade audio quality |

**Auto는 Correction만 자동화합니다.** "모든 사용자에게 해 없이 도움이 되는 보정"만 포함합니다. Shaping은 사용자가 직접 VST 플러그인으로 추가하는 영역입니다.

**Auto only automates Correction.** It includes only corrections that "help all users without causing harm." Shaping is the domain where users add their own VST plugins.

이 경계를 지키는 이유:
- USB 콘덴서, 다이나믹, 헤드셋, 내장 마이크 등 모든 마이크 타입에서 안전해야 함
- 잘못된 자동 설정이 "아무것도 안 한 것보다 나쁜" 결과를 만들면 안 됨
- 사용자가 Auto를 켜고 아무 조정 없이 바로 방송/통화를 시작할 수 있어야 함

Why we maintain this boundary:
- Must be safe across all mic types: USB condenser, dynamic, headset, built-in, etc.
- Incorrect automatic settings must never produce results "worse than doing nothing"
- Users must be able to enable Auto and immediately start streaming/calling with no adjustments

### 실전 세팅에서 출발한 배경 / Background: Derived from Real-World Setups

Auto 체인은 이론이 아닌 실제 사용 패턴에서 출발했습니다:

The Auto chain was derived from real-world usage patterns, not theory:

1. **스트리머 / Streamers**: OBS + 콘덴서 마이크 → 에어컨 소음 제거 + 볼륨 일정하게 / OBS + condenser mic -> AC noise removal + consistent volume
2. **팟캐스터 / Podcasters**: 녹음 전 세팅 → 험 제거 + 노이즈 게이트 + 레벨러 / Pre-recording setup -> hum removal + noise gate + leveler
3. **게이머 / Gamers**: 키보드 소리 제거 + 음성 통화 볼륨 안정 / Keyboard noise removal + stable voice chat volume
4. **보이스챗 / Voice chat**: 조용한 목소리 증폭 + 배경 소음 억제 / Quiet voice amplification + background noise suppression

이 4가지 시나리오에서 **공통적으로 필요한 처리 3가지**가 Auto의 구성이 되었습니다.

The **3 commonly needed processes** across these 4 scenarios became the Auto chain.

---

## 2. 체인 구성과 순서 / Chain Composition and Order

```
Input → [1. Filter] → [2. Noise Removal] → [3. Auto Gain] → [Safety Limiter] → Output
                                                                  ↑
                                                          글로벌 (Auto 체인 외부)
                                                          Global (outside Auto chain)
```

### 순서의 기술적 근거 / Technical Rationale for Order

#### 1번: Filter (HPF + LPF) / Stage 1: Filter (HPF + LPF)

**가장 먼저 처리하는 이유**: 저주파 험(50/60Hz 전원 노이즈, 에어컨 진동, 마이크 핸들링 노이즈)은 이후 모든 처리에 영향을 줍니다.

**Why it is processed first**: Low-frequency hum (50/60Hz power noise, AC vibration, mic handling noise) affects all subsequent processing.

- Noise Removal 전에 제거해야 RNNoise가 저주파 에너지에 혼동하지 않음 / Must be removed before Noise Removal so RNNoise is not confused by low-frequency energy
- Auto Gain 전에 제거해야 LUFS 측정이 험에 의해 왜곡되지 않음 / Must be removed before Auto Gain so LUFS measurement is not skewed by hum
- 가장 가벼운 처리(IIR 필터)이므로 레이턴시 0 / Lightest processing (IIR filter), so zero latency

#### 2번: Noise Removal (RNNoise) / Stage 2: Noise Removal (RNNoise)

**필터 후, AGC 전에 위치하는 이유**: 깨끗한 신호에서 LUFS를 측정해야 정확합니다.

**Why it is placed after Filter and before AGC**: LUFS must be measured on a clean signal for accuracy.

- 소음이 섞인 상태에서 LUFS를 측정하면 AGC가 소음 레벨을 포함해서 게인을 계산 → 목소리가 의도보다 작아짐 / Measuring LUFS with noise mixed in causes AGC to include noise level in gain calculation -> voice becomes quieter than intended
- RNNoise는 48kHz 고정이므로 필터로 불필요한 대역을 미리 제거하면 뉴럴넷의 부담이 줄어듦 / Since RNNoise operates at a fixed 48kHz, pre-filtering unnecessary bands reduces the neural network's burden

#### 3번: Auto Gain (LUFS Leveler) / Stage 3: Auto Gain (LUFS Leveler)

**마지막에 위치하는 이유**: "깨끗하고 필터링된 신호"의 레벨을 측정해서 보정합니다.

**Why it is placed last**: It measures and corrects the level of the "clean, filtered signal."

- 모든 보정이 끝난 최종 신호를 대상으로 레벨링 / Leveling is applied to the final signal after all corrections are complete
- 게인 변경이 이전 처리에 영향을 주지 않음 (개방 루프) / Gain changes do not affect previous processing stages (open loop)

#### Safety Limiter — 글로벌 분리 / Safety Limiter — Global Separation

Safety Limiter는 Auto 체인에 포함되지 않고 **모든 출력 경로 앞에 글로벌로** 배치됩니다.

The Safety Limiter is not part of the Auto chain — it is placed **globally before all output paths**.

분리 이유:
- VST 플러그인 체인 전체(Auto 프로세서 + 사용자 VST)를 보호해야 함
- Auto만 보호하면 사용자가 추가한 VST의 클리핑은 방지 못 함
- 프리셋 전환 시 순간적인 레벨 점프를 잡아야 하는데, 이는 체인 전체 후에 해야 의미 있음
- 별도 on/off + ceiling 조절이 가능해야 함 (Auto와 독립적)

Reasons for separation:
- Must protect the entire VST plugin chain (Auto processors + user VSTs)
- Protecting only Auto would leave user-added VST clipping unguarded
- Must catch momentary level jumps during preset switching, which is only meaningful after the full chain
- Must allow independent on/off + ceiling control (independent from Auto)

---

## 3. 포함하지 않은 것과 그 이유 / What Was Excluded and Why

### EQ (이퀄라이저) / EQ (Equalizer)

**불포함 이유**: 목소리 톤은 마이크 + 사람마다 다릅니다.

**Reason for exclusion**: Voice tone varies per microphone and person.

- 콘덴서 마이크: 고역 부스트가 이미 있어서 추가 EQ는 과도 / Condenser mics: already have high-frequency boost, so additional EQ is excessive
- 다이나믹 마이크: 고역이 부족해서 EQ로 올려야 하지만, "얼마나"는 마이크 모델에 따라 다름 / Dynamic mics: lack highs and need EQ boost, but "how much" varies by model
- 저음 남성 vs 고음 여성: 같은 EQ가 한 쪽에게는 맞고 다른 쪽에게는 해로움 / Low-pitched male vs high-pitched female: the same EQ may suit one and harm the other

자동 EQ를 안전하게 하려면 마이크 프로파일 DB가 필요하고, 이는 Auto의 "원클릭" 철학과 맞지 않습니다.

Safe automatic EQ would require a microphone profile database, which conflicts with Auto's "one-click" philosophy.

### De-esser (치찰음 제거) / De-esser (Sibilance Removal)

**불포함 이유**: 치찰음(ㅅ, ㅆ, ㅈ) 주파수는 사람마다 4~10kHz 범위에서 다릅니다.

**Reason for exclusion**: Sibilance frequencies vary per person across the 4-10kHz range.

- 잘못된 주파수에 de-esser를 걸면 "리스프" 같은 부자연스러운 발음이 됨 / Applying a de-esser at the wrong frequency produces unnatural "lisp"-like pronunciation
- 한국어/일본어/영어의 치찰음 스펙트럼이 다름 / Sibilance spectra differ across Korean, Japanese, and English
- 자동 감지가 가능하지만 오탐 시 정상 발음이 억제되는 것이 "하지 않는 것"보다 나쁨 / Auto-detection is possible, but false positives suppressing normal speech is worse than doing nothing

### Compressor (다이나믹 압축) / Compressor (Dynamic Compression)

**불포함 이유**: Auto Gain이 사실상 대체합니다.

**Reason for exclusion**: Auto Gain effectively replaces it.

- 전통적 컴프레서: threshold 이상을 ratio로 압축 → 피크 제어 / Traditional compressor: compress above threshold by ratio -> peak control
- Auto Gain: LUFS 기반으로 전체 레벨을 타겟에 맞춤 → 평균 레벨 안정화 / Auto Gain: match overall level to target based on LUFS -> average level stabilization
- 컴프레서는 "캐릭터"가 있음 (attack/release에 따라 펀치감, 투명도 등) → Shaping 영역 / Compressors have "character" (punchiness, transparency depending on attack/release) -> Shaping domain
- Auto Gain의 50% boost / 90% cut 블렌드가 자연스러운 다이나믹을 유지하면서 레벨을 안정화 / Auto Gain's 50% boost / 90% cut blend stabilizes level while maintaining natural dynamics

### Reverb / Saturation / 기타 이펙트 / Reverb / Saturation / Other Effects

**불포함 이유**: 순수 취향 영역입니다.

**Reason for exclusion**: These are purely taste-based.

- 마이크 보정과 무관 / Unrelated to microphone correction
- 방송/통화에서는 오히려 명료도를 해침 / Can actually reduce clarity in streaming/calls
- 사용자가 원하면 VST 플러그인으로 직접 추가 / Users can add them via VST plugins if desired

---

## 4. 파라미터 근거 / Parameter Rationale

### Filter

| 파라미터 / Parameter | 값 / Value | 근거 / Rationale |
|---|---|---|
| **HPF** | **ON, 60Hz** | 인간 음성의 기본 주파수(fundamental)는 남성 ~85Hz, 여성 ~165Hz. 60Hz 이하에는 유용한 음성 정보가 없고, 에어컨·진동·전원 험만 존재. 80Hz로 올리면 저음 남성 목소리의 바디감 손실 위험 / Human voice fundamental: male ~85Hz, female ~165Hz. Below 60Hz there is no useful voice information — only AC, vibration, and power hum. Raising to 80Hz risks losing body in low-pitched male voices |
| **LPF** | **OFF, 16kHz** | 대부분의 마이크가 16kHz 이상을 자연히 감쇠. LPF를 켜면 에어/숨소리의 자연스러움이 줄어들 수 있음. 필요 시 사용자가 직접 활성화 / Most mics naturally attenuate above 16kHz. Enabling LPF can reduce the naturalness of air/breath sounds. Users can enable it manually if needed |

60Hz를 선택한 이유:
- 50Hz (유럽 전원) / 60Hz (미국/한국 전원) 험을 모두 제거
- 저음 남성 목소리 (85Hz fundamental)에 영향 없음
- 키보드 타이핑의 저주파 충격 (~30-80Hz)도 제거

Why 60Hz was chosen:
- Removes both 50Hz (European power) and 60Hz (US/Korean power) hum
- Does not affect low-pitched male voices (85Hz fundamental)
- Also removes low-frequency impact from keyboard typing (~30-80Hz)

### Noise Removal (RNNoise)

| 파라미터 / Parameter | 값 / Value | 근거 / Rationale |
|---|---|---|
| **Strength** | **Standard** | Light(0.50)는 배경 소음이 남음, Aggressive(0.90)는 목소리 끝이 잘림 / Light (0.50) leaves background noise, Aggressive (0.90) clips voice tails |
| **VAD threshold** | **0.70** | 일반 대화 속도에서 자연스러운 음성/비음성 구분. 0.60은 오탐 多, 0.80은 부드러운 발화 누락 / Natural voice/non-voice separation at normal speech pace. 0.60 has many false positives, 0.80 misses soft speech |
| **Hold time** | **300ms** | *(내부 상수, UI 미노출)* 단어 사이 자연스러운 쉼(200-400ms)을 커버. 너무 짧으면 단어마다 끊김, 너무 길면 소음 통과 / *(internal constant, not exposed in UI)* Covers natural pauses between words (200-400ms). Too short causes per-word clipping, too long lets noise through |
| **Gate smoothing** | **20ms** | *(내부 상수, UI 미노출)* 5ms는 클릭 발생, 50ms는 음절 시작이 뭉개짐. 20ms는 청각적으로 감지 불가한 페이드 / *(internal constant, not exposed in UI)* 5ms causes clicks, 50ms smears syllable onsets. 20ms is a perceptually undetectable fade |

Standard(0.70)를 기본으로 선택한 이유:
- Light: 에어컨/팬 소리는 제거하지만 키보드 타이핑은 통과
- **Standard: 키보드 + 에어컨 + 팬 모두 제거, 말소리 보존** ← 가장 넓은 사용 범위
- Aggressive: 조용한 말 끝부분이 잘릴 수 있음 (녹음용에는 부적합)

Why Standard (0.70) was chosen as default:
- Light: removes AC/fan noise but lets keyboard typing through
- **Standard: removes keyboard + AC + fan, preserves speech** <- widest usage range
- Aggressive: quiet speech tails may be clipped (unsuitable for recording)

### Auto Gain (LUFS Leveler)

| 파라미터 / Parameter | 값 / Value | 근거 / Rationale |
|---|---|---|
| **Target LUFS** | **-15** | 방송/팟캐스트 표준 (-14 ~ -16 LUFS). Spotify는 -14, YouTube는 -14, 팟캐스트는 -16 ~ -19. -15는 중간값으로 대부분의 플랫폼에서 적절 / Broadcast/podcast standard (-14 to -16 LUFS). Spotify: -14, YouTube: -14, podcasts: -16 to -19. -15 is the midpoint, suitable for most platforms |
| **Internal offset** | **-6dB** | 개방 루프 AGC의 구조적 오버슈트(4-6dB) 보상. 상용 레벨러(폐루프 + look-ahead)와 유사한 출력 레벨 달성 / Compensates for structural overshoot (4-6dB) in open-loop AGC. Achieves output levels similar to commercial levelers (closed-loop + look-ahead) |
| **Low Correct** | **0.50 (50%)** | 조용한 구간에서 게인을 절반 속도로 올림. 100%면 숨소리/쉼 구간까지 증폭 → 부자연스러운 "라디오 컴프" 느낌. 50%는 자연스러운 다이나믹 유지하면서 볼륨 안정화 / Raises gain at half speed during quiet sections. 100% amplifies breath/pause sections -> unnatural "radio comp" feel. 50% stabilizes volume while maintaining natural dynamics |
| **High Correct** | **0.90 (90%)** | 큰 소리를 빠르게 컷. 100%면 피크가 완전 평탄화 → 다이나믹 상실. 90%는 살짝의 자연스러운 피크를 허용 / Cuts loud sounds quickly. 100% completely flattens peaks -> loss of dynamics. 90% allows slight natural peaks |
| **Max Gain** | **22dB** | 저게인 다이나믹 마이크(SM58 등)에서 조용히 말하면 -40 ~ -50 dBFS. 타겟 -21 LUFS(내부)까지 올리려면 ~20dB 부스트 필요. 22dB는 이를 커버하면서 과도한 증폭 방지 / Quiet speech on low-gain dynamic mics (SM58, etc.) reads -40 to -50 dBFS. ~20dB boost needed to reach internal target of -21 LUFS. 22dB covers this while preventing excessive amplification |
| **Freeze Level** | **-45 dBFS** | 이 이하의 RMS는 무음/배경소음으로 판단하고 게인을 동결. -45 dBFS는 조용한 방에서 콘덴서 마이크의 셀프 노이즈(~-50 dBFS) 바로 위. 말이 끝난 후 소음을 증폭하지 않음 / RMS below this is treated as silence/background noise and gain is frozen. -45 dBFS is just above the self-noise of condenser mics in a quiet room (~-50 dBFS). Prevents noise amplification after speech ends |
| **LUFS window** | **0.4s** | EBU R128 Momentary 표준. 0.4초는 한 음절~단어 단위의 레벨 변화에 반응. 3초(EBU Short-term)는 너무 느려서 문장 내 볼륨 변화를 못 잡음 / EBU R128 Momentary standard. 0.4s responds to syllable-to-word-level changes. 3s (EBU Short-term) is too slow to catch intra-sentence volume changes |

> **추가 안전장치 / Additional safeguard**: Freeze Level(-45 dBFS) 외에 -65 dBFS 미만 시 게인 처리 자체를 완전 바이패스합니다. Freeze는 게인을 동결(유지)하지만, -65 미만은 게인 계산을 건너뜁니다.
> In addition to Freeze Level (-45 dBFS), gain processing is completely bypassed below -65 dBFS. Freeze holds the current gain, but below -65 the gain computation is skipped entirely.

Low/High Correct가 비대칭인 이유:
- 부스트(50%)를 보수적으로 → 조용한 구간에서 소음 증폭 방지
- 컷(90%)을 적극적으로 → 큰 소리(갑자기 웃음, 기침)를 빠르게 억제
- 이 비대칭이 "자연스럽게 들리면서 볼륨이 안정적인" 핵심

Why Low/High Correct are asymmetric:
- Conservative boost (50%) -> prevents noise amplification during quiet sections
- Aggressive cut (90%) -> quickly suppresses loud sounds (sudden laughter, coughing)
- This asymmetry is the key to "sounding natural while keeping volume stable"

### Safety Limiter

| 파라미터 / Parameter | 값 / Value | 근거 / Rationale |
|---|---|---|
| **Ceiling** | **-0.3 dBFS** | 0.0은 디지털 풀스케일 → DAC에서 ISP(inter-sample peak) 클리핑 발생 가능. -0.3은 ISP 마진. -1.0은 스트리밍 권장이지만 기본값으로는 과도한 여유 / 0.0 is digital full-scale -> ISP (inter-sample peak) clipping possible at DAC. -0.3 provides ISP margin. -1.0 is recommended for streaming but excessive headroom as default |
| **Attack** | **0.1ms** | 사실상 즉시 반응. 리미터는 "절대 넘지 않게"가 목적이므로 빠를수록 좋음 / Effectively instant response. A limiter's purpose is "never exceed," so faster is better |
| **Release** | **50ms** | 10ms는 펌핑(볼륨이 숨쉬는 것처럼 올라갔다 내려갔다), 200ms는 연속 피크에서 레벨이 너무 오래 눌림. 50ms는 투명하면서 충분히 빠른 복귀 / 10ms causes pumping (volume rises and falls like breathing), 200ms holds level down too long on consecutive peaks. 50ms is transparent with sufficiently fast recovery |
| **Enabled** | **기본 ON / Default ON** | 클리핑은 어떤 상황에서도 바람직하지 않음. 성능 영향 거의 없음 (per-sample peak comparison only) / Clipping is undesirable in any situation. Virtually no performance impact (per-sample peak comparison only) |

---

## 5. 변경 시 주의사항 / Cautions When Modifying Parameters

파라미터를 사용자 정의할 때 알아야 할 트레이드오프:

Trade-offs to be aware of when customizing parameters:

### Filter

| 변경 / Change | 결과 / Result | 주의 / Caution |
|---|---|---|
| HPF 60→80Hz | 더 많은 험/진동 제거 / More hum/vibration removed | **저음 남성 목소리 바디감 손실 가능 / May lose body in low-pitched male voices** (fundamental ~85Hz에 근접 / close to fundamental ~85Hz) |
| HPF 60→40Hz | 저음 보존 / Low-end preserved | 전원 험(50/60Hz)이 통과할 수 있음 / Power hum (50/60Hz) may pass through |
| LPF 활성화 (16kHz) / LPF enabled (16kHz) | 고주파 노이즈 제거 / High-frequency noise removed | 에어/숨소리 자연스러움 감소, 콘덴서 마이크 특유의 "밝은" 톤 손실 / Reduced naturalness of air/breath sounds, loss of condenser mic's characteristic "bright" tone |

### Noise Removal

| 변경 / Change | 결과 / Result | 주의 / Caution |
|---|---|---|
| Light (0.50) | 부드러운 말 끝 보존 / Soft speech tails preserved | 키보드 타이핑 소리 통과 가능 / Keyboard typing noise may pass through |
| Aggressive (0.90) | 강력한 소음 억제 / Strong noise suppression | **조용한 발화 끝이 잘릴 수 있음 / Quiet speech tails may be clipped** (녹음 시 주의 / use caution when recording) |
| Hold time 단축 *(코드 수정 필요)* / Hold time shortened *(requires code change)* | 빠른 게이트 반응 / Faster gate response | 단어 사이에서 "숨소리 끊김" 현상 / "Breath cutoff" between words |

### Auto Gain

| 변경 / Change | 결과 / Result | 주의 / Caution |
|---|---|---|
| Max Gain ↓ (22→12dB) | 과도한 증폭 방지 / Prevents excessive amplification | **저게인 마이크(SM58, 먼 거리)에서 볼륨 부족 / Insufficient volume on low-gain mics (SM58, far distance)** |
| Max Gain ↑ (22→30dB) | 더 넓은 게인 범위 / Wider gain range | 배경 소음 증폭 위험 증가, Freeze Level 조정 필요 / Increased risk of background noise amplification, Freeze Level adjustment needed |
| Low Correct ↑ (50→90%) | 조용한 구간도 적극 부스트 / Quiet sections actively boosted | **숨소리/쉼 구간 증폭 → 부자연스러운 "라디오 컴프" / Breath/pause amplification -> unnatural "radio comp"** |
| Low/High 대칭 (50/50 또는 90/90) / Low/High symmetric (50/50 or 90/90) | 부스트와 컷이 같은 속도 / Boost and cut at same speed | **자연스러운 다이나믹 상실 / Loss of natural dynamics** — 말하기가 평탄하게 들림 / speech sounds flat |
| Freeze Level ↑ (-45→-35 dBFS) | 더 많은 구간에서 게인 동결 / Gain frozen in more sections | **조용히 말하는 구간에서 게인이 올라가지 않음 / Gain does not rise during quiet speech** |
| Freeze Level ↓ (-45→-55 dBFS) | 더 적은 구간에서 동결 / Gain frozen in fewer sections | 무음 구간에서 배경 소음 증폭 / Background noise amplified during silence |
| Target LUFS ↑ (-15→-10) | 더 큰 출력 / Louder output | Safety Limiter가 빈번히 작동, 다이나믹 압축 / Safety Limiter activates frequently, dynamic compression |
| Target LUFS ↓ (-15→-20) | 더 작은 출력 / Quieter output | 수신자 측에서 볼륨을 올려야 함 / Receivers need to turn up their volume |

### Safety Limiter

| 변경 / Change | 결과 / Result | 주의 / Caution |
|---|---|---|
| Ceiling ↑ (-0.3→0.0 dBFS) | 최대 볼륨 / Maximum volume | ISP 클리핑 가능 (DAC/코덱 의존적) / ISP clipping possible (DAC/codec dependent) |
| Ceiling ↓ (-0.3→-3.0 dBFS) | 넉넉한 헤드룸 / Generous headroom | 체감 볼륨 감소, AGC 타겟과의 갭 증가 / Perceived volume decrease, increased gap from AGC target |
| Disabled | 클리핑 방지 없음 / No clipping protection | **프리셋 전환 시 순간 피크로 클리핑 발생 가능 / Momentary peaks during preset switching may cause clipping** |
