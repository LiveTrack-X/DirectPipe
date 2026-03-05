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

namespace directpipe {

juce::String SettingsExporter::exportAll(PresetManager& presetManager,
                                          ControlMappingStore& controlStore)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 2);
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
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    int version = root->getProperty("version");
    if (version < 1) return false;

    // Import audio/output settings (strip plugins to avoid overwriting VST chain)
    if (root->hasProperty("audioSettings")) {
        auto audioSettings = root->getProperty("audioSettings");
        if (auto* audioObj = audioSettings.getDynamicObject())
            audioObj->removeProperty("plugins");
        auto audioJson = juce::JSON::toString(audioSettings, false);
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

    // Preset slots NOT imported — managed independently via slots A-E
    // (v1 backups with presetSlots are intentionally ignored)

    return true;
}

juce::String SettingsExporter::exportFullBackup(PresetManager& presetManager,
                                                  ControlMappingStore& controlStore)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 2);
    root->setProperty("type", "full");
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

    // Preset slots (A-E)
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
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    int version = root->getProperty("version");
    if (version < 1) return false;

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
                    slotFile.replaceWithText(slotJson);
                }
            }
        }
    }

    return true;
}

} // namespace directpipe
