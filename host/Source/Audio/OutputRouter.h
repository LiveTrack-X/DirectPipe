/**
 * @file OutputRouter.h
 * @brief Audio output routing to monitor (headphones)
 *
 * Routes processed audio to a separate WASAPI monitor device (headphones).
 * Main output goes through the AudioSettings Output device directly.
 */
#pragma once

#include <JuceHeader.h>
#include "VirtualMicOutput.h"
#include <atomic>

namespace directpipe {

/**
 * @brief Routes audio to multiple output destinations.
 *
 * Each output has independent volume control and enable/disable toggle.
 * Audio routing is performed in the real-time callback — no allocations.
 */
class OutputRouter {
public:
    /// Output destination identifiers
    enum class Output {
        Monitor = 0,       ///< Local monitoring (headphones, separate WASAPI device)
        Count
    };

    OutputRouter();
    ~OutputRouter();

    void initialize(double sampleRate, int bufferSize);
    void shutdown();

    /**
     * @brief Route processed audio to all enabled outputs.
     * Called from the real-time audio thread. No allocations.
     */
    void routeAudio(const juce::AudioBuffer<float>& buffer, int numSamples);

    void setVolume(Output output, float volume);
    float getVolume(Output output) const;
    void setEnabled(Output output, bool enabled);
    bool isEnabled(Output output) const;
    float getLevel(Output output) const;

    /** Wire the monitor output (non-owning pointer, separate WASAPI device). */
    void setMonitorOutput(VirtualMicOutput* mo) { monitorOutput_ = mo; }

    /** Check if monitor output is active and receiving audio. */
    bool isMonitorOutputActive() const;

private:
    static constexpr int kOutputCount = static_cast<int>(Output::Count);

    struct OutputState {
        std::atomic<float> volume{1.0f};
        std::atomic<bool> enabled{true};
        std::atomic<float> level{0.0f};
    };

    OutputState outputs_[kOutputCount];

    VirtualMicOutput* monitorOutput_ = nullptr;

    // Temporary buffer for volume-scaled output (pre-allocated)
    juce::AudioBuffer<float> scaledBuffer_;

    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputRouter)
};

} // namespace directpipe
