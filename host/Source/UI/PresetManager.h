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
    struct TargetPlugin {
        juce::String name, path;
        juce::PluginDescription desc;
        bool hasDesc = false;
        bool bypassed = false;
        juce::MemoryBlock stateData;
        bool hasState = false;
    };

    static std::vector<TargetPlugin> parseTargetPlugins(const juce::Array<juce::var>* pluginsArray);
    static bool isSameChain(const std::vector<TargetPlugin>& targets, VSTChain& chain);
    static void applyFastPath(const std::vector<TargetPlugin>& targets, VSTChain& chain);
    static void applySlowPath(const std::vector<TargetPlugin>& targets, VSTChain& chain);

    AudioEngine& engine_;
    int activeSlot_ = -1;

    static constexpr const char* kPresetExtension = ".dppreset";
};

} // namespace directpipe
