// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "BuiltinAutoGain.h"
#include "../UI/AGCEditPanel.h"
#include <cmath>

namespace directpipe {

juce::AudioProcessorEditor* BuiltinAutoGain::createEditor()
{
    return new AGCEditPanel(*this);
}

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
    lufsWindowSize_ = static_cast<int>(sampleRate * 1.5);  // 1.5s window (was 3s) -- faster response
    lufsRingBuf_.assign(static_cast<size_t>(lufsWindowSize_), 0.0f);
    lufsWritePos_ = 0;
    lufsSampleCount_ = 0;
    runningSquareSum_.store(0.0, std::memory_order_relaxed);  // I1: reset running sum

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
    // attack = 500ms, release = 300ms -- tight leveling
    if (sampleRate > 0.0) {
        attackCoeff_  = 1.0f - std::exp(-1.0f / (0.5f * static_cast<float>(sampleRate)));   // 500ms boost
        releaseCoeff_ = 1.0f - std::exp(-1.0f / (0.7f * static_cast<float>(sampleRate)));  // 700ms cut
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

    // Incremental ring buffer update with running sum.
    //
    // Instead of summing the entire ring buffer (O(windowSize) = ~72k samples at 48kHz),
    // we maintain a running sum and update it incrementally:
    //   runningSquareSum_ -= old_value_being_overwritten
    //   runningSquareSum_ += new_value
    //
    // This reduces per-block cost from O(windowSize) to O(blockSize), which is critical
    // for RT audio thread performance (blockSize is typically 128-512 samples).
    double sum = runningSquareSum_.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i) {
        float sq = kL[i] * kL[i];
        if (kR != nullptr)
            sq = (sq + kR[i] * kR[i]) * 0.5f;  // average L+R squared for stereo

        double newVal = static_cast<double>(sq);
        // Subtract the OLD value that's about to be overwritten from the running sum
        double oldVal = static_cast<double>(lufsRingBuf_[static_cast<size_t>(lufsWritePos_)]);
        sum -= oldVal;
        sum += newVal;

        lufsRingBuf_[static_cast<size_t>(lufsWritePos_)] = sq;
        lufsWritePos_++;
        if (lufsWritePos_ >= lufsWindowSize_)
            lufsWritePos_ = 0;
        if (lufsSampleCount_ < lufsWindowSize_)
            lufsSampleCount_++;
    }
    runningSquareSum_.store(std::max(sum, 0.0), std::memory_order_relaxed);

    // Step 4: Calculate mean square over window -> LUFS
    float measuredLUFS = -60.0f;

    if (lufsSampleCount_ > 0) {
        // Use the incrementally maintained running sum (see Step 3 above).
        // NOTE: Already clamped to 0.0 in the store above, but defensive here too.
        double safeSum = runningSquareSum_.load(std::memory_order_relaxed);
        double meanSquare = safeSum / static_cast<double>(lufsSampleCount_);

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

    const float freeze  = freezeLevel_.load(std::memory_order_relaxed);

    // ═══ Mode 2 AGC: always aim for target, Correction % = response speed ═══
    //
    // IMPORTANT: Correction % does NOT scale the gain amount. It scales the
    // envelope follower coefficient (see Step 6 below). This means:
    //   - 100% correction: full-speed convergence to target
    //   - 50% correction: half-speed convergence (gentler, less aggressive)
    //   - 0% correction: frozen (no gain change at all)
    //   - 200% correction: 2x speed (very aggressive, may cause audible pumping)
    //
    // Low Correct %  → controls boost speed (higher = faster response to quiet parts)
    // High Correct % → controls cut speed (higher = faster response to loud parts)
    // Both reach target eventually -- percentage affects how quickly, not how much.

    float correction = target - measuredLUFS;  // full correction to reach target
    float gaindB;

    if (correction > 0.0f) {
        // Boosting (quiet) -- Linear Low (Mode 2) + Freeze Gate
        //
        // NOTE: The Freeze Gate uses per-block RMS (instant reaction, ~2.7ms at 128 samples)
        // instead of the 1.5s LUFS window for the freeze decision. This is critical because:
        //   - A keyboard click or breath is only a few ms long
        //   - The 1.5s LUFS window barely registers it (averaged over 72k samples)
        //   - Without instant freeze, AGC would boost the click by up to +24dB
        //   - Per-block RMS catches it within ONE buffer period and freezes the gain
        float blockRMS = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i) {
                float s = buffer.getSample(ch, i);
                blockRMS += s * s;
            }
        blockRMS = (numSamples * numChannels > 0)
            ? std::sqrt(blockRMS / static_cast<float>(numSamples * numChannels))
            : 0.0f;
        float blockdBFS = (blockRMS > 1.0e-7f)
            ? 20.0f * std::log10(blockRMS)
            : -120.0f;

        if (freeze > -65.0f && blockdBFS < freeze) {
            // Block level below freeze threshold -- don't boost (silence/noise)
            gaindB = 0.0f;
        } else {
            gaindB = correction;
        }
    } else {
        // Cutting (loud) -- direct correction to bring back to target
        gaindB = correction;  // full correction (negative = cutting)
    }

    gaindB = juce::jlimit(-maxGain, maxGain, gaindB);

    // Step 6: Smooth gain with envelope follower (per-sample in block)
    float targetGainLinear = std::pow(10.0f, gaindB / 20.0f);

    // Apply per-sample smoothing to currentGainLinear_.
    // Per-sample loop with direction-dependent coefficient selection.
    float gain = currentGainLinear_;

    // Per-sample IIR smoothing -- Correction % scales the envelope speed.
    //
    // For each sample, we select direction-dependent coefficients:
    //   - targetGainLinear > gain: we're boosting → use attackCoeff_ * lowCorr
    //   - targetGainLinear < gain: we're cutting  → use releaseCoeff_ * hiCorr
    //
    // The correction factor (lowCorr/hiCorr) multiplies the base coefficient,
    // so it controls convergence SPEED, not final gain AMOUNT.
    // At lowCorr=1.0, we get the full 500ms attack time constant.
    // At lowCorr=0.5, the effective time constant doubles to ~1000ms.
    for (int i = 0; i < numSamples; ++i) {
        float baseCoeff = (targetGainLinear > gain) ? attackCoeff_ : releaseCoeff_;
        float speedScale = (targetGainLinear > gain) ? lowCorr : hiCorr;
        float c = baseCoeff * speedScale;
        gain += c * (targetGainLinear - gain);
    }

    // Step 7: Apply gain to ORIGINAL buffer (not K-weighted)
    //
    // IMPORTANT: We apply gain as a LINEAR RAMP from startGain to endGain across
    // the block. Without this ramp, a sudden gain jump between blocks causes an
    // audible click/pop (discontinuity in the waveform). The ramp interpolates
    // smoothly across all samples in the block, eliminating clicks.
    //
    // If the gain change is negligible (<1e-6), we skip the ramp and apply
    // constant gain for efficiency (FloatVectorOperations::multiply is SIMD-optimized).
    const float startGain = currentGainLinear_;
    const float endGain = gain;

    if (std::abs(startGain - endGain) < 1.0e-6f) {
        // Constant gain -- no ramp needed, use SIMD-optimized multiply
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
//
// K-weighting is defined by ITU-R BS.1770-4 for LUFS loudness measurement.
// It models human hearing sensitivity with two filter stages:
//   Stage 1: High shelf filter (+4dB above ~1681Hz) -- models head diffraction
//   Stage 2: High-pass filter (38Hz cutoff) -- removes inaudible low frequencies
//
// The ITU standard provides EXACT biquad coefficients only for 48kHz.
// For other sample rates, we approximate using JUCE's IIR coefficient designers.
// This approximation is close enough for real-time AGC purposes but may not
// pass strict EBU R128 compliance testing at non-48kHz rates.

void BuiltinAutoGain::updateKWeightingCoeffs()
{
    // ITU-R BS.1770-4 K-weighting: two biquad stages.
    // Pre-computed coefficients for 48 kHz (exact from the standard).
    // For other sample rates, we use JUCE's IIR designer as approximation.

    if (currentSR_ <= 0.0)
        return;

    if (std::abs(currentSR_ - 48000.0) < 1.0) {
        // Exact 48 kHz coefficients from ITU-R BS.1770-4.
        // These are the ONLY officially specified coefficients in the standard.
        // Format: [b0, b1, b2, a1, a2] (a0 is implicitly 1.0)

        // Stage 1: High shelf (+4 dB at ~1681 Hz) -- models head/ear diffraction
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
        // NOTE: These are approximations -- the ITU standard only defines exact
        // coefficients for 48kHz. The approximation is accurate enough for AGC
        // but may differ from reference implementations by ~0.1-0.3 LUFS.
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

void BuiltinAutoGain::setFreezeLevel(float lufs)
{
    freezeLevel_.store(juce::jlimit(-60.0f, 0.0f, lufs), std::memory_order_relaxed);
}

// ─── State persistence (JSON) ──────────────────────────────────

void BuiltinAutoGain::getStateInformation(juce::MemoryBlock& destData)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("targetLUFS", static_cast<double>(getTargetLUFS()));
    obj->setProperty("lowCorrect", static_cast<double>(getLowCorrect()));
    obj->setProperty("highCorrect", static_cast<double>(getHighCorrect()));
    obj->setProperty("maxGaindB", static_cast<double>(getMaxGaindB()));
    obj->setProperty("freezeLevel", static_cast<double>(getFreezeLevel()));

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
        if (obj->hasProperty("freezeLevel"))
            setFreezeLevel(static_cast<float>(static_cast<double>(obj->getProperty("freezeLevel"))));
    }
}

} // namespace directpipe
