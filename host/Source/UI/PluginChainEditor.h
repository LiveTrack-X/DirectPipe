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
 * - Add / Remove plugins
 * - Reorder (drag & drop)
 * - Open plugin editor
 * - Bypass toggle
 */
class PluginChainEditor : public juce::Component,
                          public juce::ListBoxModel {
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

private:
    void addPlugin();
    void removeSelectedPlugin();
    void refreshList();

    VSTChain& vstChain_;

    juce::ListBox pluginList_{"VST Chain"};
    juce::TextButton addButton_{"+ Add Plugin"};
    juce::TextButton removeButton_{"Remove"};

    /**
     * @brief Custom row component with Edit/Bypass/Remove buttons.
     */
    class PluginRowComponent : public juce::Component {
    public:
        PluginRowComponent(PluginChainEditor& owner, int rowIndex);
        void resized() override;
        void update(int newRowIndex);

    private:
        PluginChainEditor& owner_;
        int rowIndex_;

        juce::Label nameLabel_;
        juce::TextButton editButton_{"Edit"};
        juce::ToggleButton bypassButton_{"BP"};
        juce::TextButton removeButton_{"X"};
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainEditor)
};

} // namespace directpipe
