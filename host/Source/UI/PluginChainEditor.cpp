/**
 * @file PluginChainEditor.cpp
 * @brief VST plugin chain editor implementation
 */

#include "PluginChainEditor.h"

namespace directpipe {

// ─── PluginRowComponent ─────────────────────────────────────────

PluginChainEditor::PluginRowComponent::PluginRowComponent(
    PluginChainEditor& owner, int rowIndex)
    : owner_(owner), rowIndex_(rowIndex)
{
    addAndMakeVisible(nameLabel_);
    addAndMakeVisible(editButton_);
    addAndMakeVisible(bypassButton_);
    addAndMakeVisible(removeButton_);

    nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);

    editButton_.onClick = [this] {
        owner_.vstChain_.openPluginEditor(rowIndex_, &owner_);
    };

    bypassButton_.onClick = [this] {
        owner_.vstChain_.setPluginBypassed(rowIndex_, bypassButton_.getToggleState());
    };

    removeButton_.onClick = [this] {
        owner_.vstChain_.removePlugin(rowIndex_);
        owner_.refreshList();
    };

    update(rowIndex);
}

void PluginChainEditor::PluginRowComponent::resized()
{
    auto bounds = getLocalBounds().reduced(2);
    int buttonWidth = 50;
    int toggleWidth = 40;
    int gap = 4;

    removeButton_.setBounds(bounds.removeFromRight(30));
    bounds.removeFromRight(gap);
    bypassButton_.setBounds(bounds.removeFromRight(toggleWidth));
    bounds.removeFromRight(gap);
    editButton_.setBounds(bounds.removeFromRight(buttonWidth));
    bounds.removeFromRight(gap);
    nameLabel_.setBounds(bounds);
}

void PluginChainEditor::PluginRowComponent::update(int newRowIndex)
{
    rowIndex_ = newRowIndex;

    if (auto* slot = owner_.vstChain_.getPluginSlot(rowIndex_)) {
        nameLabel_.setText(
            juce::String(rowIndex_ + 1) + ". " + slot->name,
            juce::dontSendNotification);
        bypassButton_.setToggleState(slot->bypassed, juce::dontSendNotification);
    }
}

// ─── PluginChainEditor ──────────────────────────────────────────

PluginChainEditor::PluginChainEditor(VSTChain& vstChain)
    : vstChain_(vstChain)
{
    addAndMakeVisible(pluginList_);
    pluginList_.setModel(this);
    pluginList_.setRowHeight(32);
    pluginList_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF2A2A40));

    addAndMakeVisible(addButton_);
    addAndMakeVisible(removeButton_);

    addButton_.onClick = [this] { addPlugin(); };
    removeButton_.onClick = [this] { removeSelectedPlugin(); };

    vstChain_.onChainChanged = [this] { refreshList(); };
}

PluginChainEditor::~PluginChainEditor()
{
    vstChain_.onChainChanged = nullptr;
}

void PluginChainEditor::paint(juce::Graphics& /*g*/)
{
}

void PluginChainEditor::resized()
{
    auto bounds = getLocalBounds();

    // Bottom bar with buttons
    auto buttonBar = bounds.removeFromBottom(30);
    addButton_.setBounds(buttonBar.removeFromLeft(150));
    buttonBar.removeFromLeft(5);
    removeButton_.setBounds(buttonBar.removeFromLeft(100));

    // Plugin list takes remaining space
    pluginList_.setBounds(bounds);
}

int PluginChainEditor::getNumRows()
{
    return vstChain_.getPluginCount();
}

void PluginChainEditor::paintListBoxItem(int /*rowNumber*/, juce::Graphics& g,
                                          int width, int height, bool rowIsSelected)
{
    if (rowIsSelected) {
        g.setColour(juce::Colour(0xFF3A3A5A));
        g.fillRect(0, 0, width, height);
    }
}

juce::Component* PluginChainEditor::refreshComponentForRow(
    int rowNumber, bool /*isRowSelected*/,
    juce::Component* existingComponentToUpdate)
{
    if (rowNumber < 0 || rowNumber >= vstChain_.getPluginCount()) {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* row = dynamic_cast<PluginRowComponent*>(existingComponentToUpdate);
    if (row) {
        row->update(rowNumber);
    } else {
        delete existingComponentToUpdate;
        row = new PluginRowComponent(*this, rowNumber);
    }

    return row;
}

void PluginChainEditor::addPlugin()
{
    // Open file chooser for VST plugin
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select VST Plugin",
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory),
        "*.vst3;*.dll");

    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc) {
        auto result = fc.getResult();
        if (result.existsAsFile()) {
            vstChain_.addPlugin(result.getFullPathName());
        }
    });
}

void PluginChainEditor::removeSelectedPlugin()
{
    int selected = pluginList_.getSelectedRow();
    if (selected >= 0) {
        vstChain_.removePlugin(selected);
        refreshList();
    }
}

void PluginChainEditor::refreshList()
{
    pluginList_.updateContent();
    pluginList_.repaint();
}

} // namespace directpipe
