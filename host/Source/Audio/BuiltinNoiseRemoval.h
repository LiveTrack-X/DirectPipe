// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <rnnoise.h>
#include <atomic>
#include <vector>

namespace directpipe {

/**
 * @brief Built-in noise removal -- wraps RNNoise with FIFO buffering and VAD gating.
 *
 * RNNoise processes exactly 480 mono float samples at 48 kHz.
 * This processor handles arbitrary host buffer sizes via input/output FIFO
 * and applies VAD-based noise gating with smooth gain transitions.
 *
 * Strength presets:
 *   0 = Light       (VAD threshold 0.35)
 *   1 = Standard    (VAD threshold 0.60)  -- default
 *   2 = Aggressive  (VAD threshold 0.85)
 *
 * Thread Ownership:
 *   processBlock()    -- [RT audio thread]
 *   prepareToPlay()   -- [Message thread]
 *   releaseResources()-- [Message thread]
 *   setters/getters   -- [Any thread] (atomic)
 *
 * TODO: Resampling support for sample rates != 48 kHz
 *       (juce::LagrangeInterpolator up/down around RNNoise frames)
 */
class BuiltinNoiseRemoval : public juce::AudioProcessor {
public:
    BuiltinNoiseRemoval();
    ~BuiltinNoiseRemoval() override;

    // AudioProcessor interface
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    const juce::String getName() const override { return "NoiseRemoval"; }

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

    /** Set strength preset: 0=Light, 1=Standard, 2=Aggressive. */
    void setStrength(int strength);

    /** Current strength preset index. */
    int getStrength() const { return strength_.load(std::memory_order_relaxed); }

    /** Current VAD threshold (derived from strength, or set manually). */
    float getVadThreshold() const { return vadThreshold_.load(std::memory_order_relaxed); }

    /** Set VAD threshold directly (advanced override, 0.0-1.0). */
    void setVADThreshold(float threshold);

    // I5: Status accessors for UI (e.g., edit panel can show resampling warning)
    bool isActive() const { return !needsResampling_.load(std::memory_order_relaxed); }
    bool needsResampling() const { return needsResampling_.load(std::memory_order_relaxed); }

private:
    // -- Parameters --
    std::atomic<int>   strength_{ 1 };       // 0=Light, 1=Standard, 2=Aggressive
    std::atomic<float> vadThreshold_{ 0.60f };

    // -- RNNoise instances (created in prepareToPlay, destroyed in releaseResources) --
    DenoiseState* rnnL_ = nullptr;
    DenoiseState* rnnR_ = nullptr;

    // -- FIFO buffering --
    //
    // RNNoise frame size is always 480 samples.
    // FIFO capacity is 2x frame size to allow accumulation while draining.
    // Per-channel separate positions so L/R stay independent.
    static constexpr int kRNNFrameSize = 480;
    static constexpr int kFifoCapacity = kRNNFrameSize * 2;

    // Input FIFOs -- accumulate host samples until a full frame is ready
    std::vector<float> inputFifoL_;
    std::vector<float> inputFifoR_;
    int inputFifoWriteL_ = 0;
    int inputFifoWriteR_ = 0;

    // Output FIFOs -- store processed frames for the host to consume
    std::vector<float> outputFifoL_;
    std::vector<float> outputFifoR_;
    int outputFifoReadL_  = 0;
    int outputFifoWriteL_ = 0;
    int outputFifoReadR_  = 0;
    int outputFifoWriteR_ = 0;

    // -- Resampling (TODO) --
    double hostSampleRate_ = 48000.0;
    std::atomic<bool> needsResampling_{false};  // I5: atomic -- set in prepareToPlay (msg), read in processBlock (RT)
    // juce::LagrangeInterpolator resamplerInL_, resamplerInR_;
    // juce::LagrangeInterpolator resamplerOutL_, resamplerOutR_;
    // std::vector<float> resampleBuf_;

    // -- VAD gating (per-channel smooth gain) --
    float gateGainL_ = 1.0f;
    float gateGainR_ = 1.0f;

    // -- Internal helpers --
    void destroyRNNoise();

    /** Process one channel through the FIFO + RNNoise pipeline.
     *  Called from processBlock for each active channel. */
    void processChannel(const float* in, float* out, int numSamples,
                        DenoiseState* rnn,
                        std::vector<float>& inputFifo, int& inputFifoWrite,
                        std::vector<float>& outputFifo, int& outputFifoRead, int& outputFifoWrite,
                        float& gateGain);
};

} // namespace directpipe
