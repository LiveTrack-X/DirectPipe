// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include "PluginEditor.h"

DirectPipeReceiverEditor::DirectPipeReceiverEditor(DirectPipeReceiverProcessor& p)
    : AudioProcessorEditor(p)
    , processor_(p)
    , muteAttachment_(*p.getAPVTS().getParameter("mute"), muteButton_, nullptr)
{
    setSize(kWidth, kHeight);

    muteButton_.setClickingTogglesState(true);
    muteButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE05050));
    muteButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
    muteButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    muteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(muteButton_);

    // Buffer combo — populate items BEFORE creating attachment
    bufferCombo_.addItem("Ultra Low (~5ms)", 1);
    bufferCombo_.addItem("Low (~10ms)", 2);
    bufferCombo_.addItem("Medium (~21ms)", 3);
    bufferCombo_.addItem("High (~42ms)", 4);
    bufferCombo_.addItem("Safe (~85ms)", 5);
    bufferCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF2A2A40));
    bufferCombo_.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    bufferCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3A3A5A));
    addAndMakeVisible(bufferCombo_);

    // Attach after items are populated
    bufferAttachment_ = std::make_unique<juce::ComboBoxParameterAttachment>(
        *p.getAPVTS().getParameter("buffer"), bufferCombo_, nullptr);

    bufferLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF8888AA));
    bufferLabel_.setFont(juce::Font(12.0f));
    addAndMakeVisible(bufferLabel_);

    startTimerHz(10);
}

DirectPipeReceiverEditor::~DirectPipeReceiverEditor()
{
    stopTimer();
}

void DirectPipeReceiverEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E2E));

    auto bounds = getLocalBounds().reduced(12);
    int y = bounds.getY();

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("DirectPipe Receiver", bounds.getX(), y, bounds.getWidth(), 22,
               juce::Justification::centredLeft);
    y += 28;

    // Connection status
    bool connected = processor_.isConnected();
    juce::Colour statusColour = connected ? juce::Colour(0xFF4CAF50) : juce::Colour(0xFFE05050);

    // Status circle
    g.setColour(statusColour);
    g.fillEllipse(static_cast<float>(bounds.getX()), static_cast<float>(y + 3), 10.0f, 10.0f);

    // Status text
    g.setFont(juce::Font(13.0f));
    juce::String statusText = connected ? "Connected" : "Disconnected";
    g.drawText(statusText, bounds.getX() + 16, y, 100, 16, juce::Justification::centredLeft);

    // Audio info (sample rate + channels)
    if (connected) {
        uint32_t sr = processor_.getSourceSampleRate();
        uint32_t ch = processor_.getSourceChannels();
        juce::String info = juce::String(sr) + "Hz  " + juce::String(ch) + "ch";
        g.setColour(juce::Colour(0xFF8888AA));
        g.drawText(info, bounds.getX() + 120, y, bounds.getWidth() - 120, 16,
                   juce::Justification::centredRight);
    }
    y += 22;

    // Version
    g.setColour(juce::Colour(0xFF555577));
    g.setFont(juce::Font(10.0f));
    g.drawText("v3.7.0", bounds.getX(), bounds.getBottom() - 14, bounds.getWidth(), 14,
               juce::Justification::centredRight);
}

void DirectPipeReceiverEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    int y = bounds.getY() + 28 + 22 + 8; // after title + status + gap

    muteButton_.setBounds(bounds.getX(), y, bounds.getWidth(), 32);
    y += 40;

    // Buffer selector row
    int labelW = 50;
    bufferLabel_.setBounds(bounds.getX(), y, labelW, 24);
    bufferCombo_.setBounds(bounds.getX() + labelW + 4, y, bounds.getWidth() - labelW - 4, 24);
}

void DirectPipeReceiverEditor::timerCallback()
{
    bool connected = processor_.isConnected();
    uint32_t sr = processor_.getSourceSampleRate();
    uint32_t ch = processor_.getSourceChannels();

    if (connected != lastConnected_ || sr != lastSampleRate_ || ch != lastChannels_) {
        lastConnected_ = connected;
        lastSampleRate_ = sr;
        lastChannels_ = ch;
        repaint();
    }
}
