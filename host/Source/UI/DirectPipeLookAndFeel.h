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

private:
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
