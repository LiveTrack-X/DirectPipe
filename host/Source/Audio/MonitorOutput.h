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
 * @file MonitorOutput.h
 * @brief Monitor (headphone) output via a separate shared-mode output device.
 *
 * Routes processed audio to a second shared-mode output device (e.g.,
 * headphones) for real-time monitoring. Uses a lock-free ring buffer to bridge
 * the main audio callback and the monitor device's independent callback thread.
 */
#pragma once

#include <JuceHeader.h>
#include "AudioRingBuffer.h"
#include "MonitorDriftPolicy.h"
#include <atomic>
#include <memory>

namespace directpipe {

enum class VirtualCableStatus {
    NotConfigured,  ///< No device selected
    Active,         ///< Audio flowing to monitor device
    Error,          ///< Device open failed
    SampleRateMismatch  ///< Device opened but SR doesn't match main device
};

/**
 * @brief Monitor output via a separate shared-mode output device.
 *
 * Owns a separate AudioDeviceManager with its own callback thread.
 * The main audio callback writes to a lock-free ring buffer (producer),
 * and this class's callback reads from it (consumer) and outputs to the
 * selected WASAPI/CoreAudio/ALSA device.
 *
 * NOTE: This runs on a SEPARATE RT thread from the main AudioEngine.
 * The monitor device has its own independent WASAPI/CoreAudio/ALSA callback.
 * Cross-thread communication uses the lock-free AudioRingBuffer only.
 */
class MonitorOutput : public juce::AudioIODeviceCallback {
public:
    MonitorOutput();
    ~MonitorOutput() override;

    // --- Configuration (call from message thread) ---
    bool initialize(const juce::String& deviceName, double sampleRate, int bufferSize);  // [Message thread only]
    void shutdown();      // [Message thread only]
    bool setDevice(const juce::String& deviceName);   // [Message thread only]
    bool setBufferSize(int bufferSize);                // [Message thread only]
    int getPreferredBufferSize() const { return bufferSize_; }

    // --- RT-safe audio push (called from MAIN audio callback) ---
    int writeAudio(const float* const* channelData, int numChannels, int numFrames);  // [Main RT thread — lock-free ring buffer write]

    // --- Device enumeration ---
    void scanDevices();
    juce::StringArray getAvailableOutputDevices() const;
    juce::Array<int> getAvailableBufferSizes() const;

    // --- Status queries ---
    VirtualCableStatus getStatus() const { return status_.load(std::memory_order_relaxed); }
    juce::String getDeviceName() const { return deviceName_; }
    /** @brief Returns the actual device currently in use (may differ from desired on fallback). Message thread only. */
    juce::String getActualDeviceName() const;
    bool isActive() const { return status_.load(std::memory_order_relaxed) == VirtualCableStatus::Active; }
    int getDroppedFrames() const { return droppedFrames_.load(std::memory_order_relaxed); }
    int getUnderrunCount() const { return underrunCount_.load(std::memory_order_relaxed); }
    int getLatencyTrimmedFrames() const { return latencyTrimmedFrames_.load(std::memory_order_relaxed); }
    int getFillFrames() const { return currentFillFrames_.load(std::memory_order_relaxed); }
    int getTargetFillFrames() const { return targetFillFrames_.load(std::memory_order_relaxed); }
    int getTargetReasonCode() const { return targetReasonCode_.load(std::memory_order_relaxed); }
    double getPlaybackRatio() const { return playbackRatio_.load(std::memory_order_relaxed); }
    double getPllErrorFrames() const { return pllErrorFrames_.load(std::memory_order_relaxed); }
    double getPllErrorMs() const { return pllErrorMs_.load(std::memory_order_relaxed); }
    double getPllCorrection() const { return pllCorrection_.load(std::memory_order_relaxed); }
    double getDriftEstimate() const { return driftEstimate_.load(std::memory_order_relaxed); }
    bool isPriming() const { return priming_.load(std::memory_order_relaxed); }
    int getProducerBlockSize() const { return producerBlockSize_.load(std::memory_order_relaxed); }
    int getConsumerBlockSize() const { return consumerBlockSize_.load(std::memory_order_relaxed); }
    int getCapacityFrames() const { return capacityFrames_.load(std::memory_order_relaxed); }
    int getActualBufferSize() const { return actualBufferSize_.load(std::memory_order_relaxed); }
    double getActualSampleRate() const { return actualSampleRate_.load(std::memory_order_relaxed); }

    /** @brief Check and attempt monitor device reconnection (call from message thread timer). */
    void checkReconnection();  // [Message thread only]
    /**
     * @brief True if the monitor device has a retryable loss/error.
     *
     * Sample-rate mismatch is a stable configuration problem, not a hotplug loss;
     * retrying it from the 30Hz UI timer can starve the message thread.
     */
    bool isDeviceLost() const
    {
        return monitorLost_.load(std::memory_order_relaxed)
            && status_.load(std::memory_order_relaxed) != VirtualCableStatus::SampleRateMismatch;
    }

    // --- Setup guide ---
    static juce::String getSetupGuideMessage();

private:
    // juce::AudioIODeviceCallback — monitor device's shared-mode callback
    // [Monitor RT thread — SEPARATE from main AudioEngine RT thread]
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;
    bool hasUsableOutputChannels(juce::AudioIODevice* device) const;
    bool recoverActiveOutputChannelsWithDriverDefaults(const juce::String& reason);
    void scheduleActiveOutputChannelRecovery(const juce::String& reason);
    void closeProducerAdmissionAndWait() noexcept;
    void openProducerAdmission() noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // Thread Ownership — 변경 시 Audio/README.md "Thread Model" 테이블도 업데이트할 것
    // ═══════════════════════════════════════════════════════════════════

    AudioRingBuffer ringBuffer_;                          // [Main RT write, Monitor RT read — lock-free]
    std::atomic<bool> producerWriteAdmission_{false};     // [Lifecycle write, Main RT read] Closes before reset/resize
    std::atomic<uint32_t> inFlightProducerWrites_{0};     // [Main RT inc/dec, Lifecycle wait] Protects ring storage lifetime
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    using TestWriteBarrier = void (*)(void* context);
    TestWriteBarrier testWriteBarrier_ = nullptr;
    void* testWriteBarrierContext_ = nullptr;
#endif
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);  // [callAsync lifetime guard]
    std::atomic<uint64_t> lifecycleGeneration_{0};       // [Lifecycle/Device write, Message read] Invalidates stale callAsync work
    std::unique_ptr<juce::AudioDeviceManager> deviceManager_;  // [Message thread only]

    juce::String deviceName_;                             // [Message thread only]
    double sampleRate_ = 48000.0;                         // [Message thread only]
    int bufferSize_ = 128;                                // [Message thread only] Low default for minimal latency

    std::atomic<VirtualCableStatus> status_{VirtualCableStatus::NotConfigured};  // [Monitor RT/Message write, Any read]
    std::atomic<int> droppedFrames_{0};                   // [Monitor RT write, Message read]
    std::atomic<int> actualBufferSize_{0};                // [Monitor RT write, Message read]
    std::atomic<double> actualSampleRate_{0.0};           // [Monitor RT write, Message read]

    // Device reconnection tracking
    std::atomic<bool> monitorLost_{false};                // [Monitor RT/Device write, Message read]
    std::atomic<bool> intentionalTeardown_{false};        // [Message write, Device callback read]
    std::atomic<bool> activeOutputRecoveryPending_{false}; // [Monitor RT/Message write, Message read] Guards zero-active reopen retry
    int reconnectCooldown_ = 0;                           // [Message thread only] Ticks before next attempt

    // Drift monitoring: keeps monitor latency bounded when independent device clocks diverge.
    std::atomic<int> underrunCount_{0};                   // [Monitor RT write, Message read]
    std::atomic<int> latencyTrimmedFrames_{0};             // [Monitor RT write, Message read]
    std::atomic<int> callbacksSinceStart_{0};              // [Monitor RT write, Message read]
    std::atomic<int> producerBlockSize_{0};                // [Main RT write, Monitor RT read]
    std::atomic<int> consumerBlockSize_{0};                // [Monitor RT write, Message read]
    std::atomic<int> capacityFrames_{0};                   // [Message init, Any read]
    std::atomic<int> currentFillFrames_{0};                // [Monitor RT write, Message read]
    std::atomic<int> targetFillFrames_{0};                 // [Monitor RT write, Message read]
    std::atomic<int> targetReasonCode_{0};                 // [Monitor RT write, Message read]
    std::atomic<double> playbackRatio_{1.0};               // [Monitor RT write, Message read]
    std::atomic<double> pllErrorFrames_{0.0};              // [Monitor RT write, Message read]
    std::atomic<double> pllErrorMs_{0.0};                  // [Monitor RT write, Message read]
    std::atomic<double> pllCorrection_{0.0};               // [Monitor RT write, Message read]
    std::atomic<double> driftEstimate_{0.0};               // [Monitor RT write, Message read]
    std::atomic<bool> priming_{true};                       // [Message reset, Monitor RT read/write] hold playback until ring has safe fill
    double fractionalReadPhase_ = 0.0;                     // [Monitor RT only]
    monitor_drift::AdaptiveTargetState adaptiveTargetState_; // [Monitor RT, reset while inactive]
    monitor_drift::PllState pllState_;                       // [Monitor RT, reset while inactive]

#if defined(_WIN32)
    using MmcssTaskHandle = void*;
    using AvSetMmThreadCharFn = MmcssTaskHandle(__stdcall*)(const wchar_t*, unsigned long*);
    using AvSetMmThreadPrioFn = int(__stdcall*)(MmcssTaskHandle, int);
    using AvRevertMmThreadCharFn = int(__stdcall*)(MmcssTaskHandle);
    AvSetMmThreadCharFn avSetMmThreadChar_ = nullptr;      // [Device thread write-once, Monitor RT read]
    AvSetMmThreadPrioFn avSetMmThreadPrio_ = nullptr;      // [Device thread write-once, Monitor RT read]
    AvRevertMmThreadCharFn avRevertMmThreadChar_ = nullptr; // [Device callback write-once, Device callback read]
    std::atomic<bool> mmcssRegistered_{false};              // [Device thread reset, Monitor RT write+read]
    std::atomic<MmcssTaskHandle> mmcssTaskHandle_{nullptr}; // [Monitor RT write, Device callback read]
    std::atomic<unsigned long> mmcssThreadId_{0};            // [Monitor RT write, Device callback read] Creator thread for same-thread revert
#endif
};

} // namespace directpipe
