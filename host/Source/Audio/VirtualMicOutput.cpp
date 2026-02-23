/**
 * @file VirtualMicOutput.cpp
 * @brief Virtual cable output implementation via second WASAPI device.
 */

#include "VirtualMicOutput.h"

namespace directpipe {

VirtualMicOutput::VirtualMicOutput() = default;

VirtualMicOutput::~VirtualMicOutput()
{
    shutdown();
}

bool VirtualMicOutput::initialize(const juce::String& deviceName,
                                   double sampleRate, int bufferSize)
{
    shutdown();

    deviceName_ = deviceName;
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    // Initialize ring buffer: 4096 frames (power of 2), stereo
    ringBuffer_.initialize(4096, 2);

    deviceManager_ = std::make_unique<juce::AudioDeviceManager>();

    // Force WASAPI (Windows Audio) device type
    deviceManager_->setCurrentAudioDeviceType("Windows Audio", true);

    auto result = deviceManager_->initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("VirtualMicOutput: Init error: " + result);
        status_.store(VirtualCableStatus::Error, std::memory_order_relaxed);
        return false;
    }

    // Configure to use the specified virtual device as output
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_->getAudioDeviceSetup(setup);
    setup.outputDeviceName = deviceName;
    setup.sampleRate = sampleRate;
    setup.bufferSize = bufferSize;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setRange(0, 2, true);

    result = deviceManager_->setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("VirtualMicOutput: Setup error: " + result);
        status_.store(VirtualCableStatus::Error, std::memory_order_relaxed);
        return false;
    }

    // Register as the audio callback for this device
    deviceManager_->addAudioCallback(this);

    juce::Logger::writeToLog("VirtualMicOutput: Initialized on " + deviceName);
    return true;
}

void VirtualMicOutput::shutdown()
{
    if (deviceManager_) {
        deviceManager_->removeAudioCallback(this);
        deviceManager_->closeAudioDevice();
        deviceManager_.reset();
    }
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_relaxed);
    ringBuffer_.reset();
}

bool VirtualMicOutput::setDevice(const juce::String& deviceName)
{
    // Re-initialize with the new device, keeping current sample rate/buffer
    return initialize(deviceName, sampleRate_, bufferSize_);
}

// ─── RT-safe: called from main audio callback thread ─────────────────────────

int VirtualMicOutput::writeAudio(const float* const* channelData,
                                  int numChannels, int numFrames)
{
    if (status_.load(std::memory_order_relaxed) != VirtualCableStatus::Active)
        return 0;

    int written = ringBuffer_.write(channelData, numChannels, numFrames);
    if (written < numFrames)
        droppedFrames_.fetch_add(numFrames - written, std::memory_order_relaxed);

    return written;
}

// ─── Virtual cable WASAPI callback (consumer) ─────────────────────────────────

void VirtualMicOutput::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int /*numInputChannels*/,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    int read = ringBuffer_.read(outputChannelData, numOutputChannels, numSamples);

    // Fill remaining samples with silence on underrun
    if (read < numSamples) {
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            std::memset(outputChannelData[ch] + read, 0,
                        static_cast<size_t>(numSamples - read) * sizeof(float));
        }
    }
}

void VirtualMicOutput::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

    double deviceSR = device->getCurrentSampleRate();
    int deviceBS = device->getCurrentBufferSizeSamples();

    actualSampleRate_.store(deviceSR, std::memory_order_relaxed);
    actualBufferSize_.store(deviceBS, std::memory_order_relaxed);

    // Check sample rate match
    if (std::abs(deviceSR - sampleRate_) > 1.0) {
        juce::Logger::writeToLog(
            "VirtualMicOutput: Sample rate mismatch! Expected " +
            juce::String(sampleRate_) + " got " + juce::String(deviceSR));
        status_.store(VirtualCableStatus::Error, std::memory_order_relaxed);
        return;
    }

    ringBuffer_.reset();
    status_.store(VirtualCableStatus::Active, std::memory_order_relaxed);

    juce::Logger::writeToLog(
        "VirtualMicOutput: Active on " + device->getName() +
        " @ " + juce::String(deviceSR) + "Hz / " +
        juce::String(deviceBS) + " samples");
}

void VirtualMicOutput::audioDeviceStopped()
{
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_relaxed);
    juce::Logger::writeToLog("VirtualMicOutput: Device stopped");
}

// ─── Device enumeration ───────────────────────────────────────────────────────

juce::StringArray VirtualMicOutput::getAvailableOutputDevices() const
{
    juce::StringArray devices;

    // Use our own device manager if available, otherwise create temp
    if (deviceManager_) {
        if (auto* type = deviceManager_->getCurrentDeviceTypeObject())
            devices = type->getDeviceNames(false);
    } else {
        juce::AudioDeviceManager temp;
        temp.setCurrentAudioDeviceType("Windows Audio", true);
        temp.initialiseWithDefaultDevices(0, 2);
        if (auto* type = temp.getCurrentDeviceTypeObject())
            devices = type->getDeviceNames(false);
    }

    return devices;
}

juce::StringArray VirtualMicOutput::detectVirtualDevices()
{
    juce::StringArray virtualDevices;

    juce::AudioDeviceManager temp;
    temp.setCurrentAudioDeviceType("Windows Audio", true);
    temp.initialiseWithDefaultDevices(0, 2);

    if (auto* type = temp.getCurrentDeviceTypeObject()) {
        for (const auto& name : type->getDeviceNames(false)) {
            if (isVirtualDeviceName(name))
                virtualDevices.add(name);
        }
    }

    return virtualDevices;
}

bool VirtualMicOutput::isVirtualDeviceName(const juce::String& name)
{
    auto lower = name.toLowerCase();

    return lower.contains("vb-audio") ||
           lower.contains("vb-cable") ||
           lower.contains("cable input") ||
           lower.contains("cable output") ||
           lower.contains("virtual audio") ||
           lower.contains("virtual cable") ||
           lower.contains("voicemeeter") ||
           lower.contains("hi-fi cable");
}

juce::String VirtualMicOutput::getSetupGuideMessage()
{
    return "Virtual audio cable not detected.\n\n"
           "Recommended: Install VB-Audio Hi-Fi Cable\n"
           "  - Download from vb-audio.com/Cable\n"
           "  - Install and restart DirectPipe\n"
           "  - Select 'Hi-Fi Cable Output' as mic input in Discord/Zoom/OBS\n\n"
           "Other supported virtual cables:\n"
           "  - VB-Cable, VoiceMeeter, Virtual Audio Cable";
}

} // namespace directpipe
