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
    root->setProperty("version", 1);
    root->setProperty("exportDate",
        juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("appVersion",
        juce::String(ProjectInfo::versionString));

    // Audio settings (full preset)
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

    // Import audio settings
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
