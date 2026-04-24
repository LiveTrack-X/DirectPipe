// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
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
#include "../Control/ControlMapping.h"
#include "../Control/Log.h"
#include "../Util/AtomicFileIO.h"

namespace directpipe {
namespace {
juce::Array<juce::var> bigIntToIndexArray(const juce::BigInteger& mask)
{
    juce::Array<juce::var> out;
    for (int i = mask.findNextSetBit(0); i >= 0; i = mask.findNextSetBit(i + 1))
        out.add(i);
    return out;
}

juce::BigInteger readMaskWithValidation(const juce::var& value, int maxChannels)
{
    juce::BigInteger mask;
    if (auto* arr = value.getArray()) {
        // ASIO masks may be non-contiguous; persist exact indices.
        for (const auto& v : *arr) {
            int idx = static_cast<int>(v);
            if (idx >= 0 && idx < maxChannels)
                mask.setBit(idx);
        }
    }
    return mask;
}

void applySafeDefaultMask(juce::BigInteger& mask, int maxChannels)
{
    // Invalid/empty saved mask falls back to a safe first up-to-2 channels.
    if (mask.isZero()) {
        for (int i = 0; i < juce::jmin(2, maxChannels); ++i)
            mask.setBit(i);
    }
}

int fallbackChannelCount(int reportedCount, const juce::BigInteger& currentMask)
{
    if (reportedCount > 0)
        return reportedCount;
    const int fromMask = currentMask.findNextSetBit(0) >= 0 ? (currentMask.getHighestBit() + 1) : 0;
    // Some drivers briefly report 0 channel names; prefer stereo-safe fallback.
    return juce::jmax(2, fromMask);
}
} // namespace

PresetManager::PresetManager(AudioEngine& engine)
    : engine_(engine)
{
    refreshSlotOccupancyCache();
    loadSlotNames();
}

bool PresetManager::savePreset(const juce::File& file)
{
    auto json = exportToJSON();
    if (json.isEmpty()) return false;

    if (!atomicWriteFile(file, json)) {
        juce::Logger::writeToLog("[PRESET] Failed to save: " + file.getFullPathName());
        return false;
    }

    juce::Logger::writeToLog("[PRESET] Saved: " + file.getFileName()
        + " (size=" + juce::String(json.getNumBytesAsUTF8()) + ")");
    return true;
}

bool PresetManager::loadPreset(const juce::File& file)
{
    auto json = loadFileWithBackupFallback(file);
    if (json.isEmpty()) return false;

    bool ok = importFromJSON(json);
    if (ok)
        juce::Logger::writeToLog("[PRESET] Loaded: " + file.getFileName());
    else
        juce::Logger::writeToLog("[PRESET] Failed to parse: " + file.getFileName());
    return ok;
}

juce::File PresetManager::getPresetsDirectory()
{
    auto presetsDir = ControlMappingStore::getConfigDirectory().getChildFile("Presets");
    presetsDir.createDirectory();
    return presetsDir;
}

juce::File PresetManager::getAutoSaveFile()
{
    auto dir = ControlMappingStore::getConfigDirectory();
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

    // Audio settings (use desired values to survive driver fallback)
    root->setProperty("sampleRate", engine_.getDesiredSampleRate());
    root->setProperty("bufferSize", engine_.getDesiredBufferSize());
    root->setProperty("inputGain", static_cast<double>(engine_.getInputGain()));
    root->setProperty("muted", engine_.isMuted());

    // Device type (ASIO / Windows Audio)
    root->setProperty("deviceType", engine_.getDesiredDeviceType());

    // Input/output device names (use desired values to survive fallback state)
    root->setProperty("inputDevice", engine_.getDesiredInputDevice());
    root->setProperty("outputDevice", engine_.getDesiredOutputDevice());
    root->setProperty("outputNone", engine_.isOutputNone());
    {
        // Persist explicit channel masks in JSON.
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        engine_.getDeviceManager().getAudioDeviceSetup(setup);
        root->setProperty("inputChannelMask", bigIntToIndexArray(setup.inputChannels));
        root->setProperty("outputChannelMask", bigIntToIndexArray(setup.outputChannels));
    }

    // VST Chain
    juce::Array<juce::var> plugins;
    auto& chain = engine_.getVSTChain();
    for (int i = 0; i < chain.getPluginCount(); ++i) {
        if (auto* slot = chain.getPluginSlot(i)) {
            auto plugin = std::make_unique<juce::DynamicObject>();
            plugin->setProperty("name", slot->name);
            plugin->setProperty("bypassed", slot->bypassed);

            // Type field for built-in vs VST
            switch (slot->type) {
                case PluginSlot::Type::BuiltinFilter:       plugin->setProperty("type", "builtin_filter"); break;
                case PluginSlot::Type::BuiltinNoiseRemoval: plugin->setProperty("type", "builtin_noise_removal"); break;
                case PluginSlot::Type::BuiltinAutoGain:     plugin->setProperty("type", "builtin_auto_gain"); break;
                default:                                    plugin->setProperty("type", "vst"); break;
            }

            if (slot->type == PluginSlot::Type::VST) {
                plugin->setProperty("path", slot->path);
                // Store full PluginDescription as XML for accurate re-loading
                if (auto xml = slot->desc.createXml())
                    plugin->setProperty("descXml", xml->toString());
            }

            // Store processor state (parameters, settings) -- works for both VST and built-in
            if (auto* proc = slot->getProcessor()) {
                juce::MemoryBlock stateData;
                proc->getStateInformation(stateData);
                if (stateData.getSize() > 0)
                    plugin->setProperty("state", stateData.toBase64Encoding());
            }
            plugins.add(juce::var(plugin.release()));
        }
    }
    root->setProperty("plugins", plugins);

    // Output settings (monitor + main output volume)
    auto& router = engine_.getOutputRouter();
    auto outputs = std::make_unique<juce::DynamicObject>();
    outputs->setProperty("monitorVolume",
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)));
    outputs->setProperty("outputVolume",
        static_cast<double>(router.getVolume(OutputRouter::Output::Main)));
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

    // Output mute state (don't persist auto-mute from device loss)
    root->setProperty("outputMuted", engine_.isOutputAutoMuted() ? false : engine_.isOutputMuted());

    // Audit mode
    root->setProperty("auditMode", Log::isAuditMode());

    // Safety Guard state (legacy "safetyLimiter" key for compatibility)
    auto limiterObj = new juce::DynamicObject();
    auto& limiter = engine_.getSafetyLimiter();
    limiterObj->setProperty("enabled", limiter.isEnabled());
    limiterObj->setProperty("ceiling_dB", static_cast<double>(limiter.getCeilingdB()));
    limiterObj->setProperty("headroom_enabled", engine_.isSafetyHeadroomEnabled());
    limiterObj->setProperty("headroom_dB", static_cast<double>(engine_.getSafetyHeadroomdB()));
    root->setProperty("safetyLimiter", juce::var(limiterObj));

    if (onExportAppSettings)
        onExportAppSettings(*root);

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

    // Restore active slot (clamp to valid range: -1 to kNumSlots-1, i.e. -1 to 5)
    if (root->hasProperty("activeSlot")) {
        int slot = static_cast<int>(root->getProperty("activeSlot"));
        activeSlot_ = juce::jlimit(-1, kNumSlots - 1, slot);
    }

    // Pre-set SR/BS before device type switch so the new driver opens with
    // the correct values from the start (avoids e.g. ASIO 256->128 bounce).
    {
        double sr = root->hasProperty("sampleRate") ? (double)root->getProperty("sampleRate") : 0;
        int bs = root->hasProperty("bufferSize") ? (int)root->getProperty("bufferSize") : 0;
        if (sr > 0 || bs > 0)
            engine_.presetAudioParams(sr, bs);
    }

    // Restore device type first (affects which devices are available).
    // For ASIO, provide saved device name to avoid opening the wrong device.
    // Skip if the saved device type doesn't exist on this platform (cross-platform safety).
    if (root->hasProperty("deviceType")) {
        juce::String deviceType = root->getProperty("deviceType").toString();
        if (deviceType.isNotEmpty()) {
            auto availableTypes = engine_.getAvailableDeviceTypes();
            if (availableTypes.contains(deviceType)) {
                juce::String preferredDev;
                if (deviceType.containsIgnoreCase("ASIO")) {
                    if (root->hasProperty("inputDevice"))
                        preferredDev = root->getProperty("inputDevice").toString();
                    else if (root->hasProperty("outputDevice"))
                        preferredDev = root->getProperty("outputDevice").toString();
                }
                (void)engine_.setAudioDeviceType(deviceType, preferredDev);
            } else {
                juce::Logger::writeToLog("[PRESET] Skipping unavailable device type: " + deviceType);
            }
        }
    }

    // SR/BS restore policy: ASIO vs Non-ASIO
    //
    // ASIO: device owns SR/BS globally. All apps sharing the device see the
    // same SR/BS. Forcing our saved values would restart the ASIO driver and
    // disrupt other audio sources (DAWs, media players, voice chat, etc.).
    // Accept the device's current SR/BS via syncDesiredFromDevice().
    //
    // Non-ASIO (WASAPI/CoreAudio/ALSA): SR/BS is per-app. Other apps are
    // unaffected by our changes. Safe to force saved values.
    // Apply via setSampleRate/setBufferSize as before.
    //
    bool isAsio = engine_.getCurrentDeviceType().containsIgnoreCase("ASIO");
    if (isAsio) {
        engine_.syncDesiredFromDevice();
    } else {
        if (root->hasProperty("sampleRate")) {
            (void)engine_.setSampleRate(root->getProperty("sampleRate"));
        }
        if (root->hasProperty("bufferSize")) {
            (void)engine_.setBufferSize(root->getProperty("bufferSize"));
        }
    }
    if (root->hasProperty("inputGain")) {
        engine_.setInputGain(static_cast<float>((double)root->getProperty("inputGain")));
    }
    if (root->hasProperty("muted")) {
        engine_.setMuted(static_cast<bool>(root->getProperty("muted")));
    }

    // Restore devices (use engine methods for intentionalChange_ guard + desiredDevice tracking)
    if (root->hasProperty("inputDevice")) {
        juce::String inputDev = root->getProperty("inputDevice").toString();
        if (inputDev.isNotEmpty())
            (void)engine_.setInputDevice(inputDev);
    }
    // Restore output "None" mode first (before output device)
    bool outputNone = false;
    if (root->hasProperty("outputNone"))
        outputNone = static_cast<bool>(root->getProperty("outputNone"));
    engine_.setOutputNone(outputNone);

    if (!outputNone && root->hasProperty("outputDevice")) {
        juce::String outputDev = root->getProperty("outputDevice").toString();
        if (outputDev.isNotEmpty() && outputDev != "None")
            (void)engine_.setOutputDevice(outputDev);
    }

    if (root->hasProperty("inputChannelMask") || root->hasProperty("outputChannelMask")) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        auto& dm = engine_.getDeviceManager();
        dm.getAudioDeviceSetup(setup);

        int inChCount = 0;
        int outChCount = 0;
        if (auto* dev = dm.getCurrentAudioDevice()) {
            inChCount = dev->getInputChannelNames().size();
            outChCount = dev->getOutputChannelNames().size();
        }
        inChCount = fallbackChannelCount(inChCount, setup.inputChannels);
        outChCount = fallbackChannelCount(outChCount, setup.outputChannels);

        if (root->hasProperty("inputChannelMask")) {
            auto inMask = readMaskWithValidation(root->getProperty("inputChannelMask"), inChCount);
            applySafeDefaultMask(inMask, inChCount);
            setup.useDefaultInputChannels = false;
            setup.inputChannels = inMask;
        }
        if (root->hasProperty("outputChannelMask")) {
            auto outMask = readMaskWithValidation(root->getProperty("outputChannelMask"), outChCount);
            applySafeDefaultMask(outMask, outChCount);
            setup.useDefaultOutputChannels = false;
            setup.outputChannels = outMask;
        }

        auto maskResult = dm.setAudioDeviceSetup(setup, true);
        if (maskResult.isNotEmpty()) {
            juce::Logger::writeToLog("[PRESET] Channel mask restore failed: " + maskResult);
            // Retry with minimal explicit masks, then driver defaults as final fallback.
            auto retrySetup = setup;
            bool hasExplicitMask = false;
            if (root->hasProperty("inputChannelMask")) {
                retrySetup.useDefaultInputChannels = false;
                retrySetup.inputChannels.clear();
                retrySetup.inputChannels.setBit(0);
                hasExplicitMask = true;
            }
            if (root->hasProperty("outputChannelMask")) {
                retrySetup.useDefaultOutputChannels = false;
                retrySetup.outputChannels.clear();
                retrySetup.outputChannels.setBit(0);
                hasExplicitMask = true;
            }

            if (hasExplicitMask) {
                auto retryResult = dm.setAudioDeviceSetup(retrySetup, true);
                if (retryResult.isNotEmpty()) {
                    juce::Logger::writeToLog("[PRESET] Channel mask minimal fallback failed: " + retryResult);
                    auto defaultSetup = retrySetup;
                    if (root->hasProperty("inputChannelMask"))
                        defaultSetup.useDefaultInputChannels = true;
                    if (root->hasProperty("outputChannelMask"))
                        defaultSetup.useDefaultOutputChannels = true;
                    auto defaultResult = dm.setAudioDeviceSetup(defaultSetup, true);
                    if (defaultResult.isNotEmpty())
                        juce::Logger::writeToLog("[PRESET] Channel mask default fallback failed: " + defaultResult);
                }
            }
        }
    }

    // VST Chain load plugins (with fast-path for identical chain)
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

    // Output settings (monitor + main output volume)
    if (root->hasProperty("outputs")) {
        auto* outputs = root->getProperty("outputs").getDynamicObject();
        if (outputs) {
            auto& router = engine_.getOutputRouter();

            if (outputs->hasProperty("monitorVolume"))
                router.setVolume(OutputRouter::Output::Monitor,
                                 static_cast<float>((double)outputs->getProperty("monitorVolume")));
            if (outputs->hasProperty("outputVolume"))
                router.setVolume(OutputRouter::Output::Main,
                                 static_cast<float>((double)outputs->getProperty("outputVolume")));
            if (outputs->hasProperty("monitorEnabled")) {
                bool monEnabled = static_cast<bool>(outputs->getProperty("monitorEnabled"));
                router.setEnabled(OutputRouter::Output::Monitor, monEnabled);
                engine_.setMonitorEnabled(monEnabled);
            }

            if (outputs->hasProperty("monitorBufferSize")) {
                int bs = static_cast<int>(outputs->getProperty("monitorBufferSize"));
                if (bs > 0)
                    (void)engine_.setMonitorBufferSize(bs);
            }

            if (outputs->hasProperty("monitorDevice")) {
                juce::String monDevice = outputs->getProperty("monitorDevice").toString();
                if (monDevice.isNotEmpty())
                    (void)engine_.setMonitorDevice(monDevice);
            }
        }
    }

    // Channel mode (1=mono, 2=stereo)
    if (root->hasProperty("channelMode"))
        engine_.setChannelMode(static_cast<int>(root->getProperty("channelMode")));

    // IPC output
    if (root->hasProperty("ipcEnabled"))
        engine_.setIpcEnabled(static_cast<bool>(root->getProperty("ipcEnabled")));

    // Output mute state
    if (root->hasProperty("outputMuted"))
        engine_.setOutputMuted(static_cast<bool>(root->getProperty("outputMuted")));

    // Audit mode
    if (root->hasProperty("auditMode"))
        Log::setAuditMode(static_cast<bool>(root->getProperty("auditMode")));

    // Safety Guard state (legacy "safetyLimiter" key; missing key = defaults)
    // Legacy presets may omit Safety Volume fields, so reset those defaults
    // before applying optional values.
    engine_.setSafetyHeadroomEnabled(true);
    engine_.setSafetyHeadroomdB(-0.3f);
    if (auto* limiterObj = root->getProperty("safetyLimiter").getDynamicObject()) {
        auto& limiter = engine_.getSafetyLimiter();
        if (limiterObj->hasProperty("enabled"))
            limiter.setEnabled(static_cast<bool>(limiterObj->getProperty("enabled")));
        if (limiterObj->hasProperty("ceiling_dB"))
            limiter.setCeiling(static_cast<float>(static_cast<double>(limiterObj->getProperty("ceiling_dB"))));
        if (limiterObj->hasProperty("headroom_enabled"))
            engine_.setSafetyHeadroomEnabled(static_cast<bool>(limiterObj->getProperty("headroom_enabled")));
        if (limiterObj->hasProperty("headroom_dB"))
            engine_.setSafetyHeadroomdB(static_cast<float>(static_cast<double>(limiterObj->getProperty("headroom_dB"))));
    }

    if (onImportAppSettings)
        onImportAppSettings(*root);

    return true;
}

// Shared chain helpers

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

            // Parse type field (backward compat: missing type = VST)
            auto typeStr = plugin->hasProperty("type")
                ? plugin->getProperty("type").toString()
                : juce::String("vst");
            if (typeStr == "builtin_filter")
                t.type = PluginSlot::Type::BuiltinFilter;
            else if (typeStr == "builtin_noise_removal")
                t.type = PluginSlot::Type::BuiltinNoiseRemoval;
            else if (typeStr == "builtin_auto_gain")
                t.type = PluginSlot::Type::BuiltinAutoGain;
            else
                t.type = PluginSlot::Type::VST;

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

std::vector<PresetManager::TargetPlugin> PresetManager::parseSlotFile(int slotIndex)
{
    auto file = getSlotFile(slotIndex);
    auto json = loadFileWithBackupFallback(file);
    if (json.isEmpty()) return {};
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return {};
    auto* root = parsed.getDynamicObject();
    if (!root || !root->hasProperty("plugins")) return {};
    return parseTargetPlugins(root->getProperty("plugins").getArray());
}

bool PresetManager::isSameChain(const std::vector<TargetPlugin>& targets, VSTChain& chain)
{
    if (static_cast<int>(targets.size()) != chain.getPluginCount())
        return false;

    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        auto* slot = chain.getPluginSlot(i);
        auto& t = targets[static_cast<size_t>(i)];
        if (!slot) return false;

        // Type must match
        if (slot->type != t.type) return false;

        // Built-in processors: same type = same chain entry
        if (t.type != PluginSlot::Type::VST)
            continue;

        // VST: compare by description or path
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
                // Use getProcessor() to handle both built-in and VST processors
                if (auto* proc = slot->getProcessor())
                    proc->setStateInformation(
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

    // Collect loaded plugin indices for batch state restore
    struct LoadedPlugin {
        int idx;
        size_t targetIdx;
    };
    std::vector<LoadedPlugin> loaded;

    for (size_t ti = 0; ti < targets.size(); ++ti) {
        auto& target = targets[ti];
        int idx = -1;

        // Built-in processors: add via addBuiltinProcessor
        if (target.type != PluginSlot::Type::VST) {
            auto result = chain.addBuiltinProcessor(target.type);
            if (result) {
                idx = chain.getPluginCount() - 1;
            }
        } else {
            // VST loading logic
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
        }

        if (idx >= 0) {
            if (target.bypassed)
                chain.setPluginBypassed(idx, true);
            if (target.hasState)
                loaded.push_back({idx, ti});
        }
    }

    // Batch state restore under single suspend (prevents audio thread from
    // seeing partially-restored state between plugins)
    if (!loaded.empty()) {
        chain.suspendProcessing(true);
        for (auto& lp : loaded) {
            if (auto* loadedSlot = chain.getPluginSlot(lp.idx)) {
                // Use getProcessor() to handle both built-in and VST processors
                if (auto* proc = loadedSlot->getProcessor()) {
                    auto& t = targets[lp.targetIdx];
                    proc->setStateInformation(
                        t.stateData.getData(), static_cast<int>(t.stateData.getSize()));
                }
            }
        }
        chain.suspendProcessing(false);
    }
}

// Chain-only export/import

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
            plugin->setProperty("bypassed", slot->bypassed);

            // Type field for built-in vs VST
            switch (slot->type) {
                case PluginSlot::Type::BuiltinFilter:       plugin->setProperty("type", "builtin_filter"); break;
                case PluginSlot::Type::BuiltinNoiseRemoval: plugin->setProperty("type", "builtin_noise_removal"); break;
                case PluginSlot::Type::BuiltinAutoGain:     plugin->setProperty("type", "builtin_auto_gain"); break;
                default:                                    plugin->setProperty("type", "vst"); break;
            }

            if (slot->type == PluginSlot::Type::VST) {
                // VST plugins: save path and description XML
                plugin->setProperty("path", slot->path);
                if (auto xml = slot->desc.createXml())
                    plugin->setProperty("descXml", xml->toString());
            }

            // State: use getProcessor() which returns the active processor (built-in or VST)
            if (auto* proc = slot->getProcessor()) {
                juce::MemoryBlock stateData;
                proc->getStateInformation(stateData);
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

    // Bypass state validation force-sync runtime state to match saved state
    // (addresses v3.10.1 bypass corruption bug)
    for (size_t i = 0; i < targets.size() && static_cast<int>(i) < chain.getPluginCount(); ++i) {
        if (auto* slot = chain.getPluginSlot(static_cast<int>(i))) {
            if (slot->bypassed != targets[i].bypassed) {
                juce::Logger::writeToLog("[PRESET] Bypass mismatch on plugin "
                    + juce::String(static_cast<int>(i)) + " (" + targets[i].name
                    + "): saved=" + juce::String(targets[i].bypassed ? "true" : "false")
                    + ", actual=" + juce::String(slot->bypassed ? "true" : "false")
                    + " - forcing sync");
                chain.setPluginBypassed(static_cast<int>(i), targets[i].bypassed);
            }
        }
    }

    return true;
}

// Quick Preset Slots

juce::File PresetManager::getSlotFile(int slotIndex)
{
    auto dir = ControlMappingStore::getConfigDirectory().getChildFile("Slots");
    dir.createDirectory();

    // Auto slot (index 5) uses a special filename
    if (slotIndex == 5) {
        return dir.getChildFile("slot_Auto.dppreset");
    }

    auto newFile = dir.getChildFile("slot_" + juce::String::charToString(slotLabel(slotIndex)) + ".dppreset");

    // Migrate from old numeric filenames (slot_65.dppreset -> slot_A.dppreset)
    if (!newFile.existsAsFile()) {
        auto oldFile = dir.getChildFile("slot_" + juce::String(static_cast<int>(slotLabel(slotIndex))) + ".dppreset");
        if (oldFile.existsAsFile())
            oldFile.moveFileTo(newFile);
    }

    return newFile;
}

bool PresetManager::saveSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;

    // If all plugins were removed, delete the slot file to reflect empty state
    if (engine_.getVSTChain().getPluginCount() == 0) {
        auto file = getSlotFile(slotIndex);
        if (file.existsAsFile()) {
            file.deleteFile();
            file.withFileExtension("dppreset.backup").deleteFile();
            preloadCache_.invalidateSlot(slotIndex);
            slotOccupiedCache_[static_cast<size_t>(slotIndex)] = false;
            juce::Logger::writeToLog("[PRESET] Cleared empty slot " + juce::String::charToString(slotLabel(slotIndex)));
        }
        return true;  // empty-chain clear is a successful operation
    }

    auto file = getSlotFile(slotIndex);
    auto json = exportChainToJSON();
    if (json.isEmpty()) return false;

    // Inject slot name into JSON
    auto slotName = slotNames_[static_cast<size_t>(slotIndex)];
    {
        auto parsed = juce::JSON::parse(json);
        if (auto* obj = parsed.getDynamicObject()) {
            obj->setProperty("name", slotName);  // empty string clears the name
            json = juce::JSON::toString(juce::var(obj), true);
        }
    }

    // atomicWriteFile: writes to .tmp, renames original to .bak, renames .tmp to target.
    // Crash-safe: power failure at any point leaves either the original or .bak intact.
    // DO NOT replace with file.replaceWithText() that can lose data on crash.
    bool ok = atomicWriteFile(file, json);
    if (ok) {
        activeSlot_ = slotIndex;
        // Invalidate cache only if chain STRUCTURE changed (names/paths/order).
        // Parameter-only changes (bypass, state) don't need invalidation because
        // loadSlotAsync re-reads fresh state from the file at load time.
        // This avoids clearing the cache on every auto-save or bypass toggle.
        {
            auto& chain = engine_.getVSTChain();
            int count = chain.getPluginCount();
            std::vector<std::pair<juce::String, juce::String>> structure;
            structure.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (auto* slot = chain.getPluginSlot(i))
                    structure.push_back({slot->name, slot->path});
            }
            auto* device = engine_.getDeviceManager().getCurrentAudioDevice();
            double sr = device ? device->getCurrentSampleRate() : 0;
            int bs = device ? device->getCurrentBufferSizeSamples() : 0;
            if (!preloadCache_.isCachedWithStructure(slotIndex, sr, bs, structure))
                preloadCache_.invalidateSlot(slotIndex);
        }
        slotOccupiedCache_[static_cast<size_t>(slotIndex)] = true;
        juce::String bypassStr;
        auto& chain = engine_.getVSTChain();
        for (int i = 0; i < chain.getPluginCount(); ++i) {
            if (auto* slot = chain.getPluginSlot(i))
                bypassStr += (slot->bypassed ? "T" : "F") + juce::String(i < chain.getPluginCount() - 1 ? "," : "");
        }
        juce::Logger::writeToLog("[PRESET] Saved slot "
            + juce::String::charToString(slotLabel(slotIndex))
            + ": " + juce::String(chain.getPluginCount()) + " plugins"
            + ", bypass=[" + bypassStr + "]"
            + ", size=" + juce::String(json.getNumBytesAsUTF8()) + "bytes");
    }
    return ok;
}

bool PresetManager::loadSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;
    juce::Logger::writeToLog("[PRESET] Loading slot " + juce::String::charToString(slotLabel(slotIndex)));

    auto file = getSlotFile(slotIndex);
    auto json = loadFileWithBackupFallback(file);
    if (json.isEmpty()) return false;

    bool ok = importChainFromJSON(json);
    if (ok) {
        activeSlot_ = slotIndex;
        // Read slot name from file
        auto parsed = juce::JSON::parse(json);
        if (auto* root = parsed.getDynamicObject()) {
            if (root->hasProperty("name"))
                slotNames_[static_cast<size_t>(slotIndex)] = root->getProperty("name").toString();
            else
                slotNames_[static_cast<size_t>(slotIndex)] = juce::String();
        }
        juce::String bypassStr;
        auto& chain = engine_.getVSTChain();
        for (int i = 0; i < chain.getPluginCount(); ++i) {
            if (auto* slot = chain.getPluginSlot(i))
                bypassStr += (slot->bypassed ? "T" : "F") + juce::String(i < chain.getPluginCount() - 1 ? "," : "");
        }
        juce::Logger::writeToLog("[PRESET] Loaded slot "
            + juce::String::charToString(slotLabel(slotIndex))
            + ": " + juce::String(chain.getPluginCount()) + " plugins"
            + ", bypass=[" + bypassStr + "]");
    }
    return ok;
}

void PresetManager::loadSlotAsync(int slotIndex, std::function<void(bool)> onComplete)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) {
        if (onComplete) onComplete(false);
        return;
    }

    Log::audit("PRESET", "loadSlotAsync called: slot=" + juce::String(slotIndex)
        + " activeSlot=" + juce::String(activeSlot_));

    auto file = getSlotFile(slotIndex);
    if (!file.existsAsFile()) {
        auto backup = file.withFileExtension("dppreset.backup");
        if (backup.existsAsFile()) {
            juce::Logger::writeToLog("[PRESET] Slot file missing, restoring from backup");
            backup.copyFileTo(file);
        }
    }
    if (!file.existsAsFile()) {
        if (onComplete) onComplete(false);
        return;
    }

    auto json = file.loadFileAsString();
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) {
        // Try backup for corrupt file
        auto backup = file.withFileExtension("dppreset.backup");
        if (backup.existsAsFile()) {
            juce::Logger::writeToLog("[PRESET] Slot file corrupt, restoring from backup");
            json = backup.loadFileAsString();
            parsed = juce::JSON::parse(json);
            if (parsed.isObject()) backup.copyFileTo(file);
        }
    }
    if (!parsed.isObject()) {
        if (onComplete) onComplete(false);
        return;
    }

    auto* root = parsed.getDynamicObject();
    if (!root || !root->hasProperty("plugins")) {
        if (onComplete) onComplete(false);
        return;
    }

    // Update slot name from file (sync loadSlot does this too)
    if (root->hasProperty("name"))
        slotNames_[static_cast<size_t>(slotIndex)] = root->getProperty("name").toString();
    else
        slotNames_[static_cast<size_t>(slotIndex)] = juce::String();

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
        juce::Logger::writeToLog("[PRESET] Slot " + juce::String::charToString(slotLabel(slotIndex)) + ": fast path (" + juce::String(targets.size()) + " plugins)");
        activeSlot_ = slotIndex;
        if (onComplete) onComplete(true);
        // Defer preload to avoid blocking message thread
        auto alivePreload = alive_;
        juce::MessageManager::callAsync([this, alivePreload]() {
            if (!alivePreload->load()) return;
            if (suppressPreload_.load()) return;  // async load in progress, skip
            triggerPreload();
        });
        return;
    }

    // Cache path: pre-loaded instances available instant swap (~10-50ms)
    // Check cache BEFORE cancelAndWait preload thread may still be populating it.
    // replaceChainWithPreloaded does NOT use formatManager, so no concurrent access risk.
    {
        auto* device = engine_.getDeviceManager().getCurrentAudioDevice();
        double sr = device ? device->getCurrentSampleRate() : 48000.0;
        int bs = device ? device->getCurrentBufferSizeSamples() : 128;
        Log::audit("PRESET", "Cache check: slot=" + juce::String(slotIndex)
            + " sr=" + juce::String(static_cast<int>(sr)) + " bs=" + juce::String(bs));
        auto cached = preloadCache_.take(slotIndex, sr, bs);
        // Re-read fresh state from file (for both validation and state refresh)
        std::vector<TargetPlugin> freshTargets;
        if (cached) {
            Log::audit("PRESET", "Cache hit: " + juce::String(static_cast<int>(cached->entries.size()))
                + " entries, cacheSR=" + juce::String(static_cast<int>(cached->sampleRate))
                + " cacheBS=" + juce::String(cached->blockSize));

            freshTargets = parseSlotFile(slotIndex);

            // Structural mismatch: cache has different plugin count than file.
            // This happens when plugins were added/removed/moved after the cache
            // was populated. Discard stale cache and fall through to slow path.
            if (freshTargets.size() != cached->entries.size()) {
                Log::audit("PRESET", "Cache stale: cached=" + juce::String(static_cast<int>(cached->entries.size()))
                    + " file=" + juce::String(static_cast<int>(freshTargets.size()))
                    + " - discarding cache, using slow path");
                cached.reset();  // destroy stale instances (we're on message thread, safe)
                // Fall through to slow/async path below
            }
        }
        if (cached) {
            // Request preload stop (non-blocking) thread will finish current plugin then exit
            preloadCache_.requestCancel();

            std::vector<VSTChain::PreloadedPlugin> preloaded;
            for (auto& ce : cached->entries) {
                VSTChain::PreloadedPlugin pp;
                pp.instance = std::move(ce.instance);
                pp.request.desc = ce.desc;
                pp.request.name = ce.name;
                pp.request.path = ce.path;
                pp.request.bypassed = ce.bypassed;

                // Use fresh state from file if available (matches by name)
                bool foundFresh = false;
                for (auto& ft : freshTargets) {
                    if (ft.name == ce.name && ft.path == ce.path) {
                        pp.request.stateData = std::move(ft.stateData);
                        pp.request.hasState = ft.hasState;
                        pp.request.bypassed = ft.bypassed;
                        foundFresh = true;
                        break;
                    }
                }
                if (!foundFresh) {
                    pp.request.stateData = std::move(ce.stateData);
                    pp.request.hasState = ce.hasState;
                }
                preloaded.push_back(std::move(pp));
            }

            int expectedCount = static_cast<int>(preloaded.size());
            juce::Logger::writeToLog("[PRESET] Slot " + juce::String::charToString(slotLabel(slotIndex))
                + ": cache hit (" + juce::String(expectedCount) + " plugins)");

            chain.replaceChainWithPreloaded(std::move(preloaded),
                [this, slotIndex, expectedCount, onComplete]() {
                    activeSlot_ = slotIndex;
                    bool allLoaded = (engine_.getVSTChain().getPluginCount() == expectedCount);
                    if (onComplete) onComplete(allLoaded);
                });

            // Defer preload restart triggerPreload is non-blocking (old thread
            // joined on new preload thread, not message thread).
            auto alivePreload = alive_;
            juce::MessageManager::callAsync([this, alivePreload]() {
                if (!alivePreload->load()) return;
                if (suppressPreload_.load()) return;  // async load in progress, skip
                triggerPreload();
            });
            return;
        }
    }

    // Slow path: cache miss need formatManager on background thread
    Log::audit("PRESET", "Cache miss for slot " + juce::String(slotIndex) + " - using async load path");
    // Suppress deferred triggerPreloads from earlier cache-hit switches.
    // Without this, those deferred callAsyncs start new preload threads
    // that block the loadThread's preWork (joinPreloadThread).
    suppressPreload_ = true;
    // Signal preload to stop (non-blocking). The load thread will join it
    // before using formatManager, keeping the message thread responsive.
    preloadCache_.requestCancel();

    // Build load requests from targets
    std::vector<VSTChain::PluginLoadRequest> requests;
    for (auto& t : targets) {
        VSTChain::PluginLoadRequest req;
        req.desc = t.desc;
        req.name = t.name;
        req.path = t.path;
        req.bypassed = t.bypassed;
        req.stateData = std::move(t.stateData);
        req.hasState = t.hasState;
        req.builtinType = t.type;

        // VST plugins: resolve description from known plugins list
        if (t.type == PluginSlot::Type::VST && !t.hasDesc) {
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

    // Async load (non-blocking plugins loaded on background thread)
    juce::Logger::writeToLog("[PRESET] Slot " + juce::String::charToString(slotLabel(slotIndex))
        + ": full reload (" + juce::String(requests.size()) + " plugins)");

    int slot = slotIndex;
    int expectedCount = static_cast<int>(requests.size());
    auto aliveFlag = alive_;
    chain.replaceChainAsync(std::move(requests),
        [this, slot, expectedCount, onComplete, aliveFlag]() {
            if (!aliveFlag->load()) return;
            activeSlot_ = slot;
            // Report failure if some plugins failed to load (prevents auto-save of incomplete chain)
            bool allLoaded = (engine_.getVSTChain().getPluginCount() == expectedCount);
            if (!allLoaded)
                juce::Logger::writeToLog("[PRESET] Partial load: " + juce::String(engine_.getVSTChain().getPluginCount())
                    + "/" + juce::String(expectedCount) + " plugins loaded");
            suppressPreload_ = false;  // allow deferred triggerPreloads again
            if (onComplete) onComplete(allLoaded);
            triggerPreload();
        },
        // preWork: cancel + join preload thread on background thread (avoids blocking message thread)
        [this]() {
            preloadCache_.requestCancel();
            preloadCache_.joinPreloadThread();
        });
}

bool PresetManager::copySlot(int fromSlot, int toSlot)
{
    if (fromSlot < 0 || fromSlot >= kNumSlots) return false;
    if (toSlot < 0 || toSlot >= kNumSlots) return false;
    if (fromSlot == toSlot) return false;

    auto srcFile = getSlotFile(fromSlot);
    auto dstFile = getSlotFile(toSlot);

    // Empty source clear destination
    if (!srcFile.existsAsFile()) {
        if (dstFile.existsAsFile())
            dstFile.deleteFile();
        dstFile.withFileExtension("dppreset.backup").deleteFile();
        preloadCache_.invalidateSlot(toSlot);
        slotOccupiedCache_[static_cast<size_t>(toSlot)] = false;
        slotNames_[static_cast<size_t>(toSlot)] = {};
        juce::Logger::writeToLog("[PRESET] Copied empty slot "
            + juce::String::charToString(slotLabel(fromSlot)) + " -> " + juce::String::charToString(slotLabel(toSlot)) + " (cleared)");
        return true;
    }

    bool ok = srcFile.copyFileTo(dstFile);
    if (ok) {
        // Remove stale backup of destination (it no longer matches the copied data)
        dstFile.withFileExtension("dppreset.backup").deleteFile();
        preloadCache_.invalidateSlot(toSlot);
        slotOccupiedCache_[static_cast<size_t>(toSlot)] = true;
        slotNames_[static_cast<size_t>(toSlot)] = slotNames_[static_cast<size_t>(fromSlot)];
        juce::Logger::writeToLog("[PRESET] Copied slot "
            + juce::String::charToString(slotLabel(fromSlot)) + " -> " + juce::String::charToString(slotLabel(toSlot)));
    }
    return ok;
}

bool PresetManager::deleteSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;

    auto file = getSlotFile(slotIndex);
    if (!file.existsAsFile()) return false;

    bool ok = file.deleteFile();
    if (ok) {
        // Also remove backup file
        file.withFileExtension("dppreset.backup").deleteFile();
        // If we deleted the active slot, clear activeSlot
        if (activeSlot_ == slotIndex)
            activeSlot_ = -1;
        preloadCache_.invalidateSlot(slotIndex);
        slotOccupiedCache_[static_cast<size_t>(slotIndex)] = false;
        slotNames_[static_cast<size_t>(slotIndex)] = {};
        juce::Logger::writeToLog("[PRESET] Deleted slot " + juce::String::charToString(slotLabel(slotIndex)));
    }
    return ok;
}

bool PresetManager::isSlotOccupied(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;
    return slotOccupiedCache_[static_cast<size_t>(slotIndex)];
}

bool PresetManager::querySlotOccupied(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return false;
    auto file = getSlotFile(slotIndex);
    return file.existsAsFile() || file.withFileExtension("dppreset.backup").existsAsFile();
}

void PresetManager::refreshSlotOccupancyCache()
{
    for (int i = 0; i < kNumSlots; ++i)
        slotOccupiedCache_[static_cast<size_t>(i)] = querySlotOccupied(i);
}

void PresetManager::triggerPreload(std::function<void()> onComplete)
{
    auto& chain = engine_.getVSTChain();
    double sr = 48000.0;  // will be overridden from chain
    int bs = 128;

    // Get current SR/BS from the engine's audio device
    auto* device = engine_.getDeviceManager().getCurrentAudioDevice();
    if (device) {
        sr = device->getCurrentSampleRate();
        bs = device->getCurrentBufferSizeSamples();
    }

    preloadCache_.preloadAllSlots(
        activeSlot_, sr, bs,
        chain.getFormatManager(),
        chain.getKnownPlugins(),
        [](int slotIndex) -> juce::String {
            auto file = getSlotFile(slotIndex);
            if (file.existsAsFile())
                return file.loadFileAsString();
            return {};
        },
        std::move(onComplete));
}

void PresetManager::invalidatePreloadCache()
{
    preloadCache_.invalidateAll();
}

// Slot Names

juce::String PresetManager::getSlotName(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return {};
    return slotNames_[static_cast<size_t>(slotIndex)];
}

void PresetManager::setSlotName(int slotIndex, const juce::String& name)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return;
    if (slotIndex == 5) return;  // Auto slot (index 5) name is fixed - not renameable
    slotNames_[static_cast<size_t>(slotIndex)] = name.trim();

    // Persist name to slot file (re-save if file exists)
    auto file = getSlotFile(slotIndex);
    if (file.existsAsFile()) {
        auto json = file.loadFileAsString();
        auto parsed = juce::JSON::parse(json);
        if (auto* obj = parsed.getDynamicObject()) {
            if (name.trim().isEmpty())
                obj->removeProperty("name");
            else
                obj->setProperty("name", name.trim());
            if (!atomicWriteFile(file, juce::JSON::toString(juce::var(obj), true)))
                Log::warn("PRESET", "Failed to save slot name");
        }
    }
    juce::Logger::writeToLog("[PRESET] Renamed slot " + juce::String::charToString(slotLabel(slotIndex))
        + " -> \"" + name.trim() + "\"");
}

juce::String PresetManager::getSlotDisplayName(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return {};
    // Auto slot (index 5) always displays as "Auto"
    if (slotIndex == 5) return "Auto";
    auto label = juce::String::charToString(slotLabel(slotIndex));
    auto name = slotNames_[static_cast<size_t>(slotIndex)];
    if (name.isEmpty()) return label;
    // Truncate to ~8 chars for button fit
    if (name.length() > 8)
        name = name.substring(0, 8) + "..";
    return label + "|" + name;
}

void PresetManager::loadSlotNames()
{
    for (int i = 0; i < kNumSlots; ++i) {
        auto file = getSlotFile(i);
        if (!file.existsAsFile()) continue;
        auto json = file.loadFileAsString();
        auto parsed = juce::JSON::parse(json);
        if (auto* root = parsed.getDynamicObject()) {
            if (root->hasProperty("name"))
                slotNames_[static_cast<size_t>(i)] = root->getProperty("name").toString();
        }
    }
}

// Slot Export/Import

void PresetManager::exportSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) return;

    // If this is the active slot, save current state first
    if (activeSlot_ == slotIndex)
        saveSlot(slotIndex);

    auto srcFile = getSlotFile(slotIndex);
    if (!srcFile.existsAsFile()) return;

    auto defaultName = "slot_" + juce::String::charToString(slotLabel(slotIndex));
    auto name = getSlotName(slotIndex);
    if (name.isNotEmpty())
        defaultName = name;

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Slot " + juce::String::charToString(slotLabel(slotIndex)),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(defaultName + ".dpchain"),
        "*.dpchain");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles,
                         [srcFile, slotIndex, chooser](const juce::FileChooser& fc) {
        auto dest = fc.getResult();
        if (dest == juce::File()) return;
        if (!dest.hasFileExtension(".dpchain"))
            dest = dest.withFileExtension(".dpchain");
        srcFile.copyFileTo(dest);
        juce::Logger::writeToLog("[PRESET] Exported slot "
            + juce::String::charToString(PresetManager::slotLabel(slotIndex))
            + " to " + dest.getFileName());
    });
}

void PresetManager::importSlot(int slotIndex, std::function<void(bool)> onComplete)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots) {
        if (onComplete) onComplete(false);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Import to Slot " + juce::String::charToString(slotLabel(slotIndex)),
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.dpchain;*.dppreset");

    auto aliveFlag = alive_;
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles,
                         [this, slotIndex, aliveFlag, onComplete, chooser](const juce::FileChooser& fc) {
        if (!aliveFlag->load()) return;
        auto srcFile = fc.getResult();
        if (srcFile == juce::File() || !srcFile.existsAsFile()) {
            if (onComplete) onComplete(false);
            return;
        }

        // Validate: must be a valid chain preset
        auto json = srcFile.loadFileAsString();
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

        // Copy file to slot location
        auto dstFile = getSlotFile(slotIndex);
        if (!srcFile.copyFileTo(dstFile)) {
            if (onComplete) onComplete(false);
            return;
        }
        dstFile.withFileExtension("dppreset.backup").deleteFile();
        preloadCache_.invalidateSlot(slotIndex);
        slotOccupiedCache_[static_cast<size_t>(slotIndex)] = true;

        // Read name from imported file
        if (root->hasProperty("name"))
            slotNames_[static_cast<size_t>(slotIndex)] = root->getProperty("name").toString();
        else
            slotNames_[static_cast<size_t>(slotIndex)] = {};

        juce::Logger::writeToLog("[PRESET] Imported slot "
            + juce::String::charToString(slotLabel(slotIndex))
            + " from " + srcFile.getFileName());

        // If this is the active slot, reload it synchronously
        // (async path has UI refresh issues; sync is fine for user-initiated import)
        if (activeSlot_ == slotIndex) {
            bool loaded = loadSlot(slotIndex);
            if (onComplete) onComplete(loaded);
        } else {
            if (onComplete) onComplete(true);
        }
    });
}

} // namespace directpipe
