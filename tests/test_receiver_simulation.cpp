/**
 * @file test_receiver_simulation.cpp
 * @brief Simulates the Receiver VST plugin's processBlock logic
 *
 * Tests the core IPC consumer logic (de-interleave, partial read, underrun,
 * clock drift skip, producer death, disconnect sequence) using only the
 * core library — no JUCE dependency required.
 */

#include <gtest/gtest.h>
#include "directpipe/RingBuffer.h"
#include "directpipe/SharedMemory.h"
#include "directpipe/Constants.h"
#include "directpipe/Protocol.h"

#include <vector>
#include <cstring>
#include <algorithm>
#include <memory>

using namespace directpipe;

class ReceiverSimulationTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 16384;
    static constexpr uint32_t kChannels = DEFAULT_CHANNELS;  // 2
    static constexpr uint32_t kSampleRate = DEFAULT_SAMPLE_RATE;  // 48000
    static constexpr int kBlockSize = 128;

    void SetUp() override {
        size_t memSize = calculateSharedMemorySize(kCapacity, kChannels);
        memory_.resize(memSize + 64, 0);
        void* raw = memory_.data();
        size_t space = memory_.size();
        alignedMem_ = std::align(64, memSize, raw, space);
        ASSERT_NE(alignedMem_, nullptr);

        producer_.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);
    }

    /// Write interleaved stereo audio (simulates SharedMemWriter)
    void writeInterleaved(float leftVal, float rightVal, int frames) {
        std::vector<float> interleaved(static_cast<size_t>(frames) * kChannels);
        for (int i = 0; i < frames; ++i) {
            interleaved[static_cast<size_t>(i) * kChannels + 0] = leftVal;
            interleaved[static_cast<size_t>(i) * kChannels + 1] = rightVal;
        }
        producer_.write(interleaved.data(), static_cast<uint32_t>(frames));
    }

    /// Simulate Receiver's de-interleave read (same logic as processBlock)
    struct PlanarResult {
        std::vector<float> left;
        std::vector<float> right;
        int samplesRead = 0;
    };

    PlanarResult readAndDeinterleave(RingBuffer& consumer, int numSamples) {
        PlanarResult out;
        out.left.resize(static_cast<size_t>(numSamples), 0.0f);
        out.right.resize(static_cast<size_t>(numSamples), 0.0f);

        uint32_t available = consumer.availableRead();
        uint32_t toRead = (std::min)(available, static_cast<uint32_t>(numSamples));

        if (toRead == 0) return out;  // underrun — silence

        std::vector<float> interleaved(static_cast<size_t>(toRead) * kChannels);
        uint32_t readCount = consumer.read(interleaved.data(), toRead);
        if (readCount == 0) return out;

        out.samplesRead = static_cast<int>(readCount);

        // De-interleave: [L0 R0 L1 R1 ...] → [L0 L1 ...], [R0 R1 ...]
        for (int i = 0; i < out.samplesRead; ++i) {
            out.left[i] = interleaved[static_cast<size_t>(i) * kChannels + 0];
            out.right[i] = interleaved[static_cast<size_t>(i) * kChannels + 1];
        }

        return out;
    }

    std::vector<uint8_t> memory_;
    void* alignedMem_ = nullptr;
    RingBuffer producer_;
};

// ─── Normal Read + De-interleave ────────────────────────────────

TEST_F(ReceiverSimulationTest, NormalReadDeinterleave) {
    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    writeInterleaved(0.5f, -0.5f, kBlockSize);

    auto out = readAndDeinterleave(consumer, kBlockSize);
    EXPECT_EQ(out.samplesRead, kBlockSize);

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(out.left[i], 0.5f) << "L[" << i << "]";
        EXPECT_FLOAT_EQ(out.right[i], -0.5f) << "R[" << i << "]";
    }
}

TEST_F(ReceiverSimulationTest, MonoSourceDeinterleave) {
    // Re-init with mono
    size_t memSize = calculateSharedMemorySize(kCapacity, 1);
    std::vector<uint8_t> monoMem(memSize + 64, 0);
    void* raw = monoMem.data();
    size_t space = monoMem.size();
    void* aligned = std::align(64, memSize, raw, space);
    ASSERT_NE(aligned, nullptr);

    RingBuffer monoProducer;
    monoProducer.initAsProducer(aligned, kCapacity, 1, kSampleRate);

    // Write mono data
    std::vector<float> monoData(kBlockSize, 0.75f);
    monoProducer.write(monoData.data(), kBlockSize);

    RingBuffer consumer;
    consumer.attachAsConsumer(aligned);
    EXPECT_EQ(consumer.getChannels(), 1u);

    // Read and verify mono
    std::vector<float> readBuf(kBlockSize, 0.0f);
    EXPECT_EQ(consumer.read(readBuf.data(), kBlockSize), static_cast<uint32_t>(kBlockSize));
    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(readBuf[i], 0.75f);
    }
}

// ─── Underrun (no data available) ───────────────────────────────

TEST_F(ReceiverSimulationTest, UnderrunSilence) {
    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // No data written — underrun
    auto out = readAndDeinterleave(consumer, kBlockSize);
    EXPECT_EQ(out.samplesRead, 0);

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(out.left[i], 0.0f);
        EXPECT_FLOAT_EQ(out.right[i], 0.0f);
    }
}

// ─── Partial Read (pad rest with silence) ───────────────────────

TEST_F(ReceiverSimulationTest, PartialReadPadSilence) {
    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // Write only 64 frames, but request 128
    writeInterleaved(0.75f, -0.25f, 64);

    auto out = readAndDeinterleave(consumer, kBlockSize);
    EXPECT_EQ(out.samplesRead, 64);

    // First 64 samples have data
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(out.left[i], 0.75f);
        EXPECT_FLOAT_EQ(out.right[i], -0.25f);
    }
    // Remaining 64 are silence (zero-initialized)
    for (int i = 64; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(out.left[i], 0.0f);
        EXPECT_FLOAT_EQ(out.right[i], 0.0f);
    }
}

// ─── Clock Drift Compensation ───────────────────────────────────

TEST_F(ReceiverSimulationTest, ClockDriftSkip) {
    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // Receiver "Low" buffer preset: targetFill=512, highThreshold=1536
    constexpr uint32_t kTargetFill = 512;
    constexpr uint32_t kHighThreshold = 1536;

    // Write 2048 frames (> highThreshold), simulating producer clock faster than consumer
    for (int i = 0; i < 16; ++i)
        writeInterleaved(static_cast<float>(i), 0.0f, kBlockSize);

    uint32_t available = consumer.availableRead();
    EXPECT_GT(available, kHighThreshold);

    // Simulate Receiver drift compensation: skip excess frames
    uint32_t excess = available - kTargetFill;
    std::vector<float> skipBuf(static_cast<size_t>(kBlockSize) * kChannels);
    while (excess > 0) {
        uint32_t chunk = (std::min)(excess, static_cast<uint32_t>(kBlockSize));
        uint32_t read = consumer.read(skipBuf.data(), chunk);
        if (read == 0) break;
        excess -= (std::min)(read, excess);
    }

    // After skip, available should be close to targetFill
    available = consumer.availableRead();
    EXPECT_LE(available, kTargetFill + static_cast<uint32_t>(kBlockSize));
    EXPECT_GE(available, 1u);  // At least some data remains
}

// ─── Producer Death Detection ───────────────────────────────────

TEST_F(ReceiverSimulationTest, ProducerDeathDetection) {
    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    auto* header = static_cast<DirectPipeHeader*>(alignedMem_);

    // Producer is active (set by initAsProducer)
    EXPECT_TRUE(header->producer_active.load(std::memory_order_acquire));

    // Simulate producer death (host shutdown or IPC disable)
    header->producer_active.store(false, std::memory_order_release);
    EXPECT_FALSE(header->producer_active.load(std::memory_order_acquire));

    // Consumer detects and disconnects (like Receiver::disconnect)
    consumer.detach();
    EXPECT_FALSE(consumer.isValid());
}

// ─── Disconnect Sequence (detach before close) ─────────────────

TEST_F(ReceiverSimulationTest, DisconnectSequence) {
    // Test the correct disconnect order: detach() → close()
    // This is critical to prevent dangling pointer dereference.

    SharedMemory producerShm;
    size_t memSize = calculateSharedMemorySize(kCapacity, kChannels);
    ASSERT_TRUE(producerShm.create("Local\\DirectPipeTestReceiver", memSize));

    RingBuffer rbProducer;
    rbProducer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    // Write test data
    std::vector<float> data(static_cast<size_t>(kBlockSize) * kChannels, 0.5f);
    rbProducer.write(data.data(), kBlockSize);

    // Consumer opens same shared memory (like actual Receiver)
    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open("Local\\DirectPipeTestReceiver", memSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));
    EXPECT_TRUE(consumer.isValid());
    EXPECT_EQ(consumer.availableRead(), static_cast<uint32_t>(kBlockSize));

    // Disconnect sequence (matches Receiver::disconnect exactly)
    consumer.detach();          // 1. Invalidate pointers first
    EXPECT_FALSE(consumer.isValid());
    consumerShm.close();        // 2. Unmap memory — safe because pointers already nulled

    // All operations return 0 after detach (no crash)
    EXPECT_EQ(consumer.availableRead(), 0u);
    EXPECT_EQ(consumer.availableWrite(), 0u);
    EXPECT_EQ(consumer.getChannels(), 0u);
    EXPECT_EQ(consumer.getSampleRate(), 0u);
    EXPECT_EQ(consumer.getCapacity(), 0u);

    std::vector<float> readBuf(static_cast<size_t>(kBlockSize) * kChannels);
    EXPECT_EQ(consumer.read(readBuf.data(), kBlockSize), 0u);

    producerShm.close();
}

// ─── Reconnect After Disconnect ─────────────────────────────────

TEST_F(ReceiverSimulationTest, ReconnectAfterDisconnect) {
    // Simulates: connected → producer dies → disconnect → reconnect

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);
    EXPECT_TRUE(consumer.isValid());

    writeInterleaved(1.0f, -1.0f, kBlockSize);
    auto out1 = readAndDeinterleave(consumer, kBlockSize);
    EXPECT_EQ(out1.samplesRead, kBlockSize);

    // Producer dies
    auto* header = static_cast<DirectPipeHeader*>(alignedMem_);
    header->producer_active.store(false, std::memory_order_release);

    // Consumer disconnects
    consumer.detach();
    EXPECT_FALSE(consumer.isValid());

    // Producer restarts (re-init same memory)
    producer_.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);
    EXPECT_TRUE(header->producer_active.load(std::memory_order_acquire));

    // Consumer reconnects
    EXPECT_TRUE(consumer.attachAsConsumer(alignedMem_));
    EXPECT_TRUE(consumer.isValid());

    // New data flows
    writeInterleaved(0.25f, -0.75f, kBlockSize);
    auto out2 = readAndDeinterleave(consumer, kBlockSize);
    EXPECT_EQ(out2.samplesRead, kBlockSize);
    EXPECT_FLOAT_EQ(out2.left[0], 0.25f);
    EXPECT_FLOAT_EQ(out2.right[0], -0.75f);
}

// ─── Continuous Streaming Session ───────────────────────────────

TEST_F(ReceiverSimulationTest, ContinuousStreaming) {
    // Simulate ~1 second of real-time audio streaming (375 blocks @ 48kHz/128)
    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    constexpr int kTotalBlocks = 375;
    int producerBlock = 0;
    int consumerBlock = 0;

    while (consumerBlock < kTotalBlocks) {
        // Producer writes a block
        if (producerBlock < kTotalBlocks && producer_.availableWrite() >= static_cast<uint32_t>(kBlockSize)) {
            float val = static_cast<float>(producerBlock) * 0.001f;
            writeInterleaved(val, -val, kBlockSize);
            ++producerBlock;
        }

        // Consumer reads a block
        if (consumer.availableRead() >= static_cast<uint32_t>(kBlockSize)) {
            auto out = readAndDeinterleave(consumer, kBlockSize);
            ASSERT_EQ(out.samplesRead, kBlockSize);

            float expected = static_cast<float>(consumerBlock) * 0.001f;
            EXPECT_FLOAT_EQ(out.left[0], expected) << "Block " << consumerBlock;
            EXPECT_FLOAT_EQ(out.right[0], -expected) << "Block " << consumerBlock;

            ++consumerBlock;
        }
    }

    EXPECT_EQ(consumerBlock, kTotalBlocks);
}

// ─── Skip to Fresh Position ─────────────────────────────────────

TEST_F(ReceiverSimulationTest, SkipToFreshPosition) {
    // Simulates Receiver::skipToFreshPosition — on connect, advance read
    // pointer close to write pointer for minimal latency

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    constexpr uint32_t kTargetFill = 512;

    // Producer has been writing while consumer was disconnected (stale data)
    for (int i = 0; i < 40; ++i)  // 40 * 128 = 5120 frames
        writeInterleaved(static_cast<float>(i), 0.0f, kBlockSize);

    uint32_t available = consumer.availableRead();
    EXPECT_EQ(available, 5120u);

    // Skip to fresh position (same logic as Receiver)
    if (available > kTargetFill) {
        uint32_t skip = available - kTargetFill;
        std::vector<float> skipBuf(static_cast<size_t>(kBlockSize) * kChannels);
        while (skip > 0) {
            uint32_t chunk = (std::min)(skip, static_cast<uint32_t>(kBlockSize));
            uint32_t read = consumer.read(skipBuf.data(), chunk);
            if (read == 0) break;
            skip -= (std::min)(read, skip);
        }
    }

    // After skip, only ~targetFill frames remain (freshest audio)
    available = consumer.availableRead();
    EXPECT_EQ(available, kTargetFill);
}
