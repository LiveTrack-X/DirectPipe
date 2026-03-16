// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
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
 * @brief Feed-forward safety limiter implementation
 */

#include "SafetyLimiter.h"

namespace directpipe {

SafetyLimiter::SafetyLimiter()
{
    prepareToPlay(48000.0);
}

void SafetyLimiter::prepareToPlay(double sampleRate)
{
    if (sampleRate <= 0.0) sampleRate = 48000.0;
    attackCoeff_  = std::exp(-1.0f / static_cast<float>(sampleRate * kAttackMs * 0.001));
    releaseCoeff_ = std::exp(-1.0f / static_cast<float>(sampleRate * kReleaseMs * 0.001));
    currentGain_ = 1.0f;
    gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
}

void SafetyLimiter::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!enabled_.load(std::memory_order_relaxed)) {
        gainReduction_dB_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float ceiling = ceilingLinear_.load(std::memory_order_relaxed);
    const int numChannels = buffer.getNumChannels();
    const float aCoeff = attackCoeff_;
    const float rCoeff = releaseCoeff_;

    float peakGR = 1.0f;

    for (int i = 0; i < numSamples; ++i) {
        // Peak detection across all channels
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            float s = std::abs(buffer.getSample(ch, i));
            if (s > peak) peak = s;
        }

        // Compute target gain
        float targetGain = (peak > ceiling) ? (ceiling / peak) : 1.0f;

        // Envelope follower
        if (targetGain < currentGain_)
            currentGain_ = aCoeff * currentGain_ + (1.0f - aCoeff) * targetGain;
        else
            currentGain_ = rCoeff * currentGain_ + (1.0f - rCoeff) * targetGain;

        // Apply gain to all channels
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, buffer.getSample(ch, i) * currentGain_);

        if (currentGain_ < peakGR) peakGR = currentGain_;
    }

    // Update UI feedback (worst GR in this block, converted to dB)
    float grDB = (peakGR < 0.9999f) ? (20.0f * std::log10(peakGR)) : 0.0f;
    gainReduction_dB_.store(grDB, std::memory_order_relaxed);
}

void SafetyLimiter::setEnabled(bool enabled)
{
    enabled_.store(enabled, std::memory_order_relaxed);
}

void SafetyLimiter::setCeiling(float dB)
{
    dB = juce::jlimit(-6.0f, 0.0f, dB);
    ceilingdB_.store(dB, std::memory_order_relaxed);
    ceilingLinear_.store(juce::Decibels::decibelsToGain(dB), std::memory_order_relaxed);
}

} // namespace directpipe
