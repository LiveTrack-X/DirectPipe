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
    AppState snapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot = state_;
    }

    std::lock_guard<std::mutex> lock(listenerMutex_);
    for (auto* listener : listeners_) {
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

    root->setProperty("data", juce::var(data));

    return juce::JSON::toString(juce::var(root.get()), true).toStdString();
}

} // namespace directpipe
