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
 * @file ControlMapping.cpp
 * @brief Control mapping persistence implementation
 */

#include "ControlMapping.h"
#include "HotkeyHandler.h"
#include "MidiHandler.h"
#include "../Util/AtomicFileIO.h"

namespace directpipe {

/// Get the application root directory.
/// On macOS .app bundles, currentExecutableFile points to Contents/MacOS/binary,
/// so we use currentApplicationFile which returns the .app bundle root.
/// On Windows/Linux, both resolve to the same directory.
static juce::File getAppRootDirectory()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation(
        juce::File::currentApplicationFile).getParentDirectory();
#else
    return juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();
#endif
}

bool ControlMappingStore::isPortableMode()
{
    return getAppRootDirectory().getChildFile("portable.flag").existsAsFile();
}

juce::File ControlMappingStore::getConfigDirectory()
{
    if (isPortableMode())
        return getAppRootDirectory().getChildFile("config");

    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);

#if JUCE_MAC
    // macOS convention: ~/Library/Application Support/DirectPipe
    // (userApplicationDataDirectory returns ~/Library, not ~/Library/Application Support)
    return appData.getChildFile("Application Support/DirectPipe");
#else
    return appData.getChildFile("DirectPipe");
#endif
}

juce::File ControlMappingStore::getDefaultConfigFile()
{
    return getConfigDirectory().getChildFile("directpipe-controls.json");
}

bool ControlMappingStore::save(const ControlConfig& config, const juce::File& file)
{
    auto targetFile = file.getFullPathName().isEmpty() ? getDefaultConfigFile() : file;
    targetFile.getParentDirectory().createDirectory();

    auto root = std::make_unique<juce::DynamicObject>();

    // Hotkeys
    juce::Array<juce::var> hotkeys;
    for (const auto& hk : config.hotkeys) {
        auto obj = new juce::DynamicObject();
        obj->setProperty("modifiers", static_cast<int>(hk.modifiers));
        obj->setProperty("virtualKey", static_cast<int>(hk.virtualKey));
        obj->setProperty("displayName", juce::String(hk.displayName));
        obj->setProperty("action", actionEventToVar(hk.action));
        hotkeys.add(juce::var(obj));
    }
    root->setProperty("hotkeys", hotkeys);

    // MIDI mappings
    juce::Array<juce::var> midi;
    for (const auto& m : config.midiMappings) {
        auto obj = new juce::DynamicObject();
        obj->setProperty("cc", m.cc);
        obj->setProperty("note", m.note);
        obj->setProperty("channel", m.channel);
        obj->setProperty("type", static_cast<int>(m.type));
        obj->setProperty("deviceName", juce::String(m.deviceName));
        obj->setProperty("action", actionEventToVar(m.action));
        midi.add(juce::var(obj));
    }
    root->setProperty("midi", midi);

    // Server config
    auto server = new juce::DynamicObject();
    server->setProperty("websocketPort", config.server.websocketPort);
    server->setProperty("websocketEnabled", config.server.websocketEnabled);
    server->setProperty("httpPort", config.server.httpPort);
    server->setProperty("httpEnabled", config.server.httpEnabled);
    root->setProperty("server", juce::var(server));

    auto json = juce::JSON::toString(juce::var(root.release()), true);
    return atomicWriteFile(targetFile, json);
}

ControlConfig ControlMappingStore::load(const juce::File& file)
{
    auto sourceFile = file.getFullPathName().isEmpty() ? getDefaultConfigFile() : file;

    auto json = loadFileWithBackupFallback(sourceFile);
    if (json.isEmpty())
        return createDefaults();

    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject())
        return createDefaults();

    ControlConfig config;
    auto* root = parsed.getDynamicObject();

    // Hotkeys
    if (auto* hotkeys = root->getProperty("hotkeys").getArray()) {
        for (const auto& hk : *hotkeys) {
            if (auto* obj = hk.getDynamicObject()) {
                HotkeyMapping mapping;
                mapping.modifiers = static_cast<uint32_t>(static_cast<int>(obj->getProperty("modifiers")));
                mapping.virtualKey = static_cast<uint32_t>(static_cast<int>(obj->getProperty("virtualKey")));
                mapping.displayName = obj->getProperty("displayName").toString().toStdString();
                mapping.action = varToActionEvent(obj->getProperty("action"));
                config.hotkeys.push_back(mapping);
            }
        }
    }

    // MIDI
    if (auto* midi = root->getProperty("midi").getArray()) {
        for (const auto& m : *midi) {
            if (auto* obj = m.getDynamicObject()) {
                MidiMapping mapping;
                mapping.cc = obj->getProperty("cc");
                mapping.note = obj->getProperty("note");
                mapping.channel = obj->getProperty("channel");
                mapping.type = static_cast<MidiMappingType>(static_cast<int>(obj->getProperty("type")));
                mapping.deviceName = obj->getProperty("deviceName").toString().toStdString();
                mapping.action = varToActionEvent(obj->getProperty("action"));
                config.midiMappings.push_back(mapping);
            }
        }
    }

    // Server
    if (auto* server = root->getProperty("server").getDynamicObject()) {
        config.server.websocketPort = server->getProperty("websocketPort");
        config.server.websocketEnabled = server->getProperty("websocketEnabled");
        config.server.httpPort = server->getProperty("httpPort");
        config.server.httpEnabled = server->getProperty("httpEnabled");
    }

    return config;
}

ControlConfig ControlMappingStore::createDefaults()
{
    ControlConfig config;

    // Default hotkeys — minimal set to demonstrate the hotkey system
    // Users can add more via Controls > Hotkeys tab.
    // (Previous versions registered 19 hotkeys by default, causing conflicts
    //  with browsers and other apps. Now only essential ones are pre-configured.)

    // Panic mute: Ctrl+Shift+M — essential safety hotkey
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = 'M';
        hk.action = {Action::PanicMute, 0, 0.0f, "Panic Mute"};
        hk.displayName = "Ctrl+Shift+M";
        config.hotkeys.push_back(hk);
    }

    // Plugin 1 bypass: Ctrl+Shift+1 — example hotkey to show the system
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = '1';
        hk.action = {Action::PluginBypass, 0, 0.0f, "Plugin 1 Bypass"};
        hk.displayName = "Ctrl+Shift+1";
        config.hotkeys.push_back(hk);
    }

    config.server.websocketPort = 8765;
    config.server.websocketEnabled = true;
    config.server.httpPort = 8766;
    config.server.httpEnabled = true;

    return config;
}

juce::var ControlMappingStore::actionEventToVar(const ActionEvent& event)
{
    auto obj = new juce::DynamicObject();
    obj->setProperty("action", static_cast<int>(event.action));
    obj->setProperty("intParam", event.intParam);
    obj->setProperty("floatParam", static_cast<double>(event.floatParam));
    obj->setProperty("stringParam", juce::String(event.stringParam));
    obj->setProperty("intParam2", event.intParam2);
    return juce::var(obj);
}

ActionEvent ControlMappingStore::varToActionEvent(const juce::var& v)
{
    ActionEvent event;
    if (auto* obj = v.getDynamicObject()) {
        int actionVal = static_cast<int>(obj->getProperty("action"));
        if (actionVal >= 0 && actionVal <= static_cast<int>(Action::XRunReset))
            event.action = static_cast<Action>(actionVal);
        event.intParam = obj->getProperty("intParam");
        event.floatParam = static_cast<float>(static_cast<double>(obj->getProperty("floatParam")));
        event.stringParam = obj->getProperty("stringParam").toString().toStdString();
        event.intParam2 = obj->getProperty("intParam2");
    }
    return event;
}

} // namespace directpipe
