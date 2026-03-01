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
 * @file DirectPipeLookAndFeel.cpp
 * @brief Custom look and feel implementation
 */

#include "DirectPipeLookAndFeel.h"

namespace directpipe {

namespace {
    juce::String findCjkFontName()
    {
        // Try Windows CJK fonts in preference order
        static const char* candidates[] = {
            "Malgun Gothic",     // 맑은 고딕 (Windows 7+)
            "Microsoft YaHei",   // Chinese fallback
            "Yu Gothic",         // Japanese fallback
            "Segoe UI",          // Wide Unicode coverage
        };
        auto allFonts = juce::Font::findAllTypefaceNames();
        for (auto* name : candidates) {
            if (allFonts.contains(name))
                return name;
        }
        return juce::Font::getDefaultSansSerifFontName();
    }
}

DirectPipeLookAndFeel::DirectPipeLookAndFeel()
{
    // Cache CJK-capable typefaces for Korean/Japanese/Chinese device name rendering
    cjkFontName_ = findCjkFontName();
    auto fontName = cjkFontName_;
    // Use Semilight for normal text (thicker than plain Malgun Gothic)
    // and Bold for bold text — ensures Korean text is legible at small sizes
    cjkTypeface_ = juce::Typeface::createSystemTypefaceFor(
        juce::Font(fontName, 15.0f, juce::Font::plain));
    cjkBoldTypeface_ = juce::Typeface::createSystemTypefaceFor(
        juce::Font(fontName, 15.0f, juce::Font::bold));

    // Set JUCE default typeface so all Font() constructors use the CJK font
    setDefaultSansSerifTypefaceName(fontName);

    // Set the dark color scheme
    setColour(juce::ResizableWindow::backgroundColourId, bgColor_);
    setColour(juce::DocumentWindow::backgroundColourId, bgColor_);

    setColour(juce::Label::textColourId, textColor_);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, surfaceColor_);
    setColour(juce::TextButton::textColourOnId, textColor_);
    setColour(juce::TextButton::textColourOffId, textColor_);

    setColour(juce::ComboBox::backgroundColourId, surfaceColor_);
    setColour(juce::ComboBox::textColourId, textColor_);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3A3A5A));
    setColour(juce::ComboBox::arrowColourId, dimTextColor_);

    setColour(juce::PopupMenu::backgroundColourId, surfaceColor_);
    setColour(juce::PopupMenu::textColourId, textColor_);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentColor_);

    setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF15152A));
    setColour(juce::Slider::trackColourId, accentColor_);
    setColour(juce::Slider::thumbColourId, accentColor_.brighter(0.3f));
    setColour(juce::Slider::textBoxTextColourId, textColor_);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF15152A));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF2A2A40));
    setColour(juce::ListBox::textColourId, textColor_);

    setColour(juce::ToggleButton::textColourId, textColor_);
    setColour(juce::ToggleButton::tickColourId, accentAlt_);
    setColour(juce::ToggleButton::tickDisabledColourId, dimTextColor_);
}

void DirectPipeLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
    juce::Slider::SliderStyle /*style*/, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>(
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(width), static_cast<float>(height));

    // Track background
    float trackHeight = 4.0f;
    float trackY = bounds.getCentreY() - trackHeight / 2.0f;

    g.setColour(slider.findColour(juce::Slider::backgroundColourId));
    g.fillRoundedRectangle(bounds.getX(), trackY, bounds.getWidth(), trackHeight, 2.0f);

    // Filled portion
    float fillWidth = sliderPos - bounds.getX();
    g.setColour(slider.findColour(juce::Slider::trackColourId));
    g.fillRoundedRectangle(bounds.getX(), trackY, fillWidth, trackHeight, 2.0f);

    // Thumb
    float thumbSize = 14.0f;
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillEllipse(sliderPos - thumbSize / 2.0f,
                  bounds.getCentreY() - thumbSize / 2.0f,
                  thumbSize, thumbSize);
}

void DirectPipeLookAndFeel::drawToggleButton(
    juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat();
    float disabledAlpha = button.isEnabled() ? 1.0f : 0.4f;
    float toggleSize = 18.0f;
    float toggleX = bounds.getX() + 2.0f;
    float toggleY = bounds.getCentreY() - toggleSize / 2.0f;

    // Toggle background
    auto toggleBounds = juce::Rectangle<float>(toggleX, toggleY, toggleSize, toggleSize);
    g.setColour((button.getToggleState() ? accentAlt_ : surfaceColor_).withMultipliedAlpha(disabledAlpha));
    g.fillRoundedRectangle(toggleBounds, 3.0f);

    // Border
    g.setColour((shouldDrawButtonAsHighlighted ? textColor_ : dimTextColor_).withMultipliedAlpha(disabledAlpha));
    g.drawRoundedRectangle(toggleBounds, 3.0f, 1.0f);

    // Checkmark
    if (button.getToggleState()) {
        g.setColour(juce::Colours::white.withMultipliedAlpha(disabledAlpha));
        auto checkBounds = toggleBounds.reduced(4.0f);
        juce::Path checkPath;
        checkPath.startNewSubPath(checkBounds.getX(), checkBounds.getCentreY());
        checkPath.lineTo(checkBounds.getCentreX(), checkBounds.getBottom());
        checkPath.lineTo(checkBounds.getRight(), checkBounds.getY());
        g.strokePath(checkPath, juce::PathStrokeType(2.0f));
    }

    // Label text
    g.setColour(textColor_.withMultipliedAlpha(disabledAlpha));
    g.setFont(juce::Font(cjkFontName_, 14.0f, juce::Font::bold));
    g.drawText(button.getButtonText(),
               static_cast<int>(toggleX + toggleSize + 6), 0,
               button.getWidth() - static_cast<int>(toggleSize + 8),
               button.getHeight(),
               juce::Justification::centredLeft);
}

void DirectPipeLookAndFeel::drawComboBox(
    juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
    int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
    juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0,
                                          static_cast<float>(width),
                                          static_cast<float>(height));

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Draw arrow
    auto arrowBounds = bounds.removeFromRight(30.0f).reduced(8.0f);
    juce::Path arrow;
    arrow.startNewSubPath(arrowBounds.getX(), arrowBounds.getCentreY() - 2.0f);
    arrow.lineTo(arrowBounds.getCentreX(), arrowBounds.getCentreY() + 3.0f);
    arrow.lineTo(arrowBounds.getRight(), arrowBounds.getCentreY() - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(arrow, juce::PathStrokeType(1.5f));
}

void DirectPipeLookAndFeel::drawButtonBackground(
    juce::Graphics& g, juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    // Use the button's actual colour (respects buttonOnColourId for toggled buttons)
    juce::Colour bgCol = backgroundColour;
    if (shouldDrawButtonAsDown)
        bgCol = bgCol.brighter(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        bgCol = bgCol.brighter(0.1f);

    if (!button.isEnabled())
        bgCol = bgCol.withAlpha(0.4f);

    g.setColour(bgCol);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Brighter border for toggled-on buttons
    bool isOn = button.getToggleState() && button.getClickingTogglesState();
    g.setColour(isOn ? backgroundColour.brighter(0.3f) : juce::Colour(0xFF3A3A5A));
    g.drawRoundedRectangle(bounds, 4.0f, isOn ? 1.5f : 1.0f);
}

juce::Font DirectPipeLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return juce::Font(cjkFontName_,
                      juce::jmin(15.0f, static_cast<float>(box.getHeight()) * 0.85f),
                      juce::Font::bold);
}

juce::Font DirectPipeLookAndFeel::getPopupMenuFont()
{
    return juce::Font(cjkFontName_, 15.0f, juce::Font::bold);
}

juce::Typeface::Ptr DirectPipeLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    // Return cached CJK typeface for default sans-serif requests
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() ||
        font.getTypefaceName() == "<Sans-Serif>" ||
        font.getTypefaceName() == cjkFontName_)
    {
        return font.isBold() ? cjkBoldTypeface_ : cjkTypeface_;
    }
    return LookAndFeel_V4::getTypefaceForFont(font);
}

} // namespace directpipe
