// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file DeviceSelector.h
 * @brief Audio input/output device selection UI component
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief UI component for selecting audio input device,
 * sample rate, and buffer size.
 */
class DeviceSelector : public juce::Component,
                       public juce::ChangeListener {
public:
    explicit DeviceSelector(AudioEngine& engine);
    ~DeviceSelector() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshDeviceList();
    void onDeviceSelected();
    void onSampleRateChanged();
    void onBufferSizeChanged();

    AudioEngine& engine_;

    juce::ComboBox deviceCombo_;
    juce::ComboBox sampleRateCombo_;
    juce::ComboBox bufferSizeCombo_;

    juce::Label deviceLabel_{"", "Input Device:"};
    juce::Label sampleRateLabel_{"", "Sample Rate:"};
    juce::Label bufferSizeLabel_{"", "Buffer Size:"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSelector)
};

} // namespace directpipe
