/**
 * @file LatencyMonitor.h
 * @brief Real-time latency measurement and display
 *
 * Measures the total audio path latency including:
 * - Input buffer latency (WASAPI)
 * - VST processing time
 * - Output buffer latency
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace directpipe {

/**
 * @brief Measures and reports audio path latency.
 *
 * Uses high-resolution timestamps in the audio callback to measure
 * actual processing time. Combines this with known buffer latencies
 * to report total end-to-end latency.
 */
class LatencyMonitor {
public:
    LatencyMonitor() = default;

    /**
     * @brief Reset the monitor with new audio parameters.
     * @param sampleRate Audio sample rate in Hz.
     * @param bufferSize Audio buffer size in samples.
     */
    void reset(double sampleRate, int bufferSize);

    /**
     * @brief Mark the start of an audio callback (called from RT thread).
     */
    void markCallbackStart();

    /**
     * @brief Mark the end of an audio callback (called from RT thread).
     */
    void markCallbackEnd();

    /**
     * @brief Get the input buffer latency in milliseconds.
     */
    double getInputLatencyMs() const { return inputLatencyMs_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the VST processing time in milliseconds.
     */
    double getProcessingTimeMs() const { return processingTimeMs_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the output buffer latency in milliseconds.
     */
    double getOutputLatencyMs() const { return outputLatencyMs_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the total end-to-end latency for shared memory path (OBS).
     */
    double getTotalLatencyOBSMs() const;

    /**
     * @brief Get the total end-to-end latency for virtual mic path.
     */
    double getTotalLatencyVirtualMicMs() const;

    /**
     * @brief Get the current CPU usage percentage for audio processing.
     */
    double getCpuUsagePercent() const { return cpuUsage_.load(std::memory_order_relaxed); }

    /**
     * @brief Get current sample rate.
     */
    double getSampleRate() const { return sampleRate_; }

    /**
     * @brief Get current buffer size.
     */
    int getBufferSize() const { return bufferSize_; }

private:
    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;

    // Timing (updated from RT thread)
    uint64_t callbackStartTicks_ = 0;
    std::atomic<double> inputLatencyMs_{0.0};
    std::atomic<double> processingTimeMs_{0.0};
    std::atomic<double> outputLatencyMs_{0.0};
    std::atomic<double> cpuUsage_{0.0};

    // Running average for smooth display
    double avgProcessingTime_ = 0.0;
    static constexpr double kSmoothingFactor = 0.1;
};

} // namespace directpipe
