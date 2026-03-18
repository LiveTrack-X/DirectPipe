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

namespace directpipe {

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
            audioObj->removeProperty("plugins");
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

    // Import audio/output settings (strip plugins to avoid overwriting VST chain)
    if (root->hasProperty("audioSettings")) {
        auto audioSettings = root->getProperty("audioSettings");
        if (auto* audioObj = audioSettings.getDynamicObject())
            audioObj->removeProperty("plugins");
        auto audioJson = juce::JSON::toString(audioSettings, false);
        presetManager.importFromJSON(audioJson);
    }

    // Reset activeSlot to A since .dpbackup doesn't include slot files
    presetManager.setActiveSlot(0);

    // Import control config
    if (root->hasProperty("controlConfig")) {
        auto controlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        tempFile.replaceWithText(controlJson);
        auto config = controlStore.load(tempFile);
        controlStore.save(config);
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
        if (slotFile.existsAsFile()) {
            auto slotJson = slotFile.loadFileAsString();
            auto slotParsed = juce::JSON::parse(slotJson);
            if (slotParsed.isObject())
                slots->setProperty(juce::String::charToString(label), slotParsed);
        }
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

    // Import audio settings (including VST chain)
    if (root->hasProperty("audioSettings")) {
        auto audioJson = juce::JSON::toString(root->getProperty("audioSettings"), false);
        presetManager.importFromJSON(audioJson);
    }

    // Import control config
    if (root->hasProperty("controlConfig")) {
        auto controlJson = juce::JSON::toString(root->getProperty("controlConfig"), false);
        auto tempFile = juce::File::createTempFile("dpctrl");
        tempFile.replaceWithText(controlJson);
        auto config = controlStore.load(tempFile);
        controlStore.save(config);
        tempFile.deleteFile();
    }

    // Import preset slots
    if (root->hasProperty("presetSlots")) {
        auto* slots = root->getProperty("presetSlots").getDynamicObject();
        if (slots) {
            for (int i = 0; i < PresetManager::kNumSlots; ++i) {
                char label = PresetManager::slotLabel(i);
                auto key = juce::String::charToString(label);
                if (slots->hasProperty(key)) {
                    auto slotJson = juce::JSON::toString(slots->getProperty(key), true);
                    auto slotFile = PresetManager::getSlotFile(i);
                    if (!atomicWriteFile(slotFile, slotJson))
                        Log::warn("APP", "Failed to restore slot file: " + slotFile.getFileName());
                }
            }
        }
    }

    return true;
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
