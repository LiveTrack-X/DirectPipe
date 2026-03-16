// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>

namespace directpipe {

class BuiltinFilter;

/**
 * @brief Editor panel for the built-in HPF + LPF filter processor.
 *
 * Opened via createEditor() on BuiltinFilter.
 * HPF section: toggle + preset combo (60/80/120/Custom) + custom slider.
 * LPF section: toggle + preset combo (16k/12k/8k/Custom) + custom slider.
 *
 * Thread Ownership:
 *   All methods -- [Message thread]
 */
class FilterEditPanel : public juce::AudioProcessorEditor {
public:
    explicit FilterEditPanel(BuiltinFilter& processor);
    ~FilterEditPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    BuiltinFilter& filter_;

    // -- HPF controls --
    juce::ToggleButton hpfToggle_{"HPF"};
    juce::ComboBox hpfPreset_;
    juce::Slider hpfSlider_;
    juce::Label hpfSliderLabel_;

    // -- LPF controls --
    juce::ToggleButton lpfToggle_{"LPF"};
    juce::ComboBox lpfPreset_;
    juce::Slider lpfSlider_;
    juce::Label lpfSliderLabel_;

    void syncFromProcessor();
    void updateHPFVisibility();
    void updateLPFVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilterEditPanel)
};

} // namespace directpipe
