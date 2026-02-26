/**
 * @file SettingsExporter.h
 * @brief Export/import all DirectPipe settings as a single JSON file
 */
#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"
#include "../Control/ControlMapping.h"

namespace directpipe {

/**
 * @brief Bundles all application settings into a single exportable JSON file (.dpbackup).
 *
 * Includes: audio settings, VST chain, control mappings, and all 5 quick preset slots.
 */
class SettingsExporter {
public:
    static juce::String exportAll(PresetManager& presetManager,
                                   ControlMappingStore& controlStore);

    static bool importAll(const juce::String& json,
                          PresetManager& presetManager,
                          ControlMappingStore& controlStore);

    static constexpr const char* kFileExtension = ".dpbackup";
};

} // namespace directpipe
