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
 * @file MidiTab.cpp
 * @brief MIDI tab implementation — device selector, CC/Note mappings, Learn mode
 */

#include "MidiTab.h"

namespace directpipe {

// =============================================================================
//  MidiTab implementation
// =============================================================================

MidiTab::MidiTab(ControlManager& manager, VSTChain* vstChain)
    : manager_(manager), vstChain_(vstChain)
{
    // Device selector
    deviceLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(deviceLabel_);

    deviceCombo_.onChange = [this] { onDeviceSelected(); };
    addAndMakeVisible(deviceCombo_);

    rescanButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    rescanButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    rescanButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    rescanButton_.onClick = [this] { onRescanClicked(); };
    addAndMakeVisible(rescanButton_);

    // Mapping header
    mappingHeaderLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    mappingHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(mappingHeaderLabel_);

    // Add mapping buttons
    auto setupBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccentColour));
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };
    addMappingButton_.onClick = [this] { onAddMappingClicked(); };
    setupBtn(addMappingButton_);

    addParamButton_.onClick = [this] { onAddParamClicked(); };
    setupBtn(addParamButton_);
    addParamButton_.setVisible(vstChain_ != nullptr);

    // Status label
    statusLabel_.setFont(juce::Font(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(kWarningColour));
    addAndMakeVisible(statusLabel_);

    cancelButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    cancelButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    cancelButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cancelButton_.onClick = [this] { onCancelClicked(); };
    cancelButton_.setVisible(false);
    addAndMakeVisible(cancelButton_);

    // Scrollable viewport
    viewport_.setViewedComponent(&rowContainer_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    refreshAll();

    // Poll for learn completion at 10 Hz
    startTimerHz(10);
}

MidiTab::~MidiTab()
{
    stopTimer();

    // Cancel any in-progress learn (>= 0 = re-learning existing, -2 = adding new)
    if (learningIndex_ != -1) {
        manager_.getMidiHandler().stopLearn();
    }
}

void MidiTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void MidiTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;
    constexpr int labelW = 100;
    constexpr int btnW   = 70;

    int y = bounds.getY();

    // Device selector row
    deviceLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    rescanButton_.setBounds(bounds.getRight() - btnW, y, btnW, rowH);
    deviceCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - btnW - gap * 2, rowH);
    y += rowH + gap;

    // Mapping header + Add buttons (adaptive width)
    {
        int totalW = bounds.getWidth();
        bool showParam = addParamButton_.isVisible();
        int numBtns = showParam ? 2 : 1;
        int btnAreaW = totalW * numBtns / (numBtns + 1);
        int headerW = totalW - btnAreaW - gap;
        int singleBtnW = (btnAreaW - (numBtns > 1 ? gap : 0)) / numBtns;

        mappingHeaderLabel_.setBounds(bounds.getX(), y, headerW, rowH);
        addMappingButton_.setBounds(bounds.getX() + headerW + gap, y, singleBtnW, rowH);
        if (showParam)
            addParamButton_.setBounds(bounds.getRight() - singleBtnW, y, singleBtnW, rowH);
    }
    y += rowH + gap;

    // Status label + cancel button
    {
        constexpr int cancelW = 60;
        if (cancelButton_.isVisible()) {
            statusLabel_.setBounds(bounds.getX(), y, bounds.getWidth() - cancelW - gap, 20);
            cancelButton_.setBounds(bounds.getRight() - cancelW, y, cancelW, 20);
        } else {
            statusLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 20);
        }
    }
    y += 20 + gap;

    // Viewport fills the rest
    viewport_.setBounds(bounds.getX(), y, bounds.getWidth(), bounds.getBottom() - y);

    // Layout rows inside row container
    constexpr int innerRowH = 30;
    constexpr int innerGap  = 2;
    int totalH = static_cast<int>(rows_.size()) * (innerRowH + innerGap);
    rowContainer_.setSize(viewport_.getWidth() - viewport_.getScrollBarThickness(), totalH);

    int ry = 0;
    int containerW = rowContainer_.getWidth();
    constexpr int controlW   = 120;
    constexpr int learnBtnW  = 55;
    constexpr int removeBtnW = 28;

    for (auto* row : rows_) {
        int actionW = containerW - controlW - learnBtnW - removeBtnW - gap * 3;

        row->controlLabel.setBounds(0, ry, controlW, innerRowH);
        row->actionLabel.setBounds(controlW + gap, ry, actionW, innerRowH);
        row->learnButton.setBounds(controlW + gap + actionW + gap, ry, learnBtnW, innerRowH);
        row->removeButton.setBounds(containerW - removeBtnW, ry, removeBtnW, innerRowH);

        ry += innerRowH + innerGap;
    }
}

void MidiTab::refreshAll()
{
    refreshDeviceList();
    refreshMappings();
}

void MidiTab::refreshDeviceList()
{
    deviceCombo_.clear(juce::dontSendNotification);

    auto devices = manager_.getMidiHandler().getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i) {
        deviceCombo_.addItem(devices[i], i + 1);
    }

    if (devices.size() > 0) {
        deviceCombo_.setSelectedId(1, juce::dontSendNotification);
    }
}

void MidiTab::refreshMappings()
{
    // Remove old rows from the container
    for (auto* row : rows_) {
        rowContainer_.removeChildComponent(&row->controlLabel);
        rowContainer_.removeChildComponent(&row->actionLabel);
        rowContainer_.removeChildComponent(&row->learnButton);
        rowContainer_.removeChildComponent(&row->removeButton);
    }
    rows_.clear();

    auto& handler = manager_.getMidiHandler();
    const auto& bindings = handler.getBindings();

    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        auto* row = new MappingRow();
        const auto& binding = bindings[static_cast<size_t>(i)];

        // Control label (CC/Note info)
        row->controlLabel.setText(midiBindingToString(binding), juce::dontSendNotification);
        row->controlLabel.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
        row->controlLabel.setFont(juce::Font(12.0f, juce::Font::bold));

        // Action label
        row->actionLabel.setText(actionToDisplayName(binding.action),
                                 juce::dontSendNotification);
        row->actionLabel.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        row->actionLabel.setFont(juce::Font(12.0f));

        // Alternating row background
        if (i % 2 == 1) {
            row->controlLabel.setColour(juce::Label::backgroundColourId,
                                        juce::Colour(kRowAltColour));
            row->actionLabel.setColour(juce::Label::backgroundColourId,
                                       juce::Colour(kRowAltColour));
        }

        // [Learn] button
        row->learnButton.setColour(juce::TextButton::buttonColourId,
                                   juce::Colour(kSurfaceColour));
        row->learnButton.setColour(juce::TextButton::textColourOnId,
                                   juce::Colour(kTextColour));
        row->learnButton.setColour(juce::TextButton::textColourOffId,
                                   juce::Colour(kTextColour));
        row->learnButton.onClick = [this, i] { onLearnClicked(i); };

        // [X] remove button
        row->removeButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(kSurfaceColour));
        row->removeButton.setColour(juce::TextButton::textColourOnId,
                                    juce::Colour(0xFFE05050));
        row->removeButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colour(0xFFE05050));
        row->removeButton.onClick = [this, i] { onRemoveClicked(i); };

        rowContainer_.addAndMakeVisible(row->controlLabel);
        rowContainer_.addAndMakeVisible(row->actionLabel);
        rowContainer_.addAndMakeVisible(row->learnButton);
        rowContainer_.addAndMakeVisible(row->removeButton);

        rows_.add(row);
    }

    resized();
}

void MidiTab::timerCallback()
{
    // Check if learn mode has ended (>= 0 = re-learning existing, -2 = new binding)
    if (learningIndex_ != -1 && !manager_.getMidiHandler().isLearning()) {
        learningIndex_ = -1;
        statusLabel_.setText("", juce::dontSendNotification);
        cancelButton_.setVisible(false);
        refreshMappings();
    }
}

void MidiTab::onDeviceSelected()
{
    auto selectedText = deviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        manager_.getMidiHandler().openDevice(selectedText);
    }
}

void MidiTab::onRescanClicked()
{
    manager_.getMidiHandler().rescanDevices();
    refreshDeviceList();
}

void MidiTab::onLearnClicked(int mappingIndex)
{
    auto& handler = manager_.getMidiHandler();
    const auto& bindings = handler.getBindings();

    // Validate bounds BEFORE any UI mutation
    if (mappingIndex < 0 || mappingIndex >= static_cast<int>(bindings.size()))
        return;

    if (learningIndex_ >= 0) {
        handler.stopLearn();
    }

    learningIndex_ = mappingIndex;
    statusLabel_.setText("Move a MIDI control...", juce::dontSendNotification);
    cancelButton_.setVisible(true);
    resized();

    // Highlight the active row
    if (mappingIndex >= 0 && mappingIndex < rows_.size()) {
        rows_[mappingIndex]->controlLabel.setText("...", juce::dontSendNotification);
        rows_[mappingIndex]->controlLabel.setColour(juce::Label::textColourId,
                                                     juce::Colour(kWarningColour));
    }

    // Capture the action from the existing mapping
    ActionEvent targetAction = bindings[static_cast<size_t>(mappingIndex)].action;

    handler.startLearn(
        [&manager = this->manager_, mappingIndex, targetAction](int cc, int note, int channel,
                                            const juce::String& deviceName) {
            auto& h = manager.getMidiHandler();

            // Remove old binding (verify index still matches expected action to avoid stale index)
            auto bindings = h.getBindings();
            if (mappingIndex < static_cast<int>(bindings.size())
                && bindings[static_cast<size_t>(mappingIndex)].action == targetAction) {
                h.removeBinding(mappingIndex);
            }

            // Create new binding with learned CC/Note
            MidiBinding newBinding;
            newBinding.cc = cc;
            newBinding.note = note;
            newBinding.channel = channel;
            newBinding.deviceName = deviceName.toStdString();
            newBinding.action = targetAction;
            newBinding.type = (cc >= 0) ? MidiMappingType::Toggle : MidiMappingType::NoteOnOff;
            h.addBinding(newBinding);

            manager.saveConfig();
            // UI refresh will happen in timerCallback when isLearning() returns false
        });
}

void MidiTab::onRemoveClicked(int mappingIndex)
{
    auto& handler = manager_.getMidiHandler();

    if (mappingIndex >= 0 && mappingIndex < static_cast<int>(handler.getBindings().size())) {
        handler.removeBinding(mappingIndex);
        manager_.saveConfig();
        // Defer refresh to avoid deleting the button component from within its own callback
        auto safeThis = juce::Component::SafePointer<MidiTab>(this);
        juce::MessageManager::callAsync([safeThis] { if (safeThis) safeThis->refreshMappings(); });
    }
}

juce::String MidiTab::midiBindingToString(const MidiBinding& binding)
{
    juce::String result;

    if (binding.cc >= 0) {
        result = "CC " + juce::String(binding.cc);
    } else if (binding.note >= 0) {
        result = "Note " + juce::String(binding.note);
    } else {
        result = "(unset)";
    }

    if (binding.channel > 0) {
        result += " Ch " + juce::String(binding.channel);
    } else {
        result += " Ch *";
    }

    return result;
}

void MidiTab::onCancelClicked()
{
    if (learningIndex_ != -1) {
        manager_.getMidiHandler().stopLearn();
        learningIndex_ = -1;
        statusLabel_.setText("", juce::dontSendNotification);
        cancelButton_.setVisible(false);
        refreshMappings();
    }
}

void MidiTab::onAddMappingClicked()
{
    // Build an action menu similar to HotkeyTab
    juce::PopupMenu menu;

    juce::PopupMenu bypassMenu;
    for (int i = 1; i <= 16; ++i)
        bypassMenu.addItem(100 + i, "Plugin " + juce::String(i) + " Bypass");
    menu.addSubMenu("Plugin Bypass", bypassMenu);
    menu.addItem(200, "Master Bypass");
    menu.addItem(201, "Panic Mute");
    menu.addItem(202, "Input Mute Toggle");
    menu.addItem(203, "Output Mute Toggle");
    menu.addItem(204, "Monitor Toggle");
    menu.addItem(205, "IPC Toggle");
    menu.addItem(206, "Recording Toggle");
    menu.addItem(300, "Input Gain +1 dB");
    menu.addItem(301, "Input Gain -1 dB");

    juce::PopupMenu slotMenu;
    for (int i = 0; i < 5; ++i) {
        char label = 'A' + static_cast<char>(i);
        slotMenu.addItem(600 + i, "Preset Slot " + juce::String::charToString(label));
    }
    menu.addSubMenu("Preset Slot", slotMenu);
    menu.addItem(500, "Next Preset");
    menu.addItem(501, "Previous Preset");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addMappingButton_),
        [this](int result) {
            if (result == 0) return;

            ActionEvent action;
            if (result >= 100 && result < 200) {
                int idx = result - 100 - 1;
                action = {Action::PluginBypass, idx, 0.0f, "Plugin " + std::to_string(idx + 1) + " Bypass"};
            } else if (result == 200) {
                action = {Action::MasterBypass, 0, 0.0f, "Master Bypass"};
            } else if (result == 201) {
                action = {Action::PanicMute, 0, 0.0f, "Panic Mute"};
            } else if (result == 202) {
                action = {Action::InputMuteToggle, 0, 0.0f, "Input Mute Toggle"};
            } else if (result == 203) {
                action = {Action::ToggleMute, 0, 0.0f, "output"};
            } else if (result == 204) {
                action = {Action::MonitorToggle, 0, 0.0f, "Monitor Toggle"};
            } else if (result == 205) {
                action = {Action::IpcToggle, 0, 0.0f, "IPC Toggle"};
            } else if (result == 206) {
                action = {Action::RecordingToggle, 0, 0.0f, "Recording Toggle"};
            } else if (result == 300) {
                action = {Action::InputGainAdjust, 0, 1.0f, "Input Gain +1 dB"};
            } else if (result == 301) {
                action = {Action::InputGainAdjust, 0, -1.0f, "Input Gain -1 dB"};
            } else if (result == 500) {
                action = {Action::NextPreset, 0, 0.0f, "Next Preset"};
            } else if (result == 501) {
                action = {Action::PreviousPreset, 0, 0.0f, "Previous Preset"};
            } else if (result >= 600 && result < 700) {
                int slot = result - 600;
                char label = 'A' + static_cast<char>(slot);
                action = {Action::SwitchPresetSlot, slot, 0.0f, "Preset Slot " + std::string(1, label)};
            } else {
                return;
            }

            // Enter MIDI Learn mode
            statusLabel_.setText("Move a MIDI control for: " + juce::String(action.stringParam),
                                 juce::dontSendNotification);
            cancelButton_.setVisible(true);
            resized();
            learningIndex_ = -2;

            auto safeMidi = juce::Component::SafePointer<MidiTab>(this);
            manager_.getMidiHandler().startLearn(
                [safeMidi, action](int cc, int note, int channel, const juce::String& deviceName) {
                    if (!safeMidi) return;
                    MidiBinding newBinding;
                    newBinding.cc = cc;
                    newBinding.note = note;
                    newBinding.channel = channel;
                    newBinding.deviceName = deviceName.toStdString();
                    newBinding.action = action;
                    newBinding.type = (cc >= 0) ? MidiMappingType::Toggle : MidiMappingType::NoteOnOff;
                    safeMidi->manager_.getMidiHandler().addBinding(newBinding);
                    safeMidi->manager_.saveConfig();
                });
        });
}

void MidiTab::onAddParamClicked()
{
    if (vstChain_ == nullptr) return;

    int pluginCount = vstChain_->getPluginCount();
    if (pluginCount == 0) {
        statusLabel_.setText("No plugins loaded", juce::dontSendNotification);
        return;
    }

    // Step 1: Select plugin
    juce::PopupMenu pluginMenu;
    for (int i = 0; i < pluginCount; ++i) {
        auto* slot = vstChain_->getPluginSlot(i);
        if (slot)
            pluginMenu.addItem(i + 1, slot->name);
    }

    pluginMenu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&addParamButton_),
        [this](int pluginResult) {
            if (pluginResult == 0 || !vstChain_) return;
            int pluginIndex = pluginResult - 1;

            // Re-validate plugin index after async gap
            if (pluginIndex >= vstChain_->getPluginCount()) return;

            // Capture plugin name now (safe at this point)
            auto* pluginSlot = vstChain_->getPluginSlot(pluginIndex);
            juce::String pluginName = pluginSlot ? pluginSlot->name
                                                 : "Plugin " + juce::String(pluginIndex);

            // Step 2: Select parameter
            int paramCount = vstChain_->getPluginParameterCount(pluginIndex);
            if (paramCount == 0) {
                statusLabel_.setText("Plugin has no parameters", juce::dontSendNotification);
                return;
            }

            juce::PopupMenu paramMenu;
            int maxItems = juce::jmin(paramCount, 200);
            for (int p = 0; p < maxItems; ++p) {
                auto name = vstChain_->getPluginParameterName(pluginIndex, p);
                if (name.isEmpty()) name = "Param " + juce::String(p);
                paramMenu.addItem(p + 1, juce::String(p) + ": " + name);
            }

            paramMenu.showMenuAsync(
                juce::PopupMenu::Options().withTargetComponent(&addParamButton_),
                [this, pluginIndex, pluginName](int paramResult) {
                    if (paramResult == 0 || !vstChain_) return;
                    int paramIndex = paramResult - 1;

                    // Re-validate after second async gap
                    if (pluginIndex >= vstChain_->getPluginCount()) return;
                    if (paramIndex >= vstChain_->getPluginParameterCount(pluginIndex)) return;

                    auto paramName = vstChain_->getPluginParameterName(pluginIndex, paramIndex);

                    ActionEvent action;
                    action.action = Action::SetPluginParameter;
                    action.intParam = pluginIndex;
                    action.intParam2 = paramIndex;
                    action.floatParam = 0.0f;
                    action.stringParam = (pluginName + " > " + paramName).toStdString();

                    // Step 3: MIDI Learn
                    statusLabel_.setText("Move a MIDI CC for: " + pluginName + " > " + paramName,
                                         juce::dontSendNotification);
                    cancelButton_.setVisible(true);
                    resized();
                    learningIndex_ = -2;

                    auto safeParam = juce::Component::SafePointer<MidiTab>(this);
                    manager_.getMidiHandler().startLearn(
                        [safeParam, action](int cc, int note, int channel, const juce::String& deviceName) {
                            if (!safeParam) return;
                            MidiBinding newBinding;
                            newBinding.cc = cc;
                            newBinding.note = note;
                            newBinding.channel = channel;
                            newBinding.deviceName = deviceName.toStdString();
                            newBinding.action = action;
                            newBinding.type = (cc >= 0) ? MidiMappingType::Continuous
                                                        : MidiMappingType::NoteOnOff;
                            safeParam->manager_.getMidiHandler().addBinding(newBinding);
                            safeParam->manager_.saveConfig();
                        });
                });
        });
}

} // namespace directpipe
