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
 * @brief Tabbed control settings panel — container for Hotkey/MIDI/StreamDeck tabs
 */

#include "ControlSettingsPanel.h"

namespace directpipe {

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
