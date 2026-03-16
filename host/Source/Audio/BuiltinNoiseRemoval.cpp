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

void BuiltinNoiseRemoval::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate_ = sampleRate;

    // TODO: When sampleRate != 48000, set up LagrangeInterpolator resampling.
    //       For now we only handle 48 kHz natively.
    needsResampling_.store(sampleRate != 48000.0, std::memory_order_relaxed);

    // I5: Log when noise removal is inactive due to non-48kHz sample rate
    if (needsResampling_.load(std::memory_order_relaxed)) {
        juce::Logger::writeToLog("WRN [AUDIO] BuiltinNoiseRemoval: sample rate "
            + juce::String(sampleRate) + "Hz -- noise removal INACTIVE (requires 48kHz)");
    }

    // Create RNNoise denoise states
    destroyRNNoise();
    rnnL_ = rnnoise_create(nullptr);
    rnnR_ = rnnoise_create(nullptr);

    juce::Logger::writeToLog("[AUDIO] BuiltinNoiseRemoval: prepareToPlay SR="
        + juce::String(sampleRate) + " BS=" + juce::String(samplesPerBlock)
        + " rnnL=" + juce::String(rnnL_ != nullptr ? "OK" : "NULL")
        + " rnnR=" + juce::String(rnnR_ != nullptr ? "OK" : "NULL")
        + " needsResampling=" + juce::String(needsResampling_.load() ? "YES" : "NO"));

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

    // Start with gate CLOSED -- prevents initial noise burst before RNNoise stabilizes
    gateGainL_ = 0.0f;
    gateGainR_ = 0.0f;

    // Warm up RNNoise with silent frames so it learns the noise floor faster
    {
        float silent[kRNNFrameSize] = {};
        float dummy[kRNNFrameSize];
        for (int i = 0; i < 5; ++i) {  // 5 frames = ~50ms warmup
            if (rnnL_) rnnoise_process_frame(rnnL_, dummy, silent);
            if (rnnR_) rnnoise_process_frame(rnnR_, dummy, silent);
        }
    }

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
    constexpr float kGateSmooth = 0.9958f;
    constexpr float kScale = 32767.0f;
    constexpr float kInvScale = 1.0f / 32767.0f;

    // ══ PASS 1: Copy ALL input into inputFifo, process frames as they complete ══
    // CRITICAL: in and out may alias (JUCE in-place). Read all input first.
    for (int i = 0; i < numSamples; ++i) {
        inputFifo[static_cast<size_t>(inputFifoWrite)] = in[i];
        ++inputFifoWrite;

        if (inputFifoWrite >= kRNNFrameSize) {
            float rnnIn[kRNNFrameSize];
            float rnnOut[kRNNFrameSize];

            for (int j = 0; j < kRNNFrameSize; ++j)
                rnnIn[j] = inputFifo[static_cast<size_t>(j)] * kScale;

            float vad = rnnoise_process_frame(rnn, rnnOut, rnnIn);

            float targetGate = (vad >= threshold) ? 1.0f : 0.0f;
            for (int j = 0; j < kRNNFrameSize; ++j) {
                gateGain = kGateSmooth * gateGain + (1.0f - kGateSmooth) * targetGate;
                outputFifo[static_cast<size_t>(outputFifoWrite % kFifoCapacity)] = rnnOut[j] * kInvScale * gateGain;
                ++outputFifoWrite;
            }

            inputFifoWrite = 0;
        }
    }

    // ══ PASS 2: Write output from ring buffer ══
    for (int i = 0; i < numSamples; ++i) {
        if (outputFifoRead < outputFifoWrite) {
            out[i] = outputFifo[static_cast<size_t>(outputFifoRead % kFifoCapacity)];
            ++outputFifoRead;
        } else {
            out[i] = 0.0f;  // latency fill period
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
        case 0:  threshold = 0.50f; break;  // Light (was 0.35)
        case 2:  threshold = 0.90f; break;  // Aggressive (was 0.85)
        default: threshold = 0.70f; break;  // Standard (was 0.60)
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
