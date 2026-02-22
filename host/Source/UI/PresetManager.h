/**
 * @file PresetManager.h
 * @brief Preset save/load management
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

/**
 * @brief Manages saving and loading of DirectPipe presets.
 *
 * Presets include:
 * - Input device settings
 * - VST plugin chain (plugins, order, bypass state)
 * - Output volume and enable states
 * - Buffer size and sample rate
 */
class PresetManager {
public:
    explicit PresetManager(AudioEngine& engine);

    /**
     * @brief Save current settings to a preset file.
     * @param file Target file path.
     * @return true if saved successfully.
     */
    bool savePreset(const juce::File& file);

    /**
     * @brief Load settings from a preset file.
     * @param file Source file path.
     * @return true if loaded successfully.
     */
    bool loadPreset(const juce::File& file);

    /**
     * @brief Get the default presets directory.
     */
    static juce::File getPresetsDirectory();

    /**
     * @brief Get list of available preset files.
     */
    juce::Array<juce::File> getAvailablePresets();

    /**
     * @brief Export current settings as JSON string.
     */
    juce::String exportToJSON();

    /**
     * @brief Import settings from JSON string.
     */
    bool importFromJSON(const juce::String& json);

private:
    AudioEngine& engine_;

    static constexpr const char* kPresetExtension = ".dppreset";
};

} // namespace directpipe
