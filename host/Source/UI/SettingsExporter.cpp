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
                               const char* label,
                               SettingsExporter::RestoreDiagnostics& diagnostic)
{
    if (!root.hasProperty(property))
        return true;

    if (root.getProperty(property).isObject())
        return true;

    return diagnostic.fail(label, "The backup section must be an object.",
                           "Use an intact backup exported by DirectPipe; this section is malformed.");
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

bool parseBackup(const juce::String& json, juce::var& parsed,
                 SettingsExporter::RestoreDiagnostics& diagnostic)
{
    if (juce::JSON::parse(json, parsed).failed())
        return diagnostic.fail("JSON validation", "The backup is empty, incomplete or invalid JSON.",
                               "Select the original backup or export it again; do not edit its contents manually.");
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return diagnostic.fail("backup format", "The backup root must be an object.",
                               "Select a DirectPipe settings or full backup file.");
    if (static_cast<int>(root->getProperty("version")) < 1)
        return diagnostic.fail("backup version", "The backup has no valid positive version.",
                               "Check that this is an intact DirectPipe backup and export it again if possible.");
    if (!SettingsExporter::isPlatformCompatible(json))
        return diagnostic.fail("platform compatibility", "The backup was exported on a different operating system.",
                               "Restore on the same operating system as the exporting computer.");
    return true;
}

bool buildSlotRestorePlan(const juce::DynamicObject& slots,
                          std::vector<SlotRestoreEntry>& plan,
                          SettingsExporter::RestoreDiagnostics& diagnostic)
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
                return diagnostic.fail("preset slots", "Slot " + entry.key + " is not an object.",
                                       "Use an intact full backup or re-export the affected slot.");
            }

            entry.presentInBackup = true;
            entry.json = juce::JSON::toString(slotVar, true);
            if (!PresetManager::isChainJSONStructurallyValid(entry.json)) {
                return diagnostic.fail("preset slots", "Slot " + entry.key + " has invalid chain data.",
                                       "Use an intact full backup; check the slot's plug-in list and saved state.");
            }
        }

        plan.push_back(std::move(entry));
    }

    return true;
}

bool readBackupFile(const juce::File& file, juce::String& json,
                    SettingsExporter::RestoreDiagnostics& diagnostic)
{
    diagnostic.clear();
    if (!file.existsAsFile())
        return diagnostic.fail("backup file", "The selected backup file is no longer available.",
                               "Check the file location, reconnect its drive or select the backup again.");
    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return diagnostic.fail("file read", "The selected backup could not be opened.",
                               "Check read permissions, file locks and whether the drive is connected.");
    json = stream.readEntireStreamAsString();
    if (stream.getStatus().failed() || stream.getPosition() < stream.getTotalLength())
        return diagnostic.fail("file read", "The selected backup could not be read completely.",
                               "Check the drive and read permissions, then copy the intact backup to a local folder.");
    if (json.trim().isEmpty())
        return diagnostic.fail("backup file", "The selected backup is empty.",
                               "Select a nonempty backup or export it again from the source computer.");
    if (!SettingsExporter::isPlatformCompatible(json))
        return diagnostic.fail("platform compatibility", "The backup was exported on a different operating system.",
                               "Restore on the same operating system as the exporting computer.");
    return true;
}

void showRestoreFailure(SettingsExporter::RestoreDiagnostics& diagnostic)
{
    if (diagnostic.stage.isEmpty())
        diagnostic.fail("restore result", "DirectPipe could not restore this backup completely.",
                        "Open the log and check the earlier audio, plug-in and file errors before retrying.");
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                         "Import Failed", diagnostic.message());
}

} // namespace

void SettingsExporter::RestoreDiagnostics::clear()
{
    stage.clear();
    reason.clear();
    nextCheck.clear();
    rollbackIncomplete = false;
}

bool SettingsExporter::RestoreDiagnostics::fail(const juce::String& failedStage,
                                               const juce::String& safeReason,
                                               const juce::String& check)
{
    stage = failedStage;
    reason = safeReason;
    nextCheck = check;
    Log::warn("APP", message().replaceCharacter('\n', ' '));
    return false;
}

void SettingsExporter::RestoreDiagnostics::markRollbackIncomplete()
{
    rollbackIncomplete = true;
    Log::warn("APP", message().replaceCharacter('\n', ' '));
}

juce::String SettingsExporter::RestoreDiagnostics::message() const
{
    juce::String result;
    if (stage.isNotEmpty())
        result = "Restore failed at " + stage + ": " + reason;
    if (nextCheck.isNotEmpty())
        result += "\nCheck: " + nextCheck;
    if (rollbackIncomplete)
        result += "\nPrevious settings could not be fully restored. Check audio settings, controls and preset slots before continuing.";
    return result;
}

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
                                  ControlMappingStore& controlStore,
                                  RestoreDiagnostics* diagnostics)
{
    RestoreDiagnostics localDiagnostic;
    auto& diagnostic = diagnostics != nullptr ? *diagnostics : localDiagnostic;
    diagnostic.clear();
    juce::Logger::writeToLog("[PRESET] Import: tier=backup, platform=" + getCurrentPlatform());
    juce::var parsed;
    if (!parseBackup(json, parsed, diagnostic))
        return false;
    auto* root = parsed.getDynamicObject();

    if (!propertyIsObjectIfPresent(*root, "audioSettings", "audio settings", diagnostic)
        || !propertyIsObjectIfPresent(*root, "controlConfig", "control settings", diagnostic))
        return false;
    if (root->hasProperty("controlConfig")
        && !isValidControlConfig(root->getProperty("controlConfig"))) {
        return diagnostic.fail("control settings", "The control configuration has invalid fields.",
                               "Check the backup's hotkey, MIDI and server settings, or export it again.");
    }

    const bool hasAudioSettings = root->hasProperty("audioSettings");
    const bool hasControlConfig = root->hasProperty("controlConfig");

    juce::String stagedControlJson;
    ControlConfig stagedControlConfig;
    if (hasControlConfig) {
        stagedControlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(stagedControlJson)) {
            tempFile.deleteFile();
            return diagnostic.fail("control staging", "A temporary control configuration could not be written.",
                                   "Check temporary-folder permissions, free disk space and file locks.");
        }
        stagedControlConfig = controlStore.load(tempFile);
        tempFile.deleteFile();
    }

    juce::String audioSnapshot;
    if (hasAudioSettings) {
        audioSnapshot = presetManager.exportToJSON();
        if (!PresetManager::isPresetJSONStructurallyValid(audioSnapshot))
            return diagnostic.fail("audio snapshot", "Current audio settings could not be saved for rollback.",
                                   "Wait for the current load to finish and retry with a stable plug-in chain.");
    }

    std::vector<FileSnapshot> controlSnapshots;
    if (hasControlConfig && !captureControlConfigSnapshots(controlSnapshots))
        return diagnostic.fail("control snapshot", "Current control files could not be read for rollback.",
                               "Check configuration-folder read permissions and file locks.");

    auto rollbackImport = [&]() {
        bool rollbackOk = true;
        if (hasControlConfig && !restoreFileSnapshots(controlSnapshots))
            rollbackOk = false;
        if (hasAudioSettings && !presetManager.importFromJSON(audioSnapshot))
            rollbackOk = false;
        return rollbackOk;
    };

    auto failImport = [&](const juce::String& stage, const juce::String& reason,
                          const juce::String& nextCheck) {
        diagnostic.fail(stage, reason, nextCheck);
        if (!rollbackImport())
            diagnostic.markRollbackIncomplete();
        return false;
    };

    // Import audio/output settings (strip plugins to avoid overwriting VST chain)
    if (hasAudioSettings) {
        auto audioSettings = root->getProperty("audioSettings");
        if (auto* audioObj = audioSettings.getDynamicObject())
            stripSettingsOnlyPresetState(*audioObj);
        auto audioJson = juce::JSON::toString(audioSettings, false);
        if (!presetManager.importFromJSON(audioJson)) {
            return failImport("audio settings", "Audio or application settings could not be restored.",
                              "Check the backup format and earlier audio-device or application-settings log entries.");
        }
    }

    // Import control config
    if (hasControlConfig && !controlStore.save(stagedControlConfig))
        return failImport("control write", "The restored control configuration could not be saved.",
                          "Check configuration-folder write permissions, free disk space and file locks.");

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
                                          bool runtimeStateIsStableBeforeRestore,
                                          RestoreDiagnostics* diagnostics)
{
    RestoreDiagnostics localDiagnostic;
    auto& diagnostic = diagnostics != nullptr ? *diagnostics : localDiagnostic;
    diagnostic.clear();
    if (!runtimeStateIsStableBeforeRestore) {
        return diagnostic.fail("runtime state", "The current plug-in chain is loading or partially restored.",
                               "Wait for the load to finish or load a complete slot before restoring the backup.");
    }

    juce::Logger::writeToLog("[PRESET] Import: tier=full, platform=" + getCurrentPlatform());
    juce::var parsed;
    if (!parseBackup(json, parsed, diagnostic))
        return false;
    auto* root = parsed.getDynamicObject();

    if (!propertyIsObjectIfPresent(*root, "audioSettings", "audio settings", diagnostic)
        || !propertyIsObjectIfPresent(*root, "controlConfig", "control settings", diagnostic))
        return false;
    if (root->hasProperty("controlConfig")
        && !isValidControlConfig(root->getProperty("controlConfig"))) {
        return diagnostic.fail("control settings", "The control configuration has invalid fields.",
                               "Check the backup's hotkey, MIDI and server settings, or export it again.");
    }

    const bool hasAudioSettings = root->hasProperty("audioSettings");
    const bool hasControlConfig = root->hasProperty("controlConfig");

    if (hasAudioSettings) {
        auto* audioRoot = root->getProperty("audioSettings").getDynamicObject();
        if (audioRoot && audioRoot->hasProperty("plugins")) {
            return diagnostic.fail("restore method", "This backup contains a plug-in chain and needs asynchronous restoration.",
                                   "Use Full Restore in DirectPipe settings to restore this backup.");
        }
    }

    std::vector<SlotRestoreEntry> slotPlan;
    const bool hasPresetSlots = root->hasProperty("presetSlots");
    if (hasPresetSlots) {
        auto* slots = root->getProperty("presetSlots").getDynamicObject();
        if (!slots) {
            return diagnostic.fail("preset slots", "The slot section must be an object.",
                                   "Use an intact full backup or export its slots again.");
        }
        if (!buildSlotRestorePlan(*slots, slotPlan, diagnostic))
            return false;
    }

    std::vector<FileSnapshot> slotSnapshots;
    if (hasPresetSlots && !captureSlotRestoreSnapshots(slotPlan, slotSnapshots))
        return diagnostic.fail("slot snapshot", "Current preset slot files could not be read for rollback.",
                               "Check the Slots folder's read permissions and file locks.");

    juce::String audioSnapshot;
    if (hasAudioSettings) {
        audioSnapshot = presetManager.exportToJSON();
        if (!juce::JSON::parse(audioSnapshot).isObject()) {
            return diagnostic.fail("audio snapshot", "Current audio settings could not be saved for rollback.",
                                   "Wait for the current load to finish and retry with a stable plug-in chain.");
        }
    }

    std::vector<FileSnapshot> controlSnapshots;
    if (hasControlConfig && !captureControlConfigSnapshots(controlSnapshots))
        return diagnostic.fail("control snapshot", "Current control files could not be read for rollback.",
                               "Check configuration-folder read permissions and file locks.");

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

    auto failImport = [&](const juce::String& stage, const juce::String& reason,
                          const juce::String& nextCheck) {
        if (diagnostic.stage.isEmpty())
            diagnostic.fail(stage, reason, nextCheck);
        if (!rollbackImport())
            diagnostic.markRollbackIncomplete();
        return false;
    };

    // Import audio settings (including VST chain)
    if (hasAudioSettings) {
        auto audioJson = juce::JSON::toString(root->getProperty("audioSettings"), false);
        if (!presetManager.importFromJSON(audioJson))
            return failImport("audio settings", "Audio or application settings could not be restored.",
                              "Check the backup format and earlier audio-device or application-settings log entries.");
    }

    // Import control config
    if (hasControlConfig) {
        auto controlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(controlJson)) {
            tempFile.deleteFile();
            return failImport("control staging", "A temporary control configuration could not be written.",
                              "Check temporary-folder permissions, free disk space and file locks.");
        }
        auto config = controlStore.load(tempFile);
        if (!controlStore.save(config)) {
            tempFile.deleteFile();
            return failImport("control write", "The restored control configuration could not be saved.",
                              "Check configuration-folder write permissions, free disk space and file locks.");
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
                    diagnostic.fail("slot write", "Slot " + entry.key + " could not be cleared.",
                                    "Check Slots-folder write permissions and locked preset files.");
                }
                continue;
            }

            if (!atomicWriteFile(slotFile, entry.json)) {
                ok = false;
                diagnostic.fail("slot write", "Slot " + entry.key + " could not be written.",
                                "Check Slots-folder write permissions, free disk space and file locks.");
                continue;
            }

            if (!deleteSlotRestoreBackups(entry.slotIndex)) {
                ok = false;
                diagnostic.fail("slot write", "Old restore files for slot " + entry.key + " could not be cleared.",
                                "Check Slots-folder write permissions and locked preset files.");
            }
        }

        if (!ok)
            return failImport("slot write", "Preset slots could not be restored completely.",
                              "Check Slots-folder write permissions, free disk space and file locks.");

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
    std::function<void(bool)> onComplete,
    std::shared_ptr<RestoreDiagnostics> diagnostics)
{
    if (!diagnostics)
        diagnostics = std::make_shared<RestoreDiagnostics>();
    diagnostics->clear();
    auto complete = [diagnostics, onComplete = std::move(onComplete)](bool ok) mutable {
        if (!ok && diagnostics->stage.isEmpty())
            diagnostics->fail("audio and plug-in restore",
                              "The audio settings or plug-in transaction could not be completed.",
                              "Check earlier device and plug-in errors in the log; verify plug-in installation, OS/architecture and saved state.");
        if (onComplete)
            onComplete(ok);
    };

    if (!runtimeStateIsStableBeforeRestore) {
        diagnostics->fail("runtime state", "The current plug-in chain is loading or partially restored.",
                          "Wait for the load to finish or load a complete slot before restoring the backup.");
        complete(false);
        return;
    }

    juce::Logger::writeToLog(
        "[PRESET] Import: tier=full-transactional, platform=" + getCurrentPlatform());
    juce::var parsed;
    if (!parseBackup(json, parsed, *diagnostics)) {
        complete(false);
        return;
    }
    auto* root = parsed.getDynamicObject();
    if (!propertyIsObjectIfPresent(*root, "audioSettings", "audio settings", *diagnostics)
        || !propertyIsObjectIfPresent(*root, "controlConfig", "control settings", *diagnostics)) {
        complete(false);
        return;
    }

    if (root->hasProperty("controlConfig")
        && !isValidControlConfig(root->getProperty("controlConfig"))) {
        diagnostics->fail("control settings", "The control configuration has invalid fields.",
                          "Check the backup's hotkey, MIDI and server settings, or export it again.");
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
            diagnostics->fail("audio settings", "The audio settings or saved plug-in list are malformed.",
                              "Select an intact full backup or export it again; check saved plug-in state.");
            complete(false);
            return;
        }
    }

    std::vector<SlotRestoreEntry> slotPlan;
    if (hasPresetSlots) {
        auto* slots = root->getProperty("presetSlots").getDynamicObject();
        if (!slots) {
            diagnostics->fail("preset slots", "The slot section must be an object.",
                              "Use an intact full backup or export its slots again.");
            complete(false);
            return;
        }
        if (!buildSlotRestorePlan(*slots, slotPlan, *diagnostics)) {
            complete(false);
            return;
        }
    }

    std::vector<FileSnapshot> slotSnapshots;
    if (hasPresetSlots && !captureSlotRestoreSnapshots(slotPlan, slotSnapshots)) {
        diagnostics->fail("slot snapshot", "Current preset slot files could not be read for rollback.",
                          "Check the Slots folder's read permissions and file locks.");
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
            diagnostics->fail("control staging", "A temporary control configuration could not be written.",
                              "Check temporary-folder permissions, free disk space and file locks.");
            complete(false);
            return;
        }
        stagedControlConfig = controlStore.load(tempFile);
        tempFile.deleteFile();
    }

    std::vector<FileSnapshot> controlSnapshots;
    if (hasControlConfig && !captureControlConfigSnapshots(controlSnapshots)) {
        diagnostics->fail("control snapshot", "Current control files could not be read for rollback.",
                          "Check configuration-folder read permissions and file locks.");
        complete(false);
        return;
    }

    auto* presetManagerPtr = &presetManager;
    auto* controlStorePtr = &controlStore;
    auto commitExternalState =
        [diagnostics, hasControlConfig, stagedControlConfig, controlStorePtr,
         hasPresetSlots, slotPlan]() mutable {
            if (hasControlConfig && !controlStorePtr->save(stagedControlConfig)) {
                return diagnostics->fail("control write", "The restored control configuration could not be saved.",
                                         "Check configuration-folder write permissions, free disk space and file locks.");
            }

            if (hasPresetSlots) {
                for (const auto& entry : slotPlan) {
                    const auto slotFile = PresetManager::getSlotFile(entry.slotIndex);
                    if (!entry.presentInBackup) {
                        if (!deleteSlotRestoreFileFamily(entry.slotIndex)) {
                            return diagnostics->fail("slot write", "Slot " + entry.key + " could not be cleared.",
                                                     "Check Slots-folder write permissions and locked preset files.");
                        }
                        continue;
                    }

                    if (!atomicWriteFile(slotFile, entry.json)
                        || !deleteSlotRestoreBackups(entry.slotIndex)) {
                        return diagnostics->fail("slot write", "Slot " + entry.key + " or its restore files could not be written.",
                                                 "Check Slots-folder write permissions, free disk space and file locks.");
                    }
                }
            }
            return true;
        };

    auto rollbackExternalState =
        [diagnostics, hasPresetSlots, slotSnapshots, hasControlConfig, controlSnapshots,
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
            if (!ok)
                diagnostics->markRollbackIncomplete();
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
                                       std::function<bool(const juce::String& json)> importer,
                                       std::shared_ptr<RestoreDiagnostics> diagnostics)
{
    if (!diagnostics)
        diagnostics = std::make_shared<RestoreDiagnostics>();
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        filter);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [chooser, importer, diagnostics](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File())
            return; // Cancellation is not an import failure.
        juce::String json;
        if (!readBackupFile(file, json, *diagnostics)) {
            showRestoreFailure(*diagnostics);
            return;
        }
        if (!importer(json))
            showRestoreFailure(*diagnostics);
    });
}

void SettingsExporter::showLoadDialogAsync(
    const juce::String& filter,
    std::function<void(const juce::String& json,
                       std::function<void(bool)> onComplete)> importer,
    std::shared_ptr<RestoreDiagnostics> diagnostics)
{
    if (!diagnostics)
        diagnostics = std::make_shared<RestoreDiagnostics>();
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        filter);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [chooser, diagnostics, importer = std::move(importer)](
                             const juce::FileChooser& fc) mutable {
        const auto file = fc.getResult();
        if (file == juce::File())
            return; // Cancellation is not an import failure.
        juce::String json;
        if (!readBackupFile(file, json, *diagnostics)) {
            showRestoreFailure(*diagnostics);
            return;
        }

        importer(json, [diagnostics](bool ok) {
            if (ok)
                return;
            showRestoreFailure(*diagnostics);
        });
    });
}

} // namespace directpipe
