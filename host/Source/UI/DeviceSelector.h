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
