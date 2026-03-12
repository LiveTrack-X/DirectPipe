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
 * @file HotkeyTab.h
 * @brief Hotkey bindings tab — action-to-shortcut mappings with [Set] recording
 *        and drag-and-drop reorder.
 */
#pragma once

#include <JuceHeader.h>
#include "../Control/ControlManager.h"

namespace directpipe {

/**
 * @brief Tab content showing hotkey bindings with inline recording.
 *
 * Displays a scrollable list of action-to-shortcut bindings.
 * Each row has a [Set] button that enters recording mode and captures
 * the next keypress as the new shortcut.
 */
class HotkeyTab : public juce::Component,
                   public juce::Timer,
                   public juce::DragAndDropContainer {
public:
    /**
     * @brief Construct the Hotkey tab.
     * @param manager Reference to the control manager.
     */
    explicit HotkeyTab(ControlManager& manager);
    ~HotkeyTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Rebuild the binding list from the current handler state.
     */
    void refreshBindings();

private:
    void timerCallback() override;

    /** @brief Handle [Set] button click — enter recording mode for a binding. */
    void onSetClicked(int bindingIndex);

    /** @brief Handle [Remove] button click — remove a binding. */
    void onRemoveClicked(int bindingIndex);

    /** @brief Handle [Add] button click — show action selector and record key. */
    void onAddClicked();

    /** @brief Build the action menu with all available actions. */
    juce::PopupMenu buildActionMenu();

    ControlManager& manager_;

    // Header label
    juce::Label headerLabel_{"", "Keyboard Shortcuts"};

    // Add button
    juce::TextButton addButton_{"+ Add Shortcut"};

    // Scrollable viewport for the binding rows
    juce::Viewport viewport_;
    juce::Component rowContainer_;

    /// Per-row component — each row is both drag source and drop target
    class BindingRow : public juce::Component,
                       public juce::DragAndDropTarget {
    public:
        BindingRow(HotkeyTab& owner, int rowIndex);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void update(int newRowIndex, const juce::String& actionName,
                    const juce::String& shortcutName, bool alt);

        void mouseDrag(const juce::MouseEvent& e) override;

        bool isInterestedInDragSource(const SourceDetails&) override { return true; }
        void itemDragEnter(const SourceDetails&) override;
        void itemDragExit(const SourceDetails&) override;
        void itemDropped(const SourceDetails& details) override;

        int getRowIndex() const { return rowIndex_; }

    private:
        HotkeyTab& owner_;
        int rowIndex_;
        bool dragOver_ = false;
        bool altBg_ = false;

        juce::Label actionLabel_;
        juce::Label shortcutLabel_;
        juce::TextButton setButton_{"Set"};
        juce::TextButton removeButton_{"X"};
    };

    juce::OwnedArray<BindingRow> rows_;

    // Status label + cancel button (shown during recording)
    juce::Label statusLabel_{"", ""};
    juce::TextButton cancelButton_{"Cancel"};

    // Index of the binding currently being recorded (-1 = none)
    int recordingIndex_ = -1;

    /** @brief Cancel in-progress recording. */
    void onCancelClicked();

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kRowAltColour   = 0xFF252540;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;
    static constexpr juce::uint32 kWarningColour  = 0xFFFFAA33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HotkeyTab)
};

} // namespace directpipe
