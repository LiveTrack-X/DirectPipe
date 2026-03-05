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
 * @file SettingsExporter.h
 * @brief Export/import all DirectPipe settings as a single JSON file
 */
#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"
#include "../Control/ControlMapping.h"

namespace directpipe {

/**
 * @brief Bundles application settings into a single exportable JSON file (.dpbackup).
 *
 * Includes: audio device settings, output/monitor settings, and control mappings.
 * Does NOT include VST chain or preset slots (those are managed via slots A-E).
 */
class SettingsExporter {
public:
    /** Export settings only (audio, output, controls). No VST chain or slots. */
    static juce::String exportAll(PresetManager& presetManager,
                                   ControlMappingStore& controlStore);

    /** Import settings only. VST chain and slots are left untouched. */
    static bool importAll(const juce::String& json,
                          PresetManager& presetManager,
                          ControlMappingStore& controlStore);

    /** Export everything: settings + VST chain + all preset slots. */
    static juce::String exportFullBackup(PresetManager& presetManager,
                                          ControlMappingStore& controlStore);

    /** Import everything: settings + VST chain + all preset slots. */
    static bool importFullBackup(const juce::String& json,
                                  PresetManager& presetManager,
                                  ControlMappingStore& controlStore);

    static constexpr const char* kFileExtension = ".dpbackup";
    static constexpr const char* kFullBackupExtension = ".dpfullbackup";
};

} // namespace directpipe
