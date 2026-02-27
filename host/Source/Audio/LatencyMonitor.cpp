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
 * @file LatencyMonitor.cpp
 * @brief Real-time latency measurement implementation
 */

#include "LatencyMonitor.h"
#include <chrono>

namespace directpipe {

void LatencyMonitor::reset(double sampleRate, int bufferSize)
{
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    // Calculate buffer latencies
    double bufferMs = (static_cast<double>(bufferSize) / sampleRate) * 1000.0;
    inputLatencyMs_.store(bufferMs, std::memory_order_relaxed);
    outputLatencyMs_.store(bufferMs, std::memory_order_relaxed);
    processingTimeMs_.store(0.0, std::memory_order_relaxed);
    cpuUsage_.store(0.0, std::memory_order_relaxed);
    avgProcessingTime_ = 0.0;
}

void LatencyMonitor::markCallbackStart()
{
    auto now = std::chrono::high_resolution_clock::now();
    callbackStartTicks_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count());
}

void LatencyMonitor::markCallbackEnd()
{
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t endTicks = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count());

    // Calculate processing time
    double processingNs = static_cast<double>(endTicks - callbackStartTicks_);
    double processingMs = processingNs / 1000000.0;

    // Exponential moving average for smooth display
    avgProcessingTime_ = avgProcessingTime_ * (1.0 - kSmoothingFactor) +
                          processingMs * kSmoothingFactor;
    processingTimeMs_.store(avgProcessingTime_, std::memory_order_relaxed);

    // Calculate CPU usage: processing time / callback period
    double callbackPeriodMs = (static_cast<double>(bufferSize_) / sampleRate_) * 1000.0;
    if (callbackPeriodMs > 0.0) {
        double usage = (avgProcessingTime_ / callbackPeriodMs) * 100.0;
        cpuUsage_.store(usage, std::memory_order_relaxed);
    }
}

double LatencyMonitor::getTotalLatencyOBSMs() const
{
    // OBS path: Input buffer + Processing + Shared memory (negligible)
    return inputLatencyMs_.load(std::memory_order_relaxed) +
           processingTimeMs_.load(std::memory_order_relaxed);
}

double LatencyMonitor::getTotalLatencyVirtualMicMs() const
{
    // Virtual mic path: Input buffer + Processing + Output buffer (WASAPI)
    return inputLatencyMs_.load(std::memory_order_relaxed) +
           processingTimeMs_.load(std::memory_order_relaxed) +
           outputLatencyMs_.load(std::memory_order_relaxed);
}

} // namespace directpipe
