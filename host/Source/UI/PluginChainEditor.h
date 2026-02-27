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
 * @file PluginChainEditor.h
 * @brief VST plugin chain editor UI component
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/VSTChain.h"

namespace directpipe {

/**
 * @brief UI component for managing the VST plugin chain.
 *
 * Displays the list of plugins with controls for:
 * - Add / Remove plugins (from scanned list or file)
 * - Reorder (drag & drop)
 * - Open plugin editor
 * - Bypass toggle
 * - Open plugin scanner dialog
 */
class PluginChainEditor : public juce::Component,
                          public juce::ListBoxModel,
                          public juce::DragAndDropContainer {
public:
    explicit PluginChainEditor(VSTChain& vstChain);
    ~PluginChainEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g,
                          int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected,
                                            juce::Component* existingComponentToUpdate) override;

    void refreshList();

    /** @brief Called when chain content changes (add/remove/reorder/bypass). */
    std::function<void()> onChainModified;

private:
    friend class PluginRowComponent;

    void showAddPluginMenu();
    void openScannerDialog();
    void addPluginFromDescription(const juce::PluginDescription& desc);
    void addPluginFromFile();
    void removeSelectedPlugin();

    VSTChain& vstChain_;

    juce::ListBox pluginList_{"VST Chain"};
    juce::TextButton addButton_{"+ Add Plugin"};
    juce::TextButton scanButton_{"Scan..."};
    juce::TextButton removeButton_{"Remove"};

    /**
     * @brief Custom row component with drag-and-drop, Edit/Bypass/Remove buttons.
     */
    class PluginRowComponent : public juce::Component,
                               public juce::DragAndDropTarget {
    public:
        PluginRowComponent(PluginChainEditor& owner, int rowIndex);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void update(int newRowIndex);

        // Mouse handling for row selection and drag initiation
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;

        // DragAndDropTarget
        bool isInterestedInDragSource(const SourceDetails& details) override;
        void itemDragEnter(const SourceDetails& details) override;
        void itemDragExit(const SourceDetails& details) override;
        void itemDropped(const SourceDetails& details) override;

    private:
        PluginChainEditor& owner_;
        int rowIndex_;
        bool dragOver_ = false;

        juce::Label nameLabel_;
        juce::TextButton editButton_{"Edit"};
        juce::ToggleButton bypassButton_{"Bypass"};
        juce::TextButton removeButton_{"X"};
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainEditor)
};

} // namespace directpipe
