// SPDX-License-Identifier: GPL-3.0-or-later
#include "DspNoiseRemovalEditPanel.h"
#include "../Audio/BuiltinNoiseRemovalDSP.h"
namespace directpipe {
DspNoiseRemovalEditPanel::DspNoiseRemovalEditPanel(BuiltinNoiseRemovalDSP& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    for (auto* label : {&modeLabel_, &reductionLabel_, &statusLabel_, &progressLabel_, &helpLabel_}) {
        label->setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
        label->setFont(juce::Font(13.0f));
        addAndMakeVisible(label);
    }
    modeLabel_.setText("Estimation mode", juce::dontSendNotification);
    reductionLabel_.setText("Reduction", juce::dontSendNotification);
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setMinimumHorizontalScale(0.9f);
    helpLabel_.setJustificationType(juce::Justification::topLeft);
    helpLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffb5b5ce));
    helpLabel_.setMinimumHorizontalScale(1.0f);
    modeCombo_.addItem("Learn (frozen profile)", 1);
    modeCombo_.addItem("Realtime (adaptive)", 2);
    modeCombo_.setName("Estimation mode");
    modeCombo_.onChange = [this] {
        processor_.setMode(modeCombo_.getSelectedId() == 2 ? BuiltinNoiseRemovalDSP::Mode::Realtime
                                                       : BuiltinNoiseRemovalDSP::Mode::Learn);
        refresh();
    };
    addAndMakeVisible(modeCombo_);
    learnButton_.setName("Learn noise profile");
    learnButton_.onClick = [this] { processor_.startLearning(); refresh(); };
    addAndMakeVisible(learnButton_);
    reductionSlider_.setName("Reduction in decibels");
    reductionSlider_.setRange(0, 24, 0.1);
    reductionSlider_.setTextValueSuffix(" dB");
    reductionSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 24);
    reductionSlider_.onValueChange = [this] { processor_.setReductionDb(static_cast<float>(reductionSlider_.getValue())); };
    addAndMakeVisible(reductionSlider_);
    addAndMakeVisible(spectrumView_);
    setSize(640, 560);
    refresh();
    startTimerHz(20);
}

void DspNoiseRemovalEditPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
}

void DspNoiseRemovalEditPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    statusLabel_.setBounds(area.removeFromTop(36));
    area.removeFromTop(4);
    auto row = area.removeFromTop(28);
    modeLabel_.setBounds(row.removeFromLeft(122));
    modeCombo_.setBounds(row.reduced(2));
    area.removeFromTop(4);
    row = area.removeFromTop(28);
    reductionLabel_.setBounds(row.removeFromLeft(122));
    reductionSlider_.setBounds(row);
    area.removeFromTop(4);
    row = area.removeFromTop(28);
    learnButton_.setBounds(row.removeFromLeft(116));
    progressLabel_.setBounds(row.reduced(8, 0));
    area.removeFromTop(6);
    helpLabel_.setBounds(area.removeFromTop(42));
    area.removeFromTop(6);
    spectrumView_.setBounds(area);
}

void DspNoiseRemovalEditPanel::refresh()
{
    BuiltinNoiseRemovalDSP::SpectrumSnapshot snapshot;
    const bool copied = processor_.getSpectrumSnapshot(snapshot);
    spectrumView_.updateSnapshot(copied ? &snapshot : nullptr, juce::Time::getMillisecondCounterHiRes());
    const bool learnMode = processor_.getMode() == BuiltinNoiseRemovalDSP::Mode::Learn;
    const bool learning = processor_.isLearning();
    modeCombo_.setSelectedId(learnMode ? 1 : 2, juce::dontSendNotification);
    reductionSlider_.setValue(processor_.getReductionDb(), juce::dontSendNotification);
    learnButton_.setButtonText(processor_.hasLearnedProfile() ? "Relearn" : "Learn");
    learnButton_.setEnabled(learnMode && processor_.isActive() && !learning);
    statusLabel_.setText(processor_.getStatusText(), juce::dontSendNotification);
    statusLabel_.setTooltip(statusLabel_.getText());
    progressLabel_.setText(!learnMode ? "Tracking automatically"
        : learning ? "Capturing: " + juce::String(processor_.getLearnProgress() * 100, 1) + "% / 2 seconds"
        : processor_.hasLearnedProfile() ? "Manual noise profile saved" : "Manual noise profile needed", juce::dontSendNotification);
    helpLabel_.setText(learnMode
        ? "Capture 2 seconds of background noise only, without speaking or singing. The completed profile stays frozen. Relearn when the noise changes."
        : "Realtime estimates background noise automatically and needs time to settle. Weak voices may be reduced. This DSP filter does not use AI/VAD voice isolation.",
        juce::dontSendNotification);
}
}
