/**
 * @file OutputRouter.h
 * @brief Audio output distribution to 2 destinations
 *
 * Routes processed audio to:
 * 1. Virtual Cable (second WASAPI device) → Discord/Zoom/OBS
 * 2. Local Monitor → Headphones
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
        VirtualCable = 0,  ///< Virtual cable (Discord/Zoom/OBS)
        Monitor,           ///< Local monitoring (headphones)
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

    /** Wire the virtual cable output (non-owning pointer). */
    void setVirtualMicOutput(VirtualMicOutput* vmo) { virtualMicOutput_ = vmo; }

    /** Wire the monitor output (non-owning pointer, separate WASAPI device). */
    void setMonitorOutput(VirtualMicOutput* mo) { monitorOutput_ = mo; }

    /** Check if virtual cable is active and receiving audio. */
    bool isVirtualCableActive() const;

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

    VirtualMicOutput* virtualMicOutput_ = nullptr;
    VirtualMicOutput* monitorOutput_ = nullptr;

    // Temporary buffer for volume-scaled output (pre-allocated)
    juce::AudioBuffer<float> scaledBuffer_;

    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputRouter)
};

} // namespace directpipe
