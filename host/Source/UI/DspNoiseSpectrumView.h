// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "../Audio/BuiltinNoiseRemovalDSP.h"

namespace directpipe {
// Message-thread-only presentation of existing DSP analysis; no audio processing.
class DspNoiseSpectrumView final : public juce::Component {
public:
    DspNoiseSpectrumView();
    // nullptr means a contended read. Expire old analysis after a bounded grace period.
    void updateSnapshot(const BuiltinNoiseRemovalDSP::SpectrumSnapshot*, double nowMs);
    void setSelectedChannel(int channel);
    int getSelectedChannel() const noexcept { return channelCombo_.getSelectedId() == 2 ? 1 : 0; }
    bool hasVisibleData() const noexcept { return visible_; }
    juce::String getProfileCaption() const;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    BuiltinNoiseRemovalDSP::SpectrumSnapshot snapshot_;
    juce::ComboBox channelCombo_;
    bool visible_ = false, stale_ = false, haveGeneration_ = false;
    double lastFreshMs_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DspNoiseSpectrumView)
};
}
