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
 * @file ActionHandler.h
 * @brief Handles ActionEvents — routes external commands (hotkey, MIDI, WebSocket,
 *        HTTP) to the audio engine, preset system, and UI via callbacks.
 */
#pragma once

#include <JuceHeader.h>
#include "ActionDispatcher.h"
#include "../UI/NotificationBar.h"
#include <functional>

namespace directpipe {

class AudioEngine;
class PresetManager;
class PresetSlotBar;

class ActionHandler {
public:
    ActionHandler(AudioEngine& engine, PresetManager& presetMgr,
                  PresetSlotBar& slotBar,
                  std::atomic<bool>& loadingSlot,
                  std::atomic<bool>& partialLoad);

    /** Handle an action event from the dispatcher. */
    void handle(const ActionEvent& event);

    // ── Mute toggle methods (called by button onClicks) ──

    void toggleOutputMute();
    void toggleMonitorMute();
    void toggleIpcMute();
    void togglePanicMute();

    /** Restore panic mute lockout state after loading settings. */
    void restorePanicMuteFromSettings();

    // ── UI sync callbacks (set by MainComponent) ──

    std::function<void()> onDirty;
    std::function<void(float)> onInputGainSync;
    std::function<void(bool muted)> onPanicStateChanged;
    std::function<void(bool enabled)> onIpcStateChanged;
    std::function<void(const juce::String&, NotificationLevel)> onNotification;

    /** Get recording folder (from OutputPanel). */
    std::function<juce::File()> getRecordingFolder;

    /** Notify that recording stopped with file path. */
    std::function<void(const juce::File&)> onRecordingStopped;

private:
    void doPanicMute(bool mute);

    AudioEngine& engine_;
    PresetManager& presetMgr_;
    PresetSlotBar& slotBar_;
    std::atomic<bool>& loadingSlot_;
    std::atomic<bool>& partialLoad_;

    // Panic mute: remember pre-mute state for restore on unmute
    bool preMuteMonitorEnabled_ = false;
    bool preMuteOutputMuted_ = false;
    bool preMuteVstEnabled_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActionHandler)
};

} // namespace directpipe
