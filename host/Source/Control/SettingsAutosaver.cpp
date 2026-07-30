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
 * @file SettingsAutosaver.cpp
 * @brief Dirty-flag auto-save implementation
 */

#include "SettingsAutosaver.h"
#include "../Audio/AudioEngine.h"
#include "../UI/PresetManager.h"
#include "../Util/AtomicFileIO.h"
#include "Log.h"

namespace directpipe {
namespace {
bool presetJSONDeclaresOutputMuted(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (auto* root = parsed.getDynamicObject())
        return root->hasProperty("outputMuted");
    return false;
}

juce::Array<juce::var> channelMaskToIndexArray(
    const juce::BigInteger& mask)
{
    juce::Array<juce::var> result;
    for (int bit = mask.findNextSetBit(0);
         bit >= 0;
         bit = mask.findNextSetBit(bit + 1)) {
        result.add(bit);
    }
    return result;
}

juce::String exportNonPluginSettings(PresetManager& presetMgr,
                                     AudioEngine& engine)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    root->setProperty("activeSlot", presetMgr.getActiveSlot());
    root->setProperty("sampleRate", engine.getDesiredSampleRate());
    root->setProperty("bufferSize", engine.getDesiredBufferSize());
    root->setProperty(
        "inputGain", static_cast<double>(engine.getInputGain()));
    root->setProperty("muted", engine.isMuted());
    root->setProperty("deviceType", engine.getDesiredDeviceType());
    root->setProperty("inputDevice", engine.getDesiredInputDevice());
    root->setProperty("outputDevice", engine.getDesiredOutputDevice());
    root->setProperty("outputNone", engine.isOutputNone());

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine.getDeviceManager().getAudioDeviceSetup(setup);
    root->setProperty(
        "inputChannelMask", channelMaskToIndexArray(setup.inputChannels));
    root->setProperty(
        "outputChannelMask", channelMaskToIndexArray(setup.outputChannels));

    auto& router = engine.getOutputRouter();
    auto outputs = std::make_unique<juce::DynamicObject>();
    outputs->setProperty(
        "monitorVolume",
        static_cast<double>(
            router.getVolume(OutputRouter::Output::Monitor)));
    outputs->setProperty(
        "outputVolume",
        static_cast<double>(
            router.getVolume(OutputRouter::Output::Main)));
    outputs->setProperty(
        "monitorEnabled",
        router.isEnabled(OutputRouter::Output::Monitor));
    outputs->setProperty("monitorDevice", engine.getMonitorDeviceName());
    outputs->setProperty(
        "monitorBufferSize", engine.getMonitorBufferSize());
    root->setProperty("outputs", juce::var(outputs.release()));

    root->setProperty("channelMode", engine.getChannelMode());
    root->setProperty("ipcEnabled", engine.isIpcEnabled());
    root->setProperty(
        "outputMuted", engine.isOutputManuallyMuted());
    root->setProperty("auditMode", Log::isAuditMode());

    auto limiter = std::make_unique<juce::DynamicObject>();
    limiter->setProperty(
        "enabled", engine.getSafetyLimiter().isEnabled());
    limiter->setProperty(
        "ceiling_dB",
        static_cast<double>(
            engine.getSafetyLimiter().getCeilingdB()));
    limiter->setProperty(
        "headroom_enabled", engine.isSafetyHeadroomEnabled());
    limiter->setProperty(
        "headroom_dB",
        static_cast<double>(engine.getSafetyHeadroomdB()));
    root->setProperty(
        "safetyLimiter", juce::var(limiter.release()));

    if (presetMgr.onExportAppSettings)
        presetMgr.onExportAppSettings(*root);

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool isValidNonPluginSettings(const juce::String& json)
{
    const auto parsed = juce::JSON::parse(json);
    auto* root = parsed.getDynamicObject();
    return root != nullptr
        && static_cast<int>(root->getProperty("version")) >= 1
        && !root->hasProperty("plugins");
}

juce::String mergeNonPluginSettings(const juce::String& baseJSON,
                                    const juce::String& overlayJSON)
{
    auto merged = juce::JSON::parse(baseJSON);
    auto overlay = juce::JSON::parse(overlayJSON);
    auto* mergedRoot = merged.getDynamicObject();
    auto* overlayRoot = overlay.getDynamicObject();
    if (!mergedRoot || !overlayRoot || overlayRoot->hasProperty("plugins"))
        return {};

    const auto& properties = overlayRoot->getProperties();
    for (int i = 0; i < properties.size(); ++i) {
        const auto name = properties.getName(i);
        if (name != juce::Identifier("plugins"))
            mergedRoot->setProperty(name, properties.getValueAt(i));
    }
    return juce::JSON::toString(merged, true);
}

bool clearShutdownRecoveryFamily(const juce::File& recoveryFile)
{
    const juce::File files[] {
        recoveryFile,
        recoveryFile.getSiblingFile(
            recoveryFile.getFileName() + ".bak"),
        recoveryFile.withFileExtension(
            recoveryFile.getFileExtension() + ".backup"),
        recoveryFile.getSiblingFile(
            recoveryFile.getFileName() + ".tmp")
    };

    bool allDeleted = true;
    for (const auto& file : files) {
        if (file.existsAsFile() && !file.deleteFile()) {
            allDeleted = false;
            juce::Logger::writeToLog(
                "[PRESET] Could not remove shutdown recovery file: "
                + file.getFileName());
        }
    }
    return allDeleted;
}

juce::String loadShutdownRecoverySettings(
    const juce::File& recoveryFile)
{
    const auto json = loadFileWithBackupFallback(recoveryFile);
    if (json.isEmpty())
        return {};
    if (isValidNonPluginSettings(json))
        return json;

    juce::Logger::writeToLog(
        "[PRESET] Ignoring invalid shutdown settings recovery sidecar");
    return {};
}
} // namespace

SettingsAutosaver::SettingsAutosaver(PresetManager& presetMgr, AudioEngine& engine,
                                     std::atomic<bool>& loadingSlot,
                                     std::atomic<bool>& partialLoad)
    : presetMgr_(presetMgr),
      engine_(engine),
      loadingSlot_(loadingSlot),
      partialLoad_(partialLoad)
{
}

SettingsAutosaver::~SettingsAutosaver()
{
    alive_->store(false);
}

juce::File SettingsAutosaver::getShutdownRecoveryFile()
{
    const auto settingsFile = PresetManager::getAutoSaveFile();
    return settingsFile.getSiblingFile(
        "settings.shutdown-recovery.json");
}

bool SettingsAutosaver::deleteShutdownRecoveryFiles()
{
    return clearShutdownRecoveryFamily(
        getShutdownRecoveryFile());
}

void SettingsAutosaver::markDirty()
{
    dirty_ = true;
    cooldown_ = 30;  // reset debounce: save after ~1 second of inactivity
    if (waitingForCompletePluginChain_)
        missingChainRetryTicks_ = kMissingChainInitialRetryTicks;
}

void SettingsAutosaver::tick()
{
    if (!dirty_ || cooldown_ <= 0) return;

    // Once a later load completes, do not leave pending settings behind the
    // longer missing-chain backoff.
    if (waitingForCompletePluginChain_ && !partialLoad_.load()) {
        waitingForCompletePluginChain_ = false;
        missingCompletePluginChainLogged_ = false;
        missingChainRetryTicks_ = kMissingChainInitialRetryTicks;
        cooldown_ = 1;
    }

    if (--cooldown_ == 0) {
        // Defer save if chain is in transitional state (async loading or not yet prepared)
        if (loadingSlot_.load() || !engine_.getVSTChain().isStable()) {
            if (++deferCount_ >= kMaxDeferCount) {
                // The chain is still transitional, so forcing saveNow() would
                // immediately be rejected by the same stability guard. Keep
                // the dirty flag pending until a later stable tick instead of
                // silently discarding the user's changes.
                juce::Logger::writeToLog("[PRESET] Autosave still deferred after "
                    + juce::String(kMaxDeferCount) + " attempts; keeping changes pending");
                deferCount_ = 0;
                cooldown_ = 10;
            } else {
                cooldown_ = 10;  // retry in ~300ms
            }
        } else {
            deferCount_ = 0;
            if (saveNow()) {
                dirty_ = false;
            } else if (lastSaveDeferredForMissingChain_) {
                // Keep probing so a recovered primary/backup chain is picked
                // up without another UI edit, but back off to a bounded 30s.
                cooldown_ = missingChainRetryTicks_;
                missingChainRetryTicks_ = juce::jmin(
                    missingChainRetryTicks_ * 2,
                    kMissingChainMaxRetryTicks);
            } else {
                // A transient filesystem failure must not acknowledge the
                // pending change. Retry without requiring another UI edit.
                cooldown_ = 10;
                juce::Logger::writeToLog(
                    "[PRESET] Autosave failed; keeping changes pending");
            }
        }
    }
}

bool SettingsAutosaver::saveNow()
{
    lastSaveDeferredForMissingChain_ = false;

    // Skip saving during an async chain transition or before the graph is
    // prepared. A completed partial load is handled separately below.
    if (loadingSlot_.load() || !engine_.getVSTChain().isStable())
        return false;

    auto file = PresetManager::getAutoSaveFile();
    if (partialLoad_.load()) {
        const auto preservedJSON =
            PresetManager::loadPresetJSONWithBackupFallback(file, true);
        auto preserved = juce::JSON::parse(preservedJSON);
        auto current = juce::JSON::parse(presetMgr_.exportToJSON());
        auto* preservedRoot = preserved.getDynamicObject();
        auto* currentRoot = current.getDynamicObject();
        auto* preservedPlugins = preservedRoot
            ? preservedRoot->getProperty("plugins").getArray()
            : nullptr;
        if (!preservedRoot || !preservedPlugins) {
            waitingForCompletePluginChain_ = true;
            lastSaveDeferredForMissingChain_ = true;
            if (!missingCompletePluginChainLogged_) {
                juce::Logger::writeToLog(
                    "[PRESET] Partial-load settings save deferred: complete persisted plugin chain unavailable");
                missingCompletePluginChainLogged_ = true;
            }
            return false;
        }

        waitingForCompletePluginChain_ = false;
        missingCompletePluginChainLogged_ = false;
        missingChainRetryTicks_ = kMissingChainInitialRetryTicks;
        if (!currentRoot) {
            juce::Logger::writeToLog(
                "[PRESET] Partial-load settings save failed: current settings export is invalid");
            return false;
        }

        currentRoot->setProperty("plugins", preservedRoot->getProperty("plugins"));
        const bool settingsSaved =
            atomicWriteFile(file, juce::JSON::toString(current));
        if (!settingsSaved) {
            juce::Logger::writeToLog(
                "[PRESET] Partial-load settings save failed; preserved plugin chain remains authoritative");
        } else {
            clearShutdownRecoveryFamily(getShutdownRecoveryFile());
        }
        return settingsSaved;
    }

    waitingForCompletePluginChain_ = false;
    missingCompletePluginChainLogged_ = false;
    missingChainRetryTicks_ = kMissingChainInitialRetryTicks;

    // Save current slot's chain state (captures plugin internal state)
    int currentSlot = presetMgr_.getActiveSlot();
    bool slotSaved = true;
    if (currentSlot >= 0)
        slotSaved = presetMgr_.saveSlot(currentSlot);

    const bool settingsSaved = presetMgr_.savePreset(file);
    if (settingsSaved)
        clearShutdownRecoveryFamily(getShutdownRecoveryFile());
    return slotSaved && settingsSaved;
}

bool SettingsAutosaver::flushForShutdown()
{
    if (saveNow()) {
        dirty_ = false;
        return true;
    }

    const auto nonPluginJSON =
        exportNonPluginSettings(presetMgr_, engine_);
    if (!isValidNonPluginSettings(nonPluginJSON)) {
        juce::Logger::writeToLog(
            "[PRESET] Shutdown settings flush failed: current non-plugin settings are invalid");
        return false;
    }

    const auto settingsFile = PresetManager::getAutoSaveFile();
    const auto completePresetJSON =
        PresetManager::loadPresetJSONWithBackupFallback(
            settingsFile, true);
    if (completePresetJSON.isNotEmpty()) {
        const auto merged = mergeNonPluginSettings(
            completePresetJSON, nonPluginJSON);
        if (merged.isNotEmpty()
            && atomicWriteFile(settingsFile, merged)) {
            clearShutdownRecoveryFamily(
                getShutdownRecoveryFile());
            dirty_ = false;
            juce::Logger::writeToLog(
                "[PRESET] Shutdown settings merged with preserved complete plugin chain");
            return true;
        }

        juce::Logger::writeToLog(
            "[PRESET] Shutdown merge could not update the full preset; falling back to recovery sidecar");
    }

    const auto recoveryFile = getShutdownRecoveryFile();
    if (!atomicWriteFile(recoveryFile, nonPluginJSON)) {
        juce::Logger::writeToLog(
            "[PRESET] Shutdown settings flush failed: recovery sidecar could not be written");
        return false;
    }

    dirty_ = false;
    juce::Logger::writeToLog(
        "[PRESET] Shutdown settings saved to recovery sidecar; plugin chain files were left unchanged");
    return true;
}

void SettingsAutosaver::loadFromFile()
{
    if (!engine_.isOutputNone())
        engine_.setOutputMuted(true); // Startup guard: prevent transient default-driver output.

    auto file = PresetManager::getAutoSaveFile();
    // Resolve the whole atomic-write family before deciding that settings are
    // absent. A crash can legitimately leave only settings.dppreset.bak, and
    // a locked/corrupt primary must not hide explicit safety fields in backup.
    auto recoveredPresetJSON =
        PresetManager::loadPresetJSONWithBackupFallback(file);
    const auto recoveryFile = getShutdownRecoveryFile();
    const auto pendingNonPluginJSON =
        loadShutdownRecoverySettings(recoveryFile);
    bool loadRecoveredJSONFromMemory = false;
    if (pendingNonPluginJSON.isNotEmpty()) {
        const auto completePresetJSON =
            PresetManager::loadPresetJSONWithBackupFallback(
                file, true);
        const auto baseJSON = completePresetJSON.isNotEmpty()
            ? completePresetJSON
            : recoveredPresetJSON;
        const auto mergedJSON = baseJSON.isNotEmpty()
            ? mergeNonPluginSettings(
                baseJSON, pendingNonPluginJSON)
            : pendingNonPluginJSON;

        if (mergedJSON.isNotEmpty()) {
            recoveredPresetJSON = mergedJSON;
            loadRecoveredJSONFromMemory = true;
            if (completePresetJSON.isNotEmpty()) {
                if (atomicWriteFile(file, mergedJSON)) {
                    clearShutdownRecoveryFamily(recoveryFile);
                    juce::Logger::writeToLog(
                        "[PRESET] Recovered shutdown settings into complete preset");
                } else {
                    juce::Logger::writeToLog(
                        "[PRESET] Shutdown settings recovery is active in memory; sidecar retained for retry");
                }
            } else {
                juce::Logger::writeToLog(
                    "[PRESET] Loaded shutdown settings recovery sidecar without changing plugin chain files");
            }
        }
    }

    if (recoveredPresetJSON.isNotEmpty()) {
        loadingSlot_ = true;
        const bool loaded = loadRecoveredJSONFromMemory
            ? presetMgr_.importFromJSON(recoveredPresetJSON)
            : presetMgr_.loadPreset(file);
        if (loadRecoveredJSONFromMemory) {
            juce::Logger::writeToLog(
                loaded
                    ? "[PRESET] Loaded recovered shutdown settings"
                    : "[PRESET] Failed to restore recovered shutdown settings");
        }
        partialLoad_ = !loaded;
        const bool hasOutputMutedField =
            presetJSONDeclaresOutputMuted(recoveredPresetJSON);

        // Self-healing: if settings.dppreset had an empty/corrupt chain but the
        // active slot file is valid, reload chain from the slot file.
        int activeSlot = presetMgr_.getActiveSlot();
        if (activeSlot >= 0 && presetMgr_.isSlotOccupied(activeSlot)
            && engine_.getVSTChain().getPluginCount() == 0) {
            juce::Logger::writeToLog("[PRESET] Self-healing: reloading slot "
                + juce::String::charToString(PresetManager::slotLabel(activeSlot)) + " from file");
            bool ok = presetMgr_.loadSlot(activeSlot);
            partialLoad_ = !ok;  // clear on success, set on failure
        }

        loadingSlot_ = false;

        // Legacy preset may omit outputMuted; use startup default (unmuted).
        // An explicit mute remains authoritative even when a plugin only
        // partially loads, because unmuting on recovery is unsafe.
        if (!hasOutputMutedField && !engine_.isOutputNone())
            engine_.setOutputMuted(false);

        // Restore panic mute lockout (monitor/IPC disabled while muted)
        if (onRestorePanicMute) onRestorePanicMute();

        if (onPostLoad) onPostLoad();
        if (onFlushLogs) onFlushLogs();

        // Pre-load other slots in background for instant switching.
        // Deferred via callAsync to ensure audio device is fully started
        // before preload thread uses formatManager.
        // Window is shown AFTER preload completes (prevents "frozen UI" appearance).
        auto showWindowCb = onShowWindow;
        auto* pm = &presetMgr_;
        auto aliveFlag = alive_;
        juce::MessageManager::callAsync([pm, showWindowCb, aliveFlag]() {
            if (!aliveFlag->load()) return;
            pm->triggerPreload([showWindowCb, aliveFlag]() {
                if (aliveFlag->load() && showWindowCb) showWindowCb();
            });
        });
        return;  // window will be shown by preload callback
    }

    // This path is also used after Factory Reset. The caller may already have
    // claimed the loading flag, so every no-settings exit must release it.
    partialLoad_ = false;
    loadingSlot_ = false;

    if (!engine_.isOutputNone())
        engine_.setOutputMuted(false);

    // No settings file: show window immediately
    auto showWindowCb = onShowWindow;
    auto aliveFlag = alive_;
    juce::MessageManager::callAsync([showWindowCb, aliveFlag]() {
        if (aliveFlag->load() && showWindowCb) showWindowCb();
    });
}

} // namespace directpipe
