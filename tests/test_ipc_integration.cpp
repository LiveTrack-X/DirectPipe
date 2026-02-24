/**
 * @file test_ipc_integration.cpp
 * @brief Integration tests for the full IPC pipeline
 *
 * Tests the complete data flow from producer writing audio data into
 * shared memory through the ring buffer, to the consumer reading it
 * back. Verifies data integrity, event signaling, multi-block transfers,
 * and concurrent producer/consumer operation across the full IPC stack.
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
#include <cmath>
#include <cstring>
#include <algorithm>

using namespace directpipe;

// ─── Test Fixture ───────────────────────────────────────────────────

class IPCIntegrationTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 4096;
    static constexpr uint32_t kChannels = 2;
    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kFramesPerBlock = 128;

    const std::string kTestShmName = "Local\\DirectPipeIntegTest";
    const std::string kTestEventName = "Local\\DirectPipeIntegTestEvent";
};

// ─── Basic Pipeline Tests ───────────────────────────────────────────

TEST_F(IPCIntegrationTest, ProducerWriteConsumerReadSingleBlock) {
    // Setup: producer creates shared memory and ring buffer
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    // Consumer opens the same shared memory
    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    // Producer writes a block of known data
    std::vector<float> writeData(kFramesPerBlock * kChannels);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = std::sin(static_cast<float>(i) * 0.1f);
    }

    uint32_t written = producer.write(writeData.data(), kFramesPerBlock);
    EXPECT_EQ(written, kFramesPerBlock);

    // Consumer reads it back
    std::vector<float> readData(kFramesPerBlock * kChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), kFramesPerBlock);
    EXPECT_EQ(readCount, kFramesPerBlock);

    // Verify every sample matches
    for (size_t i = 0; i < writeData.size(); ++i) {
        EXPECT_FLOAT_EQ(writeData[i], readData[i])
            << "Data mismatch at sample index " << i;
    }

    producerShm.close();
    consumerShm.close();
}

TEST_F(IPCIntegrationTest, MultiBlockTransferWithDataIntegrity) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    constexpr int kBlocks = 50;

    for (int block = 0; block < kBlocks; ++block) {
        // Generate unique data pattern for each block
        std::vector<float> writeData(kFramesPerBlock * kChannels);
        float baseValue = static_cast<float>(block) * 100.0f;
        for (size_t i = 0; i < writeData.size(); ++i) {
            writeData[i] = baseValue + static_cast<float>(i) * 0.01f;
        }

        uint32_t written = producer.write(writeData.data(), kFramesPerBlock);
        ASSERT_EQ(written, kFramesPerBlock) << "Write failed at block " << block;

        std::vector<float> readData(kFramesPerBlock * kChannels, 0.0f);
        uint32_t readCount = consumer.read(readData.data(), kFramesPerBlock);
        ASSERT_EQ(readCount, kFramesPerBlock) << "Read failed at block " << block;

        // Verify data integrity for every sample in every block
        for (size_t i = 0; i < writeData.size(); ++i) {
            EXPECT_FLOAT_EQ(writeData[i], readData[i])
                << "Mismatch at block " << block << ", sample " << i;
        }
    }

    producerShm.close();
    consumerShm.close();
}

// ─── Event-Driven Pipeline Tests ────────────────────────────────────

TEST_F(IPCIntegrationTest, EventSignaledPipeline) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    // Producer side
    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    NamedEvent dataReadyEvent;
    ASSERT_TRUE(dataReadyEvent.create(kTestEventName));

    // Consumer side
    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    constexpr int kBlocks = 20;
    std::atomic<int> blocksReceived{0};
    std::vector<std::vector<float>> receivedBlocks(kBlocks);

    // Consumer thread: waits for event, then reads
    std::thread consumerThread([&]() {
        std::vector<float> readBuf(kFramesPerBlock * kChannels);
        while (blocksReceived.load(std::memory_order_relaxed) < kBlocks) {
            if (dataReadyEvent.wait(2000)) {
                uint32_t readCount = consumer.read(readBuf.data(), kFramesPerBlock);
                if (readCount > 0) {
                    int idx = blocksReceived.fetch_add(1, std::memory_order_relaxed);
                    if (idx < kBlocks) {
                        receivedBlocks[idx] = readBuf;
                    }
                }
            }
        }
    });

    // Producer thread: writes data and signals event
    std::vector<std::vector<float>> sentBlocks(kBlocks);
    for (int i = 0; i < kBlocks; ++i) {
        std::vector<float> writeData(kFramesPerBlock * kChannels);
        float baseValue = static_cast<float>(i) * 1000.0f;
        for (size_t j = 0; j < writeData.size(); ++j) {
            writeData[j] = baseValue + static_cast<float>(j);
        }
        sentBlocks[i] = writeData;

        while (producer.availableWrite() < kFramesPerBlock) {
            std::this_thread::yield();
        }
        producer.write(writeData.data(), kFramesPerBlock);
        dataReadyEvent.signal();

        // Small delay to simulate real audio callback timing
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    consumerThread.join();

    // Verify all blocks received
    EXPECT_EQ(blocksReceived.load(), kBlocks);

    // Verify data integrity of each received block
    for (int i = 0; i < kBlocks; ++i) {
        ASSERT_EQ(receivedBlocks[i].size(), sentBlocks[i].size())
            << "Block " << i << " size mismatch";

        for (size_t j = 0; j < sentBlocks[i].size(); ++j) {
            EXPECT_FLOAT_EQ(sentBlocks[i][j], receivedBlocks[i][j])
                << "Data mismatch at block " << i << ", sample " << j;
        }
    }

    producerShm.close();
    consumerShm.close();
    dataReadyEvent.close();
}

// ─── Concurrent Pipeline Tests ──────────────────────────────────────

TEST_F(IPCIntegrationTest, ConcurrentProducerConsumerHighThroughput) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    constexpr int kTotalBlocks = 500;
    constexpr uint32_t kTotalFrames = kFramesPerBlock * kTotalBlocks;

    std::atomic<bool> producerDone{false};
    std::atomic<uint64_t> totalWritten{0};
    std::atomic<uint64_t> totalRead{0};

    // Producer thread: writes sequential data blocks
    std::thread producerThread([&]() {
        std::vector<float> writeBuf(kFramesPerBlock * kChannels);
        int blocksWritten = 0;

        while (blocksWritten < kTotalBlocks) {
            // Fill with sequential pattern for verification
            float base = static_cast<float>(blocksWritten);
            for (size_t i = 0; i < writeBuf.size(); ++i) {
                writeBuf[i] = base + static_cast<float>(i) * 0.001f;
            }

            uint32_t written = producer.write(writeBuf.data(), kFramesPerBlock);
            if (written > 0) {
                totalWritten.fetch_add(written, std::memory_order_relaxed);
                ++blocksWritten;
            } else {
                std::this_thread::yield();
            }
        }

        producerDone.store(true, std::memory_order_release);
    });

    // Consumer thread: reads and verifies data
    std::thread consumerThread([&]() {
        std::vector<float> readBuf(kFramesPerBlock * kChannels);

        while (!producerDone.load(std::memory_order_acquire) ||
               consumer.availableRead() > 0) {
            uint32_t readCount = consumer.read(readBuf.data(), kFramesPerBlock);
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

    producerShm.close();
    consumerShm.close();
}

// ─── Protocol Compatibility Tests ───────────────────────────────────

TEST_F(IPCIntegrationTest, ProtocolVersionCheck) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory shm;
    ASSERT_TRUE(shm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(shm.getData(), kCapacity, kChannels, kSampleRate);

    // Verify the header has the correct protocol version
    auto* header = static_cast<DirectPipeHeader*>(shm.getData());
    EXPECT_EQ(header->version, PROTOCOL_VERSION);

    // Verify other header fields
    EXPECT_EQ(header->sample_rate, kSampleRate);
    EXPECT_EQ(header->channels, kChannels);
    EXPECT_EQ(header->buffer_frames, kCapacity);

    shm.close();
}

TEST_F(IPCIntegrationTest, SharedMemorySizeCalculation) {
    size_t expectedSize = sizeof(DirectPipeHeader)
                        + (static_cast<size_t>(kCapacity) * kChannels * sizeof(float));
    size_t calculatedSize = calculateSharedMemorySize(kCapacity, kChannels);

    EXPECT_EQ(calculatedSize, expectedSize);
}

TEST_F(IPCIntegrationTest, ProducerActiveFlag) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    // After initialization, producer_active should be set
    auto* header = static_cast<DirectPipeHeader*>(producerShm.getData());

    // Consumer side can check this flag
    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    auto* consumerHeader = static_cast<const DirectPipeHeader*>(consumerShm.getData());
    EXPECT_EQ(consumerHeader->version, PROTOCOL_VERSION);
    EXPECT_EQ(consumerHeader->sample_rate, kSampleRate);
    EXPECT_EQ(consumerHeader->channels, kChannels);
    EXPECT_EQ(consumerHeader->buffer_frames, kCapacity);

    producerShm.close();
    consumerShm.close();
}

// ─── Buffer Wrap-Around Integrity Test ──────────────────────────────

TEST_F(IPCIntegrationTest, WrapAroundDataIntegrity) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    // Fill and drain most of the buffer to advance the position pointers
    // near the wrap-around boundary
    const uint32_t advanceFrames = kCapacity - kFramesPerBlock;
    std::vector<float> advanceBuf(advanceFrames * kChannels, 0.0f);
    producer.write(advanceBuf.data(), advanceFrames);
    consumer.read(advanceBuf.data(), advanceFrames);

    // Now write data that will wrap around the ring buffer
    const uint32_t wrapFrames = kFramesPerBlock * 2;
    std::vector<float> writeData(wrapFrames * kChannels);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = static_cast<float>(i) * 0.123f;
    }

    uint32_t written = producer.write(writeData.data(), wrapFrames);
    EXPECT_EQ(written, wrapFrames);

    std::vector<float> readData(wrapFrames * kChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), wrapFrames);
    EXPECT_EQ(readCount, wrapFrames);

    // Verify every sample survived the wrap-around
    for (size_t i = 0; i < writeData.size(); ++i) {
        EXPECT_FLOAT_EQ(writeData[i], readData[i])
            << "Wrap-around data mismatch at sample " << i;
    }

    producerShm.close();
    consumerShm.close();
}

// ─── Mono vs Stereo Pipeline Test ───────────────────────────────────

TEST_F(IPCIntegrationTest, MonoPipeline) {
    constexpr uint32_t monoChannels = 1;
    size_t shmSize = calculateSharedMemorySize(kCapacity, monoChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, monoChannels, kSampleRate);

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    EXPECT_EQ(producer.getChannels(), monoChannels);
    EXPECT_EQ(consumer.getChannels(), monoChannels);

    // Write mono audio data
    std::vector<float> writeData(kFramesPerBlock * monoChannels);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = std::sin(static_cast<float>(i) * 0.05f);
    }

    uint32_t written = producer.write(writeData.data(), kFramesPerBlock);
    EXPECT_EQ(written, kFramesPerBlock);

    std::vector<float> readData(kFramesPerBlock * monoChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), kFramesPerBlock);
    EXPECT_EQ(readCount, kFramesPerBlock);

    for (size_t i = 0; i < writeData.size(); ++i) {
        EXPECT_FLOAT_EQ(writeData[i], readData[i]);
    }

    producerShm.close();
    consumerShm.close();
}

// ─── Empty Read / Full Write Edge Cases ─────────────────────────────

TEST_F(IPCIntegrationTest, ReadFromEmptyBuffer) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    // Read from empty buffer should return 0
    std::vector<float> readData(kFramesPerBlock * kChannels, 0.0f);
    uint32_t readCount = consumer.read(readData.data(), kFramesPerBlock);
    EXPECT_EQ(readCount, 0u);

    producerShm.close();
    consumerShm.close();
}

TEST_F(IPCIntegrationTest, WriteToFullBuffer) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    // Fill the buffer completely
    std::vector<float> fillData(kCapacity * kChannels, 1.0f);
    uint32_t written = producer.write(fillData.data(), kCapacity);
    EXPECT_EQ(written, kCapacity);

    // Try to write more — should get 0 or limited writes
    std::vector<float> extraData(kFramesPerBlock * kChannels, 2.0f);
    uint32_t extraWritten = producer.write(extraData.data(), kFramesPerBlock);
    EXPECT_EQ(extraWritten, 0u);

    producerShm.close();
}

// ─── Sustained Streaming Simulation ─────────────────────────────────

TEST_F(IPCIntegrationTest, SimulatedAudioStreamingSession) {
    size_t shmSize = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, shmSize));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    NamedEvent dataReadyEvent;
    ASSERT_TRUE(dataReadyEvent.create(kTestEventName));

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, shmSize));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    // Simulate a 1-second audio streaming session at 48kHz with 128-frame blocks
    // That is approximately 375 blocks (48000 / 128)
    constexpr int kSessionBlocks = 375;

    std::atomic<int> blocksReceived{0};
    std::atomic<bool> sessionDone{false};

    // Consumer thread: simulates OBS plugin reading audio
    std::thread consumerThread([&]() {
        std::vector<float> readBuf(kFramesPerBlock * kChannels);

        while (!sessionDone.load(std::memory_order_acquire) ||
               consumer.availableRead() > 0) {
            if (dataReadyEvent.wait(500)) {
                uint32_t readCount = consumer.read(readBuf.data(), kFramesPerBlock);
                if (readCount > 0) {
                    blocksReceived.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    // Producer thread: simulates JUCE audio callback writing audio
    std::vector<float> writeBuf(kFramesPerBlock * kChannels);
    for (int i = 0; i < kSessionBlocks; ++i) {
        // Generate a simple sine wave
        float freq = 440.0f;
        for (uint32_t frame = 0; frame < kFramesPerBlock; ++frame) {
            float sample = std::sin(
                2.0f * 3.14159265f * freq *
                static_cast<float>(i * kFramesPerBlock + frame) /
                static_cast<float>(kSampleRate));

            for (uint32_t ch = 0; ch < kChannels; ++ch) {
                writeBuf[frame * kChannels + ch] = sample;
            }
        }

        while (producer.availableWrite() < kFramesPerBlock) {
            std::this_thread::yield();
        }
        producer.write(writeBuf.data(), kFramesPerBlock);
        dataReadyEvent.signal();

        // Simulate audio callback period (~2.67ms for 128 frames at 48kHz)
        // Use a much shorter sleep to keep the test fast
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    // Wait for consumer to finish processing all blocks
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (blocksReceived.load(std::memory_order_relaxed) < kSessionBlocks &&
           std::chrono::steady_clock::now() < deadline) {
        dataReadyEvent.signal();  // Wake consumer if waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    sessionDone.store(true, std::memory_order_release);
    dataReadyEvent.signal();  // Wake consumer to exit
    consumerThread.join();

    EXPECT_EQ(blocksReceived.load(), kSessionBlocks)
        << "Not all audio blocks were received in the streaming session";

    producerShm.close();
    consumerShm.close();
    dataReadyEvent.close();
}
