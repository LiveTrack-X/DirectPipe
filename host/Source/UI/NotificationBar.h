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
 * @file NotificationBar.h
 * @brief Non-intrusive status bar notification component
 *
 * Displays error/warning/info messages in the status bar area,
 * replacing latency/CPU labels temporarily. Auto-fades after
 * a configurable duration. No modal dialogs.
 */
#pragma once

#include <JuceHeader.h>

namespace directpipe {

enum class NotificationLevel { Info, Warning, Error, Critical };

class NotificationBar : public juce::Component {
public:
    NotificationBar();

    void showNotification(const juce::String& message,
                          NotificationLevel level,
                          int durationTicks);

    /** Call from parent's timerCallback (30Hz). Returns true while active. */
    void tick();

    bool isActive() const { return countdownTicks_ > 0; }

    void paint(juce::Graphics& g) override;

private:
    static juce::Colour colourForLevel(NotificationLevel level);

    juce::String message_;
    juce::Colour levelColour_;
    int countdownTicks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotificationBar)
};

} // namespace directpipe
