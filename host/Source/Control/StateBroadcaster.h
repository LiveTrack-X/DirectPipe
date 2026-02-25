/**
 * @file StateBroadcaster.h
 * @brief Broadcasts application state changes to all connected clients
 *
 * When any state changes (bypass toggled, volume changed, etc.),
 * the broadcaster pushes the updated state to:
 * - GUI (repaint)
 * - WebSocket clients (Stream Deck)
 * - MIDI LED feedback
 */
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>

namespace directpipe {

/// Complete application state snapshot
struct AppState {
    struct PluginState {
        std::string name;
        bool bypassed = false;
        bool loaded = false;
    };

    std::vector<PluginState> plugins;
    float inputGain = 1.0f;
    float monitorVolume = 1.0f;
    bool masterBypassed = false;
    bool muted = false;
    bool inputMuted = false;
    std::string currentPreset;
    float latencyMs = 0.0f;
    float inputLevelDb = -60.0f;
    float cpuPercent = 0.0f;
    double sampleRate = 48000.0;
    int bufferSize = 480;
    int channelMode = 1;  // 1 = Mono, 2 = Stereo
    bool monitorEnabled = false;
    int activeSlot = 0;   // Quick preset slot index: 0=A, 1=B, 2=C, 3=D, 4=E
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
 * Listeners are notified on the calling thread (ensure GUI updates
 * are dispatched to the message thread).
 */
class StateBroadcaster {
public:
    StateBroadcaster() = default;

    /**
     * @brief Get the current state snapshot.
     */
    AppState getState() const;

    /**
     * @brief Update the state and notify all listeners.
     * @param updater Function that modifies the state.
     */
    void updateState(std::function<void(AppState&)> updater);

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

    mutable std::mutex stateMutex_;
    AppState state_;

    std::mutex listenerMutex_;
    std::vector<StateListener*> listeners_;
};

} // namespace directpipe
