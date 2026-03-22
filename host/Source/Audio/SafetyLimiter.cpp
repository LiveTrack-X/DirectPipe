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
 * @brief Global Safety Guard implementation (legacy class name retained)
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

    releaseCoeff_ = std::exp(-1.0f / static_cast<float>(sampleRate * kReleaseMs * 0.001));

    resetState();
    resetRequested_.store(false, std::memory_order_relaxed);
}

void SafetyLimiter::resetState()
{
    currentGain_ = 1.0f;

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
        float framePeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            const float in = buffer.getSample(ch, i);
            framePeak = std::max(framePeak, std::abs(in));
        }

        float targetGain = 1.0f;
        if (framePeak > ceiling && framePeak > 1.0e-9f)
            targetGain = ceiling / framePeak;

        // Zero-latency safety guard: instant attack, smooth release.
        if (targetGain < currentGain_)
            currentGain_ = targetGain;
        else
            currentGain_ = rCoeff * currentGain_ + (1.0f - rCoeff) * targetGain;

        for (int ch = 0; ch < processChannels; ++ch) {
            float out = buffer.getSample(ch, i) * currentGain_;
            // Fail-safe: enforce absolute sample ceiling.
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
