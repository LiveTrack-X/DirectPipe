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
     * @brief Get the auto-save settings file path.
     */
    static juce::File getAutoSaveFile();

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

    // ─── Quick Preset Slots (A..E) ───

    static constexpr int kNumSlots = 5;

    /**
     * @brief Save current state to a quick slot (chain only).
     * @param slotIndex 0..4 (A..E)
     */
    bool saveSlot(int slotIndex);

    /**
     * @brief Load state from a quick slot (chain only, preserves audio/output settings).
     * @param slotIndex 0..4 (A..E)
     * @return true if slot existed and loaded successfully.
     */
    bool loadSlot(int slotIndex);

    /** @brief Export only the VST chain as JSON. */
    juce::String exportChainToJSON();

    /** @brief Import only the VST chain from JSON (preserves audio/output settings). */
    bool importChainFromJSON(const juce::String& json);

    /**
     * @brief Load a slot asynchronously (non-blocking for different chains).
     * @param slotIndex 0..4 (A..E)
     * @param onComplete Called on message thread when done (bool = success).
     */
    void loadSlotAsync(int slotIndex, std::function<void(bool)> onComplete);

    /**
     * @brief Check if a slot has saved data.
     */
    bool isSlotOccupied(int slotIndex) const;

    /**
     * @brief Get the active slot index (-1 if none).
     */
    int getActiveSlot() const { return activeSlot_; }

    /**
     * @brief Get slot label character ('A'..'E').
     */
    static char slotLabel(int slotIndex) { return 'A' + static_cast<char>(slotIndex); }

    /** @brief Get the file path for a quick slot. */
    static juce::File getSlotFile(int slotIndex);

private:

    AudioEngine& engine_;
    int activeSlot_ = -1;

    static constexpr const char* kPresetExtension = ".dppreset";
};

} // namespace directpipe
