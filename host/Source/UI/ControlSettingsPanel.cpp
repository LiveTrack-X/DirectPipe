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
 * @file ControlSettingsPanel.cpp
 * @brief Tabbed control settings panel implementation
 */

#include "ControlSettingsPanel.h"

#if JUCE_WINDOWS
#endif

namespace directpipe {

static juce::String actionToDisplayName(const ActionEvent& event)
{
    // Handle actions that use stringParam for display first
    switch (event.action) {
        case Action::ToggleMute:
            if (event.stringParam == "output")  return "Output Mute Toggle";
            if (event.stringParam == "monitor") return "Monitor Mute Toggle";
            return "Toggle Mute";
        case Action::SetPluginParameter:
            if (!event.stringParam.empty())
                return juce::String(event.stringParam);
            return "Plugin[" + juce::String(event.intParam) + "].Param[" +
                   juce::String(event.intParam2) + "]";
        default:
            break;
    }

    // For other actions, prefer stringParam as friendly display name
    if (!event.stringParam.empty())
        return juce::String(event.stringParam);

    switch (event.action) {
        case Action::PluginBypass:    return "Plugin " + juce::String(event.intParam + 1) + " Bypass";
        case Action::MasterBypass:    return "Master Bypass";
        case Action::SetVolume:       return "Set Volume";
        case Action::LoadPreset:      return "Load Preset";
        case Action::PanicMute:       return "Panic Mute";
        case Action::InputGainAdjust: return "Input Gain Adjust";
        case Action::NextPreset:      return "Next Preset";
        case Action::PreviousPreset:  return "Previous Preset";
        case Action::InputMuteToggle: return "Input Mute Toggle";
        case Action::SwitchPresetSlot: {
            char label = 'A' + static_cast<char>(event.intParam);
            return "Preset Slot " + juce::String::charToString(label);
        }
        case Action::MonitorToggle:   return "Monitor Toggle";
        case Action::RecordingToggle: return "Recording Toggle";
        case Action::IpcToggle:       return "IPC Toggle";
        default:                      return "Unknown";
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  HotkeyTab::BindingRow — per-row drag source + drop target
// ═════════════════════════════════════════════════════════════════════════════

HotkeyTab::BindingRow::BindingRow(HotkeyTab& owner, int rowIndex)
    : owner_(owner), rowIndex_(rowIndex)
{
    // Action label — mouse clicks pass through to row (for drag initiation)
    actionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    actionLabel_.setFont(juce::Font(12.0f));
    actionLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(actionLabel_);

    // Shortcut label (prominent badge style) — mouse clicks pass through
    shortcutLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFAA99FF));
    shortcutLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF3D3870));
    shortcutLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    shortcutLabel_.setJustificationType(juce::Justification::centred);
    shortcutLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(shortcutLabel_);

    // [Set] button
    setButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    setButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    setButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    setButton_.onClick = [this] { owner_.onSetClicked(rowIndex_); };
    addAndMakeVisible(setButton_);

    // [X] remove button
    removeButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    removeButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFE05050));
    removeButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE05050));
    removeButton_.onClick = [this] { owner_.onRemoveClicked(rowIndex_); };
    addAndMakeVisible(removeButton_);
}

void HotkeyTab::BindingRow::update(int newRowIndex, const juce::String& actionName,
                                    const juce::String& shortcutName, bool alt)
{
    rowIndex_ = newRowIndex;
    altBg_ = alt;
    actionLabel_.setText(actionName, juce::dontSendNotification);
    shortcutLabel_.setText(shortcutName, juce::dontSendNotification);
    setButton_.onClick = [this] { owner_.onSetClicked(rowIndex_); };
    removeButton_.onClick = [this] { owner_.onRemoveClicked(rowIndex_); };
}

void HotkeyTab::BindingRow::resized()
{
    auto bounds = getLocalBounds();
    constexpr int gap = 4;
    constexpr int btnW = 44;
    constexpr int removeBtnW = 26;

    int usable = bounds.getWidth() - btnW - removeBtnW - gap * 3;
    int actionW = usable / 2;
    int shortcutW = usable - actionW;

    int x = 0;
    actionLabel_.setBounds(x, 0, actionW, bounds.getHeight());
    x += actionW + gap;
    shortcutLabel_.setBounds(x, 0, shortcutW, bounds.getHeight());
    x += shortcutW + gap;
    setButton_.setBounds(x, 0, btnW, bounds.getHeight());
    x += btnW + gap;
    removeButton_.setBounds(x, 0, removeBtnW, bounds.getHeight());
}

void HotkeyTab::BindingRow::paint(juce::Graphics& g)
{
    // Alternating row background
    if (altBg_)
        g.fillAll(juce::Colour(kRowAltColour));

    // Drag-over highlight (like PluginChainEditor)
    if (dragOver_) {
        g.setColour(juce::Colour(0xFF5050FF));
        g.drawRect(getLocalBounds(), 2);
    }
}

void HotkeyTab::BindingRow::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 5) return;

    if (auto* ddc = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
        if (!ddc->isDragAndDropActive()) {
            // Create a snapshot of this row as drag image
            auto b = getLocalBounds();
            juce::Image dragImage(juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
            juce::Graphics g(dragImage);

            g.setColour(juce::Colour(0xDD2A2A40));
            g.fillRoundedRectangle(b.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xFF6C63FF));
            g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 4.0f, 1.0f);

            // Action text
            g.setColour(juce::Colour(0xFFB0B0C0));
            g.setFont(juce::Font(12.0f));
            g.drawText(actionLabel_.getText(), 8, 0, b.getWidth() / 2 - 12,
                       b.getHeight(), juce::Justification::centredLeft);

            // Shortcut badge
            auto shortcutText = shortcutLabel_.getText();
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            int badgeX = b.getWidth() / 2;
            int badgeW = b.getWidth() / 2 - 80;
            g.setColour(juce::Colour(0xFF3D3870));
            g.fillRoundedRectangle(static_cast<float>(badgeX), 4.0f,
                                   static_cast<float>(badgeW),
                                   static_cast<float>(b.getHeight() - 8), 3.0f);
            g.setColour(juce::Colour(0xFFAA99FF));
            g.drawText(shortcutText, badgeX, 0, badgeW, b.getHeight(),
                       juce::Justification::centred);

            ddc->startDragging(juce::var(rowIndex_), this, dragImage, true);
        }
    }
}

void HotkeyTab::BindingRow::itemDragEnter(const SourceDetails&)
{
    dragOver_ = true;
    repaint();
}

void HotkeyTab::BindingRow::itemDragExit(const SourceDetails&)
{
    dragOver_ = false;
    repaint();
}

void HotkeyTab::BindingRow::itemDropped(const SourceDetails& details)
{
    dragOver_ = false;
    repaint();

    int fromIndex = static_cast<int>(details.description);
    if (fromIndex != rowIndex_) {
        owner_.manager_.getHotkeyHandler().moveBinding(fromIndex, rowIndex_);
        owner_.manager_.saveConfig();
        owner_.refreshBindings();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  HotkeyTab implementation
// ═════════════════════════════════════════════════════════════════════════════

HotkeyTab::HotkeyTab(ControlManager& manager)
    : manager_(manager)
{
    headerLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(headerLabel_);

    addButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccentColour));
    addButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addButton_.onClick = [this] { onAddClicked(); };
    addAndMakeVisible(addButton_);

    statusLabel_.setFont(juce::Font(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(kWarningColour));
    addAndMakeVisible(statusLabel_);

    cancelButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    cancelButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    cancelButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cancelButton_.onClick = [this] { onCancelClicked(); };
    cancelButton_.setVisible(false);
    addAndMakeVisible(cancelButton_);

    viewport_.setViewedComponent(&rowContainer_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    refreshBindings();

    // Poll for recording completion at 10 Hz
    startTimerHz(10);
}

HotkeyTab::~HotkeyTab()
{
    stopTimer();

    // Cancel any in-progress recording (>= 0 = editing existing, -2 = adding new)
    if (recordingIndex_ != -1) {
        manager_.getHotkeyHandler().stopRecording();
    }
}

void HotkeyTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void HotkeyTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;

    int y = bounds.getY();

    // Header + Add button
    headerLabel_.setBounds(bounds.getX(), y, bounds.getWidth() - 130, rowH);
    addButton_.setBounds(bounds.getRight() - 120, y, 120, rowH);
    y += rowH + gap;

    // Status line + cancel button
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

    // Lay out rows inside the row container (each row is a full-width component)
    constexpr int innerRowH = 30;
    constexpr int innerGap  = 2;
    int totalH = static_cast<int>(rows_.size()) * (innerRowH + innerGap);
    int containerW = viewport_.getWidth() - viewport_.getScrollBarThickness();
    rowContainer_.setSize(containerW, totalH);

    int ry = 0;
    for (auto* row : rows_) {
        row->setBounds(0, ry, containerW, innerRowH);
        ry += innerRowH + innerGap;
    }
}

void HotkeyTab::refreshBindings()
{
    rows_.clear();

    auto& handler = manager_.getHotkeyHandler();
    const auto& bindings = handler.getBindings();

    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        const auto& binding = bindings[static_cast<size_t>(i)];
        auto* row = new BindingRow(*this, i);
        row->update(i,
                    actionToDisplayName(binding.action),
                    juce::String(binding.displayName),
                    (i % 2 == 1));
        rowContainer_.addAndMakeVisible(row);
        rows_.add(row);
    }

    resized();
}

void HotkeyTab::timerCallback()
{
    // Check if recording mode has ended (handler will clear isRecording)
    // recordingIndex_ >= 0 = editing existing, -2 = adding new
    if (recordingIndex_ != -1 && !manager_.getHotkeyHandler().isRecording()) {
        recordingIndex_ = -1;
        statusLabel_.setText("", juce::dontSendNotification);
        cancelButton_.setVisible(false);
        refreshBindings();
    }
}

void HotkeyTab::onSetClicked(int bindingIndex)
{
    if (recordingIndex_ >= 0) {
        // Already recording — cancel first
        manager_.getHotkeyHandler().stopRecording();
    }

    recordingIndex_ = bindingIndex;
    statusLabel_.setText("Press a key combination...", juce::dontSendNotification);
    cancelButton_.setVisible(true);
    resized();

    auto& handler = manager_.getHotkeyHandler();
    const auto& bindings = handler.getBindings();

    if (bindingIndex < 0 || bindingIndex >= static_cast<int>(bindings.size()))
        return;

    int targetId = bindings[static_cast<size_t>(bindingIndex)].id;

    handler.startRecording(
        [this, targetId](uint32_t mods, uint32_t vk, const std::string& name) {
            // Update binding in-place (preserves list position)
            auto& h = manager_.getHotkeyHandler();
            h.updateHotkey(targetId, mods, vk, name);
            manager_.saveConfig();

            // UI refresh will happen in timerCallback when isRecording() returns false
        });
}

void HotkeyTab::onRemoveClicked(int bindingIndex)
{
    auto& handler = manager_.getHotkeyHandler();
    const auto& bindings = handler.getBindings();

    if (bindingIndex >= 0 && bindingIndex < static_cast<int>(bindings.size())) {
        handler.unregisterHotkey(bindings[static_cast<size_t>(bindingIndex)].id);
        manager_.saveConfig();
        // Defer refresh to avoid deleting the row component from within its own callback
        auto safeThis = juce::Component::SafePointer<HotkeyTab>(this);
        juce::MessageManager::callAsync([safeThis] { if (safeThis) safeThis->refreshBindings(); });
    }
}

juce::PopupMenu HotkeyTab::buildActionMenu()
{
    juce::PopupMenu menu;

    // Plugin bypass (1-16, freely selectable)
    juce::PopupMenu bypassMenu;
    for (int i = 1; i <= 16; ++i) {
        bypassMenu.addItem(100 + i, "Plugin " + juce::String(i) + " Bypass");
    }
    menu.addSubMenu("Plugin Bypass", bypassMenu);

    // Master bypass
    menu.addItem(200, "Master Bypass");

    // Mute / Panic
    menu.addItem(201, "Panic Mute");
    menu.addItem(202, "Input Mute Toggle");
    menu.addItem(203, "Output Mute Toggle");
    menu.addItem(204, "Monitor Toggle");
    menu.addItem(205, "IPC Toggle");
    menu.addItem(206, "Recording Toggle");

    // Input gain
    menu.addItem(300, "Input Gain +1 dB");
    menu.addItem(301, "Input Gain -1 dB");

    // Preset slots (A..E)
    juce::PopupMenu slotMenu;
    for (int i = 0; i < 5; ++i) {
        char label = 'A' + static_cast<char>(i);
        slotMenu.addItem(600 + i, "Preset Slot " + juce::String::charToString(label));
    }
    menu.addSubMenu("Preset Slot", slotMenu);

    menu.addItem(500, "Next Preset");
    menu.addItem(501, "Previous Preset");

    return menu;
}

void HotkeyTab::onAddClicked()
{
    auto menu = buildActionMenu();

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
        [this](int result) {
            if (result == 0) return;  // cancelled

            ActionEvent action;

            if (result >= 100 && result < 200) {
                int pluginIdx = result - 100 - 1;
                action = {Action::PluginBypass, pluginIdx, 0.0f,
                          "Plugin " + std::to_string(pluginIdx + 1) + " Bypass"};
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
                int slotIdx = result - 600;
                char label = 'A' + static_cast<char>(slotIdx);
                action = {Action::SwitchPresetSlot, slotIdx, 0.0f,
                          "Preset Slot " + std::string(1, label)};
            } else {
                return;
            }

            // Enter recording mode to capture key combination
            statusLabel_.setText("Press a key combination for: " +
                                 juce::String(action.stringParam),
                                 juce::dontSendNotification);
            cancelButton_.setVisible(true);
            resized();
            recordingIndex_ = -2;  // special value for "new binding"

            auto safeHotkey = juce::Component::SafePointer<HotkeyTab>(this);
            manager_.getHotkeyHandler().startRecording(
                [safeHotkey, action](uint32_t mods, uint32_t vk, const std::string& name) {
                    if (!safeHotkey) return;
                    safeHotkey->manager_.getHotkeyHandler().registerHotkey(mods, vk, action, name);
                    safeHotkey->manager_.saveConfig();
                    // UI refresh happens in timerCallback
                });
        });
}

void HotkeyTab::onCancelClicked()
{
    if (recordingIndex_ != -1) {
        manager_.getHotkeyHandler().stopRecording();
        recordingIndex_ = -1;
        statusLabel_.setText("", juce::dontSendNotification);
        cancelButton_.setVisible(false);
        resized();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  MidiTab implementation
// ═════════════════════════════════════════════════════════════════════════════

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
    if (learningIndex_ >= 0) {
        manager_.getMidiHandler().stopLearn();
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

    auto& handler = manager_.getMidiHandler();
    const auto& bindings = handler.getBindings();

    if (mappingIndex < 0 || mappingIndex >= static_cast<int>(bindings.size()))
        return;

    // Capture the action from the existing mapping
    ActionEvent targetAction = bindings[static_cast<size_t>(mappingIndex)].action;

    handler.startLearn(
        [this, mappingIndex, targetAction](int cc, int note, int channel,
                                            const juce::String& deviceName) {
            auto& h = manager_.getMidiHandler();

            // Remove old binding
            if (mappingIndex < static_cast<int>(h.getBindings().size())) {
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

            manager_.saveConfig();
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

// ═════════════════════════════════════════════════════════════════════════════
//  StreamDeckTab implementation
// ═════════════════════════════════════════════════════════════════════════════

StreamDeckTab::StreamDeckTab(ControlManager& manager)
    : manager_(manager)
{
    // ── WebSocket section ──
    wsSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    wsSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsSectionLabel_);

    wsPortLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsPortLabel_);
    wsPortValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    wsPortValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsPortValueLabel_);

    wsStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsStatusLabel_);
    wsStatusValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsStatusValueLabel_);

    wsClientsLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsClientsLabel_);
    wsClientsValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    wsClientsValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsClientsValueLabel_);

    wsToggleButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    wsToggleButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    wsToggleButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    wsToggleButton_.onClick = [this] {
        auto& ws = manager_.getWebSocketServer();
        if (ws.isRunning()) {
            ws.stop();
        } else {
            ws.start(ws.getPort());
        }
        updateStatus();
    };
    addAndMakeVisible(wsToggleButton_);

    // ── HTTP section ──
    httpSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    httpSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpSectionLabel_);

    httpPortLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpPortLabel_);
    httpPortValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    httpPortValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(httpPortValueLabel_);

    httpStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpStatusLabel_);
    httpStatusValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(httpStatusValueLabel_);

    httpToggleButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    httpToggleButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    httpToggleButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    httpToggleButton_.onClick = [this] {
        auto& http = manager_.getHttpApiServer();
        if (http.isRunning()) {
            http.stop();
        } else {
            http.start(http.getPort());
        }
        updateStatus();
    };
    addAndMakeVisible(httpToggleButton_);

    // ── Info text ──
    infoLabel_.setFont(juce::Font(11.0f));
    infoLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    infoLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(infoLabel_);

    updateStatus();

    // Refresh status at 2 Hz
    startTimerHz(2);
}

StreamDeckTab::~StreamDeckTab()
{
    stopTimer();
}

void StreamDeckTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    // Separator between WebSocket and HTTP sections
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;

    // After WS section: header + port + status + clients + button = 5 rows
    int separatorY = bounds.getY() + (rowH + gap) * 5 - gap / 2;
    g.setColour(juce::Colour(kDimTextColour).withAlpha(0.3f));
    g.drawHorizontalLine(separatorY, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
}

void StreamDeckTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH   = 28;
    constexpr int gap    = 6;
    constexpr int labelW = 80;
    constexpr int valueW = 100;
    constexpr int btnW   = 70;

    int y = bounds.getY();

    // ── WebSocket section ──
    wsSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    wsPortLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsPortValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsStatusLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsStatusValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsClientsLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsClientsValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsToggleButton_.setBounds(bounds.getX(), y, btnW, rowH);
    y += rowH + gap + 8; // extra gap for separator

    // ── HTTP section ──
    httpSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    httpPortLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    httpPortValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    httpStatusLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    httpStatusValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    httpToggleButton_.setBounds(bounds.getX(), y, btnW, rowH);
    y += rowH + gap + 8;

    // Info text fills remaining space
    infoLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), bounds.getBottom() - y);
}

void StreamDeckTab::timerCallback()
{
    updateStatus();
}

void StreamDeckTab::updateStatus()
{
    auto& ws = manager_.getWebSocketServer();
    auto& http = manager_.getHttpApiServer();

    // WebSocket
    wsPortValueLabel_.setText(juce::String(ws.getPort()), juce::dontSendNotification);

    if (ws.isRunning()) {
        wsStatusValueLabel_.setText("Running", juce::dontSendNotification);
        wsStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreenColour));
        wsToggleButton_.setButtonText("Stop");
    } else {
        wsStatusValueLabel_.setText("Stopped", juce::dontSendNotification);
        wsStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        wsToggleButton_.setButtonText("Start");
    }

    wsClientsValueLabel_.setText(juce::String(ws.getClientCount()), juce::dontSendNotification);

    // HTTP
    httpPortValueLabel_.setText(juce::String(http.getPort()), juce::dontSendNotification);

    if (http.isRunning()) {
        httpStatusValueLabel_.setText("Running", juce::dontSendNotification);
        httpStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreenColour));
        httpToggleButton_.setButtonText("Stop");
    } else {
        httpStatusValueLabel_.setText("Stopped", juce::dontSendNotification);
        httpStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        httpToggleButton_.setButtonText("Start");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  ControlSettingsPanel implementation (top-level tabbed container)
// ═════════════════════════════════════════════════════════════════════════════

ControlSettingsPanel::ControlSettingsPanel(ControlManager& manager, VSTChain* vstChain)
    : manager_(manager)
{
    // Create tab content components
    hotkeyTab_     = std::make_unique<HotkeyTab>(manager_);
    midiTab_       = std::make_unique<MidiTab>(manager_, vstChain);
    streamDeckTab_ = std::make_unique<StreamDeckTab>(manager_);

    // Configure the tabbed component
    tabbedComponent_.setTabBarDepth(30);
    tabbedComponent_.setOutline(0);

    // Add tabs — the tabbed component takes ownership of the colour but
    // we keep ownership of the components via unique_ptr.
    tabbedComponent_.addTab("Hotkeys",     juce::Colour(kTabBarColour), hotkeyTab_.get(),     false);
    tabbedComponent_.addTab("MIDI",        juce::Colour(kTabBarColour), midiTab_.get(),       false);
    tabbedComponent_.addTab("Stream Deck", juce::Colour(kTabBarColour), streamDeckTab_.get(), false);

    // Style the tab bar
    auto& tabBar = tabbedComponent_.getTabbedButtonBar();
    tabBar.setColour(juce::TabbedButtonBar::tabOutlineColourId,
                     juce::Colours::transparentBlack);
    tabBar.setColour(juce::TabbedButtonBar::frontOutlineColourId,
                     juce::Colour(kAccentColour));

    addAndMakeVisible(tabbedComponent_);
}

ControlSettingsPanel::~ControlSettingsPanel() = default;

void ControlSettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void ControlSettingsPanel::resized()
{
    tabbedComponent_.setBounds(getLocalBounds());
}

void ControlSettingsPanel::refreshAll()
{
    hotkeyTab_->refreshBindings();
    midiTab_->refreshAll();
    // StreamDeckTab refreshes automatically via timer
}

} // namespace directpipe
