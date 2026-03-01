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
 * @file DirectPipeLookAndFeel.h
 * @brief Custom look and feel for DirectPipe UI
 */
#pragma once

#include <JuceHeader.h>

namespace directpipe {

/**
 * @brief Dark theme look and feel for the DirectPipe application.
 *
 * Provides a modern, dark-themed UI with:
 * - Dark background (#1E1E2E)
 * - Accent color for active elements
 * - Rounded corners and subtle shadows
 */
class DirectPipeLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DirectPipeLookAndFeel();
    ~DirectPipeLookAndFeel() override = default;

    // Slider customization
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    // Toggle button customization
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    // ComboBox customization
    void drawComboBox(juce::Graphics& g, int width, int height,
                      bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    // TextButton customization
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    // Font overrides for CJK (Korean/Japanese/Chinese) character support
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;

private:
    juce::String cjkFontName_;              ///< Detected CJK-capable font name
    juce::Typeface::Ptr cjkTypeface_;       ///< Cached CJK-capable typeface
    juce::Typeface::Ptr cjkBoldTypeface_;   ///< Cached CJK-capable bold typeface
    // Color scheme
    juce::Colour bgColor_{0xFF1E1E2E};
    juce::Colour surfaceColor_{0xFF2A2A40};
    juce::Colour accentColor_{0xFF6C63FF};   // Purple accent
    juce::Colour accentAlt_{0xFF4CAF50};     // Green for active states
    juce::Colour textColor_{0xFFE0E0E0};
    juce::Colour dimTextColor_{0xFF8888AA};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DirectPipeLookAndFeel)
};

} // namespace directpipe
