// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

namespace directpipe {

/**
 * @brief Built-in LUFS-based auto-gain control (Luveler Mode 2 style).
 *
 * Measures short-term LUFS (3-second window) via K-weighted sidechain,
 * then applies asymmetric correction gain to the original audio.
 *
 * Algorithm:
 *   1. Copy buffer to scratch (measurement only)
 *   2. Apply ITU-R BS.1770 K-weighting (high shelf + HPF) to scratch
 *   3. Accumulate squared K-weighted samples in 3s ring buffer
 *   4. Compute LUFS = -0.691 + 10*log10(mean_square)
 *   5. Asymmetric correction: boost uses lowCorrect, cut uses highCorrect
 *   6. Smooth gain via envelope follower (2s attack, 3s release)
 *   7. Apply smoothed gain to original (un-weighted) audio
 *
 * Thread Ownership:
 *   processBlock()    -- [RT audio thread]
 *   prepareToPlay()   -- [Message thread]
 *   setters/getters   -- [Any thread] (atomic)
 */
class BuiltinAutoGain : public juce::AudioProcessor {
public:
    BuiltinAutoGain();

    // AudioProcessor interface
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    const juce::String getName() const override { return "AutoGain"; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Required stubs (NOT const -- JUCE 7)
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // -- Parameter accessors (atomic, any thread) --

    void setTargetLUFS(float lufs);
    float getTargetLUFS() const { return targetLUFS_.load(std::memory_order_relaxed); }

    void setLowCorrect(float factor);
    float getLowCorrect() const { return lowCorrect_.load(std::memory_order_relaxed); }

    void setHighCorrect(float factor);
    float getHighCorrect() const { return highCorrect_.load(std::memory_order_relaxed); }

    void setMaxGaindB(float dB);
    float getMaxGaindB() const { return maxGaindB_.load(std::memory_order_relaxed); }

    // -- UI feedback (read from any thread, written from RT thread) --
    float getCurrentLUFS() const { return currentLUFS_.load(std::memory_order_relaxed); }
    float getCurrentGaindB() const { return currentGaindB_.load(std::memory_order_relaxed); }

private:
    // -- Parameters (atomic, any thread) --
    std::atomic<float> targetLUFS_{ -15.0f };
    std::atomic<float> lowCorrect_{ 0.50f };    // boost correction factor
    std::atomic<float> highCorrect_{ 0.75f };   // cut correction factor
    std::atomic<float> maxGaindB_{ 18.0f };

    // -- K-weighting filters (sidechain -- measurement only, RT thread) --
    juce::IIRFilter kStage1L_, kStage1R_;  // High shelf (+4dB at ~1681Hz)
    juce::IIRFilter kStage2L_, kStage2R_;  // High-pass (38Hz, 2nd order)

    // -- LUFS measurement ring buffer (RT thread only) --
    std::vector<float> lufsRingBuf_;   // 3 seconds of squared K-weighted mono samples
    int lufsWritePos_ = 0;
    int lufsSampleCount_ = 0;          // number of valid samples in ring buffer
    int lufsWindowSize_ = 0;           // 3 * sampleRate
    double runningSquareSum_ = 0.0;    // I1: incremental sum for O(1) LUFS calculation

    // -- Gain state (RT thread only) --
    float currentGainLinear_ = 1.0f;
    float attackCoeff_ = 0.0f;    // envelope follower: 2s attack
    float releaseCoeff_ = 0.0f;   // envelope follower: 3s release

    double currentSR_ = 48000.0;

    // -- Pre-allocated scratch buffer for K-weighting measurement (RT thread only) --
    juce::AudioBuffer<float> kWeightScratch_;

    // -- UI feedback (written from RT, read from any thread) --
    std::atomic<float> currentLUFS_{ -60.0f };
    std::atomic<float> currentGaindB_{ 0.0f };

    // -- Internal helpers --
    void updateKWeightingCoeffs();
};

} // namespace directpipe
