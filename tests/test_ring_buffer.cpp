/**
 * @file test_ring_buffer.cpp
 * @brief Unit tests for SPSC lock-free ring buffer
 */

#include <gtest/gtest.h>
#include "directpipe/RingBuffer.h"
#include "directpipe/Constants.h"
#include "directpipe/Protocol.h"

#include <vector>
#include <thread>
#include <atomic>
#include <array>
#include <memory>
#include <numeric>
#include <cmath>
#include <chrono>

using namespace directpipe;

class RingBufferTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 1024;   // frames
    static constexpr uint32_t kChannels = 2;
    static constexpr uint32_t kSampleRate = 48000;

    void SetUp() override {
        size_t memSize = calculateSharedMemorySize(kCapacity, kChannels);
        memory_.resize(memSize + 64, 0);  // extra for alignment
        // Align to 64 bytes
        void* raw = memory_.data();
        size_t space = memory_.size();
        alignedMem_ = std::align(64, memSize, raw, space);
        ASSERT_NE(alignedMem_, nullptr);
    }

    std::vector<uint8_t> memory_;
    void* alignedMem_ = nullptr;
};

TEST_F(RingBufferTest, InitAsProducer) {
    RingBuffer rb;
    rb.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    EXPECT_TRUE(rb.isValid());
    EXPECT_EQ(rb.getCapacity(), kCapacity);
    EXPECT_EQ(rb.getChannels(), kChannels);
    EXPECT_EQ(rb.getSampleRate(), kSampleRate);
    EXPECT_EQ(rb.availableRead(), 0u);
    EXPECT_EQ(rb.availableWrite(), kCapacity);
}

TEST_F(RingBufferTest, AttachAsConsumer) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    EXPECT_TRUE(consumer.attachAsConsumer(alignedMem_));
    EXPECT_TRUE(consumer.isValid());
    EXPECT_EQ(consumer.getCapacity(), kCapacity);
    EXPECT_EQ(consumer.getChannels(), kChannels);
    EXPECT_EQ(consumer.getSampleRate(), kSampleRate);
}

TEST_F(RingBufferTest, ProducerGenerationChangesAndOldDetachDoesNotClearNewConsumer) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer oldConsumer;
    ASSERT_TRUE(oldConsumer.attachAsConsumer(alignedMem_));
    const auto oldGeneration = oldConsumer.getAttachedProducerGeneration();
    ASSERT_NE(oldGeneration, 0u);

    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);
    RingBuffer newConsumer;
    ASSERT_TRUE(newConsumer.attachAsConsumer(alignedMem_));
    EXPECT_NE(oldGeneration, newConsumer.getAttachedProducerGeneration());

    auto* header = static_cast<DirectPipeHeader*>(alignedMem_);
    ASSERT_TRUE(header->consumer_active.load(std::memory_order_acquire));
    oldConsumer.detach();
    EXPECT_TRUE(header->consumer_active.load(std::memory_order_acquire));
    newConsumer.detach();
    EXPECT_FALSE(header->consumer_active.load(std::memory_order_acquire));
}

TEST_F(RingBufferTest, ConcurrentAttachClaimsConsumerFlagAtomically) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    constexpr size_t kConsumerCount = 16;
    std::array<std::unique_ptr<RingBuffer>, kConsumerCount> consumers;
    std::array<bool, kConsumerCount> attached{};
    std::array<bool, kConsumerCount> warned{};
    std::vector<std::thread> threads;
    threads.reserve(kConsumerCount);

    std::atomic<size_t> ready{0};
    std::atomic<bool> start{false};
    for (size_t index = 0; index < kConsumerCount; ++index) {
        consumers[index] = std::make_unique<RingBuffer>();
        threads.emplace_back([&, index] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            attached[index] = consumers[index]->attachAsConsumer(alignedMem_);
            warned[index] = consumers[index]->anotherConsumerWasActive();
        });
    }

    while (ready.load(std::memory_order_acquire) != kConsumerCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(std::count(attached.begin(), attached.end(), true),
              static_cast<ptrdiff_t>(kConsumerCount));
    EXPECT_EQ(std::count(warned.begin(), warned.end(), false), 1);
}

TEST_F(RingBufferTest, StaleConsumerHeuristicStillSuppressesWarning) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    auto* header = static_cast<DirectPipeHeader*>(alignedMem_);
    header->consumer_active.store(true, std::memory_order_release);
    header->write_pos.store(kCapacity, std::memory_order_relaxed);
    header->read_pos.store(0, std::memory_order_relaxed);

    RingBuffer replacementConsumer;
    ASSERT_TRUE(replacementConsumer.attachAsConsumer(alignedMem_));
    EXPECT_FALSE(replacementConsumer.anotherConsumerWasActive());
}

TEST_F(RingBufferTest, AttachFailsOnNullMemory) {
    RingBuffer rb;
    EXPECT_FALSE(rb.attachAsConsumer(nullptr));
    EXPECT_FALSE(rb.isValid());
}

TEST_F(RingBufferTest, AttachRejectsTruncatedMappingBeforeDereference) {
    RingBuffer rb;
    auto* inaccessibleAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    EXPECT_FALSE(rb.attachAsConsumer(inaccessibleAddress,
                                     sizeof(DirectPipeHeader) - 1));
    EXPECT_FALSE(rb.isValid());
}

TEST_F(RingBufferTest, SingleWriteAndRead) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // Write 128 frames of stereo data
    const uint32_t frames = 128;
    std::vector<float> writeData(frames * kChannels);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = static_cast<float>(i) / writeData.size();
    }

    uint32_t written = producer.write(writeData.data(), frames);
    EXPECT_EQ(written, frames);
    EXPECT_EQ(producer.availableRead(), frames);
    EXPECT_EQ(consumer.availableRead(), frames);

    // Read back
    std::vector<float> readData(frames * kChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), frames);
    EXPECT_EQ(readCount, frames);

    // Verify data integrity
    for (size_t i = 0; i < writeData.size(); ++i) {
        EXPECT_FLOAT_EQ(writeData[i], readData[i]) << "Mismatch at index " << i;
    }
}

TEST_F(RingBufferTest, DiscardAdvancesConsumerWithoutCopying) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);
    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(alignedMem_));

    std::vector<float> data(128 * kChannels, 0.25f);
    ASSERT_EQ(producer.write(data.data(), 128), 128u);
    EXPECT_EQ(consumer.discard(96), 96u);
    EXPECT_EQ(consumer.availableRead(), 32u);
    EXPECT_EQ(consumer.discard(100), 32u);
    EXPECT_EQ(consumer.availableRead(), 0u);
}

TEST_F(RingBufferTest, MultipleWriteAndRead) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    const uint32_t frames = 64;
    std::vector<float> writeData(frames * kChannels);

    // Write and read multiple times
    for (int batch = 0; batch < 10; ++batch) {
        for (size_t i = 0; i < writeData.size(); ++i) {
            writeData[i] = static_cast<float>(batch * 1000 + i);
        }

        uint32_t written = producer.write(writeData.data(), frames);
        EXPECT_EQ(written, frames);

        std::vector<float> readData(frames * kChannels, 0.0f);
        uint32_t readCount = consumer.read(readData.data(), frames);
        EXPECT_EQ(readCount, frames);

        for (size_t i = 0; i < writeData.size(); ++i) {
            EXPECT_FLOAT_EQ(writeData[i], readData[i]);
        }
    }
}

TEST_F(RingBufferTest, WrapAround) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // Fill most of the buffer
    const uint32_t fillFrames = kCapacity - 100;
    std::vector<float> fillData(fillFrames * kChannels, 1.0f);
    producer.write(fillData.data(), fillFrames);
    consumer.read(fillData.data(), fillFrames);

    // Now write data that wraps around
    const uint32_t wrapFrames = 200;
    std::vector<float> writeData(wrapFrames * kChannels);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = static_cast<float>(i) * 0.01f;
    }

    uint32_t written = producer.write(writeData.data(), wrapFrames);
    EXPECT_EQ(written, wrapFrames);

    std::vector<float> readData(wrapFrames * kChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), wrapFrames);
    EXPECT_EQ(readCount, wrapFrames);

    for (size_t i = 0; i < writeData.size(); ++i) {
        EXPECT_FLOAT_EQ(writeData[i], readData[i]) << "Mismatch at index " << i;
    }
}

TEST_F(RingBufferTest, OverflowDropsFrames) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    // Try to write more than capacity
    std::vector<float> bigData((kCapacity + 100) * kChannels, 1.0f);
    uint32_t written = producer.write(bigData.data(), kCapacity + 100);
    EXPECT_EQ(written, kCapacity);  // Only capacity frames should be written
}

TEST_F(RingBufferTest, CorruptUsedDistanceFailsWriteClosed) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    auto* header = static_cast<DirectPipeHeader*>(alignedMem_);
    header->write_pos.store(static_cast<uint64_t>(kCapacity) + 1,
                            std::memory_order_relaxed);
    header->read_pos.store(0, std::memory_order_relaxed);

    std::vector<float> oneFrame(kChannels, 1.0f);
    EXPECT_EQ(producer.write(oneFrame.data(), 1), 0u);
    EXPECT_EQ(header->write_pos.load(std::memory_order_relaxed),
              static_cast<uint64_t>(kCapacity) + 1);
}

TEST_F(RingBufferTest, UnderrunReturnsZero) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // Try to read from empty buffer
    std::vector<float> readData(128 * kChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), 128);
    EXPECT_EQ(readCount, 0u);
}

TEST_F(RingBufferTest, Reset) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    // Write some data
    std::vector<float> data(128 * kChannels, 1.0f);
    producer.write(data.data(), 128);
    EXPECT_EQ(producer.availableRead(), 128u);

    // Reset
    producer.reset();
    EXPECT_EQ(producer.availableRead(), 0u);
    EXPECT_EQ(producer.availableWrite(), kCapacity);
}

TEST_F(RingBufferTest, ConcurrentProducerConsumer) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    constexpr uint32_t kFramesPerBlock = 128;
    constexpr uint32_t kTotalBlocks = 1000;
    constexpr uint32_t kTotalFrames = kFramesPerBlock * kTotalBlocks;

    std::atomic<bool> producerDone{false};
    std::atomic<uint64_t> totalWritten{0};
    std::atomic<uint64_t> totalRead{0};

    // Producer thread
    std::thread producerThread([&]() {
        std::vector<float> data(kFramesPerBlock * kChannels);
        uint32_t blocksWritten = 0;

        while (blocksWritten < kTotalBlocks) {
            // Fill with sequential pattern
            float base = static_cast<float>(blocksWritten * kFramesPerBlock);
            for (uint32_t i = 0; i < kFramesPerBlock * kChannels; ++i) {
                data[i] = base + static_cast<float>(i) * 0.001f;
            }

            uint32_t written = producer.write(data.data(), kFramesPerBlock);
            if (written > 0) {
                totalWritten.fetch_add(written, std::memory_order_relaxed);
                ++blocksWritten;
            } else {
                // Buffer full, yield
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumerThread([&]() {
        std::vector<float> data(kFramesPerBlock * kChannels);

        while (!producerDone.load(std::memory_order_acquire) ||
               consumer.availableRead() > 0) {
            uint32_t readCount = consumer.read(data.data(), kFramesPerBlock);
            if (readCount > 0) {
                totalRead.fetch_add(readCount, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producerThread.join();
    consumerThread.join();

    EXPECT_EQ(totalWritten.load(), kTotalFrames);
    EXPECT_EQ(totalRead.load(), kTotalFrames);
}

// ─── detach() tests ─────────────────────────────────────────────

TEST_F(RingBufferTest, DetachMakesInvalid) {
    RingBuffer rb;
    rb.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);
    EXPECT_TRUE(rb.isValid());

    rb.detach();
    EXPECT_FALSE(rb.isValid());
    EXPECT_EQ(rb.getCapacity(), 0u);
    EXPECT_EQ(rb.getChannels(), 0u);
    EXPECT_EQ(rb.getSampleRate(), 0u);
}

TEST_F(RingBufferTest, WriteAfterDetachReturnsZero) {
    RingBuffer rb;
    rb.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);
    rb.detach();

    std::vector<float> data(128 * kChannels, 1.0f);
    EXPECT_EQ(rb.write(data.data(), 128), 0u);
}

TEST_F(RingBufferTest, ReadAfterDetachReturnsZero) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    std::vector<float> data(128 * kChannels, 1.0f);
    producer.write(data.data(), 128);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);
    consumer.detach();

    std::vector<float> readData(128 * kChannels);
    EXPECT_EQ(consumer.read(readData.data(), 128), 0u);
}

TEST_F(RingBufferTest, AvailableAfterDetachReturnsZero) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    std::vector<float> data(128 * kChannels, 1.0f);
    producer.write(data.data(), 128);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);
    EXPECT_EQ(consumer.availableRead(), 128u);

    consumer.detach();
    EXPECT_EQ(consumer.availableRead(), 0u);
    EXPECT_EQ(consumer.availableWrite(), 0u);
}

TEST_F(RingBufferTest, DetachThenReattach) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    // Write data
    std::vector<float> data(128 * kChannels, 0.5f);
    producer.write(data.data(), 128);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);
    consumer.detach();
    EXPECT_FALSE(consumer.isValid());

    // Re-attach — data still in shared memory
    EXPECT_TRUE(consumer.attachAsConsumer(alignedMem_));
    EXPECT_TRUE(consumer.isValid());
    EXPECT_EQ(consumer.availableRead(), 128u);

    // Read and verify data integrity
    std::vector<float> readData(128 * kChannels, 0.0f);
    EXPECT_EQ(consumer.read(readData.data(), 128), 128u);
    for (size_t i = 0; i < readData.size(); ++i) {
        EXPECT_FLOAT_EQ(readData[i], 0.5f);
    }
}

TEST_F(RingBufferTest, ResetAfterDetachIsNoOp) {
    RingBuffer rb;
    rb.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    std::vector<float> data(128 * kChannels, 1.0f);
    rb.write(data.data(), 128);

    rb.detach();
    rb.reset();  // Should not crash (guarded by isValid check)
    EXPECT_FALSE(rb.isValid());
}

// ─── End of detach() tests ──────────────────────────────────────

TEST_F(RingBufferTest, AvailableReadWriteConsistency) {
    RingBuffer producer;
    producer.initAsProducer(alignedMem_, kCapacity, kChannels, kSampleRate);

    RingBuffer consumer;
    consumer.attachAsConsumer(alignedMem_);

    // Initially: all space available for writing, nothing to read
    EXPECT_EQ(producer.availableWrite(), kCapacity);
    EXPECT_EQ(consumer.availableRead(), 0u);

    // Write half
    std::vector<float> data(512 * kChannels, 1.0f);
    producer.write(data.data(), 512);

    EXPECT_EQ(producer.availableWrite(), kCapacity - 512);
    EXPECT_EQ(consumer.availableRead(), 512u);

    // Read some
    consumer.read(data.data(), 200);
    EXPECT_EQ(consumer.availableRead(), 312u);
    EXPECT_EQ(producer.availableWrite(), kCapacity - 312);
}
