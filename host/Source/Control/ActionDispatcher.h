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
#include <memory>
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
    SwitchPresetSlot,   ///< Switch to preset slot (intParam = 0..4 for A..E)
    MonitorToggle,      ///< Toggle monitor output on/off
    RecordingToggle,    ///< Toggle audio recording on/off
    SetPluginParameter, ///< Set plugin parameter (intParam=pluginIndex, intParam2=paramIndex, floatParam=value 0.0~1.0)
    IpcToggle,          ///< Toggle IPC output (Receiver VST) on/off
    XRunReset,          ///< Reset xrun counter
};

/// Carries an action with its parameters
struct ActionEvent {
    Action action = Action::PanicMute;
    int intParam = 0;
    float floatParam = 0.0f;
    std::string stringParam;
    int intParam2 = 0;      ///< Secondary int param (appended for backward compatibility)

    bool operator==(const ActionEvent& o) const {
        return action == o.action && intParam == o.intParam
            && intParam2 == o.intParam2 && stringParam == o.stringParam;
    }
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
 * The dispatcher always delivers to listeners on the JUCE message thread.
 * If called from the message thread, delivery is synchronous (zero latency).
 * If called from another thread, delivery is deferred via callAsync.
 */
class ActionDispatcher {
public:
    ActionDispatcher() = default;
    ~ActionDispatcher() { alive_->store(false); }

    /**
     * @brief Dispatch an action from any thread.
     *
     * This is the main entry point for all control sources.
     * The action will be forwarded to all registered listeners.
     *
     * @param event The action to dispatch.
     */
    void dispatch(const ActionEvent& event);  // [Any thread → guarantees Message thread delivery]

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
    void dispatchOnMessageThread(const ActionEvent& event);

    std::vector<ActionListener*> listeners_;                // [Protected by listenerMutex_]
    std::mutex listenerMutex_;                              // [Protects listeners_]

    // [callAsync lifetime guard — shared_ptr captured by value in lambda, checked before accessing this]
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

/// Convert ActionEvent to user-facing display name (for UI labels in HotkeyTab, MidiTab).
/// Guarded by JUCE include check since this header is also used by non-JUCE test code.
#ifdef JUCE_CORE_H_INCLUDED
inline juce::String actionToDisplayName(const ActionEvent& event)
{
    switch (event.action) {
        case Action::ToggleMute:
            if (event.stringParam == "output")  return "Output Mute Toggle";
            if (event.stringParam == "monitor") return "Monitor Mute Toggle";
            return "Toggle Mute";
        case Action::SetPluginParameter:
            if (!event.stringParam.empty())
                return juce::String(event.stringParam);
            return "Plugin[" + juce::String(event.intParam) + "].Param[" +
                   juce::String(event.intParam2) + "]";
        default:
            break;
    }
    if (!event.stringParam.empty())
        return juce::String(event.stringParam);
    switch (event.action) {
        case Action::PluginBypass:    return "Plugin " + juce::String(event.intParam + 1) + " Bypass";
        case Action::MasterBypass:    return "Master Bypass";
        case Action::SetVolume:       return "Set Volume";
        case Action::LoadPreset:      return "Load Preset";
        case Action::PanicMute:       return "Panic Mute";
        case Action::InputGainAdjust: return "Input Gain Adjust";
        case Action::NextPreset:      return "Next Preset";
        case Action::PreviousPreset:  return "Previous Preset";
        case Action::InputMuteToggle: return "Input Mute Toggle";
        case Action::SwitchPresetSlot: {
            char label = 'A' + static_cast<char>(event.intParam);
            return "Preset Slot " + juce::String::charToString(label);
        }
        case Action::MonitorToggle:   return "Monitor Toggle";
        case Action::RecordingToggle: return "Recording Toggle";
        case Action::IpcToggle:       return "IPC Toggle";
        case Action::XRunReset:       return "XRun Reset";
        default:                      return "Unknown";
    }
}
#endif // JUCE_CORE_H_INCLUDED

/// Convert Action enum to human-readable string (for logging)
inline const char* actionToString(Action a)
{
    switch (a) {
        case Action::PluginBypass:       return "PluginBypass";
        case Action::MasterBypass:       return "MasterBypass";
        case Action::SetVolume:          return "SetVolume";
        case Action::ToggleMute:         return "ToggleMute";
        case Action::LoadPreset:         return "LoadPreset";
        case Action::PanicMute:          return "PanicMute";
        case Action::InputGainAdjust:    return "InputGainAdjust";
        case Action::NextPreset:         return "NextPreset";
        case Action::PreviousPreset:     return "PreviousPreset";
        case Action::InputMuteToggle:    return "InputMuteToggle";
        case Action::SwitchPresetSlot:   return "SwitchPresetSlot";
        case Action::MonitorToggle:      return "MonitorToggle";
        case Action::RecordingToggle:    return "RecordingToggle";
        case Action::SetPluginParameter: return "SetPluginParameter";
        case Action::IpcToggle:          return "IpcToggle";
        case Action::XRunReset:          return "XRunReset";
        default:                         return "Unknown";
    }
}

} // namespace directpipe
