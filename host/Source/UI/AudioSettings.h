/**
 * @file AudioSettings.h
 * @brief Unified audio I/O configuration panel
 *
 * Combines driver type selection (ASIO/WASAPI), input/output device selection,
 * sample rate, buffer size, channel mode, and latency display
 * into a single cohesive panel.
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief Unified audio settings panel.
 *
 * Always shows separate Input + Output device combos regardless of driver type.
 * In ASIO mode, an additional "ASIO Control Panel" button is shown.
 *
 * Sample rate and buffer size lists are queried dynamically
 * from the active audio device.
 */
class AudioSettings : public juce::Component,
                      public juce::ChangeListener {
public:
    explicit AudioSettings(AudioEngine& engine);
    ~AudioSettings() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Refresh all controls to match the current engine state.
     * Call after preset load or external device changes.
     */
    void refreshFromEngine();

    /** Called when the user changes any audio setting. */
    std::function<void()> onSettingsChanged;

private:
    // ChangeListener for device manager notifications
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Callbacks
    void onDriverTypeChanged();
    void onInputDeviceChanged();
    void onOutputDeviceChanged();
    void onInputChannelChanged();
    void onOutputChannelChanged();
    void onSampleRateChanged();
    void onBufferSizeChanged();
    void onChannelModeChanged();

    // Helpers
    void rebuildDeviceLists();
    void rebuildChannelLists();
    void rebuildSampleRateList();
    void rebuildBufferSizeList();
    void updateLatencyDisplay();
    void updateChannelModeDescription();

    bool isAsioMode() const;

    // ─── Data ───
    AudioEngine& engine_;

    // Section title
    juce::Label titleLabel_{"", "Audio Settings"};

    // Driver type (ASIO / Windows Audio)
    juce::Label driverLabel_{"", "Driver:"};
    juce::ComboBox driverCombo_;

    // Device selection — always Input + Output
    juce::Label inputLabel_{"", "Input:"};
    juce::ComboBox inputCombo_;
    juce::Label outputLabel_{"", "Output:"};
    juce::ComboBox outputCombo_;

    // ASIO channel selection (visible only in ASIO mode)
    juce::Label inputChLabel_{"", "Input Ch:"};
    juce::ComboBox inputChCombo_;
    juce::Label outputChLabel_{"", "Output Ch:"};
    juce::ComboBox outputChCombo_;

    // Sample rate
    juce::Label sampleRateLabel_{"", "Sample Rate:"};
    juce::ComboBox sampleRateCombo_;

    // Buffer size
    juce::Label bufferSizeLabel_{"", "Buffer Size:"};
    juce::ComboBox bufferSizeCombo_;

    // Channel mode
    juce::Label channelModeLabel_{"", "Channel Mode:"};
    juce::ToggleButton monoButton_{"Mono"};
    juce::ToggleButton stereoButton_{"Stereo"};
    juce::Label channelModeDescLabel_{"", ""};

    // Latency display
    juce::Label latencyTitleLabel_{"", "Estimated Latency:"};
    juce::Label latencyValueLabel_{"", "-- ms"};

    // ASIO Control Panel button (visible only in ASIO mode)
    juce::TextButton asioControlBtn_{"ASIO Control Panel"};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettings)
};

} // namespace directpipe
