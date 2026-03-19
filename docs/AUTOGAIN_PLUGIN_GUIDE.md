# AutoGain 독립 플러그인 분리 가이드 / AutoGain Standalone Plugin Extraction Guide

> DirectPipe 내장 BuiltinAutoGain을 독립 VST2/VST3/AU 플러그인으로 빌드하는 방법.
> 필요 시 이 가이드를 따라 분리하면 됩니다.
>
> How to build the built-in BuiltinAutoGain as a standalone VST2/VST3/AU plugin.
> Follow this guide when you need to extract it.

## 현재 상태 / Current State

`BuiltinAutoGain`은 이미 `juce::AudioProcessor`를 완전 구현:
/ `BuiltinAutoGain` already fully implements `juce::AudioProcessor`:

```
host/Source/Audio/BuiltinAutoGain.h/.cpp  — DSP 코어 + AudioProcessor / DSP core + AudioProcessor
host/Source/UI/AGCEditPanel.h/.cpp        — 에디터 UI / Editor UI
```

구현된 기능:
/ Implemented features:
- K-weighting ITU-R BS.1770 사이드체인 / K-weighting ITU-R BS.1770 sidechain
- 0.4초 EBU Momentary LUFS 윈도우 (증분 계산) / 0.4s EBU Momentary LUFS window (incremental calculation)
- 듀얼 엔벨로프 (fast 10ms + slow LUFS, max 선택) / Dual envelope (fast 10ms + slow LUFS, max selection)
- 프리즈 게이트 (무음 시 게인 유지) / Freeze gate (holds gain during silence)
- 파라미터: Target LUFS, Low/High Correct, Max Gain, Freeze Level / Parameters: Target LUFS, Low/High Correct, Max Gain, Freeze Level
- getStateInformation/setStateInformation (프리셋 저장/로드) / getStateInformation/setStateInformation (preset save/load)
- isBusesLayoutSupported (모노 + 스테레오) / isBusesLayoutSupported (mono + stereo)

## 분리 절차 / Extraction Procedure

### Step 1: 디렉토리 생성 / Create Directory Structure

```
plugins/autogain/
  CMakeLists.txt
  Source/
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp
```

### Step 2: CMakeLists.txt

```cmake
juce_add_plugin(DirectPipeAutoGain
    PRODUCT_NAME "DirectPipe AutoGain"
    COMPANY_NAME "LiveTrack"
    PLUGIN_MANUFACTURER_CODE Lvtk
    PLUGIN_CODE Dagn
    FORMATS VST3 VST AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VERSION "${PROJECT_VERSION}"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DirectPipeAutoGain PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_link_libraries(DirectPipeAutoGain PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
)

target_compile_definitions(DirectPipeAutoGain PUBLIC
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
```

### Step 3: PluginProcessor — BuiltinAutoGain 래핑 / Wrapping BuiltinAutoGain

두 가지 방법:
/ Two approaches:

**방법 A (빠름)**: `BuiltinAutoGain.h/.cpp`를 그대로 복사하고 클래스명만 변경.
/ **Approach A (quick)**: Copy `BuiltinAutoGain.h/.cpp` as-is, only rename the class.

**방법 B (클린)**: APVTS(AudioProcessorValueTreeState) 연동으로 DAW 오토메이션 지원.
/ **Approach B (clean)**: Integrate with APVTS (AudioProcessorValueTreeState) for DAW automation support.

```cpp
// PluginProcessor.h
class AutoGainProcessor : public juce::AudioProcessor {
public:
    AutoGainProcessor();
    // ... 기존 BuiltinAutoGain의 모든 메서드 동일
    // ... all methods identical to existing BuiltinAutoGain

    // 추가: APVTS로 DAW 오토메이션 지원
    // Added: APVTS for DAW automation support
    juce::AudioProcessorValueTreeState apvts;

private:
    // 기존 BuiltinAutoGain 멤버 전부 복사
    // Copy all existing BuiltinAutoGain members
    // atomic 파라미터를 APVTS 파라미터로 교체
    // Replace atomic parameters with APVTS parameters
};
```

APVTS 파라미터 레이아웃:
/ APVTS parameter layout:

```cpp
static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    return {
        std::make_unique<juce::AudioParameterFloat>(
            "target", "Target LUFS",
            juce::NormalisableRange<float>(-24.0f, -6.0f, 0.1f), -15.0f),
        std::make_unique<juce::AudioParameterFloat>(
            "lowCorrect", "Boost Blend",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.50f),
        std::make_unique<juce::AudioParameterFloat>(
            "highCorrect", "Cut Blend",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.90f),
        std::make_unique<juce::AudioParameterFloat>(
            "maxGain", "Max Gain (dB)",
            // Range wider than host default (22dB) for DAW use cases.
            // Above 30dB: risk of background noise amplification — see AUTO_DESIGN.md
            juce::NormalisableRange<float>(0.0f, 40.0f, 0.5f), 22.0f),
        std::make_unique<juce::AudioParameterFloat>(
            "freeze", "Freeze Level (dBFS)",
            juce::NormalisableRange<float>(-60.0f, -20.0f, 0.5f), -45.0f),
    };
}
```

### Step 4: PluginEditor — UI 개선 / UI Improvements

`AGCEditPanel`을 기반으로 하되:
/ Based on `AGCEditPanel`, with these changes:
- 리사이즈 가능하게 변경 (`setResizable(true, true)`) / Make resizable (`setResizable(true, true)`)
- 추천 크기: 400x300 / Recommended size: 400x300
- LUFS 미터 + 게인 리덕션 미터 추가 (시각적 피드백) / Add LUFS meter + gain reduction meter (visual feedback)
- 프리셋 드롭다운 (Voice/Podcast/Music/Aggressive) / Preset dropdown (Voice/Podcast/Music/Aggressive)

### Step 5: 루트 CMakeLists.txt에 연결 / Connect to Root CMakeLists.txt

```cmake
# plugins/autogain/ (독립 AutoGain VST / standalone AutoGain VST)
option(BUILD_AUTOGAIN_PLUGIN "Build standalone AutoGain VST plugin" OFF)
if(BUILD_AUTOGAIN_PLUGIN)
    add_subdirectory(plugins/autogain)
endif()
```

빌드: `cmake -B build -DBUILD_AUTOGAIN_PLUGIN=ON`
/ Build: `cmake -B build -DBUILD_AUTOGAIN_PLUGIN=ON`

### Step 6: CI 추가 (선택) / Add CI (Optional)

`.github/workflows/build.yml`에 job 추가:
/ Add a job to `.github/workflows/build.yml`:

```yaml
build-autogain:
    if: # 조건부 (태그에 "autogain" 포함 시만 등) / conditional (e.g. only when tag contains "autogain")
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [windows-latest, macos-14, ubuntu-24.04]
```

## 핵심 변경 포인트 / Key Changes Summary

| 현재 (BuiltinAutoGain) / Current (BuiltinAutoGain) | 독립 플러그인 / Standalone Plugin |
|---|---|
| `atomic<float>` 직접 접근 / Direct `atomic<float>` access | APVTS 파라미터 (DAW 오토메이션) / APVTS parameters (DAW automation) |
| DirectPipe 전용 에디터 / DirectPipe-specific editor | 리사이즈 가능 독립 에디터 / Resizable standalone editor |
| DirectPipe 빌드에 포함 / Bundled with DirectPipe build | `BUILD_AUTOGAIN_PLUGIN=ON`으로 선택적 빌드 / Optional build with `BUILD_AUTOGAIN_PLUGIN=ON` |
| 플러그인 ID 없음 / No plugin ID | `Lvtk` / `Dagn` (고유 ID / unique ID) |

## 예상 작업량 / Estimated Work

| 작업 / Task | 시간 / Time |
|---|---|
| CMakeLists.txt + 디렉토리 / CMakeLists.txt + directory | 15분 / 15 min |
| PluginProcessor (복사 + APVTS) / PluginProcessor (copy + APVTS) | 2시간 / 2 hours |
| PluginEditor (UI 개선) / PluginEditor (UI improvements) | 1-2시간 / 1-2 hours |
| 테스트 + CI / Testing + CI | 30분 / 30 min |
| **합계 / Total** | **3-5시간 / 3-5 hours** |
