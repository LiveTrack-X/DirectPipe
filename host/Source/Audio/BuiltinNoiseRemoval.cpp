// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "BuiltinNoiseRemoval.h"
#include "../UI/NoiseRemovalEditPanel.h"

namespace directpipe {

juce::AudioProcessorEditor* BuiltinNoiseRemoval::createEditor()
{
    return new NoiseRemovalEditPanel(*this);
}

// ─── Construction / Destruction ─────────────────────────────────

BuiltinNoiseRemoval::BuiltinNoiseRemoval()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // RNNoise instances are NOT created here (they malloc internally).
    // Created in prepareToPlay, destroyed in releaseResources / destructor.
}

BuiltinNoiseRemoval::~BuiltinNoiseRemoval()
{
    destroyRNNoise();
}

// ─── AudioProcessor lifecycle ───────────────────────────────────

void BuiltinNoiseRemoval::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    hostSampleRate_ = sampleRate;

    // TODO: When sampleRate != 48000, set up LagrangeInterpolator resampling.
    //       For now we only handle 48 kHz natively.
    needsResampling_.store(sampleRate != 48000.0, std::memory_order_relaxed);

    // I5: Log when noise removal is inactive due to non-48kHz sample rate
    if (needsResampling_.load(std::memory_order_relaxed)) {
        juce::Logger::writeToLog("[AUDIO] BuiltinNoiseRemoval: sample rate "
            + juce::String(sampleRate) + "Hz -- noise removal inactive (requires 48kHz, resampling TODO)");
    }

    // Create RNNoise denoise states
    destroyRNNoise();
    rnnL_ = rnnoise_create(nullptr);
    rnnR_ = rnnoise_create(nullptr);

    // Allocate FIFO buffers (pre-allocated, zero-filled)
    inputFifoL_.assign(kFifoCapacity, 0.0f);
    inputFifoR_.assign(kFifoCapacity, 0.0f);
    outputFifoL_.assign(kFifoCapacity, 0.0f);
    outputFifoR_.assign(kFifoCapacity, 0.0f);

    // Reset FIFO positions
    inputFifoWriteL_ = 0;
    inputFifoWriteR_ = 0;
    outputFifoReadL_  = 0;
    outputFifoWriteL_ = 0;
    outputFifoReadR_  = 0;
    outputFifoWriteR_ = 0;

    // Reset VAD gate gains
    gateGainL_ = 1.0f;
    gateGainR_ = 1.0f;

    // I2: Use base class setLatencySamples for proper AudioProcessor latency reporting
    setLatencySamples(kRNNFrameSize);  // 480 samples FIFO delay
}

void BuiltinNoiseRemoval::releaseResources()
{
    destroyRNNoise();
}

// ─── processBlock  [RT audio thread] ────────────────────────────

void BuiltinNoiseRemoval::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // If RNNoise was not created (should not happen) or resampling is needed
    // but not yet implemented, pass audio through unchanged.
    if (rnnL_ == nullptr || needsResampling_.load(std::memory_order_relaxed))
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0)
        return;

    // Channel 0 (Left / mono)
    if (numChannels > 0) {
        processChannel(buffer.getReadPointer(0), buffer.getWritePointer(0),
                       numSamples, rnnL_,
                       inputFifoL_, inputFifoWriteL_,
                       outputFifoL_, outputFifoReadL_, outputFifoWriteL_,
                       gateGainL_);
    }

    // Channel 1 (Right) -- dual-mono, independent RNNoise instance
    if (numChannels > 1 && rnnR_ != nullptr) {
        processChannel(buffer.getReadPointer(1), buffer.getWritePointer(1),
                       numSamples, rnnR_,
                       inputFifoR_, inputFifoWriteR_,
                       outputFifoR_, outputFifoReadR_, outputFifoWriteR_,
                       gateGainR_);
    }
}

// ─── Per-channel FIFO + RNNoise processing ──────────────────────

void BuiltinNoiseRemoval::processChannel(
    const float* in, float* out, int numSamples,
    DenoiseState* rnn,
    std::vector<float>& inputFifo,  int& inputFifoWrite,
    std::vector<float>& outputFifo, int& outputFifoRead, int& outputFifoWrite,
    float& gateGain)
{
    const float threshold = vadThreshold_.load(std::memory_order_relaxed);

    // Smoothing coefficient for ~5 ms gate transition at 48 kHz.
    // At 48 kHz, 5 ms = 240 samples.  Per-sample exponential:
    //   coeff = exp(-1 / 240) ~ 0.9958
    // We apply this per-sample inside the 480-sample frame loop.
    constexpr float kGateSmooth = 0.9958f;

    int samplesRead = 0;   // how many output samples we have written so far
    int samplesIn   = 0;   // how many input samples we have consumed so far

    while (samplesRead < numSamples) {
        // 1. Feed input samples into the input FIFO
        while (samplesIn < numSamples && inputFifoWrite < kRNNFrameSize) {
            inputFifo[static_cast<size_t>(inputFifoWrite)] = in[samplesIn];
            ++inputFifoWrite;
            ++samplesIn;
        }

        // 2. If we have a full RNNoise frame, process it
        if (inputFifoWrite >= kRNNFrameSize) {
            // C1: Bounds check -- prevent output FIFO overflow for large buffer sizes
            if (outputFifoWrite + kRNNFrameSize > kFifoCapacity) {
                // Zero remaining output samples to prevent unprocessed input leaking through
                while (samplesRead < numSamples) {
                    out[samplesRead] = 0.0f;
                    ++samplesRead;
                }
                break;
            }

            // C2: Use stack-allocated scratch buffers (480 * 4 = 1920 bytes each -- safe)
            float rnnIn[kRNNFrameSize];
            float rnnOut[kRNNFrameSize];

            // Copy from FIFO to scratch input (RNNoise may read/write in place)
            for (int i = 0; i < kRNNFrameSize; ++i)
                rnnIn[i] = inputFifo[static_cast<size_t>(i)];

            // Process through RNNoise -- returns VAD probability [0..1]
            float vad = rnnoise_process_frame(rnn, rnnOut, rnnIn);

            // VAD-based noise gating with smooth transition
            float targetGate = (vad >= threshold) ? 1.0f : 0.0f;
            for (int i = 0; i < kRNNFrameSize; ++i) {
                gateGain = kGateSmooth * gateGain + (1.0f - kGateSmooth) * targetGate;
                rnnOut[i] *= gateGain;
            }

            // Store processed frame into output FIFO
            for (int i = 0; i < kRNNFrameSize; ++i)
                outputFifo[static_cast<size_t>(outputFifoWrite + i)] = rnnOut[i];
            outputFifoWrite += kRNNFrameSize;

            // Reset input FIFO for next frame
            inputFifoWrite = 0;
        }

        // 3. Drain output FIFO into the host output buffer
        while (samplesRead < numSamples && outputFifoRead < outputFifoWrite) {
            out[samplesRead] = outputFifo[static_cast<size_t>(outputFifoRead)];
            ++outputFifoRead;
            ++samplesRead;
        }

        // If output FIFO is fully drained, reset positions
        if (outputFifoRead >= outputFifoWrite) {
            outputFifoRead  = 0;
            outputFifoWrite = 0;
        }

        // If we have consumed all input but still need more output samples
        // (initial latency period), output silence
        if (samplesIn >= numSamples && samplesRead < numSamples) {
            out[samplesRead] = 0.0f;
            ++samplesRead;
        }
    }
}

// ─── Strength / VAD threshold ───────────────────────────────────

void BuiltinNoiseRemoval::setStrength(int strength)
{
    strength = juce::jlimit(0, 2, strength);
    strength_.store(strength, std::memory_order_relaxed);

    float threshold;
    switch (strength) {
        case 0:  threshold = 0.35f; break;  // Light
        case 2:  threshold = 0.85f; break;  // Aggressive
        default: threshold = 0.60f; break;  // Standard
    }
    vadThreshold_.store(threshold, std::memory_order_relaxed);
}

void BuiltinNoiseRemoval::setVADThreshold(float threshold)
{
    vadThreshold_.store(juce::jlimit(0.0f, 1.0f, threshold), std::memory_order_relaxed);
}

// ─── State persistence (JSON) ───────────────────────────────────

void BuiltinNoiseRemoval::getStateInformation(juce::MemoryBlock& destData)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("strength", getStrength());
    obj->setProperty("vadThreshold", static_cast<double>(getVadThreshold()));

    auto json = juce::JSON::toString(juce::var(obj.release()));
    destData.replaceWith(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BuiltinNoiseRemoval::setStateInformation(const void* data, int sizeInBytes)
{
    auto json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    auto parsed = juce::JSON::parse(json);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("strength"))
            setStrength(static_cast<int>(obj->getProperty("strength")));
        // I7: Restore custom VAD threshold (may differ from strength-derived default)
        if (obj->hasProperty("vadThreshold"))
            setVADThreshold(static_cast<float>(static_cast<double>(obj->getProperty("vadThreshold"))));
    }
}

// ─── Internal helpers ───────────────────────────────────────────

void BuiltinNoiseRemoval::destroyRNNoise()
{
    if (rnnL_ != nullptr) {
        rnnoise_destroy(rnnL_);
        rnnL_ = nullptr;
    }
    if (rnnR_ != nullptr) {
        rnnoise_destroy(rnnR_);
        rnnR_ = nullptr;
    }
}

} // namespace directpipe
