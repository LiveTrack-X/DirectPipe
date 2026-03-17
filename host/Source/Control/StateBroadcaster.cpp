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

// Fast hash of the state fields that change frequently (volumes, levels, CPU, recording)
// to skip full JSON serialization + broadcast when nothing changed.
//
// MAINTENANCE WARNING: When adding a new field to AppState, you MUST also
// add it to this hash function. If omitted, changes to that field will NEVER
// trigger a WebSocket broadcast or UI update — a silent, hard-to-diagnose bug.
static uint32_t quickStateHash(const AppState& s)
{
    uint32_t h = 0;
    auto hashFloat = [&](float v) {
        // Quantize to reduce jitter: 0.05 precision for levels/CPU
        auto q = static_cast<uint32_t>(v * 20.0f);
        h = h * 31u + q;
    };
    hashFloat(s.inputGain);
    hashFloat(s.monitorVolume);
    hashFloat(s.latencyMs);
    hashFloat(s.monitorLatencyMs);
    hashFloat(s.inputLevelDb);
    hashFloat(s.cpuPercent);
    h = h * 31u + static_cast<uint32_t>(s.muted);
    h = h * 31u + static_cast<uint32_t>(s.outputMuted);
    h = h * 31u + static_cast<uint32_t>(s.monitorEnabled);
    h = h * 31u + static_cast<uint32_t>(s.masterBypassed);
    h = h * 31u + static_cast<uint32_t>(s.activeSlot);
    h = h * 31u + static_cast<uint32_t>(s.recording);
    h = h * 31u + static_cast<uint32_t>(s.recordingSeconds * 2.0);  // 0.5s precision
    h = h * 31u + static_cast<uint32_t>(s.inputMuted);
    h = h * 31u + static_cast<uint32_t>(s.ipcEnabled);
    h = h * 31u + static_cast<uint32_t>(s.deviceLost);
    h = h * 31u + static_cast<uint32_t>(s.monitorLost);
    h = h * 31u + static_cast<uint32_t>(s.sampleRate);
    h = h * 31u + static_cast<uint32_t>(s.bufferSize);
    hashFloat(s.outputVolume);
    h = h * 31u + static_cast<uint32_t>(s.xrunCount);
    h = h * 31u + static_cast<uint32_t>(s.limiterEnabled);
    hashFloat(s.limiterCeilingdB);
    hashFloat(s.limiterGainReduction);
    h = h * 31u + static_cast<uint32_t>(s.limiterActive);
    h = h * 31u + static_cast<uint32_t>(s.channelMode);
    h = h * 31u + static_cast<uint32_t>(s.plugins.size());
    for (const auto& p : s.plugins)
        h = h * 31u + (static_cast<uint32_t>(p.bypassed) | (static_cast<uint32_t>(p.loaded) << 1));
    for (const auto& p : s.plugins)
        h = h * 31u + static_cast<uint32_t>(p.latencySamples);
    for (const auto& p : s.plugins)
        h = h * 31u + static_cast<uint32_t>(std::hash<std::string>{}(p.type));
    h = h * 31u + static_cast<uint32_t>(s.chainPDCSamples);
    for (const auto& n : s.slotNames)
        h = h * 31u + static_cast<uint32_t>(std::hash<std::string>{}(n));
    h = h * 31u + static_cast<uint32_t>(std::hash<std::string>{}(s.currentPreset));
    return h;
}

void StateBroadcaster::updateState(std::function<void(AppState&)> updater)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        updater(state_);
        // Skip broadcast if state hasn't meaningfully changed
        uint32_t hash = quickStateHash(state_);
        if (hash == lastBroadcastHash_) return;
        lastBroadcastHash_ = hash;
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
        plugin->setProperty("latency_samples", p.latencySamples);
        plugin->setProperty("type", juce::String(p.type));
        plugins.add(juce::var(plugin));
    }
    data->setProperty("plugins", plugins);

    // Volumes
    auto volumes = new juce::DynamicObject();
    volumes->setProperty("input", static_cast<double>(state.inputGain));
    volumes->setProperty("monitor", static_cast<double>(state.monitorVolume));
    volumes->setProperty("output", static_cast<double>(state.outputVolume));
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
    data->setProperty("ipc_enabled", state.ipcEnabled);
    data->setProperty("device_lost", state.deviceLost);
    data->setProperty("monitor_lost", state.monitorLost);
    data->setProperty("xrun_count", state.xrunCount);

    // Slot names
    juce::Array<juce::var> slotNamesArr;
    for (const auto& name : state.slotNames)
        slotNamesArr.add(juce::String(name));
    data->setProperty("slot_names", slotNamesArr);

    // Safety Limiter
    auto limiterJson = new juce::DynamicObject();
    limiterJson->setProperty("enabled", state.limiterEnabled);
    limiterJson->setProperty("ceiling_dB", static_cast<double>(state.limiterCeilingdB));
    limiterJson->setProperty("gain_reduction_dB", static_cast<double>(state.limiterGainReduction));
    limiterJson->setProperty("is_limiting", state.limiterActive);
    data->setProperty("safety_limiter", juce::var(limiterJson));

    // Chain PDC (plugin delay compensation)
    data->setProperty("chain_pdc_samples", state.chainPDCSamples);
    data->setProperty("chain_pdc_ms", static_cast<double>(state.chainPDCMs));

    root->setProperty("data", juce::var(data));

    return juce::JSON::toString(juce::var(root.get()), true).toStdString();
}

} // namespace directpipe
