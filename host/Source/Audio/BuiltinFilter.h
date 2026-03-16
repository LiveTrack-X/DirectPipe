// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace directpipe {

/**
 * @brief Built-in HPF + LPF filter -- AudioProcessor for chain insertion.
 *
 * Behaves like a VST plugin in the AudioProcessorGraph.
 * Edit button opens FilterEditPanel (DirectPipe custom UI).
 *
 * Thread Ownership:
 *   processBlock()    -- [RT audio thread]
 *   prepareToPlay()   -- [Message thread]
 *   setters/getters   -- [Any thread] (atomic)
 */
class BuiltinFilter : public juce::AudioProcessor {
public:
    BuiltinFilter();

    // AudioProcessor interface
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    const juce::String getName() const override { return "Filter"; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Required stubs
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // Parameter accessors (atomic, any thread)
    void setHPFEnabled(bool enabled);
    void setHPFFrequency(float hz);
    void setLPFEnabled(bool enabled);
    void setLPFFrequency(float hz);

    bool isHPFEnabled() const { return hpfEnabled_.load(std::memory_order_relaxed); }
    float getHPFFrequency() const { return hpfFreq_.load(std::memory_order_relaxed); }
    bool isLPFEnabled() const { return lpfEnabled_.load(std::memory_order_relaxed); }
    float getLPFFrequency() const { return lpfFreq_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> hpfEnabled_{true};
    std::atomic<float> hpfFreq_{60.0f};
    std::atomic<bool> lpfEnabled_{false};
    std::atomic<float> lpfFreq_{16000.0f};

    // IIR filters (per-channel, RT thread only)
    juce::IIRFilter hpfL_, hpfR_;
    juce::IIRFilter lpfL_, lpfR_;

    double currentSampleRate_ = 48000.0;

    // Track last applied frequencies to detect changes
    float lastHPFFreq_ = 0.0f;
    float lastLPFFreq_ = 0.0f;

    void updateFilterCoeffs();
};

} // namespace directpipe
