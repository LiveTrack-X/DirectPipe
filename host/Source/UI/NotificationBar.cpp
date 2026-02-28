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
 * @file NotificationBar.cpp
 * @brief Non-intrusive status bar notification implementation
 */

#include "NotificationBar.h"

namespace directpipe {

NotificationBar::NotificationBar()
{
    setInterceptsMouseClicks(false, false);
}

void NotificationBar::showNotification(const juce::String& message,
                                       NotificationLevel level,
                                       int durationTicks)
{
    message_ = message;
    levelColour_ = colourForLevel(level);
    countdownTicks_ = durationTicks;
    setVisible(true);
    repaint();
}

void NotificationBar::tick()
{
    if (countdownTicks_ <= 0) return;

    --countdownTicks_;

    // Repaint during fade-out phase (last 30 ticks = ~1 second)
    if (countdownTicks_ < 30)
        repaint();

    if (countdownTicks_ <= 0)
        setVisible(false);
}

void NotificationBar::paint(juce::Graphics& g)
{
    if (countdownTicks_ <= 0) return;

    // Background slightly darker than status bar
    g.fillAll(juce::Colour(0xFF12122A));

    // Colored left-edge indicator strip (4px)
    g.setColour(levelColour_);
    g.fillRect(0, 0, 4, getHeight());

    // Fade out in last ~1 second (30 ticks at 30Hz)
    float alpha = 1.0f;
    if (countdownTicks_ < 30)
        alpha = static_cast<float>(countdownTicks_) / 30.0f;

    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.setFont(juce::Font(11.5f));
    g.drawText(message_, getLocalBounds().withTrimmedLeft(8),
               juce::Justification::centredLeft, true);
}

juce::Colour NotificationBar::colourForLevel(NotificationLevel level)
{
    switch (level) {
        case NotificationLevel::Critical: return juce::Colour(0xFFE05050);
        case NotificationLevel::Error:    return juce::Colour(0xFFE05050);
        case NotificationLevel::Warning:  return juce::Colour(0xFFFFAA33);
        case NotificationLevel::Info:     return juce::Colour(0xFF6C63FF);
    }
    return juce::Colour(0xFFE05050);
}

} // namespace directpipe
