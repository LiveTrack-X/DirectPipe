/**
 * @file OutputPanel.h
 * @brief Output status and control panel for Virtual Loop Mic and Monitor
 *
 * Displays connection status indicators, output device selection,
 * volume sliders, and mute toggles for the two user-facing outputs.
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief UI component showing output status and controls.
 *
 * Layout (top to bottom):
 * - Virtual Loop Mic section
 *     - Connected / Disconnected status indicator (LED-style)
 *     - Volume slider  (0 .. 100 %)
 *     - Mute toggle button
 * - Monitor section
 *     - Output device selector combo box
 *     - Volume slider  (0 .. 100 %)
 *     - Mute toggle button
 *
 * The panel periodically refreshes the Virtual Loop Mic connection
 * state via a juce::Timer so the indicator stays up-to-date.
 */
class OutputPanel : public juce::Component,
                    public juce::Timer {
public:
    /**
     * @brief Construct an OutputPanel.
     * @param engine Reference to the audio engine for output control.
     */
    explicit OutputPanel(AudioEngine& engine);
    ~OutputPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Force a refresh of the monitor device list.
     *
     * Call after an external device change notification.
     */
    void refreshMonitorDeviceList();

private:
    void timerCallback() override;

    /** @brief Repaint the Virtual Mic status indicator. */
    void updateVirtualMicStatus();

    /** @brief Called when the user selects a monitor output device. */
    void onMonitorDeviceSelected();

    /** @brief Called when the Virtual Mic volume slider moves. */
    void onVirtualMicVolumeChanged();

    /** @brief Called when the Monitor volume slider moves. */
    void onMonitorVolumeChanged();

    /** @brief Called when the Virtual Mic mute button is toggled. */
    void onVirtualMicMuteToggled();

    /** @brief Called when the Monitor enable button is toggled. */
    void onMonitorEnableToggled();

    // ─── Data ───
    AudioEngine& engine_;

    // Section title
    juce::Label titleLabel_{"", "Outputs"};

    // ── Virtual Loop Mic section ──
    juce::Label vmicSectionLabel_{"", "Virtual Loop Mic"};
    juce::Label vmicStatusLabel_{"", "Checking..."};
    juce::Label vmicDriverLabel_{"", ""};
    juce::Slider vmicVolumeSlider_;
    juce::Label vmicVolumeLabel_{"", "Volume:"};
    juce::ToggleButton vmicMuteButton_{"Mute"};
    bool vmicConnected_ = false;
    bool nativeDriverDetected_ = false;
    bool driverCheckDone_ = false;

    // ── Monitor section ──
    juce::Label monitorSectionLabel_{"", "Monitor Output"};
    juce::Label monitorDeviceLabel_{"", "Device:"};
    juce::ComboBox monitorDeviceCombo_;
    juce::Slider monitorVolumeSlider_;
    juce::Label monitorVolumeLabel_{"", "Volume:"};
    juce::ToggleButton monitorEnableButton_{"Enable"};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kGreenColour     = 0xFF4CAF50;
    static constexpr juce::uint32 kRedColour       = 0xFFE05050;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputPanel)
};

} // namespace directpipe
