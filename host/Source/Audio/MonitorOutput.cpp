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
 * @brief Monitor (headphone) output implementation via a separate shared-mode device.
 */

#include "MonitorOutput.h"
#include "../Control/Log.h"
#include "../Platform/PlatformAudio.h"
#include "MonitorDriftPolicy.h"
#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace directpipe {

namespace {
constexpr int kMonitorRingBufferFrames = 8192;
constexpr int kDriftWarmupCallbacks = 50;
#if defined(_WIN32)
constexpr int kMonitorMmcssPriority = 0; // AVRT_PRIORITY_NORMAL; monitor must not preempt main OUT.
#endif
}

MonitorOutput::MonitorOutput() = default;

MonitorOutput::~MonitorOutput()
{
    alive_->store(false);
    shutdown();
}

bool MonitorOutput::initialize(const juce::String& deviceName,
                                   double sampleRate, int bufferSize)
{
    shutdown();

    deviceName_ = deviceName;
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    // Initialize ring buffer: power-of-two stereo buffer, bounded by adaptive PLL + emergency trim.
    ringBuffer_.initialize(kMonitorRingBufferFrames, 2);
    capacityFrames_.store(kMonitorRingBufferFrames, std::memory_order_relaxed);
    callbacksSinceStart_.store(0, std::memory_order_relaxed);
    producerBlockSize_.store(0, std::memory_order_relaxed);
    consumerBlockSize_.store(0, std::memory_order_relaxed);
    currentFillFrames_.store(0, std::memory_order_relaxed);
    targetFillFrames_.store(0, std::memory_order_relaxed);
    targetReasonCode_.store(static_cast<int>(monitor_drift::TargetReason::RuntimeMinimum), std::memory_order_relaxed);
    playbackRatio_.store(1.0, std::memory_order_relaxed);
    pllErrorFrames_.store(0.0, std::memory_order_relaxed);
    pllErrorMs_.store(0.0, std::memory_order_relaxed);
    pllCorrection_.store(0.0, std::memory_order_relaxed);
    driftEstimate_.store(0.0, std::memory_order_relaxed);
    priming_.store(true, std::memory_order_relaxed);
    fractionalReadPhase_ = 0.0;
    adaptiveTargetState_ = {};
    monitor_drift::resetPll(pllState_);

    deviceManager_ = std::make_unique<juce::AudioDeviceManager>();

    // Force shared-mode device type (WASAPI on Windows, CoreAudio on macOS, ALSA on Linux).
    deviceManager_->setCurrentAudioDeviceType(PlatformAudio::getDefaultSharedDeviceType(), true);

    auto result = deviceManager_->initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty()) {
        Log::error("MONITOR", "Init error (device='" + deviceName + "' SR=" + juce::String(sampleRate) + " BS=" + juce::String(bufferSize) + "): " + result);
        monitorLost_.store(true, std::memory_order_relaxed);
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
        Log::error("MONITOR", "Setup error (device='" + deviceName + "' SR=" + juce::String(sampleRate) + " BS=" + juce::String(bufferSize) + "): " + result);
        monitorLost_.store(true, std::memory_order_relaxed);
        status_.store(VirtualCableStatus::Error, std::memory_order_relaxed);
        return false;
    }

    // Register as the audio callback for this device
    deviceManager_->addAudioCallback(this);

    Log::info("MONITOR", "Initialized on " + deviceName + " (SR=" + juce::String(sampleRate) + " BS=" + juce::String(bufferSize) + ")");
    Log::audit("MONITOR", "Ring buffer: " + juce::String(kMonitorRingBufferFrames) + " frames, 2 channels");
    return true;
}

void MonitorOutput::shutdown()
{
    // Set status BEFORE teardown so producer (writeAudio) stops writing to ring buffer
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_release);
    actualSampleRate_.store(0.0, std::memory_order_relaxed);
    actualBufferSize_.store(0, std::memory_order_relaxed);
    callbacksSinceStart_.store(0, std::memory_order_relaxed);
    producerBlockSize_.store(0, std::memory_order_relaxed);
    consumerBlockSize_.store(0, std::memory_order_relaxed);
    currentFillFrames_.store(0, std::memory_order_relaxed);
    targetFillFrames_.store(0, std::memory_order_relaxed);
    targetReasonCode_.store(static_cast<int>(monitor_drift::TargetReason::RuntimeMinimum), std::memory_order_relaxed);
    playbackRatio_.store(1.0, std::memory_order_relaxed);
    pllErrorFrames_.store(0.0, std::memory_order_relaxed);
    pllErrorMs_.store(0.0, std::memory_order_relaxed);
    pllCorrection_.store(0.0, std::memory_order_relaxed);
    driftEstimate_.store(0.0, std::memory_order_relaxed);
    priming_.store(true, std::memory_order_relaxed);
    if (deviceManager_) {
        intentionalTeardown_.store(true, std::memory_order_release);
        deviceManager_->removeAudioCallback(this);
        deviceManager_->closeAudioDevice();
        deviceManager_.reset();
        intentionalTeardown_.store(false, std::memory_order_release);
    }
#if defined(_WIN32)
    const auto ownerThread = mmcssThreadId_.exchange(0, std::memory_order_relaxed);
    if (auto h = mmcssTaskHandle_.exchange(nullptr, std::memory_order_relaxed))
        if (avRevertMmThreadChar_ && ownerThread == GetCurrentThreadId())
            avRevertMmThreadChar_(h);
    mmcssRegistered_.store(false, std::memory_order_release);
#endif
    ringBuffer_.reset();
    fractionalReadPhase_ = 0.0;
    adaptiveTargetState_ = {};
    monitor_drift::resetPll(pllState_);
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

// RT-safe: called from main audio callback thread.

int MonitorOutput::writeAudio(const float* const* channelData,
                                  int numChannels, int numFrames)
{
    if (status_.load(std::memory_order_acquire) != VirtualCableStatus::Active)
        return 0;

    producerBlockSize_.store(numFrames, std::memory_order_relaxed);
    int written = ringBuffer_.write(channelData, numChannels, numFrames);
    if (written < numFrames)
        droppedFrames_.fetch_add(numFrames - written, std::memory_order_relaxed);

    return written;
}

// Monitor device shared-mode callback (consumer).

void MonitorOutput::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int /*numInputChannels*/,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    if (numSamples <= 0 || numOutputChannels <= 0 || outputChannelData == nullptr)
        return;

#if defined(_WIN32)
    if (!mmcssRegistered_.load(std::memory_order_acquire)) {
        if (avSetMmThreadChar_ && avSetMmThreadPrio_) {
            unsigned long taskIndex = 0;
            auto h = avSetMmThreadChar_(L"Pro Audio", &taskIndex);
            if (h) {
                mmcssTaskHandle_.store(h, std::memory_order_relaxed);
                mmcssThreadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);
                avSetMmThreadPrio_(h, kMonitorMmcssPriority);
            }
        }
        mmcssRegistered_.store(true, std::memory_order_release);
    }
#endif

    auto clearOutputs = [&] {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                std::memset(outputChannelData[ch], 0,
                            static_cast<size_t>(numSamples) * sizeof(float));
    };

    // Guard: if not Active, output silence without touching ring buffer.
    // Prevents data race when reset() is called from another thread.
    if (status_.load(std::memory_order_acquire) != VirtualCableStatus::Active) {
        clearOutputs();
        return;
    }

    const int callbacks = callbacksSinceStart_.fetch_add(1, std::memory_order_relaxed) + 1;
    int available = ringBuffer_.availableRead();
    int trimmedThisCallback = 0;
    const int consumerBlock = (std::max)(numSamples, actualBufferSize_.load(std::memory_order_relaxed));
    const int producerBlock = producerBlockSize_.load(std::memory_order_relaxed);
    const double sr = actualSampleRate_.load(std::memory_order_relaxed) > 0.0
        ? actualSampleRate_.load(std::memory_order_relaxed)
        : sampleRate_;

    consumerBlockSize_.store(consumerBlock, std::memory_order_relaxed);

    auto plan = monitor_drift::calculateBufferPlan(
        ringBuffer_.getCapacityFrames(),
        producerBlock,
        consumerBlock,
        sr,
        adaptiveTargetState_);

    if (adaptiveTargetState_.lastProducerBlock != plan.producerBlock
        || adaptiveTargetState_.lastConsumerBlock != plan.consumerBlock) {
        adaptiveTargetState_.lastProducerBlock = plan.producerBlock;
        adaptiveTargetState_.lastConsumerBlock = plan.consumerBlock;
        adaptiveTargetState_.stableCallbacks = 0;
        fractionalReadPhase_ = 0.0;
        monitor_drift::resetPll(pllState_);
        priming_.store(true, std::memory_order_relaxed);
    }

    auto publishSnapshot = [&](int fillFrames,
                               const monitor_drift::BufferPlan& snapshotPlan,
                               const monitor_drift::PllUpdate& pll) {
        currentFillFrames_.store(fillFrames, std::memory_order_relaxed);
        targetFillFrames_.store(snapshotPlan.targetFill, std::memory_order_relaxed);
        targetReasonCode_.store(static_cast<int>(snapshotPlan.reason), std::memory_order_relaxed);
        playbackRatio_.store(pll.playbackRatio, std::memory_order_relaxed);
        pllErrorFrames_.store(pll.errorFrames, std::memory_order_relaxed);
        pllErrorMs_.store(pll.errorMs, std::memory_order_relaxed);
        pllCorrection_.store(pll.correction, std::memory_order_relaxed);
        driftEstimate_.store(pll.driftEstimate, std::memory_order_relaxed);
    };

    auto neutralPll = [&](int fillFrames, const monitor_drift::BufferPlan& snapshotPlan) {
        const double errorFrames = static_cast<double>(fillFrames - snapshotPlan.targetFill);
        return monitor_drift::PllUpdate {
            1.0,
            0.0,
            errorFrames,
            (errorFrames / monitor_drift::positiveOrFallback(sr, 48000.0)) * 1000.0,
            pllState_.driftEstimateFramesPerCallback,
            monitor_drift::ratioDeltaFromCents(monitor_drift::kNormalPllPitchToleranceCents)
        };
    };

    // Do not play partial blocks. With small monitor WASAPI buffers, partial
    // read + zero-fill creates an audible discontinuity; hold silence briefly
    // until the ring is safely primed, then resume with continuous blocks.
    if (priming_.load(std::memory_order_relaxed)) {
        if (available < plan.targetFill) {
            publishSnapshot(available, plan, neutralPll(available, plan));
            clearOutputs();
            return;
        }
        fractionalReadPhase_ = 0.0;
        monitor_drift::resetPll(pllState_);
        priming_.store(false, std::memory_order_relaxed);
    }

    if (callbacks > kDriftWarmupCallbacks && available > plan.emergencyThreshold) {
        const auto trimPlan = monitor_drift::calculateEmergencyTrimPlan(available, plan);
        if (trimPlan.trimFrames > 0) {
            const int trimmed = ringBuffer_.advanceRead(trimPlan.trimFrames);
            if (trimmed > 0) {
                trimmedThisCallback = trimmed;
                available -= trimmed;
                fractionalReadPhase_ = 0.0;
                monitor_drift::noteNearOverflow(adaptiveTargetState_);
                latencyTrimmedFrames_.fetch_add(trimmed, std::memory_order_relaxed);
            }
        }
    }

    plan = monitor_drift::calculateBufferPlan(
        ringBuffer_.getCapacityFrames(),
        producerBlock,
        consumerBlock,
        sr,
        adaptiveTargetState_);

    const int errorFrames = available - plan.targetFill;
    const bool emergencyCorrection = available > plan.highThreshold;
    auto pll = monitor_drift::updatePll(pllState_,
                                        static_cast<double>(errorFrames),
                                        plan.targetFill,
                                        consumerBlock,
                                        sr,
                                        emergencyCorrection);

    const int requiredFrames = AudioRingBuffer::requiredFramesForInterpolatedRead(
        numSamples,
        fractionalReadPhase_,
        pll.playbackRatio);

    if (available < requiredFrames) {
        underrunCount_.fetch_add(1, std::memory_order_relaxed);
        monitor_drift::noteUnderrun(adaptiveTargetState_, plan);
        auto raisedPlan = monitor_drift::calculateBufferPlan(
            ringBuffer_.getCapacityFrames(),
            producerBlock,
            consumerBlock,
            sr,
            adaptiveTargetState_);
        priming_.store(true, std::memory_order_relaxed);
        fractionalReadPhase_ = 0.0;
        monitor_drift::resetPll(pllState_);
        publishSnapshot(available, raisedPlan, neutralPll(available, raisedPlan));
        clearOutputs();
        return;
    }

    int framesConsumed = 0;
    const int read = ringBuffer_.readInterpolated(outputChannelData,
                                                  numOutputChannels,
                                                  numSamples,
                                                  pll.playbackRatio,
                                                  fractionalReadPhase_,
                                                  framesConsumed);

    if (read < numSamples) {
        underrunCount_.fetch_add(1, std::memory_order_relaxed);
        monitor_drift::noteUnderrun(adaptiveTargetState_, plan);
        priming_.store(true, std::memory_order_relaxed);
        fractionalReadPhase_ = 0.0;
        monitor_drift::resetPll(pllState_);
        publishSnapshot(available, plan, neutralPll(available, plan));
        clearOutputs();
        return;
    }

    // Emergency trims can create a discontinuity at the read boundary.
    if (trimmedThisCallback > 0) {
        const int fadeSamples = (std::min)(read, 16);
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] == nullptr)
                continue;
            for (int i = 0; i < fadeSamples; ++i) {
                const float gain = static_cast<float>(i + 1) / static_cast<float>(fadeSamples);
                outputChannelData[ch][i] *= gain;
            }
        }
    }

    monitor_drift::noteStableCallback(adaptiveTargetState_, plan, errorFrames);
    publishSnapshot((std::max)(0, available - framesConsumed), plan, pll);
}

void MonitorOutput::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

#if defined(_WIN32)
    // New monitor device means a new callback thread, so re-register MMCSS there.
    if (!avSetMmThreadChar_) {
        if (auto* avrt = LoadLibraryA("avrt.dll")) {
            avSetMmThreadChar_ = reinterpret_cast<AvSetMmThreadCharFn>(
                GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
            avSetMmThreadPrio_ = reinterpret_cast<AvSetMmThreadPrioFn>(
                GetProcAddress(avrt, "AvSetMmThreadPriority"));
            avRevertMmThreadChar_ = reinterpret_cast<AvRevertMmThreadCharFn>(
                GetProcAddress(avrt, "AvRevertMmThreadCharacteristics"));
            if (!avSetMmThreadChar_ || !avSetMmThreadPrio_) {
                avSetMmThreadChar_ = nullptr;
                avSetMmThreadPrio_ = nullptr;
                avRevertMmThreadChar_ = nullptr;
            }
        }
    }
    mmcssTaskHandle_.store(nullptr, std::memory_order_relaxed);
    mmcssThreadId_.store(0, std::memory_order_relaxed);
    mmcssRegistered_.store(false, std::memory_order_release);
#endif

    // Detect JUCE auto-fallback: device changed without our initialize() call.
    // Keep monitorLost_ true so checkReconnection() keeps trying the desired device.
    bool isFallback = device->getName() != deviceName_;
    if (!isFallback)
        monitorLost_.store(false, std::memory_order_relaxed);

    double deviceSR = device->getCurrentSampleRate();
    int deviceBS = device->getCurrentBufferSizeSamples();

    actualSampleRate_.store(deviceSR, std::memory_order_relaxed);
    actualBufferSize_.store(deviceBS, std::memory_order_relaxed);

    // Check sample rate match
    if (std::abs(deviceSR - sampleRate_) > 1.0) {
        Log::warn("MONITOR", "Sample rate mismatch! Expected " + juce::String(sampleRate_) + " got " + juce::String(deviceSR));
        // Set status BEFORE reset so the consumer callback sees non-Active
        // and skips ring buffer access (prevents data race on reset)
        status_.store(VirtualCableStatus::SampleRateMismatch, std::memory_order_release);
        // This is a stable configuration mismatch, not a retryable hotplug loss.
        // Leave the monitor disabled until the main SR or selected monitor changes.
        monitorLost_.store(false, std::memory_order_relaxed);
        ringBuffer_.reset();
        return;
    }

    if (isFallback) {
        // Do not use the fallback device; shut down and wait for reconnection.
        status_.store(VirtualCableStatus::Error, std::memory_order_release);
        monitorLost_.store(true, std::memory_order_relaxed);
        Log::warn("MONITOR", "Fallback to " + device->getName()
                   + " rejected (desired: " + deviceName_ + ") - shutting down, waiting for reconnection");
        auto aliveFlag = alive_;
        juce::MessageManager::callAsync([this, aliveFlag] {
            if (!aliveFlag->load()) return;
            if (!deviceManager_) return;
            deviceManager_->removeAudioCallback(this);
            deviceManager_->closeAudioDevice();
        });
        return;
    }

    // Set non-Active before reset to prevent consumer from reading during reset
    status_.store(VirtualCableStatus::NotConfigured, std::memory_order_release);
    ringBuffer_.reset();
    callbacksSinceStart_.store(0, std::memory_order_relaxed);
    producerBlockSize_.store(0, std::memory_order_relaxed);
    consumerBlockSize_.store(0, std::memory_order_relaxed);
    currentFillFrames_.store(0, std::memory_order_relaxed);
    targetFillFrames_.store(0, std::memory_order_relaxed);
    targetReasonCode_.store(static_cast<int>(monitor_drift::TargetReason::RuntimeMinimum), std::memory_order_relaxed);
    playbackRatio_.store(1.0, std::memory_order_relaxed);
    pllErrorFrames_.store(0.0, std::memory_order_relaxed);
    pllErrorMs_.store(0.0, std::memory_order_relaxed);
    pllCorrection_.store(0.0, std::memory_order_relaxed);
    driftEstimate_.store(0.0, std::memory_order_relaxed);
    priming_.store(true, std::memory_order_relaxed);
    fractionalReadPhase_ = 0.0;
    adaptiveTargetState_ = {};
    monitor_drift::resetPll(pllState_);
    status_.store(VirtualCableStatus::Active, std::memory_order_release);

    Log::info("MONITOR", "Active on " + device->getName() + " @ " + juce::String(deviceSR) + "Hz / " + juce::String(deviceBS) + " samples");
    if (Log::isAuditMode()) {
        Log::audit("MONITOR", "Device type: " + device->getTypeName());
        Log::audit("MONITOR", "Output channels: " + device->getOutputChannelNames().joinIntoString(", "));
        Log::audit("MONITOR", "Input latency: " + juce::String(device->getInputLatencyInSamples()) + " Output latency: " + juce::String(device->getOutputLatencyInSamples()));
        auto bsSizes = device->getAvailableBufferSizes();
        juce::String bsList;
        for (int b : bsSizes) bsList += (bsList.isEmpty() ? "" : ", ") + juce::String(b);
        Log::audit("MONITOR", "Available BS: " + bsList);
    }
}

void MonitorOutput::audioDeviceStopped()
{
#if defined(_WIN32)
    const auto ownerThread = mmcssThreadId_.exchange(0, std::memory_order_relaxed);
    if (auto h = mmcssTaskHandle_.exchange(nullptr, std::memory_order_relaxed))
        if (avRevertMmThreadChar_ && ownerThread == GetCurrentThreadId())
            avRevertMmThreadChar_(h);
    mmcssRegistered_.store(false, std::memory_order_release);
#endif

    if (intentionalTeardown_.load(std::memory_order_acquire))
        return;

    // shutdown() removes callback BEFORE closeAudioDevice(), so this only
    // fires on external events (device unplug, driver error), not our own teardown.
    monitorLost_.store(true, std::memory_order_relaxed);
    status_.store(VirtualCableStatus::Error, std::memory_order_release);
    Log::warn("MONITOR", "Device stopped (lost): " + deviceName_);
}

void MonitorOutput::audioDeviceError(const juce::String& errorMessage)
{
    Log::error("MONITOR", "Device error on '" + deviceName_ + "': " + errorMessage);
    monitorLost_.store(true, std::memory_order_relaxed);
    status_.store(VirtualCableStatus::Error, std::memory_order_release);
}

// Device enumeration.

juce::String MonitorOutput::getActualDeviceName() const
{
    if (deviceManager_)
        if (auto* device = deviceManager_->getCurrentAudioDevice())
            return device->getName();
    return {};
}

void MonitorOutput::scanDevices()
{
    if (deviceManager_) {
        if (auto* type = deviceManager_->getCurrentDeviceTypeObject())
            type->scanForDevices();
    }
}

void MonitorOutput::checkReconnection()
{
    if (!isDeviceLost()) return;
    if (deviceName_.isEmpty()) return;

    if (reconnectCooldown_ > 0) {
        --reconnectCooldown_;
        return;
    }
    reconnectCooldown_ = 90;  // ~3 seconds at 30Hz

    Log::info("MONITOR", "Reconnection attempt: " + deviceName_);

    scanDevices();
    auto devices = getAvailableOutputDevices();
    Log::audit("MONITOR", "Available devices: [" + devices.joinIntoString(", ") + "]");

    if (!devices.contains(deviceName_)) {
        Log::info("MONITOR", "Device '" + deviceName_ + "' not yet available");
        return;
    }

    if (initialize(deviceName_, sampleRate_, bufferSize_)
        && status_.load(std::memory_order_relaxed) == VirtualCableStatus::Active
        && !isDeviceLost()) {
        // monitorLost_ cleared in audioDeviceAboutToStart
        reconnectCooldown_ = 0;
        Log::info("MONITOR", "Device reconnected: " + deviceName_);
    } else if (status_.load(std::memory_order_relaxed) == VirtualCableStatus::SampleRateMismatch) {
        Log::warn("MONITOR", "Reconnection paused: sample rate mismatch (device='" + deviceName_
            + "' expected SR=" + juce::String(sampleRate_)
            + " actual SR=" + juce::String(actualSampleRate_.load(std::memory_order_relaxed)) + ")");
    } else {
        Log::error("MONITOR", "Reconnection failed: initialize returned false (device='" + deviceName_ + "' SR=" + juce::String(sampleRate_) + " BS=" + juce::String(bufferSize_) + ")");
    }
}

juce::StringArray MonitorOutput::getAvailableOutputDevices() const
{
    juce::StringArray devices;

    // Use our own device manager if available, otherwise create temp
    if (deviceManager_) {
        if (auto* type = deviceManager_->getCurrentDeviceTypeObject())
            devices = type->getDeviceNames(false);
    } else {
        juce::AudioDeviceManager temp;
        temp.setCurrentAudioDeviceType(PlatformAudio::getDefaultSharedDeviceType(), true);
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
