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
 * @file MidiTab.h
 * @brief MIDI device selector + CC/Note mappings with [Learn] mode
 *        and 3-step plugin parameter mapping popup.
 */
#pragma once

#include <JuceHeader.h>
#include "../Control/ControlManager.h"
#include "../Audio/VSTChain.h"

namespace directpipe {

/**
 * @brief Tab content showing MIDI device selection and CC/Note mappings.
 *
 * Top section: MIDI device selector combo box with a [Rescan] button.
 * Bottom section: scrollable list of CC/Note-to-action mappings, each
 * with a [Learn] button that enters MIDI Learn mode.
 */
class MidiTab : public juce::Component,
                public juce::Timer {
public:
    /**
     * @brief Construct the MIDI tab.
     * @param manager Reference to the control manager.
     */
    explicit MidiTab(ControlManager& manager, VSTChain* vstChain = nullptr);
    ~MidiTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Rebuild the device list and binding list from current state.
     */
    void refreshAll();

private:
    void timerCallback() override;

    /** @brief Refresh the MIDI device combo box. */
    void refreshDeviceList();

    /** @brief Rebuild the mapping rows from the handler. */
    void refreshMappings();

    /** @brief Handle device selection change. */
    void onDeviceSelected();

    /** @brief Handle [Rescan] button. */
    void onRescanClicked();

    /** @brief Handle [Learn] button click. */
    void onLearnClicked(int mappingIndex);

    /** @brief Handle [Remove] button click. */
    void onRemoveClicked(int mappingIndex);

    /**
     * @brief Convert a MIDI binding to a display string (e.g., "CC 7 Ch 1").
     * @param binding The MIDI binding.
     * @return Human-readable representation.
     */
    static juce::String midiBindingToString(const MidiBinding& binding);

    ControlManager& manager_;

    // Device selection
    juce::Label deviceLabel_{"", "MIDI Device:"};
    juce::ComboBox deviceCombo_;
    juce::TextButton rescanButton_{"Rescan"};

    // Header
    juce::Label mappingHeaderLabel_{"", "MIDI Mappings"};

    // Scrollable mapping list
    juce::Viewport viewport_;
    juce::Component rowContainer_;

    // Add buttons
    juce::TextButton addMappingButton_{"+ Add Mapping"};
    juce::TextButton addParamButton_{"+ Plugin Param"};

    /// One UI row per MIDI mapping
    struct MappingRow {
        juce::Label controlLabel;   // e.g., "CC 7 Ch 1"
        juce::Label actionLabel;    // e.g., "ToggleMute"
        juce::TextButton learnButton{"Learn"};
        juce::TextButton removeButton{"X"};
    };
    juce::OwnedArray<MappingRow> rows_;

    // Status label + cancel button (shown during learn mode)
    juce::Label statusLabel_{"", ""};
    juce::TextButton cancelButton_{"Cancel"};

    // Index of the mapping currently in learn mode (-1 = none, -2 = new binding)
    int learningIndex_ = -1;

    // VSTChain pointer for plugin parameter mapping
    VSTChain* vstChain_ = nullptr;

    /** @brief Cancel in-progress MIDI Learn. */
    void onCancelClicked();

    /** @brief Handle [Add Mapping] button click. */
    void onAddMappingClicked();

    /** @brief Handle [Add Plugin Param] button click — 3-step popup flow. */
    void onAddParamClicked();

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kRowAltColour   = 0xFF252540;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;
    static constexpr juce::uint32 kWarningColour  = 0xFFFFAA33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTab)
};

} // namespace directpipe
