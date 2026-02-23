/**
 * @file ActionDispatcher.h
 * @brief Unified action interface for all control sources
 *
 * All control inputs (GUI, Hotkey, MIDI, WebSocket, HTTP) route through
 * this dispatcher, ensuring consistent behavior regardless of input source.
 * Communication with the audio thread uses lock-free mechanisms.
 */
#pragma once

#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>

namespace directpipe {

/// All available control actions
enum class Action {
    PluginBypass,       ///< Toggle bypass for a specific plugin (intParam = index)
    MasterBypass,       ///< Toggle master bypass for entire chain
    SetVolume,          ///< Set volume for a target (stringParam = target, floatParam = 0.0~1.0)
    ToggleMute,         ///< Toggle mute for a target (stringParam = target)
    LoadPreset,         ///< Load a preset by index (intParam = index)
    PanicMute,          ///< Immediately mute all outputs
    InputGainAdjust,    ///< Adjust input gain (floatParam = delta_db, +1 or -1)
    NextPreset,         ///< Switch to next preset
    PreviousPreset,     ///< Switch to previous preset
    InputMuteToggle,    ///< Toggle microphone input mute
};

/// Carries an action with its parameters
struct ActionEvent {
    Action action = Action::PanicMute;
    int intParam = 0;
    float floatParam = 0.0f;
    std::string stringParam;
};

/// Listener interface for action events
class ActionListener {
public:
    virtual ~ActionListener() = default;
    virtual void onAction(const ActionEvent& event) = 0;
};

/**
 * @brief Central dispatcher that routes control actions to the audio engine.
 *
 * Thread-safe: actions can be dispatched from any thread (GUI, MIDI, network).
 * The dispatcher forwards to registered listeners on the message thread.
 */
class ActionDispatcher {
public:
    ActionDispatcher() = default;

    /**
     * @brief Dispatch an action from any thread.
     *
     * This is the main entry point for all control sources.
     * The action will be forwarded to all registered listeners.
     *
     * @param event The action to dispatch.
     */
    void dispatch(const ActionEvent& event);

    /**
     * @brief Register a listener for action events.
     */
    void addListener(ActionListener* listener);

    /**
     * @brief Remove a listener.
     */
    void removeListener(ActionListener* listener);

    // ─── Convenience dispatch methods ───

    void pluginBypass(int pluginIndex);
    void masterBypass();
    void setVolume(const std::string& target, float value);
    void toggleMute(const std::string& target);
    void loadPreset(int presetIndex);
    void panicMute();
    void inputGainAdjust(float deltaDb);
    void inputMuteToggle();

private:
    std::vector<ActionListener*> listeners_;
    std::mutex listenerMutex_;
};

} // namespace directpipe
