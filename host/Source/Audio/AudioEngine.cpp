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
 * @file AudioEngine.cpp
 * @brief Core audio engine implementation
 */

#include "AudioEngine.h"
#include <cmath>

namespace directpipe {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialize()
{
    // Initialize device manager with default settings
    auto result = deviceManager_.initialiseWithDefaultDevices(
        2,  // numInputChannels
        2   // numOutputChannels
    );

    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("[AUDIO] Init error: " + result);
        return false;
    }

    // Configure for WASAPI Shared mode (non-exclusive, preserves other apps' mic access)
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);

        // Resolve actual device names so that settings persist correctly.
        // initialiseWithDefaultDevices() may leave these empty on some systems.
        if (setup.inputDeviceName.isEmpty() || setup.outputDeviceName.isEmpty()) {
            if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
                if (setup.inputDeviceName.isEmpty()) {
                    auto inputs = type->getDeviceNames(true);
                    if (inputs.size() > 0)
                        setup.inputDeviceName = inputs[type->getDefaultDeviceIndex(true)];
                }
                if (setup.outputDeviceName.isEmpty()) {
                    auto outputs = type->getDeviceNames(false);
                    if (outputs.size() > 0)
                        setup.outputDeviceName = outputs[type->getDefaultDeviceIndex(false)];
                }
            }
        }

        setup.bufferSize = currentBufferSize_;
        setup.sampleRate = currentSampleRate_;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, 2, true);
        setup.outputChannels.setRange(0, 2, true);

        auto setupResult = deviceManager_.setAudioDeviceSetup(setup, true);
        if (setupResult.isNotEmpty()) {
            juce::Logger::writeToLog("[AUDIO] Setup error: " + setupResult);
        }
    }

    // Initialize the output router and wire outputs BEFORE registering callback
    // to ensure scaledBuffer_ is sized before the first audio callback fires
    outputRouter_.initialize(currentSampleRate_, currentBufferSize_);
    outputRouter_.setMonitorOutput(&monitorOutput_);

    // Register as the audio callback
    deviceManager_.addAudioCallback(this);

    // Listen for device list changes (plug/unplug detection)
    deviceManager_.addChangeListener(this);

    running_ = true;
    return true;
}

void AudioEngine::shutdown()
{
    if (!running_) return;

    alive_->store(false);
    running_ = false;
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
    sharedMemWriter_.shutdown();
    ipcEnabled_.store(false, std::memory_order_relaxed);
    monitorOutput_.shutdown();
    outputRouter_.shutdown();
    vstChain_.releaseResources();
}

void AudioEngine::setIpcEnabled(bool enabled)
{
    if (enabled == ipcEnabled_.load(std::memory_order_relaxed))
        return;

    if (enabled) {
        uint32_t sr = static_cast<uint32_t>(currentSampleRate_);
        if (sharedMemWriter_.initialize(sr, 2)) {
            ipcEnabled_.store(true, std::memory_order_release);
            juce::Logger::writeToLog("[IPC] Output enabled");
        } else {
            juce::Logger::writeToLog("[IPC] Output failed to initialize");
        }
    } else {
        ipcEnabled_.store(false, std::memory_order_release);
        sharedMemWriter_.shutdown();
        juce::Logger::writeToLog("[IPC] Output disabled");
    }
}

bool AudioEngine::setInputDevice(const juce::String& deviceName)
{
    desiredInputDevice_ = deviceName;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.inputDeviceName = deviceName;

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("[AUDIO] Failed to set input device: " + result);
        return false;
    }
    return true;
}

bool AudioEngine::setOutputDevice(const juce::String& deviceName)
{
    desiredOutputDevice_ = deviceName;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.outputDeviceName = deviceName;

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("[AUDIO] Failed to set output device: " + result);
        return false;
    }
    return true;
}

bool AudioEngine::setMonitorDevice(const juce::String& deviceName)
{
    return monitorOutput_.setDevice(deviceName);
}

bool AudioEngine::setMonitorBufferSize(int bufferSize)
{
    return monitorOutput_.setBufferSize(bufferSize);
}

void AudioEngine::setBufferSize(int bufferSize)
{
    currentBufferSize_ = bufferSize;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.bufferSize = bufferSize;
    auto error = deviceManager_.setAudioDeviceSetup(setup, true);

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (!device) return;

    int actual = device->getCurrentBufferSizeSamples();

    // If the requested size didn't apply, find the best alternative from device-supported sizes
    if (actual != bufferSize || error.isNotEmpty()) {
        auto supported = device->getAvailableBufferSizes();

        if (supported.size() > 0) {
            // Find closest supported size <= requested (prefer lower latency)
            int best = supported.getFirst();
            for (int s : supported) {
                if (s <= bufferSize)
                    best = s;
                else if (best > bufferSize)
                    best = juce::jmin(best, s);  // all are larger, pick smallest
            }

            // Try the best alternative if different from what we already got
            if (best != actual) {
                setup.bufferSize = best;
                deviceManager_.setAudioDeviceSetup(setup, true);
                actual = device->getCurrentBufferSizeSamples();
            }
        }

        juce::Logger::writeToLog("[AUDIO] Buffer: requested " + juce::String(bufferSize)
            + " -> applied " + juce::String(actual));
        pushNotification("Buffer: " + juce::String(bufferSize) + " -> "
            + juce::String(actual) + " smp", NotificationLevel::Info);
    }

    currentBufferSize_ = actual;
}

void AudioEngine::setChannelMode(int channels)
{
    channelMode_.store(juce::jlimit(1, 2, channels), std::memory_order_relaxed);
}

void AudioEngine::setSampleRate(double sampleRate)
{
    currentSampleRate_ = sampleRate;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.sampleRate = sampleRate;
    auto error = deviceManager_.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        juce::Logger::writeToLog("[AUDIO] setSampleRate error: " + error);
}

juce::StringArray AudioEngine::getAvailableInputDevices() const
{
    juce::StringArray devices;
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        devices = type->getDeviceNames(true);  // input devices
    }
    return devices;
}

juce::StringArray AudioEngine::getAvailableOutputDevices() const
{
    juce::StringArray devices;
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        devices = type->getDeviceNames(false);  // output devices
    }
    return devices;
}

juce::StringArray AudioEngine::getWasapiOutputDevices()
{
    juce::StringArray devices;
    for (auto* type : deviceManager_.getAvailableDeviceTypes()) {
        if (type->getTypeName().containsIgnoreCase("Windows Audio") ||
            type->getTypeName().containsIgnoreCase("DirectSound") ||
            type->getTypeName().containsIgnoreCase("WASAPI")) {
            type->scanForDevices();
            devices = type->getDeviceNames(false);
            if (devices.size() > 0)
                break;
        }
    }
    return devices;
}

// ─── Device type management ─────────────────────────────────────────────────

bool AudioEngine::setAudioDeviceType(const juce::String& typeName)
{
    auto currentType = getCurrentDeviceType();
    if (currentType == typeName) return true;

    // Remove callback before switching
    deviceManager_.removeAudioCallback(this);

    deviceManager_.setCurrentAudioDeviceType(typeName, true);

    // For ASIO, pick the first available device explicitly
    if (typeName.containsIgnoreCase("ASIO")) {
        if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
            type->scanForDevices();
            auto devices = type->getDeviceNames(false);
            if (devices.size() > 0) {
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                setup.inputDeviceName = devices[0];
                setup.outputDeviceName = devices[0];
                setup.sampleRate = currentSampleRate_;
                setup.bufferSize = currentBufferSize_;
                setup.useDefaultInputChannels = false;
                setup.useDefaultOutputChannels = false;
                setup.inputChannels.setRange(0, 2, true);
                setup.outputChannels.setRange(0, 2, true);

                auto result = deviceManager_.setAudioDeviceSetup(setup, true);
                if (result.isNotEmpty()) {
                    juce::Logger::writeToLog("[AUDIO] ASIO setup failed: " + result);
                    // Try with device defaults
                    setup.sampleRate = 0;  // let device choose
                    setup.bufferSize = 0;
                    result = deviceManager_.setAudioDeviceSetup(setup, true);
                    if (result.isNotEmpty()) {
                        juce::Logger::writeToLog("[AUDIO] ASIO fallback failed: " + result);
                        deviceManager_.setCurrentAudioDeviceType(currentType, true);
                        deviceManager_.initialiseWithDefaultDevices(2, 2);
                        deviceManager_.addAudioCallback(this);
                        if (onDeviceError) onDeviceError("ASIO switch failed - reverted to previous driver");
                        return false;
                    }
                }
            }
        }
    } else {
        // Non-ASIO: use default initialization
        auto result = deviceManager_.initialiseWithDefaultDevices(2, 2);
        if (result.isNotEmpty()) {
            juce::Logger::writeToLog("[AUDIO] Failed to switch to " + typeName + ": " + result);
            deviceManager_.setCurrentAudioDeviceType(currentType, true);
            deviceManager_.initialiseWithDefaultDevices(2, 2);
            deviceManager_.addAudioCallback(this);
            if (onDeviceError) onDeviceError("Driver switch failed - reverted to " + currentType);
            return false;
        }

        // Resolve device names and apply settings
        if (auto* device = deviceManager_.getCurrentAudioDevice()) {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager_.getAudioDeviceSetup(setup);
            setup.bufferSize = currentBufferSize_;
            setup.sampleRate = currentSampleRate_;
            setup.useDefaultInputChannels = false;
            setup.useDefaultOutputChannels = false;
            setup.inputChannels.setRange(0, 2, true);
            setup.outputChannels.setRange(0, 2, true);
            deviceManager_.setAudioDeviceSetup(setup, true);
        }
    }

    // Update current SR/BS from actual device
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        currentSampleRate_ = device->getCurrentSampleRate();
        currentBufferSize_ = device->getCurrentBufferSizeSamples();
    }

    deviceManager_.addAudioCallback(this);

    juce::Logger::writeToLog("[AUDIO] Switched to " + typeName);
    return true;
}

juce::String AudioEngine::getCurrentDeviceType() const
{
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
        return type->getTypeName();
    return {};
}

juce::StringArray AudioEngine::getAvailableDeviceTypes()
{
    juce::StringArray types;
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
        types.add(type->getTypeName());
    return types;
}

juce::Array<double> AudioEngine::getAvailableSampleRates() const
{
    juce::Array<double> rates;
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        for (auto sr : device->getAvailableSampleRates())
            rates.add(sr);
    }
    if (rates.isEmpty()) {
        // Fallback defaults
        rates.add(44100.0);
        rates.add(48000.0);
        rates.add(96000.0);
    }
    return rates;
}

juce::Array<int> AudioEngine::getAvailableBufferSizes() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getAvailableBufferSizes();

    return {};
}

bool AudioEngine::showAsioControlPanel()
{
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        if (device->hasControlPanel()) {
            device->showControlPanel();
            return true;
        }
    }
    return false;
}

int AudioEngine::getRecentXRunCount() const
{
    return recentXRuns_.load(std::memory_order_relaxed);
}

void AudioEngine::updateXRunTracking()
{
    // Called from message-thread timer (~30Hz). Accumulates xrun deltas
    // into 1-second buckets in a 60-slot circular buffer.
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (!device) return;

    // Handle reset signal from audioDeviceAboutToStart (device thread)
    if (xrunResetRequested_.exchange(false, std::memory_order_acquire)) {
        int xruns = device->getXRunCount();
        lastDeviceXRunCount_ = (xruns >= 0) ? xruns : 0;
        std::memset(xrunHistory_, 0, sizeof(xrunHistory_));
        xrunHistoryIdx_ = 0;
        xrunAccumulatorTime_ = 0.0;
        return;
    }

    int currentCount = device->getXRunCount();
    if (currentCount < 0) return;  // Unsupported

    int delta = currentCount - lastDeviceXRunCount_;
    if (delta < 0) delta = 0;  // Device was reset
    lastDeviceXRunCount_ = currentCount;

    // Accumulate time (~33ms per call at 30Hz)
    xrunAccumulatorTime_ += 1.0 / 30.0;

    // Every ~1 second, rotate the circular buffer
    if (xrunAccumulatorTime_ >= 1.0) {
        xrunAccumulatorTime_ -= 1.0;

        // Move to next slot, subtract the old value from total
        xrunHistoryIdx_ = (xrunHistoryIdx_ + 1) % 60;
        xrunHistory_[xrunHistoryIdx_] = 0;

        // Recompute total from all 60 slots
        int total = 0;
        for (int i = 0; i < 60; ++i)
            total += xrunHistory_[i];
        recentXRuns_.store(total, std::memory_order_relaxed);
    }

    // Add current delta to the active slot
    if (delta > 0) {
        xrunHistory_[xrunHistoryIdx_] += delta;

        // Update total immediately
        int total = 0;
        for (int i = 0; i < 60; ++i)
            total += xrunHistory_[i];
        recentXRuns_.store(total, std::memory_order_relaxed);
    }
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getInputChannelNames();
    return {};
}

juce::StringArray AudioEngine::getOutputChannelNames() const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getOutputChannelNames();
    return {};
}

bool AudioEngine::setActiveInputChannels(int firstChannel, int numChannels)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange(firstChannel, numChannels, true);

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("[AUDIO] Failed to set input channels: " + result);
        return false;
    }
    return true;
}

bool AudioEngine::setActiveOutputChannels(int firstChannel, int numChannels)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange(firstChannel, numChannels, true);

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("[AUDIO] Failed to set output channels: " + result);
        return false;
    }
    return true;
}

int AudioEngine::getActiveInputChannelOffset() const
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    return setup.inputChannels.findNextSetBit(0);
}

int AudioEngine::getActiveOutputChannelOffset() const
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    return setup.outputChannels.findNextSetBit(0);
}

// ═══════════════════════════════════════════════════════════════════
// Real-time audio callback — NO allocations, NO locks, NO I/O
// ═══════════════════════════════════════════════════════════════════

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Flush denormalized floats to zero — prevents 10-100x CPU spikes
    // when VST plugins process near-silence (reverb tails, compressor release, etc.)
    juce::ScopedNoDenormals noDenormals;

    latencyMonitor_.markCallbackStart();

    const int chMode = channelMode_.load(std::memory_order_relaxed);
    const float gain = inputGain_.load(std::memory_order_relaxed);
    const bool muted = muted_.load(std::memory_order_relaxed);
    const bool outputMuted = outputMuted_.load(std::memory_order_relaxed);

    numSamples = juce::jmin(numSamples, workBuffer_.getNumSamples());

    // ── Fast path: panic muted → zero output, skip all processing ──
    if (muted) {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            std::memset(outputChannelData[ch], 0,
                        sizeof(float) * static_cast<size_t>(numSamples));
        inputLevel_.store(0.0f, std::memory_order_relaxed);
        outputLevel_.store(0.0f, std::memory_order_relaxed);
        latencyMonitor_.markCallbackEnd();
        return;
    }

    // 1. Copy input data into the pre-allocated work buffer (no heap allocation)
    auto& buffer = workBuffer_;
    int workChannels = juce::jmin(
        juce::jmax(chMode, juce::jmax(numInputChannels, numOutputChannels)),
        buffer.getNumChannels());
    buffer.clear();

    if (chMode == 1) {
        // Mono mode: sum all input channels to channel 0 (full gain, no attenuation)
        if (numInputChannels > 0 && inputChannelData[0] != nullptr) {
            buffer.copyFrom(0, 0, inputChannelData[0], numSamples);
            for (int ch = 1; ch < numInputChannels; ++ch) {
                if (inputChannelData[ch] != nullptr)
                    buffer.addFrom(0, 0, inputChannelData[ch], numSamples);
            }
        }
        // Duplicate mono to channel 1 so both L/R outputs carry the same signal
        if (buffer.getNumChannels() > 1)
            buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    } else {
        // Stereo mode: copy channels as-is
        for (int ch = 0; ch < numInputChannels && ch < workChannels; ++ch) {
            if (inputChannelData[ch] != nullptr) {
                buffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);
            }
        }
    }

    // Apply input gain (SIMD-optimized inside JUCE)
    if (std::abs(gain - 1.0f) > 0.001f) {
        buffer.applyGain(gain);
    }

    // Measure input level (RMS) — decimated: every 4th callback (~23Hz at 48kHz/512smp).
    // UI timer runs at 30Hz so per-callback RMS is wasted work.
    const bool measureThisCallback = (++rmsDecimationCounter_ & 3) == 0;
    if (measureThisCallback && buffer.getNumChannels() > 0) {
        float rms = calculateRMS(buffer.getReadPointer(0), numSamples);
        inputLevel_.store(rms, std::memory_order_relaxed);
    }

    // 2. Process through VST plugin chain (inline, zero additional latency)
    //    Each plugin's bypass flag is atomic — can be toggled from any thread
    vstChain_.processBlock(buffer, numSamples);

    // 2.5. Write processed audio to recorder (lock-free)
    recorder_.writeBlock(buffer, numSamples);

    // 2.6. Write to shared memory for Receiver VST (if IPC enabled)
    if (ipcEnabled_.load(std::memory_order_acquire)) {
        sharedMemWriter_.writeAudio(buffer, numSamples);
    }

    // 3. Route processed audio to monitor (separate WASAPI device)
    outputRouter_.routeAudio(buffer, numSamples);

    // 4. Copy processed audio to main output (AudioSettings Output device)
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (ch < buffer.getNumChannels() && !outputMuted) {
            std::memcpy(outputChannelData[ch], buffer.getReadPointer(ch),
                        sizeof(float) * static_cast<size_t>(numSamples));
        } else {
            std::memset(outputChannelData[ch], 0,
                        sizeof(float) * static_cast<size_t>(numSamples));
        }
    }

    // Measure output level — same decimation as input
    if (measureThisCallback && buffer.getNumChannels() > 0) {
        float rms = calculateRMS(buffer.getReadPointer(0), numSamples);
        if (buffer.getNumChannels() > 1)
            rms = juce::jmax(rms, calculateRMS(buffer.getReadPointer(1), numSamples));
        outputLevel_.store(rms, std::memory_order_relaxed);
    }

    latencyMonitor_.markCallbackEnd();
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

    // Device is running — clear lost flag and save current device names
    deviceLost_.store(false, std::memory_order_relaxed);
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);
        if (setup.inputDeviceName.isNotEmpty())
            desiredInputDevice_ = setup.inputDeviceName;
        if (setup.outputDeviceName.isNotEmpty())
            desiredOutputDevice_ = setup.outputDeviceName;
    }

    // Stop recording before device parameters change (prevents WAV corruption)
    if (recorder_.isRecording())
        recorder_.stopRecording();

    currentSampleRate_ = device->getCurrentSampleRate();
    currentBufferSize_ = device->getCurrentBufferSizeSamples();

    // Log device capabilities for diagnostics
    {
        auto typeName = device->getTypeName();
        auto bs = device->getAvailableBufferSizes();
        juce::String bsList;
        for (int i = 0; i < bs.size(); ++i)
            bsList += (i > 0 ? ", " : "") + juce::String(bs[i]);
        juce::Logger::writeToLog("[AUDIO] Device started: " + typeName
            + " | SR=" + juce::String(currentSampleRate_.load())
            + " | BS=" + juce::String(currentBufferSize_.load())
            + " | Available BS: [" + bsList + "]");
    }

    // Signal message-thread to reset xrun tracking (avoids data race
    // between device thread writing and message-thread timer reading).
    recentXRuns_.store(0, std::memory_order_relaxed);
    xrunResetRequested_.store(true, std::memory_order_release);

    // Pre-allocate work buffer conservatively to avoid heap allocation in audio callback
    // Use 8 channels minimum to handle any device channel configuration
    int maxChannels = juce::jmax(8, device->getActiveInputChannels().countNumberOfSetBits(),
                                    device->getActiveOutputChannels().countNumberOfSetBits());
    workBuffer_.setSize(maxChannels, currentBufferSize_);

    // Touch all buffer pages to prevent page faults in the RT audio callback.
    // On first access, virtual memory pages may trigger soft faults, causing latency spikes.
    workBuffer_.clear();

    vstChain_.prepareToPlay(currentSampleRate_, currentBufferSize_);
    outputRouter_.initialize(currentSampleRate_, currentBufferSize_);
    latencyMonitor_.reset(currentSampleRate_, currentBufferSize_);

    // Re-initialize monitor output if configured (SR may have changed).
    // Deferred to message thread to avoid blocking device startup with
    // monitor WASAPI teardown/restart (potential deadlock between device managers).
    if (monitorOutput_.getStatus() != VirtualCableStatus::NotConfigured) {
        auto devName = monitorOutput_.getDeviceName();
        double sr = currentSampleRate_;
        int bs = monitorOutput_.getPreferredBufferSize();
        auto aliveFlag = alive_;
        juce::MessageManager::callAsync([this, aliveFlag, devName, sr, bs]() {
            if (!aliveFlag->load()) return;
            monitorOutput_.initialize(devName, sr, bs);
        });
    }

    // Re-initialize IPC if it was enabled before device stopped
    if (ipcWasEnabled_) {
        uint32_t sr = static_cast<uint32_t>(currentSampleRate_);
        if (sharedMemWriter_.initialize(sr, 2)) {
            ipcEnabled_.store(true, std::memory_order_release);
            ipcWasEnabled_ = false;
        } else {
            juce::Logger::writeToLog("[IPC] Failed to re-initialize after device restart");
            ipcWasEnabled_ = false;
        }
    }

    juce::Logger::writeToLog(
        "[AUDIO] Device started: " + device->getName() +
        " @ " + juce::String(currentSampleRate_) + "Hz" +
        " / " + juce::String(currentBufferSize_) + " samples");
}

void AudioEngine::audioDeviceStopped()
{
    // Remember IPC state for re-init in audioDeviceAboutToStart,
    // then disable before shutdown to prevent audio callback from
    // calling writeAudio on a shutdown writer during device restart
    ipcWasEnabled_ = ipcEnabled_.load(std::memory_order_acquire);
    ipcEnabled_.store(false, std::memory_order_release);

    vstChain_.releaseResources();
    outputRouter_.shutdown();
    sharedMemWriter_.shutdown();
    juce::Logger::writeToLog("[AUDIO] Device stopped");
}

void AudioEngine::audioDeviceError(const juce::String& errorMessage)
{
    juce::Logger::writeToLog("[AUDIO] Device error: " + errorMessage);
    pushNotification("Device disconnected", NotificationLevel::Warning);
    deviceLost_.store(true, std::memory_order_relaxed);
}

// ─── Device reconnection ────────────────────────────────────────────────────

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    // Device list changed (plug/unplug). If we lost our device, try to reconnect.
    if (deviceLost_.load(std::memory_order_relaxed))
        attemptReconnection();
}

void AudioEngine::checkReconnection()
{
    // Main device reconnection
    if (deviceLost_.load(std::memory_order_relaxed)) {
        if (reconnectCooldown_ > 0) {
            --reconnectCooldown_;
        } else {
            reconnectCooldown_ = 90;  // ~3 seconds at 30Hz
            attemptReconnection();
        }
    }

    // Monitor device reconnection (independent check)
    bool wasMonitorLost = monitorOutput_.isDeviceLost();
    monitorOutput_.checkReconnection();
    if (wasMonitorLost && !monitorOutput_.isDeviceLost())
        pushNotification("Monitor reconnected", NotificationLevel::Info);
}

void AudioEngine::attemptReconnection()
{
    if (!deviceLost_.load(std::memory_order_relaxed)) return;
    if (attemptingReconnection_) return;  // Re-entrancy guard
    attemptingReconnection_ = true;

    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (!type) {
        juce::Logger::writeToLog("[AUDIO] Reconnection failed: no device type available");
        attemptingReconnection_ = false;
        return;
    }

    // Rescan to pick up newly connected devices
    type->scanForDevices();

    auto inputs = type->getDeviceNames(true);
    auto outputs = type->getDeviceNames(false);

    // Check if our desired devices are available
    bool inputOk = desiredInputDevice_.isEmpty() || inputs.contains(desiredInputDevice_);
    bool outputOk = desiredOutputDevice_.isEmpty() || outputs.contains(desiredOutputDevice_);

    if (!inputOk || !outputOk) {
        juce::Logger::writeToLog("[AUDIO] Reconnection: waiting for devices"
            + juce::String(!inputOk ? " (input: " + desiredInputDevice_ + ")" : "")
            + juce::String(!outputOk ? " (output: " + desiredOutputDevice_ + ")" : ""));
        attemptingReconnection_ = false;
        return;
    }

    // Re-apply previous setup with desired device names.
    // Preserve SR, BS, and channel routing (important for ASIO channel pairs).
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    if (desiredInputDevice_.isNotEmpty())
        setup.inputDeviceName = desiredInputDevice_;
    if (desiredOutputDevice_.isNotEmpty())
        setup.outputDeviceName = desiredOutputDevice_;

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isEmpty()) {
        // deviceLost_ cleared in audioDeviceAboutToStart
        reconnectCooldown_ = 0;
        juce::Logger::writeToLog("[AUDIO] Device reconnected: "
            + setup.inputDeviceName + " / " + setup.outputDeviceName);
        pushNotification("Device reconnected", NotificationLevel::Info);
        if (onDeviceReconnected) onDeviceReconnected();
    } else {
        juce::Logger::writeToLog("[AUDIO] Reconnection failed: " + result);
    }
    attemptingReconnection_ = false;
}

// ─── Notification queue ─────────────────────────────────────────────────────

void AudioEngine::pushNotification(const juce::String& msg, NotificationLevel level)
{
    uint32_t w = notifWriteIdx_.load(std::memory_order_relaxed);
    uint32_t r = notifReadIdx_.load(std::memory_order_acquire);
    if (w - r >= static_cast<uint32_t>(kNotifQueueSize)) return;  // Queue full, drop (RT-safe)
    notifQueue_[w % static_cast<uint32_t>(kNotifQueueSize)] = {msg, level};
    notifWriteIdx_.store(w + 1, std::memory_order_release);
}

bool AudioEngine::popNotification(PendingNotification& out)
{
    uint32_t r = notifReadIdx_.load(std::memory_order_relaxed);
    uint32_t w = notifWriteIdx_.load(std::memory_order_acquire);
    if (r == w) return false;
    out = notifQueue_[r % static_cast<uint32_t>(kNotifQueueSize)];
    notifReadIdx_.store(r + 1, std::memory_order_release);
    return true;
}

float AudioEngine::calculateRMS(const float* data, int numSamples)
{
    if (numSamples <= 0) return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / static_cast<float>(numSamples));
}

} // namespace directpipe
