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
 * @file LevelMeter.cpp
 * @brief Audio level meter implementation
 */

#include "LevelMeter.h"
#include <cmath>

namespace directpipe {

LevelMeter::LevelMeter(const juce::String& label)
    : label_(label)
{
    startTimerHz(30);
}

LevelMeter::~LevelMeter()
{
    stopTimer();
}

// Convert linear RMS level to logarithmic display scale.
// Maps -60 dB..0 dB to 0.0..1.0 so normal speech (~-30 to -10 dB)
// fills 50-83% of the meter height instead of a tiny sliver.
static float linearToLogDisplay(float linear)
{
    if (linear < 0.001f) return 0.0f;           // below -60 dB
    float db = 20.0f * std::log10(linear);       // 0..1 → -inf..0 dB
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);  // -60..0 → 0..1
}

void LevelMeter::setLevel(float level)
{
    float display = linearToLogDisplay(juce::jlimit(0.0f, 1.0f, level));
    targetLevel_.store(display, std::memory_order_relaxed);
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xFF1A1A2E));
    g.fillRoundedRectangle(bounds, 2.0f);

    // Meter fill
    float level = displayLevel_;
    if (level > 0.001f) {
        juce::Rectangle<float> fillBounds;

        if (vertical_) {
            float fillHeight = bounds.getHeight() * level;
            fillBounds = bounds.removeFromBottom(fillHeight);
        } else {
            float fillWidth = bounds.getWidth() * level;
            fillBounds = juce::Rectangle<float>(
                bounds.getX(), bounds.getY(), fillWidth, bounds.getHeight());
        }

        // Color gradient: green → yellow → red
        juce::Colour color;
        if (level < 0.6f) {
            color = juce::Colour(0xFF4CAF50);  // Green
        } else if (level < 0.85f) {
            float t = (level - 0.6f) / 0.25f;
            color = juce::Colour(0xFF4CAF50).interpolatedWith(
                juce::Colour(0xFFFFEB3B), t);  // Green → Yellow
        } else {
            float t = (level - 0.85f) / 0.15f;
            color = juce::Colour(0xFFFFEB3B).interpolatedWith(
                juce::Colour(0xFFF44336), t);  // Yellow → Red
        }

        g.setColour(color);
        g.fillRoundedRectangle(fillBounds, 2.0f);
    }

    // Peak hold indicator
    if (peakLevel_ > 0.01f) {
        float peakPos;
        g.setColour(juce::Colours::white.withAlpha(0.8f));

        auto fullBounds = getLocalBounds().toFloat();
        if (vertical_) {
            peakPos = fullBounds.getBottom() - fullBounds.getHeight() * peakLevel_;
            g.drawHorizontalLine(static_cast<int>(peakPos),
                                 fullBounds.getX(), fullBounds.getRight());
        } else {
            peakPos = fullBounds.getX() + fullBounds.getWidth() * peakLevel_;
            g.drawVerticalLine(static_cast<int>(peakPos),
                               fullBounds.getY(), fullBounds.getBottom());
        }
    }

    // Clipping indicator
    if (clipping_) {
        g.setColour(juce::Colour(0xFFF44336));
        if (vertical_) {
            g.fillRect(getLocalBounds().removeFromTop(3));
        } else {
            g.fillRect(getLocalBounds().removeFromRight(3));
        }
    }

    // Label
    if (label_.isNotEmpty()) {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font(10.0f));
        g.drawText(label_, getLocalBounds(), juce::Justification::centred);
    }
}

void LevelMeter::resized()
{
}

void LevelMeter::timerCallback()
{
    float target = targetLevel_.load(std::memory_order_relaxed);

    // Smooth level display
    if (target > displayLevel_) {
        displayLevel_ += (target - displayLevel_) * kAttack;
    } else {
        displayLevel_ += (target - displayLevel_) * kRelease;
    }

    // Clamp very small values to zero
    if (displayLevel_ < 0.001f) displayLevel_ = 0.0f;

    // Peak hold
    if (displayLevel_ > peakLevel_) {
        peakLevel_ = displayLevel_;
        peakHoldCounter_ = kPeakHoldFrames;
    } else if (peakHoldCounter_ > 0) {
        --peakHoldCounter_;
    } else {
        peakLevel_ *= 0.95f;  // Slowly decay peak
    }

    // Clipping detection
    clipping_ = target >= 0.99f;

    repaint();
}

} // namespace directpipe
