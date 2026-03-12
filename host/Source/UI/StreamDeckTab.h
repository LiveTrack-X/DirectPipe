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
 * @file StreamDeckTab.h
 * @brief WebSocket/HTTP server status display for Stream Deck integration.
 */
#pragma once

#include <JuceHeader.h>
#include "../Control/ControlManager.h"

namespace directpipe {

/**
 * @brief Tab content showing Stream Deck server status.
 *
 * Displays:
 * - WebSocket server: port, running/stopped, connected client count
 * - HTTP API server: port, running/stopped
 * - Start/Stop toggle buttons for each server
 */
class StreamDeckTab : public juce::Component,
                      public juce::Timer {
public:
    /**
     * @brief Construct the Stream Deck tab.
     * @param manager Reference to the control manager.
     */
    explicit StreamDeckTab(ControlManager& manager);
    ~StreamDeckTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    /** @brief Refresh all status labels from server state. */
    void updateStatus();

    ControlManager& manager_;

    // WebSocket section
    juce::Label wsSectionLabel_{"", "WebSocket Server"};
    juce::Label wsPortLabel_{"", "Port:"};
    juce::Label wsPortValueLabel_{"", "8765"};
    juce::Label wsStatusLabel_{"", "Status:"};
    juce::Label wsStatusValueLabel_{"", "Stopped"};
    juce::Label wsClientsLabel_{"", "Clients:"};
    juce::Label wsClientsValueLabel_{"", "0"};
    juce::TextButton wsToggleButton_{"Start"};

    // HTTP section
    juce::Label httpSectionLabel_{"", "HTTP API Server"};
    juce::Label httpPortLabel_{"", "Port:"};
    juce::Label httpPortValueLabel_{"", "8766"};
    juce::Label httpStatusLabel_{"", "Status:"};
    juce::Label httpStatusValueLabel_{"", "Stopped"};
    juce::TextButton httpToggleButton_{"Start"};

    // Info text
    juce::Label infoLabel_{"", "Stream Deck plugins connect via WebSocket.\n"
                                "HTTP API is available for custom integrations."};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kGreenColour     = 0xFF4CAF50;
    static constexpr juce::uint32 kRedColour       = 0xFFE05050;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamDeckTab)
};

} // namespace directpipe
