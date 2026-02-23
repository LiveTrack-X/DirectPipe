/**
 * @file VirtualMicOutput.h
 * @brief Virtual cable output via second WASAPI device.
 *
 * Routes processed audio to a virtual audio cable (e.g., VB-Audio Hi-Fi Cable)
 * so that Discord, Zoom, OBS, and other apps can capture the processed audio.
 * Uses a lock-free ring buffer to bridge the main audio callback and the
 * virtual cable's independent WASAPI callback thread.
 */
#pragma once

#include <JuceHeader.h>
#include "AudioRingBuffer.h"
#include <atomic>
#include <memory>

namespace directpipe {

enum class VirtualCableStatus {
    NotConfigured,  ///< No device selected
    Active,         ///< Audio flowing to virtual cable
    Error           ///< Device open failed or sample rate mismatch
};

/**
 * @brief WASAPI output to a virtual audio cable device.
 *
 * Owns a separate AudioDeviceManager with its own callback thread.
 * The main audio callback writes to a lock-free ring buffer (producer),
 * and this class's callback reads from it (consumer) and outputs to WASAPI.
 */
class VirtualMicOutput : public juce::AudioIODeviceCallback {
public:
    VirtualMicOutput();
    ~VirtualMicOutput() override;

    // --- Configuration (call from message thread) ---
    bool initialize(const juce::String& deviceName, double sampleRate, int bufferSize);
    void shutdown();
    bool setDevice(const juce::String& deviceName);

    // --- RT-safe audio push (called from MAIN audio callback) ---
    int writeAudio(const float* const* channelData, int numChannels, int numFrames);

    // --- Device enumeration ---
    juce::StringArray getAvailableOutputDevices() const;
    static juce::StringArray detectVirtualDevices();

    // --- Status queries ---
    VirtualCableStatus getStatus() const { return status_.load(std::memory_order_relaxed); }
    juce::String getDeviceName() const { return deviceName_; }
    bool isActive() const { return status_.load(std::memory_order_relaxed) == VirtualCableStatus::Active; }
    int getDroppedFrames() const { return droppedFrames_.load(std::memory_order_relaxed); }
    int getActualBufferSize() const { return actualBufferSize_.load(std::memory_order_relaxed); }
    double getActualSampleRate() const { return actualSampleRate_.load(std::memory_order_relaxed); }

    // --- Setup guide ---
    static juce::String getSetupGuideMessage();

private:
    // juce::AudioIODeviceCallback — virtual cable's WASAPI callback
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    static bool isVirtualDeviceName(const juce::String& name);

    AudioRingBuffer ringBuffer_;
    std::unique_ptr<juce::AudioDeviceManager> deviceManager_;

    juce::String deviceName_;
    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;  // Low default for minimal latency

    std::atomic<VirtualCableStatus> status_{VirtualCableStatus::NotConfigured};
    std::atomic<int> droppedFrames_{0};
    std::atomic<int> actualBufferSize_{0};
    std::atomic<double> actualSampleRate_{0.0};
};

} // namespace directpipe
