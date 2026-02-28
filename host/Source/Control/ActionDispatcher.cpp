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
 * @file ActionDispatcher.cpp
 * @brief Unified action dispatcher implementation
 */

#include "ActionDispatcher.h"
#include <algorithm>

namespace directpipe {

void ActionDispatcher::dispatch(const ActionEvent& event)
{
    // Copy listener list to avoid deadlock if a listener adds/removes listeners
    std::vector<ActionListener*> snapshot;
    {
        std::lock_guard<std::mutex> lock(listenerMutex_);
        snapshot = listeners_;
    }
    for (auto* listener : snapshot) {
        listener->onAction(event);
    }
}

void ActionDispatcher::addListener(ActionListener* listener)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listeners_.push_back(listener);
}

void ActionDispatcher::removeListener(ActionListener* listener)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

void ActionDispatcher::pluginBypass(int pluginIndex)
{
    dispatch({Action::PluginBypass, pluginIndex, 0.0f, ""});
}

void ActionDispatcher::masterBypass()
{
    dispatch({Action::MasterBypass, 0, 0.0f, ""});
}

void ActionDispatcher::setVolume(const std::string& target, float value)
{
    dispatch({Action::SetVolume, 0, value, target});
}

void ActionDispatcher::toggleMute(const std::string& target)
{
    dispatch({Action::ToggleMute, 0, 0.0f, target});
}

void ActionDispatcher::loadPreset(int presetIndex)
{
    dispatch({Action::LoadPreset, presetIndex, 0.0f, ""});
}

void ActionDispatcher::panicMute()
{
    dispatch({Action::PanicMute, 0, 0.0f, ""});
}

void ActionDispatcher::inputGainAdjust(float deltaDb)
{
    dispatch({Action::InputGainAdjust, 0, deltaDb, ""});
}

void ActionDispatcher::inputMuteToggle()
{
    dispatch({Action::InputMuteToggle, 0, 0.0f, ""});
}

} // namespace directpipe
