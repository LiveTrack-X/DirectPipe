# Safety Limiter Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** VST 체인 직후 모든 출력 경로에 적용되는 글로벌 안전 리미터를 추가한다.

**Architecture:** SafetyLimiter 클래스(RT-safe, feed-forward, look-ahead 없음)를 AudioEngine에 삽입. VSTChain processBlock 직후, Recording/IPC/Monitor/Output 앞에 배치. OutputPanel에 UI 추가. StateBroadcaster + HTTP/WS로 외부 제어 연동.

**Tech Stack:** C++17, JUCE 7.0.12, std::atomic (RT↔UI 통신)

**Spec:** Integrated into `docs/PRODUCT_SPEC.md` Section 4.1.8 and `CLAUDE.md` Key Implementations

**Base:** `DirectPipe-v4/host/Source/`

**Version:** 현재 4.0.0 — 이 기능은 v4.1.0 릴리스 대상

---

## File Structure

### 신규 파일
| 파일 | 책임 |
|------|------|
| `Audio/SafetyLimiter.h` | 리미터 클래스 선언 — atomic 파라미터, RT-safe process() |
| `Audio/SafetyLimiter.cpp` | 리미터 구현 — 엔벨로프 팔로워, prepareToPlay, 직렬화 |
| `tests/test_safety_limiter.cpp` | 유닛 테스트 11개 |

### 수정 파일
| 파일 | 변경 |
|------|------|
| `Audio/AudioEngine.h` | SafetyLimiter 멤버 + getter |
| `Audio/AudioEngine.cpp` | process() 호출 삽입 (line 961-963 사이), prepareToPlay 연결 |
| `UI/OutputPanel.h/cpp` | Safety Limiter 섹션 (enable toggle + ceiling slider + GR 표시) |
| `UI/StatusUpdater.cpp` | AppState에 safety limiter 필드 매핑 |
| `Control/StateBroadcaster.h/cpp` | AppState 확장 + toJSON + quickStateHash |
| `Control/ActionDispatcher.h` | SafetyLimiterToggle + SetSafetyLimiterCeiling 액션 |
| `Control/ActionHandler.cpp` | 핸들러 추가 |
| `Control/ControlMapping.cpp` | varToActionEvent 상한 업데이트 |
| `Control/HttpApiServer.cpp` | /api/limiter/toggle, /api/limiter/ceiling/:value |
| `Control/WebSocketServer.cpp` | safety_limiter_toggle, set_safety_limiter_ceiling 메시지 |
| `UI/PresetManager.cpp` | safetyLimiter 저장/복원 |
| `UI/MainComponent.cpp` | 상태 바 LIM 인디케이터 |
| `tests/CMakeLists.txt` | test_safety_limiter.cpp 등록 |
| `CMakeLists.txt` | SafetyLimiter.cpp 등록 |
| `CLAUDE.md` | Key Implementations, Thread Table, Modules 업데이트 |

---

## Chunk 1: SafetyLimiter Core + AudioEngine Integration

### Task 1: SafetyLimiter 클래스 작성

**Files:**
- Create: `host/Source/Audio/SafetyLimiter.h`
- Create: `host/Source/Audio/SafetyLimiter.cpp`

- [ ] **Step 1: SafetyLimiter.h 작성**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace directpipe {

/**
 * @brief Feed-forward safety limiter — RT-safe, no look-ahead.
 *
 * Inserted after VST chain, before all output paths (Recording/IPC/Monitor/Main).
 * Prevents unexpected clipping from plugin parameter changes or preset switches.
 *
 * Thread Ownership:
 *   process()               — [RT audio thread only]
 *   prepareToPlay()         — [Message thread] (JUCE convention)
 *   setEnabled/setCeiling() — [Any thread] (atomic writes)
 *   isEnabled/getCeiling/getCurrentGainReduction/isLimiting() — [Any thread] (atomic reads)
 */
class SafetyLimiter {
public:
    SafetyLimiter();

    void prepareToPlay(double sampleRate);

    /** Process audio buffer in-place. RT-safe — no alloc, no mutex, no logging. */
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    // Parameter setters (atomic, any thread)
    void setEnabled(bool enabled);
    void setCeiling(float dB);  // -6.0 ~ 0.0 dBFS

    // State getters (atomic, any thread)
    bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }
    float getCeilingdB() const { return ceilingdB_.load(std::memory_order_relaxed); }
    float getCurrentGainReduction() const { return gainReduction_dB_.load(std::memory_order_relaxed); }
    bool isLimiting() const { return gainReduction_dB_.load(std::memory_order_relaxed) < -0.1f; }

private:
    std::atomic<bool> enabled_{true};
    std::atomic<float> ceilingLinear_{0.9661f};  // dBtoLinear(-0.3)
    std::atomic<float> ceilingdB_{-0.3f};

    // Envelope state — RT audio thread only, non-atomic
    float currentGain_ = 1.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    // UI feedback — written by RT, read by UI
    std::atomic<float> gainReduction_dB_{0.0f};

    static constexpr float kAttackMs = 0.1f;
    static constexpr float kReleaseMs = 50.0f;
};

} // namespace directpipe
```

- [ ] **Step 2: SafetyLimiter.cpp 작성**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "SafetyLimiter.h"

namespace directpipe {

SafetyLimiter::SafetyLimiter()
{
    // Default coefficients for 48kHz
    prepareToPlay(48000.0);
}

void SafetyLimiter::prepareToPlay(double sampleRate)
{
    if (sampleRate <= 0.0) sampleRate = 48000.0;
    attackCoeff_  = std::exp(-1.0f / static_cast<float>(sampleRate * kAttackMs * 0.001));
    releaseCoeff_ = std::exp(-1.0f / static_cast<float>(sampleRate * kReleaseMs * 0.001));
    currentGain_ = 1.0f;
    gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
}

void SafetyLimiter::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!enabled_.load(std::memory_order_relaxed)) {
        gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float ceiling = ceilingLinear_.load(std::memory_order_relaxed);
    const int numChannels = buffer.getNumChannels();
    const float aCoeff = attackCoeff_;
    const float rCoeff = releaseCoeff_;

    float peakGR = 1.0f;  // track worst-case GR for UI feedback

    for (int i = 0; i < numSamples; ++i) {
        // Peak detection across all channels
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            float s = std::abs(buffer.getSample(ch, i));
            if (s > peak) peak = s;
        }

        // Compute target gain
        float targetGain = (peak > ceiling) ? (ceiling / peak) : 1.0f;

        // Envelope follower
        if (targetGain < currentGain_)
            currentGain_ = aCoeff * currentGain_ + (1.0f - aCoeff) * targetGain;
        else
            currentGain_ = rCoeff * currentGain_ + (1.0f - rCoeff) * targetGain;

        // Apply gain to all channels
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, buffer.getSample(ch, i) * currentGain_);

        if (currentGain_ < peakGR) peakGR = currentGain_;
    }

    // Update UI feedback (worst GR in this block, converted to dB)
    float grDB = (peakGR < 0.9999f) ? (20.0f * std::log10(peakGR)) : 0.0f;
    gainReduction_dB_.store(grDB, std::memory_order_relaxed);
}

void SafetyLimiter::setEnabled(bool enabled)
{
    enabled_.store(enabled, std::memory_order_relaxed);
}

void SafetyLimiter::setCeiling(float dB)
{
    dB = juce::jlimit(-6.0f, 0.0f, dB);
    ceilingdB_.store(dB, std::memory_order_relaxed);
    ceilingLinear_.store(juce::Decibels::decibelsToGain(dB), std::memory_order_relaxed);
}

} // namespace directpipe
```

- [ ] **Step 3: Commit**

```
feat(audio): add SafetyLimiter class — RT-safe feed-forward limiter
```

---

### Task 2: AudioEngine에 SafetyLimiter 통합

**Files:**
- Modify: `host/Source/Audio/AudioEngine.h`
- Modify: `host/Source/Audio/AudioEngine.cpp`

- [ ] **Step 1: AudioEngine.h — include + 멤버 + getter 추가**

include 블록에:
```cpp
#include "SafetyLimiter.h"
```

멤버 (private, 기존 `AudioRecorder recorder_` 근처):
```cpp
    SafetyLimiter safetyLimiter_;
```

public getter:
```cpp
    SafetyLimiter& getSafetyLimiter() { return safetyLimiter_; }
```

- [ ] **Step 2: AudioEngine.cpp — process() 호출 삽입**

현재 line 961-963:
```cpp
    vstChain_.processBlock(buffer, numSamples);

    // 2.5. Write processed audio to recorder (lock-free)
    recorder_.writeBlock(buffer, numSamples);
```

사이에 삽입:
```cpp
    // 2.1. Safety Limiter — clip prevention for all output paths (RT-safe)
    safetyLimiter_.process(buffer, numSamples);
```

결과:
```
vstChain_.processBlock → safetyLimiter_.process → recorder_.writeBlock → IPC → Monitor → Output
```

- [ ] **Step 3: AudioEngine.cpp — prepareToPlay 연결**

`audioDeviceAboutToStart` 메서드에서, 기존 `vstChain_.prepareToPlay()` 호출 근처에:
```cpp
    safetyLimiter_.prepareToPlay(sampleRate);
```

- [ ] **Step 4: CMakeLists.txt — SafetyLimiter.cpp 등록**

`host/CMakeLists.txt` 또는 최상위 CMakeLists.txt에서 host source files 목록에 추가:
```
host/Source/Audio/SafetyLimiter.cpp
```

- [ ] **Step 5: 빌드 확인 + Commit**

```
feat(audio): integrate SafetyLimiter into AudioEngine callback
```

---

### Task 3: 유닛 테스트 작성

**Files:**
- Create: `tests/test_safety_limiter.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: test_safety_limiter.cpp 작성**

```cpp
#include <gtest/gtest.h>
#include "../host/Source/Audio/SafetyLimiter.h"

using namespace directpipe;

class SafetyLimiterTest : public ::testing::Test {
protected:
    SafetyLimiter limiter;

    void SetUp() override {
        limiter.prepareToPlay(48000.0);
    }

    // Helper: create buffer with constant value
    juce::AudioBuffer<float> makeBuffer(float value, int numSamples = 512, int channels = 2) {
        juce::AudioBuffer<float> buf(channels, numSamples);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buf.setSample(ch, i, value);
        return buf;
    }
};

TEST_F(SafetyLimiterTest, DefaultState) {
    EXPECT_TRUE(limiter.isEnabled());
    EXPECT_FLOAT_EQ(limiter.getCeilingdB(), -0.3f);
    EXPECT_FALSE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, DisabledPassthrough) {
    limiter.setEnabled(false);
    auto buf = makeBuffer(2.0f);  // way above ceiling
    limiter.process(buf, buf.getNumSamples());
    // Signal should be unchanged
    EXPECT_FLOAT_EQ(buf.getSample(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(limiter.getCurrentGainReduction(), 0.0f);
}

TEST_F(SafetyLimiterTest, BelowCeiling) {
    auto buf = makeBuffer(0.5f);  // well below default ceiling (-0.3 dBFS ≈ 0.966)
    limiter.process(buf, buf.getNumSamples());
    // Should be unchanged (or very close)
    EXPECT_NEAR(buf.getSample(0, 256), 0.5f, 0.01f);
}

TEST_F(SafetyLimiterTest, AboveCeiling) {
    limiter.setCeiling(-6.0f);  // ceiling = 0.5012
    auto buf = makeBuffer(1.0f, 2048);  // above ceiling
    limiter.process(buf, buf.getNumSamples());
    // After envelope settles, output should be near ceiling
    float lastSample = buf.getSample(0, buf.getNumSamples() - 1);
    EXPECT_LT(lastSample, 0.55f);  // should be limited near 0.5
}

TEST_F(SafetyLimiterTest, CeilingRange) {
    limiter.setCeiling(-10.0f);  // below min
    EXPECT_FLOAT_EQ(limiter.getCeilingdB(), -6.0f);  // clamped

    limiter.setCeiling(5.0f);  // above max
    EXPECT_FLOAT_EQ(limiter.getCeilingdB(), 0.0f);  // clamped
}

TEST_F(SafetyLimiterTest, GainReductionFeedback) {
    limiter.setCeiling(-6.0f);
    auto buf = makeBuffer(1.0f, 1024);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_LT(limiter.getCurrentGainReduction(), -0.1f);  // negative dB = gain reduction
}

TEST_F(SafetyLimiterTest, IsLimiting) {
    limiter.setCeiling(-6.0f);
    auto buf = makeBuffer(1.0f, 1024);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_TRUE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, NotLimitingBelowCeiling) {
    auto buf = makeBuffer(0.1f, 512);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_FALSE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, SampleRateChange) {
    limiter.prepareToPlay(96000.0);
    // Just verify no crash and defaults reset
    EXPECT_FLOAT_EQ(limiter.getCurrentGainReduction(), 0.0f);

    limiter.prepareToPlay(44100.0);
    auto buf = makeBuffer(1.0f, 512);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_TRUE(limiter.isLimiting());  // still works at different SR
}

TEST_F(SafetyLimiterTest, MonoBuffer) {
    auto buf = makeBuffer(1.0f, 512, 1);  // mono
    limiter.setCeiling(-6.0f);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_LT(buf.getSample(0, buf.getNumSamples() - 1), 0.55f);
}

TEST_F(SafetyLimiterTest, ZeroCeiling) {
    limiter.setCeiling(0.0f);
    auto buf = makeBuffer(0.99f, 512);
    limiter.process(buf, buf.getNumSamples());
    // 0.99 is below 1.0 (0 dBFS) — should pass through
    EXPECT_NEAR(buf.getSample(0, 256), 0.99f, 0.02f);
}
```

- [ ] **Step 2: tests/CMakeLists.txt에 등록**

`target_sources(directpipe-host-tests PRIVATE` 블록에 추가:
```
        # Slice 5: Safety Limiter
        test_safety_limiter.cpp
```

그리고 host source files 목록에:
```
        ${CMAKE_SOURCE_DIR}/host/Source/Audio/SafetyLimiter.cpp
```

- [ ] **Step 3: 빌드 + 테스트 실행 + Commit**

```
test(audio): add SafetyLimiter unit tests (11 cases)
```

---

## Chunk 2: Settings Persistence + StateBroadcaster

### Task 4: PresetManager 저장/복원

**Files:**
- Modify: `host/Source/UI/PresetManager.cpp`

- [ ] **Step 1: saveToFile()에 safetyLimiter 섹션 추가**

기존 설정 저장 코드 끝 근처에:
```cpp
    // Safety Limiter
    auto limiterObj = new juce::DynamicObject();
    auto& limiter = engine_.getSafetyLimiter();
    limiterObj->setProperty("enabled", limiter.isEnabled());
    limiterObj->setProperty("ceiling_dB", static_cast<double>(limiter.getCeilingdB()));
    root->setProperty("safetyLimiter", juce::var(limiterObj));
```

- [ ] **Step 2: loadFromFile()에 safetyLimiter 복원 추가**

기존 설정 로드 코드에:
```cpp
    // Safety Limiter (v4.1.0+ — missing key = defaults)
    if (auto* limiterObj = root->getProperty("safetyLimiter").getDynamicObject()) {
        auto& limiter = engine_.getSafetyLimiter();
        if (limiterObj->hasProperty("enabled"))
            limiter.setEnabled(static_cast<bool>(limiterObj->getProperty("enabled")));
        if (limiterObj->hasProperty("ceiling_dB"))
            limiter.setCeiling(static_cast<float>(static_cast<double>(limiterObj->getProperty("ceiling_dB"))));
    }
```

- [ ] **Step 3: Commit**

```
feat(preset): persist SafetyLimiter enabled/ceiling in settings
```

---

### Task 5: StateBroadcaster + StatusUpdater 확장

**Files:**
- Modify: `host/Source/Control/StateBroadcaster.h`
- Modify: `host/Source/Control/StateBroadcaster.cpp`
- Modify: `host/Source/UI/StatusUpdater.cpp`

- [ ] **Step 1: AppState에 safety limiter 필드 추가**

StateBroadcaster.h의 AppState struct에:
```cpp
    // Safety Limiter
    bool limiterEnabled = true;
    float limiterCeilingdB = -0.3f;
    float limiterGainReduction = 0.0f;
    bool limiterActive = false;
```

- [ ] **Step 2: quickStateHash()에 포함**

```cpp
    h = h * 31u + static_cast<uint32_t>(s.limiterEnabled);
    hashFloat(s.limiterCeilingdB);
    hashFloat(s.limiterGainReduction);
    h = h * 31u + static_cast<uint32_t>(s.limiterActive);
```

- [ ] **Step 3: toJSON()에 직렬화**

```cpp
    // Safety Limiter
    auto limiterJson = new juce::DynamicObject();
    limiterJson->setProperty("enabled", state.limiterEnabled);
    limiterJson->setProperty("ceiling_dB", static_cast<double>(state.limiterCeilingdB));
    limiterJson->setProperty("gain_reduction_dB", static_cast<double>(state.limiterGainReduction));
    limiterJson->setProperty("is_limiting", state.limiterActive);
    data->setProperty("safety_limiter", juce::var(limiterJson));
```

- [ ] **Step 4: StatusUpdater.cpp — 필드 매핑**

updateState 람다에:
```cpp
        auto& limiter = engine_.getSafetyLimiter();
        s.limiterEnabled = limiter.isEnabled();
        s.limiterCeilingdB = limiter.getCeilingdB();
        s.limiterGainReduction = limiter.getCurrentGainReduction();
        s.limiterActive = limiter.isLimiting();
```

- [ ] **Step 5: Commit**

```
feat(state): broadcast SafetyLimiter status to WebSocket clients
```

---

## Chunk 3: UI (OutputPanel + 상태 바)

### Task 6: OutputPanel에 Safety Limiter 섹션 추가

**Files:**
- Modify: `host/Source/UI/OutputPanel.h`
- Modify: `host/Source/UI/OutputPanel.cpp`

- [ ] **Step 1: OutputPanel.h — 멤버 추가**

기존 Recording section 멤버 앞에:
```cpp
    // ── Safety Limiter section ──
    juce::Label limiterHeaderLabel_{"", "Safety Limiter"};
    juce::ToggleButton limiterEnableBtn_{"Enable"};
    juce::Slider limiterCeilingSlider_;
    juce::Label limiterCeilingLabel_{"", "Ceiling:"};
    juce::Label limiterGRLabel_;  // "GR: -2.3 dB" 실시간 표시
```

private 메서드:
```cpp
    void onLimiterEnableChanged();
    void onLimiterCeilingChanged();
```

- [ ] **Step 2: OutputPanel.cpp — 초기화 (생성자)**

Output Volume 슬라이더 초기화 뒤에:
```cpp
    // Safety Limiter section
    limiterHeaderLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    limiterHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(limiterHeaderLabel_);

    limiterEnableBtn_.setToggleState(engine_.getSafetyLimiter().isEnabled(), juce::dontSendNotification);
    limiterEnableBtn_.onClick = [this] { onLimiterEnableChanged(); };
    addAndMakeVisible(limiterEnableBtn_);

    limiterCeilingLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(limiterCeilingLabel_);

    limiterCeilingSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    limiterCeilingSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    limiterCeilingSlider_.setRange(-6.0, 0.0, 0.1);
    limiterCeilingSlider_.setValue(engine_.getSafetyLimiter().getCeilingdB(), juce::dontSendNotification);
    limiterCeilingSlider_.setTextValueSuffix(" dBFS");
    limiterCeilingSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(0xFFFF6B6B));
    limiterCeilingSlider_.setColour(juce::Slider::trackColourId, juce::Colour(0xFFFF6B6B).withAlpha(0.4f));
    limiterCeilingSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour(kSurfaceColour).brighter(0.1f));
    limiterCeilingSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(kTextColour));
    limiterCeilingSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    limiterCeilingSlider_.onValueChange = [this] { onLimiterCeilingChanged(); };
    addAndMakeVisible(limiterCeilingSlider_);

    limiterGRLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF6B6B));
    limiterGRLabel_.setFont(juce::Font(11.0f));
    addAndMakeVisible(limiterGRLabel_);
```

- [ ] **Step 3: OutputPanel.cpp — resized() 레이아웃**

Recording section 레이아웃 앞에 Safety Limiter 행 추가:
```cpp
    // ── Safety Limiter ──
    limiterHeaderLabel_.setBounds(x, y, w, rowH);
    y += rowH;
    limiterEnableBtn_.setBounds(x, y, 80, rowH);
    limiterCeilingLabel_.setBounds(x + 85, y, 50, rowH);
    limiterCeilingSlider_.setBounds(x + 135, y, w - 135, rowH);
    y += rowH + gap;
    limiterGRLabel_.setBounds(x, y, w, rowH);
    y += rowH + gap;
```

- [ ] **Step 4: OutputPanel.cpp — 콜백 구현**

```cpp
void OutputPanel::onLimiterEnableChanged()
{
    engine_.getSafetyLimiter().setEnabled(limiterEnableBtn_.getToggleState());
    if (onSettingsChanged) onSettingsChanged();
}

void OutputPanel::onLimiterCeilingChanged()
{
    engine_.getSafetyLimiter().setCeiling(static_cast<float>(limiterCeilingSlider_.getValue()));
    if (onSettingsChanged) onSettingsChanged();
}
```

- [ ] **Step 5: OutputPanel.cpp — timerCallback에서 GR 표시 갱신**

기존 타이머 콜백에서 output volume 동기화 뒤에:
```cpp
    // Safety Limiter GR display
    auto& limiter = engine_.getSafetyLimiter();
    float gr = limiter.getCurrentGainReduction();
    if (gr < -0.1f)
        limiterGRLabel_.setText("GR: " + juce::String(gr, 1) + " dB", juce::dontSendNotification);
    else
        limiterGRLabel_.setText("", juce::dontSendNotification);

    // Sync enable button with actual state (external control may change it)
    if (limiterEnableBtn_.getToggleState() != limiter.isEnabled())
        limiterEnableBtn_.setToggleState(limiter.isEnabled(), juce::dontSendNotification);
```

- [ ] **Step 6: Commit**

```
feat(ui): add Safety Limiter section to OutputPanel
```

---

### Task 7: 상태 바 LIM 인디케이터

**Files:**
- Modify: `host/Source/UI/StatusUpdater.cpp`
- Modify: `host/Source/MainComponent.cpp` (또는 StatusUpdater에서 직접)

- [ ] **Step 1: StatusUpdater에서 상태 바 라벨 갱신**

StatusUpdater의 tick()에서, 기존 xrun 표시 근처에 LIM 인디케이터 추가.
LIM 표시를 위한 Label 포인터를 StatusUpdater에 추가하거나, 기존 formatLabel을 활용.

간단한 접근: StatusUpdater의 format 라벨 텍스트에 리미팅 상태 포함:
```cpp
    // Limiter indicator in status bar
    if (engine_.getSafetyLimiter().isLimiting()) {
        // Append to existing format label or use separate label
    }
```

⚠️ 기존 UI 구조를 읽고 가장 자연스러운 위치에 배치. MainComponent의 상태 바 라벨 배열 확인 필요.

- [ ] **Step 2: Commit**

```
feat(ui): add LIM indicator to status bar when safety limiter is active
```

---

## Chunk 4: External Control (Action + HTTP + WebSocket)

### Task 8: ActionDispatcher + ActionHandler

**Files:**
- Modify: `host/Source/Control/ActionDispatcher.h`
- Modify: `host/Source/Control/ActionHandler.cpp`
- Modify: `host/Source/Control/ControlMapping.cpp`

- [ ] **Step 1: Action enum에 추가**

XRunReset 뒤에:
```cpp
    SafetyLimiterToggle,       ///< Toggle safety limiter on/off
    SetSafetyLimiterCeiling,   ///< Set safety limiter ceiling (floatParam = dB, -6.0~0.0)
```

- [ ] **Step 2: actionToString() + actionToDisplayName()에 case 추가**

- [ ] **Step 3: ActionHandler.cpp — handle()에 case 추가**

```cpp
        case Action::SafetyLimiterToggle: {
            auto& limiter = engine_.getSafetyLimiter();
            limiter.setEnabled(!limiter.isEnabled());
            if (onDirty) onDirty();
            break;
        }
        case Action::SetSafetyLimiterCeiling: {
            engine_.getSafetyLimiter().setCeiling(event.floatParam);
            if (onDirty) onDirty();
            break;
        }
```

⚠️ Safety limiter 설정은 panic mute 중에도 변경 가능 — `isMuted()` 가드 없음.

- [ ] **Step 4: ControlMapping.cpp — varToActionEvent 상한 업데이트**

`SetSafetyLimiterCeiling`이 마지막 enum이면 상한을 이에 맞게 변경.

- [ ] **Step 5: Commit**

```
feat(action): add SafetyLimiterToggle and SetSafetyLimiterCeiling actions
```

---

### Task 9: HTTP API + WebSocket

**Files:**
- Modify: `host/Source/Control/HttpApiServer.cpp`
- Modify: `host/Source/Control/WebSocketServer.cpp`

- [ ] **Step 1: HttpApiServer — 2개 엔드포인트 추가**

```cpp
    // GET /api/limiter/toggle
    if (action == "limiter" && segments.size() >= 3 && segments[2] == "toggle") {
        dispatcher_.dispatch({Action::SafetyLimiterToggle});
        return {200, R"({"ok": true, "action": "safety_limiter_toggle"})"};
    }

    // GET /api/limiter/ceiling/:value
    if (action == "limiter" && segments.size() >= 4 && segments[2] == "ceiling") {
        float value = juce::String(segments[3]).getFloatValue();
        if (value < -6.0f || value > 0.0f)
            return {400, R"({"error": "ceiling must be -6.0 to 0.0"})"};
        dispatcher_.dispatch({Action::SetSafetyLimiterCeiling, 0, value});
        return {200, R"({"ok": true, "action": "set_ceiling", "value": )" + std::to_string(value) + "}"};
    }
```

- [ ] **Step 2: WebSocketServer — 2개 메시지 핸들러 추가**

```cpp
    } else if (actionStr == "safety_limiter_toggle") {
        event.action = Action::SafetyLimiterToggle;
    } else if (actionStr == "set_safety_limiter_ceiling") {
        event.action = Action::SetSafetyLimiterCeiling;
        event.floatParam = params ? static_cast<float>(static_cast<double>(params->getProperty("value"))) : -0.3f;
```

- [ ] **Step 3: Commit**

```
feat(control): add HTTP/WebSocket endpoints for Safety Limiter
```

---

### Task 10: 문서 업데이트

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/CONTROL_API.md`

- [ ] **Step 1: CLAUDE.md 업데이트**

- Key Implementations > Audio 섹션에 Safety Limiter 설명 추가
- Thread Ownership Table에 SafetyLimiter 항목 추가
- Modules > Audio 목록에 SafetyLimiter 추가
- ActionHandler의 panic mute 차단 목록에서 SafetyLimiterToggle/SetSafetyLimiterCeiling 제외 명시

- [ ] **Step 2: docs/CONTROL_API.md 업데이트**

- state JSON에 `safety_limiter` 객체 추가
- HTTP 엔드포인트: /api/limiter/toggle, /api/limiter/ceiling/:value
- WebSocket 명령: safety_limiter_toggle, set_safety_limiter_ceiling

- [ ] **Step 3: Commit**

```
docs: document Safety Limiter in CLAUDE.md and CONTROL_API.md
```

---

## Implementation Order

1. **Chunk 1** (Task 1-3): Core — SafetyLimiter 클래스 + AudioEngine 통합 + 테스트
2. **Chunk 2** (Task 4-5): Persistence — PresetManager + StateBroadcaster
3. **Chunk 3** (Task 6-7): UI — OutputPanel 섹션 + 상태 바
4. **Chunk 4** (Task 8-10): External Control — Action + HTTP/WS + 문서

Chunk 1이 완료되면 빌드+테스트 확인 후 나머지 진행.
