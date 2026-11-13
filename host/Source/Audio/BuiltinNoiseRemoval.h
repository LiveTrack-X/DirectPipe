// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <rnnoise.h>
#include <atomic>
#include <vector>
#include "StreamingSincResampler.h"

namespace directpipe {

/**
 * @brief Built-in noise removal -- wraps RNNoise with FIFO buffering and VAD gating.
 *
 * ## Architecture Overview
 *
 * RNNoise is a neural-network-based noise suppressor that processes exactly
 * 480 mono float samples per frame at 48 kHz. This class bridges the gap
 * between RNNoise's fixed requirements and the JUCE host's variable buffer sizes.
 *
 * ## Key Design Decisions
 *
 * ### FIFO Buffering (2-pass architecture)
 * The host sends buffers of arbitrary size (e.g., 128, 256, 512 samples).
 * A FIFO accumulates input samples until a full 480-sample frame is ready,
 * then feeds it to RNNoise. The output FIFO stores processed frames for
 * the host to drain at its own pace.
 *
 * JUCE may reuse the same input/output buffer. The native path stages the
 * entire callback before draining output. The converted path instead consumes
 * each sample completely before writing that sample's output. Both preserve
 * unread input; writing over input that has not yet been consumed is unsafe.
 *
 * ### RNNoise Scaling (int16 range)
 * IMPORTANT: RNNoise was trained on int16 audio data, so it expects sample
 * values in the range [-32767, +32767]. JUCE audio is float [-1.0, +1.0].
 * We must scale by 32767 before processing and scale back after.
 * Forgetting this scaling makes RNNoise treat all audio as near-silence.
 *
 * ### VAD Gate with Hold Time
 * RNNoise returns a Voice Activity Detection (VAD) probability [0.0, 1.0]
 * alongside the denoised audio. We use this to gate the output:
 * - Above threshold: gate opens (voice detected)
 * - Below threshold but within hold time: gate stays open (natural pauses between words)
 * - Below threshold and hold expired: gate closes (suppress background noise)
 *
 * The gate starts CLOSED (0.0); lifecycle warmup and FIFO priming reduce startup
 * transients. The hold branch may begin opening it during initial frames.
 *
 * ### Sample-rate conversion
 * Native 48 kHz retains the original FIFO path. Other 8-192 kHz rates use
 * preallocated streaming low-pass converters around the same 48 kHz engine.
 * Gate timing always runs at 48 kHz. Reported latency includes conversion.
 *
 * ### Dual-Mono Processing
 * Each channel (L/R) has its own RNNoise instance, FIFO, and gate state.
 * This means stereo is processed as dual-mono (no cross-channel interaction).
 * Independent suppression/gating can change stereo balance; this is not
 * linked-stereo or binaural-preservation processing.
 *
 * Strength presets:
 *   0 = Light       (VAD threshold 0.50)
 *   1 = Standard    (VAD threshold 0.70)  -- default
 *   2 = Aggressive  (VAD threshold 0.90)
 *
 * Thread Ownership:
 *   processBlock()    -- [RT audio thread]
 *   prepareToPlay()   -- [Non-RT control/device lifecycle, rendering excluded]
 *   releaseResources()-- [Non-RT control/device lifecycle, rendering excluded]
 *   setters/getters   -- [Any thread] (atomic)
 *
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

    /** Set strength preset: 0=Light, 1=Standard, 2=Aggressive. */
    void setStrength(int strength);

    /** Current strength preset index. */
    int getStrength() const { return strength_.load(std::memory_order_relaxed); }

    /** Current VAD threshold (derived from strength, or set manually). */
    float getVadThreshold() const { return vadThreshold_.load(std::memory_order_relaxed); }

    /** Set VAD threshold directly (advanced override, 0.0-1.0). */
    void setVADThreshold(float threshold);

    enum class Mode { Inactive, Native48k, Resampled, UnsupportedRate, Unavailable };
    Mode getMode() const { return mode_.load(std::memory_order_acquire); }
    bool isActive() const { const auto m = getMode(); return m == Mode::Native48k || m == Mode::Resampled; }
    bool needsResampling() const { return needsResampling_.load(std::memory_order_relaxed); }
    juce::String getStatusText() const;

private:
    friend struct NoiseRemovalTestAccess;
    // -- Parameters --
    std::atomic<int>   strength_{ 1 };       // 0=Light, 1=Standard, 2=Aggressive
    std::atomic<float> vadThreshold_{ 0.70f };  // Standard preset default

    // -- RNNoise instances (created in prepareToPlay, destroyed in releaseResources) --
    DenoiseState* rnnL_ = nullptr;
    DenoiseState* rnnR_ = nullptr;

    // -- FIFO buffering --
    //
    // RNNoise frame size is always 480 samples (10ms at 48kHz, fixed by the neural network architecture).
    // The output FIFO is sized in prepareToPlay() from the largest supported
    // callback so Pass 1 can enqueue a whole callback before Pass 2 drains it.
    // Per-channel separate positions so L/R stay independent.
    //
    // NOTE: The output FIFO uses modular (ring buffer) indexing with its runtime
    // capacity
    // wrapping. Read and write positions grow monotonically and wrap via modulo.
    // This avoids the complexity of linear reset (which would need atomic
    // coordination between the read/write positions).
    // A power-of-two capacity keeps ring indexing continuous at uint32_t wrap
    // (~25 hours). Unsigned (write - read) gives the queued count across wrap.
    static constexpr int kRNNFrameSize = 480;
    // Vendored denoise.c: analysis overlap + delayed_X lookahead = two frames.
    static constexpr int kRNNAlgorithmDelay = 2 * kRNNFrameSize;
    static constexpr int kMaxSupportedBlockSize = 4096;
    static constexpr int kMinimumOutputFifoCapacity = kRNNFrameSize * 2;

    // Input FIFOs -- accumulate host samples until a full frame is ready
    std::vector<float> inputFifoL_;
    std::vector<float> inputFifoR_;
    int inputFifoWriteL_ = 0;  // Reset to 0 after each frame — no overflow risk
    int inputFifoWriteR_ = 0;

    // Output FIFOs -- store processed frames for the host to consume.
    // Read/write positions grow monotonically, wrapped by outputFifo.size().
    // uint32_t: unsigned overflow is well-defined (modulo 2^32), preventing
    // undefined behavior that would occur with signed int after ~12 hours
    // of continuous use at 48kHz (INT_MAX / 48000 ≈ 12.4 hours).
    std::vector<float> outputFifoL_;
    std::vector<float> outputFifoR_;
    uint32_t outputFifoReadL_  = 0;
    uint32_t outputFifoWriteL_ = 0;
    uint32_t outputFifoReadR_  = 0;
    uint32_t outputFifoWriteR_ = 0;

    // Lifecycle writes with rendering excluded; UI reads atomic status only.
    std::atomic<double> hostSampleRate_{48000.0};
    std::atomic<bool> needsResampling_{false};  // Lifecycle write, rendering/UI read.
    std::atomic<Mode> mode_{Mode::Inactive};
    struct ConversionChannel {
        StreamingSincResampler to48k, from48k;
        std::vector<float> output;
        size_t read = 0, write = 0, queued = 0;
    };
    ConversionChannel conversion_[2];

    // -- VAD gating (per-channel smooth gain + hold time) --
    //
    // Hold keeps gate open for ~300ms after last voice detection,
    // preventing choppy audio between words.
    //
    // NOTE: 300ms was chosen because it matches the typical duration of a natural
    // speech pause (e.g., between sentences or while thinking). Shorter hold times
    // (e.g., 100ms) cause choppy gating between words; longer (e.g., 1s) fails
    // to suppress noise during actual silence.
    int holdSamples_ = 14400;      // 300ms at 48kHz — recalculated in prepareToPlay
    float gateSmooth_ = 0.9990f;   // 20ms gate smoothing at 48kHz — recalculated in prepareToPlay
    float gateGainL_ = 0.0f;   // starts closed (warmup)
    float gateGainR_ = 0.0f;
    int holdCounterL_ = 0;
    int holdCounterR_ = 0;

    // -- Internal helpers --
    void destroyRNNoise();
    void processConvertedChannel(float* samples, int count, int channel);

    /** Process one channel through the FIFO + RNNoise pipeline.
     *  Called from processBlock for each active channel. */
    void processChannel(const float* in, float* out, int numSamples,
                        DenoiseState* rnn,
                        std::vector<float>& inputFifo, int& inputFifoWrite,
                        std::vector<float>& outputFifo, uint32_t& outputFifoRead, uint32_t& outputFifoWrite,
                        float& gateGain, int& holdCounter);
};

} // namespace directpipe
