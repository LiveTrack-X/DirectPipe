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
 * @brief Monitor (headphone) output via separate WASAPI device.
 *
 * Routes processed audio to a second WASAPI output device (e.g., headphones)
 * for real-time monitoring. Uses a lock-free ring buffer to bridge the main
 * audio callback and the monitor device's independent WASAPI callback thread.
 */
#pragma once

#include <JuceHeader.h>
#include "AudioRingBuffer.h"
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
 * @brief Monitor output via separate WASAPI device (e.g., headphones).
 *
 * Owns a separate AudioDeviceManager with its own callback thread.
 * The main audio callback writes to a lock-free ring buffer (producer),
 * and this class's callback reads from it (consumer) and outputs to WASAPI.
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
    int getActualBufferSize() const { return actualBufferSize_.load(std::memory_order_relaxed); }
    double getActualSampleRate() const { return actualSampleRate_.load(std::memory_order_relaxed); }

    /** @brief Check and attempt monitor device reconnection (call from message thread timer). */
    void checkReconnection();  // [Message thread only]
    /** @brief True if the monitor device was lost (error/disconnect). */
    bool isDeviceLost() const { return monitorLost_.load(std::memory_order_relaxed); }

    // --- Setup guide ---
    static juce::String getSetupGuideMessage();

private:
    // juce::AudioIODeviceCallback — monitor device's WASAPI callback
    // [Monitor RT thread — SEPARATE from main AudioEngine RT thread]
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

    // ═══════════════════════════════════════════════════════════════════
    // Thread Ownership — 변경 시 Audio/README.md "Thread Model" 테이블도 업데이트할 것
    // ═══════════════════════════════════════════════════════════════════

    AudioRingBuffer ringBuffer_;                          // [Main RT write, Monitor RT read — lock-free]
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);  // [callAsync lifetime guard]
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
    int reconnectCooldown_ = 0;                           // [Message thread only] Ticks before next attempt

    // Drift monitoring: counts consecutive low-fill / high-fill callbacks for diagnostics
    std::atomic<int> underrunCount_{0};                   // [Monitor RT write, Message read]
};

} // namespace directpipe
