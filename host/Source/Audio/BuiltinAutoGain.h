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
 * ## Architecture Overview
 *
 * Measures short-term LUFS via K-weighted sidechain, then applies asymmetric
 * correction gain to the ORIGINAL (un-weighted) audio. The K-weighting is
 * applied to a COPY of the audio -- it is purely for measurement, never
 * heard by the user.
 *
 * ## Key Design Decisions
 *
 * ### Sidechain Measurement (K-weighting on copy, not original)
 * IMPORTANT: K-weighting reshapes the frequency response to match human hearing
 * perception (ITU-R BS.1770). If we applied K-weighting to the actual audio,
 * it would color the sound. Instead, we copy the buffer to a scratch buffer,
 * apply K-weighting there, measure loudness, then apply ONLY a flat gain
 * change to the original unmodified audio. This preserves the tonal character.
 *
 * ### Asymmetric Correction (boost vs cut have different speeds)
 * - Boosting (quiet audio): controlled by lowCorrect (Low Correction %).
 *   Slower response avoids amplifying transient noises (keyboard, breathing).
 * - Cutting (loud audio): controlled by highCorrect (High Correction %).
 *   Faster response prevents clipping and protects downstream equipment.
 *
 * ### Correction % Controls Envelope SPEED, Not Gain AMOUNT
 * IMPORTANT: A common misconception is that "50% correction" means "apply 50%
 * of the needed gain." In this implementation, correction % scales the envelope
 * follower COEFFICIENT, not the gain amount. Both 50% and 100% correction
 * eventually reach the same target gain -- 100% just gets there faster.
 * This gives users intuitive control: "how aggressively should AGC react?"
 *
 * ### Freeze Level Uses Per-Block RMS (Instant Reaction)
 * The Freeze Level threshold uses per-block RMS (~2.7ms window at 128 samples)
 * instead of the 1.5s LUFS window. This is critical because:
 * - LUFS has a 1.5s window -- a brief keyboard click barely moves the average
 * - Per-block RMS reacts within ONE buffer period (~2.7ms)
 * - Without instant freeze, AGC would boost keyboard clicks and breathing
 *   by up to +24dB before the LUFS window catches up
 *
 * ### Attack/Release Envelope Follower
 * Per-sample IIR smoothing with direction-dependent coefficients:
 * - Attack (500ms): gain increasing (boosting quiet audio)
 * - Release (700ms): gain decreasing (cutting loud audio)
 * These are computed as: coeff = 1 - exp(-1 / (time_seconds * sampleRate))
 * and applied per-sample as: gain += coeff * (target - gain)
 *
 * ### Max Gain Limit
 * Limits total boost/cut to prevent extreme amplification. Without this,
 * silence between sentences could be boosted by +60dB, amplifying room noise
 * to painful levels.
 *
 * Algorithm:
 *   1. Copy buffer to scratch (measurement only)
 *   2. Apply ITU-R BS.1770 K-weighting (high shelf + HPF) to scratch
 *   3. Accumulate squared K-weighted samples in 1.5s ring buffer
 *   4. Compute LUFS = -0.691 + 10*log10(mean_square)
 *   5. Asymmetric correction: boost uses lowCorrect, cut uses highCorrect
 *   6. Smooth gain via envelope follower (500ms attack, 700ms release)
 *   7. Apply smoothed gain to original (un-weighted) audio with linear ramp
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

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        auto in = layouts.getMainInputChannelSet();
        auto out = layouts.getMainOutputChannelSet();
        if (in != out) return false;
        return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    }

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

    void setFreezeLevel(float lufs);
    float getFreezeLevel() const { return freezeLevel_.load(std::memory_order_relaxed); }

    // -- UI feedback (read from any thread, written from RT thread) --
    float getCurrentLUFS() const { return currentLUFS_.load(std::memory_order_relaxed); }
    float getCurrentGaindB() const { return currentGaindB_.load(std::memory_order_relaxed); }

private:
    // -- Parameters (atomic, any thread) --
    std::atomic<float> targetLUFS_{ -15.0f };    // target loudness level in LUFS
    std::atomic<float> lowCorrect_{ 0.50f };     // boost correction SPEED factor (0.0=frozen, 2.0=2x speed)
    std::atomic<float> highCorrect_{ 0.75f };    // cut correction SPEED factor (0.0=frozen, 2.0=2x speed)
    std::atomic<float> maxGaindB_{ 24.0f };      // maximum boost/cut in dB (prevents extreme amplification)
    std::atomic<float> freezeLevel_{ -45.0f };   // dBFS -- per-block RMS below this = don't boost (silence/breath/keyboard)

    // -- K-weighting filters (sidechain -- measurement only, RT thread) --
    juce::IIRFilter kStage1L_, kStage1R_;  // High shelf (+4dB at ~1681Hz)
    juce::IIRFilter kStage2L_, kStage2R_;  // High-pass (38Hz, 2nd order)

    // -- LUFS measurement ring buffer (RT thread only) --
    //
    // Stores squared K-weighted mono samples for a sliding window.
    // runningSquareSum_ is an incremental accumulator: when a new sample enters
    // the ring buffer, we subtract the old value being overwritten and add the
    // new value. This gives O(blockSize) per processBlock instead of O(windowSize)
    // (~72k samples for 1.5s at 48kHz). Without this optimization, scanning the
    // entire ring buffer every block would consume excessive CPU on the RT thread.
    //
    // NOTE: Floating-point drift can cause runningSquareSum_ to go slightly negative
    // over time due to accumulated rounding errors. We clamp to 0.0 before use.
    std::vector<float> lufsRingBuf_;   // 1.5 seconds of squared K-weighted mono samples
    int lufsWritePos_ = 0;
    int lufsSampleCount_ = 0;          // number of valid samples (grows until window is full)
    int lufsWindowSize_ = 0;           // 1.5 * sampleRate (was 3s, shortened for faster response)
    double runningSquareSum_ = 0.0;    // incremental sum -- avoids O(windowSize) scan per block

    // -- Gain state (RT thread only) --
    float currentGainLinear_ = 1.0f;   // current smoothed gain (persists across blocks for continuity)
    float attackCoeff_ = 0.0f;         // envelope follower: 500ms attack (gain increasing = boosting)
    float releaseCoeff_ = 0.0f;        // envelope follower: 700ms release (gain decreasing = cutting)

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
