// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "BuiltinAutoGain.h"
#include <cmath>

namespace directpipe {

// ─── Construction ──────────────────────────────────────────────

BuiltinAutoGain::BuiltinAutoGain()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

// ─── AudioProcessor lifecycle ──────────────────────────────────

void BuiltinAutoGain::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSR_ = sampleRate;

    // Pre-allocate LUFS ring buffer (3-second window)
    lufsWindowSize_ = static_cast<int>(sampleRate * 3.0);
    lufsRingBuf_.assign(static_cast<size_t>(lufsWindowSize_), 0.0f);
    lufsWritePos_ = 0;
    lufsSampleCount_ = 0;

    // Pre-allocate scratch buffer for K-weighting measurement
    kWeightScratch_.setSize(2, samplesPerBlock);

    // Reset K-weighting filters
    kStage1L_.reset();
    kStage1R_.reset();
    kStage2L_.reset();
    kStage2R_.reset();
    updateKWeightingCoeffs();

    // Reset gain state
    currentGainLinear_ = 1.0f;

    // Envelope follower coefficients:
    //   coeff = 1 - exp(-1 / (time_seconds * sampleRate))
    // We compute per-sample coefficients from time constants.
    // attack = 2s, release = 3s
    if (sampleRate > 0.0) {
        attackCoeff_  = 1.0f - std::exp(-1.0f / (2.0f * static_cast<float>(sampleRate)));
        releaseCoeff_ = 1.0f - std::exp(-1.0f / (3.0f * static_cast<float>(sampleRate)));
    }

    // Reset UI feedback
    currentLUFS_.store(-60.0f, std::memory_order_relaxed);
    currentGaindB_.store(0.0f, std::memory_order_relaxed);
}

void BuiltinAutoGain::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Ensure scratch buffer is large enough (should be, from prepareToPlay)
    if (kWeightScratch_.getNumSamples() < numSamples)
        return;  // Safety: skip if scratch is too small (should not happen)

    // Step 1: Copy buffer to scratch for K-weighting measurement
    for (int ch = 0; ch < juce::jmin(numChannels, 2); ++ch)
        juce::FloatVectorOperations::copy(
            kWeightScratch_.getWritePointer(ch),
            buffer.getReadPointer(ch),
            numSamples);

    // Step 2: Apply K-weighting to scratch copy (sidechain -- not to original audio)
    // Stage 1: High shelf
    if (numChannels > 0)
        kStage1L_.processSamples(kWeightScratch_.getWritePointer(0), numSamples);
    if (numChannels > 1)
        kStage1R_.processSamples(kWeightScratch_.getWritePointer(1), numSamples);

    // Stage 2: HPF
    if (numChannels > 0)
        kStage2L_.processSamples(kWeightScratch_.getWritePointer(0), numSamples);
    if (numChannels > 1)
        kStage2R_.processSamples(kWeightScratch_.getWritePointer(1), numSamples);

    // Step 3: Compute mean squared value and store in ring buffer
    // For stereo, average both channels; for mono, use single channel
    const float* kL = kWeightScratch_.getReadPointer(0);
    const float* kR = (numChannels > 1) ? kWeightScratch_.getReadPointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float sq = kL[i] * kL[i];
        if (kR != nullptr)
            sq = (sq + kR[i] * kR[i]) * 0.5f;  // average L+R squared

        lufsRingBuf_[static_cast<size_t>(lufsWritePos_)] = sq;
        lufsWritePos_++;
        if (lufsWritePos_ >= lufsWindowSize_)
            lufsWritePos_ = 0;
        if (lufsSampleCount_ < lufsWindowSize_)
            lufsSampleCount_++;
    }

    // Step 4: Calculate mean square over window -> LUFS
    float measuredLUFS = -60.0f;

    if (lufsSampleCount_ > 0) {
        // Sum all valid samples in ring buffer
        double sum = 0.0;
        const int count = lufsSampleCount_;
        const float* ringData = lufsRingBuf_.data();
        for (int i = 0; i < count; ++i)
            sum += static_cast<double>(ringData[i]);

        double meanSquare = sum / static_cast<double>(count);

        // LUFS = -0.691 + 10 * log10(meanSquare)
        // Clamp meanSquare to avoid log10(0)
        constexpr double kMinMeanSquare = 1.0e-12;  // ~ -120 dBFS
        if (meanSquare > kMinMeanSquare)
            measuredLUFS = static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
        else
            measuredLUFS = -60.0f;
    }

    // Step 5: Asymmetric correction
    const float target  = targetLUFS_.load(std::memory_order_relaxed);
    const float lowCorr = lowCorrect_.load(std::memory_order_relaxed);
    const float hiCorr  = highCorrect_.load(std::memory_order_relaxed);
    const float maxGain = maxGaindB_.load(std::memory_order_relaxed);

    float correction = target - measuredLUFS;
    float gaindB;
    if (correction > 0.0f)
        gaindB = correction * lowCorr;   // boosting (quieter than target)
    else
        gaindB = correction * hiCorr;    // cutting (louder than target)

    gaindB = juce::jlimit(-maxGain, maxGain, gaindB);

    // Step 6: Smooth gain with envelope follower (per-sample in block)
    float targetGainLinear = std::pow(10.0f, gaindB / 20.0f);

    // Apply per-sample smoothing to currentGainLinear_.
    // Per-sample loop with direction-dependent coefficient selection.
    float gain = currentGainLinear_;

    // Per-sample smoothing across the block
    for (int i = 0; i < numSamples; ++i) {
        const float c = (targetGainLinear > gain) ? attackCoeff_ : releaseCoeff_;
        gain += c * (targetGainLinear - gain);
    }

    // Step 7: Apply gain to ORIGINAL buffer (not K-weighted)
    // Use a linear ramp from old gain to new gain for click-free operation
    const float startGain = currentGainLinear_;
    const float endGain = gain;

    if (std::abs(startGain - endGain) < 1.0e-6f) {
        // Constant gain -- no ramp needed
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply(
                buffer.getWritePointer(ch), endGain, numSamples);
    } else {
        // Linear ramp
        const float gainStep = (endGain - startGain) / static_cast<float>(numSamples);
        for (int ch = 0; ch < numChannels; ++ch) {
            float* data = buffer.getWritePointer(ch);
            float g = startGain;
            for (int i = 0; i < numSamples; ++i) {
                data[i] *= g;
                g += gainStep;
            }
        }
    }

    currentGainLinear_ = endGain;

    // Step 8: Update UI feedback atomics
    currentLUFS_.store(measuredLUFS, std::memory_order_relaxed);
    currentGaindB_.store(
        20.0f * std::log10(juce::jmax(endGain, 1.0e-6f)),
        std::memory_order_relaxed);
}

// ─── K-weighting coefficients ──────────────────────────────────

void BuiltinAutoGain::updateKWeightingCoeffs()
{
    // ITU-R BS.1770-4 K-weighting: two biquad stages.
    // Pre-computed coefficients for 48 kHz.
    // For other sample rates, we use JUCE's IIR designer as approximation.

    if (currentSR_ <= 0.0)
        return;

    if (std::abs(currentSR_ - 48000.0) < 1.0) {
        // Exact 48 kHz coefficients from ITU-R BS.1770-4

        // Stage 1: High shelf (+4 dB at ~1681 Hz)
        juce::IIRCoefficients stage1;
        stage1.coefficients[0] = 1.53512485958697f;
        stage1.coefficients[1] = -2.69169618940638f;
        stage1.coefficients[2] = 1.19839281085285f;
        stage1.coefficients[3] = -1.69065929318241f;
        stage1.coefficients[4] = 0.73248077421585f;
        kStage1L_.setCoefficients(stage1);
        kStage1R_.setCoefficients(stage1);

        // Stage 2: High-pass filter (38 Hz, 2nd order)
        juce::IIRCoefficients stage2;
        stage2.coefficients[0] = 1.0f;
        stage2.coefficients[1] = -2.0f;
        stage2.coefficients[2] = 1.0f;
        stage2.coefficients[3] = -1.99004745483398f;
        stage2.coefficients[4] = 0.99007225036621f;
        kStage2L_.setCoefficients(stage2);
        kStage2R_.setCoefficients(stage2);
    } else {
        // Approximate K-weighting for non-48kHz rates using JUCE's built-in designers.
        // Stage 1: High shelf approximation (+4 dB, 1681 Hz)
        auto stage1 = juce::IIRCoefficients::makeHighShelf(
            currentSR_, 1681.0, 0.7071, // Q ~ sqrt(2)/2
            juce::Decibels::decibelsToGain(4.0f));
        kStage1L_.setCoefficients(stage1);
        kStage1R_.setCoefficients(stage1);

        // Stage 2: HPF at 38 Hz (2nd order Butterworth)
        auto stage2 = juce::IIRCoefficients::makeHighPass(currentSR_, 38.0);
        kStage2L_.setCoefficients(stage2);
        kStage2R_.setCoefficients(stage2);
    }
}

// ─── Parameter setters ─────────────────────────────────────────

void BuiltinAutoGain::setTargetLUFS(float lufs)
{
    targetLUFS_.store(juce::jlimit(-40.0f, 0.0f, lufs), std::memory_order_relaxed);
}

void BuiltinAutoGain::setLowCorrect(float factor)
{
    lowCorrect_.store(juce::jlimit(0.0f, 2.0f, factor), std::memory_order_relaxed);
}

void BuiltinAutoGain::setHighCorrect(float factor)
{
    highCorrect_.store(juce::jlimit(0.0f, 2.0f, factor), std::memory_order_relaxed);
}

void BuiltinAutoGain::setMaxGaindB(float dB)
{
    maxGaindB_.store(juce::jlimit(0.0f, 40.0f, dB), std::memory_order_relaxed);
}

// ─── State persistence (JSON) ──────────────────────────────────

void BuiltinAutoGain::getStateInformation(juce::MemoryBlock& destData)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("targetLUFS", static_cast<double>(getTargetLUFS()));
    obj->setProperty("lowCorrect", static_cast<double>(getLowCorrect()));
    obj->setProperty("highCorrect", static_cast<double>(getHighCorrect()));
    obj->setProperty("maxGaindB", static_cast<double>(getMaxGaindB()));

    auto json = juce::JSON::toString(juce::var(obj.release()));
    destData.replaceWith(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BuiltinAutoGain::setStateInformation(const void* data, int sizeInBytes)
{
    auto json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    auto parsed = juce::JSON::parse(json);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("targetLUFS"))
            setTargetLUFS(static_cast<float>(static_cast<double>(obj->getProperty("targetLUFS"))));
        if (obj->hasProperty("lowCorrect"))
            setLowCorrect(static_cast<float>(static_cast<double>(obj->getProperty("lowCorrect"))));
        if (obj->hasProperty("highCorrect"))
            setHighCorrect(static_cast<float>(static_cast<double>(obj->getProperty("highCorrect"))));
        if (obj->hasProperty("maxGaindB"))
            setMaxGaindB(static_cast<float>(static_cast<double>(obj->getProperty("maxGaindB"))));
    }
}

} // namespace directpipe
