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
 * @file OutputRouter.h
 * @brief Audio output routing to monitor (headphones)
 *
 * Routes processed audio to a separate WASAPI monitor device (headphones).
 * Main output goes through the AudioSettings Output device directly.
 */
#pragma once

#include <JuceHeader.h>
#include "MonitorOutput.h"
#include <atomic>

namespace directpipe {

/**
 * @brief Routes audio to multiple output destinations.
 *
 * Each output has independent volume control and enable/disable toggle.
 * Audio routing is performed in the real-time callback — no allocations.
 */
class OutputRouter {
public:
    /// Output destination identifiers
    enum class Output {
        Monitor = 0,       ///< Local monitoring (headphones, separate WASAPI device)
        Count
    };

    OutputRouter();
    ~OutputRouter();

    void initialize(double sampleRate, int bufferSize);
    void shutdown();

    /**
     * @brief Route processed audio to all enabled outputs.
     * Called from the real-time audio thread. No allocations.
     */
    void routeAudio(const juce::AudioBuffer<float>& buffer, int numSamples);

    void setVolume(Output output, float volume);
    float getVolume(Output output) const;
    void setEnabled(Output output, bool enabled);
    bool isEnabled(Output output) const;
    float getLevel(Output output) const;

    /** Wire the monitor output (non-owning pointer, separate WASAPI device). */
    void setMonitorOutput(MonitorOutput* mo) { monitorOutput_ = mo; }

    /** Check if monitor output is active and receiving audio. */
    bool isMonitorOutputActive() const;

private:
    static constexpr int kOutputCount = static_cast<int>(Output::Count);

    struct OutputState {
        std::atomic<float> volume{1.0f};
        std::atomic<bool> enabled{true};
        std::atomic<float> level{0.0f};
    };

    OutputState outputs_[kOutputCount];

    MonitorOutput* monitorOutput_ = nullptr;

    // Temporary buffer for volume-scaled output (pre-allocated)
    juce::AudioBuffer<float> scaledBuffer_;

    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputRouter)
};

} // namespace directpipe
