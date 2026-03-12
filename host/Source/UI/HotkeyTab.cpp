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
 * @file HotkeyTab.cpp
 * @brief Hotkey bindings tab implementation
 */

#include "HotkeyTab.h"

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

// =============================================================================
//  HotkeyTab::BindingRow — per-row drag source + drop target
// =============================================================================

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

// =============================================================================
//  HotkeyTab implementation
// =============================================================================

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

} // namespace directpipe
