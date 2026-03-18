// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

namespace directpipe {

/**
 * @brief Built-in LUFS-based auto-gain control (WebRTC AGC dual-envelope pattern).
 *
 * ## Architecture Overview
 *
 * Measures short-term LUFS via K-weighted sidechain, then applies asymmetric
 * correction gain to the ORIGINAL (un-weighted) audio. Uses dual-envelope level
 * detection (fast + slow) inspired by WebRTC AGC for responsive loud speech suppression.
 *
 * ## Key Design Decisions
 *
 * ### Dual-Envelope Level Detection (WebRTC AGC pattern)
 * Instead of relying solely on a slow LUFS window (which lags behind level changes),
 * we use two parallel level trackers:
 * - Fast envelope (~10ms attack, ~200ms release): catches loud transients instantly
 * - Slow LUFS window (0.4s EBU Momentary): provides stable average
 * Effective level = max(fast, slow) — loud signals are caught immediately while
 * quiet signals use the stable average for gentle boost.
 * Reference: WebRTC AGC uses capacitorFast + capacitorSlow with max selection.
 *
 * ### Direct Gain Computation (no IIR gain envelope)
 * Gain is computed directly from the level measurement and applied via linear ramp.
 * Previous design used an IIR envelope follower on the gain (500ms/700ms), which
 * created DOUBLE smoothing (slow measurement + slow gain convergence). Removing
 * the gain envelope and using only level-domain smoothing gives faster response.
 * Correction % (lowCorr/hiCorr) now blends between hold and full correction per block.
 *
 * ### Sidechain Measurement (K-weighting on copy, not original)
 * K-weighting reshapes the frequency response to match human hearing perception
 * (ITU-R BS.1770). Applied to a COPY of the audio — purely for measurement.
 *
 * ### Asymmetric Correction (boost vs cut have different speeds)
 * - Boosting (quiet): lowCorrect blends between hold and full correction (gentle)
 * - Cutting (loud): highCorrect blends between hold and full correction (fast)
 *
 * ### Freeze Level Uses Per-Block RMS (Instant Reaction)
 * Freeze gate uses per-block RMS (~2.7ms) to detect silence immediately.
 * During freeze, current gain is HELD (not reset to 0dB).
 *
 * ### Target Offset (-6dB)
 * Internal target is 6dB below user setting to compensate for systematic overshoot
 * from open-loop pre-gain measurement. Keeps output closer to commercial levelers.
 *
 * Algorithm:
 *   1. Copy buffer to scratch (measurement only)
 *   2. Apply ITU-R BS.1770 K-weighting (high shelf + HPF) to scratch
 *   3. Accumulate squared K-weighted samples in 0.4s ring buffer (EBU Momentary)
 *   4. Compute LUFS = -0.691 + 10*log10(mean_square)
 *   5. Dual-envelope: fast(10ms/200ms) + slow(LUFS), effective = max(fast, slow)
 *   6. Compute gain directly: correction = (target-6dB) - effective, blend by lowCorr/hiCorr
 *   7. Apply gain to original audio with per-block linear ramp (click-free)
 *
 * Defaults: target -15 LUFS, lowCorr 0.50, hiCorr 0.90, maxGain 22dB, freeze -45dBFS
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
    std::atomic<float> lowCorrect_{ 0.50f };     // boost correction blend (0.0=hold current gain, 1.0=full correction)
    std::atomic<float> highCorrect_{ 0.90f };    // cut correction blend (0.0=hold current gain, 1.0=full correction)
    std::atomic<float> maxGaindB_{ 22.0f };      // maximum boost/cut in dB (prevents extreme amplification)
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
    // (~19k samples for 0.4s at 48kHz). Without this optimization, scanning the
    // entire ring buffer every block would consume excessive CPU on the RT thread.
    //
    // NOTE: Floating-point drift can cause runningSquareSum_ to go slightly negative
    // over time due to accumulated rounding errors. We clamp to 0.0 before use.
    std::vector<float> lufsRingBuf_;   // 0.4 seconds of squared K-weighted mono samples (EBU Momentary)
    int lufsWritePos_ = 0;
    int lufsSampleCount_ = 0;          // number of valid samples (grows until window is full)
    int lufsWindowSize_ = 0;           // 0.4 * sampleRate (EBU R128 Momentary window)
    std::atomic<double> runningSquareSum_{0.0};    // incremental sum -- avoids O(windowSize) scan per block

    // -- Dual envelope level detection (WebRTC AGC pattern, RT thread only) --
    float fastEnvelope_ = -60.0f;      // fast envelope on LUFS (~10ms attack, ~200ms release)

    // -- Gain state (RT thread only) --
    float currentGainLinear_ = 1.0f;   // current smoothed gain (persists across blocks for continuity)
    float attackCoeff_ = 0.0f;         // per-sample smoothing: 500ms attack (gain increasing = boosting)
    float releaseCoeff_ = 0.0f;        // per-sample smoothing: 100ms release (gain decreasing = cutting)

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
