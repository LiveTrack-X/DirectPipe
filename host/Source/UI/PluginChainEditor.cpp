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
 * @file PluginChainEditor.cpp
 * @brief VST plugin chain editor implementation
 */

#include "PluginChainEditor.h"
#include "PluginScanner.h"

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
    nameLabel_.setInterceptsMouseClicks(false, false);

    editButton_.onClick = [this] {
        owner_.vstChain_.openPluginEditor(rowIndex_, &owner_);
    };

    bypassButton_.onClick = [this] {
        owner_.vstChain_.setPluginBypassed(rowIndex_, bypassButton_.getToggleState());
    };

    removeButton_.onClick = [this] {
        auto pluginName = nameLabel_.getText();
        auto safeOwner = juce::Component::SafePointer<PluginChainEditor>(&owner_);
        int idx = rowIndex_;
        bool ok = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon,
            "Remove Plugin",
            "Remove \"" + pluginName + "\" from the chain?",
            "Remove", "Cancel", nullptr, nullptr);
        if (ok) {
            juce::MessageManager::callAsync([safeOwner, idx] {
                if (safeOwner)
                    safeOwner->vstChain_.removePlugin(idx);
            });
        }
    };

    update(rowIndex);
}

void PluginChainEditor::PluginRowComponent::resized()
{
    auto bounds = getLocalBounds().reduced(2);
    int gap = 4;

    removeButton_.setBounds(bounds.removeFromRight(28));
    bounds.removeFromRight(gap);
    bypassButton_.setBounds(bounds.removeFromRight(70));
    bounds.removeFromRight(gap);
    editButton_.setBounds(bounds.removeFromRight(40));
    bounds.removeFromRight(gap);
    nameLabel_.setBounds(bounds);
}

void PluginChainEditor::PluginRowComponent::paint(juce::Graphics& g)
{
    if (dragOver_) {
        g.setColour(juce::Colour(0xFF5050FF));
        g.drawRect(getLocalBounds(), 2);
    }
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

void PluginChainEditor::PluginRowComponent::mouseDown(const juce::MouseEvent& /*e*/)
{
    owner_.pluginList_.selectRow(rowIndex_);
}

void PluginChainEditor::PluginRowComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 5) {
        if (auto* ddc = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
            if (!ddc->isDragAndDropActive())
                ddc->startDragging(juce::var(rowIndex_), this);
        }
    }
}

bool PluginChainEditor::PluginRowComponent::isInterestedInDragSource(
    const SourceDetails& details)
{
    return details.description.isInt();
}

void PluginChainEditor::PluginRowComponent::itemDragEnter(const SourceDetails&)
{
    dragOver_ = true;
    repaint();
}

void PluginChainEditor::PluginRowComponent::itemDragExit(const SourceDetails&)
{
    dragOver_ = false;
    repaint();
}

void PluginChainEditor::PluginRowComponent::itemDropped(const SourceDetails& details)
{
    dragOver_ = false;
    repaint();

    int fromIndex = static_cast<int>(details.description);
    if (fromIndex != rowIndex_) {
        owner_.vstChain_.movePlugin(fromIndex, rowIndex_);
        owner_.pluginList_.selectRow(rowIndex_);
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
    addAndMakeVisible(scanButton_);
    addAndMakeVisible(removeButton_);

    addButton_.onClick = [this] { showAddPluginMenu(); };
    scanButton_.onClick = [this] { openScannerDialog(); };
    removeButton_.onClick = [this] { removeSelectedPlugin(); };

    vstChain_.onChainChanged = [this] {
        refreshList();
        if (onChainModified) onChainModified();
    };
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

    // Bottom bar with buttons (evenly distributed)
    auto buttonBar = bounds.removeFromBottom(30);
    int gap = 4;
    int btnW = (buttonBar.getWidth() - gap * 2) / 3;
    int lastW = buttonBar.getWidth() - (btnW + gap) * 2;
    addButton_.setBounds(buttonBar.getX(), buttonBar.getY(), btnW, buttonBar.getHeight());
    scanButton_.setBounds(buttonBar.getX() + btnW + gap, buttonBar.getY(), btnW, buttonBar.getHeight());
    removeButton_.setBounds(buttonBar.getX() + (btnW + gap) * 2, buttonBar.getY(), lastW, buttonBar.getHeight());

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

void PluginChainEditor::showAddPluginMenu()
{
    juce::PopupMenu menu;

    // Add scanned plugins as submenu
    auto& knownPlugins = vstChain_.getKnownPlugins();
    auto types = knownPlugins.getTypes();

    if (!types.isEmpty()) {
        // Group by vendor
        std::map<juce::String, juce::PopupMenu> vendorMenus;
        for (int i = 0; i < types.size(); ++i) {
            auto vendor = types[i].manufacturerName.isEmpty() ?
                          juce::String("Unknown") : types[i].manufacturerName;
            vendorMenus[vendor].addItem(1000 + i, types[i].name +
                " (" + types[i].pluginFormatName + ")");
        }

        juce::PopupMenu scannedMenu;
        for (auto& [vendor, submenu] : vendorMenus) {
            scannedMenu.addSubMenu(vendor, submenu);
        }
        menu.addSubMenu("Scanned Plugins (" + juce::String(types.size()) + ")", scannedMenu);
        menu.addSeparator();
    }

    menu.addItem(1, "Browse for plugin file...");
    menu.addItem(2, "Open Scanner...");

    auto safeThis = juce::Component::SafePointer<PluginChainEditor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
        [safeThis, types](int result) {
            if (!safeThis) return;
            if (result == 1) {
                safeThis->addPluginFromFile();
            } else if (result == 2) {
                safeThis->openScannerDialog();
            } else if (result >= 1000) {
                int idx = result - 1000;
                if (idx < types.size())
                    safeThis->addPluginFromDescription(types[idx]);
            }
        });
}

void PluginChainEditor::openScannerDialog()
{
    auto* scanner = new PluginScannerComponent(vstChain_);
    scanner->setSize(550, 500);

    auto safeThis = juce::Component::SafePointer<PluginChainEditor>(this);
    scanner->onPluginSelected = [safeThis](const juce::PluginDescription& desc) {
        if (safeThis) safeThis->addPluginFromDescription(desc);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(scanner);
    options.dialogTitle = "VST Plugin Scanner";
    options.dialogBackgroundColour = juce::Colour(0xFF1E1E2E);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void PluginChainEditor::addPluginFromDescription(const juce::PluginDescription& desc)
{
    // Use callAsync to prevent scanner dialog freeze during plugin loading
    auto descCopy = desc;
    auto safeThis = juce::Component::SafePointer<PluginChainEditor>(this);
    juce::MessageManager::callAsync([safeThis, descCopy] {
        if (safeThis) safeThis->vstChain_.addPlugin(descCopy);
    });
}

void PluginChainEditor::addPluginFromFile()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select VST Plugin",
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory),
        "*.vst3;*.dll");

    auto safeThis = juce::Component::SafePointer<PluginChainEditor>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [safeThis, chooser](const juce::FileChooser& fc) {
        if (!safeThis) return;
        auto result = fc.getResult();
        if (result.existsAsFile()) {
            safeThis->vstChain_.addPlugin(result.getFullPathName());
        }
    });
}

void PluginChainEditor::removeSelectedPlugin()
{
    int selected = pluginList_.getSelectedRow();
    if (selected >= 0) {
        juce::String pluginName;
        if (auto* slot = vstChain_.getPluginSlot(selected))
            pluginName = slot->name;
        else
            pluginName = "Plugin " + juce::String(selected + 1);

        bool ok = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon,
            "Remove Plugin",
            "Remove \"" + pluginName + "\" from the chain?",
            "Remove", "Cancel", nullptr, nullptr);
        if (ok)
            vstChain_.removePlugin(selected);
    }
}

void PluginChainEditor::refreshList()
{
    pluginList_.updateContent();
    pluginList_.repaint();
}

} // namespace directpipe
