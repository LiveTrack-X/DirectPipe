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
 * @file StateBroadcaster.cpp
 * @brief State broadcaster implementation
 */

#include "StateBroadcaster.h"
#include <algorithm>
#include <JuceHeader.h>

namespace directpipe {

AppState StateBroadcaster::getState() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

void StateBroadcaster::updateState(std::function<void(AppState&)> updater)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        updater(state_);
    }
    notifyListeners();
}

void StateBroadcaster::addListener(StateListener* listener)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listeners_.push_back(listener);
}

void StateBroadcaster::removeListener(StateListener* listener)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

void StateBroadcaster::notifyListeners()
{
    // Always deliver to listeners on the message thread.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        auto aliveFlag = alive_;
        juce::MessageManager::callAsync([this, aliveFlag] {
            if (!aliveFlag->load()) return;
            notifyOnMessageThread();
        });
        return;
    }
    notifyOnMessageThread();
}

void StateBroadcaster::notifyOnMessageThread()
{
    AppState snapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot = state_;
    }

    // Copy listener list to avoid issues if a listener adds/removes listeners
    std::vector<StateListener*> listenerSnapshot;
    {
        std::lock_guard<std::mutex> lock(listenerMutex_);
        listenerSnapshot = listeners_;
    }
    for (auto* listener : listenerSnapshot) {
        listener->onStateChanged(snapshot);
    }
}

std::string StateBroadcaster::toJSON() const
{
    auto state = getState();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "state");

    auto data = new juce::DynamicObject();

    // Plugins
    juce::Array<juce::var> plugins;
    for (const auto& p : state.plugins) {
        auto plugin = new juce::DynamicObject();
        plugin->setProperty("name", juce::String(p.name));
        plugin->setProperty("bypass", p.bypassed);
        plugin->setProperty("loaded", p.loaded);
        plugins.add(juce::var(plugin));
    }
    data->setProperty("plugins", plugins);

    // Volumes
    auto volumes = new juce::DynamicObject();
    volumes->setProperty("input", static_cast<double>(state.inputGain));
    volumes->setProperty("monitor", static_cast<double>(state.monitorVolume));
    data->setProperty("volumes", juce::var(volumes));

    // Status
    data->setProperty("master_bypassed", state.masterBypassed);
    data->setProperty("muted", state.muted);
    data->setProperty("output_muted", state.outputMuted);
    data->setProperty("input_muted", state.inputMuted);
    data->setProperty("preset", juce::String(state.currentPreset));
    data->setProperty("latency_ms", static_cast<double>(state.latencyMs));
    data->setProperty("monitor_latency_ms", static_cast<double>(state.monitorLatencyMs));
    data->setProperty("level_db", static_cast<double>(state.inputLevelDb));
    data->setProperty("cpu_percent", static_cast<double>(state.cpuPercent));
    data->setProperty("sample_rate", state.sampleRate);
    data->setProperty("buffer_size", state.bufferSize);
    data->setProperty("channel_mode", state.channelMode);
    data->setProperty("monitor_enabled", state.monitorEnabled);
    data->setProperty("active_slot", state.activeSlot);
    data->setProperty("recording", state.recording);
    data->setProperty("recording_seconds", state.recordingSeconds);

    root->setProperty("data", juce::var(data));

    return juce::JSON::toString(juce::var(root.get()), true).toStdString();
}

} // namespace directpipe
