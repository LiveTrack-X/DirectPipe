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
 * @file SafetyLimiter.h
 * @brief Brickwall-style safety limiter with look-ahead and hard ceiling clamp.
 *
 * Inserted after VST chain, before all output paths (Recording/IPC/Monitor/Main).
 * Prevents unexpected clipping from plugin parameter changes or preset switches.
 */
#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace directpipe {

/**
 * @brief RT-safe brickwall limiter with atomic parameter control.
 *
 * Thread Ownership (see Audio/README.md "Thread Model"):
 *   process()               [RT audio thread only]
 *   prepareToPlay()         [Message thread] (JUCE convention)
 *   setEnabled/setCeiling() [Any thread] (atomic writes)
 *   get* / isLimiting()     [Any thread] (atomic reads)
 */
class SafetyLimiter {
public:
    SafetyLimiter();

    /** Recalculate coefficients for new sample rate and reset look-ahead state. */
    void prepareToPlay(double sampleRate);

    /** Process audio buffer in-place. RT-safe: no alloc, no mutex, no logging. */
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    // Parameter setters (atomic, any thread)
    void setEnabled(bool enabled);
    void setCeiling(float dB);  // -6.0 ~ 0.0 dBFS

    // State getters (atomic, any thread)
    bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }
    float getCeilingdB() const { return ceilingdB_.load(std::memory_order_relaxed); }
    float getCurrentGainReduction() const { return gainReduction_dB_.load(std::memory_order_relaxed); }
    bool isLimiting() const { return gainReduction_dB_.load(std::memory_order_relaxed) < -0.1f; }

private:
    void resetState();

    std::atomic<bool> enabled_{true};
    std::atomic<bool> resetRequested_{true};
    std::atomic<float> ceilingLinear_{0.9661f};  // dBtoLinear(-0.3)
    std::atomic<float> ceilingdB_{-0.3f};

    // Envelope + delay state (RT audio thread only, non-atomic)
    float currentGain_ = 1.0f;
    float releaseCoeff_ = 0.0f;
    int lookAheadSamples_ = 1;
    int delaySize_ = 2;
    int writePos_ = 0;

    static constexpr int kMaxChannels = 64;
    // Brickwall safety window. This intentionally adds fixed output delay.
    static constexpr float kLookAheadMs = 2.0f;
    static constexpr float kReleaseMs = 50.0f;
    static constexpr int kMaxLookAheadSamples = 1024;
    static constexpr int kMaxDelayBufferSize = kMaxLookAheadSamples + 1;

    std::array<std::array<float, kMaxDelayBufferSize>, kMaxChannels> delayedSamples_{};
    std::array<float, kMaxDelayBufferSize> peakRing_{};

    // UI feedback [written by RT, read by UI]
    std::atomic<float> gainReduction_dB_{0.0f};
};

} // namespace directpipe
