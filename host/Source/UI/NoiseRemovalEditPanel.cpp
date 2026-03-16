// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "NoiseRemovalEditPanel.h"
#include "../Audio/BuiltinNoiseRemoval.h"

namespace directpipe {

// Dark theme colors (matching DirectPipeLookAndFeel)
namespace NRColors {
    static constexpr juce::uint32 kBg      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurface = 0xFF2A2A40;
    static constexpr juce::uint32 kAccent  = 0xFF6C63FF;
    static constexpr juce::uint32 kText    = 0xFFE0E0E0;
    static constexpr juce::uint32 kDim     = 0xFF8888AA;
}

NoiseRemovalEditPanel::NoiseRemovalEditPanel(BuiltinNoiseRemoval& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    setSize(300, 150);

    // -- Strength label --
    strengthLabel_.setText("Strength:", juce::dontSendNotification);
    strengthLabel_.setColour(juce::Label::textColourId, juce::Colour(NRColors::kText));
    addAndMakeVisible(strengthLabel_);

    // -- Strength combo --
    strengthCombo_.addItem("Light", 1);
    strengthCombo_.addItem("Standard", 2);
    strengthCombo_.addItem("Aggressive", 3);
    strengthCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(NRColors::kSurface));
    strengthCombo_.setColour(juce::ComboBox::textColourId, juce::Colour(NRColors::kText));
    strengthCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(NRColors::kDim));
    strengthCombo_.onChange = [this] {
        int sel = strengthCombo_.getSelectedId();
        if (sel >= 1 && sel <= 3)
            processor_.setStrength(sel - 1);  // combo IDs 1-3 -> strength 0-2
    };
    addAndMakeVisible(strengthCombo_);

    // -- Advanced toggle --
    advancedToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(NRColors::kDim));
    advancedToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour(NRColors::kAccent));
    advancedToggle_.onClick = [this] { updateAdvancedVisibility(); };
    addAndMakeVisible(advancedToggle_);

    // -- VAD threshold label --
    vadLabel_.setText("VAD Threshold:", juce::dontSendNotification);
    vadLabel_.setColour(juce::Label::textColourId, juce::Colour(NRColors::kDim));
    addAndMakeVisible(vadLabel_);

    // -- VAD threshold slider --
    vadSlider_.setRange(0.0, 1.0, 0.01);
    vadSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    vadSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(NRColors::kAccent));
    vadSlider_.setColour(juce::Slider::trackColourId, juce::Colour(NRColors::kSurface));
    vadSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(NRColors::kText));
    vadSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(NRColors::kSurface));
    vadSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    vadSlider_.onValueChange = [this] {
        processor_.setVADThreshold(static_cast<float>(vadSlider_.getValue()));
    };
    addAndMakeVisible(vadSlider_);

    // -- Status warning label (non-48kHz) --
    statusLabel_.setFont(juce::Font(11.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF8800));
    addAndMakeVisible(statusLabel_);

    // Sync from processor and hide advanced by default
    syncFromProcessor();
    advancedToggle_.setToggleState(false, juce::dontSendNotification);
    updateAdvancedVisibility();
    updateStatusWarning();
}

void NoiseRemovalEditPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(NRColors::kBg));
}

void NoiseRemovalEditPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int rowH = 28;

    // Status warning (only takes space when visible)
    if (statusLabel_.isVisible()) {
        statusLabel_.setBounds(area.removeFromTop(18));
        area.removeFromTop(4);
    }

    // Strength row
    auto strengthRow = area.removeFromTop(rowH);
    strengthLabel_.setBounds(strengthRow.removeFromLeft(80));
    strengthCombo_.setBounds(strengthRow.reduced(4, 2));

    area.removeFromTop(8);

    // Advanced toggle
    advancedToggle_.setBounds(area.removeFromTop(rowH));

    area.removeFromTop(4);

    // VAD threshold row (advanced section)
    auto vadRow = area.removeFromTop(rowH);
    vadLabel_.setBounds(vadRow.removeFromLeft(100));
    vadSlider_.setBounds(vadRow);
}

void NoiseRemovalEditPanel::syncFromProcessor()
{
    // Strength: 0=Light, 1=Standard, 2=Aggressive -> combo IDs 1-3
    strengthCombo_.setSelectedId(processor_.getStrength() + 1, juce::dontSendNotification);

    // VAD threshold
    vadSlider_.setValue(processor_.getVadThreshold(), juce::dontSendNotification);
}

void NoiseRemovalEditPanel::updateAdvancedVisibility()
{
    bool show = advancedToggle_.getToggleState();
    vadLabel_.setVisible(show);
    vadSlider_.setVisible(show);
}

void NoiseRemovalEditPanel::updateStatusWarning()
{
    if (processor_.needsResampling()) {
        statusLabel_.setText("Inactive: requires 48 kHz sample rate", juce::dontSendNotification);
        statusLabel_.setVisible(true);
    } else {
        statusLabel_.setVisible(false);
    }
}

} // namespace directpipe
