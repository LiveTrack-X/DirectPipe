/**
 * @file OutputPanel.h
 * @brief Output control panel for Monitor output
 *
 * Monitor: device selector, volume, enable toggle
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief UI component showing monitor output controls.
 *
 * Layout (top to bottom):
 * - Monitor section
 *     - Output device selector combo box
 *     - Volume slider  (0 .. 100 %)
 *     - Enable toggle button
 */
class OutputPanel : public juce::Component,
                    public juce::Timer {
public:
    explicit OutputPanel(AudioEngine& engine);
    ~OutputPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Force a refresh of the monitor device list.
     * Call after an external device change notification.
     */
    void refreshMonitorDeviceList();

private:
    void timerCallback() override;

    void onMonitorDeviceSelected();
    void onMonitorVolumeChanged();
    void onMonitorEnableToggled();

    // ─── Data ───
    AudioEngine& engine_;

    // Section title
    juce::Label titleLabel_{"", "Monitor Output"};

    // ── Monitor section ──
    juce::Label monitorDeviceLabel_{"", "Device:"};
    juce::ComboBox monitorDeviceCombo_;
    juce::Slider monitorVolumeSlider_;
    juce::Label monitorVolumeLabel_{"", "Volume:"};
    juce::ToggleButton monitorEnableButton_{"Enable"};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputPanel)
};

} // namespace directpipe
