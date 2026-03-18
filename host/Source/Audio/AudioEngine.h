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
 * @brief Core audio engine — audio input → VST chain → output routing
 *
 * Manages the audio device, VST plugin processing chain, and output
 * distribution to main output, monitor (headphones), IPC, and recorder.
 */
#pragma once

#include <JuceHeader.h>
#include "../ActionResult.h"
#include "DeviceState.h"
#include "VSTChain.h"
#include "OutputRouter.h"
#include "LatencyMonitor.h"
#include "MonitorOutput.h"
#include "AudioRecorder.h"
#include "SafetyLimiter.h"
#include "../IPC/SharedMemWriter.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>

#include "../UI/NotificationBar.h"

namespace directpipe {

struct PendingNotification {
    juce::String message;
    NotificationLevel level = NotificationLevel::Error;
};

/** @brief Snapshot of audio settings for a specific driver type (for restore on type switch). */
struct DriverTypeSnapshot {
    juce::String inputDevice;
    juce::String outputDevice;
    double sampleRate = 0;
    int bufferSize = 0;
    bool outputNone = false;  // "None" output selection state
};

/**
 * @brief Main audio processing engine.
 *
 * Coordinates:
 * 1. Audio input (WASAPI/ASIO on Windows, CoreAudio on macOS, ALSA/JACK on Linux)
 * 2. VST plugin chain processing with atomic bypass flags
 * 3. Output routing to monitor (headphones via separate shared-mode device)
 * 4. Mono/Stereo channel mode selection
 */
class AudioEngine : public juce::AudioIODeviceCallback,
                     public juce::ChangeListener {
public:
    AudioEngine();
    ~AudioEngine() override;

    [[nodiscard]] bool initialize();
    void shutdown();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager_; }
    VSTChain& getVSTChain() { return vstChain_; }
    OutputRouter& getOutputRouter() { return outputRouter_; }
    LatencyMonitor& getLatencyMonitor() { return latencyMonitor_; }
    AudioRecorder& getRecorder() { return recorder_; }
    SafetyLimiter& getSafetyLimiter() { return safetyLimiter_; }
    // Device type (ASIO / Windows Audio)
    [[nodiscard]] ActionResult setAudioDeviceType(const juce::String& typeName, const juce::String& preferredAsioDevice = {});
    juce::String getCurrentDeviceType() const;
    juce::StringArray getAvailableDeviceTypes();

    // Device selection
    [[nodiscard]] ActionResult setInputDevice(const juce::String& deviceName);   // [Message thread only]
    [[nodiscard]] ActionResult setOutputDevice(const juce::String& deviceName);  // [Message thread only]
    [[nodiscard]] ActionResult setAsioDevice(const juce::String& deviceName);    // [Message thread only]

    /** @brief Get the desired device type (survives fallback, unlike getCurrentDeviceType). */
    juce::String getDesiredDeviceType() const { return desiredDeviceType_.isEmpty() ? getCurrentDeviceType() : desiredDeviceType_; }
    /** @brief Get the desired input/output device names (survive fallback). */
    juce::String getDesiredInputDevice() const { return desiredInputDevice_; }
    juce::String getDesiredOutputDevice() const { return desiredOutputDevice_; }
    /** @brief Get the desired SR/BS (survives driver fallback). */
    double getDesiredSampleRate() const { return desiredSampleRate_; }
    int getDesiredBufferSize() const { return desiredBufferSize_; }
    // Dynamic capabilities (depends on current device type and device)
    juce::Array<double> getAvailableSampleRates() const;
    juce::Array<int> getAvailableBufferSizes() const;

    [[nodiscard]] ActionResult setBufferSize(int bufferSize);    // [Message thread only]
    [[nodiscard]] ActionResult setSampleRate(double sampleRate); // [Message thread only]

    /** @brief Pre-set SR/BS before device type switch (avoids intermediate restarts). */
    void presetAudioParams(double sampleRate, int bufferSize);

    /** @brief Show the ASIO control panel (only works when ASIO device is active). */
    bool showAsioControlPanel();

    // Channel names (useful for ASIO channel routing)
    juce::StringArray getInputChannelNames() const;
    juce::StringArray getOutputChannelNames() const;

    /** @brief Set which input channels are active (stereo pair starting at firstChannel). */
    [[nodiscard]] ActionResult setActiveInputChannels(int firstChannel, int numChannels = 2);
    /** @brief Set which output channels are active (stereo pair starting at firstChannel). */
    [[nodiscard]] ActionResult setActiveOutputChannels(int firstChannel, int numChannels = 2);

    /** @brief Get the first active input channel index. */
    int getActiveInputChannelOffset() const;
    /** @brief Get the first active output channel index. */
    int getActiveOutputChannelOffset() const;

    void setMonitorEnabled(bool enabled) { outputRouter_.setEnabled(OutputRouter::Output::Monitor, enabled); }
    bool isMonitorEnabled() const { return outputRouter_.isEnabled(OutputRouter::Output::Monitor); }

    /** Set the monitor output WASAPI device (independent of main driver). */
    [[nodiscard]] ActionResult setMonitorDevice(const juce::String& deviceName);
    juce::String getMonitorDeviceName() const { return monitorOutput_.getDeviceName(); }
    MonitorOutput& getMonitorOutput() { return monitorOutput_; }

    /** Set/get the monitor output buffer size (independent of main device). */
    [[nodiscard]] ActionResult setMonitorBufferSize(int bufferSize);
    int getMonitorBufferSize() const { return monitorOutput_.getPreferredBufferSize(); }

    void setChannelMode(int channels);
    int getChannelMode() const { return channelMode_.load(std::memory_order_relaxed); }

    void setInputGain(float gain) { inputGain_.store(gain, std::memory_order_relaxed); }
    float getInputGain() const { return inputGain_.load(std::memory_order_relaxed); }

    void setMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }
    bool isMuted() const { return muted_.load(std::memory_order_relaxed); }

    void setOutputMuted(bool muted) { outputMuted_.store(muted, std::memory_order_relaxed); }
    bool isOutputMuted() const { return outputMuted_.load(std::memory_order_relaxed); }

    /** Output "None" mode — user selected no output device (persists across restart). */
    void setOutputNone(bool none) { outputNone_.store(none, std::memory_order_relaxed); setOutputMuted(none); }
    bool isOutputNone() const { return outputNone_.load(std::memory_order_relaxed); }

    void setIpcEnabled(bool enabled);
    bool isIpcEnabled() const { return ipcEnabled_.load(std::memory_order_relaxed); }

    /** @brief Block IPC from being enabled (audio-only multi-instance mode). */
    void setIpcAllowed(bool allowed) { ipcAllowed_ = allowed; }

    float getInputLevel() const { return inputLevel_.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputLevel_.load(std::memory_order_relaxed); }

    juce::StringArray getAvailableInputDevices() const;
    juce::StringArray getAvailableOutputDevices() const;

    /** @brief Get shared-mode output devices regardless of current driver type.
     *  On Windows returns WASAPI devices, on macOS CoreAudio, on Linux ALSA. */
    juce::StringArray getSharedModeOutputDevices();

    bool isRunning() const { return running_; }

    /** @brief Get the number of xruns in the last 60 seconds. Returns -1 if unsupported. */
    int getRecentXRunCount() const;
    /** @brief Call from UI timer (~30Hz) to update the rolling xrun window. */
    void updateXRunTracking();
    /** @brief Request xrun counter reset (safe from any thread — sets atomic flag). */
    void requestXRunReset() { xrunResetRequested_.store(true, std::memory_order_release); }

    /** @brief Check and attempt device reconnection (call from message thread timer). */
    void checkReconnection();  // [Message thread only]
    /** @brief True if the audio device was lost (error/disconnect). */
    bool isDeviceLost() const { return deviceLost_.load(std::memory_order_relaxed); }

    /** @brief True if the input device was lost (audio callback zeroes input). */
    bool isInputDeviceLost() const { return inputDeviceLost_.load(std::memory_order_relaxed); }
    /** @brief True if output was auto-muted due to device loss. */
    bool isOutputAutoMuted() const { return outputAutoMuted_.load(std::memory_order_relaxed); }
    /** @brief Clear auto-mute tracking (call when user manually controls output mute). */
    void clearOutputAutoMute() { outputAutoMuted_.store(false, std::memory_order_relaxed); }

    /**
     * @brief Get the current device state as an explicit enum.
     *
     * Computes DeviceState from the underlying atomic flags.
     * Use this for switch-based state handling instead of checking
     * individual boolean flags (compiler warns on missing cases).
     *
     * [Message thread only] — reads attemptingReconnection_ (non-atomic).
     */
    DeviceState getDeviceState() const
    {
        if (attemptingReconnection_)
            return DeviceState::Reconnecting;
        bool devLost = deviceLost_.load(std::memory_order_relaxed);
        bool inLost = inputDeviceLost_.load(std::memory_order_relaxed);
        bool outMuted = outputAutoMuted_.load(std::memory_order_relaxed);
        if (devLost)
            return DeviceState::BothLost;
        if (inLost && outMuted)
            return DeviceState::BothLost;
        if (inLost)
            return DeviceState::InputLost;
        if (outMuted)
            return DeviceState::OutputLost;
        return DeviceState::Running;
    }

    std::function<void(float)> onInputLevelChanged;
    std::function<void(float)> onOutputLevelChanged;
    std::function<void(const juce::String&)> onDeviceError;
    std::function<void()> onDeviceReconnected;

    /** @brief Clear the chain-crash flag (call after chain structure change: plugin add/remove/slot switch). */
    void clearChainCrash() { chainCrashed_.store(false, std::memory_order_relaxed); }

    /** Drain pending notifications (call from message thread timer). */
    bool popNotification(PendingNotification& out);

private:
    void pushNotification(const juce::String& msg, NotificationLevel level);
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void attemptReconnection();
    // [RT thread only]
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

    // ═══════════════════════════════════════════════════════════════════
    // Thread Ownership — 변경 시 Audio/README.md "Thread Model" 테이블도 업데이트할 것
    // ═══════════════════════════════════════════════════════════════════

    // ─── Subsystems (owned, thread-safe internally) ───
    juce::AudioDeviceManager deviceManager_;
    VSTChain vstChain_;
    OutputRouter outputRouter_;
    LatencyMonitor latencyMonitor_;
    MonitorOutput monitorOutput_;
    AudioRecorder recorder_;
    SafetyLimiter safetyLimiter_;
    SharedMemWriter sharedMemWriter_;

    // ─── Cross-thread atomics ───
    std::atomic<bool> ipcEnabled_{false};              // [Message write, RT read]
    std::atomic<bool> ipcWasEnabled_{false};            // [Device thread only] Remembers IPC state across device stop/start
    bool ipcAllowed_ = true;                            // [Message thread only] false in audio-only multi-instance mode

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);  // [callAsync lifetime guard]
    std::atomic<bool> running_{false};                  // [Message write, RT read]

    std::atomic<float> inputLevel_{0.0f};               // [RT write, Message read]
    std::atomic<float> outputLevel_{0.0f};              // [RT write, Message read]
    std::atomic<float> inputGain_{1.0f};                // [Message write, RT read]
    std::atomic<int> channelMode_{2};                   // [Message write, RT read]
    std::atomic<bool> muted_{false};                    // [Message write, RT read]
    std::atomic<bool> outputMuted_{false};              // [Message write, RT read]
    std::atomic<bool> outputNone_{false};               // [Message write, RT read] "None" output device (persists)

    std::atomic<double> currentSampleRate_{48000.0};    // [Message write, RT read]
    std::atomic<int> currentBufferSize_{480};            // [Message write, RT read]

    // ─── XRun tracking (Message thread only, except atomics) ───
    // Two separate flags avoid a data race between device thread and message thread:
    //   xrunBaselineResync_: device restart → resync lastDeviceXRunCount_ only (preserve history)
    //   xrunResetRequested_: user action   → full clear (history + display)
    std::atomic<int> recentXRuns_{0};                   // [Message write, UI read]
    std::atomic<bool> xrunBaselineResync_{false};       // [Device thread write, Message read]
    std::atomic<bool> xrunResetRequested_{false};       // [Any thread write, Message read]
    int lastDeviceXRunCount_ = 0;                       // [Message thread only]
    int xrunHistory_[60] = {};                          // [Message thread only]
    int xrunHistoryIdx_ = 0;                            // [Message thread only]
    double xrunAccumulatorTime_ = 0.0;                  // [Message thread only]

    // ─── Device reconnection tracking (Message thread only, except atomics) ───
    juce::SpinLock desiredDeviceLock_;                   // Protects desiredInputDevice_ / desiredOutputDevice_
    juce::String desiredInputDevice_;                   // [Protected by desiredDeviceLock_]
    juce::String desiredOutputDevice_;                  // [Protected by desiredDeviceLock_]
    juce::String desiredDeviceType_;                    // [Message thread only] Tracks intended driver type across fallbacks
    juce::String lastAsioDevice_;                       // [Message thread only] Remembers last used ASIO device for type switches
    std::map<juce::String, DriverTypeSnapshot> driverSnapshots_;  // [Message thread only] Per-driver-type settings for restore on switch
    bool desiredSRBSSet_ = false;                       // [Message thread only] true after user/settings explicitly set SR/BS
    double desiredSampleRate_ = 48000.0;                // [Message thread only]
    int desiredBufferSize_ = 480;                       // [Message thread only]
    std::atomic<bool> deviceLost_{false};               // [RT/Device write, Message read]
    std::atomic<bool> inputDeviceLost_{false};          // [RT/Device write, Message read] Input specifically lost → zero input in audio callback
    std::atomic<bool> outputAutoMuted_{false};          // [RT/Device write, Message read] Output auto-muted due to device loss
    bool attemptingReconnection_ = false;               // [Message thread only] Re-entrancy guard
    std::atomic<bool> intentionalChange_{false};        // [Message write, Device thread read] Guards audioDeviceStopped from setting deviceLost_ during intentional changes
    int reconnectCooldown_ = 0;                         // [Message thread only] Ticks before next reconnect attempt (30Hz timer)
    int reconnectMissCount_ = 0;                        // [Message thread only] Consecutive failed reconnect attempts
    static constexpr int kMaxReconnectMisses = 5;       // ~15s at 3s intervals
    bool monitorWasLost_ = false;                       // [Message thread only] Edge detection for monitor disconnect notification
    bool monitorSRMismatchNotified_ = false;             // [Message thread only] One-shot SR mismatch notification
    bool inputWasLost_ = false;                         // [Message thread only] Edge detection for input device loss notification
    bool outputWasAutoMuted_ = false;                   // [Message thread only] Edge detection for output auto-mute notification

    // ─── RT thread only ───
    juce::AudioBuffer<float> workBuffer_;               // [RT thread only]
    uint32_t rmsDecimationCounter_ = 0;                 // [RT thread only] RMS computed every 4th callback (no atomic needed)
    std::atomic<bool> chainCrashed_{false};              // [RT write, Message read] Plugin processBlock exception → silence output
    std::atomic<bool> mmcssRegistered_{false};           // [Device thread reset, RT thread write+read] MMCSS registration flag (Windows)

#if defined(_WIN32)
    // Cached MMCSS function pointers — loaded once in audioDeviceAboutToStart (device thread),
    // read from RT callback. Safe: written once before any RT read, never modified after.
    using AvSetMmThreadCharFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    using AvSetMmThreadPrioFn = BOOL(WINAPI*)(HANDLE, int);
    using AvRevertMmThreadCharFn = BOOL(WINAPI*)(HANDLE);
    AvSetMmThreadCharFn avSetMmThreadChar_ = nullptr;   // [Device thread write-once, RT thread read]
    AvSetMmThreadPrioFn avSetMmThreadPrio_ = nullptr;   // [Device thread write-once, RT thread read]
    AvRevertMmThreadCharFn avRevertMmThreadChar_ = nullptr; // [Device thread write-once, device thread read]
    std::atomic<HANDLE> mmcssTaskHandle_{nullptr};        // [RT thread write, device thread read]
#endif

    // ─── Lock-free notification queue (RT write → Message read) ───
    static constexpr int kNotifQueueSize = 8;
    PendingNotification notifQueue_[kNotifQueueSize];
    std::atomic<bool> notifReady_[kNotifQueueSize]{};   // [RT write, Message read] per-slot ready flag
    std::atomic<uint32_t> notifWriteIdx_{0};            // [RT write]
    std::atomic<uint32_t> notifReadIdx_{0};             // [Message read]

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace directpipe
