// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>

namespace directpipe {

class BuiltinAutoGain;

/**
 * @brief Editor panel for the built-in auto-gain (AGC) processor.
 *
 * Opened via createEditor() on BuiltinAutoGain.
 * Target LUFS slider + read-only current LUFS/Gain display + collapsible
 * advanced section with Low Correct, High Correct, and Max Gain sliders.
 * Polls processor feedback at ~10 Hz via juce::Timer.
 *
 * Thread Ownership:
 *   All methods -- [Message thread]
 *   timerCallback() -- [Message thread, ~10 Hz]
 */
class AGCEditPanel : public juce::AudioProcessorEditor,
                     private juce::Timer {
public:
    explicit AGCEditPanel(BuiltinAutoGain& processor);
    ~AGCEditPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    BuiltinAutoGain& processor_;

    // -- Target LUFS --
    juce::Label targetLabel_;
    juce::Slider targetSlider_;

    // -- Read-only feedback displays --
    juce::Label currentLufsLabel_;
    juce::Label currentLufsValue_;
    juce::Label currentGainLabel_;
    juce::Label currentGainValue_;

    // -- Advanced section --
    juce::ToggleButton advancedToggle_{"Advanced"};
    juce::Label lowCorrLabel_;
    juce::Slider lowCorrSlider_;
    juce::Label highCorrLabel_;
    juce::Slider highCorrSlider_;
    juce::Label maxGainLabel_;
    juce::Slider maxGainSlider_;

    void timerCallback() override;
    void syncFromProcessor();
    void updateAdvancedVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AGCEditPanel)
};

} // namespace directpipe
