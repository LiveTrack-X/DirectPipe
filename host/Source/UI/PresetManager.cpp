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
 * @file PresetManager.cpp
 * @brief Preset management implementation
 */

#include "PresetManager.h"

namespace directpipe {

PresetManager::PresetManager(AudioEngine& engine)
    : engine_(engine)
{
}

bool PresetManager::savePreset(const juce::File& file)
{
    auto json = exportToJSON();
    if (json.isEmpty()) return false;

    bool ok = file.replaceWithText(json);
    if (ok) juce::Logger::writeToLog("[PRESET] Saved: " + file.getFileName());
    return ok;
}

bool PresetManager::loadPreset(const juce::File& file)
{
    if (!file.existsAsFile()) return false;

    auto json = file.loadFileAsString();
    bool ok = importFromJSON(json);
    if (ok) juce::Logger::writeToLog("[PRESET] Loaded: " + file.getFileName());
    return ok;
}

juce::File PresetManager::getPresetsDirectory()
{
    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    auto presetsDir = appData.getChildFile("DirectPipe").getChildFile("Presets");
    presetsDir.createDirectory();
    return presetsDir;
}

juce::File PresetManager::getAutoSaveFile()
{
    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    auto dir = appData.getChildFile("DirectPipe");
    dir.createDirectory();
    return dir.getChildFile("settings.dppreset");
}

juce::Array<juce::File> PresetManager::getAvailablePresets()
{
    auto dir = getPresetsDirectory();
    return dir.findChildFiles(juce::File::findFiles, false,
                              juce::String("*") + kPresetExtension);
}

juce::String PresetManager::exportToJSON()
{
    auto root = std::make_unique<juce::DynamicObject>();

    root->setProperty("version", 4);
    root->setProperty("activeSlot", activeSlot_);

    // Audio settings
    auto& monitor = engine_.getLatencyMonitor();
    root->setProperty("sampleRate", monitor.getSampleRate());
    root->setProperty("bufferSize", monitor.getBufferSize());
    root->setProperty("inputGain", static_cast<double>(engine_.getInputGain()));

    // Device type (ASIO / Windows Audio)
    root->setProperty("deviceType", engine_.getCurrentDeviceType());

    // Input/output device names
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine_.getDeviceManager().getAudioDeviceSetup(setup);
    root->setProperty("inputDevice", setup.inputDeviceName);
    root->setProperty("outputDevice", setup.outputDeviceName);
    root->setProperty("outputNone", engine_.isOutputNone());

    // VST Chain
    juce::Array<juce::var> plugins;
    auto& chain = engine_.getVSTChain();
    for (int i = 0; i < chain.getPluginCount(); ++i) {
        if (auto* slot = chain.getPluginSlot(i)) {
            auto plugin = std::make_unique<juce::DynamicObject>();
            plugin->setProperty("name", slot->name);
            plugin->setProperty("path", slot->path);
            plugin->setProperty("bypassed", slot->bypassed);
            // Store full PluginDescription as XML for accurate re-loading
            if (auto xml = slot->desc.createXml())
                plugin->setProperty("descXml", xml->toString());
            // Store plugin internal state (parameters, settings)
            if (slot->instance) {
                juce::MemoryBlock stateData;
                slot->instance->getStateInformation(stateData);
                if (stateData.getSize() > 0)
                    plugin->setProperty("state", stateData.toBase64Encoding());
            }
            plugins.add(juce::var(plugin.release()));
        }
    }
    root->setProperty("plugins", plugins);

    // Output settings (monitor only — main output uses AudioSettings)
    auto& router = engine_.getOutputRouter();
    auto outputs = std::make_unique<juce::DynamicObject>();
    outputs->setProperty("monitorVolume",
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)));
    outputs->setProperty("monitorEnabled",
        router.isEnabled(OutputRouter::Output::Monitor));
    outputs->setProperty("monitorDevice",
        engine_.getMonitorDeviceName());
    outputs->setProperty("monitorBufferSize",
        engine_.getMonitorBufferSize());
    root->setProperty("outputs", juce::var(outputs.release()));

    // Channel mode (1=mono, 2=stereo)
    root->setProperty("channelMode", engine_.getChannelMode());

    // IPC output
    root->setProperty("ipcEnabled", engine_.isIpcEnabled());

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool PresetManager::importFromJSON(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    // Check version
    int version = root->getProperty("version");
    if (version < 1) return false;

    // Restore active slot
    if (root->hasProperty("activeSlot"))
        activeSlot_ = static_cast<int>(root->getProperty("activeSlot"));

    // Restore device type first (affects which devices are available)
    if (root->hasProperty("deviceType")) {
        juce::String deviceType = root->getProperty("deviceType").toString();
        if (deviceType.isNotEmpty())
            engine_.setAudioDeviceType(deviceType);
    }

    // Audio settings
    if (root->hasProperty("sampleRate")) {
        engine_.setSampleRate(root->getProperty("sampleRate"));
    }
    if (root->hasProperty("bufferSize")) {
        engine_.setBufferSize(root->getProperty("bufferSize"));
    }
    if (root->hasProperty("inputGain")) {
        engine_.setInputGain(static_cast<float>((double)root->getProperty("inputGain")));
    }

    // Restore devices
    if (root->hasProperty("inputDevice")) {
        juce::String inputDev = root->getProperty("inputDevice").toString();
        if (inputDev.isNotEmpty()) {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            engine_.getDeviceManager().getAudioDeviceSetup(setup);
            setup.inputDeviceName = inputDev;
            engine_.getDeviceManager().setAudioDeviceSetup(setup, true);
        }
    }
    // Restore output "None" mode first (before output device)
    bool outputNone = false;
    if (root->hasProperty("outputNone"))
        outputNone = static_cast<bool>(root->getProperty("outputNone"));
    engine_.setOutputNone(outputNone);

    if (!outputNone && root->hasProperty("outputDevice")) {
        juce::String outputDev = root->getProperty("outputDevice").toString();
        if (outputDev.isNotEmpty() && outputDev != "None")
            engine_.setOutputDevice(outputDev);
    }

    // VST Chain — load plugins (with fast-path for identical chain)
    if (root->hasProperty("plugins")) {
        auto* pluginsArray = root->getProperty("plugins").getArray();
        if (pluginsArray) {
            // Build a JSON string of just the chain portion and delegate
            auto chainRoot = std::make_unique<juce::DynamicObject>();
            chainRoot->setProperty("plugins", root->getProperty("plugins"));
            auto chainJson = juce::JSON::toString(juce::var(chainRoot.release()), false);
            importChainFromJSON(chainJson);
        }
    }

    // Output settings (monitor only — main output uses AudioSettings)
    if (root->hasProperty("outputs")) {
        auto* outputs = root->getProperty("outputs").getDynamicObject();
        if (outputs) {
            auto& router = engine_.getOutputRouter();

            if (outputs->hasProperty("monitorVolume"))
                router.setVolume(OutputRouter::Output::Monitor,
                                 static_cast<float>((double)outputs->getProperty("monitorVolume")));
            if (outputs->hasProperty("monitorEnabled")) {
                bool monEnabled = static_cast<bool>(outputs->getProperty("monitorEnabled"));
                router.setEnabled(OutputRouter::Output::Monitor, monEnabled);
                engine_.setMonitorEnabled(monEnabled);
            }

            if (outputs->hasProperty("monitorBufferSize")) {
                int bs = static_cast<int>(outputs->getProperty("monitorBufferSize"));
                if (bs > 0)
                    engine_.setMonitorBufferSize(bs);
            }

            if (outputs->hasProperty("monitorDevice")) {
                juce::String monDevice = outputs->getProperty("monitorDevice").toString();
                if (monDevice.isNotEmpty())
                    engine_.setMonitorDevice(monDevice);
            }
        }
    }

    // Channel mode (1=mono, 2=stereo)
    if (root->hasProperty("channelMode"))
        engine_.setChannelMode(static_cast<int>(root->getProperty("channelMode")));

    // IPC output
    if (root->hasProperty("ipcEnabled"))
        engine_.setIpcEnabled(static_cast<bool>(root->getProperty("ipcEnabled")));

    return true;
}

// ─── Shared chain helpers ────────────────────────────────────────────────────

std::vector<PresetManager::TargetPlugin> PresetManager::parseTargetPlugins(
    const juce::Array<juce::var>* pluginsArray)
{
    std::vector<TargetPlugin> targets;
    if (!pluginsArray) return targets;

    for (auto& pluginVar : *pluginsArray) {
        if (auto* plugin = pluginVar.getDynamicObject()) {
            TargetPlugin t;
            t.name = plugin->getProperty("name").toString();
            t.path = plugin->getProperty("path").toString();
            t.bypassed = static_cast<bool>(plugin->getProperty("bypassed"));

            if (plugin->hasProperty("descXml")) {
                auto xmlStr = plugin->getProperty("descXml").toString();
                if (auto xml = juce::XmlDocument::parse(xmlStr))
                    t.hasDesc = t.desc.loadFromXml(*xml);
            }
            if (plugin->hasProperty("state")) {
                auto stateStr = plugin->getProperty("state").toString();
                if (stateStr.isNotEmpty())
                    t.hasState = t.stateData.fromBase64Encoding(stateStr);
            }
            targets.push_back(std::move(t));
        }
    }
    return targets;
}

bool PresetManager::isSameChain(const std::vector<TargetPlugin>& targets, VSTChain& chain)
{
    if (static_cast<int>(targets.size()) != chain.getPluginCount())
        return false;

    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        auto* slot = chain.getPluginSlot(i);
        auto& t = targets[static_cast<size_t>(i)];
        if (!slot) return false;

        if (t.hasDesc) {
            if (slot->desc.uniqueId != t.desc.uniqueId ||
                slot->desc.fileOrIdentifier != t.desc.fileOrIdentifier)
                return false;
        } else {
            if (slot->path != t.path) return false;
        }
    }
    return true;
}

void PresetManager::applyFastPath(const std::vector<TargetPlugin>& targets, VSTChain& chain)
{
    // Suspend graph processing to prevent audio thread from calling processBlock
    // while we modify plugin state via setStateInformation (not thread-safe)
    chain.suspendProcessing(true);

    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        auto& t = targets[static_cast<size_t>(i)];
        chain.setPluginBypassed(i, t.bypassed);
        if (t.hasState) {
            if (auto* slot = chain.getPluginSlot(i)) {
                if (slot->instance)
                    slot->instance->setStateInformation(
                        t.stateData.getData(), static_cast<int>(t.stateData.getSize()));
            }
        }
    }

    chain.suspendProcessing(false);
}

void PresetManager::applySlowPath(const std::vector<TargetPlugin>& targets, VSTChain& chain)
{
    while (chain.getPluginCount() > 0)
        chain.removePlugin(0);

    for (auto& target : targets) {
        int idx = -1;

        if (target.hasDesc)
            idx = chain.addPlugin(target.desc);

        if (idx < 0) {
            auto& knownPlugins = chain.getKnownPlugins();
            for (const auto& desc : knownPlugins.getTypes()) {
                if (desc.fileOrIdentifier == target.path && desc.name == target.name) {
                    idx = chain.addPlugin(desc);
                    break;
                }
            }
        }

        if (idx < 0) {
            auto& knownPlugins = chain.getKnownPlugins();
            for (const auto& desc : knownPlugins.getTypes()) {
                if (desc.name == target.name) {
                    idx = chain.addPlugin(desc);
                    break;
                }
            }
        }

        if (idx < 0)
            idx = chain.addPlugin(target.path);

        if (idx >= 0) {
            if (target.bypassed)
                chain.setPluginBypassed(idx, true);
            if (target.hasState) {
                if (auto* loadedSlot = chain.getPluginSlot(idx)) {
                    if (loadedSlot->instance) {
                        chain.suspendProcessing(true);
                        loadedSlot->instance->setStateInformation(
                            target.stateData.getData(), static_cast<int>(target.stateData.getSize()));
                        chain.suspendProcessing(false);
                    }
                }
            }
        }
    }
}

// ─── Chain-only export/import ────────────────────────────────────────────────

juce::String PresetManager::exportChainToJSON()
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    root->setProperty("type", "chain");

    juce::Array<juce::var> plugins;
    auto& chain = engine_.getVSTChain();
    for (int i = 0; i < chain.getPluginCount(); ++i) {
        if (auto* slot = chain.getPluginSlot(i)) {
            auto plugin = std::make_unique<juce::DynamicObject>();
            plugin->setProperty("name", slot->name);
            plugin->setProperty("path", slot->path);
            plugin->setProperty("bypassed", slot->bypassed);
            if (auto xml = slot->desc.createXml())
                plugin->setProperty("descXml", xml->toString());
            if (slot->instance) {
                juce::MemoryBlock stateData;
                slot->instance->getStateInformation(stateData);
                if (stateData.getSize() > 0)
                    plugin->setProperty("state", stateData.toBase64Encoding());
            }
            plugins.add(juce::var(plugin.release()));
        }
    }
    root->setProperty("plugins", plugins);

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool PresetManager::importChainFromJSON(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root || !root->hasProperty("plugins")) return false;

    auto* pluginsArray = root->getProperty("plugins").getArray();
    if (!pluginsArray) return false;

    auto& chain = engine_.getVSTChain();
    auto targets = parseTargetPlugins(pluginsArray);

    if (isSameChain(targets, chain)) {
        applyFastPath(targets, chain);
    } else {
        applySlowPath(targets, chain);
    }

    return true;
}

// ─── Quick Preset Slots ─────────────────────────────────────────────────────

juce::File PresetManager::getSlotFile(int slotIndex)
{
    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    auto dir = appData.getChildFile("DirectPipe").getChildFile("Slots");
    dir.createDirectory();
    return dir.getChildFile("slot_" + juce::String(slotLabel(slotIndex)) + ".dppreset");
}

bool PresetManager::saveSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;

    auto file = getSlotFile(slotIndex);
    auto json = exportChainToJSON();
    if (json.isEmpty()) return false;

    bool ok = file.replaceWithText(json);
    if (ok) activeSlot_ = slotIndex;
    if (ok) juce::Logger::writeToLog("[PRESET] Saved slot " + juce::String(slotLabel(slotIndex)));
    return ok;
}

bool PresetManager::loadSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;
    juce::Logger::writeToLog("[PRESET] Loading slot " + juce::String(slotLabel(slotIndex)));

    auto file = getSlotFile(slotIndex);
    if (!file.existsAsFile()) return false;

    auto json = file.loadFileAsString();
    bool ok = importChainFromJSON(json);
    if (ok) activeSlot_ = slotIndex;
    return ok;
}

void PresetManager::loadSlotAsync(int slotIndex, std::function<void(bool)> onComplete)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) {
        if (onComplete) onComplete(false);
        return;
    }

    auto file = getSlotFile(slotIndex);
    if (!file.existsAsFile()) {
        if (onComplete) onComplete(false);
        return;
    }

    auto json = file.loadFileAsString();
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) {
        if (onComplete) onComplete(false);
        return;
    }

    auto* root = parsed.getDynamicObject();
    if (!root || !root->hasProperty("plugins")) {
        if (onComplete) onComplete(false);
        return;
    }

    auto* pluginsArray = root->getProperty("plugins").getArray();
    if (!pluginsArray) {
        if (onComplete) onComplete(false);
        return;
    }

    auto& chain = engine_.getVSTChain();
    auto targets = parseTargetPlugins(pluginsArray);

    // Fast path: same plugins in same order -> sync (instant)
    if (isSameChain(targets, chain)) {
        applyFastPath(targets, chain);
        juce::Logger::writeToLog("[PRESET] Slot " + juce::String(slotLabel(slotIndex)) + ": fast path (" + juce::String(targets.size()) + " plugins)");
        activeSlot_ = slotIndex;
        if (onComplete) onComplete(true);
        return;
    }

    // Slow path: different chain -> async (non-blocking)
    std::vector<VSTChain::PluginLoadRequest> requests;
    for (auto& t : targets) {
        VSTChain::PluginLoadRequest req;
        req.desc = t.desc;
        req.name = t.name;
        req.path = t.path;
        req.bypassed = t.bypassed;
        req.stateData = std::move(t.stateData);
        req.hasState = t.hasState;

        if (!t.hasDesc) {
            for (const auto& desc : chain.getKnownPlugins().getTypes()) {
                if (desc.fileOrIdentifier == t.path && desc.name == t.name) {
                    req.desc = desc;
                    break;
                }
            }
            if (req.desc.name.isEmpty()) {
                for (const auto& desc : chain.getKnownPlugins().getTypes()) {
                    if (desc.name == t.name) {
                        req.desc = desc;
                        break;
                    }
                }
            }
        }
        requests.push_back(std::move(req));
    }

    juce::Logger::writeToLog("[PRESET] Slot " + juce::String(slotLabel(slotIndex)) + ": full reload (" + juce::String(targets.size()) + " plugins)");

    int slot = slotIndex;
    auto aliveFlag = alive_;
    chain.replaceChainAsync(std::move(requests), [this, slot, onComplete, aliveFlag]() {
        if (!aliveFlag->load()) return;
        activeSlot_ = slot;
        if (onComplete) onComplete(true);
    });
}

bool PresetManager::isSlotOccupied(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;
    return getSlotFile(slotIndex).existsAsFile();
}

} // namespace directpipe
