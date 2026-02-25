/**
 * @file OutputPanel.h
 * @brief Monitor output control panel
 *
 * Monitor: device selector, volume, enable toggle
 * (Main output is controlled via AudioSettings Output dropdown)
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

class OutputPanel : public juce::Component,
                    public juce::Timer {
public:
    explicit OutputPanel(AudioEngine& engine);
    ~OutputPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshDeviceLists();

    std::function<void()> onSettingsChanged;

private:
    void timerCallback() override;

    void onMonitorDeviceSelected();
    void onMonitorVolumeChanged();
    void onMonitorEnableToggled();

    AudioEngine& engine_;

    juce::Label titleLabel_{"", "Monitor Output"};

    juce::Label monitorDeviceLabel_{"", "Device:"};
    juce::ComboBox monitorDeviceCombo_;
    juce::Slider monitorVolumeSlider_;
    juce::Label monitorVolumeLabel_{"", "Volume:"};
    juce::ToggleButton monitorEnableButton_{"Enable"};
    juce::Label monitorStatusLabel_;

    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputPanel)
};

} // namespace directpipe
