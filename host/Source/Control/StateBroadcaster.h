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
 * @file StateBroadcaster.h
 * @brief Broadcasts application state changes to all connected clients
 *
 * When any state changes (bypass toggled, volume changed, etc.),
 * the broadcaster pushes the updated state to:
 * - GUI (repaint)
 * - WebSocket clients (Stream Deck)
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <array>

namespace directpipe {

/// Complete application state snapshot
struct AppState {
    struct PluginState {
        std::string name;
        bool bypassed = false;
        bool loaded = false;
        int latencySamples = 0;
        std::string type;  // "vst", "builtin_filter", "builtin_noise_removal", "builtin_auto_gain"
    };

    std::vector<PluginState> plugins;
    float inputGain = 1.0f;
    float monitorVolume = 1.0f;
    bool masterBypassed = false;
    bool muted = false;       // Panic mute state (output-path emergency lock)
    bool outputMuted = false; // User mute state for main output path
    bool inputMuted = false;  // Independent input-only mute (chain/output paths keep running)
    std::string currentPreset;
    float latencyMs = 0.0f;
    float monitorLatencyMs = 0.0f;
    float inputLevelDb = -60.0f;
    float cpuPercent = 0.0f;
    double sampleRate = 48000.0;
    int bufferSize = 480;
    int channelMode = 1;  // 1 = Mono, 2 = Stereo
    bool monitorEnabled = false;
    int activeSlot = 0;   // Quick preset slot index: 0=A, 1=B, 2=C, 3=D, 4=E, 5=Auto, -1=none
    bool autoSlotActive = false;  // Deprecated — auto-derived from activeSlot==5. Kept for backward compat.
    bool recording = false;
    double recordingSeconds = 0.0;
    bool ipcEnabled = false;
    bool deviceLost = false;
    bool monitorLost = false;
    float outputVolume = 1.0f;  // Main output volume (0.0-1.0)
    int xrunCount = 0;

    // Safety Guard / Safety Volume (legacy safety_limiter JSON key)
    bool limiterEnabled = true;
    float limiterCeilingdB = -0.3f;
    bool safetyHeadroomEnabled = true;
    float safetyHeadroomdB = -0.3f;
    float limiterGainReduction = 0.0f;
    bool limiterActive = false;

    int chainPDCSamples = 0;
    float chainPDCMs = 0.0f;

    std::array<std::string, 6> slotNames{};  // A-E (0-4) + Auto (5)
};

/// Listener for state changes
class StateListener {
public:
    virtual ~StateListener() = default;
    virtual void onStateChanged(const AppState& state) = 0;
};

/**
 * @brief Manages and broadcasts application state.
 *
 * Thread-safe: state can be updated from any thread.
 * Listeners are always notified on the JUCE message thread.
 * If updateState() is called from the message thread, notification is synchronous.
 * If called from another thread, notification is deferred via callAsync.
 */
class StateBroadcaster {
public:
    StateBroadcaster() = default;
    ~StateBroadcaster() { alive_->store(false); }

    /**
     * @brief Get the current state snapshot.
     */
    AppState getState() const;

    /**
     * @brief Update the state and notify all listeners.
     * @param updater Function that modifies the state.
     */
    void updateState(std::function<void(AppState&)> updater);  // [Any thread → listener notification on Message thread]

    /**
     * @brief Register a state change listener.
     */
    void addListener(StateListener* listener);

    /**
     * @brief Remove a state change listener.
     */
    void removeListener(StateListener* listener);

    /**
     * @brief Serialize current state to JSON string.
     */
    std::string toJSON() const;

private:
    void notifyListeners();
    void notifyOnMessageThread();

    // ═══════════════════════════════════════════════════════════════════
    // Thread Ownership — 변경 시 Control/README.md "Thread Model" 테이블도 업데이트할 것
    // ═══════════════════════════════════════════════════════════════════

    mutable std::mutex stateMutex_;                       // [Protects state_]
    AppState state_;                                      // [Protected by stateMutex_]

    std::mutex listenerMutex_;                            // [Protects listeners_]
    std::vector<StateListener*> listeners_;               // [Protected by listenerMutex_]

    // [callAsync lifetime guard — shared_ptr captured by value in lambda, checked before accessing this]
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    uint32_t lastBroadcastHash_ = 0;                      // [Protected by stateMutex_] Throttle: skip broadcast when state is unchanged
};

} // namespace directpipe
