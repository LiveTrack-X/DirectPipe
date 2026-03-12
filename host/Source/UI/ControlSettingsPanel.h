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
 * @file ControlSettingsPanel.h
 * @brief Tabbed container for Hotkeys, MIDI, and Stream Deck configuration tabs.
 */
#pragma once

#include <JuceHeader.h>
#include "HotkeyTab.h"
#include "MidiTab.h"
#include "StreamDeckTab.h"

namespace directpipe {

/**
 * @brief Tabbed settings panel combining Hotkey, MIDI, and Stream Deck tabs.
 *
 * Uses juce::TabbedComponent to switch between the three configuration
 * sub-panels. All tabs share a reference to the same ControlManager.
 */
class ControlSettingsPanel : public juce::Component {
public:
    /**
     * @brief Construct the control settings panel.
     * @param manager Reference to the control manager that owns all handlers.
     */
    explicit ControlSettingsPanel(ControlManager& manager, VSTChain* vstChain = nullptr);
    ~ControlSettingsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Refresh all tabs to reflect the current control configuration.
     */
    void refreshAll();

private:
    ControlManager& manager_;

    // Tabbed component (owns the tab bar and content area)
    juce::TabbedComponent tabbedComponent_{juce::TabbedButtonBar::TabsAtTop};

    // Tab content components (owned separately, added to tabbed component)
    std::unique_ptr<HotkeyTab> hotkeyTab_;
    std::unique_ptr<MidiTab> midiTab_;
    std::unique_ptr<StreamDeckTab> streamDeckTab_;

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kTabBarColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlSettingsPanel)
};

} // namespace directpipe
