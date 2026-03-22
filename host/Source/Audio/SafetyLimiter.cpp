// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file SafetyLimiter.cpp
 * @brief Brickwall-style safety limiter implementation
 */

#include "SafetyLimiter.h"

namespace directpipe {

SafetyLimiter::SafetyLimiter()
{
    // Initialize coefficients/state with 48kHz defaults.
    prepareToPlay(48000.0);
}

void SafetyLimiter::prepareToPlay(double sampleRate)
{
    if (sampleRate <= 0.0) sampleRate = 48000.0;

    lookAheadSamples_ = juce::jlimit(
        1,
        kMaxLookAheadSamples,
        juce::roundToInt(static_cast<float>(sampleRate) * (kLookAheadMs * 0.001f)));
    delaySize_ = lookAheadSamples_ + 1;

    releaseCoeff_ = std::exp(-1.0f / static_cast<float>(sampleRate * kReleaseMs * 0.001));

    resetState();
    resetRequested_.store(false, std::memory_order_relaxed);
}

void SafetyLimiter::resetState()
{
    currentGain_ = 1.0f;
    writePos_ = 0;

    for (int ch = 0; ch < kMaxChannels; ++ch)
        std::fill_n(delayedSamples_[ch].begin(), delaySize_, 0.0f);
    std::fill_n(peakRing_.begin(), delaySize_, 0.0f);

    gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
}

void SafetyLimiter::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0) {
        gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    if (resetRequested_.exchange(false, std::memory_order_relaxed))
        resetState();

    if (!enabled_.load(std::memory_order_relaxed)) {
        gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float ceiling = ceilingLinear_.load(std::memory_order_relaxed);
    const int numChannels = buffer.getNumChannels();
    const int processChannels = std::min(numChannels, kMaxChannels);
    const float rCoeff = releaseCoeff_;

    float minGain = 1.0f;

    for (int i = 0; i < numSamples; ++i) {
        float inputPeak = 0.0f;

        // Capture current sample into delay ring and track full-channel peak.
        for (int ch = 0; ch < numChannels; ++ch) {
            const float in = buffer.getSample(ch, i);
            inputPeak = std::max(inputPeak, std::abs(in));
            if (ch < processChannels)
                delayedSamples_[ch][writePos_] = in;
        }
        peakRing_[writePos_] = inputPeak;

        // Read from the delayed position so gain can react ahead of the emitted sample.
        int readPos = writePos_ - lookAheadSamples_;
        if (readPos < 0)
            readPos += delaySize_;

        // Peak over look-ahead window [readPos .. writePos].
        float windowPeak = 0.0f;
        int idx = readPos;
        for (int k = 0; k <= lookAheadSamples_; ++k) {
            windowPeak = std::max(windowPeak, peakRing_[idx]);
            if (++idx >= delaySize_)
                idx = 0;
        }

        float targetGain = 1.0f;
        if (windowPeak > ceiling && windowPeak > 1.0e-9f)
            targetGain = ceiling / windowPeak;

        // Instant attack for brickwall safety, smooth release for recovery.
        if (targetGain < currentGain_)
            currentGain_ = targetGain;
        else
            currentGain_ = rCoeff * currentGain_ + (1.0f - rCoeff) * targetGain;

        for (int ch = 0; ch < processChannels; ++ch) {
            float out = delayedSamples_[ch][readPos] * currentGain_;
            // Fail-safe: enforce absolute sample ceiling even if envelope math under-shoots.
            out = juce::jlimit(-ceiling, ceiling, out);
            buffer.setSample(ch, i, out);
        }

        // Fallback for channels beyond internal fixed capacity.
        for (int ch = processChannels; ch < numChannels; ++ch) {
            float out = buffer.getSample(ch, i) * currentGain_;
            out = juce::jlimit(-ceiling, ceiling, out);
            buffer.setSample(ch, i, out);
        }

        if (currentGain_ < minGain)
            minGain = currentGain_;

        if (++writePos_ >= delaySize_)
            writePos_ = 0;
    }

    const float grDB = (minGain < 0.9999f)
        ? (20.0f * std::log10(std::max(minGain, 1.0e-9f)))
        : 0.0f;
    gainReduction_dB_.store(grDB, std::memory_order_relaxed);
}

void SafetyLimiter::setEnabled(bool enabled)
{
    const bool previous = enabled_.exchange(enabled, std::memory_order_relaxed);
    if (previous != enabled)
        resetRequested_.store(true, std::memory_order_relaxed);

    if (!enabled)
        gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
}

void SafetyLimiter::setCeiling(float dB)
{
    dB = juce::jlimit(-6.0f, 0.0f, dB);
    ceilingdB_.store(dB, std::memory_order_relaxed);
    ceilingLinear_.store(juce::Decibels::decibelsToGain(dB), std::memory_order_relaxed);
}

} // namespace directpipe
