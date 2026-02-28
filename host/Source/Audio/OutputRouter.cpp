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
 * @file OutputRouter.cpp
 * @brief Audio output routing implementation
 */

#include "OutputRouter.h"
#include <cmath>

namespace directpipe {

OutputRouter::OutputRouter()
{
    // Monitor: OFF by default (user enables explicitly in Output tab)
    outputs_[static_cast<int>(Output::Monitor)].enabled.store(false, std::memory_order_relaxed);
}

OutputRouter::~OutputRouter()
{
    shutdown();
}

void OutputRouter::initialize(double sampleRate, int bufferSize)
{
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    // Pre-allocate the scaled buffer (stereo)
    scaledBuffer_.setSize(2, bufferSize);
    scaledBuffer_.clear();
}

void OutputRouter::shutdown()
{
}

void OutputRouter::routeAudio(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    numSamples = juce::jmin(numSamples, scaledBuffer_.getNumSamples());
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    // Main output goes directly through the audio callback's outputChannelData.
    // OutputRouter only handles additional routing to separate WASAPI devices.

    // ── Monitor → Headphones (separate WASAPI device) ──
    if (outputs_[static_cast<int>(Output::Monitor)].enabled.load(std::memory_order_relaxed)
        && monitorOutput_ != nullptr)
    {
        float vol = outputs_[static_cast<int>(Output::Monitor)].volume.load(std::memory_order_relaxed);

        if (vol > 0.001f) {
            if (std::abs(vol - 1.0f) < 0.001f) {
                const float* channels[2] = {
                    buffer.getReadPointer(0),
                    numChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0)
                };
                monitorOutput_->writeAudio(channels, 2, numSamples);
            } else {
                for (int ch = 0; ch < numChannels; ++ch) {
                    scaledBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                    scaledBuffer_.applyGain(ch, 0, numSamples, vol);
                }
                for (int ch = numChannels; ch < 2; ++ch)
                    scaledBuffer_.clear(ch, 0, numSamples);

                const float* channels[2] = {
                    scaledBuffer_.getReadPointer(0),
                    scaledBuffer_.getReadPointer(1)
                };
                monitorOutput_->writeAudio(channels, 2, numSamples);
            }
        }

        if (numChannels > 0) {
            float rms = buffer.getRMSLevel(0, 0, numSamples) * vol;
            outputs_[static_cast<int>(Output::Monitor)].level.store(rms, std::memory_order_relaxed);
        }
    }
}

void OutputRouter::setVolume(Output output, float volume)
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        outputs_[idx].volume.store(juce::jlimit(0.0f, 1.0f, volume), std::memory_order_relaxed);
}

float OutputRouter::getVolume(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        return outputs_[idx].volume.load(std::memory_order_relaxed);
    return 0.0f;
}

void OutputRouter::setEnabled(Output output, bool enabled)
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        outputs_[idx].enabled.store(enabled, std::memory_order_relaxed);
}

bool OutputRouter::isEnabled(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        return outputs_[idx].enabled.load(std::memory_order_relaxed);
    return false;
}

float OutputRouter::getLevel(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        return outputs_[idx].level.load(std::memory_order_relaxed);
    return 0.0f;
}

bool OutputRouter::isMonitorOutputActive() const
{
    return monitorOutput_ != nullptr && monitorOutput_->isActive();
}

} // namespace directpipe
