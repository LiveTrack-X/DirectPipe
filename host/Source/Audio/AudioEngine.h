/**
 * @file AudioEngine.h
 * @brief Core audio engine — WASAPI input → VST chain → output routing
 *
 * Manages the audio device, VST plugin processing chain, and output
 * distribution to shared memory, virtual mic, and local monitor.
 */
#pragma once

#include <JuceHeader.h>
#include "VSTChain.h"
#include "OutputRouter.h"
#include "LatencyMonitor.h"
#include "VirtualMicOutput.h"
#include "../IPC/SharedMemWriter.h"

#include <functional>
#include <memory>

namespace directpipe {

/**
 * @brief Main audio processing engine.
 *
 * Coordinates:
 * 1. WASAPI Shared mode input from USB microphone (non-exclusive)
 * 2. VST plugin chain processing with atomic bypass flags
 * 3. Output routing to shared memory (Virtual Loop Mic) and monitor
 * 4. Mono/Stereo channel mode selection
 */
class AudioEngine : public juce::AudioIODeviceCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    /**
     * @brief Initialize the audio engine with device manager.
     * @return true if initialization succeeded.
     */
    bool initialize();

    /**
     * @brief Shut down the audio engine and release resources.
     */
    void shutdown();

    /**
     * @brief Get the JUCE audio device manager for UI binding.
     */
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager_; }

    /**
     * @brief Get the VST chain for UI manipulation.
     */
    VSTChain& getVSTChain() { return vstChain_; }

    /**
     * @brief Get the output router for volume/enable control.
     */
    OutputRouter& getOutputRouter() { return outputRouter_; }

    /**
     * @brief Get the latency monitor for UI display.
     */
    LatencyMonitor& getLatencyMonitor() { return latencyMonitor_; }

    /**
     * @brief Get the virtual mic output for driver detection.
     */
    VirtualMicOutput& getVirtualMicOutput() { return virtualMicOutput_; }

    /**
     * @brief Set the input device by name.
     * @param deviceName Name of the audio input device.
     * @return true if the device was successfully opened.
     */
    bool setInputDevice(const juce::String& deviceName);

    /**
     * @brief Set the output device for monitoring.
     * @param deviceName Name of the audio output device.
     * @return true if the device was successfully opened.
     */
    bool setOutputDevice(const juce::String& deviceName);

    /**
     * @brief Set WASAPI buffer size.
     * @param bufferSize Buffer size in samples (e.g., 64, 128, 256, 512).
     */
    void setBufferSize(int bufferSize);

    /**
     * @brief Set sample rate.
     * @param sampleRate Sample rate in Hz (e.g., 44100, 48000, 96000).
     */
    void setSampleRate(double sampleRate);

    /**
     * @brief Enable or disable local monitoring output.
     */
    void setMonitorEnabled(bool enabled) { monitorEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isMonitorEnabled() const { return monitorEnabled_.load(std::memory_order_relaxed); }

    /**
     * @brief Set channel mode (1 = Mono, 2 = Stereo).
     */
    void setChannelMode(int channels);
    int getChannelMode() const { return channelMode_.load(std::memory_order_relaxed); }

    /**
     * @brief Set input gain (0.0 - 2.0+).
     */
    void setInputGain(float gain) { inputGain_.store(gain, std::memory_order_relaxed); }
    float getInputGain() const { return inputGain_.load(std::memory_order_relaxed); }

    /**
     * @brief Set master mute (all outputs).
     */
    void setMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }
    bool isMuted() const { return muted_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the current input level (0.0 - 1.0).
     */
    float getInputLevel() const { return inputLevel_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the current output level (0.0 - 1.0).
     */
    float getOutputLevel() const { return outputLevel_.load(std::memory_order_relaxed); }

    /**
     * @brief Get list of available input devices.
     */
    juce::StringArray getAvailableInputDevices() const;

    /**
     * @brief Get list of available output devices.
     */
    juce::StringArray getAvailableOutputDevices() const;

    /**
     * @brief Check if the engine is currently running.
     */
    bool isRunning() const { return running_; }

    // ─── Callback for input level change (for UI) ───
    std::function<void(float)> onInputLevelChanged;
    std::function<void(float)> onOutputLevelChanged;

private:
    // juce::AudioIODeviceCallback overrides
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

    /**
     * @brief Calculate RMS level of an audio buffer.
     */
    static float calculateRMS(const float* data, int numSamples);

    juce::AudioDeviceManager deviceManager_;
    VSTChain vstChain_;
    OutputRouter outputRouter_;
    LatencyMonitor latencyMonitor_;
    VirtualMicOutput virtualMicOutput_;

    bool running_ = false;
    std::atomic<bool> monitorEnabled_{false};

    // Thread-safe control flags (set from any thread, read by audio thread)
    std::atomic<float> inputLevel_{0.0f};
    std::atomic<float> outputLevel_{0.0f};
    std::atomic<float> inputGain_{1.0f};
    std::atomic<int> channelMode_{2};    // 1 = Mono, 2 = Stereo
    std::atomic<bool> muted_{false};

    // Current audio settings
    double currentSampleRate_ = 48000.0;
    int currentBufferSize_ = 480;  // v3.1: default 480 samples

    // Pre-allocated work buffer (avoids heap allocation in audio callback)
    juce::AudioBuffer<float> workBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace directpipe
