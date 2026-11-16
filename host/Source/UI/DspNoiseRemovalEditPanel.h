// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "DspNoiseSpectrumView.h"
namespace directpipe {
class BuiltinNoiseRemovalDSP;
class DspNoiseRemovalEditPanel final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DspNoiseRemovalEditPanel(BuiltinNoiseRemovalDSP&);
    ~DspNoiseRemovalEditPanel() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void refresh();
    void timerCallback() override { refresh(); }
    BuiltinNoiseRemovalDSP& processor_;
    juce::Label modeLabel_, reductionLabel_, statusLabel_, progressLabel_, helpLabel_;
    juce::ComboBox modeCombo_;
    juce::TextButton learnButton_{"Learn"};
    juce::Slider reductionSlider_;
    DspNoiseSpectrumView spectrumView_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DspNoiseRemovalEditPanel)
};
}
