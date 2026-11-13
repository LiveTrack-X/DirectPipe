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
 * @file LevelMeter.h
 * @brief Real-time audio level meter UI component
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace directpipe {

/**
 * @brief Visual audio level meter with peak hold.
 *
 * Displays a vertical or horizontal bar meter with:
 * - Current RMS level (green → yellow → red)
 * - Peak hold indicator
 * - Clipping indicator
 *
 * No internal timer — call tick() from the parent's timer callback.
 */
class LevelMeter : public juce::Component {
public:
    explicit LevelMeter(const juce::String& label = "");
    ~LevelMeter() override = default;

    /**
     * @brief Set the current level (0.0 - 1.0). Thread-safe.
     */
    void setLevel(float level);

    /**
     * @brief Advance smoothing and repaint if display changed.
     * Call from parent's timerCallback (e.g., 30Hz).
     */
    void tick();

    /**
     * @brief Set orientation.
     * @param vertical true for vertical meter, false for horizontal.
     */
    void setVertical(bool vertical) { vertical_ = vertical; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String label_;
    bool vertical_ = true;

    // Current level (thread-safe)
    std::atomic<float> targetLevel_{0.0f};
    float displayLevel_ = 0.0f;
    float peakLevel_ = 0.0f;
    int peakHoldCounter_ = 0;

    bool clipping_ = false;

    // Smoothing
    static constexpr float kAttack = 0.3f;    // Fast attack
    static constexpr float kRelease = 0.12f;  // Smooth release (~230ms half-life at 30Hz)
    static constexpr int kPeakHoldFrames = 30; // ~1 second at 30Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

/** Independent channel smoothing/peak holds in the existing meter footprint. */
class StereoLevelMeter : public juce::Component {
public:
    explicit StereoLevelMeter(const juce::String& label) : label_(label)
    {
        addAndMakeVisible(left_);
        addAndMakeVisible(right_);
        setName(label + " L/R level meter");
    }
    void setLevels(float left, float right) { left_.setLevel(left); right_.setLevel(right); }
    void tick() { left_.tick(); right_.tick(); }
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::white.withAlpha(0.65f));
        g.setFont(juce::Font(9.0f));
        g.drawText(label_, getLocalBounds().removeFromTop(16), juce::Justification::centred);
    }
    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(16);
        const int width = juce::jmax(0, (area.getWidth() - 4) / 2);
        left_.setBounds(area.removeFromLeft(width));
        area.removeFromLeft(4);
        right_.setBounds(area);
    }
private:
    juce::String label_;
    LevelMeter left_{"L"}, right_{"R"};
};

} // namespace directpipe
