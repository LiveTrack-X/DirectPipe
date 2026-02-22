/**
 * @file AudioSettings.h
 * @brief Audio configuration panel — sample rate, buffer size, channel mode
 *
 * Provides user-facing controls for core audio parameters and displays
 * the estimated round-trip latency derived from the current settings.
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief UI component for configuring audio engine parameters.
 *
 * Exposes three settings:
 * - Sample Rate  (44100, 48000, 96000 Hz)
 * - Buffer Size  (64, 128, 256, 480, 512 samples)
 * - Channel Mode (Mono / Stereo radio buttons)
 *
 * On every change the component immediately pushes the new value
 * to the referenced AudioEngine and recalculates the estimated
 * latency display.
 */
class AudioSettings : public juce::Component {
public:
    /**
     * @brief Construct an AudioSettings panel.
     * @param engine Reference to the audio engine to configure.
     */
    explicit AudioSettings(AudioEngine& engine);
    ~AudioSettings() override;

    // Non-copyable
    AudioSettings(const AudioSettings&) = delete;
    AudioSettings& operator=(const AudioSettings&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Refresh all controls to match the current engine state.
     *
     * Call this after an external change (e.g. preset load) so the
     * UI stays synchronised with the engine.
     */
    void refreshFromEngine();

private:
    /** @brief Push the selected sample rate to the engine. */
    void onSampleRateChanged();

    /** @brief Push the selected buffer size to the engine. */
    void onBufferSizeChanged();

    /** @brief Push the selected channel mode to the engine. */
    void onChannelModeChanged();

    /** @brief Recalculate and display estimated latency. */
    void updateLatencyDisplay();

    /**
     * @brief Convert a combo-box selection ID to the corresponding sample rate.
     * @param id 1-based combo-box item ID.
     * @return Sample rate in Hz, or 48000.0 as fallback.
     */
    static double sampleRateFromId(int id);

    /**
     * @brief Convert a combo-box selection ID to the corresponding buffer size.
     * @param id 1-based combo-box item ID.
     * @return Buffer size in samples, or 480 as fallback.
     */
    static int bufferSizeFromId(int id);

    // ─── Data ───
    AudioEngine& engine_;

    // Section title
    juce::Label titleLabel_{"", "Audio Settings"};

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

    // Latency display
    juce::Label latencyTitleLabel_{"", "Estimated Latency:"};
    juce::Label latencyValueLabel_{"", "-- ms"};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettings)
};

} // namespace directpipe
