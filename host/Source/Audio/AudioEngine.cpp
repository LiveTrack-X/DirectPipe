// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
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
namespace {

constexpr int kReconnectCooldownTicks = 90;

juce::String channelMaskToLogString(const juce::BigInteger& mask)
{
    juce::StringArray bits;
    for (int bit = mask.findNextSetBit(0); bit >= 0; bit = mask.findNextSetBit(bit + 1))
        bits.add(juce::String(bit));
    return bits.isEmpty() ? juce::String("none") : bits.joinIntoString(",");
}

void addUniqueDevice(juce::StringArray& devices, const juce::String& device)
{
    if (device.isNotEmpty() && !devices.contains(device))
        devices.add(device);
}

juce::String selectAsioDuplexDevice(const juce::String& inputDevice,
                                    const juce::String& outputDevice)
{
    return inputDevice.isNotEmpty() ? inputDevice : outputDevice;
}

juce::AudioIODeviceType* findDeviceType(juce::AudioDeviceManager& deviceManager,
                                        const juce::String& typeName)
{
    if (typeName.isEmpty())
        return nullptr;

    for (auto* type : deviceManager.getAvailableDeviceTypes()) {
        if (type && type->getTypeName() == typeName)
            return type;
    }

    return nullptr;
}

void useDefaultConfiguredChannels(juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                  bool input, bool output)
{
    // Let the driver pick its native active channel mask. This is deliberately
    // different from forcing bit 0: stereo devices stay stereo, while mono
    // devices can still open if they only expose one usable channel.
    if (input && setup.inputDeviceName.isNotEmpty()) {
        setup.useDefaultInputChannels = true;
        setup.inputChannels.clear();
    }
    if (output && setup.outputDeviceName.isNotEmpty()) {
        setup.useDefaultOutputChannels = true;
        setup.outputChannels.clear();
    }
}

bool sampleRatesDiffer(double a, double b)
{
    return a > 0.0 && b > 0.0 && std::abs(a - b) > 1.0;
}

bool explicitMaskIsActive(const juce::BigInteger& requested,
                          const juce::BigInteger& active)
{
    if (requested.isZero())
        return active.countNumberOfSetBits() > 0;

    for (int bit = requested.findNextSetBit(0); bit >= 0; bit = requested.findNextSetBit(bit + 1)) {
        if (!active[bit])
            return false;
    }

    return true;
}

bool channelSelectionReady(bool configured,
                           bool useDefaultChannels,
                           const juce::BigInteger& requested,
                           const juce::BigInteger& active,
                           int availableChannels)
{
    if (!configured)
        return true;

    const int activeCount = active.countNumberOfSetBits();
    const int requiredCount = availableChannels == 1 ? 1 : 2;
    if (useDefaultChannels)
        return activeCount >= requiredCount;

    if (!explicitMaskIsActive(requested, active))
        return false;

    if (availableChannels == 1)
        return requested.countNumberOfSetBits() == 1;
    if (availableChannels > 1)
        return requested.countNumberOfSetBits() >= 2;

    // An unknown layout cannot prove that a singleton is a genuine mono
    // device. Explicit pair requests remain verifiable from the active mask.
    return requested.countNumberOfSetBits() >= 2;
}

juce::String normalizeAudioDeviceName(juce::String name)
{
    name = name.trim().toLowerCase();
    return name.removeCharacters(" \t\r\n");
}

bool audioDeviceNamesExactlyMatch(const juce::String& a, const juce::String& b)
{
    const auto lhs = normalizeAudioDeviceName(a);
    const auto rhs = normalizeAudioDeviceName(b);
    return lhs.isNotEmpty() && rhs.isNotEmpty() && lhs == rhs;
}

} // namespace

namespace audio_device_recovery_detail {

constexpr double kEndpointEventReopenSuppressionMs = 1000.0;

juce::String initialiseWithDefaultDeviceFallbacks(
    juce::AudioDeviceManager& deviceManager,
    const juce::String& logContext)
{
    auto result = deviceManager.initialiseWithDefaultDevices(2, 2);
    if (result.isNotEmpty()) {
        Log::warn("AUDIO", logContext + " retry with single input after: " + result);
        result = deviceManager.initialiseWithDefaultDevices(1, 2);
    }
    if (result.isNotEmpty()) {
        Log::warn("AUDIO", logContext + " retry with single input/output after: " + result);
        result = deviceManager.initialiseWithDefaultDevices(1, 1);
    }
    return result;
}

juce::String forceReopenAudioDevice(
    juce::AudioDeviceManager& deviceManager,
    const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    // JUCE treats an unchanged setup as a no-op while a device is open.
    // External Windows endpoint changes need a real close/recreate cycle.
    deviceManager.closeAudioDevice();
    return deviceManager.setAudioDeviceSetup(setup, true);
}

bool monitorDeviceConflictsWithExclusiveMainOutput(
    const juce::String& deviceType,
    const juce::String& mainOutputDevice,
    const juce::String& monitorDevice)
{
    return PlatformAudio::isExclusiveDriverType(deviceType)
        && audioDeviceNamesExactlyMatch(mainOutputDevice, monitorDevice);
}

bool endpointEventIsSuppressed(double nowMs, double suppressedUntilMs) noexcept
{
    return suppressedUntilMs > 0.0 && nowMs < suppressedUntilMs;
}

int reconnectCooldownAfterRecovery(bool recovered) noexcept
{
    return recovered ? 0 : kReconnectCooldownTicks;
}

bool shouldSuspendMonitorBeforeExclusiveOpen(
    const juce::String& deviceType,
    const juce::String& targetOutputDevice,
    const juce::String& monitorDevice,
    bool suspendForAnyExclusiveTypeSwitch)
{
    if (!PlatformAudio::isExclusiveDriverType(deviceType)
        || monitorDevice.isEmpty())
        return false;

    return suspendForAnyExclusiveTypeSwitch
        || audioDeviceNamesExactlyMatch(targetOutputDevice, monitorDevice);
}

bool prepareOutputDeviceChangeChannels(
    juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    bool synthesizedInputPair = false;

    // A zero mask is meaningful while useDefaultInputChannels is true. Keep
    // that policy intact so JUCE can reopen a genuine one-channel input with
    // the manager's existing input-channel requirement.
    if (setup.inputDeviceName.isNotEmpty()
        && setup.inputChannels.isZero()
        && !setup.useDefaultInputChannels) {
        setup.inputChannels.setRange(0, 2, true);
        synthesizedInputPair = true;
    }

    if (setup.outputChannels.isZero()) {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange(0, 2, true);
    }

    return synthesizedInputPair;
}

bool seedCompatibleWindowsDriverSnapshot(
    const juce::String& currentType,
    const juce::String& targetType,
    bool targetHasSnapshot,
    const DriverTypeSnapshot& currentSnapshot,
    DriverTypeSnapshot& seededSnapshot)
{
    const bool currentIsWindowsAudio =
        currentType.containsIgnoreCase("Windows Audio");
    const bool targetIsWindowsAudio =
        targetType.containsIgnoreCase("Windows Audio");
    if (targetHasSnapshot || !currentIsWindowsAudio || !targetIsWindowsAudio)
        return false;

    seededSnapshot = currentSnapshot;
    // A first switch should preserve the user's endpoint/routing choices, but
    // "None" remains a per-driver decision and must not leak into a new type.
    seededSnapshot.outputNone = false;
    return true;
}

bool snapshotEndpointsAvailable(
    const DriverTypeSnapshot& snapshot,
    const juce::StringArray& availableInputs,
    const juce::StringArray& availableOutputs)
{
    const bool inputAvailable =
        snapshot.inputDevice.isEmpty()
        || availableInputs.contains(snapshot.inputDevice);
    const bool outputAvailable =
        snapshot.outputNone
        || snapshot.outputDevice.isEmpty()
        || availableOutputs.contains(snapshot.outputDevice);
    return inputAvailable && outputAvailable;
}

juce::String restoreDriverSnapshot(
    juce::AudioDeviceManager& deviceManager,
    const DriverTypeSnapshot& snapshot)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = snapshot.inputDevice;
    setup.outputDeviceName = snapshot.outputNone
        ? juce::String()
        : snapshot.outputDevice;
    setup.sampleRate = snapshot.sampleRate;
    setup.bufferSize = snapshot.bufferSize;

    setup.useDefaultInputChannels = snapshot.inputChannels.isZero();
    setup.inputChannels = snapshot.inputChannels;
    setup.useDefaultOutputChannels =
        !snapshot.outputNone && snapshot.outputChannels.isZero();
    if (!snapshot.outputNone)
        setup.outputChannels = snapshot.outputChannels;

    auto result = forceReopenAudioDevice(deviceManager, setup);
    if (result.isNotEmpty()) {
        useDefaultConfiguredChannels(
            setup,
            setup.inputDeviceName.isNotEmpty(),
            setup.outputDeviceName.isNotEmpty());
        result = forceReopenAudioDevice(deviceManager, setup);
    }
    return result;
}

} // namespace audio_device_recovery_detail

// SEH crash guard for VST processBlock (Windows only)
// MSVC forbids __try/__except in functions that have C++ objects with destructors
// on the stack (e.g. ScopedNoDenormals, AudioBuffer references). Extracting the
// __try block into a plain-C-style helper avoids this compiler restriction.
// try/catch(...) does NOT catch Windows SEH exceptions (access violations), so
// this is the only way to guard against crashing plugins on Windows.
#if defined(_WIN32)
static bool processBlockSEH(VSTChain& chain, juce::AudioBuffer<float>& buffer, int numSamples)
{
    __try {
        chain.processBlock(buffer, numSamples);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#endif

AudioEngine::AudioEngine()
{
    setSafetyHeadroomdB(-0.3f);
}

AudioEngine::~AudioEngine()
{
    // Device callbacks can queue message-thread work even before initialize()
    // marks the engine as running (tests and partial-start failure paths do
    // this). Invalidate every queued callback before shutdown's early return.
    alive_->store(false, std::memory_order_release);
    shutdown();
}

bool AudioEngine::initialize()
{
    // Startup strategy: initialize with a safe default driver, then restore saved type.
    //
    // We always open with the system default driver (WASAPI on Windows) first,
    // then switch to the saved driver type (e.g., ASIO) via importFromJSON.
    //
    // WHY NOT open ASIO directly
    //   - ASIO driver may not be installed, or the saved device may be unplugged.
    //     Opening ASIO directly would fail, leaving the app with no audio at all.
    //   - WASAPI is always available on Windows and provides a safe fallback.
    //   - The WASAPI-to-ASIO transition takes ~100ms and is invisible to the user
    //     (happens before the window is shown).
    //
    // ASIO SR/BS policy:
    //   ASIO devices own SR/BS globally (shared across all apps on the device).
    //   Forcing a saved BS on ASIO would restart the driver, disrupting other
    //   audio sources (DAWs, media players, etc.).
    // importFromJSON calls syncDesiredFromDevice() for ASIO instead of
    //     setSampleRate/setBufferSize. It accepts actual BS, but preserves an
    //     explicit requested SR if ASIO reports a mismatched value (e.g. 44.1k).
    // Non-ASIO drivers (WASAPI/CoreAudio/ALSA) use per-app SR/BS, so saved
    //     values are safely forced via setSampleRate/setBufferSize.
    //
    // See also: importFromJSON in PresetManager.cpp (ASIO vs non-ASIO branch)
    //           syncDesiredFromDevice() below (syncs desired from actual device)
    //           audioDeviceAboutToStart callAsync (ASIO always syncs desired)
    //
    auto result = audio_device_recovery_detail::initialiseWithDefaultDeviceFallbacks(
        deviceManager_, "Default device init");

    if (result.isNotEmpty()) {
        Log::error("AUDIO", "Device manager init failed: " + result);
        return false;
    }

    // Sync member atomics from the device that initialiseWithDefaultDevices started.
    // Avoid calling setAudioDeviceSetup here it would restart the device (e.g. ASIO
    // resets buffer size to its default on restart). importFromJSON will apply the
    // correct SR/BS/channel routing via setBufferSize/setInputDevice/etc.
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        auto initialSR = device->getCurrentSampleRate();
        auto initialBS = device->getCurrentBufferSizeSamples();
        if (initialSR > 0.0 && initialBS > 0) {
            currentSampleRate_ = initialSR;
            currentBufferSize_ = initialBS;
        } else {
            Log::warn("AUDIO", "Default device reported invalid SR=" + juce::String(initialSR)
                + " BS=" + juce::String(initialBS) + " during initialize");
        }

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
                Log::warn("AUDIO", "Device setup retry with driver default channels after: " + setupResult);
                useDefaultConfiguredChannels(setup, true, !outputNone_.load(std::memory_order_relaxed));
                setupResult = deviceManager_.setAudioDeviceSetup(setup, true);
            }
            if (setupResult.isNotEmpty()) {
                Log::error("AUDIO", "Device setup failed (SR=" + juce::String(static_cast<int>(setup.sampleRate))
                    + " BS=" + juce::String(setup.bufferSize) + " in='" + setup.inputDeviceName
                    + "' out='" + setup.outputDeviceName + "'): " + setupResult);
            }
            // Re-fetch device pointer (setAudioDeviceSetup may have replaced it)
            if (auto* freshDev = deviceManager_.getCurrentAudioDevice()) {
                auto refreshedSR = freshDev->getCurrentSampleRate();
                auto refreshedBS = freshDev->getCurrentBufferSizeSamples();
                if (refreshedSR > 0.0 && refreshedBS > 0) {
                    currentSampleRate_ = refreshedSR;
                    currentBufferSize_ = refreshedBS;
                } else {
                    Log::warn("AUDIO", "Device setup reported invalid SR=" + juce::String(refreshedSR)
                        + " BS=" + juce::String(refreshedBS) + " during initialize");
                }
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

    // Startup guard: keep output muted until settings restore completes.
    if (!outputNone_.load(std::memory_order_relaxed))
        outputMuted_.store(true, std::memory_order_seq_cst);

    // Register as the audio callback
    deviceManager_.addAudioCallback(this);

    // Listen for device list changes (plug/unplug detection)
    deviceManager_.addChangeListener(this);

    endpointChangeWatcher_.setCallback([this, alive = alive_](const juce::String& deviceName,
                                                              const juce::String& reason) {
        if (!alive->load(std::memory_order_acquire))
            return;

        juce::MessageManager::callAsync([this, alive, deviceName, reason] {
            if (!alive->load(std::memory_order_acquire))
                return;
            handleInputEndpointChanged(deviceName, reason);
        });
    });
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);
        updateInputEndpointWatcherTarget(setup.inputDeviceName);
    }
    running_ = true;
    endpointChangeWatcher_.start();

    return true;
}

void AudioEngine::shutdown()
{
    if (!running_) return;

    alive_->store(false);
    endpointChangeWatcher_.stop();
    running_ = false;
    deviceManager_.removeChangeListener(this);
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
    }
    // audioDeviceStopped() already called sharedMemWriter_.shutdown() via
    // closeAudioDevice() above, but guard against the path where the device
    // was never started (e.g. init failure) only shut down if still connected.
    if (sharedMemWriter_.isConnected())
        sharedMemWriter_.shutdown();
    ipcEnabled_.store(false, std::memory_order_relaxed);
    monitorConfigurationGeneration_.fetch_add(1, std::memory_order_acq_rel);
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
        auto currentSR = currentSampleRate_.load();
        if (currentSR <= 0.0) {
            Log::error("IPC", "Output failed to initialize: invalid sample rate "
                + juce::String(currentSR));
            return;
        }
        uint32_t sr = static_cast<uint32_t>(currentSR);
        if (sharedMemWriter_.initialize(sr, 2, directpipe::DEFAULT_BUFFER_FRAMES)) {
            ipcEnabled_.store(true, std::memory_order_release);
            Log::info("IPC", "Output enabled (SR=" + juce::String(sr) + ")");
        } else {
            Log::error("IPC", "Output failed to initialize (SR=" + juce::String(sr) + ")");
        }
    } else {
        ipcEnabled_.store(false, std::memory_order_release);
        // Clear the "restore on device restart" flag so that if the user
        // toggles IPC OFF during a device stop/start cycle (e.g., buffer size
        // change), audioDeviceAboutToStart won't re-enable it.
        ipcWasEnabled_ = false;
        sharedMemWriter_.shutdown();
        Log::info("IPC", "Output disabled");
    }
}

ActionResult AudioEngine::setInputDevice(const juce::String& deviceName)
{
    if (getCurrentDeviceType().containsIgnoreCase("ASIO")) {
        Log::warn("AUDIO", "ASIO input selection opens duplex device: " + deviceName);
        return setAsioDevice(deviceName);
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    const auto previousSetup = setup;
    const auto previousRecoveryState = captureDeviceRecoveryState();

    // Skip restart if device is already set (avoids ASIO re-init that resets BS)
    if (setup.inputDeviceName == deviceName && !setup.inputChannels.isZero()
        && isCurrentAudioDeviceReady()) {
        {
            const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
            desiredInputDevice_ = deviceName;
        }
        updateInputEndpointWatcherTarget(deviceName);
        clearLossAfterManualInputSelection();
        return ActionResult::ok();
    }

    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        type->scanForDevices();
        if (!type->getDeviceNames(true).contains(deviceName)) {
            const auto msg = "Input device not available: " + deviceName;
            Log::error("AUDIO", msg);
            return ActionResult::fail(msg);
        }
    }

    setup.inputDeviceName = deviceName;

    // Ensure input channels are active (JUCE may clear them after output device change)
    if (setup.inputChannels.isZero()) {
        setup.useDefaultInputChannels = false;
        // Physical routing stays a stereo pair in both processing modes.
        // Mono is produced from that pair at the start of the audio callback.
        setup.inputChannels.setRange(0, 2, true);
    }

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
        if (result.isNotEmpty()) {
            Log::warn("AUDIO", "Input setup retry with driver default channels after: " + result);
            useDefaultConfiguredChannels(setup, true, false);
            result = deviceManager_.setAudioDeviceSetup(setup, true);
        }
    }
    if (result.isEmpty()) {
        juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
        deviceManager_.getAudioDeviceSetup(appliedSetup);
        auto* appliedDevice = deviceManager_.getCurrentAudioDevice();
        if (!appliedDevice
            || appliedDevice->getCurrentSampleRate() <= 0.0
            || appliedDevice->getCurrentBufferSizeSamples() <= 0
            || !hasUsableActiveChannels(appliedSetup, appliedDevice)) {
            result = "device opened without usable active channels";
        }
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set input device '" + deviceName + "': " + result;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup,
            outputNone_.load(std::memory_order_relaxed),
            previousRecoveryState,
            msg,
            "input-device change");
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    { const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_); desiredInputDevice_ = deviceName; }
    updateInputEndpointWatcherTarget(deviceName);
    clearLossAfterManualInputSelection();
    Log::info("AUDIO", "Input device set: " + deviceName);
    Log::audit("AUDIO", "Input device change: '" + setup.inputDeviceName + "' SR=" + juce::String(setup.sampleRate) + " BS=" + juce::String(setup.bufferSize));
    return ActionResult::ok();
}

ActionResult AudioEngine::setOutputDevice(const juce::String& deviceName)
{
    const auto currentType = getCurrentDeviceType();
    if (currentType.containsIgnoreCase("ASIO") && deviceName != "None") {
        Log::warn("AUDIO", "ASIO output selection opens duplex device: " + deviceName);
        return setAsioDevice(deviceName);
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    const auto previousSetup = setup;
    const auto previousRecoveryState = captureDeviceRecoveryState();

    // Skip restart if device is already set (avoids ASIO re-init that resets BS)
    if (setup.outputDeviceName == deviceName && !setup.outputChannels.isZero()
        && isCurrentAudioDeviceReady()) {
        {
            const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
            desiredOutputDevice_ = deviceName;
        }
        clearLossAfterManualOutputSelection();
        return ActionResult::ok();
    }

    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        type->scanForDevices();
        if (!type->getDeviceNames(false).contains(deviceName)) {
            const auto msg = "Output device not available: " + deviceName;
            Log::error("AUDIO", msg);
            return ActionResult::fail(msg);
        }
    }

    setup.outputDeviceName = deviceName;

    const bool synthesizedInputPair =
        audio_device_recovery_detail::prepareOutputDeviceChangeChannels(setup);

    const auto suspendedMonitor = suspendMonitorBeforeExclusiveOpen(
        currentType, deviceName, false);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
        if (result.isNotEmpty()) {
            Log::warn("AUDIO", "Output setup retry with driver default channels after: " + result);
            useDefaultConfiguredChannels(setup, synthesizedInputPair, true);
            result = deviceManager_.setAudioDeviceSetup(setup, true);
        }
    }
    if (result.isEmpty()) {
        juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
        deviceManager_.getAudioDeviceSetup(appliedSetup);
        auto* appliedDevice = deviceManager_.getCurrentAudioDevice();
        if (!appliedDevice
            || appliedDevice->getCurrentSampleRate() <= 0.0
            || appliedDevice->getCurrentBufferSizeSamples() <= 0
            || !hasUsableActiveChannels(appliedSetup, appliedDevice, false)) {
            result = "device opened without usable active channels";
        }
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set output device '" + deviceName + "': " + result;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup,
            outputNone_.load(std::memory_order_relaxed),
            previousRecoveryState,
            msg,
            "output-device change");
        if (!restoreSuspendedMonitor(
                suspendedMonitor, currentSampleRate_.load(std::memory_order_relaxed),
                "failed main-output change"))
            msg += "; monitor restore failed";
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    { const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_); desiredOutputDevice_ = deviceName; }
    clearLossAfterManualOutputSelection();
    Log::info("AUDIO", "Output device set: " + deviceName);
    Log::audit("AUDIO", "Output device change: '" + setup.outputDeviceName + "' SR=" + juce::String(setup.sampleRate) + " BS=" + juce::String(setup.bufferSize));
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    resolveSuspendedMonitorAfterMainOpen(
        suspendedMonitor, currentType, appliedSetup.outputDeviceName);
    return ActionResult::ok();
}

void AudioEngine::setOutputNone(bool none)
{
    outputNone_.store(none, std::memory_order_seq_cst);
    outputMuted_.store(none, std::memory_order_seq_cst);

    if (!none)
        return;

    // "None" is an explicit decision not to use the main output. Resolve only
    // the output side of a pending loss so a missing speaker cannot keep the
    // input device in the generic reconnect loop.
    outputAutoMuted_.store(false, std::memory_order_seq_cst);
    bool inputStillPending = inputDeviceLost_.load(std::memory_order_acquire);
    deviceLost_.store(inputStillPending, std::memory_order_release);
    // Input-loss publishers set the directional flag before the aggregate.
    // Recheck after a false aggregate write so a concurrent publication cannot
    // be hidden by selecting output None.
    if (!inputStillPending && inputDeviceLost_.load(std::memory_order_acquire)) {
        inputStillPending = true;
        deviceLost_.store(true, std::memory_order_release);
    }
    if (!inputStillPending)
        startupRestorePending_ = false;
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
}

ActionResult AudioEngine::setAsioDevice(const juce::String& deviceName)
{
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        if (type->getTypeName().containsIgnoreCase("ASIO")) {
            type->scanForDevices();
            const auto inputs = type->getDeviceNames(true);
            const auto outputs = type->getDeviceNames(false);
            if (!inputs.contains(deviceName) && !outputs.contains(deviceName)) {
                auto msg = "ASIO device not available: " + deviceName;
                Log::error("AUDIO", msg);
                return ActionResult::fail(msg);
            }
        }
    }

    juce::AudioDeviceManager::AudioDeviceSetup previousSetup;
    deviceManager_.getAudioDeviceSetup(previousSetup);
    const auto previousRecoveryState = captureDeviceRecoveryState();
    auto setup = previousSetup;
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = deviceName;
    if (desiredSampleRate_ > 0.0)
        setup.sampleRate = desiredSampleRate_;
    if (desiredBufferSize_ > 0)
        setup.bufferSize = desiredBufferSize_;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    if (setup.inputChannels.isZero())
        setup.inputChannels.setRange(0, 2, true);
    if (setup.outputChannels.isZero())
        setup.outputChannels.setRange(0, 2, true);

    const auto currentType = getCurrentDeviceType();
    const auto suspendedMonitor = suspendMonitorBeforeExclusiveOpen(
        currentType, deviceName, true);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
        if (result.isNotEmpty()) {
            Log::warn("AUDIO", "ASIO setup retry with driver default channels after: " + result);
            useDefaultConfiguredChannels(setup, true, true);
            result = deviceManager_.setAudioDeviceSetup(setup, true);
        }
    }
    if (result.isEmpty()) {
        juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
        deviceManager_.getAudioDeviceSetup(appliedSetup);
        auto* appliedDevice = deviceManager_.getCurrentAudioDevice();
        if (!appliedDevice
            || appliedDevice->getCurrentSampleRate() <= 0.0
            || appliedDevice->getCurrentBufferSizeSamples() <= 0
            || !hasUsableActiveChannels(appliedSetup, appliedDevice, false)) {
            result = "device opened without a usable duplex channel set";
        }
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to set ASIO device '" + deviceName + "': " + result;
        juce::String rollbackResult;
        {
            AtomicGuard intentionalGuard(intentionalChange_);
            endpointEventSuppressedUntilMs_ =
                juce::Time::getMillisecondCounterHiRes()
                + audio_device_recovery_detail::kEndpointEventReopenSuppressionMs;
            rollbackResult = audio_device_recovery_detail::forceReopenAudioDevice(
                deviceManager_, previousSetup);
        }
        bool rollbackReady = false;
        if (rollbackResult.isEmpty()) {
            juce::AudioDeviceManager::AudioDeviceSetup restoredSetup;
            deviceManager_.getAudioDeviceSetup(restoredSetup);
            auto* restoredDevice = deviceManager_.getCurrentAudioDevice();
            rollbackReady = restoredDevice
                && restoredDevice->getCurrentSampleRate() > 0.0
                && restoredDevice->getCurrentBufferSizeSamples() > 0
                && hasUsableActiveChannels(restoredSetup, restoredDevice, false);
        }
        if (rollbackReady) {
            restoreDeviceRecoveryState(previousRecoveryState);
            Log::info("AUDIO", "Restored previous ASIO duplex setup after failed device change");
        } else {
            if (rollbackResult.isEmpty())
                rollbackResult = "previous ASIO setup reopened without usable active channels";
            msg += "; previous setup restore failed: " + rollbackResult;
            enterDeviceRecoveryAfterRestoreFailure();
        }
        if (!restoreSuspendedMonitor(
                suspendedMonitor, currentSampleRate_.load(std::memory_order_relaxed),
                "failed ASIO device change"))
            msg += "; monitor restore failed";
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    if (auto* appliedDevice = deviceManager_.getCurrentAudioDevice()) {
        syncRuntimeRateFromActual(
            appliedDevice->getCurrentSampleRate(),
            appliedDevice->getCurrentBufferSizeSamples(),
            "ASIO device change actual rate",
            true);
    }
    { const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_); desiredInputDevice_ = deviceName; desiredOutputDevice_ = deviceName; }
    updateInputEndpointWatcherTarget({});
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    releaseAutomaticOutputMute();
    deviceLost_.store(false, std::memory_order_relaxed);
    cancelPendingExternalRestartReopen();
    startupRestorePending_ = false;
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    lastAsioDevice_ = deviceName;
    Log::info("AUDIO", "ASIO device set: " + deviceName);
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    resolveSuspendedMonitorAfterMainOpen(
        suspendedMonitor, currentType, appliedSetup.outputDeviceName);
    return ActionResult::ok();
}

AudioEngine::SuspendedMonitorState AudioEngine::suspendMonitorBeforeExclusiveOpen(
    const juce::String& targetDeviceType,
    const juce::String& targetOutputDevice,
    bool suspendForAnyExclusiveTypeSwitch)
{
    SuspendedMonitorState state;
    if (monitorOutput_.getStatus() == VirtualCableStatus::NotConfigured)
        return state;

    const auto monitorDevice = monitorOutput_.getDeviceName();
    if (!audio_device_recovery_detail::shouldSuspendMonitorBeforeExclusiveOpen(
            targetDeviceType, targetOutputDevice, monitorDevice,
            suspendForAnyExclusiveTypeSwitch))
        return state;

    state.suspended = true;
    state.deviceName = monitorDevice;
    state.sampleRate = monitorOutput_.getActualSampleRate();
    if (state.sampleRate <= 0.0)
        state.sampleRate = currentSampleRate_.load(std::memory_order_relaxed);
    if (state.sampleRate <= 0.0)
        state.sampleRate = desiredSampleRate_;
    state.bufferSize = monitorOutput_.getPreferredBufferSize();

    monitorConfigurationGeneration_.fetch_add(1, std::memory_order_acq_rel);
    monitorOutput_.shutdown();
    Log::info("MONITOR", "Temporarily suspended before exclusive main-device open: '"
        + state.deviceName + "'");
    return state;
}

bool AudioEngine::restoreSuspendedMonitor(
    const SuspendedMonitorState& state,
    double sampleRate,
    const juce::String& reason)
{
    if (!state.suspended)
        return true;

    auto restoreSampleRate = sampleRate > 0.0 ? sampleRate : state.sampleRate;
    if (restoreSampleRate <= 0.0)
        restoreSampleRate = 48000.0;
    const int restoreBufferSize = state.bufferSize > 0 ? state.bufferSize : 128;
    monitorConfigurationGeneration_.fetch_add(1, std::memory_order_acq_rel);
    if (monitorOutput_.initialize(
            state.deviceName, restoreSampleRate, restoreBufferSize)) {
        Log::info("MONITOR", "Restored after " + reason + ": '" + state.deviceName + "'");
        return true;
    }

    const auto message = "Monitor restore failed after " + reason + ": "
        + state.deviceName;
    Log::error("MONITOR", message);
    pushNotification(message, NotificationLevel::Warning);
    return false;
}

void AudioEngine::resolveSuspendedMonitorAfterMainOpen(
    const SuspendedMonitorState& state,
    const juce::String& actualDeviceType,
    const juce::String& actualOutputDevice)
{
    if (!state.suspended)
        return;

    if (audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
            actualDeviceType, actualOutputDevice, state.deviceName)) {
        const juce::String message =
            "Monitor disabled: it matches the exclusive main output";
        Log::warn("MONITOR", message + " ('" + state.deviceName + "')");
        pushNotification(message, NotificationLevel::Warning);
        return;
    }

    restoreSuspendedMonitor(
        state, currentSampleRate_.load(std::memory_order_relaxed),
        "exclusive main-device switch");
}

ActionResult AudioEngine::setMonitorDevice(const juce::String& deviceName)
{
    juce::AudioDeviceManager::AudioDeviceSetup mainSetup;
    deviceManager_.getAudioDeviceSetup(mainSetup);
    if (audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
            getCurrentDeviceType(), mainSetup.outputDeviceName, deviceName)) {
        const auto message = "Monitor device cannot use the exclusive main output: "
            + deviceName;
        Log::warn("MONITOR", message);
        return ActionResult::fail(message);
    }

    monitorConfigurationGeneration_.fetch_add(1, std::memory_order_acq_rel);
    if (monitorOutput_.setDevice(deviceName))
        return ActionResult::ok();
    return ActionResult::fail("Failed to set monitor device: " + deviceName);
}

ActionResult AudioEngine::setMonitorBufferSize(int bufferSize)
{
    monitorConfigurationGeneration_.fetch_add(1, std::memory_order_acq_rel);
    if (monitorOutput_.setBufferSize(bufferSize))
        return ActionResult::ok();
    return ActionResult::fail("Failed to set monitor buffer size: " + juce::String(bufferSize));
}

void AudioEngine::setSafetyHeadroomdB(float dB)
{
    const float clamped = juce::jlimit(-6.0f, 0.0f, dB);
    safetyHeadroomdB_.store(clamped, std::memory_order_relaxed);
    safetyHeadroomGain_.store(juce::Decibels::decibelsToGain(clamped), std::memory_order_relaxed);
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

void AudioEngine::syncDesiredFromDevice()
{
    // Sync desiredSR/BS FROM the current audio device (not TO).
    //
    // Primary use case: ASIO devices.
    // ASIO SR/BS is global; changing it restarts the driver and disrupts ALL apps
    // sharing that device (DAWs, media players, etc.). Instead of forcing our saved
    // values (which would cause an unnecessary driver restart), we accept the
    // actual ASIO buffer size. If settings explicitly requested a sample rate,
    // keep that requested SR when the driver reports a mismatched one.
    //
    // Called from:
    //   - importFromJSON (ASIO path): on startup, accept device's actual BS and
    //     preserve explicit requested SR across accidental driver mismatch
    //   - Could also be called after ASIO control panel changes SR/BS externally
    //
    // NOT used for WASAPI/CoreAudio/ALSA those use per-app SR/BS, so forcing
    // saved values is safe and expected (via setSampleRate/setBufferSize).
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (!device) return;

    double sr = device->getCurrentSampleRate();
    int bs = device->getCurrentBufferSizeSamples();

    if (sr <= 0.0 || bs <= 0) {
        Log::warn("AUDIO", "Skipped desired sync from invalid device: SR="
            + juce::String(sr) + " BS=" + juce::String(bs));
        return;
    }

    syncRuntimeRateFromActual(sr, bs, "Desired sync from device", true);
}

bool AudioEngine::isCurrentAudioDeviceReady()
{
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (!device) return false;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    return device->getCurrentSampleRate() > 0.0
        && device->getCurrentBufferSizeSamples() > 0
        && hasUsableActiveChannels(setup, device);
}

bool AudioEngine::hasUsableActiveChannels(const juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                          juce::AudioIODevice* device) const
{
    return hasUsableActiveChannels(
        setup, device, outputNone_.load(std::memory_order_relaxed));
}

bool AudioEngine::hasUsableActiveChannels(const juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                          juce::AudioIODevice* device,
                                          bool outputNone) const
{
    if (!device)
        return false;

    const bool inputConfigured = setup.inputDeviceName.isNotEmpty()
        || !device->getInputChannelNames().isEmpty();
    const bool outputConfigured = !outputNone
        && (setup.outputDeviceName.isNotEmpty() || !device->getOutputChannelNames().isEmpty());

    const bool inputReady = channelSelectionReady(inputConfigured,
                                                  setup.useDefaultInputChannels,
                                                  setup.inputChannels,
                                                  device->getActiveInputChannels(),
                                                  device->getInputChannelNames().size());
    const bool outputReady = channelSelectionReady(outputConfigured,
                                                   setup.useDefaultOutputChannels,
                                                   setup.outputChannels,
                                                   device->getActiveOutputChannels(),
                                                   device->getOutputChannelNames().size());

    return inputReady && outputReady;
}

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
bool AudioEngine::restoredDeviceTargetsSatisfiedForTest(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup) const
{
    return restoredDeviceTargetsSatisfied(setup);
}

bool AudioEngine::clearDeviceLossAfterReadyForTest(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    return clearDeviceLossAfterReady(setup);
}

bool AudioEngine::hasUsableActiveChannelsForTest(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup,
    juce::AudioIODevice* device) const
{
    return hasUsableActiveChannels(setup, device);
}

void AudioEngine::markInputDeviceLostForTest(const juce::String& desiredInputDevice)
{
    {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        desiredInputDevice_ = desiredInputDevice;
    }

    startupRestorePending_ = false;
    publishInputDeviceLoss();
    outputAutoMuted_.store(false, std::memory_order_seq_cst);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
}

void AudioEngine::markOutputDeviceLostForTest(const juce::String& desiredOutputDevice)
{
    {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        desiredOutputDevice_ = desiredOutputDevice;
    }

    startupRestorePending_ = false;
    deviceLost_.store(true, std::memory_order_relaxed);
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    autoMuteOutputForDeviceLoss();
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
}

void AudioEngine::setDesiredInputDeviceForTest(const juce::String& desiredInputDevice)
{
    const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
    desiredInputDevice_ = desiredInputDevice;
}

bool AudioEngine::markInputEndpointRestartPendingForTest(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup,
    const juce::String& reason)
{
    return markInputEndpointRestartPending(setup, reason);
}
#endif

bool AudioEngine::restoredDeviceTargetsSatisfied(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup) const
{
    if (!startupRestorePending_)
        return true;

    juce::String desiredType;
    juce::String desiredInput;
    juce::String desiredOutput;
    {
        const juce::SpinLock::ScopedLockType lock(desiredDeviceLock_);
        desiredType = desiredDeviceType_;
        desiredInput = desiredInputDevice_;
        desiredOutput = desiredOutputDevice_;
    }

    const bool inputMatches = desiredInput.isEmpty() || setup.inputDeviceName == desiredInput;
    const bool outputMatches = outputNone_.load(std::memory_order_relaxed)
        || desiredOutput.isEmpty()
        || setup.outputDeviceName == desiredOutput;
    const bool typeMatches = desiredType.isEmpty() || getCurrentDeviceType() == desiredType;

    return inputMatches && outputMatches && typeMatches;
}

bool AudioEngine::clearDeviceLossAfterReady(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    if (!restoredDeviceTargetsSatisfied(setup)) {
        deviceLost_.store(true, std::memory_order_relaxed);
        Log::warn("AUDIO", "Audio device is open, but startup restore target is still pending: current in='"
            + setup.inputDeviceName + "' out='" + setup.outputDeviceName + "'");
        return false;
    }

    deviceLost_.store(false, std::memory_order_relaxed);
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    startupRestorePending_ = false;
    releaseAutomaticOutputMute();

    return true;
}

void AudioEngine::enterDeviceRecoveryAfterRestoreFailure()
{
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        deviceManager_.closeAudioDevice();
    }
    cancelPendingExternalRestartReopen();
    publishInputDeviceLoss();
    autoMuteOutputForDeviceLoss();
    inputLevel_.store(0.0f, std::memory_order_relaxed);
    outputLevel_.store(0.0f, std::memory_order_relaxed);
    reconnectCooldown_ = kReconnectCooldownTicks;
    reconnectMissCount_ = 0;
}

AudioEngine::DeviceRecoverySnapshot
AudioEngine::captureDeviceRecoveryState() const noexcept
{
    DeviceRecoverySnapshot snapshot;
    snapshot.deviceLost =
        deviceLost_.load(std::memory_order_acquire);
    snapshot.inputDeviceLost =
        inputDeviceLost_.load(std::memory_order_acquire);
    snapshot.outputAutoMuted =
        outputAutoMuted_.load(std::memory_order_acquire);
    snapshot.startupRestorePending = startupRestorePending_;
    snapshot.reconnectCooldown = reconnectCooldown_;
    snapshot.reconnectMissCount = reconnectMissCount_;
    return snapshot;
}

void AudioEngine::restoreDeviceRecoveryState(
    const DeviceRecoverySnapshot& snapshot) noexcept
{
    deviceLost_.store(snapshot.deviceLost, std::memory_order_release);
    inputDeviceLost_.store(
        snapshot.inputDeviceLost, std::memory_order_release);
    outputAutoMuted_.store(
        snapshot.outputAutoMuted, std::memory_order_release);
    startupRestorePending_ = snapshot.startupRestorePending;
    reconnectCooldown_ = snapshot.reconnectCooldown;
    reconnectMissCount_ = snapshot.reconnectMissCount;
}

bool AudioEngine::restorePreviousSetupAfterFailedChange(
    const juce::AudioDeviceManager::AudioDeviceSetup& previousSetup,
    bool previousOutputNone,
    const DeviceRecoverySnapshot& previousRecoveryState,
    juce::String& failureMessage,
    const juce::String& context)
{
    juce::String rollbackResult;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        endpointEventSuppressedUntilMs_ =
            juce::Time::getMillisecondCounterHiRes()
            + audio_device_recovery_detail::kEndpointEventReopenSuppressionMs;
        rollbackResult = audio_device_recovery_detail::forceReopenAudioDevice(
            deviceManager_, previousSetup);
    }

    juce::AudioDeviceManager::AudioDeviceSetup restoredSetup;
    deviceManager_.getAudioDeviceSetup(restoredSetup);
    auto* restoredDevice = deviceManager_.getCurrentAudioDevice();
    const bool rollbackReady = rollbackResult.isEmpty()
        && restoredDevice
        && restoredDevice->getCurrentSampleRate() > 0.0
        && restoredDevice->getCurrentBufferSizeSamples() > 0
        && hasUsableActiveChannels(
            restoredSetup, restoredDevice, previousOutputNone);

    if (!rollbackReady) {
        if (rollbackResult.isEmpty())
            rollbackResult = "previous setup reopened without usable active channels";
        failureMessage += "; previous setup restore failed: " + rollbackResult;
        Log::error("AUDIO", failureMessage);
        enterDeviceRecoveryAfterRestoreFailure();
        return false;
    }

    currentSampleRate_ = restoredDevice->getCurrentSampleRate();
    currentBufferSize_ = restoredDevice->getCurrentBufferSizeSamples();
    restoreDeviceRecoveryState(previousRecoveryState);
    Log::info("AUDIO", "Restored previous audio setup after failed " + context
        + ": in='" + restoredSetup.inputDeviceName
        + "' out='" + restoredSetup.outputDeviceName
        + "' SR=" + juce::String(currentSampleRate_.load())
        + " BS=" + juce::String(currentBufferSize_.load()));
    return true;
}

void AudioEngine::autoMuteOutputForDeviceLoss() noexcept
{
    if (outputNone_.load(std::memory_order_relaxed))
        return;

    // Automatic safety state is an independent mute reason. Keeping it
    // separate means recovery can clear this reason without overwriting a
    // concurrent user/settings mute.
    outputAutoMuted_.store(true, std::memory_order_seq_cst);
}

void AudioEngine::releaseAutomaticOutputMute() noexcept
{
    outputAutoMuted_.store(false, std::memory_order_seq_cst);
}

void AudioEngine::publishInputDeviceLoss() noexcept
{
    // Directional-first publication pairs with setOutputNone/manual-output
    // rechecks. A concurrent clearer either observes input loss or is followed
    // by this aggregate true store.
    inputDeviceLost_.store(true, std::memory_order_release);
    deviceLost_.store(true, std::memory_order_release);
}

bool AudioEngine::markActiveChannelLossIfNeeded(const juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                                juce::AudioIODevice* device,
                                                const juce::String& reason)
{
    if (hasUsableActiveChannels(setup, device))
        return false;

    Log::warn("AUDIO", reason + ": no usable active channels");
    logDeviceSetupSnapshot(reason.toRawUTF8(), device);

    const bool inputConfigured = setup.inputDeviceName.isNotEmpty()
        || (device && !device->getInputChannelNames().isEmpty());
    const bool outputConfigured = !outputNone_.load(std::memory_order_relaxed)
        && (setup.outputDeviceName.isNotEmpty()
            || (device && !device->getOutputChannelNames().isEmpty()));

    const bool inputReady = device && channelSelectionReady(inputConfigured,
                                                            setup.useDefaultInputChannels,
                                                            setup.inputChannels,
                                                            device->getActiveInputChannels(),
                                                            device->getInputChannelNames().size());
    const bool outputReady = device && channelSelectionReady(outputConfigured,
                                                             setup.useDefaultOutputChannels,
                                                             setup.outputChannels,
                                                             device->getActiveOutputChannels(),
                                                             device->getOutputChannelNames().size());

    if (inputConfigured && !inputReady)
        inputDeviceLost_.store(true, std::memory_order_relaxed);

    if (outputConfigured && !outputReady) {
        autoMuteOutputForDeviceLoss();
    }

    return true;
}

void AudioEngine::cancelPendingExternalRestartReopen() noexcept
{
    externalDeviceRestartPending_.store(false, std::memory_order_release);
    sameDeviceReopenPending_.store(false, std::memory_order_release);
    sameDeviceReopenGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

void AudioEngine::updateInputEndpointWatcherTarget(const juce::String& inputDeviceName)
{
    const auto desiredType = getDesiredDeviceType();
    if (PlatformAudio::isExclusiveDriverType(desiredType)) {
        endpointChangeWatcher_.setInputDeviceName({});
        return;
    }

    endpointChangeWatcher_.setInputDeviceName(inputDeviceName);
}

void AudioEngine::handleInputEndpointChanged(const juce::String& deviceName,
                                             const juce::String& reason)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (!running_ || intentionalChange_.load(std::memory_order_acquire))
        return;

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (audio_device_recovery_detail::endpointEventIsSuppressed(
            nowMs, endpointEventSuppressedUntilMs_)) {
        Log::audit("AUDIO", "Ignoring endpoint notification emitted during DirectPipe re-open: target='"
            + deviceName + "' reason='" + reason + "'");
        return;
    }

    const auto currentType = getCurrentDeviceType();
    if (PlatformAudio::isExclusiveDriverType(currentType))
        return;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    if (setup.inputDeviceName.isEmpty())
        return;

    if (deviceName.isNotEmpty() && !audioDeviceNamesExactlyMatch(setup.inputDeviceName, deviceName)) {
        Log::audit("AUDIO", "Ignoring endpoint notification for non-current input: event='"
            + deviceName + "' current='" + setup.inputDeviceName + "' reason='" + reason + "'");
        return;
    }

    if (!markInputEndpointRestartPending(setup, reason))
        return;

    scheduleSameDeviceReopenAfterExternalRestart(setup);
}

bool AudioEngine::markInputEndpointRestartPending(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup,
    const juce::String& reason)
{
    if (setup.inputDeviceName.isEmpty())
        return false;

    if (sameDeviceReopenPending_.load(std::memory_order_acquire)) {
        Log::audit("AUDIO", "Coalescing endpoint restart notification while reopen is pending: reason='"
            + reason + "'");
        return false;
    }

    juce::String desiredInput;
    {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        desiredInput = desiredInputDevice_;
    }

    if (desiredInput.isNotEmpty()
        && !audioDeviceNamesExactlyMatch(setup.inputDeviceName, desiredInput)) {
        Log::audit("AUDIO", "Ignoring endpoint restart for non-desired input: current='"
            + setup.inputDeviceName + "' desired='" + desiredInput + "' reason='" + reason + "'");
        return false;
    }

    if (desiredInput.isEmpty()) {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        desiredInputDevice_ = setup.inputDeviceName;
    }

    Log::warn("AUDIO", "Input endpoint changed; forcing same-device re-open: in='"
        + setup.inputDeviceName + "' reason='" + reason + "'");
    Log::audit("AUDIO", "Endpoint restart pending: in='" + setup.inputDeviceName
        + "' out='" + setup.outputDeviceName + "' reason='" + reason + "'");

    publishInputDeviceLoss();
    externalDeviceRestartPending_.store(true, std::memory_order_release);
    autoMuteOutputForDeviceLoss();
    inputLevel_.store(0.0f, std::memory_order_relaxed);
    outputLevel_.store(0.0f, std::memory_order_relaxed);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    return true;
}

void AudioEngine::clearLossAfterManualInputSelection()
{
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    cancelPendingExternalRestartReopen();

    const bool outputStillPending = outputAutoMuted_.load(std::memory_order_relaxed);
    deviceLost_.store(outputStillPending, std::memory_order_relaxed);
    if (!outputStillPending)
        startupRestorePending_ = false;

    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
}

void AudioEngine::clearLossAfterManualOutputSelection()
{
    releaseAutomaticOutputMute();
    cancelPendingExternalRestartReopen();

    bool inputStillPending = inputDeviceLost_.load(std::memory_order_acquire);
    deviceLost_.store(inputStillPending, std::memory_order_release);
    if (!inputStillPending && inputDeviceLost_.load(std::memory_order_acquire)) {
        inputStillPending = true;
        deviceLost_.store(true, std::memory_order_release);
    }
    if (!inputStillPending)
        startupRestorePending_ = false;

    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
}

bool AudioEngine::recoverActiveChannelsWithDriverDefaults(const juce::String& reason)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    const bool alreadyPending = activeChannelRecoveryPending_.exchange(true, std::memory_order_acq_rel);
    auto finish = [this, alreadyPending](bool ok) {
        if (!alreadyPending)
            activeChannelRecoveryPending_.store(false, std::memory_order_release);
        reconnectCooldown_ =
            audio_device_recovery_detail::reconnectCooldownAfterRecovery(ok);
        return ok;
    };

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    Log::warn("AUDIO", "Retrying active-channel recovery with driver default channels: " + reason);
    useDefaultConfiguredChannels(setup, true, !outputNone_.load(std::memory_order_relaxed));

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }

    if (result.isNotEmpty()) {
        Log::error("AUDIO", "Active-channel recovery failed: " + result);
        deviceLost_.store(true, std::memory_order_relaxed);
        reconnectCooldown_ =
            audio_device_recovery_detail::reconnectCooldownAfterRecovery(false);
        return finish(false);
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);

    if (!device || device->getCurrentSampleRate() <= 0.0 || device->getCurrentBufferSizeSamples() <= 0) {
        Log::error("AUDIO", "Active-channel recovery reopened an invalid device");
        logDeviceSetupSnapshot("Active-channel recovery invalid device", device);
        deviceLost_.store(true, std::memory_order_relaxed);
        reconnectCooldown_ =
            audio_device_recovery_detail::reconnectCooldownAfterRecovery(false);
        return finish(false);
    }

    if (markActiveChannelLossIfNeeded(appliedSetup, device, "Active-channel recovery invalid setup")) {
        deviceLost_.store(true, std::memory_order_relaxed);
        reconnectCooldown_ =
            audio_device_recovery_detail::reconnectCooldownAfterRecovery(false);
        return finish(false);
    }

    syncRuntimeRateFromActual(device->getCurrentSampleRate(),
                              device->getCurrentBufferSizeSamples(),
                              "Active-channel recovery actual rate",
                              getCurrentDeviceType().containsIgnoreCase("ASIO"));
    clearDeviceLossAfterReady(appliedSetup);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;

    Log::info("AUDIO", "Active-channel recovery ready: " + device->getName());
    logDeviceSetupSnapshot("Active-channel recovery ready", device);
    return finish(true);
}

void AudioEngine::scheduleActiveChannelRecovery(const juce::String& reason)
{
    if (activeChannelRecoveryPending_.exchange(true, std::memory_order_acq_rel)) {
        Log::audit("AUDIO", "Active-channel recovery already pending: " + reason);
        return;
    }

    deviceLost_.store(true, std::memory_order_relaxed);
    auto aliveFlag = alive_;
    juce::MessageManager::callAsync([this, aliveFlag, reason] {
        if (!aliveFlag->load())
            return;

        const bool recovered =
            recoverActiveChannelsWithDriverDefaults(reason);
        activeChannelRecoveryPending_.store(false, std::memory_order_release);
        reconnectCooldown_ =
            audio_device_recovery_detail::reconnectCooldownAfterRecovery(recovered);
    });
}

void AudioEngine::logDeviceSetupSnapshot(const char* reason, juce::AudioIODevice* device)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    juce::BigInteger activeInput;
    juce::BigInteger activeOutput;
    juce::String deviceName = "(none)";
    juce::String typeName = getCurrentDeviceType();
    int inputChannelCount = 0;
    int outputChannelCount = 0;

    if (device) {
        deviceName = device->getName();
        typeName = device->getTypeName();
        activeInput = device->getActiveInputChannels();
        activeOutput = device->getActiveOutputChannels();
        inputChannelCount = device->getInputChannelNames().size();
        outputChannelCount = device->getOutputChannelNames().size();
    }

    Log::info("AUDIO", juce::String(reason)
        + ": type='" + typeName
        + "' device='" + deviceName
        + "' setupIn='" + setup.inputDeviceName
        + "' setupOut='" + setup.outputDeviceName
        + "' activeIn=[" + channelMaskToLogString(activeInput)
        + "] activeOut=[" + channelMaskToLogString(activeOutput)
        + "] availableIn=" + juce::String(inputChannelCount)
        + " availableOut=" + juce::String(outputChannelCount));
}

void AudioEngine::syncRuntimeRateFromActual(double sampleRate, int bufferSize,
                                            const char* reason, bool preserveExplicitSampleRate)
{
    if (sampleRate <= 0.0 || bufferSize <= 0) {
        Log::warn("AUDIO", juce::String(reason) + " skipped invalid SR="
            + juce::String(sampleRate) + " BS=" + juce::String(bufferSize));
        return;
    }

    const double requestedSampleRate = desiredSampleRate_;
    const bool keepRequestedSampleRate = preserveExplicitSampleRate
        && desiredSRBSSet_
        && sampleRatesDiffer(requestedSampleRate, sampleRate);

    currentSampleRate_ = sampleRate;
    currentBufferSize_ = bufferSize;

    if (keepRequestedSampleRate) {
        Log::warn("AUDIO", juce::String(reason) + ": actual SR="
            + juce::String(static_cast<int>(sampleRate))
            + " differs from requested SR="
            + juce::String(static_cast<int>(requestedSampleRate))
            + "; preserving requested SR for next restore");
    } else {
        desiredSampleRate_ = sampleRate;
    }

    desiredBufferSize_ = bufferSize;
    desiredSRBSSet_ = true;

    Log::info("AUDIO", juce::String(reason) + ": actual SR="
        + juce::String(sampleRate) + " BS=" + juce::String(bufferSize)
        + " desired SR=" + juce::String(desiredSampleRate_)
        + " BS=" + juce::String(desiredBufferSize_));
}

ActionResult AudioEngine::ensureAudioDeviceReady()
{
    if (isCurrentAudioDeviceReady())
        return ActionResult::ok();

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    const bool hadCurrentDevice = deviceManager_.getCurrentAudioDevice() != nullptr;

    {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        if (setup.inputDeviceName.isEmpty() && desiredInputDevice_.isNotEmpty())
            setup.inputDeviceName = desiredInputDevice_;
        if (setup.outputDeviceName.isEmpty() && desiredOutputDevice_.isNotEmpty())
            setup.outputDeviceName = desiredOutputDevice_;
    }

    if (!hadCurrentDevice && setup.inputDeviceName.isEmpty() && setup.outputDeviceName.isEmpty())
        return ActionResult::fail("No audio device available for restore refresh");

    if (setup.sampleRate <= 0.0)
        setup.sampleRate = desiredSampleRate_ > 0.0 ? desiredSampleRate_ : 48000.0;
    if (setup.bufferSize <= 0)
        setup.bufferSize = desiredBufferSize_ > 0 ? desiredBufferSize_ : 256;

    if (setup.inputChannels.isZero() && setup.inputDeviceName.isNotEmpty()) {
        setup.useDefaultInputChannels = false;
        setup.inputChannels.setRange(0, 2, true);
    }
    if (setup.outputChannels.isZero() && setup.outputDeviceName.isNotEmpty()) {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange(0, 2, true);
    }

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
        if (result.isNotEmpty()) {
            Log::warn("AUDIO", "Restore reopen retry with driver default channels after: " + result);
            useDefaultConfiguredChannels(setup, true, !outputNone_.load(std::memory_order_relaxed));
            result = deviceManager_.setAudioDeviceSetup(setup, true);
        }
    }
    if (result.isNotEmpty()) {
        auto msg = "Failed to reopen audio device after restore: " + result;
        Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    if (device && device->getCurrentSampleRate() > 0.0 && device->getCurrentBufferSizeSamples() > 0
        && !hasUsableActiveChannels(appliedSetup, device)) {
        markActiveChannelLossIfNeeded(appliedSetup, device, "Restore reopen active-channel invalid");
        if (recoverActiveChannelsWithDriverDefaults("Restore reopen active-channel recovery")) {
            device = deviceManager_.getCurrentAudioDevice();
            deviceManager_.getAudioDeviceSetup(appliedSetup);
        }
    }
    if (!device || device->getCurrentSampleRate() <= 0.0 || device->getCurrentBufferSizeSamples() <= 0
        || !hasUsableActiveChannels(appliedSetup, device)) {
        auto msg = "Audio device still invalid after restore reopen";
        Log::error("AUDIO", msg);
        logDeviceSetupSnapshot("Restore reopen invalid setup", device);
        return ActionResult::fail(msg);
    }

    syncRuntimeRateFromActual(device->getCurrentSampleRate(),
                              device->getCurrentBufferSizeSamples(),
                              "Restore reopen actual rate",
                              getCurrentDeviceType().containsIgnoreCase("ASIO"));
    clearDeviceLossAfterReady(appliedSetup);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;

    Log::info("AUDIO", "Audio device reopened after restore: " + device->getName()
        + " @ " + juce::String(currentSampleRate_.load()) + "Hz / "
        + juce::String(currentBufferSize_.load()) + " samples");
    logDeviceSetupSnapshot("Restore reopen ready", device);
    return ActionResult::ok();
}

void AudioEngine::rememberRestoredDeviceTargets(const juce::String& deviceType,
                                                const juce::String& inputDevice,
                                                const juce::String& outputDevice)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (deviceType.isNotEmpty())
        desiredDeviceType_ = deviceType;

    juce::String inputTarget = inputDevice;
    juce::String outputTarget = outputDevice;
    if (deviceType.containsIgnoreCase("ASIO")) {
        const auto asioDevice = selectAsioDuplexDevice(inputTarget, outputTarget);
        if (inputTarget.isNotEmpty() && outputTarget.isNotEmpty() && inputTarget != outputTarget) {
            Log::warn("AUDIO", "ASIO restore target mismatch; using duplex device '" + asioDevice
                + "' instead of input='" + inputTarget + "' output='" + outputTarget + "'");
        }
        if (asioDevice.isNotEmpty()) {
            inputTarget = asioDevice;
            outputTarget = outputNone_.load(std::memory_order_relaxed) ? juce::String{} : asioDevice;
        }
    }

    const bool hasInputTarget = inputTarget.isNotEmpty();
    const bool hasOutputTarget = outputTarget.isNotEmpty();

    if (!hasInputTarget && !hasOutputTarget)
        return;

    {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        if (hasInputTarget)
            desiredInputDevice_ = inputTarget;
        if (hasOutputTarget)
            desiredOutputDevice_ = outputTarget;
    }
    if (hasInputTarget)
        updateInputEndpointWatcherTarget(inputTarget);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    const bool currentReady = isCurrentAudioDeviceReady();
    const bool inputNeedsRestore = hasInputTarget
        && (!currentReady || setup.inputDeviceName != inputTarget);
    const bool outputNeedsRestore = hasOutputTarget
        && (!currentReady || setup.outputDeviceName != outputTarget);

    const auto currentType = getCurrentDeviceType();
    const bool typeNeedsRestore = deviceType.isNotEmpty()
        && currentType.isNotEmpty()
        && currentType != deviceType;

    if (!inputNeedsRestore && !outputNeedsRestore && !typeNeedsRestore) {
        startupRestorePending_ = false;
        return;
    }

    startupRestorePending_ = true;
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;

    if (inputNeedsRestore)
        publishInputDeviceLoss();
    else
        deviceLost_.store(true, std::memory_order_release);

    if (outputNeedsRestore && !outputNone_.load(std::memory_order_relaxed)) {
        autoMuteOutputForDeviceLoss();
    }

    Log::warn("AUDIO", "Startup restore waiting for saved device target: in='"
        + inputTarget + "' out='" + outputTarget + "' current in='"
        + setup.inputDeviceName + "' out='" + setup.outputDeviceName + "'");
}

ActionResult AudioEngine::applyAudioDeviceSetup(const juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                                const juce::String& context)
{
    juce::AudioDeviceManager::AudioDeviceSetup previousSetup;
    deviceManager_.getAudioDeviceSetup(previousSetup);
    const bool previousOutputNone =
        outputNone_.load(std::memory_order_relaxed);
    const auto previousRecoveryState = captureDeviceRecoveryState();
    auto setupToApply = setup;
    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setupToApply, true);
        if (result.isNotEmpty()) {
            Log::warn("AUDIO", "Device setup retry with driver default channels after: " + result);
            useDefaultConfiguredChannels(setupToApply, true, !outputNone_.load(std::memory_order_relaxed));
            result = deviceManager_.setAudioDeviceSetup(setupToApply, true);
        }
    }

    if (result.isNotEmpty()) {
        auto msg = context.isNotEmpty()
            ? context + " failed: " + result
            : juce::String("Audio device setup failed: ") + result;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup, previousOutputNone, previousRecoveryState, msg,
            context.isNotEmpty() ? context : juce::String("device-setup apply"));
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    if (device
        && device->getCurrentSampleRate() > 0.0
        && device->getCurrentBufferSizeSamples() > 0
        && !hasUsableActiveChannels(appliedSetup, device)) {
        markActiveChannelLossIfNeeded(
            appliedSetup, device,
            context.isNotEmpty()
                ? context + " active-channel invalid"
                : juce::String("Audio device setup active-channel invalid"));
        recoverActiveChannelsWithDriverDefaults(
            context.isNotEmpty()
                ? context + " active-channel recovery"
                : juce::String("Audio device setup active-channel recovery"));
        device = deviceManager_.getCurrentAudioDevice();
        deviceManager_.getAudioDeviceSetup(appliedSetup);
    }

    const bool ready = device
        && device->getCurrentSampleRate() > 0.0
        && device->getCurrentBufferSizeSamples() > 0
        && hasUsableActiveChannels(appliedSetup, device);
    if (!ready) {
        auto msg = context.isNotEmpty()
            ? context + " failed: device did not reopen with usable active channels"
            : juce::String("Audio device setup failed: device is not ready");
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup, previousOutputNone, previousRecoveryState, msg,
            context.isNotEmpty() ? context : juce::String("device-setup apply"));
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }

    clearDeviceLossAfterReady(appliedSetup);
    updateInputEndpointWatcherTarget(appliedSetup.inputDeviceName);
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    return ActionResult::ok();
}

ActionResult AudioEngine::setBufferSize(int bufferSize)
{
    if (bufferSize <= 0)
        return ActionResult::fail("Buffer size must be greater than zero");

    juce::AudioDeviceManager::AudioDeviceSetup previousSetup;
    deviceManager_.getAudioDeviceSetup(previousSetup);
    const bool previousOutputNone =
        outputNone_.load(std::memory_order_relaxed);
    const auto previousRecoveryState = captureDeviceRecoveryState();
    const auto previousDesiredBufferSize = desiredBufferSize_;
    const auto previousDesiredSRBSSet = desiredSRBSSet_;

    // Skip restart if device already has the requested buffer size
    {
        int actualBS = 0;
        if (auto* dev = deviceManager_.getCurrentAudioDevice())
            actualBS = dev->getCurrentBufferSizeSamples();
        if (previousSetup.bufferSize == bufferSize && actualBS == bufferSize) {
            currentBufferSize_ = bufferSize;
            desiredBufferSize_ = bufferSize;
            desiredSRBSSet_ = true;
            return ActionResult::ok();
        }
    }

    juce::Array<int> supported;
    if (auto* currentDevice = deviceManager_.getCurrentAudioDevice())
        supported = currentDevice->getAvailableBufferSizes();

    auto setup = previousSetup;
    setup.bufferSize = bufferSize;
    juce::String error;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        error = deviceManager_.setAudioDeviceSetup(setup, true);
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    auto isReady = [this, previousOutputNone](
                       juce::AudioIODevice* candidate,
                       const juce::AudioDeviceManager::AudioDeviceSetup& candidateSetup) {
        return candidate
            && candidate->getCurrentSampleRate() > 0.0
            && candidate->getCurrentBufferSizeSamples() > 0
            && hasUsableActiveChannels(
                candidateSetup, candidate, previousOutputNone);
    };
    int actual = device ? device->getCurrentBufferSizeSamples() : 0;

    // If the requested size didn't apply, find the best alternative from device-supported sizes
    if (error.isNotEmpty() || !isReady(device, appliedSetup)
        || actual != bufferSize) {
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
            if (best != bufferSize && best != actual) {
                setup.bufferSize = best;
                juce::String fallbackError;
                {
                    AtomicGuard intentionalGuard(intentionalChange_);
                    fallbackError = deviceManager_.setAudioDeviceSetup(setup, true);
                }
                if (fallbackError.isNotEmpty())
                    Log::warn("AUDIO", "Buffer fallback failed (requested=" + juce::String(bufferSize)
                        + " fallback=" + juce::String(best) + "): " + fallbackError);
                // Re-fetch device pointer (setAudioDeviceSetup may replace it)
                device = deviceManager_.getCurrentAudioDevice();
                deviceManager_.getAudioDeviceSetup(appliedSetup);
                actual = device ? device->getCurrentBufferSizeSamples() : 0;
                if (fallbackError.isNotEmpty())
                    error = fallbackError;
                else
                    error.clear();
            }
        }
    }

    if (error.isNotEmpty() || !isReady(device, appliedSetup)) {
        auto msg = "setBufferSize failed (requested=" + juce::String(bufferSize)
            + "): " + (error.isNotEmpty()
                ? error
                : juce::String("device did not reopen ready"));
        desiredBufferSize_ = previousDesiredBufferSize;
        desiredSRBSSet_ = previousDesiredSRBSSet;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup, previousOutputNone, previousRecoveryState, msg,
            "buffer-size change");
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }

    // Only notify if the final applied size differs from requested.
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

    currentBufferSize_ = actual;
    desiredBufferSize_ = actual;  // keep runtime-aligned value to avoid inconsistent restart/reconnect state
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
    if (sampleRate <= 0.0)
        return ActionResult::fail("Sample rate must be greater than zero");

    juce::AudioDeviceManager::AudioDeviceSetup previousSetup;
    deviceManager_.getAudioDeviceSetup(previousSetup);
    const bool previousOutputNone =
        outputNone_.load(std::memory_order_relaxed);
    const auto previousRecoveryState = captureDeviceRecoveryState();
    const auto previousDesiredSampleRate = desiredSampleRate_;
    const auto previousDesiredSRBSSet = desiredSRBSSet_;

    // Skip restart if device already has the requested sample rate
    {
        double actualSR = 0;
        if (auto* dev = deviceManager_.getCurrentAudioDevice())
            actualSR = dev->getCurrentSampleRate();
        if (previousSetup.sampleRate == sampleRate && actualSR == sampleRate) {
            currentSampleRate_ = sampleRate;
            desiredSampleRate_ = sampleRate;
            desiredSRBSSet_ = true;
            return ActionResult::ok();
        }
    }

    auto setup = previousSetup;
    setup.sampleRate = sampleRate;
    juce::String error;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        error = deviceManager_.setAudioDeviceSetup(setup, true);
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    const bool ready = error.isEmpty()
        && device
        && device->getCurrentSampleRate() > 0.0
        && device->getCurrentBufferSizeSamples() > 0
        && hasUsableActiveChannels(
            appliedSetup, device, previousOutputNone);
    if (!ready) {
        auto msg = "setSampleRate failed (requested=" + juce::String(sampleRate)
            + "): " + (error.isNotEmpty()
                ? error
                : juce::String("device did not reopen ready"));
        desiredSampleRate_ = previousDesiredSampleRate;
        desiredSRBSSet_ = previousDesiredSRBSSet;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup, previousOutputNone, previousRecoveryState, msg,
            "sample-rate change");
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }

    const double actual = device->getCurrentSampleRate();

    if (std::abs(actual - sampleRate) > 1.0) {
        auto msg = "Sample rate: " + juce::String(static_cast<int>(sampleRate))
            + " -> " + juce::String(static_cast<int>(actual)) + " Hz";
        Log::info("AUDIO", "Sample rate: requested " + juce::String(sampleRate)
            + " -> applied " + juce::String(actual));
        pushNotification(msg, NotificationLevel::Info);
    }

    currentSampleRate_ = actual;
    if (getCurrentDeviceType().containsIgnoreCase("ASIO") && sampleRatesDiffer(sampleRate, actual)) {
        desiredSampleRate_ = sampleRate;
        Log::warn("AUDIO", "ASIO sample-rate request was not applied; preserving requested SR="
            + juce::String(static_cast<int>(sampleRate)) + " actual SR="
            + juce::String(static_cast<int>(actual)));
    } else {
        desiredSampleRate_ = actual;
    }
    desiredSRBSSet_ = true;
    Log::audit("AUDIO", "Sample rate set: requested=" + juce::String(sampleRate)
        + " actual=" + juce::String(actual));
    return ActionResult::ok(std::abs(actual - sampleRate) > 1.0
        ? "Sample rate: " + juce::String(static_cast<int>(sampleRate))
            + " -> " + juce::String(static_cast<int>(actual)) + " Hz"
        : juce::String());
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

// Device type management

ActionResult AudioEngine::setAudioDeviceType(const juce::String& typeName, const juce::String& preferredAsioDevice)
{
    auto currentType = getCurrentDeviceType();
    auto* targetType = findDeviceType(deviceManager_, typeName);
    if (!targetType) {
        const auto msg = "Audio driver type is not available: " + typeName;
        Log::error("AUDIO", msg);
        if (onDeviceError)
            onDeviceError(msg);
        return ActionResult::fail(msg);
    }

    if (!running_ && deviceManager_.getCurrentAudioDevice() == nullptr) {
        desiredDeviceType_ = typeName;
        return ActionResult::ok();
    }
    if (currentType == typeName && isCurrentAudioDeviceReady()) {
        desiredDeviceType_ = typeName;
        // Already on ASIO but possibly wrong device switch to preferred
        if (typeName.containsIgnoreCase("ASIO") && preferredAsioDevice.isNotEmpty()) {
            juce::AudioDeviceManager::AudioDeviceSetup cur;
            deviceManager_.getAudioDeviceSetup(cur);
            if (cur.inputDeviceName != preferredAsioDevice) {
                auto result = setAsioDevice(preferredAsioDevice);
                if (!result)
                    return result;
            }
        }
        return ActionResult::ok();
    }

    const auto previousDesiredSampleRate = desiredSampleRate_;
    const auto previousDesiredBufferSize = desiredBufferSize_;
    const auto previousDesiredSRBSSet = desiredSRBSSet_;
    const auto previousRecoveryState = captureDeviceRecoveryState();

    juce::AudioDeviceManager::AudioDeviceSetup currentSetup;
    deviceManager_.getAudioDeviceSetup(currentSetup);
    DriverTypeSnapshot previousRuntimeSnapshot;
    previousRuntimeSnapshot.inputDevice = currentSetup.inputDeviceName;
    previousRuntimeSnapshot.outputDevice = currentSetup.outputDeviceName;
    previousRuntimeSnapshot.sampleRate = currentSetup.sampleRate;
    previousRuntimeSnapshot.bufferSize = currentSetup.bufferSize;
    previousRuntimeSnapshot.inputChannels = currentSetup.inputChannels;
    previousRuntimeSnapshot.outputChannels = currentSetup.outputChannels;
    previousRuntimeSnapshot.outputNone =
        outputNone_.load(std::memory_order_relaxed);
    if (auto* currentDevice = deviceManager_.getCurrentAudioDevice()) {
        if (currentDevice->getCurrentSampleRate() > 0.0)
            previousRuntimeSnapshot.sampleRate =
                currentDevice->getCurrentSampleRate();
        if (currentDevice->getCurrentBufferSizeSamples() > 0)
            previousRuntimeSnapshot.bufferSize =
                currentDevice->getCurrentBufferSizeSamples();
    }

    // Save long-lived preferences only when they belong to the current driver.
    // During cross-driver recovery, desired names belong to the missing target
    // and must never contaminate the actual fallback driver's snapshot.
    if (currentType.isNotEmpty()) {
        const bool desiredTargetsBelongToCurrentType =
            desiredDeviceType_.isEmpty() || desiredDeviceType_ == currentType;
        if (desiredTargetsBelongToCurrentType) {
            auto snap = previousRuntimeSnapshot;
            {
                const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
                if (desiredInputDevice_.isNotEmpty())
                    snap.inputDevice = desiredInputDevice_;
                if (desiredOutputDevice_.isNotEmpty())
                    snap.outputDevice = desiredOutputDevice_;
            }
            driverSnapshots_[currentType] = snap;
            Log::info("AUDIO", "Saved " + currentType + " snapshot: in='" + snap.inputDevice
                + "' out='" + snap.outputDevice + "' SR=" + juce::String(snap.sampleRate)
                + " BS=" + juce::String(snap.bufferSize)
                + (snap.outputNone ? " outputNone" : ""));
        } else {
            Log::audit("AUDIO", "Preserving " + currentType
                + " preference snapshot while recovering desired driver "
                + desiredDeviceType_);
        }
    }

    // Resolve and preflight the target snapshot before JUCE is allowed to
    // close the current stream or open a replacement default endpoint.
    auto snapIt = driverSnapshots_.find(typeName);
    const bool hasSnapshot = snapIt != driverSnapshots_.end();
    DriverTypeSnapshot seededSnapshot;
    const DriverTypeSnapshot* restoreSnapshot =
        hasSnapshot ? &snapIt->second : nullptr;
    bool usingSeededSnapshot = false;
    if (!restoreSnapshot) {
        const auto currentSnapIt = driverSnapshots_.find(currentType);
        if (currentSnapIt != driverSnapshots_.end()
            && audio_device_recovery_detail::seedCompatibleWindowsDriverSnapshot(
                currentType, typeName, false, currentSnapIt->second,
                seededSnapshot)) {
            restoreSnapshot = &seededSnapshot;
            usingSeededSnapshot = true;
            Log::info("AUDIO", "Carrying current Windows endpoint selection into first "
                + typeName + " switch: in='" + seededSnapshot.inputDevice
                + "' out='" + seededSnapshot.outputDevice + "'");
        }
    }

    targetType->scanForDevices();
    const auto targetInputs = targetType->getDeviceNames(true);
    const auto targetOutputs = targetType->getDeviceNames(false);
    const bool hasExplicitAsioTarget =
        typeName.containsIgnoreCase("ASIO")
        && preferredAsioDevice.isNotEmpty();
    if (restoreSnapshot
        && !hasExplicitAsioTarget
        && !audio_device_recovery_detail::snapshotEndpointsAvailable(
            *restoreSnapshot, targetInputs, targetOutputs)) {
        const auto msg = (usingSeededSnapshot
            ? juce::String("Cannot preserve the selected Windows endpoints while switching to ")
            : juce::String("Saved endpoints are unavailable while switching to "))
            + typeName + ": in='" + restoreSnapshot->inputDevice
            + "' out='" + restoreSnapshot->outputDevice + "'";
        Log::error("AUDIO", msg);
        if (onDeviceError)
            onDeviceError(msg);
        return ActionResult::fail(msg);
    }
    if (typeName.containsIgnoreCase("ASIO")
        && preferredAsioDevice.isNotEmpty()
        && !targetOutputs.contains(preferredAsioDevice)) {
        const auto msg =
            "Preferred ASIO device not available: " + preferredAsioDevice;
        Log::error("AUDIO", msg);
        if (onDeviceError)
            onDeviceError(msg);
        return ActionResult::fail(msg);
    }

    // A driver-type change can open the target driver's default device inside
    // JUCE before our later setup restore runs. Suspend any configured monitor
    // before entering an exclusive driver, then restore it if the actual main
    // output is different or the switch rolls back.
    const auto suspendedMonitor = suspendMonitorBeforeExclusiveOpen(
        typeName, {}, true);

    // Set intentional flag BEFORE removing callback, because removeAudioCallback
    // synchronously calls audioDeviceStopped() which checks intentionalChange_.
    intentionalChange_.store(true, std::memory_order_release);
    deviceManager_.removeAudioCallback(this);

    deviceManager_.setCurrentAudioDeviceType(typeName, true);

    auto restorePreviousDriverAfterFailure =
        [this, &currentType, &suspendedMonitor,
         previousRuntimeSnapshot,
         previousDesiredSampleRate, previousDesiredBufferSize,
         previousDesiredSRBSSet, previousRecoveryState](
            const juce::String& failureMessage,
            const juce::String& notificationMessage) -> ActionResult {
            Log::error("AUDIO", failureMessage);
            juce::String restoreError;
            if (currentType.isEmpty()) {
                restoreError = "previous driver type is unavailable";
            } else {
                deviceManager_.setCurrentAudioDeviceType(currentType, true);
                restoreError =
                    audio_device_recovery_detail::restoreDriverSnapshot(
                        deviceManager_, previousRuntimeSnapshot);
            }
            juce::AudioDeviceManager::AudioDeviceSetup restoredSetup;
            deviceManager_.getAudioDeviceSetup(restoredSetup);
            auto* restoredDevice = deviceManager_.getCurrentAudioDevice();
            const bool restoreReady = restoreError.isEmpty()
                && restoredDevice
                && restoredDevice->getCurrentSampleRate() > 0.0
                && restoredDevice->getCurrentBufferSizeSamples() > 0
                && hasUsableActiveChannels(
                    restoredSetup, restoredDevice,
                    previousRuntimeSnapshot.outputNone);
            if (!restoreReady)
                deviceManager_.closeAudioDevice();

            desiredSampleRate_ = previousDesiredSampleRate;
            desiredBufferSize_ = previousDesiredBufferSize;
            desiredSRBSSet_ = previousDesiredSRBSSet;
            deviceManager_.addAudioCallback(this);
            intentionalChange_.store(false, std::memory_order_release);
            if (restoreReady) {
                currentSampleRate_ = restoredDevice->getCurrentSampleRate();
                currentBufferSize_ =
                    restoredDevice->getCurrentBufferSizeSamples();
                restoreDeviceRecoveryState(previousRecoveryState);
            }

            auto finalMessage = failureMessage;
            if (!restoreSuspendedMonitor(
                    suspendedMonitor,
                    currentSampleRate_.load(std::memory_order_relaxed),
                    "driver-switch rollback"))
                finalMessage += "; monitor restore failed";
            if (!restoreReady) {
                const auto reason = restoreError.isNotEmpty()
                    ? restoreError
                    : juce::String("snapshot restored without a usable input/output stream");
                finalMessage += "; previous driver snapshot restore failed: " + reason;
                Log::error("AUDIO", finalMessage);
                enterDeviceRecoveryAfterRestoreFailure();
            } else {
                Log::info("AUDIO", "Previous driver snapshot restored: " + currentType);
            }

            if (onDeviceError)
                onDeviceError(restoreReady ? notificationMessage : finalMessage);
            return ActionResult::fail(finalMessage);
        };

    // For ASIO, pick the preferred device (or first available)
    if (typeName.containsIgnoreCase("ASIO")) {
        auto failAsioSwitch = [&restorePreviousDriverAfterFailure](
                                  const juce::String& msg) -> ActionResult {
            return restorePreviousDriverAfterFailure(
                msg, "ASIO switch failed - reverted to previous driver");
        };

        auto* type = deviceManager_.getCurrentDeviceTypeObject();
        if (!type)
            return failAsioSwitch("ASIO driver type is not available");

        type->scanForDevices();
        auto devices = type->getDeviceNames(false);
        if (devices.isEmpty())
            return failAsioSwitch("No ASIO devices available");

        if (preferredAsioDevice.isNotEmpty() && !devices.contains(preferredAsioDevice))
            return failAsioSwitch("Preferred ASIO device not available: " + preferredAsioDevice);

        juce::String savedAsioDevice;
        if (hasSnapshot && preferredAsioDevice.isEmpty()) {
            const auto& snapshot = snapIt->second;
            if (!audio_device_recovery_detail::snapshotEndpointsAvailable(
                    snapshot, devices, devices)) {
                return failAsioSwitch(
                    "Saved ASIO device is unavailable: in='" + snapshot.inputDevice
                    + "' out='" + snapshot.outputDevice + "'");
            }
            savedAsioDevice = selectAsioDuplexDevice(
                snapshot.inputDevice, snapshot.outputDevice);
        }

        // Build ordered try-list. A saved ASIO restore target is strict:
        // never fall through to FL/Realtek/etc. when the saved device is
        // missing, because ASIO drivers are duplex and not interchangeable.
        juce::StringArray tryOrder;
        if (preferredAsioDevice.isNotEmpty()) {
            addUniqueDevice(tryOrder, preferredAsioDevice);
        } else if (savedAsioDevice.isNotEmpty()) {
            addUniqueDevice(tryOrder, savedAsioDevice);
        } else {
            if (lastAsioDevice_.isNotEmpty() && devices.contains(lastAsioDevice_))
                addUniqueDevice(tryOrder, lastAsioDevice_);
            for (auto& d : devices)
                addUniqueDevice(tryOrder, d);
        }
        Log::info("AUDIO", "ASIO device try order: [" + tryOrder.joinIntoString(", ")
            + "] preferred='" + preferredAsioDevice + "' last='" + lastAsioDevice_ + "'");

        // Use snapshot SR/BS if available (known to work on this ASIO device),
        // otherwise fall back to current values from the old driver.
        double sr = desiredSampleRate_ > 0.0
            ? desiredSampleRate_
            : currentSampleRate_.load();
        int bs = desiredBufferSize_ > 0
            ? desiredBufferSize_
            : currentBufferSize_.load();
        if (hasSnapshot) {
            if (!desiredSRBSSet_ && snapIt->second.sampleRate > 0)
                sr = snapIt->second.sampleRate;
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
            if (hasSnapshot && !snapIt->second.inputChannels.isZero())
                setup.inputChannels = snapIt->second.inputChannels;
            else
                setup.inputChannels.setRange(0, 2, true);
            if (hasSnapshot && !snapIt->second.outputChannels.isZero())
                setup.outputChannels = snapIt->second.outputChannels;
            else
                setup.outputChannels.setRange(0, 2, true);

            auto result = deviceManager_.setAudioDeviceSetup(setup, true);
            if (result.isNotEmpty()) {
                Log::warn("AUDIO", "ASIO setup failed (device='" + deviceToUse + "' SR=" + juce::String(sr) + " BS=" + juce::String(bs) + "): " + result);
                // First relax only the channel mask. If the explicit
                // mask was the problem, keep the requested SR/BS so a
                // 48k restore does not unnecessarily fall through to a
                // driver-chosen 44.1k mode.
                useDefaultConfiguredChannels(setup, true, true);
                result = deviceManager_.setAudioDeviceSetup(setup, true);
                if (result.isNotEmpty()) {
                    Log::warn("AUDIO", "ASIO default-channel retry failed (device='" + deviceToUse + "'): " + result);
                    // Last resort: let the ASIO driver choose SR/BS as well,
                    // still using its native channel layout.
                    setup.sampleRate = 0;
                    setup.bufferSize = 0;
                    useDefaultConfiguredChannels(setup, true, true);
                    result = deviceManager_.setAudioDeviceSetup(setup, true);
                }
                if (result.isNotEmpty()) {
                    Log::warn("AUDIO", "ASIO fallback also failed (device='" + deviceToUse + "'): " + result);
                    continue;  // try next device
                }
            }
            juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
            deviceManager_.getAudioDeviceSetup(appliedSetup);
            auto* opened = deviceManager_.getCurrentAudioDevice();
            const auto actualSR = opened ? opened->getCurrentSampleRate() : 0.0;
            const auto actualBS = opened ? opened->getCurrentBufferSizeSamples() : 0;
            const auto activeInput = opened
                ? opened->getActiveInputChannels()
                : juce::BigInteger {};
            const auto activeOutput = opened
                ? opened->getActiveOutputChannels()
                : juce::BigInteger {};
            const bool candidateReady = opened
                && opened->isOpen()
                && opened->getName() == deviceToUse
                && actualSR > 0.0
                && actualBS > 0
                && hasUsableActiveChannels(appliedSetup, opened, false);
            if (!candidateReady) {
                Log::warn("AUDIO", "ASIO candidate opened without a usable duplex stream "
                    "(requested='" + deviceToUse
                    + "' actual='" + (opened ? opened->getName() : juce::String("none"))
                    + "' SR=" + juce::String(actualSR)
                    + " BS=" + juce::String(actualBS)
                    + " activeIn=[" + channelMaskToLogString(activeInput)
                    + "] activeOut=[" + channelMaskToLogString(activeOutput)
                    + "]); trying next candidate");
                continue;
            }
            if (sampleRatesDiffer(sr, actualSR)) {
                Log::warn("AUDIO", "ASIO opened at unexpected sample rate (device='"
                    + deviceToUse + "' requested SR=" + juce::String(static_cast<int>(sr))
                    + " actual SR=" + juce::String(static_cast<int>(actualSR)) + ")");
            }
            asioOpened = true;
            break;
        }

        if (!asioOpened) {
            auto msg = juce::String("All ASIO devices failed - reverting to previous driver");
            return restorePreviousDriverAfterFailure(
                msg, "ASIO switch failed - reverted to previous driver");
        }
    } else {
        // Non-ASIO: initialize with defaults first, then try to restore snapshot
        auto result = deviceManager_.initialiseWithDefaultDevices(2, 2);
        if (result.isNotEmpty()) {
            auto msg = "Failed to switch to " + typeName + ": " + result;
            return restorePreviousDriverAfterFailure(
                msg, "Driver switch failed - reverted to " + currentType);
        }

        // Resolve device names and apply settings. A saved target snapshot wins;
        // on the first switch between Windows shared/low-latency/exclusive
        // variants, carry the current endpoint selection instead of accepting
        // arbitrary target-driver defaults.
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);

        if (restoreSnapshot) {
            const auto& snap = *restoreSnapshot;
            // Check if saved devices still exist in this driver type
            if (auto* typeObj = deviceManager_.getCurrentDeviceTypeObject()) {
                typeObj->scanForDevices();
                const auto availableInputs = typeObj->getDeviceNames(true);
                const auto availableOutputs = typeObj->getDeviceNames(false);
                if (!audio_device_recovery_detail::snapshotEndpointsAvailable(
                        snap, availableInputs, availableOutputs)) {
                    auto msg = (usingSeededSnapshot
                        ? juce::String("Cannot preserve the selected Windows endpoints while switching to ")
                        : juce::String("Saved endpoints are unavailable while switching to "))
                        + typeName + ": in='" + snap.inputDevice
                        + "' out='" + snap.outputDevice + "'";
                    return restorePreviousDriverAfterFailure(
                        msg, "Driver switch cancelled - selected endpoints are unavailable");
                }
                if (availableInputs.contains(snap.inputDevice))
                    setup.inputDeviceName = snap.inputDevice;
                if (availableOutputs.contains(snap.outputDevice))
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
        if (restoreSnapshot && !restoreSnapshot->inputChannels.isZero())
            setup.inputChannels = restoreSnapshot->inputChannels;
        else
            setup.inputChannels.setRange(0, 2, true);
        if (restoreSnapshot && !restoreSnapshot->outputChannels.isZero())
            setup.outputChannels = restoreSnapshot->outputChannels;
        else
            setup.outputChannels.setRange(0, 2, true);
        auto restoreErr = deviceManager_.setAudioDeviceSetup(setup, true);
        if (restoreErr.isNotEmpty()) {
            Log::warn("AUDIO", "Snapshot restore failed: " + restoreErr);
            const bool snapshotUsesOutput =
                restoreSnapshot && !restoreSnapshot->outputNone;
            useDefaultConfiguredChannels(
                setup, true,
                restoreSnapshot
                    ? snapshotUsesOutput
                    : !outputNone_.load(std::memory_order_relaxed));
            restoreErr = deviceManager_.setAudioDeviceSetup(setup, true);
            if (restoreErr.isNotEmpty()) {
                Log::warn("AUDIO", "Snapshot default-channel restore failed: " + restoreErr);
                if (restoreSnapshot) {
                    auto msg = "Cannot restore the selected endpoints while switching to "
                        + typeName + ": " + restoreErr;
                    return restorePreviousDriverAfterFailure(
                        msg, "Driver switch failed - previous endpoints restored");
                }
            }
        }
    }
    // Update current runtime SR/BS from the ACTUAL device after type switch.
    // The device may have opened at a different SR/BS than the snapshot requested
    // (e.g., ASIO ignores our request and uses its global setting).
    // For ASIO, preserve an explicit requested SR so accidental 44.1k reports do
    // not become the next startup target; still save the actual buffer size.
    bool switchedDeviceReady = false;
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        auto sr = device->getCurrentSampleRate();
        auto bs = device->getCurrentBufferSizeSamples();
        if (sr > 0.0 && bs > 0) {
            syncRuntimeRateFromActual(sr, bs, "Driver switch actual rate",
                                      typeName.containsIgnoreCase("ASIO"));
            juce::AudioDeviceManager::AudioDeviceSetup switchedSetup;
            deviceManager_.getAudioDeviceSetup(switchedSetup);
            const bool targetOutputNone =
                restoreSnapshot && restoreSnapshot->outputNone;
            switchedDeviceReady = hasUsableActiveChannels(
                switchedSetup, device, targetOutputNone);
            if (!switchedDeviceReady)
                Log::warn("AUDIO", "Driver switch opened without usable active channels: "
                    + typeName);
        } else {
            Log::warn("AUDIO", "Driver switch reported invalid SR=" + juce::String(sr)
                + " BS=" + juce::String(bs) + " after switching to " + typeName);
        }
    }
    if (!switchedDeviceReady) {
        return restorePreviousDriverAfterFailure(
            "Driver switch completed but audio device is not ready: " + typeName,
            "Driver switch failed - previous endpoints restored");
    }
    // Clear reconnection state after intentional type switch.
    // Without this, audioDeviceAboutToStart would see stale desired names
    // (e.g. ASIO "TOPPING") vs actual WASAPI names "fallback detected" infinite loop.
    deviceLost_.store(false, std::memory_order_relaxed);
    inputDeviceLost_.store(false, std::memory_order_relaxed);
    cancelPendingExternalRestartReopen();
    startupRestorePending_ = false;
    releaseAutomaticOutputMute();
    // Clear "None" output state new driver type has its own output device.
    // Without this, OUT mute button stays locked after WASAPI "None" ASIO switch.
    if (outputNone_.load(std::memory_order_relaxed)) {
        outputNone_.store(false, std::memory_order_seq_cst);
        outputMuted_.store(false, std::memory_order_seq_cst);
    }
    // Restore "None" from snapshot if target driver had it saved
    if (hasSnapshot && snapIt->second.outputNone) {
        outputNone_.store(true, std::memory_order_seq_cst);
        outputMuted_.store(true, std::memory_order_seq_cst);
    }
    reconnectCooldown_ = 0;
    reconnectMissCount_ = 0;
    desiredDeviceType_ = typeName;

    // Update desired device names to new driver's actual devices
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);
        auto appliedInput = setup.inputDeviceName;
        {
            const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
            if (setup.inputDeviceName.isNotEmpty())
                desiredInputDevice_ = setup.inputDeviceName;
            if (setup.outputDeviceName.isNotEmpty())
                desiredOutputDevice_ = setup.outputDeviceName;
        }
        if (typeName.containsIgnoreCase("ASIO"))
            lastAsioDevice_ = selectAsioDuplexDevice(
                setup.inputDeviceName, setup.outputDeviceName);
        updateInputEndpointWatcherTarget(appliedInput);
    }

    deviceManager_.addAudioCallback(this);
    intentionalChange_.store(false, std::memory_order_release);

    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    resolveSuspendedMonitorAfterMainOpen(
        suspendedMonitor, typeName, appliedSetup.outputDeviceName);

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
    // only resync the baseline counter so delta calculation stays correct.
    //   History is preserved so XRun display doesn't vanish on device restart.
    if (xrunBaselineResync_.exchange(false, std::memory_order_acquire)) {
        int xruns = device->getXRunCount();
        lastDeviceXRunCount_ = (xruns >= 0) ? xruns : 0;
    }

    // User-initiated full reset (Action::XRunReset) clear everything.
    if (xrunResetRequested_.exchange(false, std::memory_order_acquire)) {
        int xruns = device->getXRunCount();
        lastDeviceXRunCount_ = (xruns >= 0) ? xruns : 0;
        std::memset(xrunHistory_, 0, sizeof(xrunHistory_));
        xrunHistoryIdx_ = 0;
        lastXRunBucketTime_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        lastXRunWarningTime_ = 0.0;
        recentXRuns_.store(0, std::memory_order_relaxed);
        return;
    }

    int currentCount = device->getXRunCount();
    if (currentCount < 0) return;  // Unsupported

    int delta = currentCount - lastDeviceXRunCount_;
    if (delta < 0) delta = 0;  // Device was reset
    lastDeviceXRunCount_ = currentCount;

    // Use real elapsed time for accurate 1-second bucket rotation
    // (JUCE timers are not perfectly 30Hz accumulated drift causes
    //  the 60-slot window to take longer than 60 seconds)
    double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    double elapsed = now - lastXRunBucketTime_;

    if (elapsed >= 1.0) {
        const int elapsedBuckets = juce::jlimit(1, 60, static_cast<int>(elapsed));
        lastXRunBucketTime_ = (elapsedBuckets >= 60)
            ? now
            : lastXRunBucketTime_ + static_cast<double>(elapsedBuckets);

        // Move by every elapsed second. If the message thread was blocked by
        // device restart or restore work, stale XRuns must still age out of the
        // 60-second display window instead of lingering for minutes.
        for (int i = 0; i < elapsedBuckets; ++i) {
            xrunHistoryIdx_ = (xrunHistoryIdx_ + 1) % 60;
            xrunHistory_[xrunHistoryIdx_] = 0;
        }

        // Recompute total from all 60 slots
        int total = 0;
        for (int i = 0; i < 60; ++i)
            total += xrunHistory_[i];
        recentXRuns_.store(total, std::memory_order_relaxed);
    }

    // Add current delta to the active slot
    if (delta > 0) {
        totalXRunEvents_.fetch_add(static_cast<uint64_t>(delta), std::memory_order_relaxed);
        xrunHistory_[xrunHistoryIdx_] += delta;

        // Update total immediately
        int total = 0;
        for (int i = 0; i < 60; ++i)
            total += xrunHistory_[i];
        recentXRuns_.store(total, std::memory_order_relaxed);

        if (now - lastXRunWarningTime_ >= 1.0) {
            lastXRunWarningTime_ = now;
            Log::warn("AUDIO", "XRun increased: delta=" + juce::String(delta)
                + " recent60=" + juce::String(total)
                + " total=" + juce::String(static_cast<juce::int64>(
                    totalXRunEvents_.load(std::memory_order_relaxed)))
                + " device='" + device->getName()
                + "' SR=" + juce::String(device->getCurrentSampleRate())
                + " BS=" + juce::String(device->getCurrentBufferSizeSamples()));
            logDeviceSetupSnapshot("XRun device setup", device);
        }
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
    if (firstChannel < 0 || numChannels <= 0)
        return ActionResult::fail("Input channel range is invalid");

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    const auto previousSetup = setup;
    const bool previousOutputNone =
        outputNone_.load(std::memory_order_relaxed);
    const auto previousRecoveryState = captureDeviceRecoveryState();

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange(firstChannel, numChannels, true);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    auto* appliedDevice = deviceManager_.getCurrentAudioDevice();
    const bool ready = result.isEmpty()
        && appliedDevice
        && appliedDevice->getCurrentSampleRate() > 0.0
        && appliedDevice->getCurrentBufferSizeSamples() > 0
        && hasUsableActiveChannels(
            appliedSetup, appliedDevice, previousOutputNone);
    if (!ready) {
        if (result.isEmpty())
            result = "device did not reopen with usable active channels";
        auto msg = "Failed to set input channels (first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels) + "): " + result;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup, previousOutputNone, previousRecoveryState, msg,
            "input-channel change");
        if (restored)
            Log::error("AUDIO", msg);
        return ActionResult::fail(msg);
    }
    Log::audit("AUDIO", "Input channels set: first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels));
    return ActionResult::ok();
}

ActionResult AudioEngine::setActiveOutputChannels(int firstChannel, int numChannels)
{
    if (firstChannel < 0 || numChannels <= 0)
        return ActionResult::fail("Output channel range is invalid");

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    const auto previousSetup = setup;
    const bool previousOutputNone =
        outputNone_.load(std::memory_order_relaxed);
    const auto previousRecoveryState = captureDeviceRecoveryState();

    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange(firstChannel, numChannels, true);

    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setup, true);
    }
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    deviceManager_.getAudioDeviceSetup(appliedSetup);
    auto* appliedDevice = deviceManager_.getCurrentAudioDevice();
    const bool ready = result.isEmpty()
        && appliedDevice
        && appliedDevice->getCurrentSampleRate() > 0.0
        && appliedDevice->getCurrentBufferSizeSamples() > 0
        && hasUsableActiveChannels(
            appliedSetup, appliedDevice, previousOutputNone);
    if (!ready) {
        if (result.isEmpty())
            result = "device did not reopen with usable active channels";
        auto msg = "Failed to set output channels (first=" + juce::String(firstChannel) + " num=" + juce::String(numChannels) + "): " + result;
        const auto restored = restorePreviousSetupAfterFailedChange(
            previousSetup, previousOutputNone, previousRecoveryState, msg,
            "output-channel change");
        if (restored)
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

// ============================================================================
// Real-time audio callback NO allocations, NO locks, NO I/O
// ============================================================================

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // RT audio callback rules:
    // RULES: no allocation | no mutex | no writeToLog | no throw
    // Pre-allocated: workBuffer_, emptyMidi_ | Atomics: relaxed ordering
    // Keep this path deterministic and lock-free.

    // RT thread only must NOT be called from the message thread
    jassert(!juce::MessageManager::getInstanceWithoutCreating()
            || !juce::MessageManager::getInstance()->isThisTheMessageThread());

    // One-time MMCSS registration on the audio callback thread (Windows only).
    // Boosts thread priority via Multimedia Class Scheduler Service, reducing
    // DPC latency interference. JUCE WASAPI registers at AVRT_PRIORITY_NORMAL;
    // DirectPipe keeps the main OUT callback at AVRT_PRIORITY_CRITICAL for
    // strongest protection; MonitorOutput stays lower so it cannot preempt it.
    // For ASIO (where JUCE does NO MMCSS), this is the only MMCSS registration.
#if defined(_WIN32)
    if (!mmcssRegistered_.load(std::memory_order_acquire)) {
        if (avSetMmThreadChar_ && avSetMmThreadPrio_) {
            DWORD taskIndex = 0;
            HANDLE h = avSetMmThreadChar_(L"Pro Audio", &taskIndex);
            if (h) {
                mmcssTaskHandle_.store(h, std::memory_order_relaxed);
                mmcssThreadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);
                avSetMmThreadPrio_(h, 2);  // AVRT_PRIORITY_CRITICAL
            }
        }
        mmcssRegistered_.store(true, std::memory_order_release);
    }
#endif

    // Flush denormalized floats to zero prevents 10-100x CPU spikes
    // when VST plugins process near-silence (reverb tails, compressor release, etc.)
    juce::ScopedNoDenormals noDenormals;

    latencyMonitor_.markCallbackStart();

    const int chMode = channelMode_.load(std::memory_order_relaxed);
    const float gain = inputGain_.load(std::memory_order_relaxed);
    const bool muted = muted_.load(std::memory_order_relaxed);
    const bool outputMuted = isOutputMuted();

    const int callbackSamples = numSamples;
    numSamples = juce::jlimit(0, workBuffer_.getNumSamples(), numSamples);

    auto clearOutputRange = [&](int startSample, int samplesToClear) {
        if (samplesToClear <= 0) return;
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch])
                std::memset(outputChannelData[ch] + startSample, 0,
                            sizeof(float) * static_cast<size_t>(samplesToClear));
    };

    // Fast path: panic muted zero output, skip all processing
    if (muted) {
        clearOutputRange(0, callbackSamples);
        inputLevel_.store(0.0f, std::memory_order_relaxed);
        outputLevel_.store(0.0f, std::memory_order_relaxed);
        latencyMonitor_.markCallbackEnd();
        return;
    }

    // Plugin crash guard: chain crashed previously silence all outputs
    if (chainCrashed_.load(std::memory_order_relaxed)) {
        clearOutputRange(0, callbackSamples);
        latencyMonitor_.markCallbackEnd();
        return;
    }

    if (numSamples <= 0) {
        clearOutputRange(0, callbackSamples);
        latencyMonitor_.markCallbackEnd();
        return;
    }

    // 1. Copy input data into the pre-allocated work buffer (no heap allocation)
    auto& buffer = workBuffer_;
    int workChannels = juce::jmin(
        juce::jmax(chMode, juce::jmax(numInputChannels, numOutputChannels)),
        buffer.getNumChannels());
    // Clear all pre-allocated channels so hidden/stale channels cannot drive
    // stereo-linked Safety Guard gain reduction.
    buffer.clear(0, numSamples);

    // Input device lost use silence instead of fallback device input.
    // Work buffer already cleared above, so just skip the copy.
    if (!inputDeviceLost_.load(std::memory_order_relaxed)) {
        if (chMode == 1) {
            // Mono mode: average only the first two enabled device inputs into
            // internal channel 0 before the VST chain. A genuine one-channel
            // device is copied directly. Limiting the fold-down to the selected
            // pair prevents unrelated active driver channels from entering the
            // signal and avoids a +3 dB boost for identical L/R input.
            int validInputChannels = 0;
            for (int ch = 0; ch < numInputChannels && validInputChannels < 2; ++ch) {
                if (inputChannelData[ch] != nullptr) {
                    if (validInputChannels == 0)
                        buffer.copyFrom(0, 0, inputChannelData[ch], numSamples);
                    else
                        buffer.addFrom(0, 0, inputChannelData[ch], numSamples);
                    ++validInputChannels;
                }
            }
            if (validInputChannels > 1)
                buffer.applyGain(0, 0, numSamples, 1.0f / static_cast<float>(validInputChannels));

            // Duplicate mono to channel 1 so both L/R outputs carry the same signal
            if (buffer.getNumChannels() > 1)
                buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
        } else {
            // Stereo mode: compact the first two enabled device channels to
            // internal L/R. JUCE backends normally supply packed active
            // pointers, but the callback contract also permits null entries
            // for disabled physical channels.
            int internalChannel = 0;
            for (int ch = 0;
                 ch < numInputChannels && internalChannel < juce::jmin(2, workChannels);
                 ++ch) {
                if (inputChannelData[ch] != nullptr) {
                    buffer.copyFrom(internalChannel, 0, inputChannelData[ch], numSamples);
                    ++internalChannel;
                }
            }
        }
    }

    // Apply input gain (SIMD-optimized inside JUCE)
    if (std::abs(gain - 1.0f) > 0.001f) {
        buffer.applyGain(gain);
    }

    // Independent input mute: silence the buffer but keep processing
    // (reverb tails fade naturally, AGC enters freeze, compressors release)
    // Different from panic mute which skips processBlock entirely.
    if (inputMuted_.load(std::memory_order_relaxed)) {
        buffer.clear();
    }

    // Measure input level (RMS) decimated: every 4th callback (~23Hz at 48kHz/512smp).
    // UI timer runs at 30Hz so per-callback RMS is wasted work.
    const bool measureThisCallback = (++rmsDecimationCounter_ & 3) == 0;
    if (measureThisCallback && buffer.getNumChannels() > 0) {
        float rms = calculateRMS(buffer.getReadPointer(0), numSamples);
        inputLevel_.store(rms, std::memory_order_relaxed);
    }

    // 2. Process through VST plugin chain (inline, zero additional latency)
    // Each plugin's bypass flag is atomic can be toggled from any thread
    //
    // Windows: __try/__except catches SEH exceptions (access violations) that
    //          try/catch(...) silently misses. The helper is extracted into a
    //          separate function because MSVC forbids __try in functions with
    //          C++ objects that have destructors on the stack.
    // Other:   try/catch(...) is the best available mechanism.
#if defined(_WIN32)
    if (!processBlockSEH(vstChain_, buffer, numSamples)) {
        buffer.clear();
        chainCrashed_.store(true, std::memory_order_relaxed);
    }
#else
    try {
        vstChain_.processBlock(buffer, numSamples);
    } catch (...) {
        buffer.clear();
        chainCrashed_.store(true, std::memory_order_relaxed);
    }
#endif

    // CRITICAL: Steps 2.1-4 MUST execute in this exact order.
    // Safety Guard (legacy SafetyLimiter) must run BEFORE all output paths (steps 2.5-4).
    // Reordering would cause un-limited audio to be recorded/broadcast/monitored.

    // 2.1. Safety Guard clip prevention for all output paths (RT-safe)
    safetyLimiter_.process(buffer, numSamples);

    // 2.2. Safety Volume: final global headroom trim for all output paths.
    const bool safetyHeadroomEnabled = safetyHeadroomEnabled_.load(std::memory_order_relaxed);
    const float safetyHeadroomGain = safetyHeadroomGain_.load(std::memory_order_relaxed);
    if (safetyHeadroomEnabled && safetyHeadroomGain < 0.9999f)
        buffer.applyGain(safetyHeadroomGain);

    // 2.5. Write processed audio to recorder (lock-free)
    recorder_.writeBlock(buffer, numSamples);

    // 2.6. Write to shared memory for Receiver VST (if IPC enabled)
    if (ipcEnabled_.load(std::memory_order_acquire)) {
        sharedMemWriter_.writeAudio(buffer, numSamples);
    }

    // 3. Route processed audio to monitor (separate shared-mode device)
    outputRouter_.routeAudio(buffer, numSamples);

    // 4. Apply output volume & copy to main output (AudioSettings Output device)
    float outVol = outputRouter_.getVolume(OutputRouter::Output::Main);
    int internalOutputChannel = 0;
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (!outputChannelData[ch]) continue;
        if (internalOutputChannel < juce::jmin(2, buffer.getNumChannels()) && !outputMuted) {
            if (std::abs(outVol - 1.0f) < 0.001f) {
                // Unity gain direct copy (most common path)
                std::memcpy(outputChannelData[ch], buffer.getReadPointer(internalOutputChannel),
                            sizeof(float) * static_cast<size_t>(numSamples));
            } else if (outVol > 0.001f) {
                // Apply gain
                const float* src = buffer.getReadPointer(internalOutputChannel);
                for (int i = 0; i < numSamples; ++i)
                    outputChannelData[ch][i] = src[i] * outVol;
            } else {
                // Volume ~0 silence
                std::memset(outputChannelData[ch], 0,
                            sizeof(float) * static_cast<size_t>(numSamples));
            }
        } else {
            std::memset(outputChannelData[ch], 0,
                        sizeof(float) * static_cast<size_t>(numSamples));
        }
        ++internalOutputChannel;
    }
    clearOutputRange(numSamples, callbackSamples - numSamples);

    // Measure output level same decimation as input
    if (measureThisCallback && buffer.getNumChannels() > 0) {
        float rms = calculateRMS(buffer.getReadPointer(0), numSamples);
        if (buffer.getNumChannels() > 1)
            rms = juce::jmax(rms, calculateRMS(buffer.getReadPointer(1), numSamples));
        outputLevel_.store(rms, std::memory_order_relaxed);
    }

    latencyMonitor_.markCallbackEnd();
}

// Device Start/Reconnection Handler
// Device start/reconnection handler notes:
// Called on the device thread (not the message thread).
// intentionalChange_: true means user-requested device change; false means auto event.
// desiredInputDevice_/desiredOutputDevice_: preserve user-selected target devices.
// ===========================================================================
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;
    const bool intentionalRestart =
        intentionalChange_.load(std::memory_order_acquire);

#if defined(_WIN32)
    if (!avSetMmThreadChar_) {
        if (auto* avrt = LoadLibraryA("avrt.dll")) {
            avSetMmThreadChar_ = reinterpret_cast<AvSetMmThreadCharFn>(
                GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
            avSetMmThreadPrio_ = reinterpret_cast<AvSetMmThreadPrioFn>(
                GetProcAddress(avrt, "AvSetMmThreadPriority"));
            avRevertMmThreadChar_ = reinterpret_cast<AvRevertMmThreadCharFn>(
                GetProcAddress(avrt, "AvRevertMmThreadCharacteristics"));
            // Guard: if either function missing (corrupted DLL), null both to allow future retry
            if (!avSetMmThreadChar_ || !avSetMmThreadPrio_) {
                avSetMmThreadChar_ = nullptr;
                avSetMmThreadPrio_ = nullptr;
                avRevertMmThreadChar_ = nullptr;
            }
        }
    }
    // New device means a new callback thread. Reset after function pointer
    // refresh so the RT thread sees a coherent registration state.
    mmcssTaskHandle_.store(nullptr, std::memory_order_relaxed);
    mmcssThreadId_.store(0, std::memory_order_relaxed);
    mmcssRegistered_.store(false, std::memory_order_release);
#endif

    // Remember last used ASIO device for future type switches
    if (!intentionalRestart
        && device->getTypeName().containsIgnoreCase("ASIO")) {
        const auto asioName = device->getName();
        auto aliveFlag = alive_;
        juce::MessageManager::callAsync([this, aliveFlag, asioName] {
            if (!aliveFlag->load())
                return;
            if (auto* current = deviceManager_.getCurrentAudioDevice()) {
                if (current->getTypeName().containsIgnoreCase("ASIO")
                    && current->getName() == asioName) {
                    lastAsioDevice_ = asioName;
                }
            }
        });
    }

    // Stop recording before device parameters change (prevents WAV corruption)
    if (recorder_.isRecording())
        recorder_.stopRecording();

    const double reportedSampleRate = device->getCurrentSampleRate();
    const int reportedBufferSize = device->getCurrentBufferSizeSamples();

    if (reportedSampleRate <= 0.0 || reportedBufferSize <= 0) {
        if (!intentionalChange_.load(std::memory_order_acquire))
            deviceLost_.store(true, std::memory_order_relaxed);
        Log::warn("AUDIO", "Device reported invalid SR=" + juce::String(reportedSampleRate)
                  + " BS=" + juce::String(reportedBufferSize)
                  + " - preserving previous runtime state and skipping prepare");
        return;
    }

    // Detect JUCE auto-fallback by comparing actual vs desired device names.
    // Two modes:
    //   - deviceLost_ already true (after audioDeviceError): keep it true, don't overwrite desired
    //   - deviceLost_ false (startup / external restart): update desired from actual
    // Intentional transactions commit desired/loss state only after their
    // message-thread setter verifies that the opened stream is usable.
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
        const bool externalRestart = intentionalRestart
            ? externalDeviceRestartPending_.load(std::memory_order_acquire)
            : externalDeviceRestartPending_.exchange(
                false, std::memory_order_acq_rel);
        const bool sameDeviceExternalRestart = wasLost
            && externalRestart
            && !intentionalRestart
            && !inputMismatch
            && !outputMismatch;

        if (!intentionalRestart
            && (inputMismatch || outputMismatch) && wasLost) {
            // Real fallback after device error keep deviceLost_ true,
            // don't overwrite desired names so checkReconnection restores them
            Log::warn("AUDIO", "Fallback detected: in='" + setup.inputDeviceName
                       + "' out='" + setup.outputDeviceName
                       + "' (desired: in='" + desiredIn
                       + "' out='" + desiredOut + "')");

            // Per-direction loss tracking:
            // Input lost silence input in audio callback (don't use fallback mic)
            inputDeviceLost_.store(inputMismatch, std::memory_order_relaxed);
            // Output lost auto-mute output (VST chain + monitor unaffected)
            if (outputMismatch)
                autoMuteOutputForDeviceLoss();

            // Restore any device that JUCE changed but is still available.
            // e.g. output unplugged JUCE fallback changes both restore input + BS/SR.
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

                juce::String restoreError;
                {
                    AtomicGuard intentionalGuard(intentionalChange_);
                    juce::AudioDeviceManager::AudioDeviceSetup s;
                    deviceManager_.getAudioDeviceSetup(s);
                    if (inAvail) s.inputDeviceName = desiredIn;
                    if (outAvail) s.outputDeviceName = desiredOut;
                    s.sampleRate = desiredSR;
                    s.bufferSize = desiredBS;
                    restoreError = deviceManager_.setAudioDeviceSetup(s, true);
                    if (restoreError.isNotEmpty()) {
                        Log::warn("AUDIO", "Fallback restore retry with driver default channels after: " + restoreError);
                        useDefaultConfiguredChannels(s, inAvail, outAvail && !outputNone_.load(std::memory_order_relaxed));
                        restoreError = deviceManager_.setAudioDeviceSetup(s, true);
                    }
                }

                if (restoreError.isNotEmpty()) {
                    Log::warn("AUDIO", "Fallback restore failed (in='" + desiredIn
                        + "' out='" + desiredOut + "' BS=" + juce::String(desiredBS)
                        + " SR=" + juce::String(desiredSR) + "): " + restoreError);
                    return;
                }

                juce::String restored;
                if (inAvail && !outAvail) restored = "Input";
                else if (!inAvail && outAvail) restored = "Output";
                else restored = "Both devices";
                Log::info("AUDIO", restored + " restored after fallback (BS="
                    + juce::String(desiredBS) + " SR=" + juce::String(desiredSR) + ")");
            });
        } else if (!intentionalRestart && sameDeviceExternalRestart) {
            Log::warn("AUDIO", "External same-device restart detected, forcing device re-open before clearing loss: in='"
                + setup.inputDeviceName + "' out='" + setup.outputDeviceName + "'");
            publishInputDeviceLoss();
            autoMuteOutputForDeviceLoss();
            scheduleSameDeviceReopenAfterExternalRestart(setup);
        } else if (!intentionalRestart) {
            // Atomics safe from any thread
            deviceLost_.store(false, std::memory_order_relaxed);
            inputDeviceLost_.store(false, std::memory_order_relaxed);
            // Auto-unmute output if it was auto-muted due to device loss
            releaseAutomaticOutputMute();
            // Non-atomic [Message thread only] variables must write on
            // message thread to avoid data race with device thread.
            auto aliveFlag = alive_;
            auto inName = setup.inputDeviceName;
            auto outName = setup.outputDeviceName;
            auto sr = reportedSampleRate;
            auto bs = reportedBufferSize;
            bool isAsio = device->getTypeName().containsIgnoreCase("ASIO");
            juce::MessageManager::callAsync([this, aliveFlag, inName, outName, sr, bs, isAsio] {
                if (!aliveFlag->load()) return;
                reconnectMissCount_ = 0;
                {
                    const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
                    if (inName.isNotEmpty())
                        desiredInputDevice_ = inName;
                    if (outName.isNotEmpty())
                        desiredOutputDevice_ = outName;
                }
                if (inName.isNotEmpty())
                    updateInputEndpointWatcherTarget(inName);
                if (isAsio) {
                    // ASIO may report a global SR owned by the driver. Keep the
                    // actual runtime values, but do not let an accidental 44.1k
                    // report overwrite an explicit 48k settings request.
                    syncRuntimeRateFromActual(sr, bs, "Device callback actual rate", true);
                } else if (!desiredSRBSSet_ && sr > 0.0 && bs > 0) {
                    // Non-ASIO first launch (no saved settings): accept device
                    // defaults. Once settings are loaded (desiredSRBSSet_=true),
                    // this branch is skipped and saved values are preserved.
                    syncRuntimeRateFromActual(sr, bs, "Device callback actual rate", false);
                }
            });
        }
    }

    currentSampleRate_ = reportedSampleRate;
    currentBufferSize_ = reportedBufferSize;

    {
        juce::AudioDeviceManager::AudioDeviceSetup activeSetup;
        deviceManager_.getAudioDeviceSetup(activeSetup);
        if (!intentionalRestart
            && markActiveChannelLossIfNeeded(
                activeSetup, device, "Device active-channel warning")) {
            scheduleActiveChannelRecovery("Device active-channel recovery");
        }
    }

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
            {
                juce::String desIn, desOut;
                {
                    const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
                    desIn = desiredInputDevice_;
                    desOut = desiredOutputDevice_;
                }
                Log::audit("AUDIO", "Desired devices: in='" + desIn + "' out='" + desOut + "'");
            }
        }
    }

    // Signal message-thread to resync xrun baseline counter (avoids data race
    // between device thread writing and message-thread timer reading).
    // NOTE: Do NOT clear recentXRuns_ or history here; device restarts (WASAPI
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
    // NOTE: chainCrashed_ is NOT reset here: device events (WASAPI session changes,
    // ASIO buffer size change) fire audioDeviceAboutToStart without any chain change,
    // which would silently re-enable a crashed chain. Instead, chainCrashed_ is cleared
    // by clearChainCrash() which is called from onChainModified (plugin add/remove/slot switch).
    safetyLimiter_.prepareToPlay(currentSampleRate_);
    outputRouter_.initialize(currentSampleRate_, currentBufferSize_);
    latencyMonitor_.reset(currentSampleRate_, currentBufferSize_,
                          device->getInputLatencyInSamples(),
                          device->getOutputLatencyInSamples());

    // Re-initialize monitor output if configured (SR may have changed).
    // Deferred to message thread to avoid blocking device startup with
    // monitor WASAPI teardown/restart (potential deadlock between device managers).
    if (monitorOutput_.getStatus() != VirtualCableStatus::NotConfigured) {
        auto devName = monitorOutput_.getDeviceName();
        double sr = currentSampleRate_;
        int bs = monitorOutput_.getPreferredBufferSize();
        auto aliveFlag = alive_;
        const auto requestGeneration = monitorConfigurationGeneration_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        juce::MessageManager::callAsync([this, aliveFlag, devName, sr, bs, requestGeneration]() {
            if (!aliveFlag->load()) return;
            if (requestGeneration
                != monitorConfigurationGeneration_.load(std::memory_order_acquire)) return;

            juce::AudioDeviceManager::AudioDeviceSetup mainSetup;
            deviceManager_.getAudioDeviceSetup(mainSetup);
            if (audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
                    getCurrentDeviceType(), mainSetup.outputDeviceName, devName)) {
                monitorOutput_.shutdown();
                const juce::String message =
                    "Monitor disabled: it matches the exclusive main output";
                Log::warn("MONITOR", message + " ('" + devName + "')");
                pushNotification(message, NotificationLevel::Warning);
                return;
            }

            monitorOutput_.initialize(devName, sr, bs);
        });
    }

    // Re-initialize IPC if it was enabled before device stopped
    if (ipcWasEnabled_) {
        uint32_t sr = static_cast<uint32_t>(currentSampleRate_);
        if (sharedMemWriter_.initialize(sr, 2, directpipe::DEFAULT_BUFFER_FRAMES)) {
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
    logDeviceSetupSnapshot("Device ready setup", device);
}

void AudioEngine::audioDeviceStopped()
{
#if defined(_WIN32)
    // AvRevertMmThreadCharacteristics must run on the same thread that called
    // AvSetMmThreadCharacteristics. If JUCE calls this from a control thread,
    // avoid a guaranteed-failing cross-thread revert.
    HANDLE h = mmcssTaskHandle_.exchange(nullptr, std::memory_order_relaxed);
    const DWORD ownerThread = mmcssThreadId_.exchange(0, std::memory_order_relaxed);
    if (h && avRevertMmThreadChar_ && ownerThread == GetCurrentThreadId())
        avRevertMmThreadChar_(h);
    mmcssRegistered_.store(false, std::memory_order_release);
#endif

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
    if (!intentionalChange_.load(std::memory_order_acquire)) {
        publishInputDeviceLoss();
        externalDeviceRestartPending_.store(true, std::memory_order_release);
        autoMuteOutputForDeviceLoss();
        inputLevel_.store(0.0f, std::memory_order_relaxed);
        outputLevel_.store(0.0f, std::memory_order_relaxed);
    }

    Log::info("AUDIO", "Device stopped");
    Log::audit("AUDIO", "Device stopped: ipcWasEnabled=" + juce::String(ipcWasEnabled_ ? "true" : "false"));
}

void AudioEngine::audioDeviceError(const juce::String& errorMessage)
{
    Log::error("AUDIO", "Device error: " + errorMessage);
    pushNotification("Device disconnected", NotificationLevel::Warning);
    publishInputDeviceLoss();
    externalDeviceRestartPending_.store(true, std::memory_order_release);
    autoMuteOutputForDeviceLoss();
    inputLevel_.store(0.0f, std::memory_order_relaxed);
    outputLevel_.store(0.0f, std::memory_order_relaxed);
    requestImmediateReconnect();
}

// Device reconnection

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    // Device list changed (plug/unplug). Retry immediately instead of waiting
    // for the 3s polling cooldown when a lost input/output target may be back.
    if (intentionalChange_.load(std::memory_order_acquire))
        return;

    if (deviceLost_.load(std::memory_order_relaxed)
        || inputDeviceLost_.load(std::memory_order_relaxed)
        || outputAutoMuted_.load(std::memory_order_relaxed))
        requestImmediateReconnect();
}

void AudioEngine::requestImmediateReconnect()
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        attemptImmediateReconnectionFromMessageThread();
        return;
    }

    auto aliveFlag = alive_;
    juce::MessageManager::callAsync([this, aliveFlag]() {
        if (!aliveFlag->load()) return;
        attemptImmediateReconnectionFromMessageThread();
    });
}

void AudioEngine::attemptImmediateReconnectionFromMessageThread()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (intentionalChange_.load(std::memory_order_acquire))
        return;

    if (sameDeviceReopenPending_.load(std::memory_order_acquire))
        return;

    if (inputDeviceLost_.load(std::memory_order_relaxed)
        || outputAutoMuted_.load(std::memory_order_relaxed))
        deviceLost_.store(true, std::memory_order_relaxed);

    if (!deviceLost_.load(std::memory_order_relaxed))
        return;

    reconnectCooldown_ = 0;
    attemptReconnection();
}

void AudioEngine::scheduleSameDeviceReopenAfterExternalRestart(
    const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    if (sameDeviceReopenPending_.exchange(true, std::memory_order_acq_rel))
        return;

    const auto reopenGeneration = sameDeviceReopenGeneration_.load(std::memory_order_acquire);
    auto aliveFlag = alive_;
    juce::MessageManager::callAsync([this, aliveFlag, setup, reopenGeneration]() {
        if (!aliveFlag->load()) {
            return;
        }

        juce::Timer::callAfterDelay(250, [this, aliveFlag, setup, reopenGeneration]() mutable {
            if (!aliveFlag->load())
                return;

            auto clearPending = [this]() {
                sameDeviceReopenPending_.store(false, std::memory_order_release);
            };

            if (reopenGeneration != sameDeviceReopenGeneration_.load(std::memory_order_acquire)) {
                // A newer lifecycle generation may already own the pending
                // flag. The stale callback must not cancel that newer guard.
                return;
            }

            if (!deviceLost_.load(std::memory_order_relaxed)
                && !inputDeviceLost_.load(std::memory_order_relaxed)
                && !outputAutoMuted_.load(std::memory_order_relaxed)) {
                clearPending();
                return;
            }

            juce::AudioDeviceManager::AudioDeviceSetup reopenSetup;
            deviceManager_.getAudioDeviceSetup(reopenSetup);
            if (reopenSetup.inputDeviceName.isEmpty() && setup.inputDeviceName.isNotEmpty())
                reopenSetup.inputDeviceName = setup.inputDeviceName;
            if (reopenSetup.outputDeviceName.isEmpty() && setup.outputDeviceName.isNotEmpty())
                reopenSetup.outputDeviceName = setup.outputDeviceName;
            if (desiredSampleRate_ > 0.0)
                reopenSetup.sampleRate = desiredSampleRate_;
            if (desiredBufferSize_ > 0)
                reopenSetup.bufferSize = desiredBufferSize_;

            juce::String result;
            {
                AtomicGuard intentionalGuard(intentionalChange_);
                endpointEventSuppressedUntilMs_ =
                    juce::Time::getMillisecondCounterHiRes()
                    + audio_device_recovery_detail::kEndpointEventReopenSuppressionMs;
                Log::info("AUDIO", "Same-device restart is closing and recreating the current audio device");
                result = audio_device_recovery_detail::forceReopenAudioDevice(
                    deviceManager_, reopenSetup);
                if (result.isNotEmpty()) {
                    Log::warn("AUDIO", "Same-device restart re-open retry with driver default channels after: " + result);
                    useDefaultConfiguredChannels(reopenSetup,
                                                  reopenSetup.inputDeviceName.isNotEmpty(),
                                                  reopenSetup.outputDeviceName.isNotEmpty()
                                                      && !outputNone_.load(std::memory_order_relaxed));
                    result = audio_device_recovery_detail::forceReopenAudioDevice(
                        deviceManager_, reopenSetup);
                }
                endpointEventSuppressedUntilMs_ =
                    juce::Time::getMillisecondCounterHiRes()
                    + audio_device_recovery_detail::kEndpointEventReopenSuppressionMs;
            }

            if (result.isNotEmpty()) {
                Log::warn("AUDIO", "Same-device restart re-open failed: " + result);
                deviceLost_.store(true, std::memory_order_relaxed);
                reconnectCooldown_ = 0;
                clearPending();
                requestImmediateReconnect();
                return;
            }

            auto* device = deviceManager_.getCurrentAudioDevice();
            juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
            deviceManager_.getAudioDeviceSetup(appliedSetup);
            const bool ready = device
                && device->getCurrentSampleRate() > 0.0
                && device->getCurrentBufferSizeSamples() > 0
                && hasUsableActiveChannels(appliedSetup, device);

            if (!ready) {
                Log::warn("AUDIO", "Same-device restart re-open did not produce a ready input/output stream");
                markActiveChannelLossIfNeeded(appliedSetup, device, "Same-device restart invalid setup");
                deviceLost_.store(true, std::memory_order_relaxed);
                reconnectCooldown_ = 0;
                clearPending();
                requestImmediateReconnect();
                return;
            }

            if (!clearDeviceLossAfterReady(appliedSetup)) {
                reconnectCooldown_ = 0;
                clearPending();
                requestImmediateReconnect();
                return;
            }

            reconnectCooldown_ = 0;
            reconnectMissCount_ = 0;
            clearPending();
            Log::info("AUDIO", "Same-device restart re-open ready: in='"
                + appliedSetup.inputDeviceName + "' out='" + appliedSetup.outputDeviceName + "'");
            if (onDeviceReconnected) onDeviceReconnected();
        });
    });
}

// Dual-Mechanism Reconnection
// 1) Immediate ChangeListener callback on deviceManager_ (fast detection).
// 2) 3s timer polling fallback (covers missed ChangeListener events).
// reconnectMissCount_: after repeated misses, accept current device to avoid loop
// unless startup restore or output loss requires waiting for an explicit target.
// WARNING: checkReconnection() is message-thread only. Access desired device
// targets only while holding desiredDeviceLock_ because device callbacks also read them.
// ===========================================================================
void AudioEngine::checkReconnection()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Directional flags are authoritative. Re-arm the aggregate gate if a
    // concurrent output-only clear ever races with an input/output loss event.
    if (inputDeviceLost_.load(std::memory_order_acquire)
        || outputAutoMuted_.load(std::memory_order_acquire)) {
        deviceLost_.store(true, std::memory_order_release);
    }

    // Main device reconnection
    if (deviceLost_.load(std::memory_order_relaxed)) {
        // Endpoint property/state changes intentionally wait for the delayed
        // same-device reopen. Do not let the 30 Hz generic reconnect path
        // bypass that settle window and reopen the device immediately.
        if (sameDeviceReopenPending_.load(std::memory_order_acquire)) {
            // Keep processing notifications and monitor recovery below.
        } else if (reconnectCooldown_ > 0) {
            --reconnectCooldown_;
        } else {
            reconnectCooldown_ = kReconnectCooldownTicks;  // ~3 seconds at 30Hz
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

    if (isMonitorLost && monitorOutput_.getStatus() == VirtualCableStatus::Active && !monitorOutput_.isDeviceLost())
        pushNotification("Monitor reconnected", NotificationLevel::Info);

    // Monitor sample rate mismatch notification (one-shot, resets when mismatch clears)
    if (monitorOutput_.getStatus() == VirtualCableStatus::SampleRateMismatch && !monitorSRMismatchNotified_) {
        monitorSRMismatchNotified_ = true;
        pushNotification("Monitor: sample rate mismatch - monitor disabled", NotificationLevel::Warning);
    }
    if (monitorOutput_.getStatus() != VirtualCableStatus::SampleRateMismatch)
        monitorSRMismatchNotified_ = false;

    // Chain crash notification (moved off RT thread detected here on message thread)
    if (chainCrashed_.load(std::memory_order_relaxed) && !chainCrashNotified_.load(std::memory_order_relaxed)) {
        chainCrashNotified_.store(true, std::memory_order_relaxed);
        pushNotification("Plugin crash detected \xe2\x80\x94 chain bypassed. Remove the problematic plugin.", NotificationLevel::Error);
    }
    if (!chainCrashed_.load(std::memory_order_relaxed))
        chainCrashNotified_.store(false, std::memory_order_relaxed);
}

void AudioEngine::attemptReconnection()
{
    if (!deviceLost_.load(std::memory_order_relaxed)) return;
    if (attemptingReconnection_) return;  // Re-entrancy guard
    BoolGuard reconnectGuard(attemptingReconnection_);

    juce::String desiredType;
    juce::String desiredIn;
    juce::String desiredOut;
    {
        const juce::SpinLock::ScopedLockType sl(desiredDeviceLock_);
        desiredType = desiredDeviceType_;
        desiredIn = desiredInputDevice_;
        desiredOut = desiredOutputDevice_;
    }

    Log::info("AUDIO", "Reconnect attempt #" + juce::String(reconnectMissCount_ + 1)
        + " - desired type='" + desiredType + "' in='" + desiredIn + "' out='" + desiredOut + "'");

    const auto currentType = getCurrentDeviceType();
    if (desiredType.isNotEmpty()
        && currentType != desiredType
        && desiredType.containsIgnoreCase("ASIO")) {
        auto* desiredTypeObj = findDeviceType(deviceManager_, desiredType);
        if (!desiredTypeObj) {
            Log::warn("AUDIO", "Reconnection: desired driver type not available: " + desiredType);
            reconnectMissCount_ = 0;
            return;
        }

        desiredTypeObj->scanForDevices();
        const auto asioDevices = desiredTypeObj->getDeviceNames(false);
        const auto preferredAsioDevice = selectAsioDuplexDevice(desiredIn, desiredOut);
        Log::audit("AUDIO", "Reconnection ASIO scan: devices=["
            + asioDevices.joinIntoString(", ") + "] preferred='" + preferredAsioDevice + "'");

        if (preferredAsioDevice.isEmpty() || !asioDevices.contains(preferredAsioDevice)) {
            ++reconnectMissCount_;
            if (reconnectMissCount_ >= kMaxReconnectMisses)
                reconnectMissCount_ = 0;
            Log::info("AUDIO", "Reconnection: waiting for ASIO device '" + preferredAsioDevice + "'");
            return;
        }

        auto result = setAudioDeviceType(desiredType, preferredAsioDevice);
        if (!result) {
            deviceLost_.store(true, std::memory_order_relaxed);
            // checkReconnection() armed the normal ~3s cooldown before this
            // attempt. Preserve it on failure so an unavailable/busy ASIO
            // driver is not reopened again on every 30 Hz timer tick.
            Log::warn("AUDIO", "Reconnection ASIO switch failed: " + result.message);
            return;
        }

        reconnectMissCount_ = 0;
        reconnectCooldown_ = 0;
        if (onDeviceReconnected) onDeviceReconnected();
        return;
    }

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
    Log::audit("AUDIO", "Reconnection desired: in='" + desiredIn + "' out='" + desiredOut + "'");

    // Check if our desired devices are available
    bool inputOk = desiredIn.isEmpty() || inputs.contains(desiredIn);
    bool outputOk = outputNone_.load(std::memory_order_relaxed)
        || desiredOut.isEmpty() || outputs.contains(desiredOut);

    if (!inputOk || !outputOk) {
        ++reconnectMissCount_;
        if (reconnectMissCount_ >= kMaxReconnectMisses) {
            // Saved startup targets and per-direction loss are explicit user choices:
            // keep waiting instead of accepting the wrong fallback device.
            const bool explicitDeviceTargetPending = startupRestorePending_
                || inputDeviceLost_.load(std::memory_order_relaxed)
                || outputAutoMuted_.load(std::memory_order_relaxed);
            if (explicitDeviceTargetPending) {
                reconnectMissCount_ = 0;
                Log::audit("AUDIO", "Reconnection: explicit device target not ready, continuing to wait for in='"
                    + desiredIn + "' out='" + desiredOut + "'");
            } else {
                // Stale cross-driver names accept current driver's devices to break the loop
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
                startupRestorePending_ = false;
                reconnectMissCount_ = 0;
                pushNotification("Device not found, using current device", NotificationLevel::Warning);
                if (onDeviceReconnected) onDeviceReconnected();
            }
        } else {
            Log::info("AUDIO", "Reconnection: waiting for devices"
                + juce::String(!inputOk ? " (input: " + desiredIn + ")" : "")
                + juce::String(!outputOk ? " (output: " + desiredOut + ")" : ""));
        }
        return;  // BoolGuard resets attemptingReconnection_ on scope exit
    }
    reconnectMissCount_ = 0;

    // Re-apply previous setup with desired device names, SR, and BS.
    // Preserve channel routing (important for ASIO channel pairs).
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    if (desiredIn.isNotEmpty())
        setup.inputDeviceName = desiredIn;
    if (!outputNone_.load(std::memory_order_relaxed) && desiredOut.isNotEmpty())
        setup.outputDeviceName = desiredOut;
    setup.sampleRate = desiredSampleRate_;
    setup.bufferSize = desiredBufferSize_;

    auto setupToApply = setup;
    juce::String result;
    {
        AtomicGuard intentionalGuard(intentionalChange_);
        result = deviceManager_.setAudioDeviceSetup(setupToApply, true);
        if (result.isNotEmpty()) {
            Log::warn("AUDIO", "Reconnection retry with driver default channels after: " + result);
            useDefaultConfiguredChannels(setupToApply, true, !outputNone_.load(std::memory_order_relaxed));
            result = deviceManager_.setAudioDeviceSetup(setupToApply, true);
        }
    }
    if (result.isEmpty()) {
        auto* device = deviceManager_.getCurrentAudioDevice();
        juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
        deviceManager_.getAudioDeviceSetup(appliedSetup);
        bool readyAfterReconnect = device
            && device->getCurrentSampleRate() > 0.0
            && device->getCurrentBufferSizeSamples() > 0
            && hasUsableActiveChannels(appliedSetup, device);

        if (!readyAfterReconnect && device
            && device->getCurrentSampleRate() > 0.0
            && device->getCurrentBufferSizeSamples() > 0) {
            markActiveChannelLossIfNeeded(appliedSetup, device, "Reconnection active-channel invalid");
            readyAfterReconnect = recoverActiveChannelsWithDriverDefaults("Reconnection active-channel recovery");
            device = deviceManager_.getCurrentAudioDevice();
            deviceManager_.getAudioDeviceSetup(appliedSetup);
        }

        if (!readyAfterReconnect) {
            Log::error("AUDIO", "Reconnection opened an invalid or zero-active device");
            logDeviceSetupSnapshot("Reconnection invalid setup", device);
            deviceLost_.store(true, std::memory_order_relaxed);
            reconnectCooldown_ =
                audio_device_recovery_detail::reconnectCooldownAfterRecovery(false);
            return;
        }

        if (!clearDeviceLossAfterReady(appliedSetup)) {
            reconnectCooldown_ =
                audio_device_recovery_detail::reconnectCooldownAfterRecovery(false);
            reconnectMissCount_ = 0;
            return;
        }
        reconnectCooldown_ = 0;
        reconnectMissCount_ = 0;
        Log::info("AUDIO", "Device reconnected: " + setupToApply.inputDeviceName + " / " + setupToApply.outputDeviceName);
        Log::audit("AUDIO", "Reconnection success: SR=" + juce::String(setupToApply.sampleRate) + " BS=" + juce::String(setupToApply.bufferSize));
        pushNotification("Device reconnected", NotificationLevel::Info);
        if (onDeviceReconnected) onDeviceReconnected();
    } else {
        Log::error("AUDIO", "Reconnection failed (in='" + setup.inputDeviceName + "' out='" + setup.outputDeviceName + "'): " + result);
    }
    // BoolGuard resets attemptingReconnection_ on scope exit
}

// Notification queue

// NOTE: notifQueue_[slot] assignment copies a juce::String (heap allocation).
// This is acceptable because pushNotification is only called from the device
// error thread (audioDeviceError) and the message thread, never from the RT
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
        // CAS failed another producer advanced w, retry with updated w
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
