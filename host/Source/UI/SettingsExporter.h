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
