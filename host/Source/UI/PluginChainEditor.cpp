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

    latencyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF8888AA));
    latencyLabel_.setFont(juce::Font(10.0f));
    latencyLabel_.setJustificationType(juce::Justification::centredRight);
    latencyLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(latencyLabel_);
    latencyLabel_.setVisible(false);

    editButton_.onClick = [this] {
        owner_.vstChain_.openPluginEditor(rowIndex_, &owner_);
    };

    bypassButton_.onClick = [this] {
        owner_.vstChain_.setPluginBypassed(rowIndex_, bypassButton_.getToggleState());
    };

    removeButton_.onClick = [this] {
        // IMPORTANT: Use slot->name ("Clear"), NOT nameLabel_.getText() ("1. Clear").
        // nameLabel_ includes row number prefix — mismatch causes silent delete failure.
        juce::String pluginName;
        if (auto* slot = owner_.vstChain_.getPluginSlot(rowIndex_))
            pluginName = slot->name;
        else
            return;
        auto safeOwner = juce::Component::SafePointer<PluginChainEditor>(&owner_);
        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Remove Plugin")
            .withMessage("Remove \"" + pluginName + "\" from the chain?")
            .withButton("Remove")
            .withButton("Cancel");
        juce::AlertWindow::showAsync(options, [safeOwner, pluginName](int result) {
            if (result != 1 || !safeOwner) return;
            // Re-find plugin by name (chain may have changed while dialog was open)
            for (int i = 0; i < safeOwner->vstChain_.getPluginCount(); ++i) {
                if (auto* s = safeOwner->vstChain_.getPluginSlot(i)) {
                    if (s->name == pluginName) {
                        safeOwner->vstChain_.removePlugin(i);
                        return;
                    }
                }
            }
        });
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
    latencyLabel_.setBounds(bounds.removeFromRight(60));
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
        juce::String displayName = juce::String(rowIndex_ + 1) + ". " + slot->name;
        if (slot->type != PluginSlot::Type::VST)
            displayName += " (Built-in)";
        nameLabel_.setText(displayName, juce::dontSendNotification);
        bypassButton_.setToggleState(slot->bypassed, juce::dontSendNotification);
    }
}

void PluginChainEditor::PluginRowComponent::updateLatency(int samples)
{
    if (samples > 0) {
        latencyLabel_.setText(juce::String(samples) + "smp", juce::dontSendNotification);
        latencyLabel_.setVisible(true);
    } else {
        latencyLabel_.setVisible(false);
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
        owner_.vstChain_.movePlugin(PluginIndex{fromIndex}, PluginIndex{rowIndex_});
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

    // Safety Limiter toggle — above the action buttons
    limiterButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xFFE0E0E0));
    limiterButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFFFF6B6B));
    limiterButton_.onClick = [this] {
        if (onLimiterToggled) onLimiterToggled(limiterButton_.getToggleState());
    };
    addAndMakeVisible(limiterButton_);

    chainPDCLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFFAA33));
    chainPDCLabel_.setFont(juce::Font(11.0f));
    chainPDCLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(chainPDCLabel_);
    chainPDCLabel_.setVisible(false);

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

void PluginChainEditor::paint(juce::Graphics& g)
{
    if (loading_) {
        g.setColour(juce::Colour(0xCC1A1A2E));
        g.fillRect(pluginList_.getBounds());
        g.setColour(juce::Colour(0xFF7B6FFF));
        g.setFont(16.0f);
        g.drawText("Loading...", pluginList_.getBounds(), juce::Justification::centred);
    }
}

void PluginChainEditor::showLoadingState()
{
    loading_ = true;
    pluginList_.setEnabled(false);
    addButton_.setEnabled(false);
    scanButton_.setEnabled(false);
    removeButton_.setEnabled(false);
    repaint();
}

void PluginChainEditor::setLimiterState(bool enabled)
{
    if (limiterButton_.getToggleState() != enabled)
        limiterButton_.setToggleState(enabled, juce::dontSendNotification);
}

void PluginChainEditor::hideLoadingState()
{
    loading_ = false;
    pluginList_.setEnabled(true);
    addButton_.setEnabled(true);
    scanButton_.setEnabled(true);
    removeButton_.setEnabled(true);
    refreshList();
}

void PluginChainEditor::resized()
{
    auto bounds = getLocalBounds();

    // Bottom bar: limiter toggle + PDC label + action buttons
    auto buttonBar = bounds.removeFromBottom(30);
    auto limiterBar = bounds.removeFromBottom(26);
    auto pdcBar = bounds.removeFromBottom(20);
    chainPDCLabel_.setBounds(pdcBar);
    limiterButton_.setBounds(limiterBar);

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

    if (rowNumber < static_cast<int>(perPluginLatencies_.size()))
        row->updateLatency(perPluginLatencies_[static_cast<size_t>(rowNumber)]);
    else
        row->updateLatency(0);

    return row;
}

void PluginChainEditor::showAddPluginMenu()
{
    juce::PopupMenu menu;

    // Built-in processors submenu
    {
        juce::PopupMenu builtinMenu;
        builtinMenu.addItem(901, "Filter (HPF + LPF)");
        builtinMenu.addItem(902, "Noise Removal (RNNoise)");
        builtinMenu.addItem(903, "Auto Gain (LUFS AGC)");
        menu.addSubMenu("Built-in", builtinMenu);
        menu.addSeparator();
    }

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
            if (result == 901) {
                auto r = safeThis->vstChain_.addBuiltinProcessor(PluginSlot::Type::BuiltinFilter);
                if (!r) juce::Logger::writeToLog("[VST] Failed to add Filter: " + r.message);
            } else if (result == 902) {
                auto r = safeThis->vstChain_.addBuiltinProcessor(PluginSlot::Type::BuiltinNoiseRemoval);
                if (!r) juce::Logger::writeToLog("[VST] Failed to add Noise Removal: " + r.message);
            } else if (result == 903) {
                auto r = safeThis->vstChain_.addBuiltinProcessor(PluginSlot::Type::BuiltinAutoGain);
                if (!r) juce::Logger::writeToLog("[VST] Failed to add Auto Gain: " + r.message);
            }
            else if (result == 1) {
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
    juce::String pluginFilter;
#if JUCE_WINDOWS
    pluginFilter = "*.vst3;*.dll";
#elif JUCE_MAC
    pluginFilter = "*.vst3;*.vst;*.component";
#else
    pluginFilter = "*.vst3;*.so";
#endif
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select VST Plugin",
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory),
        pluginFilter);

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

        auto safeThis = juce::Component::SafePointer<PluginChainEditor>(this);
        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Remove Plugin")
            .withMessage("Remove \"" + pluginName + "\" from the chain?")
            .withButton("Remove")
            .withButton("Cancel");
        juce::AlertWindow::showAsync(options, [safeThis, pluginName](int result) {
            if (result != 1 || !safeThis) return;
            // Re-find plugin by name (chain may have changed while dialog was open)
            for (int i = 0; i < safeThis->vstChain_.getPluginCount(); ++i) {
                if (auto* s = safeThis->vstChain_.getPluginSlot(i)) {
                    if (s->name == pluginName) {
                        safeThis->vstChain_.removePlugin(i);
                        return;
                    }
                }
            }
        });
    }
}

void PluginChainEditor::updateLatencyDisplay(const std::vector<int>& perPluginSamples,
                                              int totalPDC, double sampleRate)
{
    perPluginLatencies_ = perPluginSamples;

    if (totalPDC > 0 && sampleRate > 0.0) {
        float ms = static_cast<float>(totalPDC) / static_cast<float>(sampleRate) * 1000.0f;
        chainPDCLabel_.setText(
            "Chain PDC: " + juce::String(totalPDC) + " samples ("
            + juce::String(ms, 1) + "ms @ " + juce::String(static_cast<int>(sampleRate)) + "Hz)",
            juce::dontSendNotification);
        chainPDCLabel_.setVisible(true);
    } else {
        chainPDCLabel_.setVisible(false);
    }

    pluginList_.repaint();
}

void PluginChainEditor::refreshList()
{
    pluginList_.updateContent();
    pluginList_.repaint();
}

} // namespace directpipe
