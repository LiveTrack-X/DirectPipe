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
 * @file LatencyMonitor.h
 * @brief Audio-path latency estimation and callback timing diagnostics
 *
 * Tracks the input/output latency reported by the current audio driver, with a
 * one-buffer fallback for either direction when the driver cannot report it.
 * Callback execution time is measured separately for CPU/XRun diagnostics and
 * is not treated as additional sample-path latency.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace directpipe {

/**
 * @brief Estimates device-path latency and reports callback timing.
 *
 * Uses a monotonic clock in the audio callback to measure actual processing
 * time. Device latency remains a software estimate based on driver-reported
 * samples (or a one-buffer fallback), not a hardware loopback measurement.
 */
class LatencyMonitor {
public:
    LatencyMonitor() = default;

    /**
     * @brief Reset the monitor with new audio parameters.
     * @param sampleRate Audio sample rate in Hz.
     * @param bufferSize Audio buffer size in samples.
     * @param inputLatencySamples Driver-reported input latency, or <= 0 to
     *        fall back to one buffer.
     * @param outputLatencySamples Driver-reported output latency, or <= 0 to
     *        fall back to one buffer.
     */
    void reset(double sampleRate, int bufferSize,
               int inputLatencySamples = 0,
               int outputLatencySamples = 0);

    /**
     * @brief Mark the start of an audio callback (called from RT thread).
     */
    void markCallbackStart();

    /**
     * @brief Mark the end of an audio callback (called from RT thread).
     */
    void markCallbackEnd();

    /**
     * @brief Get the estimated input-device latency in milliseconds.
     */
    double getInputLatencyMs() const { return inputLatencyMs_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the input-device latency used by the estimate, in samples.
     */
    int getInputLatencySamples() const { return inputLatencySamples_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the measured callback execution time in milliseconds.
     */
    double getProcessingTimeMs() const { return processingTimeMs_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the estimated output-device latency in milliseconds.
     */
    double getOutputLatencyMs() const { return outputLatencyMs_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the output-device latency used by the estimate, in samples.
     */
    int getOutputLatencySamples() const { return outputLatencySamples_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the estimated input-device latency before shared-memory output.
     *
     * Receiver/host buffering and active plug-in PDC are intentionally separate.
     */
    double getTotalLatencyOBSMs() const;

    /**
     * @brief Get the estimated main device I/O latency.
     *
     * Active plug-in PDC is intentionally added by the caller.
     */
    double getTotalLatencyVirtualMicMs() const;

    /**
     * @brief Get the current CPU usage percentage for audio processing.
     */
    double getCpuUsagePercent() const { return cpuUsage_.load(std::memory_order_relaxed); }

    /**
     * @brief Get current sample rate.
     */
    double getSampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }

    /**
     * @brief Get current buffer size.
     */
    int getBufferSize() const { return bufferSize_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the number of callback overruns detected since last reset.
     * A callback overrun means the processing time exceeded the buffer period,
     * which guarantees an audio glitch (the hardware ran out of data to play).
     */
    uint32_t getCallbackOverrunCount() const { return callbackOverruns_.load(std::memory_order_relaxed); }

    /**
     * @brief Reset the callback overrun counter.
     */
    void resetCallbackOverruns() { callbackOverruns_.store(0, std::memory_order_relaxed); }

private:
    std::atomic<double> sampleRate_{48000.0};       // [Message write, RT read]
    std::atomic<int> bufferSize_{128};               // [Message write, RT read]

    // Timing (updated from RT thread)
    std::atomic<uint64_t> callbackStartTicks_{0};    // [RT thread only, atomic for safety across reset()]
    std::atomic<int> inputLatencySamples_{128};
    std::atomic<int> outputLatencySamples_{128};
    std::atomic<double> inputLatencyMs_{0.0};
    std::atomic<double> processingTimeMs_{0.0};
    std::atomic<double> outputLatencyMs_{0.0};
    std::atomic<double> cpuUsage_{0.0};

    // Running average for smooth display
    std::atomic<double> avgProcessingTime_{0.0};     // [Message write (reset), RT read+write]
    static constexpr double kSmoothingFactor = 0.1;

    // Callback overrun detection: processing time > buffer period = guaranteed glitch
    std::atomic<uint32_t> callbackOverruns_{0};       // [RT write, Message read]
};

} // namespace directpipe
