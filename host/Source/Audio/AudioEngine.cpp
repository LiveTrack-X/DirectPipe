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
#include "../Control/Log.h"
#include "../Platform/PlatformAudio.h"
#include "../Util/ScopedGuard.h"
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
        Log::error("AUDIO", "Device manager init failed: " + result);
        return false;
    }

    // Sync member atomics from the device that initialiseWithDefaultDevices started.
    // Avoid calling setAudioDeviceSetup here — it would restart the device (e.g. ASIO
    // resets buffer size to its default on restart). importFromJSON will apply the
    // correct SR/BS/channel routing via setBufferSize/setInputDevice/etc.
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        currentSampleRate_ = device->getCurrentSampleRate();
        currentBufferSize_ = device->getCurrentBufferSizeSamples();

        // Resolve empty device names (edge case: some systems leave them blank)
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);
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
            setup.useDefaultInputChannels = false;
            setup.useDefaultOutputChannels = false;
            setup.inputChannels.setRange(0, 2, true);
            setup.outputChannels.setRange(0, 2, true);
            auto setupResult = deviceManager_.setAudioDeviceSetup(setup, true);
            if (setupResult.isNotEmpty()) {
                Log::error("AUDIO", "Device setup failed (SR=" + juce::String(static_cast<int>(setup.sampleRate))
                    + " BS=" + juce::String(setup.bufferSize) + " in='" + setup.inputDeviceName
                    + "' out='" + setup.outputDeviceName + "'): " + setupResult);
            }
            // Re-fetch device pointer (setAudioDeviceSetup may have replaced it)
            if (auto* freshDev = deviceManager_.getCurrentAudioDevice()) {
                currentSampleRate_ = freshDev->getCurrentSampleRate();
                currentBufferSize_ = freshDev->getCurrentBufferSizeSamples();
            }
        }
    }

    // Audit: log all available device types and devices at startup
    if (Log::isAuditMode()) {
        juce::String auditInfo = "Available device types: ";
        for (auto* type : deviceManager_.getAvailableDeviceTypes()) {
            auditInfo += type->getTypeName() + ", ";
            type->scanForDevices();
            auto ins = type->getDeviceNames(true);
            auto outs = type->getDeviceNames(false);
            Log::audit("AUDIO", "  " + type->getTypeName() + " inputs: " + ins.joinIntoString(", "));
            Log::audit("AUDIO", "  " + type->getTypeName() + " outputs: " + outs.joinIntoString(", "));
        }
        Log::audit("AUDIO", auditInfo);
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
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
    }
    sharedMemWriter_.shutdown();
    ipcEnabled_.store(false, std::memory_order_relaxed);
    monitorOutput_.shutdown();
    outputRouter_.shutdown();
    vstChain_.releaseResources();
}

void AudioEngine::setIpcEnabled(bool enabled)
{
    if (enabled && !ipcAllowed_)
        return;  // Blocked in audio-only multi-instance mode

    if (enabled == ipcEnabled_.load(std::memory_order_relaxed))
        return;

    if (enabled) {
        uint32_t sr = static_cast<uint32_t>(currentSampleRate_);
        if (sharedMemWriter_.initialize(sr, 2)) {
            ipcEnabled_.store(true, std::memory_order_release);
            Log::info("IPC", "Output enabled (SR=" + juce::String(sr) + ")");
        } else {
            Log::error("IPC", "Output failed to initialize (SR=" + juce::String(sr) + ")");
        }
    } else {
        ipcEnabled_.store(false, std::memory_order_release);
        sharedMemWriter_.shutdown();
        Log::info("IPC", "Output disabled");
    }
}

ActionResult AudioEngine::setInputDevice(const juce::String& deviceName)
{
    { const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_); desiredInputDevice_ = deviceName; }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    // Skip restart if device is already set (avoids ASIO re-init that resets BS)
    if (setup.inputDeviceName == deviceName && !setup.inputChannels.isZero())
        return ActionResult::ok();

    setup.inputDeviceName = deviceName;

    // Ensure input channels are active (JUCE may clear them after output device change)
    if (setup.inputChannels.isZero()) {
        setup.useDefaultInputChannels = false;
        int numCh = channelMode_.load(std::memory_order_relaxed) == 1 ? 1 : 2;
        setup.inputChannels.setRange(0, numCh, true);
    }

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set input device '" + deviceName + "': " + result;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    // Clear device-loss state — user intentionally picked a new input device
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    deviceLost_.store(false, std::memory_order_relaxed);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    Log::info("AUDIO", "Input device set: " + deviceName);
    Log::audit("AUDIO", "Input device change: '" + setup.inputDeviceName + "' SR=" + juce::String(setup.sampleRate) + " BS=" + juce::String(setup.bufferSize));
    return ActionResult::ok();
}

ActionResult AudioEngine::setOutputDevice(const juce::String& deviceName)
{
    { const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_); desiredOutputDevice_ = deviceName; }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    // Skip restart if device is already set (avoids ASIO re-init that resets BS)
    if (setup.outputDeviceName == deviceName && !setup.outputChannels.isZero())
        return ActionResult::ok();

    setup.outputDeviceName = deviceName;

    // Preserve input channel routing when changing output device
    if (setup.inputChannels.isZero() && setup.inputDeviceName.isNotEmpty()) {
        setup.useDefaultInputChannels = false;
        int numCh = channelMode_.load(std::memory_order_relaxed) == 1 ? 1 : 2;
        setup.inputChannels.setRange(0, numCh, true);
    }
    // Ensure output channels are active
    if (setup.outputChannels.isZero()) {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange(0, 2, true);
    }

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set output device '" + deviceName + "': " + result;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    // Clear device-loss state — user intentionally picked a new output device
    if (outputAutoMuted_.load(std::memory_order_relaxed)) {
        outputAutoMuted_.store(false, std::memory_order_relaxed);
        outputMuted_.store(false, std::memory_order_relaxed);
    }
    deviceLost_.store(false, std::memory_order_relaxed);
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    Log::info("AUDIO", "Output device set: " + deviceName);
    Log::audit("AUDIO", "Output device change: '" + setup.outputDeviceName + "' SR=" + juce::String(setup.sampleRate) + " BS=" + juce::String(setup.bufferSize));
    return ActionResult::ok();
}

ActionResult AudioEngine::setAsioDevice(const juce::String& deviceName)
{
    { const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_); desiredInputDevice_ = deviceName; desiredOutputDevice_ = deviceName; }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = deviceName;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange(0, 2, true);
    setup.outputChannels.setRange(0, 2, true);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set ASIO device '" + deviceName + "': " + result;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    if (outputAutoMuted_.load(std::memory_order_relaxed)) {
        outputAutoMuted_.store(false, std::memory_order_relaxed);
        outputMuted_.store(false, std::memory_order_relaxed);
    }
    Log::info("AUDIO", "ASIO device set: " + deviceName);
    return ActionResult::ok();
}

ActionResult AudioEngine::setMonitorDevice(const juce::String& deviceName)
{
    if (monitorOutput_.setDevice(deviceName))
        return ActionResult::ok();
    return ActionResult::fail("Failed to set monitor device: " + deviceName);
}

ActionResult AudioEngine::setMonitorBufferSize(int bufferSize)
{
    if (monitorOutput_.setBufferSize(bufferSize))
        return ActionResult::ok();
    return ActionResult::fail("Failed to set monitor buffer size: " + juce::String(bufferSize));
}

void AudioEngine::presetAudioParams(double sampleRate, int bufferSize)
{
    bool anySet = false;
    if (sampleRate > 0) {
        currentSampleRate_ = sampleRate;
        desiredSampleRate_ = sampleRate;
        anySet = true;
    }
    if (bufferSize > 0) {
        currentBufferSize_ = bufferSize;
        desiredBufferSize_ = bufferSize;
        anySet = true;
    }
    if (anySet)
        desiredSRBSSet_ = true;
}

ActionResult AudioEngine::setBufferSize(int bufferSize)
{
    // Skip restart if device already has the requested buffer size
    {
        juce::AudioDeviceManager::AudioDeviceSetup cur;
        deviceManager_.getAudioDeviceSetup(cur);
        int actualBS = 0;
        if (auto* dev = deviceManager_.getCurrentAudioDevice())
            actualBS = dev->getCurrentBufferSizeSamples();
        if (cur.bufferSize == bufferSize && actualBS == bufferSize) {
            currentBufferSize_ = bufferSize;
            desiredBufferSize_ = bufferSize;
            desiredSRBSSet_ = true;
            return ActionResult::ok();
        }
    }

    currentBufferSize_ = bufferSize;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.bufferSize = bufferSize;
    juce::String error;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        error = deviceManager_.setAudioDeviceSetup(setup, true);
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (!device) return ActionResult::fail("No audio device available");

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
                {
                    AtomicGuard intentionalGuard(intentionalChange_);
                    deviceManager_.setAudioDeviceSetup(setup, true);
                }
                // Re-fetch device pointer (setAudioDeviceSetup may replace it)
                device = deviceManager_.getCurrentAudioDevice();
                if (!device) return ActionResult::fail("No audio device after buffer fallback");
                actual = device->getCurrentBufferSizeSamples();
            }
        }

        // Only notify if the final applied size differs from requested
        // (ASIO may report stale value briefly after setAudioDeviceSetup)
        if (actual != bufferSize) {
            auto msg = "Buffer: " + juce::String(bufferSize) + " -> " + juce::String(actual) + " smp";
            Log::info("AUDIO", "Buffer: requested " + juce::String(bufferSize) + " -> applied " + juce::String(actual));
            Log::audit("AUDIO", "Available buffer sizes: " + [&]() {
                juce::String s;
                for (int sz : supported) s += (s.isEmpty() ? "" : ", ") + juce::String(sz);
                return s;
            }());
            pushNotification(msg, NotificationLevel::Info);
        }
    }

    currentBufferSize_ = actual;
    desiredBufferSize_ = bufferSize;  // preserve user's request (not fallback)
    desiredSRBSSet_ = true;
    return ActionResult::ok(actual != bufferSize
        ? "Buffer: " + juce::String(bufferSize) + " -> " + juce::String(actual) + " smp"
        : juce::String());
}

void AudioEngine::setChannelMode(int channels)
{
    channelMode_.store(juce::jlimit(1, 2, channels), std::memory_order_relaxed);
}

ActionResult AudioEngine::setSampleRate(double sampleRate)
{
    // Skip restart if device already has the requested sample rate
    {
        juce::AudioDeviceManager::AudioDeviceSetup cur;
        deviceManager_.getAudioDeviceSetup(cur);
        double actualSR = 0;
        if (auto* dev = deviceManager_.getCurrentAudioDevice())
            actualSR = dev->getCurrentSampleRate();
        if (cur.sampleRate == sampleRate && actualSR == sampleRate) {
            currentSampleRate_ = sampleRate;
            desiredSampleRate_ = sampleRate;
            desiredSRBSSet_ = true;
            return ActionResult::ok();
        }
    }

    currentSampleRate_ = sampleRate;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.sampleRate = sampleRate;
    juce::String error;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        error = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (error.isNotEmpty()) {
        auto msg = "setSampleRate failed (requested=" + juce::String(sampleRate) + "): " + error;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    desiredSampleRate_ = sampleRate;
    desiredSRBSSet_ = true;
    Log::audit("AUDIO", "Sample rate set: " + juce::String(sampleRate));
    return ActionResult::ok();
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

juce::StringArray AudioEngine::getSharedModeOutputDevices()
{
    return PlatformAudio::getSharedModeOutputDevices(deviceManager_);
}

// ─── Device type management ─────────────────────────────────────────────────

ActionResult AudioEngine::setAudioDeviceType(const juce::String& typeName, const juce::String& preferredAsioDevice)
{
    auto currentType = getCurrentDeviceType();
    if (currentType == typeName) {
        desiredDeviceType_ = typeName;
        // Already on ASIO but possibly wrong device — switch to preferred
        if (typeName.containsIgnoreCase("ASIO") && preferredAsioDevice.isNotEmpty()) {
            juce::AudioDeviceManager::AudioDeviceSetup cur;
            deviceManager_.getAudioDeviceSetup(cur);
            if (cur.inputDeviceName != preferredAsioDevice)
                (void)setAsioDevice(preferredAsioDevice);
        }
        return ActionResult::ok();
    }

    // Save current driver's settings as snapshot for future restore
    if (currentType.isNotEmpty()) {
        DriverTypeSnapshot snap;
        snap.inputDevice = desiredInputDevice_;
        snap.outputDevice = desiredOutputDevice_;
        snap.sampleRate = currentSampleRate_;
        snap.bufferSize = currentBufferSize_;
        snap.outputNone = outputNone_.load(std::memory_order_relaxed);
        driverSnapshots_[currentType] = snap;
        Log::info("AUDIO", "Saved " + currentType + " snapshot: in='" + snap.inputDevice
            + "' out='" + snap.outputDevice + "' SR=" + juce::String(snap.sampleRate)
            + " BS=" + juce::String(snap.bufferSize)
            + (snap.outputNone ? " outputNone" : ""));
    }

    // Set intentional flag BEFORE removing callback, because removeAudioCallback
    // synchronously calls audioDeviceStopped() which checks intentionalChange_.
    intentionalChange_.store(true, std::memory_order_release);
    deviceManager_.removeAudioCallback(this);

    deviceManager_.setCurrentAudioDeviceType(typeName, true);

    // Check for saved snapshot of the target driver type
    auto snapIt = driverSnapshots_.find(typeName);
    bool hasSnapshot = snapIt != driverSnapshots_.end();

    // For ASIO, pick the preferred device (or first available)
    if (typeName.containsIgnoreCase("ASIO")) {
        if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
            type->scanForDevices();
            auto devices = type->getDeviceNames(false);
            if (devices.size() > 0) {
                // Build ordered try-list: preferred first, then lastAsio, then rest
                juce::StringArray tryOrder;
                if (preferredAsioDevice.isNotEmpty() && devices.contains(preferredAsioDevice))
                    tryOrder.add(preferredAsioDevice);
                if (lastAsioDevice_.isNotEmpty() && devices.contains(lastAsioDevice_)
                    && !tryOrder.contains(lastAsioDevice_))
                    tryOrder.add(lastAsioDevice_);
                for (auto& d : devices) {
                    if (!tryOrder.contains(d))
                        tryOrder.add(d);
                }

                // Use snapshot SR/BS if available (known to work on this ASIO device),
                // otherwise fall back to current values from the old driver.
                double sr = currentSampleRate_;
                int bs = currentBufferSize_;
                if (hasSnapshot) {
                    if (snapIt->second.sampleRate > 0) sr = snapIt->second.sampleRate;
                    if (snapIt->second.bufferSize > 0) bs = snapIt->second.bufferSize;
                }

                bool asioOpened = false;
                for (auto& deviceToUse : tryOrder) {
                    juce::AudioDeviceManager::AudioDeviceSetup setup;
                    setup.inputDeviceName = deviceToUse;
                    setup.outputDeviceName = deviceToUse;
                    setup.sampleRate = sr;
                    setup.bufferSize = bs;
                    setup.useDefaultInputChannels = false;
                    setup.useDefaultOutputChannels = false;
                    setup.inputChannels.setRange(0, 2, true);
                    setup.outputChannels.setRange(0, 2, true);

                    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
                    if (result.isNotEmpty()) {
                        Log::warn("AUDIO", "ASIO setup failed (device='" + deviceToUse + "' SR=" + juce::String(sr) + " BS=" + juce::String(bs) + "): " + result);
                        // Try with device defaults
                        setup.sampleRate = 0;  // let device choose
                        setup.bufferSize = 0;
                        result = deviceManager_.setAudioDeviceSetup(setup, true);
                        if (result.isNotEmpty()) {
                            Log::warn("AUDIO", "ASIO fallback also failed (device='" + deviceToUse + "'): " + result);
                            continue;  // try next device
                        }
                    }
                    asioOpened = true;
                    break;
                }

                if (!asioOpened) {
                    auto msg = juce::String("All ASIO devices failed — reverting to previous driver");
                    Log::error("AUDIO", msg);
                    intentionalChange_.store(false, std::memory_order_release);
                    deviceManager_.setCurrentAudioDeviceType(currentType, true);
                    deviceManager_.initialiseWithDefaultDevices(2, 2);
                    deviceManager_.addAudioCallback(this);
                    if (onDeviceError) onDeviceError("ASIO switch failed - reverted to previous driver");
                    return ActionResult::fail(msg);
                }
            }
        }
    } else {
        // Non-ASIO: initialize with defaults first, then try to restore snapshot
        auto result = deviceManager_.initialiseWithDefaultDevices(2, 2);
        if (result.isNotEmpty()) {
            auto msg = "Failed to switch to " + typeName + ": " + result;
            Log::error("AUDIO", msg);
            intentionalChange_.store(false, std::memory_order_release);
            deviceManager_.setCurrentAudioDeviceType(currentType, true);
            deviceManager_.initialiseWithDefaultDevices(2, 2);
            deviceManager_.addAudioCallback(this);
            if (onDeviceError) onDeviceError("Driver switch failed - reverted to " + currentType);
            return ActionResult::fail(msg);
        }

        // Resolve device names and apply settings (restore from snapshot if available)
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);

        if (hasSnapshot) {
            auto& snap = snapIt->second;
            // Check if saved devices still exist in this driver type
            if (auto* typeObj = deviceManager_.getCurrentDeviceTypeObject()) {
                typeObj->scanForDevices();
                if (typeObj->getDeviceNames(true).contains(snap.inputDevice))
                    setup.inputDeviceName = snap.inputDevice;
                if (typeObj->getDeviceNames(false).contains(snap.outputDevice))
                    setup.outputDeviceName = snap.outputDevice;
            }
            if (snap.sampleRate > 0) setup.sampleRate = snap.sampleRate;
            if (snap.bufferSize > 0) setup.bufferSize = snap.bufferSize;
            Log::info("AUDIO", "Restoring " + typeName + " snapshot: in='" + setup.inputDeviceName
                + "' out='" + setup.outputDeviceName + "' SR=" + juce::String(setup.sampleRate)
                + " BS=" + juce::String(setup.bufferSize));
        } else {
            setup.bufferSize = currentBufferSize_;
            setup.sampleRate = currentSampleRate_;
        }

        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, 2, true);
        setup.outputChannels.setRange(0, 2, true);
        deviceManager_.setAudioDeviceSetup(setup, true);
    }
    intentionalChange_.store(false, std::memory_order_release);

    // Update current SR/BS from actual device
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        currentSampleRate_ = device->getCurrentSampleRate();
        currentBufferSize_ = device->getCurrentBufferSizeSamples();
    }

    // Clear reconnection state after intentional type switch.
    // Without this, audioDeviceAboutToStart would see stale desired names
    // (e.g. ASIO "TOPPING") vs actual WASAPI names → "fallback detected" → infinite loop.
    deviceLost_.store(false, std::memory_order_relaxed);
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    if (outputAutoMuted_.load(std::memory_order_relaxed)) {
        outputAutoMuted_.store(false, std::memory_order_relaxed);
        outputMuted_.store(false, std::memory_order_relaxed);
    }
    // Clear "None" output state — new driver type has its own output device.
    // Without this, OUT mute button stays locked after WASAPI "None" → ASIO switch.
    if (outputNone_.load(std::memory_order_relaxed)) {
        outputNone_.store(false, std::memory_order_relaxed);
        outputMuted_.store(false, std::memory_order_relaxed);
    }
    // Restore "None" from snapshot if target driver had it saved
    if (hasSnapshot && snapIt->second.outputNone) {
        outputNone_.store(true, std::memory_order_relaxed);
        outputMuted_.store(true, std::memory_order_relaxed);
    }
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    desiredDeviceType_ = typeName;

    // Update desired device names to new driver's actual devices
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        if (setup.inputDeviceName.isNotEmpty())
            desiredInputDevice_ = setup.inputDeviceName;
        if (setup.outputDeviceName.isNotEmpty())
            desiredOutputDevice_ = setup.outputDeviceName;
    }

    deviceManager_.addAudioCallback(this);
    Log::info("AUDIO", "Driver switch: " + currentType + " -> " + typeName
        + " (SR=" + juce::String(currentSampleRate_) + " BS=" + juce::String(currentBufferSize_) + ")");
    if (Log::isAuditMode()) {
        if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
            Log::audit("AUDIO", "Driver switch details: device='" + dev->getName() + "' type=" + dev->getTypeName());
            auto sr = dev->getAvailableSampleRates();
            juce::String srList;
            for (auto r : sr) srList += (srList.isEmpty() ? "" : ", ") + juce::String(r);
            Log::audit("AUDIO", "  Available SR: " + srList);
            auto bs = dev->getAvailableBufferSizes();
            juce::String bsList;
            for (int b : bs) bsList += (bsList.isEmpty() ? "" : ", ") + juce::String(b);
            Log::audit("AUDIO", "  Available BS: " + bsList);
            Log::audit("AUDIO", "  Input channels: " + dev->getInputChannelNames().joinIntoString(", "));
            Log::audit("AUDIO", "  Output channels: " + dev->getOutputChannelNames().joinIntoString(", "));
        }
    }
    return ActionResult::ok();
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

    // Device restart (WASAPI session event, internal JUCE recovery, etc.)
    // — only resync the baseline counter so delta calculation stays correct.
    //   History is preserved so XRun display doesn't vanish on device restart.
    if (xrunBaselineResync_.exchange(false, std::memory_order_acquire)) {
        int xruns = device->getXRunCount();
        lastDeviceXRunCount_ = (xruns >= 0) ? xruns : 0;
    }

    // User-initiated full reset (Action::XRunReset) — clear everything.
    if (xrunResetRequested_.exchange(false, std::memory_order_acquire)) {
        int xruns = device->getXRunCount();
        lastDeviceXRunCount_ = (xruns >= 0) ? xruns : 0;
        std::memset(xrunHistory_, 0, sizeof(xrunHistory_));
        xrunHistoryIdx_ = 0;
        xrunAccumulatorTime_ = 0.0;
        recentXRuns_.store(0, std::memory_order_relaxed);
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

ActionResult AudioEngine::setActiveInputChannels(int firstChannel, int numChannels)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange(firstChannel, numChannels, true);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set input channels (first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels) + "): " + result;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    Log::audit("AUDIO", "Input channels set: first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels));
    return ActionResult::ok();
}

ActionResult AudioEngine::setActiveOutputChannels(int firstChannel, int numChannels)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange(firstChannel, numChannels, true);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set output channels (first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels) + "): " + result;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    Log::audit("AUDIO", "Output channels set: first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels));
    return ActionResult::ok();
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
    // ═══ RT AUDIO CALLBACK ═══════════════════════════════════════════════
    // RULES: 힙 할당 금지 | mutex 금지 | writeToLog 금지 | 예외 throw 금지
    // Pre-allocated: workBuffer_, emptyMidi_ | Atomics: relaxed ordering
    // ════════════════════════════════════════════════════════════════════

    // RT thread only — must NOT be called from the message thread
    jassert(!juce::MessageManager::getInstanceWithoutCreating()
            || !juce::MessageManager::getInstance()->isThisTheMessageThread());

    // One-time MMCSS registration on the audio callback thread (Windows only).
    // Boosts thread priority via Multimedia Class Scheduler Service, reducing
    // DPC latency interference. JUCE WASAPI registers at AVRT_PRIORITY_NORMAL;
    // we re-register at AVRT_PRIORITY_HIGH for stronger protection.
    // For ASIO (where JUCE does NO MMCSS), this is the only MMCSS registration.
#if defined(_WIN32)
    if (!mmcssRegistered_.load(std::memory_order_acquire)) {
        mmcssRegistered_.store(true, std::memory_order_relaxed);
        if (avSetMmThreadChar_ && avSetMmThreadPrio_) {
            DWORD taskIndex = 0;
            HANDLE task = avSetMmThreadChar_(L"Pro Audio", &taskIndex);
            if (task)
                avSetMmThreadPrio_(task, 2);  // AVRT_PRIORITY_HIGH
        }
    }
#endif

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
            if (outputChannelData[ch])
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
    // Only clear the channels we'll use (not all 8 pre-allocated channels)
    for (int ch = 0; ch < workChannels; ++ch)
        juce::FloatVectorOperations::clear(buffer.getWritePointer(ch), numSamples);

    // Input device lost → use silence instead of fallback device input.
    // Work buffer already cleared above, so just skip the copy.
    if (!inputDeviceLost_.load(std::memory_order_relaxed)) {
        if (chMode == 1) {
            // Mono mode: average all input channels to channel 0.
            // Single input channel: direct copy (no division needed).
            // Multiple input channels: sum then divide by channel count to prevent
            // +3dB boost when stereo mic sends identical signal on both channels.
            if (numInputChannels > 0 && inputChannelData[0] != nullptr) {
                buffer.copyFrom(0, 0, inputChannelData[0], numSamples);
                for (int ch = 1; ch < numInputChannels; ++ch) {
                    if (inputChannelData[ch] != nullptr)
                        buffer.addFrom(0, 0, inputChannelData[ch], numSamples);
                }
                if (numInputChannels > 1)
                    buffer.applyGain(0, 0, numSamples, 1.0f / static_cast<float>(numInputChannels));
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

    // CRITICAL: Steps 2.1-4 MUST execute in this exact order.
    // Safety Limiter (step 2.1) must run BEFORE all output paths (steps 2.5-4).
    // Reordering would cause un-limited audio to be recorded/broadcast/monitored.

    // 2.1. Safety Limiter — clip prevention for all output paths (RT-safe)
    safetyLimiter_.process(buffer, numSamples);

    // 2.5. Write processed audio to recorder (lock-free)
    recorder_.writeBlock(buffer, numSamples);

    // 2.6. Write to shared memory for Receiver VST (if IPC enabled)
    if (ipcEnabled_.load(std::memory_order_acquire)) {
        sharedMemWriter_.writeAudio(buffer, numSamples);
    }

    // 3. Route processed audio to monitor (separate WASAPI device)
    outputRouter_.routeAudio(buffer, numSamples);

    // 4. Apply output volume & copy to main output (AudioSettings Output device)
    float outVol = outputRouter_.getVolume(OutputRouter::Output::Main);
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (!outputChannelData[ch]) continue;
        if (ch < buffer.getNumChannels() && !outputMuted) {
            if (std::abs(outVol - 1.0f) < 0.001f) {
                // Unity gain — direct copy (most common path)
                std::memcpy(outputChannelData[ch], buffer.getReadPointer(ch),
                            sizeof(float) * static_cast<size_t>(numSamples));
            } else if (outVol > 0.001f) {
                // Apply gain
                const float* src = buffer.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    outputChannelData[ch][i] = src[i] * outVol;
            } else {
                // Volume ~0 — silence
                std::memset(outputChannelData[ch], 0,
                            sizeof(float) * static_cast<size_t>(numSamples));
            }
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

// ─── Device Start/Reconnection Handler ──────────────────────────────
// 이 콜백은 디바이스 스레드에서 호출됨 (Message 스레드 아님!)
// intentionalChange_: true면 사용자 의도 변경, false면 JUCE 자동 폴백 가능
// desiredInputDevice_/desiredOutputDevice_: 사용자가 원래 선택한 디바이스 보존
// 폴백 감지: 실제 디바이스 ≠ desired → deviceLost_ 유지, 재연결 대기
// ────────────────────────────────────────────────────────────────────
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

    // Reset MMCSS flag — new device means new audio thread, re-registration needed
    mmcssRegistered_.store(false, std::memory_order_release);

#if defined(_WIN32)
    if (!avSetMmThreadChar_) {
        if (auto* avrt = LoadLibraryA("avrt.dll")) {
            avSetMmThreadChar_ = reinterpret_cast<AvSetMmThreadCharFn>(
                GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
            avSetMmThreadPrio_ = reinterpret_cast<AvSetMmThreadPrioFn>(
                GetProcAddress(avrt, "AvSetMmThreadPriority"));
            // Guard: if either function missing (corrupted DLL), null both to allow future retry
            if (!avSetMmThreadChar_ || !avSetMmThreadPrio_) {
                avSetMmThreadChar_ = nullptr;
                avSetMmThreadPrio_ = nullptr;
            }
        }
    }
#endif

    // Remember last used ASIO device for future type switches
    if (device->getTypeName().containsIgnoreCase("ASIO"))
        lastAsioDevice_ = device->getName();

    // Detect JUCE auto-fallback by comparing actual vs desired device names.
    // Two modes:
    //   - deviceLost_ already true (after audioDeviceError): keep it true, don't overwrite desired
    //   - deviceLost_ false (startup / normal switch): update desired from actual
    //     (covers startup intermediate states and intentional device changes)
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);

        // Capture desired device names under lock (read from device thread)
        juce::String desiredIn, desiredOut;
        {
            const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
            desiredIn = desiredInputDevice_;
            desiredOut = desiredOutputDevice_;
        }

        bool inputMismatch = desiredIn.isNotEmpty()
                             && setup.inputDeviceName.isNotEmpty()
                             && setup.inputDeviceName != desiredIn;
        bool outputMismatch = desiredOut.isNotEmpty()
                              && setup.outputDeviceName.isNotEmpty()
                              && setup.outputDeviceName != desiredOut;

        bool wasLost = deviceLost_.load(std::memory_order_relaxed);

        if ((inputMismatch || outputMismatch) && wasLost) {
            // Real fallback after device error — keep deviceLost_ true,
            // don't overwrite desired names so checkReconnection restores them
            Log::warn("AUDIO", "Fallback detected: in='" + setup.inputDeviceName
                       + "' out='" + setup.outputDeviceName
                       + "' (desired: in='" + desiredIn
                       + "' out='" + desiredOut + "')");

            // Per-direction loss tracking:
            // Input lost → silence input in audio callback (don't use fallback mic)
            inputDeviceLost_.store(inputMismatch, std::memory_order_relaxed);
            // Output lost → auto-mute output (VST chain + monitor unaffected)
            if (outputMismatch && !outputAutoMuted_.load(std::memory_order_relaxed)) {
                outputMuted_.store(true, std::memory_order_relaxed);
                outputAutoMuted_.store(true, std::memory_order_relaxed);
            }

            // Restore any device that JUCE changed but is still available.
            // e.g. output unplugged → JUCE fallback changes both → restore input + BS/SR.
            juce::MessageManager::callAsync([this, alive = alive_,
                desiredIn, desiredOut,
                desiredSR = desiredSampleRate_, desiredBS = desiredBufferSize_] {
                if (!alive->load()) return;
                auto* type = deviceManager_.getCurrentDeviceTypeObject();
                if (!type) return;
                type->scanForDevices();
                bool inAvail = type->getDeviceNames(true).contains(desiredIn);
                bool outAvail = type->getDeviceNames(false).contains(desiredOut);
                if (!inAvail && !outAvail) return;  // both lost, wait for checkReconnection

                {
                    AtomicGuard intentionalGuard(intentionalChange_);
                    juce::AudioDeviceManager::AudioDeviceSetup s;
                    deviceManager_.getAudioDeviceSetup(s);
                    if (inAvail) s.inputDeviceName = desiredIn;
                    if (outAvail) s.outputDeviceName = desiredOut;
                    s.sampleRate = desiredSR;
                    s.bufferSize = desiredBS;
                    deviceManager_.setAudioDeviceSetup(s, true);
                }

                juce::String restored;
                if (inAvail && !outAvail) restored = "Input";
                else if (!inAvail && outAvail) restored = "Output";
                else restored = "Both devices";
                Log::info("AUDIO", restored + " restored after fallback (BS="
                    + juce::String(desiredBS) + " SR=" + juce::String(desiredSR) + ")");
            });
        } else {
            // Atomics — safe from any thread
            deviceLost_.store(false, std::memory_order_relaxed);
            inputDeviceLost_.store(false, std::memory_order_relaxed);
            // Auto-unmute output if it was auto-muted due to device loss
            if (outputAutoMuted_.load(std::memory_order_relaxed)) {
                outputAutoMuted_.store(false, std::memory_order_relaxed);
                // Keep outputMuted_ true if user intentionally selected "None"
                if (!outputNone_.load(std::memory_order_relaxed))
                    outputMuted_.store(false, std::memory_order_relaxed);
            }
            // Non-atomic [Message thread only] variables — must write on
            // message thread to avoid data race with device thread.
            auto aliveFlag = alive_;
            auto inName = setup.inputDeviceName;
            auto outName = setup.outputDeviceName;
            auto sr = setup.sampleRate;
            auto bs = setup.bufferSize;
            bool srbsSet = desiredSRBSSet_;
            juce::MessageManager::callAsync([this, aliveFlag, inName, outName, sr, bs, srbsSet] {
                if (!aliveFlag->load()) return;
                reconnectMissCount_ = 0;
                {
                    const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
                    if (inName.isNotEmpty())
                        desiredInputDevice_ = inName;
                    if (outName.isNotEmpty())
                        desiredOutputDevice_ = outName;
                }
                // Only set desired SR/BS from device if user/settings haven't
                // explicitly set them (first launch with no saved settings).
                if (!srbsSet) {
                    desiredSampleRate_ = sr;
                    desiredBufferSize_ = bs;
                }
            });
        }
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
        Log::info("AUDIO", "Device started: " + typeName
            + " | SR=" + juce::String(currentSampleRate_.load())
            + " | BS=" + juce::String(currentBufferSize_.load())
            + " | Available BS: [" + bsList + "]");

        // Audit: full device capabilities snapshot
        if (Log::isAuditMode()) {
            Log::audit("AUDIO", "Device name: " + device->getName());
            Log::audit("AUDIO", "Device type: " + typeName);
            auto sr = device->getAvailableSampleRates();
            juce::String srList;
            for (auto r : sr) srList += (srList.isEmpty() ? "" : ", ") + juce::String(r);
            Log::audit("AUDIO", "Available SR: " + srList);
            Log::audit("AUDIO", "Available BS: " + bsList);
            Log::audit("AUDIO", "Input channels: " + device->getInputChannelNames().joinIntoString(", "));
            Log::audit("AUDIO", "Output channels: " + device->getOutputChannelNames().joinIntoString(", "));
            Log::audit("AUDIO", "Active input bits: " + device->getActiveInputChannels().toString(2));
            Log::audit("AUDIO", "Active output bits: " + device->getActiveOutputChannels().toString(2));
            Log::audit("AUDIO", "Input latency: " + juce::String(device->getInputLatencyInSamples()) + " samples");
            Log::audit("AUDIO", "Output latency: " + juce::String(device->getOutputLatencyInSamples()) + " samples");
            Log::audit("AUDIO", "Desired devices: in='" + desiredInputDevice_ + "' out='" + desiredOutputDevice_ + "'");
        }
    }

    // Signal message-thread to resync xrun baseline counter (avoids data race
    // between device thread writing and message-thread timer reading).
    // NOTE: Do NOT clear recentXRuns_ or history here — device restarts (WASAPI
    // session events, internal JUCE recovery) would erase legitimate XRun display.
    // Only the user-initiated XRunReset action clears history.
    xrunBaselineResync_.store(true, std::memory_order_release);

    // Pre-allocate work buffer conservatively to avoid heap allocation in audio callback
    // Use 8 channels minimum to handle any device channel configuration
    int maxChannels = juce::jmax(8, device->getActiveInputChannels().countNumberOfSetBits(),
                                    device->getActiveOutputChannels().countNumberOfSetBits());
    workBuffer_.setSize(maxChannels, currentBufferSize_);

    // Touch all buffer pages to prevent page faults in the RT audio callback.
    // On first access, virtual memory pages may trigger soft faults, causing latency spikes.
    workBuffer_.clear();

    vstChain_.prepareToPlay(currentSampleRate_, currentBufferSize_);
    safetyLimiter_.prepareToPlay(currentSampleRate_);
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
            Log::error("IPC", "Failed to re-initialize after device restart (SR=" + juce::String(sr) + ")");
            ipcWasEnabled_ = false;
        }
    }

    Log::info("AUDIO", "Device ready: " + device->getName()
        + " @ " + juce::String(currentSampleRate_) + "Hz"
        + " / " + juce::String(currentBufferSize_) + " samples");
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

    // Stop recording to prevent WAV corruption at wrong sample rate after device loss
    if (recorder_.isRecording())
        recorder_.stopRecording();

    // Mark device as lost ONLY for external events (USB unplug, driver error).
    // Intentional changes (setInputDevice, setBufferSize, etc.) set intentionalChange_
    // before calling setAudioDeviceSetup, so we skip setting deviceLost_ for those.
    if (!intentionalChange_.load(std::memory_order_acquire))
        deviceLost_.store(true, std::memory_order_relaxed);

    Log::info("AUDIO", "Device stopped");
    Log::audit("AUDIO", "Device stopped: ipcWasEnabled=" + juce::String(ipcWasEnabled_ ? "true" : "false"));
}

void AudioEngine::audioDeviceError(const juce::String& errorMessage)
{
    Log::error("AUDIO", "Device error: " + errorMessage);
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

// ─── Dual-Mechanism Reconnection ────────────────────────────────────
// 1차: ChangeListener on deviceManager_ (즉시 감지)
// 2차: 3s 타이머 폴링 (ChangeListener 누락 시 폴백)
// reconnectMissCount_: 5회 실패 후 현재 디바이스 수용 (무한 루프 방지)
// WARNING: desiredInputDevice_/desiredOutputDevice_는 Message thread only
// ────────────────────────────────────────────────────────────────────
void AudioEngine::checkReconnection()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Main device reconnection
    if (deviceLost_.load(std::memory_order_relaxed)) {
        if (reconnectCooldown_ > 0) {
            --reconnectCooldown_;
        } else {
            reconnectCooldown_ = 90;  // ~3 seconds at 30Hz
            attemptReconnection();
        }
    }

    // Input device loss notification (edge detection)
    {
        bool isInputLost = inputDeviceLost_.load(std::memory_order_relaxed);
        if (!inputWasLost_ && isInputLost)
            pushNotification("Input device lost", NotificationLevel::Warning);
        else if (inputWasLost_ && !isInputLost && !deviceLost_.load(std::memory_order_relaxed))
            pushNotification("Input device restored", NotificationLevel::Info);
        inputWasLost_ = isInputLost;
    }

    // Output auto-mute notification (edge detection)
    {
        bool isAutoMuted = outputAutoMuted_.load(std::memory_order_relaxed);
        if (!outputWasAutoMuted_ && isAutoMuted)
            pushNotification("Output device lost - muted", NotificationLevel::Warning);
        else if (outputWasAutoMuted_ && !isAutoMuted)
            pushNotification("Output device restored", NotificationLevel::Info);
        outputWasAutoMuted_ = isAutoMuted;
    }

    // Monitor device reconnection (independent check)
    bool wasMonitorLost = monitorWasLost_;
    bool isMonitorLost = monitorOutput_.isDeviceLost();
    monitorWasLost_ = isMonitorLost;

    if (!wasMonitorLost && isMonitorLost)
        pushNotification("Monitor disconnected", NotificationLevel::Warning);

    monitorOutput_.checkReconnection();

    if (isMonitorLost && !monitorOutput_.isDeviceLost())
        pushNotification("Monitor reconnected", NotificationLevel::Info);
}

void AudioEngine::attemptReconnection()
{
    if (!deviceLost_.load(std::memory_order_relaxed)) return;
    if (attemptingReconnection_) return;  // Re-entrancy guard
    BoolGuard reconnectGuard(attemptingReconnection_);

    Log::info("AUDIO", "Reconnect attempt #" + juce::String(reconnectMissCount_ + 1)
        + " — desired in='" + desiredInputDevice_ + "' out='" + desiredOutputDevice_ + "'");

    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (!type) {
        Log::error("AUDIO", "Reconnection failed: no device type available");
        return;
    }

    // Rescan to pick up newly connected devices
    type->scanForDevices();

    auto inputs = type->getDeviceNames(true);
    auto outputs = type->getDeviceNames(false);

    Log::audit("AUDIO", "Reconnection scan: inputs=[" + inputs.joinIntoString(", ") + "] outputs=[" + outputs.joinIntoString(", ") + "]");
    Log::audit("AUDIO", "Reconnection desired: in='" + desiredInputDevice_ + "' out='" + desiredOutputDevice_ + "'");

    // Check if our desired devices are available
    bool inputOk = desiredInputDevice_.isEmpty() || inputs.contains(desiredInputDevice_);
    bool outputOk = desiredOutputDevice_.isEmpty() || outputs.contains(desiredOutputDevice_);

    if (!inputOk || !outputOk) {
        ++reconnectMissCount_;
        if (reconnectMissCount_ >= kMaxReconnectMisses) {
            // Output genuinely lost (physically unplugged) — keep waiting, don't accept fallback
            if (outputAutoMuted_.load(std::memory_order_relaxed)) {
                reconnectMissCount_ = 0;
                Log::audit("AUDIO", "Reconnection: output device lost, continuing to wait for '"
                    + desiredOutputDevice_ + "'");
            } else {
                // Stale cross-driver names — accept current driver's devices to break the loop
                Log::warn("AUDIO", "Reconnection: desired devices not found after "
                    + juce::String(kMaxReconnectMisses) + " attempts, accepting current devices");
                juce::AudioDeviceManager::AudioDeviceSetup curSetup;
                deviceManager_.getAudioDeviceSetup(curSetup);
                {
                    const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
                    if (curSetup.inputDeviceName.isNotEmpty())
                        desiredInputDevice_ = curSetup.inputDeviceName;
                    if (curSetup.outputDeviceName.isNotEmpty())
                        desiredOutputDevice_ = curSetup.outputDeviceName;
                }
                deviceLost_.store(false, std::memory_order_relaxed);
                inputDeviceLost_.store(false, std::memory_order_relaxed);
                reconnectMissCount_ = 0;
                pushNotification("Device not found, using current device", NotificationLevel::Warning);
                if (onDeviceReconnected) onDeviceReconnected();
            }
        } else {
            Log::info("AUDIO", "Reconnection: waiting for devices"
                + juce::String(!inputOk ? " (input: " + desiredInputDevice_ + ")" : "")
                + juce::String(!outputOk ? " (output: " + desiredOutputDevice_ + ")" : ""));
        }
        return;  // BoolGuard resets attemptingReconnection_ on scope exit
    }
    reconnectMissCount_ = 0;

    // Re-apply previous setup with desired device names, SR, and BS.
    // Preserve channel routing (important for ASIO channel pairs).
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    if (desiredInputDevice_.isNotEmpty())
        setup.inputDeviceName = desiredInputDevice_;
    if (desiredOutputDevice_.isNotEmpty())
        setup.outputDeviceName = desiredOutputDevice_;
    setup.sampleRate = desiredSampleRate_;
    setup.bufferSize = desiredBufferSize_;

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    if (result.isEmpty()) {
        // Explicitly clear — audioDeviceAboutToStart may keep it true
        // due to ASIO device name mismatch (single device vs desired I/O names)
        deviceLost_.store(false, std::memory_order_relaxed);
        inputDeviceLost_.store(false, std::memory_order_relaxed);
        if (outputAutoMuted_.load(std::memory_order_relaxed)) {
            outputAutoMuted_.store(false, std::memory_order_relaxed);
            // Keep outputMuted_ true if user intentionally selected "None"
            if (!outputNone_.load(std::memory_order_relaxed))
                outputMuted_.store(false, std::memory_order_relaxed);
        }
        reconnectCooldown_ = 0;
        reconnectMissCount_ = 0;
        Log::info("AUDIO", "Device reconnected: " + setup.inputDeviceName + " / " + setup.outputDeviceName);
        Log::audit("AUDIO", "Reconnection success: SR=" + juce::String(setup.sampleRate) + " BS=" + juce::String(setup.bufferSize));
        pushNotification("Device reconnected", NotificationLevel::Info);
        if (onDeviceReconnected) onDeviceReconnected();
    } else {
        Log::error("AUDIO", "Reconnection failed (in='" + setup.inputDeviceName + "' out='" + setup.outputDeviceName + "'): " + result);
    }
    // BoolGuard resets attemptingReconnection_ on scope exit
}

// ─── Notification queue ─────────────────────────────────────────────────────

// NOTE: notifQueue_[slot] assignment copies a juce::String (heap allocation).
// This is acceptable because pushNotification is only called from the device
// error thread (audioDeviceError) and message thread — never from the RT
// audio callback. If a future caller needs to push from the RT callback,
// switch to a fixed-size char array or pre-allocated string pool.
void AudioEngine::pushNotification(const juce::String& msg, NotificationLevel level)
{
    // MPSC-safe: device thread (audioDeviceError) and message thread can both call.
    // Use CAS loop to reserve a slot, write data, then publish via per-slot ready flag.
    uint32_t w = notifWriteIdx_.load(std::memory_order_relaxed);
    for (;;) {
        uint32_t r = notifReadIdx_.load(std::memory_order_acquire);
        if (w - r >= static_cast<uint32_t>(kNotifQueueSize)) return;  // Queue full, drop
        if (notifWriteIdx_.compare_exchange_weak(w, w + 1, std::memory_order_acq_rel))
            break;
        // CAS failed — another producer advanced w, retry with updated w
    }
    auto slot = w % static_cast<uint32_t>(kNotifQueueSize);
    notifQueue_[slot] = {msg, level};
    notifReady_[slot].store(true, std::memory_order_release);  // publish after data write
}

bool AudioEngine::popNotification(PendingNotification& out)
{
    uint32_t r = notifReadIdx_.load(std::memory_order_relaxed);
    uint32_t w = notifWriteIdx_.load(std::memory_order_acquire);
    if (r == w) return false;
    auto slot = r % static_cast<uint32_t>(kNotifQueueSize);
    if (!notifReady_[slot].load(std::memory_order_acquire)) return false;  // data not yet written
    out = notifQueue_[slot];
    notifReady_[slot].store(false, std::memory_order_relaxed);
    notifReadIdx_.store(r + 1, std::memory_order_release);
    return true;
}

float AudioEngine::calculateRMS(const float* data, int numSamples)
{
    if (numSamples <= 0) return 0.0f;
    // Loop unrolled for MSVC auto-vectorization (4 accumulators reduce dependency chain)
    float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f, sum3 = 0.0f;
    int i = 0;
    for (; i + 3 < numSamples; i += 4) {
        sum0 += data[i]   * data[i];
        sum1 += data[i+1] * data[i+1];
        sum2 += data[i+2] * data[i+2];
        sum3 += data[i+3] * data[i+3];
    }
    float sum = sum0 + sum1 + sum2 + sum3;
    for (; i < numSamples; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / static_cast<float>(numSamples));
}

} // namespace directpipe
