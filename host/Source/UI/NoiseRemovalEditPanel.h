// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>

namespace directpipe {

class BuiltinNoiseRemoval;

/**
 * @brief Editor panel for the built-in noise removal processor.
 *
 * Opened via createEditor() on BuiltinNoiseRemoval.
 * Strength combo (Light/Standard/Aggressive) + collapsible advanced section
 * with VAD threshold slider.
 *
 * Thread Ownership:
 *   All methods -- [Message thread]
 */
class NoiseRemovalEditPanel : public juce::AudioProcessorEditor {
public:
    explicit NoiseRemovalEditPanel(BuiltinNoiseRemoval& processor);
    ~NoiseRemovalEditPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    BuiltinNoiseRemoval& processor_;

    // -- Strength controls --
    juce::Label strengthLabel_;
    juce::ComboBox strengthCombo_;

    // -- Advanced section --
    juce::ToggleButton advancedToggle_{"Advanced"};
    juce::Label vadLabel_;
    juce::Slider vadSlider_;

    void syncFromProcessor();
    void updateAdvancedVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseRemovalEditPanel)
};

} // namespace directpipe
