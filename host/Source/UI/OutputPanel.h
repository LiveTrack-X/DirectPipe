/**
 * @file OutputPanel.h
 * @brief Output routing control panel — VirtualCable + Monitor
 *
 * VirtualCable: device selector, volume
 * Monitor: device selector, volume, enable toggle
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief UI component showing output routing controls.
 *
 * Layout (top to bottom):
 * - Virtual Cable section
 *     - Output device selector combo box
 *     - Volume slider  (0 .. 100 %)
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

    void refreshDeviceLists();

    /** Called when the user changes any output setting. */
    std::function<void()> onSettingsChanged;

private:
    void timerCallback() override;

    // VirtualCable callbacks
    void onVCDeviceSelected();
    void onVCVolumeChanged();

    // Monitor callbacks
    void onMonitorDeviceSelected();
    void onMonitorVolumeChanged();
    void onMonitorEnableToggled();

    // ─── Data ───
    AudioEngine& engine_;

    // ── Virtual Cable section ──
    juce::Label vcTitleLabel_{"", "Virtual Cable"};
    juce::Label vcDeviceLabel_{"", "Device:"};
    juce::ComboBox vcDeviceCombo_;
    juce::Slider vcVolumeSlider_;
    juce::Label vcVolumeLabel_{"", "Volume:"};

    // ── Monitor section ──
    juce::Label monTitleLabel_{"", "Monitor Output"};
    juce::Label monDeviceLabel_{"", "Device:"};
    juce::ComboBox monDeviceCombo_;
    juce::Slider monVolumeSlider_;
    juce::Label monVolumeLabel_{"", "Volume:"};
    juce::ToggleButton monEnableButton_{"Enable"};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputPanel)
};

} // namespace directpipe
