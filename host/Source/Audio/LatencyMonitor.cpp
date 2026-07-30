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

// RT 오디오 스레드에서 atomic 연산이 lock-free여야 함 (mutex 사용 시 glitch)
static_assert(std::atomic<double>::is_always_lock_free,
    "std::atomic<double> must be lock-free for RT audio thread safety");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
    "std::atomic<uint64_t> must be lock-free for RT audio thread safety");
#include <chrono>

namespace directpipe {

void LatencyMonitor::reset(double sampleRate, int bufferSize,
                           int inputLatencySamples, int outputLatencySamples)
{
    double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;
    int bs = (bufferSize > 0) ? bufferSize : 128;
    const int inputSamples = inputLatencySamples > 0 ? inputLatencySamples : bs;
    const int outputSamples = outputLatencySamples > 0 ? outputLatencySamples : bs;
    sampleRate_.store(sr, std::memory_order_relaxed);
    bufferSize_.store(bs, std::memory_order_relaxed);

    // Prefer the device's own latency report. Some drivers return zero when
    // unavailable, so each direction independently falls back to one buffer.
    inputLatencySamples_.store(inputSamples, std::memory_order_relaxed);
    outputLatencySamples_.store(outputSamples, std::memory_order_relaxed);
    inputLatencyMs_.store(
        (static_cast<double>(inputSamples) / sr) * 1000.0,
        std::memory_order_relaxed);
    outputLatencyMs_.store(
        (static_cast<double>(outputSamples) / sr) * 1000.0,
        std::memory_order_relaxed);
    processingTimeMs_.store(0.0, std::memory_order_relaxed);
    cpuUsage_.store(0.0, std::memory_order_relaxed);
    avgProcessingTime_.store(0.0, std::memory_order_relaxed);
    callbackStartTicks_.store(0, std::memory_order_relaxed);
    callbackOverruns_.store(0, std::memory_order_relaxed);
}

void LatencyMonitor::markCallbackStart()
{
    auto now = std::chrono::steady_clock::now();
    callbackStartTicks_.store(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count()), std::memory_order_relaxed);
}

void LatencyMonitor::markCallbackEnd()
{
    auto now = std::chrono::steady_clock::now();
    uint64_t endTicks = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count());

    const uint64_t startTicks =
        callbackStartTicks_.load(std::memory_order_relaxed);
    // A missing start marker or a non-monotonic/corrupt sample must not turn an
    // unsigned subtraction into a huge false latency/CPU/XRun spike.
    if (startTicks == 0 || endTicks < startTicks)
        return;

    // Calculate callback execution time for diagnostics only.
    double processingNs = static_cast<double>(endTicks - startTicks);
    double processingMs = processingNs / 1000000.0;

    // Exponential moving average for smooth display
    double avg = avgProcessingTime_.load(std::memory_order_relaxed);
    avg = avg * (1.0 - kSmoothingFactor) + processingMs * kSmoothingFactor;
    avgProcessingTime_.store(avg, std::memory_order_relaxed);
    processingTimeMs_.store(avg, std::memory_order_relaxed);

    // Calculate CPU usage: processing time / callback period
    double callbackPeriodMs = (static_cast<double>(bufferSize_.load(std::memory_order_relaxed))
                               / sampleRate_.load(std::memory_order_relaxed)) * 1000.0;
    if (callbackPeriodMs > 0.0) {
        double usage = (avg / callbackPeriodMs) * 100.0;
        cpuUsage_.store(usage, std::memory_order_relaxed);

        // Callback overrun detection: if THIS callback (not the average) took longer
        // than the buffer period, the audio hardware ran out of data — guaranteed glitch.
        // Use raw processingMs, not the smoothed average, for instant detection.
        if (processingMs > callbackPeriodMs)
            callbackOverruns_.fetch_add(1, std::memory_order_relaxed);
    }
}

double LatencyMonitor::getTotalLatencyOBSMs() const
{
    // DirectPipe-side shared-memory path stops before Receiver/host buffering.
    return inputLatencyMs_.load(std::memory_order_relaxed);
}

double LatencyMonitor::getTotalLatencyVirtualMicMs() const
{
    // Main device path: driver-reported input + output latency. Callback
    // execution is part of the real-time budget, not an algorithmic sample delay.
    return inputLatencyMs_.load(std::memory_order_relaxed) +
           outputLatencyMs_.load(std::memory_order_relaxed);
}

} // namespace directpipe
