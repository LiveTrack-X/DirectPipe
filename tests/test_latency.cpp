/**
 * @file test_latency.cpp
 * @brief Latency benchmarks for ring buffer IPC
 *
 * Measures the time from producer write to consumer read
 * to validate that the IPC path meets the <1ms latency target.
 */

#include <gtest/gtest.h>
#include "directpipe/RingBuffer.h"
#include "directpipe/SharedMemory.h"
#include "directpipe/Constants.h"
#include "directpipe/Protocol.h"

#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>

using namespace directpipe;
using Clock = std::chrono::high_resolution_clock;

class LatencyTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 4096;
    static constexpr uint32_t kChannels = 2;
    static constexpr uint32_t kSampleRate = 48000;

    void SetUp() override {
        size_t memSize = calculateSharedMemorySize(kCapacity, kChannels);
        memory_.resize(memSize + 64, 0);
        void* raw = memory_.data();
        size_t space = memory_.size();
        alignedMem_ = std::align(64, memSize, raw, space);
        ASSERT_NE(alignedMem_, nullptr);
    }

    std::vector<uint8_t> memory_;
    void* alignedMem_ = nullptr;
};

TEST_F(LatencyTest, SingleWriteReadLatency) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    constexpr uint32_t kFrames = 128;
    constexpr int kIterations = 10000;

    std::vector<float> writeData(kFrames * kChannels, 1.0f);
    std::vector<float> readData(kFrames * kChannels, 0.0f);
    std::vector<double> latencies;
    latencies.reserve(kIterations);

    for (int i = 0; i < kIterations; ++i) {
        auto start = Clock::now();
        producer.write(writeData.data(), kFrames);
        consumer.read(readData.data(), kFrames);
        auto end = Clock::now();

        double us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(us);
    }

    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());
    double median = latencies[kIterations / 2];
    double p99 = latencies[static_cast<size_t>(kIterations * 0.99)];
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / kIterations;
    double minVal = latencies.front();
    double maxVal = latencies.back();

    // Print results
    std::cout << "\n=== Ring Buffer Latency Benchmark ===" << std::endl;
    std::cout << "  Frames per block: " << kFrames << std::endl;
    std::cout << "  Channels: " << kChannels << std::endl;
    std::cout << "  Iterations: " << kIterations << std::endl;
    std::cout << "  Min:    " << minVal << " us" << std::endl;
    std::cout << "  Avg:    " << avg << " us" << std::endl;
    std::cout << "  Median: " << median << " us" << std::endl;
    std::cout << "  P99:    " << p99 << " us" << std::endl;
    std::cout << "  Max:    " << maxVal << " us" << std::endl;
    std::cout << "======================================\n" << std::endl;

    // Ring buffer write+read should be well under 100 microseconds
    EXPECT_LT(median, 100.0) << "Median latency exceeds 100us";
    EXPECT_LT(p99, 500.0) << "P99 latency exceeds 500us";
}

TEST_F(LatencyTest, CrossThreadLatency) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    constexpr uint32_t kFrames = 128;
    constexpr int kIterations = 5000;

    // Shared timestamp buffer: producer writes timestamp, consumer reads it
    struct TimestampedBlock {
        Clock::time_point send_time;
    };

    std::vector<double> latencies;
    latencies.reserve(kIterations);
    std::atomic<bool> done{false};
    std::atomic<int> received{0};

    // We embed a "timestamp" by writing a specific pattern at block start
    // The latency is measured by checking timestamps in the main thread
    std::vector<Clock::time_point> sendTimes(kIterations);
    std::vector<Clock::time_point> recvTimes(kIterations);

    // Consumer thread
    std::thread consumerThread([&]() {
        std::vector<float> readBuf(kFrames * kChannels);
        while (!done.load(std::memory_order_acquire)) {
            uint32_t readCount = consumer.read(readBuf.data(), kFrames);
            if (readCount > 0) {
                int idx = received.fetch_add(1, std::memory_order_relaxed);
                if (idx < kIterations) {
                    recvTimes[idx] = Clock::now();
                }
            } else {
                // Busy-wait (simulating real-time behavior)
                std::this_thread::yield();
            }
        }
    });

    // Producer thread
    std::vector<float> writeBuf(kFrames * kChannels, 0.5f);
    for (int i = 0; i < kIterations; ++i) {
        sendTimes[i] = Clock::now();
        producer.write(writeBuf.data(), kFrames);
        // Simulate ~2.67ms period (128 samples @ 48kHz)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Wait for consumer to finish
    while (received.load(std::memory_order_relaxed) < kIterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    done.store(true, std::memory_order_release);
    consumerThread.join();

    // Calculate cross-thread latencies
    for (int i = 0; i < kIterations; ++i) {
        double us = std::chrono::duration<double, std::micro>(recvTimes[i] - sendTimes[i]).count();
        latencies.push_back(us);
    }

    std::sort(latencies.begin(), latencies.end());
    double median = latencies[kIterations / 2];
    double p99 = latencies[static_cast<size_t>(kIterations * 0.99)];
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / kIterations;

    std::cout << "\n=== Cross-Thread Latency Benchmark ===" << std::endl;
    std::cout << "  Frames per block: " << kFrames << std::endl;
    std::cout << "  Iterations: " << kIterations << std::endl;
    std::cout << "  Min:    " << latencies.front() << " us" << std::endl;
    std::cout << "  Avg:    " << avg << " us" << std::endl;
    std::cout << "  Median: " << median << " us" << std::endl;
    std::cout << "  P99:    " << p99 << " us" << std::endl;
    std::cout << "  Max:    " << latencies.back() << " us" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Cross-thread IPC should be well under 1ms
    EXPECT_LT(median, 1000.0) << "Median cross-thread latency exceeds 1ms";
}

TEST_F(LatencyTest, ThroughputBenchmark) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    constexpr uint32_t kFrames = 128;
    constexpr int kIterations = 100000;

    std::vector<float> writeBuf(kFrames * kChannels, 1.0f);
    std::vector<float> readBuf(kFrames * kChannels, 0.0f);

    auto start = Clock::now();

    for (int i = 0; i < kIterations; ++i) {
        producer.write(writeBuf.data(), kFrames);
        consumer.read(readBuf.data(), kFrames);
    }

    auto end = Clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    double opsPerSec = kIterations / (totalMs / 1000.0);
    double totalFrames = static_cast<double>(kIterations) * kFrames;
    double durationInAudioSec = totalFrames / kSampleRate;

    std::cout << "\n=== Throughput Benchmark ===" << std::endl;
    std::cout << "  Total operations: " << kIterations << std::endl;
    std::cout << "  Total time: " << totalMs << " ms" << std::endl;
    std::cout << "  Operations/sec: " << opsPerSec << std::endl;
    std::cout << "  Audio processed: " << durationInAudioSec << " seconds" << std::endl;
    std::cout << "  Realtime ratio: " << (durationInAudioSec / (totalMs / 1000.0)) << "x" << std::endl;
    std::cout << "============================\n" << std::endl;

    // Should process much faster than realtime
    EXPECT_GT(durationInAudioSec / (totalMs / 1000.0), 10.0)
        << "Throughput is less than 10x realtime";
}
