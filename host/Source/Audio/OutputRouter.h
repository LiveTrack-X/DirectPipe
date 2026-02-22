/**
 * @file OutputRouter.h
 * @brief Audio output distribution to 3 simultaneous destinations
 *
 * Routes processed audio to:
 * 1. Shared Memory Ring Buffer → OBS Plugin
 * 2. WASAPI Output → Virtual Microphone (Discord/Zoom)
 * 3. Local Monitor → Headphones
 */
#pragma once

#include <JuceHeader.h>
#include "../IPC/SharedMemWriter.h"
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
        SharedMemory = 0,  ///< OBS Plugin (via shared memory IPC)
        VirtualMic,        ///< Discord/Zoom (via virtual audio device)
        Monitor,           ///< Local monitoring (headphones)
        Count
    };

    OutputRouter();
    ~OutputRouter();

    /**
     * @brief Initialize the router with audio parameters.
     * @param sampleRate Sample rate in Hz.
     * @param bufferSize Buffer size in samples.
     */
    void initialize(double sampleRate, int bufferSize);

    /**
     * @brief Shut down all outputs and release resources.
     */
    void shutdown();

    /**
     * @brief Route processed audio to all enabled outputs.
     *
     * Called from the real-time audio thread. No allocations.
     *
     * @param buffer Processed audio data.
     * @param numSamples Number of samples in the buffer.
     */
    void routeAudio(const juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Set volume for an output (0.0 to 1.0).
     * @param output Target output.
     * @param volume Volume level (0.0 = silence, 1.0 = unity).
     */
    void setVolume(Output output, float volume);

    /**
     * @brief Get current volume for an output.
     */
    float getVolume(Output output) const;

    /**
     * @brief Enable or disable an output.
     */
    void setEnabled(Output output, bool enabled);

    /**
     * @brief Check if an output is enabled.
     */
    bool isEnabled(Output output) const;

    /**
     * @brief Get the current level for an output (for metering).
     */
    float getLevel(Output output) const;

    /**
     * @brief Check if the OBS plugin is connected (shared memory active).
     */
    bool isOBSConnected() const;

    /**
     * @brief Get the shared memory writer (for status queries).
     */
    SharedMemWriter& getSharedMemWriter() { return shmWriter_; }

private:
    static constexpr int kOutputCount = static_cast<int>(Output::Count);

    struct OutputState {
        std::atomic<float> volume{1.0f};
        std::atomic<bool> enabled{true};
        std::atomic<float> level{0.0f};
    };

    OutputState outputs_[kOutputCount];

    SharedMemWriter shmWriter_;

    // Temporary buffer for volume-scaled output (pre-allocated)
    juce::AudioBuffer<float> scaledBuffer_;

    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputRouter)
};

} // namespace directpipe
