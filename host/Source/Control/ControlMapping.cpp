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

namespace directpipe {

bool ControlMappingStore::isPortableMode()
{
    auto exeDir = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();
    return exeDir.getChildFile("portable.flag").existsAsFile();
}

juce::File ControlMappingStore::getConfigDirectory()
{
    if (isPortableMode()) {
        auto exeDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        return exeDir.getChildFile("config");
    }

    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    return appData.getChildFile("DirectPipe");
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
    return targetFile.replaceWithText(json);
}

ControlConfig ControlMappingStore::load(const juce::File& file)
{
    auto sourceFile = file.getFullPathName().isEmpty() ? getDefaultConfigFile() : file;

    if (!sourceFile.existsAsFile()) {
        return createDefaults();
    }

    auto json = sourceFile.loadFileAsString();
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) {
        return createDefaults();
    }

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

    // Default hotkeys (Ctrl+Shift+1..9 for plugin bypass)
    for (int i = 1; i <= 9; ++i) {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = '0' + static_cast<uint32_t>(i);
        hk.action = {Action::PluginBypass, i - 1, 0.0f, "Plugin " + std::to_string(i) + " Bypass"};
        hk.displayName = "Ctrl+Shift+" + std::to_string(i);
        config.hotkeys.push_back(hk);
    }

    // Master bypass: Ctrl+Shift+0
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = '0';
        hk.action = {Action::MasterBypass, 0, 0.0f, "Master Bypass"};
        hk.displayName = "Ctrl+Shift+0";
        config.hotkeys.push_back(hk);
    }

    // Panic mute: Ctrl+Shift+M
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = 'M';
        hk.action = {Action::PanicMute, 0, 0.0f, "Panic Mute"};
        hk.displayName = "Ctrl+Shift+M";
        config.hotkeys.push_back(hk);
    }

    // Input mute toggle: Ctrl+Shift+N
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = 'N';
        hk.action = {Action::InputMuteToggle, 0, 0.0f, "Input Mute Toggle"};
        hk.displayName = "Ctrl+Shift+N";
        config.hotkeys.push_back(hk);
    }

    // Output mute toggle: Ctrl+Shift+O
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = 'O';
        hk.action = {Action::ToggleMute, 0, 0.0f, "output"};
        hk.displayName = "Ctrl+Shift+O";
        config.hotkeys.push_back(hk);
    }

    // Monitor toggle: Ctrl+Shift+H
    {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = 'H';
        hk.action = {Action::MonitorToggle, 0, 0.0f, ""};
        hk.displayName = "Ctrl+Shift+H";
        config.hotkeys.push_back(hk);
    }

    // Preset slots: Ctrl+Shift+F1..F5
    for (int i = 0; i < 5; ++i) {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL | HK_SHIFT;
        hk.virtualKey = VK_F1 + static_cast<uint32_t>(i);
        std::string label(1, 'A' + static_cast<char>(i));
        hk.action = {Action::SwitchPresetSlot, i, 0.0f, "Preset Slot " + label};
        hk.displayName = "Ctrl+Shift+F" + std::to_string(i + 1);
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
        event.action = static_cast<Action>(static_cast<int>(obj->getProperty("action")));
        event.intParam = obj->getProperty("intParam");
        event.floatParam = static_cast<float>(static_cast<double>(obj->getProperty("floatParam")));
        event.stringParam = obj->getProperty("stringParam").toString().toStdString();
        event.intParam2 = obj->getProperty("intParam2");
    }
    return event;
}

} // namespace directpipe
