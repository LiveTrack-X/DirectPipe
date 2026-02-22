/**
 * @file VirtualMicOutput.cpp
 * @brief Virtual microphone output implementation
 */

#include "VirtualMicOutput.h"

namespace directpipe {

VirtualMicOutput::VirtualMicOutput() = default;

VirtualMicOutput::~VirtualMicOutput()
{
    shutdown();
}

juce::StringArray VirtualMicOutput::detectVirtualDevices()
{
    juce::StringArray virtualDevices;

    // Create temporary device manager to enumerate devices
    juce::AudioDeviceManager tempManager;
    tempManager.initialiseWithDefaultDevices(0, 2);

    if (auto* type = tempManager.getCurrentDeviceTypeObject()) {
        auto outputDevices = type->getDeviceNames(false);  // output devices

        for (const auto& name : outputDevices) {
            if (isVirtualDeviceName(name)) {
                virtualDevices.add(name);
            }
        }
    }

    return virtualDevices;
}

bool VirtualMicOutput::initialize(const juce::String& deviceName,
                                   double sampleRate, int bufferSize)
{
    shutdown();

    deviceName_ = deviceName;
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    virtualDeviceManager_ = std::make_unique<juce::AudioDeviceManager>();

    auto result = virtualDeviceManager_->initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("VirtualMicOutput: Init error: " + result);
        status_.store(VirtualMicStatus::Error, std::memory_order_relaxed);
        return false;
    }

    // Configure to use the virtual device as output
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    virtualDeviceManager_->getAudioDeviceSetup(setup);
    setup.outputDeviceName = deviceName;
    setup.sampleRate = sampleRate;
    setup.bufferSize = bufferSize;
    setup.outputChannels.setRange(0, 2, true);

    result = virtualDeviceManager_->setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("VirtualMicOutput: Setup error: " + result);
        status_.store(VirtualMicStatus::Error, std::memory_order_relaxed);
        return false;
    }

    status_.store(VirtualMicStatus::Active, std::memory_order_relaxed);
    juce::Logger::writeToLog("VirtualMicOutput: Active on " + deviceName);
    return true;
}

void VirtualMicOutput::shutdown()
{
    if (virtualDeviceManager_) {
        virtualDeviceManager_->closeAudioDevice();
        virtualDeviceManager_.reset();
    }
    status_.store(VirtualMicStatus::NotDetected, std::memory_order_relaxed);
}

void VirtualMicOutput::writeAudio(const juce::AudioBuffer<float>& /*buffer*/, int /*numSamples*/)
{
    // The virtual mic output is handled through the WASAPI device manager's
    // audio callback. The OutputRouter feeds audio to this device via the
    // secondary AudioDeviceManager's callback mechanism.
    //
    // In a production implementation, this would use a lock-free queue
    // to pass audio from the main engine's callback to the virtual device's
    // callback thread.
}

bool VirtualMicOutput::isVirtualDeviceAvailable() const
{
    return status_.load(std::memory_order_relaxed) != VirtualMicStatus::NotDetected;
}

bool VirtualMicOutput::isVirtualDeviceName(const juce::String& name)
{
    auto lower = name.toLowerCase();

    // Known virtual audio device name patterns
    return lower.contains("vb-audio") ||
           lower.contains("vb-cable") ||
           lower.contains("cable input") ||
           lower.contains("cable output") ||
           lower.contains("virtual audio") ||
           lower.contains("virtual cable") ||
           lower.contains("voicemeeter") ||
           lower.contains("blackhole") ||    // macOS virtual audio
           lower.contains("soundflower") ||   // macOS virtual audio
           lower.contains("hi-fi cable");
}

juce::String VirtualMicOutput::getSetupGuideMessage()
{
    return "Virtual microphone driver not detected.\n\n"
           "To use DirectPipe with Discord, Zoom, and other apps, "
           "you need a virtual audio cable driver.\n\n"
           "Recommended (free):\n"
           "  - VB-Cable (vb-audio.com/Cable)\n\n"
           "After installation:\n"
           "1. Restart DirectPipe\n"
           "2. The virtual mic output will auto-detect\n"
           "3. In Discord/Zoom, select 'CABLE Output' as your input device";
}

} // namespace directpipe
