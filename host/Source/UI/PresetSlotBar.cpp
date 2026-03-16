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
 * @file PresetSlotBar.cpp
 * @brief Quick Preset Slot buttons (A..E) implementation
 */

#include "PresetSlotBar.h"
#include "../Audio/AudioEngine.h"
#include "PluginChainEditor.h"

namespace directpipe {

PresetSlotBar::PresetSlotBar(PresetManager& presetManager, AudioEngine& engine,
                             PluginChainEditor& chainEditor,
                             std::atomic<bool>& loadingSlot,
                             std::atomic<bool>& partialLoad)
    : presetManager_(presetManager),
      audioEngine_(engine),
      chainEditor_(chainEditor),
      loadingSlot_(loadingSlot),
      partialLoad_(partialLoad)
{
    for (int i = 0; i < kNumPresetSlots; ++i) {
        juce::String label = presetManager_.getSlotDisplayName(i);

        slotButtons_[static_cast<size_t>(i)] = std::make_unique<juce::TextButton>(label);
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        btn->setClickingTogglesState(false);

        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
        btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7B6FFF));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btn->onClick = [this, i] { onSlotClicked(i); };
        addAndMakeVisible(*btn);
    }

    // Right-click on slot buttons → context menu
    for (int i = 0; i < kNumPresetSlots; ++i) {
        slotButtons_[static_cast<size_t>(i)]->setMouseClickGrabsKeyboardFocus(false);
        slotButtons_[static_cast<size_t>(i)]->addMouseListener(this, false);
    }
}

PresetSlotBar::~PresetSlotBar()
{
    for (int i = 0; i < kNumPresetSlots; ++i)
        slotButtons_[static_cast<size_t>(i)]->removeMouseListener(this);
}

// ─── Layout ──────────────────────────────────────────────────────────────────

void PresetSlotBar::resized()
{
    auto bounds = getLocalBounds();
    int w = bounds.getWidth();
    int slotBtnW = (w - kSlotBtnGap * (kNumPresetSlots - 1)) / kNumPresetSlots;
    for (int i = 0; i < kNumPresetSlots; ++i) {
        slotButtons_[static_cast<size_t>(i)]->setBounds(
            i * (slotBtnW + kSlotBtnGap), 0, slotBtnW, bounds.getHeight());
    }
}

// ─── Preset Action Handling ──────────────────────────────────────────────────

void PresetSlotBar::handlePresetAction(const ActionEvent& event)
{
    switch (event.action) {
        case Action::LoadPreset:
        case Action::SwitchPresetSlot: {
            int slot = event.intParam;
            if (slot >= 0 && slot < kNumPresetSlots) {
                if (loadingSlot_) {
                    pendingSlot_ = slot;
                } else {
                    onSlotClicked(slot);
                }
            }
            break;
        }

        case Action::NextPreset:
        case Action::PreviousPreset: {
            int currentSlot = presetManager_.getActiveSlot();
            int direction = (event.action == Action::NextPreset) ? 1 : -1;
            int start = (currentSlot < 0 || currentSlot >= kNumPresetSlots) ? 0 : currentSlot;
            int nextSlot = (start + direction + kNumPresetSlots) % kNumPresetSlots;
            if (loadingSlot_) {
                pendingSlot_ = nextSlot;
            } else {
                onSlotClicked(nextSlot);
            }
            break;
        }

        default:
            break;
    }
}

// ─── Right-click Context Menu ────────────────────────────────────────────────

void PresetSlotBar::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu()) return;
    if (loadingSlot_) return;

    auto* src = event.eventComponent;
    int sourceSlot = -1;
    for (int i = 0; i < kNumPresetSlots; ++i) {
        if (src == slotButtons_[static_cast<size_t>(i)].get()) {
            sourceSlot = i;
            break;
        }
    }
    if (sourceSlot < 0) return;

    bool occupied = presetManager_.isSlotOccupied(sourceSlot);
    auto slotChar = juce::String::charToString(PresetManager::slotLabel(sourceSlot));

    juce::PopupMenu menu;

    // Rename (only for occupied slots)
    if (occupied)
        menu.addItem(200, "Rename " + slotChar + "...");

    // Copy submenu (occupied only)
    if (occupied) {
        juce::PopupMenu copyMenu;
        for (int i = 0; i < kNumPresetSlots; ++i) {
            if (i == sourceSlot) continue;
            copyMenu.addItem(i + 1, "-> " + juce::String::charToString(PresetManager::slotLabel(i)));
        }
        menu.addSubMenu("Copy " + slotChar, copyMenu);
    }

    // Export/Import
    if (occupied)
        menu.addItem(300, "Export " + slotChar + "...");
    menu.addItem(301, "Import to " + slotChar + "...");

    // Delete (occupied only)
    if (occupied) {
        menu.addSeparator();
        menu.addItem(100, "Delete " + slotChar);
    }

    auto safeThis = juce::Component::SafePointer<PresetSlotBar>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(),
        [safeThis, sourceSlot, slotChar](int result) {
            if (!safeThis || result == 0) return;

            if (result == 200) {
                // Rename
                auto currentName = safeThis->presetManager_.getSlotName(sourceSlot);
                auto* aw = new juce::AlertWindow("Rename Slot " + slotChar,
                    "Enter a name for slot " + slotChar + ":",
                    juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor("name", currentName, "Name:");
                aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                aw->addButton("Clear", 2);
                aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                auto safeThis2 = safeThis;
                aw->enterModalState(true, juce::ModalCallbackFunction::create(
                    [safeThis2, sourceSlot, aw](int btnResult) {
                        if (!safeThis2) { delete aw; return; }
                        if (btnResult == 1) {
                            auto newName = aw->getTextEditorContents("name");
                            safeThis2->presetManager_.setSlotName(sourceSlot, newName);
                        } else if (btnResult == 2) {
                            safeThis2->presetManager_.setSlotName(sourceSlot, "");
                        }
                        delete aw;
                        if (btnResult > 0) {
                            safeThis2->updateSlotButtonStates();
                            if (safeThis2->onSettingsDirty) safeThis2->onSettingsDirty();
                        }
                    }), false);
                return;
            }

            if (result == 300) {
                // Export
                safeThis->presetManager_.exportSlot(sourceSlot);
                return;
            }

            if (result == 301) {
                // Import
                auto safeThis2 = safeThis;
                safeThis->presetManager_.importSlot(sourceSlot, [safeThis2](bool ok) {
                    if (!safeThis2) return;
                    safeThis2->updateSlotButtonStates();
                    if (safeThis2->onRefreshUI) safeThis2->onRefreshUI();
                    if (ok && safeThis2->onSettingsDirty) safeThis2->onSettingsDirty();
                });
                return;
            }

            if (result == 100) {
                // Delete
                bool wasActive = (safeThis->presetManager_.getActiveSlot() == sourceSlot);
                if (safeThis->presetManager_.deleteSlot(sourceSlot)) {
                    if (wasActive) {
                        safeThis->loadingSlot_ = true;
                        auto& chain = safeThis->audioEngine_.getVSTChain();
                        while (chain.getPluginCount() > 0)
                            chain.removePlugin(0);
                        safeThis->loadingSlot_ = false;
                        // Keep deleted slot selected (now empty)
                        safeThis->presetManager_.setActiveSlot(sourceSlot);
                        safeThis->chainEditor_.refreshList();
                    }
                    safeThis->updateSlotButtonStates();
                    if (safeThis->onNotification)
                        safeThis->onNotification("Deleted slot " + slotChar,
                                                  NotificationLevel::Info);
                    if (safeThis->onSettingsDirty) safeThis->onSettingsDirty();
                }
                return;
            }

            // Copy (result 1-4 = target slot index)
            int targetSlot = result - 1;
            int active = safeThis->presetManager_.getActiveSlot();
            if (active == sourceSlot && !safeThis->partialLoad_.load())
                safeThis->presetManager_.saveSlot(sourceSlot);

            if (safeThis->presetManager_.copySlot(sourceSlot, targetSlot)) {
                safeThis->updateSlotButtonStates();
                if (safeThis->onNotification)
                    safeThis->onNotification(
                        "Copied slot " + slotChar
                        + " to " + juce::String::charToString(PresetManager::slotLabel(targetSlot)),
                        NotificationLevel::Info);

                // If copied TO the active slot, reload chain to reflect new content
                if (safeThis->presetManager_.getActiveSlot() == targetSlot) {
                    safeThis->loadingSlot_ = true;
                    safeThis->chainEditor_.showLoadingState();
                    safeThis->presetManager_.loadSlotAsync(targetSlot,
                        [safeThis](bool ok) {
                            if (!safeThis) return;
                            safeThis->loadingSlot_ = false;
                            safeThis->partialLoad_ = !ok;
                            safeThis->chainEditor_.hideLoadingState();
                            if (safeThis->onRefreshUI) safeThis->onRefreshUI();
                            safeThis->updateSlotButtonStates();
                            if (ok && safeThis->onSettingsDirty) safeThis->onSettingsDirty();
                        });
                }
            }
        });
}

// ─── Auto Slot Support ───────────────────────────────────────────────────────

void PresetSlotBar::deselectAllSlots()
{
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        btn->setToggleState(false, juce::dontSendNotification);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
        btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF2A2A40));
    }
}

void PresetSlotBar::setActiveSlot(int slotIndex)
{
    if (slotIndex == kAutoSlotIndex || slotIndex < 0 || slotIndex >= kNumPresetSlots) {
        // Auto slot or invalid → deselect all A-E buttons
        deselectAllSlots();
    } else {
        // Normal A-E slot → highlight the active one
        for (int i = 0; i < kNumPresetSlots; ++i) {
            auto* btn = slotButtons_[static_cast<size_t>(i)].get();
            bool isTarget = (i == slotIndex);
            btn->setToggleState(isTarget, juce::dontSendNotification);
            btn->setColour(juce::TextButton::buttonColourId,
                           juce::Colour(isTarget ? 0xFF7B6FFF : 0xFF2A2A40));
            btn->setColour(juce::TextButton::buttonOnColourId,
                           juce::Colour(isTarget ? 0xFF7B6FFF : 0xFF2A2A40));
        }
    }
}

// ─── Slot Click ──────────────────────────────────────────────────────────────

void PresetSlotBar::onSlotClicked(int slotIndex)
{
    if (loadingSlot_) return;

    // Same slot click = just save current state
    if (presetManager_.getActiveSlot() == slotIndex) {
        if (!partialLoad_.load())
            presetManager_.saveSlot(slotIndex);
        updateSlotButtonStates();
        return;
    }

    // Save current slot first (captures plugin internal state)
    // Skip save if previous load was partial (preserve original slot file)
    int currentSlot = presetManager_.getActiveSlot();
    if (currentSlot >= 0 && !partialLoad_.load())
        presetManager_.saveSlot(currentSlot);
    partialLoad_ = false;

    // Slot has data -> async load
    if (presetManager_.isSlotOccupied(slotIndex)) {
        loadingSlot_ = true;
        setSlotButtonsEnabled(false);
        chainEditor_.showLoadingState();
        presetManager_.loadSlotAsync(slotIndex, [safeThis = juce::Component::SafePointer<PresetSlotBar>(this)](bool ok) {
            if (!safeThis) return;
            safeThis->loadingSlot_ = false;
            safeThis->partialLoad_ = !ok;
            safeThis->setSlotButtonsEnabled(true);
            safeThis->chainEditor_.hideLoadingState();
            if (safeThis->onRefreshUI) safeThis->onRefreshUI();
            safeThis->updateSlotButtonStates();
            if (ok && safeThis->onSettingsDirty) safeThis->onSettingsDirty();

            // Process queued slot request (from rapid switching)
            if (safeThis->pendingSlot_ >= 0) {
                int pending = safeThis->pendingSlot_;
                safeThis->pendingSlot_ = -1;
                safeThis->onSlotClicked(pending);
            }
        });
    } else {
        // Empty slot → clear chain, select the empty slot
        loadingSlot_ = true;
        auto& chain = audioEngine_.getVSTChain();
        while (chain.getPluginCount() > 0)
            chain.removePlugin(chain.getPluginCount() - 1);
        presetManager_.setActiveSlot(slotIndex);
        loadingSlot_ = false;
        if (onSettingsDirty) onSettingsDirty();
    }

    // Immediately show target slot as selected
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        bool isTarget = (i == slotIndex);
        btn->setToggleState(isTarget, juce::dontSendNotification);

        btn->setColour(juce::TextButton::buttonColourId,
                       juce::Colour(isTarget ? 0xFF7B6FFF : 0xFF2A2A40));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       juce::Colour(isTarget ? 0xFF7B6FFF : 0xFF2A2A40));
    }
}

// ─── Slot Button State ───────────────────────────────────────────────────────

void PresetSlotBar::updateSlotButtonStates()
{
    int active = presetManager_.getActiveSlot();
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        bool isActive = (i == active);

        btn->setButtonText(presetManager_.getSlotDisplayName(i));
        btn->setToggleState(isActive, juce::dontSendNotification);

        if (isActive) {
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF7B6FFF));
            btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7B6FFF));
        } else {
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
            btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF2A2A40));
        }
        btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->repaint();
    }
}

void PresetSlotBar::setSlotButtonsEnabled(bool enabled)
{
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        btn->setEnabled(enabled);
        btn->setAlpha(enabled ? 1.0f : 0.5f);
    }
}

} // namespace directpipe
