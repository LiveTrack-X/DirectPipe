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
 * @file SettingsExporter.cpp
 * @brief Settings export/import implementation
 */

#include "SettingsExporter.h"
#include "../Control/Log.h"
#include "../Util/AtomicFileIO.h"

#include <cmath>
#include <limits>
#include <vector>

namespace directpipe {
namespace {

void stripSettingsOnlyPresetState(juce::DynamicObject& audioObj)
{
    audioObj.removeProperty("plugins");
    audioObj.removeProperty("activeSlot");
}

juce::File slotBakFileFor(const juce::File& file)
{
    return file.getSiblingFile(file.getFileName() + ".bak");
}

juce::File legacySlotBackupFileFor(const juce::File& file)
{
    return file.withFileExtension(file.getFileExtension() + ".backup");
}

juce::File slotTempFileFor(const juce::File& file)
{
    return file.getSiblingFile(file.getFileName() + ".tmp");
}

juce::File getLegacyNumericSlotFile(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= PresetManager::kNumSlots || slotIndex == 5)
        return {};

    auto dir = ControlMappingStore::getConfigDirectory().getChildFile("Slots");
    return dir.getChildFile("slot_" + juce::String(static_cast<int>(PresetManager::slotLabel(slotIndex))) + ".dppreset");
}

bool deleteFileIfPresent(const juce::File& file)
{
    return !file.existsAsFile() || file.deleteFile();
}

bool deleteSlotRestoreBackups(const juce::File& file)
{
    bool ok = true;
    ok = deleteFileIfPresent(slotBakFileFor(file)) && ok;
    ok = deleteFileIfPresent(legacySlotBackupFileFor(file)) && ok;
    ok = deleteFileIfPresent(slotTempFileFor(file)) && ok;
    return ok;
}

bool deleteSlotRestoreFileFamily(const juce::File& file)
{
    bool ok = true;
    ok = deleteFileIfPresent(file) && ok;
    ok = deleteSlotRestoreBackups(file) && ok;
    return ok;
}

bool deleteSlotRestoreBackups(int slotIndex)
{
    bool ok = deleteSlotRestoreBackups(PresetManager::getSlotFile(slotIndex));

    auto legacyFile = getLegacyNumericSlotFile(slotIndex);
    if (legacyFile != juce::File())
        ok = deleteSlotRestoreFileFamily(legacyFile) && ok;

    return ok;
}

bool deleteSlotRestoreFileFamily(int slotIndex)
{
    bool ok = deleteSlotRestoreFileFamily(PresetManager::getSlotFile(slotIndex));

    auto legacyFile = getLegacyNumericSlotFile(slotIndex);
    if (legacyFile != juce::File())
        ok = deleteSlotRestoreFileFamily(legacyFile) && ok;

    return ok;
}

struct SlotRestoreEntry {
    int slotIndex = -1;
    juce::String key;
    bool presentInBackup = false;
    juce::String json;
};

struct FileSnapshot {
    juce::File file;
    bool existed = false;
    juce::MemoryBlock data;
};

void appendRestoreFileFamily(std::vector<juce::File>& files, const juce::File& file)
{
    if (file == juce::File())
        return;

    files.push_back(file);
    files.push_back(slotBakFileFor(file));
    files.push_back(legacySlotBackupFileFor(file));
    files.push_back(slotTempFileFor(file));
}

std::vector<juce::File> getSlotRestoreFileFamily(int slotIndex)
{
    std::vector<juce::File> files;
    files.reserve(8);

    appendRestoreFileFamily(files, PresetManager::getSlotFile(slotIndex));

    auto legacyFile = getLegacyNumericSlotFile(slotIndex);
    if (legacyFile != juce::File())
        appendRestoreFileFamily(files, legacyFile);

    return files;
}

bool captureFileSnapshot(const juce::File& file, FileSnapshot& snapshot)
{
    snapshot.file = file;
    snapshot.existed = file.existsAsFile();
    snapshot.data.reset();

    if (!snapshot.existed)
        return true;

    if (file.loadFileAsData(snapshot.data))
        return true;

    Log::warn("APP", "Failed to snapshot restore file: " + file.getFileName());
    return false;
}

bool restoreFileSnapshot(const FileSnapshot& snapshot)
{
    if (!snapshot.existed)
        return deleteFileIfPresent(snapshot.file);

    auto parent = snapshot.file.getParentDirectory();
    if (!parent.exists() && !parent.createDirectory())
        return false;

    if (snapshot.data.getSize() == 0)
        return snapshot.file.replaceWithText({});

    return snapshot.file.replaceWithData(snapshot.data.getData(), snapshot.data.getSize());
}

bool captureSlotRestoreSnapshots(const std::vector<SlotRestoreEntry>& plan,
                                 std::vector<FileSnapshot>& snapshots)
{
    snapshots.clear();
    snapshots.reserve(plan.size() * 8);

    for (const auto& entry : plan) {
        for (const auto& file : getSlotRestoreFileFamily(entry.slotIndex)) {
            FileSnapshot snapshot;
            if (!captureFileSnapshot(file, snapshot))
                return false;
            snapshots.push_back(std::move(snapshot));
        }
    }

    return true;
}

bool captureControlConfigSnapshots(std::vector<FileSnapshot>& snapshots)
{
    std::vector<juce::File> files;
    files.reserve(4);
    appendRestoreFileFamily(files, ControlMappingStore::getDefaultConfigFile());

    snapshots.clear();
    snapshots.reserve(files.size());
    for (const auto& file : files) {
        FileSnapshot snapshot;
        if (!captureFileSnapshot(file, snapshot))
            return false;
        snapshots.push_back(std::move(snapshot));
    }

    return true;
}

bool restoreFileSnapshots(const std::vector<FileSnapshot>& snapshots)
{
    bool ok = true;
    for (const auto& snapshot : snapshots) {
        if (!restoreFileSnapshot(snapshot)) {
            ok = false;
            Log::warn("APP", "Failed to roll back restore file: " + snapshot.file.getFileName());
        }
    }
    return ok;
}

bool propertyIsObjectIfPresent(const juce::DynamicObject& root,
                               const juce::Identifier& property,
                               const char* label)
{
    if (!root.hasProperty(property))
        return true;

    if (root.getProperty(property).isObject())
        return true;

    Log::warn("APP", juce::String("Invalid ") + label + " in settings backup");
    return false;
}

bool isIntegralInRange(const juce::var& value, juce::int64 minimum, juce::int64 maximum)
{
    if (!value.isInt() && !value.isInt64())
        return false;
    const auto integer = static_cast<juce::int64>(value);
    return integer >= minimum && integer <= maximum;
}

bool isValidControlMappingArray(const juce::var& value, bool midi)
{
    auto* entries = value.getArray();
    if (!entries)
        return false;

    for (const auto& entry : *entries) {
        auto* object = entry.getDynamicObject();
        if (!object)
            return false;

        const bool requiredFieldsPresent = midi
            ? object->hasProperty("cc")
                && object->hasProperty("note")
                && object->hasProperty("channel")
                && object->hasProperty("type")
            : object->hasProperty("modifiers")
                && object->hasProperty("virtualKey");
        auto* action = object->getProperty("action").getDynamicObject();
        if (!requiredFieldsPresent || !action || !action->hasProperty("action"))
            return false;

        if (midi) {
            if (!isIntegralInRange(object->getProperty("cc"), -1, 127)
                || !isIntegralInRange(object->getProperty("note"), -1, 127)
                || !isIntegralInRange(object->getProperty("channel"), 0, 16)
                || !isIntegralInRange(object->getProperty("type"), 0, 3)) {
                return false;
            }
        } else if (!isIntegralInRange(object->getProperty("modifiers"), 0, 2147483647LL)
                   || !isIntegralInRange(object->getProperty("virtualKey"), 0, 2147483647LL)) {
            return false;
        }

        const auto actionProperty = action->getProperty("action");
        if (!actionProperty.isInt() && !actionProperty.isInt64())
            return false;

        const auto actionValue = static_cast<juce::int64>(actionProperty);
        if (actionValue < static_cast<juce::int64>(Action::PluginBypass)
            || actionValue > static_cast<juce::int64>(Action::AutoProcessorsAdd)) {
            return false;
        }

        if (action->hasProperty("intParam")
            && !isIntegralInRange(action->getProperty("intParam"),
                                  (std::numeric_limits<int>::min)(),
                                  (std::numeric_limits<int>::max)())) {
            return false;
        }
        if (action->hasProperty("intParam2")
            && !isIntegralInRange(action->getProperty("intParam2"),
                                  (std::numeric_limits<int>::min)(),
                                  (std::numeric_limits<int>::max)())) {
            return false;
        }
        if (action->hasProperty("floatParam")) {
            const auto floatParam = action->getProperty("floatParam");
            if (!floatParam.isDouble() && !floatParam.isInt() && !floatParam.isInt64())
                return false;
            const auto number = static_cast<double>(floatParam);
            const auto maximum = static_cast<double>((std::numeric_limits<float>::max)());
            if (!std::isfinite(number) || number < -maximum || number > maximum)
                return false;
        }
        if (action->hasProperty("stringParam")
            && !action->getProperty("stringParam").isString()) {
            return false;
        }
    }

    return true;
}

bool isValidControlConfig(const juce::var& value)
{
    auto* root = value.getDynamicObject();
    if (!root
        || !root->hasProperty("hotkeys")
        || !root->hasProperty("midi")
        || !root->hasProperty("server")
        || !isValidControlMappingArray(root->getProperty("hotkeys"), false)
        || !isValidControlMappingArray(root->getProperty("midi"), true)) {
        return false;
    }

    auto* server = root->getProperty("server").getDynamicObject();
    if (!server
        || !server->hasProperty("websocketPort")
        || !server->hasProperty("websocketEnabled")
        || !server->hasProperty("httpPort")
        || !server->hasProperty("httpEnabled")) {
        return false;
    }

    return isIntegralInRange(server->getProperty("websocketPort"), 1, 65535)
        && isIntegralInRange(server->getProperty("httpPort"), 1, 65535)
        && server->getProperty("websocketEnabled").isBool()
        && server->getProperty("httpEnabled").isBool();
}

bool presetFileFamilyExists(const juce::File& file)
{
    return file.existsAsFile()
        || file.getSiblingFile(file.getFileName() + ".bak").existsAsFile()
        || file.withFileExtension(file.getFileExtension() + ".backup").existsAsFile();
}

bool buildSlotRestorePlan(const juce::DynamicObject& slots,
                          std::vector<SlotRestoreEntry>& plan)
{
    plan.clear();
    plan.reserve(PresetManager::kNumSlots);

    for (int i = 0; i < PresetManager::kNumSlots; ++i) {
        SlotRestoreEntry entry;
        entry.slotIndex = i;
        entry.key = juce::String::charToString(PresetManager::slotLabel(i));

        if (slots.hasProperty(entry.key)) {
            auto slotVar = slots.getProperty(entry.key);
            if (!slotVar.isObject()) {
                Log::warn("APP", "Invalid slot object in full backup: " + entry.key);
                return false;
            }

            entry.presentInBackup = true;
            entry.json = juce::JSON::toString(slotVar, true);
            if (!PresetManager::isChainJSONStructurallyValid(entry.json)) {
                Log::warn("APP", "Invalid slot JSON in full backup: " + entry.key);
                return false;
            }
        }

        plan.push_back(std::move(entry));
    }

    return true;
}

} // namespace

juce::String SettingsExporter::getCurrentPlatform()
{
#if JUCE_WINDOWS
    return "windows";
#elif JUCE_MAC
    return "macos";
#elif JUCE_LINUX
    return "linux";
#else
    return "unknown";
#endif
}

juce::String SettingsExporter::getBackupPlatform(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return {};
    auto* root = parsed.getDynamicObject();
    if (!root || !root->hasProperty("platform")) return {};
    return root->getProperty("platform").toString();
}

bool SettingsExporter::isPlatformCompatible(const juce::String& json)
{
    auto backupPlatform = getBackupPlatform(json);
    if (backupPlatform.isEmpty()) return true;  // legacy backup — no platform field
    return backupPlatform == getCurrentPlatform();
}

juce::String SettingsExporter::exportAll(PresetManager& presetManager,
                                          ControlMappingStore& controlStore)
{
    juce::Logger::writeToLog("[PRESET] Export: tier=backup, platform=" + getCurrentPlatform());
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 2);
    root->setProperty("platform", getCurrentPlatform());
    root->setProperty("exportDate",
        juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("appVersion",
        juce::String(ProjectInfo::versionString));

    // Audio/output settings (strip VST chain — managed by slots only)
    auto audioJson = presetManager.exportToJSON();
    auto audioParsed = juce::JSON::parse(audioJson);
    if (!audioParsed.isObject())
        return {};
    auto* audioObj = audioParsed.getDynamicObject();
    if (audioObj)
        stripSettingsOnlyPresetState(*audioObj);
    root->setProperty("audioSettings", audioParsed);

    // Control config (hotkeys, MIDI, server)
    auto tempFile = juce::File::createTempFile("dpctrl");
    tempFile.deleteFile(); // atomicWriteFile should not rotate an empty temp placeholder to .bak
    auto controlConfig = controlStore.load();
    if (!controlStore.save(controlConfig, tempFile)) {
        tempFile.deleteFile();
        return {};
    }
    auto controlJson = tempFile.loadFileAsString();
    tempFile.deleteFile();
    auto controlParsed = juce::JSON::parse(controlJson);
    if (!controlParsed.isObject())
        return {};
    root->setProperty("controlConfig", controlParsed);

    // Preset slots NOT included — managed independently via slots A-E

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool SettingsExporter::importAll(const juce::String& json,
                                  PresetManager& presetManager,
                                  ControlMappingStore& controlStore)
{
    juce::Logger::writeToLog("[PRESET] Import: tier=backup, platform=" + getCurrentPlatform());
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    int version = root->getProperty("version");
    if (version < 1) return false;

    // Block cross-platform backup restore
    auto backupPlatform = getBackupPlatform(json);
    if (!isPlatformCompatible(json)) {
        Log::warn("APP", "Cross-platform backup restore blocked: " + backupPlatform);
        return false;
    }

    if (!propertyIsObjectIfPresent(*root, "audioSettings", "audioSettings")
        || !propertyIsObjectIfPresent(*root, "controlConfig", "controlConfig"))
        return false;
    if (root->hasProperty("controlConfig")
        && !isValidControlConfig(root->getProperty("controlConfig"))) {
        Log::warn("APP", "Invalid controlConfig schema in settings backup");
        return false;
    }

    const bool hasAudioSettings = root->hasProperty("audioSettings");
    const bool hasControlConfig = root->hasProperty("controlConfig");

    juce::String stagedControlJson;
    ControlConfig stagedControlConfig;
    if (hasControlConfig) {
        stagedControlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(stagedControlJson))
            return false;
        stagedControlConfig = controlStore.load(tempFile);
        tempFile.deleteFile();
    }

    juce::String audioSnapshot;
    if (hasAudioSettings) {
        audioSnapshot = presetManager.exportToJSON();
        if (!PresetManager::isPresetJSONStructurallyValid(audioSnapshot))
            return false;
    }

    std::vector<FileSnapshot> controlSnapshots;
    if (hasControlConfig && !captureControlConfigSnapshots(controlSnapshots))
        return false;

    auto rollbackImport = [&]() {
        bool rollbackOk = true;
        if (hasControlConfig && !restoreFileSnapshots(controlSnapshots))
            rollbackOk = false;
        if (hasAudioSettings && !presetManager.importFromJSON(audioSnapshot))
            rollbackOk = false;
        return rollbackOk;
    };

    auto failImport = [&](const juce::String& reason) {
        Log::warn("APP", reason + "; rolling back settings import");
        if (!rollbackImport())
            Log::warn("APP", "Settings import rollback incomplete");
        return false;
    };

    // Import audio/output settings (strip plugins to avoid overwriting VST chain)
    if (hasAudioSettings) {
        auto audioSettings = root->getProperty("audioSettings");
        if (auto* audioObj = audioSettings.getDynamicObject())
            stripSettingsOnlyPresetState(*audioObj);
        auto audioJson = juce::JSON::toString(audioSettings, false);
        if (!presetManager.importFromJSON(audioJson)) {
            Log::warn("APP", "Failed to import audio settings from backup");
            return failImport("Failed to import audio settings from backup");
        }
    }

    // Import control config
    if (hasControlConfig && !controlStore.save(stagedControlConfig))
        return failImport("Failed to import control config from backup");

    // Preset slots NOT imported — managed independently via slots A-E
    // (v1 backups with presetSlots are intentionally ignored)

    return true;
}

juce::String SettingsExporter::exportFullBackup(PresetManager& presetManager,
                                                  ControlMappingStore& controlStore,
                                                  bool runtimeStateIsStable)
{
    juce::Logger::writeToLog("[PRESET] Export: tier=full, platform=" + getCurrentPlatform());
    if (!runtimeStateIsStable) {
        Log::warn("APP", "Full backup blocked while preset/plugin state is incomplete or changing");
        return {};
    }

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 2);
    root->setProperty("type", "full");
    root->setProperty("platform", getCurrentPlatform());
    root->setProperty("exportDate",
        juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("appVersion",
        juce::String(ProjectInfo::versionString));

    // Keep the active quick-slot file in sync with the current chain before
    // collecting slot files for a full backup.
    auto activeSlot = presetManager.getActiveSlot();
    if (activeSlot >= 0 && activeSlot < PresetManager::kNumSlots
        && !presetManager.saveSlot(activeSlot)) {
        Log::warn("APP", "Failed to synchronize active slot before full backup");
        return {};
    }

    // Audio settings (including VST chain)
    auto audioJson = presetManager.exportToJSON();
    auto audioParsed = juce::JSON::parse(audioJson);
    if (!audioParsed.isObject())
        return {};
    root->setProperty("audioSettings", audioParsed);

    // Control config (hotkeys, MIDI, server)
    auto tempFile = juce::File::createTempFile("dpctrl");
    tempFile.deleteFile(); // avoid leaking an empty sibling .bak from the placeholder file
    auto controlConfig = controlStore.load();
    if (!controlStore.save(controlConfig, tempFile)) {
        tempFile.deleteFile();
        return {};
    }
    auto controlJson = tempFile.loadFileAsString();
    tempFile.deleteFile();
    auto controlParsed = juce::JSON::parse(controlJson);
    if (!controlParsed.isObject())
        return {};
    root->setProperty("controlConfig", controlParsed);

    // Preset slots (A-E + Auto)
    auto slots = std::make_unique<juce::DynamicObject>();
    for (int i = 0; i < PresetManager::kNumSlots; ++i) {
        char label = PresetManager::slotLabel(i);
        auto slotFile = PresetManager::getSlotFile(i);
        auto slotJson = PresetManager::loadPresetJSONWithBackupFallback(slotFile, true);
        auto slotParsed = juce::JSON::parse(slotJson);
        if (slotParsed.isObject())
            slots->setProperty(juce::String::charToString(label), slotParsed);
        else if (presetFileFamilyExists(slotFile)) {
            Log::warn("APP", "Failed to export structurally valid slot "
                + juce::String::charToString(label));
            return {};
        }
    }
    root->setProperty("presetSlots", juce::var(slots.release()));

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool SettingsExporter::importFullBackup(const juce::String& json,
                                          PresetManager& presetManager,
                                          ControlMappingStore& controlStore,
                                          bool runtimeStateIsStableBeforeRestore)
{
    if (!runtimeStateIsStableBeforeRestore) {
        Log::warn("APP", "Full backup restore blocked while runtime state is unstable");
        return false;
    }

    juce::Logger::writeToLog("[PRESET] Import: tier=full, platform=" + getCurrentPlatform());
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    int version = root->getProperty("version");
    if (version < 1) return false;

    // Block cross-platform backup restore
    auto backupPlatform = getBackupPlatform(json);
    if (!isPlatformCompatible(json)) {
        Log::warn("APP", "Cross-platform backup restore blocked: " + backupPlatform);
        return false;
    }

    if (!propertyIsObjectIfPresent(*root, "audioSettings", "audioSettings")
        || !propertyIsObjectIfPresent(*root, "controlConfig", "controlConfig"))
        return false;
    if (root->hasProperty("controlConfig")
        && !isValidControlConfig(root->getProperty("controlConfig"))) {
        Log::warn("APP", "Invalid controlConfig schema in full backup");
        return false;
    }

    const bool hasAudioSettings = root->hasProperty("audioSettings");
    const bool hasControlConfig = root->hasProperty("controlConfig");

    if (hasAudioSettings) {
        auto* audioRoot = root->getProperty("audioSettings").getDynamicObject();
        if (audioRoot && audioRoot->hasProperty("plugins")) {
            Log::warn("APP",
                "Synchronous full restore blocked for plug-in backups; use transactional async restore");
            return false;
        }
    }

    std::vector<SlotRestoreEntry> slotPlan;
    const bool hasPresetSlots = root->hasProperty("presetSlots");
    if (hasPresetSlots) {
        auto* slots = root->getProperty("presetSlots").getDynamicObject();
        if (!slots) {
            Log::warn("APP", "Invalid presetSlots in full backup");
            return false;
        }
        if (!buildSlotRestorePlan(*slots, slotPlan))
            return false;
    }

    std::vector<FileSnapshot> slotSnapshots;
    if (hasPresetSlots && !captureSlotRestoreSnapshots(slotPlan, slotSnapshots))
        return false;

    juce::String audioSnapshot;
    if (hasAudioSettings) {
        audioSnapshot = presetManager.exportToJSON();
        if (!juce::JSON::parse(audioSnapshot).isObject()) {
            Log::warn("APP", "Failed to snapshot current audio settings");
            return false;
        }
    }

    std::vector<FileSnapshot> controlSnapshots;
    if (hasControlConfig && !captureControlConfigSnapshots(controlSnapshots))
        return false;

    auto rollbackImport = [&]() {
        bool rollbackOk = true;

        if (hasPresetSlots && !restoreFileSnapshots(slotSnapshots)) {
            Log::warn("APP", "Full backup slot rollback incomplete");
            rollbackOk = false;
        }

        if (hasControlConfig && !restoreFileSnapshots(controlSnapshots)) {
            Log::warn("APP", "Full backup control-config rollback incomplete");
            rollbackOk = false;
        }

        if (hasAudioSettings && !presetManager.importFromJSON(audioSnapshot)) {
            Log::warn("APP", "Full backup audio-settings rollback incomplete");
            rollbackOk = false;
        }

        if (hasPresetSlots) {
            presetManager.refreshSlotOccupancy();
            presetManager.loadSlotNames();
            presetManager.clearPreloadCache();
        }

        return rollbackOk;
    };

    auto failImport = [&](const juce::String& reason) {
        Log::warn("APP", reason + "; rolling back full backup import");
        if (!rollbackImport())
            Log::warn("APP", "Full backup import rollback incomplete");
        return false;
    };

    // Import audio settings (including VST chain)
    if (hasAudioSettings) {
        auto audioJson = juce::JSON::toString(root->getProperty("audioSettings"), false);
        if (!presetManager.importFromJSON(audioJson))
            return failImport("Failed to import audio settings from full backup");
    }

    // Import control config
    if (hasControlConfig) {
        auto controlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(controlJson)) {
            tempFile.deleteFile();
            return failImport("Failed to stage control config from full backup");
        }
        auto config = controlStore.load(tempFile);
        if (!controlStore.save(config)) {
            tempFile.deleteFile();
            return failImport("Failed to import control config from full backup");
        }
        tempFile.deleteFile();
    }

    // Import preset slots. Full restore is exact: slots missing from the
    // backup are removed from disk instead of leaving stale local slots behind.
    bool ok = true;
    if (hasPresetSlots) {
        for (const auto& entry : slotPlan) {
            auto slotFile = PresetManager::getSlotFile(entry.slotIndex);

            if (!entry.presentInBackup) {
                if (!deleteSlotRestoreFileFamily(entry.slotIndex)) {
                    ok = false;
                    Log::warn("APP", "Failed to clear missing slot file: " + slotFile.getFileName());
                }
                continue;
            }

            if (!atomicWriteFile(slotFile, entry.json)) {
                ok = false;
                Log::warn("APP", "Failed to restore slot file: " + slotFile.getFileName());
                continue;
            }

            if (!deleteSlotRestoreBackups(entry.slotIndex)) {
                ok = false;
                Log::warn("APP", "Failed to clear slot restore backups: " + slotFile.getFileName());
            }
        }

        if (!ok)
            return failImport("Full backup slot restore failed");

        presetManager.refreshSlotOccupancy();
        presetManager.loadSlotNames();
        presetManager.clearPreloadCache();
    }

    return true;
}

// ─── FileChooser dialog helpers ──────────────────────────────────────────────

void SettingsExporter::importFullBackupAsync(
    const juce::String& json,
    PresetManager& presetManager,
    ControlMappingStore& controlStore,
    bool runtimeStateIsStableBeforeRestore,
    std::function<void(bool)> onComplete)
{
    auto complete = [onComplete = std::move(onComplete)](bool ok) mutable {
        if (onComplete)
            onComplete(ok);
    };

    if (!runtimeStateIsStableBeforeRestore) {
        Log::warn("APP", "Full backup restore blocked while runtime state is unstable");
        complete(false);
        return;
    }

    juce::Logger::writeToLog(
        "[PRESET] Import: tier=full-transactional, platform=" + getCurrentPlatform());
    const auto parsed = juce::JSON::parse(json);
    auto* root = parsed.getDynamicObject();
    if (!root || static_cast<int>(root->getProperty("version")) < 1
        || !isPlatformCompatible(json)
        || !propertyIsObjectIfPresent(*root, "audioSettings", "audioSettings")
        || !propertyIsObjectIfPresent(*root, "controlConfig", "controlConfig")) {
        complete(false);
        return;
    }

    if (root->hasProperty("controlConfig")
        && !isValidControlConfig(root->getProperty("controlConfig"))) {
        Log::warn("APP", "Invalid controlConfig schema in full backup");
        complete(false);
        return;
    }

    const bool hasAudioSettings = root->hasProperty("audioSettings");
    const bool hasControlConfig = root->hasProperty("controlConfig");
    const bool hasPresetSlots = root->hasProperty("presetSlots");

    juce::String audioJson;
    if (hasAudioSettings) {
        audioJson = juce::JSON::toString(root->getProperty("audioSettings"), false);
        if (!PresetManager::isPresetJSONStructurallyValid(audioJson)) {
            Log::warn("APP", "Invalid audioSettings schema in full backup");
            complete(false);
            return;
        }
    }

    std::vector<SlotRestoreEntry> slotPlan;
    if (hasPresetSlots) {
        auto* slots = root->getProperty("presetSlots").getDynamicObject();
        if (!slots || !buildSlotRestorePlan(*slots, slotPlan)) {
            complete(false);
            return;
        }
    }

    std::vector<FileSnapshot> slotSnapshots;
    if (hasPresetSlots && !captureSlotRestoreSnapshots(slotPlan, slotSnapshots)) {
        complete(false);
        return;
    }

    ControlConfig stagedControlConfig;
    if (hasControlConfig) {
        const auto controlJson =
            juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(controlJson)) {
            tempFile.deleteFile();
            complete(false);
            return;
        }
        stagedControlConfig = controlStore.load(tempFile);
        tempFile.deleteFile();
    }

    std::vector<FileSnapshot> controlSnapshots;
    if (hasControlConfig && !captureControlConfigSnapshots(controlSnapshots)) {
        complete(false);
        return;
    }

    auto* presetManagerPtr = &presetManager;
    auto* controlStorePtr = &controlStore;
    auto commitExternalState =
        [hasControlConfig, stagedControlConfig, controlStorePtr,
         hasPresetSlots, slotPlan]() mutable {
            if (hasControlConfig && !controlStorePtr->save(stagedControlConfig)) {
                Log::warn("APP", "Failed to import control config from full backup");
                return false;
            }

            if (hasPresetSlots) {
                for (const auto& entry : slotPlan) {
                    const auto slotFile = PresetManager::getSlotFile(entry.slotIndex);
                    if (!entry.presentInBackup) {
                        if (!deleteSlotRestoreFileFamily(entry.slotIndex)) {
                            Log::warn("APP", "Failed to clear missing slot file: "
                                + slotFile.getFileName());
                            return false;
                        }
                        continue;
                    }

                    if (!atomicWriteFile(slotFile, entry.json)
                        || !deleteSlotRestoreBackups(entry.slotIndex)) {
                        Log::warn("APP", "Failed to restore slot file family: "
                            + slotFile.getFileName());
                        return false;
                    }
                }
            }
            return true;
        };

    auto rollbackExternalState =
        [hasPresetSlots, slotSnapshots, hasControlConfig, controlSnapshots,
         presetManagerPtr]() mutable {
            bool ok = true;
            if (hasPresetSlots && !restoreFileSnapshots(slotSnapshots))
                ok = false;
            if (hasControlConfig && !restoreFileSnapshots(controlSnapshots))
                ok = false;
            if (hasPresetSlots) {
                presetManagerPtr->refreshSlotOccupancy();
                presetManagerPtr->loadSlotNames();
                presetManagerPtr->clearPreloadCache();
            }
            return ok;
        };

    auto finishRestore =
        [presetManagerPtr, hasPresetSlots,
         complete = std::move(complete)](bool ok) mutable {
            if (ok && hasPresetSlots) {
                presetManagerPtr->refreshSlotOccupancy();
                presetManagerPtr->loadSlotNames();
                presetManagerPtr->clearPreloadCache();
                presetManagerPtr->triggerPreload();
            }
            complete(ok);
        };

    if (!hasAudioSettings) {
        const bool committed = commitExternalState();
        if (!committed)
            rollbackExternalState();
        finishRestore(committed);
        return;
    }

    presetManager.importFromJSONTransactionalAsync(
        audioJson,
        std::move(commitExternalState),
        std::move(rollbackExternalState),
        std::move(finishRestore));
}

void SettingsExporter::showSaveDialog(const juce::String& defaultFilename,
                                       const juce::String& filter,
                                       const juce::String& extension,
                                       std::function<juce::String()> exporter)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile(defaultFilename),
        filter);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [chooser, extension, exporter](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        auto target = file.withFileExtension(extension);
        auto json = exporter();
        if (json.isEmpty()) {
            Log::warn("APP", "Failed to prepare settings export");
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Export Failed",
                "DirectPipe could not prepare a complete backup. Existing files were not changed.");
            return;
        }
        if (!atomicWriteFile(target, json)) {
            Log::warn("APP", "Failed to export settings");
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Export Failed",
                "DirectPipe could not write the backup file. Existing files were preserved.");
        }
    });
}

void SettingsExporter::showLoadDialog(const juce::String& filter,
                                       std::function<bool(const juce::String& json)> importer)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        filter);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [chooser, importer](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (!file.existsAsFile()) return;
        auto json = file.loadFileAsString();
        if (!isPlatformCompatible(json)) {
            auto backupPlatform = getBackupPlatform(json);
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Platform Mismatch",
                "This backup was created on " + backupPlatform + ".\n"
                "Backup/restore is only supported between the same OS.");
            juce::Logger::writeToLog("[APP] Platform mismatch: backup=" + backupPlatform
                + " current=" + getCurrentPlatform());
            return;
        }
        if (!importer(json)) {
            Log::warn("APP", "Settings import failed");
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Import Failed",
                "DirectPipe could not restore this backup completely. "
                "The previous settings were preserved where possible.");
        }
    });
}

void SettingsExporter::showLoadDialogAsync(
    const juce::String& filter,
    std::function<void(const juce::String& json,
                       std::function<void(bool)> onComplete)> importer)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        filter);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [chooser, importer = std::move(importer)](
                             const juce::FileChooser& fc) mutable {
        const auto file = fc.getResult();
        if (!file.existsAsFile())
            return;

        const auto json = file.loadFileAsString();
        if (!isPlatformCompatible(json)) {
            const auto backupPlatform = getBackupPlatform(json);
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Platform Mismatch",
                "This backup was created on " + backupPlatform + ".\n"
                "Backup/restore is only supported between the same OS.");
            juce::Logger::writeToLog("[APP] Platform mismatch: backup="
                + backupPlatform + " current=" + getCurrentPlatform());
            return;
        }

        importer(json, [](bool ok) {
            if (ok)
                return;
            Log::warn("APP", "Settings import failed");
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Import Failed",
                "DirectPipe could not restore this backup completely. "
                "The previous settings and plug-in chain were preserved where possible.");
        });
    });
}

} // namespace directpipe
