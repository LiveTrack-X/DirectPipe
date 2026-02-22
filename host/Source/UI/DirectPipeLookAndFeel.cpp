/**
 * @file DirectPipeLookAndFeel.cpp
 * @brief Custom look and feel implementation
 */

#include "DirectPipeLookAndFeel.h"

namespace directpipe {

DirectPipeLookAndFeel::DirectPipeLookAndFeel()
{
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
    float toggleSize = 18.0f;
    float toggleX = bounds.getX() + 2.0f;
    float toggleY = bounds.getCentreY() - toggleSize / 2.0f;

    // Toggle background
    auto toggleBounds = juce::Rectangle<float>(toggleX, toggleY, toggleSize, toggleSize);
    g.setColour(button.getToggleState() ? accentAlt_ : surfaceColor_);
    g.fillRoundedRectangle(toggleBounds, 3.0f);

    // Border
    g.setColour(shouldDrawButtonAsHighlighted ? textColor_ : dimTextColor_);
    g.drawRoundedRectangle(toggleBounds, 3.0f, 1.0f);

    // Checkmark
    if (button.getToggleState()) {
        g.setColour(juce::Colours::white);
        auto checkBounds = toggleBounds.reduced(4.0f);
        juce::Path checkPath;
        checkPath.startNewSubPath(checkBounds.getX(), checkBounds.getCentreY());
        checkPath.lineTo(checkBounds.getCentreX(), checkBounds.getBottom());
        checkPath.lineTo(checkBounds.getRight(), checkBounds.getY());
        g.strokePath(checkPath, juce::PathStrokeType(2.0f));
    }

    // Label text
    g.setColour(textColor_);
    g.setFont(juce::FontOptions(13.0f));
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
    const juce::Colour& /*backgroundColour*/,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    juce::Colour bgCol = surfaceColor_;
    if (shouldDrawButtonAsDown)
        bgCol = accentColor_;
    else if (shouldDrawButtonAsHighlighted)
        bgCol = surfaceColor_.brighter(0.15f);

    g.setColour(bgCol);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colour(0xFF3A3A5A));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

} // namespace directpipe
