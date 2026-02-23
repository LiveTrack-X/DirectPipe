/**
 * @file VirtualMicOutput.h
 * @brief Virtual microphone output management
 *
 * Handles routing processed audio to a virtual audio device
 * (e.g., VB-Cable, Virtual Audio Cable) so that Discord, Zoom,
 * and other communication apps can receive the processed audio.
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace directpipe {

/**
 * @brief Detection and usage status of virtual microphone drivers.
 */
enum class VirtualMicStatus {
    NotDetected,    ///< No virtual audio device found
    Detected,       ///< Virtual audio device found but not active
    Active,         ///< Audio is being routed to virtual device
    Error           ///< Error opening the virtual device
};

/**
 * @brief Manages output to a virtual microphone device.
 *
 * Strategy:
 * 1. Auto-detect installed virtual audio devices (VB-Cable, etc.)
 * 2. Open the device as WASAPI output
 * 3. Route processed PCM audio to it
 * 4. Fall back to Shared mode if Exclusive not available
 */
class VirtualMicOutput {
public:
    VirtualMicOutput();
    ~VirtualMicOutput();

    /**
     * @brief Scan for available virtual audio devices.
     * @return List of detected virtual audio device names.
     */
    juce::StringArray detectVirtualDevices();

    /**
     * @brief Check if the native Virtual Loop Mic WDM driver is installed.
     *
     * Scans Windows capture (input) devices for "Virtual Loop Mic".
     * When the native driver is installed, audio flows via shared memory
     * directly to the driver -- no third-party virtual cable needed.
     *
     * @return true if the native driver's capture device is present.
     */
    bool isNativeDriverInstalled();

    /**
     * @brief Get the name used by the native WDM driver.
     */
    static juce::String getNativeDriverDeviceName() { return "Virtual Loop Mic"; }

    /**
     * @brief Initialize output to a specific virtual device.
     * @param deviceName Name of the virtual audio device.
     * @param sampleRate Audio sample rate.
     * @param bufferSize Buffer size in samples.
     * @return true if successfully opened.
     */
    bool initialize(const juce::String& deviceName, double sampleRate, int bufferSize);

    /**
     * @brief Shut down the virtual mic output.
     */
    void shutdown();

    /**
     * @brief Write audio to the virtual microphone.
     *
     * Called from the real-time audio thread.
     *
     * @param buffer Audio data to write.
     * @param numSamples Number of samples.
     */
    void writeAudio(const juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Get current status.
     */
    VirtualMicStatus getStatus() const { return status_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the name of the currently selected virtual device.
     */
    juce::String getDeviceName() const { return deviceName_; }

    /**
     * @brief Check if a virtual audio device is available on the system.
     */
    bool isVirtualDeviceAvailable() const;

    /**
     * @brief Get a user-friendly setup guide message.
     */
    static juce::String getSetupGuideMessage();

private:
    /**
     * @brief Check if a device name looks like a virtual audio device.
     */
    static bool isVirtualDeviceName(const juce::String& name);

    std::atomic<VirtualMicStatus> status_{VirtualMicStatus::NotDetected};
    juce::String deviceName_;

    // WASAPI output for virtual device
    std::unique_ptr<juce::AudioDeviceManager> virtualDeviceManager_;

    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;
};

} // namespace directpipe
