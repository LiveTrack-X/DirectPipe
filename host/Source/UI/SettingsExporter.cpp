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

void appendSlotRestoreFileFamily(std::vector<juce::File>& files, const juce::File& file)
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

    appendSlotRestoreFileFamily(files, PresetManager::getSlotFile(slotIndex));

    auto legacyFile = getLegacyNumericSlotFile(slotIndex);
    if (legacyFile != juce::File())
        appendSlotRestoreFileFamily(files, legacyFile);

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

    Log::warn("APP", "Failed to snapshot slot restore file: " + file.getFileName());
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

bool restoreSlotRestoreSnapshots(const std::vector<FileSnapshot>& snapshots)
{
    bool ok = true;
    for (const auto& snapshot : snapshots) {
        if (!restoreFileSnapshot(snapshot)) {
            ok = false;
            Log::warn("APP", "Failed to roll back slot restore file: " + snapshot.file.getFileName());
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
            if (!juce::JSON::parse(entry.json).isObject()) {
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
    if (audioParsed.isObject()) {
        auto* audioObj = audioParsed.getDynamicObject();
        if (audioObj)
            stripSettingsOnlyPresetState(*audioObj);
        root->setProperty("audioSettings", audioParsed);
    }

    // Control config (hotkeys, MIDI, server)
    auto tempFile = juce::File::createTempFile("dpctrl");
    auto controlConfig = controlStore.load();
    controlStore.save(controlConfig, tempFile);
    auto controlJson = tempFile.loadFileAsString();
    tempFile.deleteFile();
    auto controlParsed = juce::JSON::parse(controlJson);
    if (controlParsed.isObject())
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

    // Import audio/output settings (strip plugins to avoid overwriting VST chain)
    if (root->hasProperty("audioSettings")) {
        auto audioSettings = root->getProperty("audioSettings");
        if (auto* audioObj = audioSettings.getDynamicObject())
            stripSettingsOnlyPresetState(*audioObj);
        auto audioJson = juce::JSON::toString(audioSettings, false);
        if (!presetManager.importFromJSON(audioJson)) {
            Log::warn("APP", "Failed to import audio settings from backup");
            return false;
        }
    }

    // Import control config
    if (root->hasProperty("controlConfig")) {
        auto controlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(controlJson))
            return false;
        auto config = controlStore.load(tempFile);
        if (!controlStore.save(config)) {
            tempFile.deleteFile();
            return false;
        }
        tempFile.deleteFile();
    }

    // Preset slots NOT imported — managed independently via slots A-E
    // (v1 backups with presetSlots are intentionally ignored)

    return true;
}

juce::String SettingsExporter::exportFullBackup(PresetManager& presetManager,
                                                  ControlMappingStore& controlStore)
{
    juce::Logger::writeToLog("[PRESET] Export: tier=full, platform=" + getCurrentPlatform());
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
    if (activeSlot >= 0 && activeSlot < PresetManager::kNumSlots)
        presetManager.saveSlot(activeSlot);

    // Audio settings (including VST chain)
    auto audioJson = presetManager.exportToJSON();
    auto audioParsed = juce::JSON::parse(audioJson);
    if (audioParsed.isObject())
        root->setProperty("audioSettings", audioParsed);

    // Control config (hotkeys, MIDI, server)
    auto tempFile = juce::File::createTempFile("dpctrl");
    auto controlConfig = controlStore.load();
    controlStore.save(controlConfig, tempFile);
    auto controlJson = tempFile.loadFileAsString();
    tempFile.deleteFile();
    auto controlParsed = juce::JSON::parse(controlJson);
    if (controlParsed.isObject())
        root->setProperty("controlConfig", controlParsed);

    // Preset slots (A-E + Auto)
    auto slots = std::make_unique<juce::DynamicObject>();
    for (int i = 0; i < PresetManager::kNumSlots; ++i) {
        char label = PresetManager::slotLabel(i);
        auto slotFile = PresetManager::getSlotFile(i);
        auto slotJson = loadFileWithBackupFallback(slotFile);
        auto slotParsed = juce::JSON::parse(slotJson);
        if (slotParsed.isObject())
            slots->setProperty(juce::String::charToString(label), slotParsed);
    }
    root->setProperty("presetSlots", juce::var(slots.release()));

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool SettingsExporter::importFullBackup(const juce::String& json,
                                          PresetManager& presetManager,
                                          ControlMappingStore& controlStore)
{
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

    // Import audio settings (including VST chain)
    if (root->hasProperty("audioSettings")) {
        auto audioJson = juce::JSON::toString(root->getProperty("audioSettings"), false);
        if (!presetManager.importFromJSON(audioJson)) {
            Log::warn("APP", "Failed to import audio settings from full backup");
            return false;
        }
    }

    // Import control config
    if (root->hasProperty("controlConfig")) {
        auto controlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        if (!tempFile.replaceWithText(controlJson))
            return false;
        auto config = controlStore.load(tempFile);
        if (!controlStore.save(config)) {
            tempFile.deleteFile();
            return false;
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

        if (!ok) {
            Log::warn("APP", "Full backup slot restore failed; rolling back slot files");
            if (!restoreSlotRestoreSnapshots(slotSnapshots))
                Log::warn("APP", "Full backup slot rollback incomplete");
            presetManager.refreshSlotOccupancy();
            presetManager.loadSlotNames();
            presetManager.clearPreloadCache();
            return false;
        }

        presetManager.refreshSlotOccupancy();
        presetManager.loadSlotNames();
        presetManager.clearPreloadCache();
    }

    return ok;
}

// ─── FileChooser dialog helpers ──────────────────────────────────────────────

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
        if (json.isNotEmpty()) {
            if (!atomicWriteFile(target, json))
                Log::warn("APP", "Failed to export settings");
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
        importer(json);
    });
}

} // namespace directpipe
