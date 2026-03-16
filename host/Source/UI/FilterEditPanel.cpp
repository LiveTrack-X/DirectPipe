// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "FilterEditPanel.h"
#include "../Audio/BuiltinFilter.h"

namespace directpipe {

// Dark theme colors (matching DirectPipeLookAndFeel)
namespace FilterColors {
    static constexpr juce::uint32 kBg      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurface = 0xFF2A2A40;
    static constexpr juce::uint32 kAccent  = 0xFF6C63FF;
    static constexpr juce::uint32 kText    = 0xFFE0E0E0;
    static constexpr juce::uint32 kDim     = 0xFF8888AA;
}

FilterEditPanel::FilterEditPanel(BuiltinFilter& processor)
    : AudioProcessorEditor(processor), filter_(processor)
{
    setSize(300, 200);

    // -- HPF toggle --
    hpfToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(FilterColors::kText));
    hpfToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour(FilterColors::kAccent));
    hpfToggle_.onClick = [this] {
        filter_.setHPFEnabled(hpfToggle_.getToggleState());
    };
    addAndMakeVisible(hpfToggle_);

    // -- HPF preset combo --
    hpfPreset_.addItem("60 Hz", 1);
    hpfPreset_.addItem("80 Hz", 2);
    hpfPreset_.addItem("120 Hz", 3);
    hpfPreset_.addItem("Custom", 4);
    hpfPreset_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(FilterColors::kSurface));
    hpfPreset_.setColour(juce::ComboBox::textColourId, juce::Colour(FilterColors::kText));
    hpfPreset_.setColour(juce::ComboBox::outlineColourId, juce::Colour(FilterColors::kDim));
    hpfPreset_.onChange = [this] {
        int sel = hpfPreset_.getSelectedId();
        switch (sel) {
            case 1: filter_.setHPFFrequency(60.0f);  hpfSlider_.setValue(60.0, juce::dontSendNotification); break;
            case 2: filter_.setHPFFrequency(80.0f);  hpfSlider_.setValue(80.0, juce::dontSendNotification); break;
            case 3: filter_.setHPFFrequency(120.0f); hpfSlider_.setValue(120.0, juce::dontSendNotification); break;
            case 4: // Custom -- use slider value
                filter_.setHPFFrequency(static_cast<float>(hpfSlider_.getValue()));
                break;
            default: break;
        }
        updateHPFVisibility();
    };
    addAndMakeVisible(hpfPreset_);

    // -- HPF custom slider --
    hpfSlider_.setRange(20.0, 300.0, 1.0);
    hpfSlider_.setValue(filter_.getHPFFrequency(), juce::dontSendNotification);
    hpfSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    hpfSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(FilterColors::kAccent));
    hpfSlider_.setColour(juce::Slider::trackColourId, juce::Colour(FilterColors::kSurface));
    hpfSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(FilterColors::kText));
    hpfSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(FilterColors::kSurface));
    hpfSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    hpfSlider_.setTextValueSuffix(" Hz");
    hpfSlider_.onValueChange = [this] {
        if (hpfPreset_.getSelectedId() == 4)
            filter_.setHPFFrequency(static_cast<float>(hpfSlider_.getValue()));
    };
    addAndMakeVisible(hpfSlider_);

    hpfSliderLabel_.setText("Freq:", juce::dontSendNotification);
    hpfSliderLabel_.setColour(juce::Label::textColourId, juce::Colour(FilterColors::kDim));
    addAndMakeVisible(hpfSliderLabel_);

    // -- LPF toggle --
    lpfToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(FilterColors::kText));
    lpfToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour(FilterColors::kAccent));
    lpfToggle_.onClick = [this] {
        filter_.setLPFEnabled(lpfToggle_.getToggleState());
    };
    addAndMakeVisible(lpfToggle_);

    // -- LPF preset combo --
    lpfPreset_.addItem("16 kHz", 1);
    lpfPreset_.addItem("12 kHz", 2);
    lpfPreset_.addItem("8 kHz", 3);
    lpfPreset_.addItem("Custom", 4);
    lpfPreset_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(FilterColors::kSurface));
    lpfPreset_.setColour(juce::ComboBox::textColourId, juce::Colour(FilterColors::kText));
    lpfPreset_.setColour(juce::ComboBox::outlineColourId, juce::Colour(FilterColors::kDim));
    lpfPreset_.onChange = [this] {
        int sel = lpfPreset_.getSelectedId();
        switch (sel) {
            case 1: filter_.setLPFFrequency(16000.0f); lpfSlider_.setValue(16000.0, juce::dontSendNotification); break;
            case 2: filter_.setLPFFrequency(12000.0f); lpfSlider_.setValue(12000.0, juce::dontSendNotification); break;
            case 3: filter_.setLPFFrequency(8000.0f);  lpfSlider_.setValue(8000.0, juce::dontSendNotification); break;
            case 4: // Custom -- use slider value
                filter_.setLPFFrequency(static_cast<float>(lpfSlider_.getValue()));
                break;
            default: break;
        }
        updateLPFVisibility();
    };
    addAndMakeVisible(lpfPreset_);

    // -- LPF custom slider --
    lpfSlider_.setRange(4000.0, 20000.0, 100.0);
    lpfSlider_.setValue(filter_.getLPFFrequency(), juce::dontSendNotification);
    lpfSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 20);
    lpfSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(FilterColors::kAccent));
    lpfSlider_.setColour(juce::Slider::trackColourId, juce::Colour(FilterColors::kSurface));
    lpfSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(FilterColors::kText));
    lpfSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(FilterColors::kSurface));
    lpfSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lpfSlider_.setTextValueSuffix(" Hz");
    lpfSlider_.onValueChange = [this] {
        if (lpfPreset_.getSelectedId() == 4)
            filter_.setLPFFrequency(static_cast<float>(lpfSlider_.getValue()));
    };
    addAndMakeVisible(lpfSlider_);

    lpfSliderLabel_.setText("Freq:", juce::dontSendNotification);
    lpfSliderLabel_.setColour(juce::Label::textColourId, juce::Colour(FilterColors::kDim));
    addAndMakeVisible(lpfSliderLabel_);

    // Sync initial state from processor
    syncFromProcessor();
}

void FilterEditPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(FilterColors::kBg));

    // Section divider line between HPF and LPF
    g.setColour(juce::Colour(FilterColors::kSurface));
    g.drawHorizontalLine(getHeight() / 2, 10.0f, static_cast<float>(getWidth() - 10));
}

void FilterEditPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int rowH = 28;

    // -- HPF section (top half) --
    auto hpfArea = area.removeFromTop(area.getHeight() / 2);

    auto hpfRow1 = hpfArea.removeFromTop(rowH);
    hpfToggle_.setBounds(hpfRow1.removeFromLeft(60));
    hpfPreset_.setBounds(hpfRow1.reduced(4, 2));

    hpfArea.removeFromTop(4);

    auto hpfRow2 = hpfArea.removeFromTop(rowH);
    hpfSliderLabel_.setBounds(hpfRow2.removeFromLeft(40));
    hpfSlider_.setBounds(hpfRow2);

    // -- LPF section (bottom half) --
    area.removeFromTop(6);  // gap after divider
    auto lpfArea = area;

    auto lpfRow1 = lpfArea.removeFromTop(rowH);
    lpfToggle_.setBounds(lpfRow1.removeFromLeft(60));
    lpfPreset_.setBounds(lpfRow1.reduced(4, 2));

    lpfArea.removeFromTop(4);

    auto lpfRow2 = lpfArea.removeFromTop(rowH);
    lpfSliderLabel_.setBounds(lpfRow2.removeFromLeft(40));
    lpfSlider_.setBounds(lpfRow2);
}

void FilterEditPanel::syncFromProcessor()
{
    hpfToggle_.setToggleState(filter_.isHPFEnabled(), juce::dontSendNotification);
    lpfToggle_.setToggleState(filter_.isLPFEnabled(), juce::dontSendNotification);

    // Determine which HPF preset matches (or Custom)
    float hpfHz = filter_.getHPFFrequency();
    if (std::abs(hpfHz - 60.0f) < 0.5f)
        hpfPreset_.setSelectedId(1, juce::dontSendNotification);
    else if (std::abs(hpfHz - 80.0f) < 0.5f)
        hpfPreset_.setSelectedId(2, juce::dontSendNotification);
    else if (std::abs(hpfHz - 120.0f) < 0.5f)
        hpfPreset_.setSelectedId(3, juce::dontSendNotification);
    else {
        hpfPreset_.setSelectedId(4, juce::dontSendNotification);
        hpfSlider_.setValue(hpfHz, juce::dontSendNotification);
    }

    // Determine which LPF preset matches (or Custom)
    float lpfHz = filter_.getLPFFrequency();
    if (std::abs(lpfHz - 16000.0f) < 50.0f)
        lpfPreset_.setSelectedId(1, juce::dontSendNotification);
    else if (std::abs(lpfHz - 12000.0f) < 50.0f)
        lpfPreset_.setSelectedId(2, juce::dontSendNotification);
    else if (std::abs(lpfHz - 8000.0f) < 50.0f)
        lpfPreset_.setSelectedId(3, juce::dontSendNotification);
    else {
        lpfPreset_.setSelectedId(4, juce::dontSendNotification);
        lpfSlider_.setValue(lpfHz, juce::dontSendNotification);
    }

    updateHPFVisibility();
    updateLPFVisibility();
}

void FilterEditPanel::updateHPFVisibility()
{
    bool isCustom = (hpfPreset_.getSelectedId() == 4);
    hpfSlider_.setVisible(isCustom);
    hpfSliderLabel_.setVisible(isCustom);
}

void FilterEditPanel::updateLPFVisibility()
{
    bool isCustom = (lpfPreset_.getSelectedId() == 4);
    lpfSlider_.setVisible(isCustom);
    lpfSliderLabel_.setVisible(isCustom);
}

} // namespace directpipe
