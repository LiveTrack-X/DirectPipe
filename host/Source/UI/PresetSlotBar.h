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
 * @file PresetSlotBar.h
 * @brief Quick Preset Slot buttons (A..E) — slot switching, right-click menu,
 *        and preset action handling extracted from MainComponent.
 */
#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"
#include "NotificationBar.h"
#include "../Control/ActionDispatcher.h"
#include <array>
#include <functional>

namespace directpipe {

class AudioEngine;
class PluginChainEditor;

class PresetSlotBar : public juce::Component {
public:
    PresetSlotBar(PresetManager& presetManager, AudioEngine& engine,
                  PluginChainEditor& chainEditor,
                  std::atomic<bool>& loadingSlot,
                  std::atomic<bool>& partialLoad);
    ~PresetSlotBar() override;

    void updateSlotButtonStates();
    void setSlotButtonsEnabled(bool enabled);
    void onSlotClicked(int slotIndex);

    /** Handle LoadPreset, SwitchPresetSlot, NextPreset, PreviousPreset actions. */
    void handlePresetAction(const ActionEvent& event);

    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // Callbacks to owner
    std::function<void()> onSettingsDirty;
    std::function<void()> onRefreshUI;
    std::function<void(const juce::String&, NotificationLevel)> onNotification;

    static constexpr int kNumPresetSlots = 5;

    /** Auto preset slot index (not a visible button — used by MainComponent's Auto button). */
    static constexpr int kAutoSlotIndex = 5;

    /** Deselect all A-E buttons (used when Auto slot is active). */
    void deselectAllSlots();

    /** Update visual state of the active slot, including handling Auto slot (index 5). */
    void setActiveSlot(int slotIndex);

    /** Reset the pending slot index (call before bulk operations like Clear All Presets / Factory Reset). */
    void resetPendingSlot() { pendingSlot_ = -1; }

private:
    static constexpr int kSlotBtnGap = 4;

    PresetManager& presetManager_;
    AudioEngine& audioEngine_;
    PluginChainEditor& chainEditor_;
    std::atomic<bool>& loadingSlot_;
    std::atomic<bool>& partialLoad_;

    std::array<std::unique_ptr<juce::TextButton>, kNumPresetSlots> slotButtons_;
    int pendingSlot_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetSlotBar)
};

} // namespace directpipe
