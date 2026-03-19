// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "AGCEditPanel.h"
#include "../Audio/BuiltinAutoGain.h"

namespace directpipe {

// Dark theme colors (matching DirectPipeLookAndFeel)
namespace AGCColors {
    static constexpr juce::uint32 kBg      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurface = 0xFF2A2A40;
    static constexpr juce::uint32 kAccent  = 0xFF6C63FF;
    static constexpr juce::uint32 kText    = 0xFFE0E0E0;
    static constexpr juce::uint32 kDim     = 0xFF8888AA;
    static constexpr juce::uint32 kGreen   = 0xFF4CAF50;
}

AGCEditPanel::AGCEditPanel(BuiltinAutoGain& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    setSize(300, 280);

    // -- Target LUFS --
    targetLabel_.setText("Target LUFS:", juce::dontSendNotification);
    targetLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kText));
    addAndMakeVisible(targetLabel_);

    targetSlider_.setRange(-24.0, -6.0, 0.5);
    targetSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    targetSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(AGCColors::kAccent));
    targetSlider_.setColour(juce::Slider::trackColourId, juce::Colour(AGCColors::kSurface));
    targetSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(AGCColors::kText));
    targetSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(AGCColors::kSurface));
    targetSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    targetSlider_.setTextValueSuffix(" LUFS");
    targetSlider_.onValueChange = [this] {
        processor_.setTargetLUFS(static_cast<float>(targetSlider_.getValue()));
    };
    addAndMakeVisible(targetSlider_);

    // -- Current LUFS display --
    currentLufsLabel_.setText("Current LUFS:", juce::dontSendNotification);
    currentLufsLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kDim));
    addAndMakeVisible(currentLufsLabel_);

    currentLufsValue_.setText("--", juce::dontSendNotification);
    currentLufsValue_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kGreen));
    currentLufsValue_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(currentLufsValue_);

    // -- Current Gain display --
    currentGainLabel_.setText("Current Gain:", juce::dontSendNotification);
    currentGainLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kDim));
    addAndMakeVisible(currentGainLabel_);

    currentGainValue_.setText("--", juce::dontSendNotification);
    currentGainValue_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kGreen));
    currentGainValue_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(currentGainValue_);

    // -- Advanced toggle --
    advancedToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(AGCColors::kDim));
    advancedToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour(AGCColors::kAccent));
    advancedToggle_.onClick = [this] { updateAdvancedVisibility(); };
    addAndMakeVisible(advancedToggle_);

    // -- Low Correct slider (0-100%) --
    lowCorrLabel_.setText("Low Correct (Boost):", juce::dontSendNotification);
    lowCorrLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kDim));
    addAndMakeVisible(lowCorrLabel_);

    lowCorrSlider_.setRange(0.0, 100.0, 1.0);
    lowCorrSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    lowCorrSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(AGCColors::kAccent));
    lowCorrSlider_.setColour(juce::Slider::trackColourId, juce::Colour(AGCColors::kSurface));
    lowCorrSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(AGCColors::kText));
    lowCorrSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(AGCColors::kSurface));
    lowCorrSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lowCorrSlider_.setTextValueSuffix("%");
    lowCorrSlider_.onValueChange = [this] {
        // UI shows 0-100%, processor expects 0.0-1.0 factor
        processor_.setLowCorrect(static_cast<float>(lowCorrSlider_.getValue() / 100.0));
    };
    addAndMakeVisible(lowCorrSlider_);

    // -- High Correct slider (0-100%) --
    highCorrLabel_.setText("High Correct (Cut):", juce::dontSendNotification);
    highCorrLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kDim));
    addAndMakeVisible(highCorrLabel_);

    highCorrSlider_.setRange(0.0, 100.0, 1.0);
    highCorrSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    highCorrSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(AGCColors::kAccent));
    highCorrSlider_.setColour(juce::Slider::trackColourId, juce::Colour(AGCColors::kSurface));
    highCorrSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(AGCColors::kText));
    highCorrSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(AGCColors::kSurface));
    highCorrSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    highCorrSlider_.setTextValueSuffix("%");
    highCorrSlider_.onValueChange = [this] {
        processor_.setHighCorrect(static_cast<float>(highCorrSlider_.getValue() / 100.0));
    };
    addAndMakeVisible(highCorrSlider_);

    // -- Max Gain slider (6-30 dB) --
    maxGainLabel_.setText("Max Gain:", juce::dontSendNotification);
    maxGainLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kDim));
    addAndMakeVisible(maxGainLabel_);

    maxGainSlider_.setRange(6.0, 30.0, 0.5);
    maxGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    maxGainSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(AGCColors::kAccent));
    maxGainSlider_.setColour(juce::Slider::trackColourId, juce::Colour(AGCColors::kSurface));
    maxGainSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(AGCColors::kText));
    maxGainSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(AGCColors::kSurface));
    maxGainSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    maxGainSlider_.setTextValueSuffix(" dB");
    maxGainSlider_.onValueChange = [this] {
        processor_.setMaxGaindB(static_cast<float>(maxGainSlider_.getValue()));
    };
    addAndMakeVisible(maxGainSlider_);

    // -- Freeze Level slider (-60 to -10 dBFS) --
    // NOTE: Freeze uses per-block RMS (dBFS), NOT LUFS. See BuiltinAutoGain.cpp.
    freezeLabel_.setText("Freeze Level:", juce::dontSendNotification);
    freezeLabel_.setColour(juce::Label::textColourId, juce::Colour(AGCColors::kDim));
    addAndMakeVisible(freezeLabel_);

    freezeSlider_.setRange(-60.0, -10.0, 1.0);
    freezeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    freezeSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(AGCColors::kAccent));
    freezeSlider_.setColour(juce::Slider::trackColourId, juce::Colour(AGCColors::kSurface));
    freezeSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(AGCColors::kText));
    freezeSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(AGCColors::kSurface));
    freezeSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    freezeSlider_.setTextValueSuffix(" dBFS");
    freezeSlider_.onValueChange = [this] {
        processor_.setFreezeLevel(static_cast<float>(freezeSlider_.getValue()));
    };
    addAndMakeVisible(freezeSlider_);

    // Sync initial state
    syncFromProcessor();
    advancedToggle_.setToggleState(false, juce::dontSendNotification);
    updateAdvancedVisibility();

    // Start polling at ~10 Hz
    startTimerHz(10);
}

AGCEditPanel::~AGCEditPanel()
{
    stopTimer();
}

void AGCEditPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(AGCColors::kBg));
}

void AGCEditPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int rowH = 24;
    const int labelW = 120;

    // Target LUFS row
    auto targetRow = area.removeFromTop(rowH);
    targetLabel_.setBounds(targetRow.removeFromLeft(labelW));
    targetSlider_.setBounds(targetRow);
    area.removeFromTop(4);

    // Current LUFS row
    auto lufsRow = area.removeFromTop(rowH);
    currentLufsLabel_.setBounds(lufsRow.removeFromLeft(labelW));
    currentLufsValue_.setBounds(lufsRow);
    area.removeFromTop(2);

    // Current Gain row
    auto gainRow = area.removeFromTop(rowH);
    currentGainLabel_.setBounds(gainRow.removeFromLeft(labelW));
    currentGainValue_.setBounds(gainRow);
    area.removeFromTop(8);

    // Advanced toggle
    advancedToggle_.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(4);

    // Low Correct row
    auto lowRow = area.removeFromTop(rowH);
    lowCorrLabel_.setBounds(lowRow.removeFromLeft(labelW));
    lowCorrSlider_.setBounds(lowRow);
    area.removeFromTop(2);

    // High Correct row
    auto highRow = area.removeFromTop(rowH);
    highCorrLabel_.setBounds(highRow.removeFromLeft(labelW));
    highCorrSlider_.setBounds(highRow);
    area.removeFromTop(2);

    // Max Gain row
    auto maxRow = area.removeFromTop(rowH);
    maxGainLabel_.setBounds(maxRow.removeFromLeft(labelW));
    maxGainSlider_.setBounds(maxRow);
    area.removeFromTop(2);

    // Freeze Level row
    auto freezeRow = area.removeFromTop(rowH);
    freezeLabel_.setBounds(freezeRow.removeFromLeft(labelW));
    freezeSlider_.setBounds(freezeRow);
}

void AGCEditPanel::timerCallback()
{
    // Poll current LUFS and gain from processor (atomics, safe from any thread)
    float lufs = processor_.getCurrentLUFS();
    float gaindB = processor_.getCurrentGaindB();

    juce::String lufsStr;
    if (lufs <= -59.0f)
        lufsStr = "-inf";
    else
        lufsStr = juce::String(lufs, 1) + " LUFS";
    currentLufsValue_.setText(lufsStr, juce::dontSendNotification);

    juce::String gainStr = ((gaindB >= 0.0f) ? "+" : "") + juce::String(gaindB, 1) + " dB";
    currentGainValue_.setText(gainStr, juce::dontSendNotification);
}

void AGCEditPanel::syncFromProcessor()
{
    targetSlider_.setValue(processor_.getTargetLUFS(), juce::dontSendNotification);
    lowCorrSlider_.setValue(processor_.getLowCorrect() * 100.0, juce::dontSendNotification);
    highCorrSlider_.setValue(processor_.getHighCorrect() * 100.0, juce::dontSendNotification);
    maxGainSlider_.setValue(processor_.getMaxGaindB(), juce::dontSendNotification);
    freezeSlider_.setValue(processor_.getFreezeLevel(), juce::dontSendNotification);
}

void AGCEditPanel::updateAdvancedVisibility()
{
    bool show = advancedToggle_.getToggleState();
    lowCorrLabel_.setVisible(show);
    lowCorrSlider_.setVisible(show);
    highCorrLabel_.setVisible(show);
    highCorrSlider_.setVisible(show);
    maxGainLabel_.setVisible(show);
    maxGainSlider_.setVisible(show);
    freezeLabel_.setVisible(show);
    freezeSlider_.setVisible(show);
}

} // namespace directpipe
