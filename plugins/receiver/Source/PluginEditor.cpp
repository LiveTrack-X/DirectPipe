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
    bufferCombo_.addItem("Ultra Low (256)", 1);
    bufferCombo_.addItem("Low (512)", 2);
    bufferCombo_.addItem("Medium (1024)", 3);
    bufferCombo_.addItem("High (2048)", 4);
    bufferCombo_.addItem("Safe (4096)", 5);
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

    bufferLatencyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF8888AA));
    bufferLatencyLabel_.setFont(juce::Font(10.0f));
    addAndMakeVisible(bufferLatencyLabel_);

    srWarningLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFCC8844));
    srWarningLabel_.setFont(juce::Font(10.0f));
    addAndMakeVisible(srWarningLabel_);

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
    g.drawText("v3.9.0", bounds.getX(), bounds.getBottom() - 14, bounds.getWidth(), 14,
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
    y += 26;
    bufferLatencyLabel_.setBounds(bounds.getX() + labelW + 4, y, bounds.getWidth() - labelW - 4, 14);
    y += 16;
    srWarningLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 14);
}

void DirectPipeReceiverEditor::timerCallback()
{
    bool connected = processor_.isConnected();
    uint32_t sr = processor_.getSourceSampleRate();
    uint32_t ch = processor_.getSourceChannels();
    bool srChanged = sr != lastSampleRate_;

    if (connected != lastConnected_ || srChanged || ch != lastChannels_) {
        lastConnected_ = connected;
        lastSampleRate_ = sr;
        lastChannels_ = ch;
        repaint();
    }

    // Update buffer latency display based on host sample rate
    // (host SR is always known; source SR may be 0 when disconnected)
    int bufIdx = bufferCombo_.getSelectedItemIndex();
    uint32_t hostSr = static_cast<uint32_t>(processor_.getSampleRate());
    if (bufIdx != lastBufferIdx_ || srChanged || hostSr != lastHostSr_) {
        lastBufferIdx_ = bufIdx;
        lastHostSr_ = hostSr;
        uint32_t samples = processor_.getTargetFillFrames();
        if (hostSr > 0 && samples > 0) {
            double ms = (static_cast<double>(samples) / static_cast<double>(hostSr)) * 1000.0;
            bufferLatencyLabel_.setText(
                juce::String(ms, 2) + " ms  (" + juce::String(samples) + " samples @ "
                    + juce::String(hostSr) + " Hz)",
                juce::dontSendNotification);
        } else {
            bufferLatencyLabel_.setText("", juce::dontSendNotification);
        }
    }

    // SR mismatch warning (source vs host)
    bool mismatch = connected && sr > 0 && hostSr > 0 && sr != hostSr;
    if (mismatch != lastSrMismatch_) {
        lastSrMismatch_ = mismatch;
        if (mismatch)
            srWarningLabel_.setText(
                "SR mismatch: " + juce::String(sr) + " vs " + juce::String(hostSr),
                juce::dontSendNotification);
        else
            srWarningLabel_.setText("", juce::dontSendNotification);
    }
}
