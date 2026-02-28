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
 * @file AudioEngine.h
 * @brief Core audio engine — WASAPI input → VST chain → output routing
 *
 * Manages the audio device, VST plugin processing chain, and output
 * distribution to virtual cable and local monitor.
 */
#pragma once

#include <JuceHeader.h>
#include "VSTChain.h"
#include "OutputRouter.h"
#include "LatencyMonitor.h"
#include "VirtualMicOutput.h"
#include "AudioRecorder.h"

#include <atomic>
#include <functional>
#include <memory>

#include "../UI/NotificationBar.h"

namespace directpipe {

struct PendingNotification {
    juce::String message;
    NotificationLevel level = NotificationLevel::Error;
};

/**
 * @brief Main audio processing engine.
 *
 * Coordinates:
 * 1. WASAPI Shared mode input from USB microphone (non-exclusive)
 * 2. VST plugin chain processing with atomic bypass flags
 * 3. Output routing to monitor (headphones via separate WASAPI device)
 * 4. Mono/Stereo channel mode selection
 */
class AudioEngine : public juce::AudioIODeviceCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    bool initialize();
    void shutdown();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager_; }
    VSTChain& getVSTChain() { return vstChain_; }
    OutputRouter& getOutputRouter() { return outputRouter_; }
    LatencyMonitor& getLatencyMonitor() { return latencyMonitor_; }
    AudioRecorder& getRecorder() { return recorder_; }
    // Device type (ASIO / Windows Audio)
    bool setAudioDeviceType(const juce::String& typeName);
    juce::String getCurrentDeviceType() const;
    juce::StringArray getAvailableDeviceTypes();

    // Device selection
    bool setInputDevice(const juce::String& deviceName);
    bool setOutputDevice(const juce::String& deviceName);
    // Dynamic capabilities (depends on current device type and device)
    juce::Array<double> getAvailableSampleRates() const;
    juce::Array<int> getAvailableBufferSizes() const;

    void setBufferSize(int bufferSize);
    void setSampleRate(double sampleRate);

    /** @brief Show the ASIO control panel (only works when ASIO device is active). */
    bool showAsioControlPanel();

    // Channel names (useful for ASIO channel routing)
    juce::StringArray getInputChannelNames() const;
    juce::StringArray getOutputChannelNames() const;

    /** @brief Set which input channels are active (stereo pair starting at firstChannel). */
    bool setActiveInputChannels(int firstChannel, int numChannels = 2);
    /** @brief Set which output channels are active (stereo pair starting at firstChannel). */
    bool setActiveOutputChannels(int firstChannel, int numChannels = 2);

    /** @brief Get the first active input channel index. */
    int getActiveInputChannelOffset() const;
    /** @brief Get the first active output channel index. */
    int getActiveOutputChannelOffset() const;

    void setMonitorEnabled(bool enabled) { outputRouter_.setEnabled(OutputRouter::Output::Monitor, enabled); }
    bool isMonitorEnabled() const { return outputRouter_.isEnabled(OutputRouter::Output::Monitor); }

    /** Set the monitor output WASAPI device (independent of main driver). */
    bool setMonitorDevice(const juce::String& deviceName);
    juce::String getMonitorDeviceName() const { return monitorOutput_.getDeviceName(); }
    VirtualMicOutput& getMonitorOutput() { return monitorOutput_; }

    void setChannelMode(int channels);
    int getChannelMode() const { return channelMode_.load(std::memory_order_relaxed); }

    void setInputGain(float gain) { inputGain_.store(gain, std::memory_order_relaxed); }
    float getInputGain() const { return inputGain_.load(std::memory_order_relaxed); }

    void setMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }
    bool isMuted() const { return muted_.load(std::memory_order_relaxed); }

    void setOutputMuted(bool muted) { outputMuted_.store(muted, std::memory_order_relaxed); }
    bool isOutputMuted() const { return outputMuted_.load(std::memory_order_relaxed); }

    float getInputLevel() const { return inputLevel_.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputLevel_.load(std::memory_order_relaxed); }

    juce::StringArray getAvailableInputDevices() const;
    juce::StringArray getAvailableOutputDevices() const;

    /** @brief Get Windows Audio (WASAPI) output devices regardless of current driver type. */
    juce::StringArray getWasapiOutputDevices();

    bool isRunning() const { return running_; }

    std::function<void(float)> onInputLevelChanged;
    std::function<void(float)> onOutputLevelChanged;
    std::function<void(const juce::String&)> onDeviceError;

    /** Drain pending notifications (call from message thread timer). */
    bool popNotification(PendingNotification& out);

private:
    void pushNotification(const juce::String& msg, NotificationLevel level);
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

    static float calculateRMS(const float* data, int numSamples);

    juce::AudioDeviceManager deviceManager_;
    VSTChain vstChain_;
    OutputRouter outputRouter_;
    LatencyMonitor latencyMonitor_;
    VirtualMicOutput monitorOutput_;
    AudioRecorder recorder_;

    bool running_ = false;

    std::atomic<float> inputLevel_{0.0f};
    std::atomic<float> outputLevel_{0.0f};
    std::atomic<float> inputGain_{1.0f};
    std::atomic<int> channelMode_{2};
    std::atomic<bool> muted_{false};
    std::atomic<bool> outputMuted_{false};

    double currentSampleRate_ = 48000.0;
    int currentBufferSize_ = 480;

    juce::AudioBuffer<float> workBuffer_;

    // Lock-free notification queue (device thread -> message thread)
    static constexpr int kNotifQueueSize = 8;
    PendingNotification notifQueue_[kNotifQueueSize];
    std::atomic<int> notifWriteIdx_{0};
    std::atomic<int> notifReadIdx_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace directpipe
