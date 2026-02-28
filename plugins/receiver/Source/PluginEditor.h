// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class DirectPipeReceiverEditor : public juce::AudioProcessorEditor,
                                  public juce::Timer {
public:
    explicit DirectPipeReceiverEditor(DirectPipeReceiverProcessor& processor);
    ~DirectPipeReceiverEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    DirectPipeReceiverProcessor& processor_;

    juce::TextButton muteButton_{"MUTE"};
    juce::ButtonParameterAttachment muteAttachment_;

    juce::ComboBox bufferCombo_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> bufferAttachment_;
    juce::Label bufferLabel_{"", "Buffer:"};

    bool lastConnected_ = false;
    uint32_t lastSampleRate_ = 0;
    uint32_t lastChannels_ = 0;

    static constexpr int kWidth = 240;
    static constexpr int kHeight = 170;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DirectPipeReceiverEditor)
};
