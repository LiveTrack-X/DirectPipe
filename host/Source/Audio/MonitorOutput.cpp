// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file MonitorOutput.cpp
 * @brief Virtual cable output implementation via second WASAPI device.
 */

#include "MonitorOutput.h"

namespace directpipe {

MonitorOutput::MonitorOutput() = default;

MonitorOutput::~MonitorOutput()
{
    shutdown();
}

bool MonitorOutput::initialize(const juce::String& deviceName,
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
        juce::Logger::writeToLog("[MONITOR] Init error: " + result);
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
        juce::Logger::writeToLog("[MONITOR] Setup error: " + result);
        status_.store(VirtualCableStatus::Error, std::memory_order_relaxed);
        return false;
    }

    // Register as the audio callback for this device
    deviceManager_->addAudioCallback(this);

    juce::Logger::writeToLog("[MONITOR] Initialized on " + deviceName);
    return true;
}

void MonitorOutput::shutdown()
{
    // Set status BEFORE teardown so producer (writeAudio) stops writing to ring buffer
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_release);
    actualSampleRate_.store(0.0, std::memory_order_relaxed);
    actualBufferSize_.store(0, std::memory_order_relaxed);
    if (deviceManager_) {
        deviceManager_->removeAudioCallback(this);
        deviceManager_->closeAudioDevice();
        deviceManager_.reset();
    }
    ringBuffer_.reset();
}

bool MonitorOutput::setDevice(const juce::String& deviceName)
{
    // Re-initialize with the new device, keeping current sample rate/buffer
    return initialize(deviceName, sampleRate_, bufferSize_);
}

bool MonitorOutput::setBufferSize(int bufferSize)
{
    if (status_.load(std::memory_order_relaxed) == VirtualCableStatus::NotConfigured)
    {
        bufferSize_ = bufferSize;
        return true;
    }
    return initialize(deviceName_, sampleRate_, bufferSize);
}

// ─── RT-safe: called from main audio callback thread ─────────────────────────

int MonitorOutput::writeAudio(const float* const* channelData,
                                  int numChannels, int numFrames)
{
    if (status_.load(std::memory_order_acquire) != VirtualCableStatus::Active)
        return 0;

    int written = ringBuffer_.write(channelData, numChannels, numFrames);
    if (written < numFrames)
        droppedFrames_.fetch_add(numFrames - written, std::memory_order_relaxed);

    return written;
}

// ─── Virtual cable WASAPI callback (consumer) ─────────────────────────────────

void MonitorOutput::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int /*numInputChannels*/,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Guard: if not Active, output silence without touching ring buffer.
    // Prevents data race when reset() is called from another thread.
    if (status_.load(std::memory_order_acquire) != VirtualCableStatus::Active) {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            std::memset(outputChannelData[ch], 0,
                        static_cast<size_t>(numSamples) * sizeof(float));
        return;
    }

    int read = ringBuffer_.read(outputChannelData, numOutputChannels, numSamples);

    // Fill remaining samples with silence on underrun
    if (read < numSamples) {
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            std::memset(outputChannelData[ch] + read, 0,
                        static_cast<size_t>(numSamples - read) * sizeof(float));
        }
    }
}

void MonitorOutput::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

    double deviceSR = device->getCurrentSampleRate();
    int deviceBS = device->getCurrentBufferSizeSamples();

    actualSampleRate_.store(deviceSR, std::memory_order_relaxed);
    actualBufferSize_.store(deviceBS, std::memory_order_relaxed);

    // Check sample rate match
    if (std::abs(deviceSR - sampleRate_) > 1.0) {
        juce::Logger::writeToLog(
            "[MONITOR] Sample rate mismatch! Expected " +
            juce::String(sampleRate_) + " got " + juce::String(deviceSR));
        // Set status BEFORE reset so the consumer callback sees non-Active
        // and skips ring buffer access (prevents data race on reset)
        status_.store(VirtualCableStatus::SampleRateMismatch, std::memory_order_release);
        ringBuffer_.reset();
        return;
    }

    // Set non-Active before reset to prevent consumer from reading during reset
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_release);
    ringBuffer_.reset();
    status_.store(VirtualCableStatus::Active, std::memory_order_release);

    juce::Logger::writeToLog(
        "[MONITOR] Active on " + device->getName() +
        " @ " + juce::String(deviceSR) + "Hz / " +
        juce::String(deviceBS) + " samples");
}

void MonitorOutput::audioDeviceStopped()
{
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_relaxed);
    juce::Logger::writeToLog("[MONITOR] Device stopped");
}

// ─── Device enumeration ───────────────────────────────────────────────────────

juce::StringArray MonitorOutput::getAvailableOutputDevices() const
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

juce::Array<int> MonitorOutput::getAvailableBufferSizes() const
{
    if (deviceManager_) {
        if (auto* device = deviceManager_->getCurrentAudioDevice())
            return device->getAvailableBufferSizes();
    }
    return {};
}

juce::String MonitorOutput::getSetupGuideMessage()
{
    return "Virtual audio cable not configured.\n\n"
           "Recommended: Install VB-Audio Hi-Fi Cable\n"
           "  - Download from vb-audio.com/Cable\n"
           "  - Install and restart DirectPipe\n"
           "  - Select the virtual cable device in Output settings\n"
           "  - Select 'Hi-Fi Cable Output' as mic input in Discord/Zoom/OBS\n\n"
           "Other supported virtual cables:\n"
           "  - VB-Cable, VoiceMeeter, Virtual Audio Cable";
}

} // namespace directpipe
