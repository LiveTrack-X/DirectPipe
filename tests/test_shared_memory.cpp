/**
 * @file test_shared_memory.cpp
 * @brief Unit tests for shared memory IPC wrapper
 */

#include <gtest/gtest.h>
#include "directpipe/SharedMemory.h"
#include "directpipe/RingBuffer.h"
#include "directpipe/Constants.h"
#include "directpipe/Protocol.h"

#include <thread>
#include <chrono>
#include <vector>

using namespace directpipe;

class SharedMemoryTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 4096;
    static constexpr uint32_t kChannels = 2;
    static constexpr uint32_t kSampleRate = 48000;
    const std::string kTestShmName = "Local\\DirectPipeTest";
    const std::string kTestEventName = "Local\\DirectPipeTestEvent";
};

TEST_F(SharedMemoryTest, CreateAndMap) {
    SharedMemory shm;
    size_t size = calculateSharedMemorySize(kCapacity, kChannels);

    EXPECT_TRUE(shm.create(kTestShmName, size));
    EXPECT_TRUE(shm.isOpen());
    EXPECT_NE(shm.getData(), nullptr);
    EXPECT_EQ(shm.getSize(), size);

    shm.close();
    EXPECT_FALSE(shm.isOpen());
    EXPECT_EQ(shm.getData(), nullptr);
}

TEST_F(SharedMemoryTest, ProducerConsumerSharedMemory) {
    size_t size = calculateSharedMemorySize(kCapacity, kChannels);

    // Producer creates shared memory
    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, size));

    // Initialize ring buffer in shared memory
    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    // Consumer opens the same shared memory
    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, size));

    // Consumer attaches to ring buffer
    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    // Write from producer
    const uint32_t frames = 128;
    std::vector<float> writeData(frames * kChannels);
    for (size_t i = 0; i < writeData.size(); ++i) {
        writeData[i] = static_cast<float>(i) * 0.1f;
    }
    EXPECT_EQ(producer.write(writeData.data(), frames), frames);

    // Read from consumer
    std::vector<float> readData(frames * kChannels, 0.0f);
    EXPECT_EQ(consumer.read(readData.data(), frames), frames);

    // Verify
    for (size_t i = 0; i < writeData.size(); ++i) {
        EXPECT_FLOAT_EQ(writeData[i], readData[i]);
    }

    producerShm.close();
    consumerShm.close();
}

TEST_F(SharedMemoryTest, MoveSemantics) {
    SharedMemory shm1;
    size_t size = calculateSharedMemorySize(kCapacity, kChannels);
    ASSERT_TRUE(shm1.create(kTestShmName, size));

    void* originalData = shm1.getData();

    // Move construct
    SharedMemory shm2(std::move(shm1));
    EXPECT_FALSE(shm1.isOpen());
    EXPECT_TRUE(shm2.isOpen());
    EXPECT_EQ(shm2.getData(), originalData);

    // Move assign
    SharedMemory shm3;
    shm3 = std::move(shm2);
    EXPECT_FALSE(shm2.isOpen());
    EXPECT_TRUE(shm3.isOpen());
    EXPECT_EQ(shm3.getData(), originalData);

    shm3.close();
}

TEST_F(SharedMemoryTest, NamedEventCreateAndSignal) {
    NamedEvent event;
    EXPECT_TRUE(event.create(kTestEventName));
    EXPECT_TRUE(event.isOpen());

    // Signal and wait should succeed
    event.signal();
    EXPECT_TRUE(event.wait(100));  // Should not block

    // Wait without signal should timeout
    EXPECT_FALSE(event.wait(50));

    event.close();
    EXPECT_FALSE(event.isOpen());
}

TEST_F(SharedMemoryTest, NamedEventCrossThread) {
    NamedEvent event;
    ASSERT_TRUE(event.create(kTestEventName));

    std::atomic<bool> signaled{false};

    // Consumer thread waits for event
    std::thread consumer([&]() {
        bool result = event.wait(2000);  // 2 second timeout
        signaled.store(result, std::memory_order_release);
    });

    // Brief delay then signal from main thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    event.signal();

    consumer.join();
    EXPECT_TRUE(signaled.load(std::memory_order_acquire));

    event.close();
}

TEST_F(SharedMemoryTest, NamedEventMoveSemantics) {
    NamedEvent ev1;
    ASSERT_TRUE(ev1.create(kTestEventName));

    // Move construct
    NamedEvent ev2(std::move(ev1));
    EXPECT_FALSE(ev1.isOpen());
    EXPECT_TRUE(ev2.isOpen());

    // Move assign
    NamedEvent ev3;
    ev3 = std::move(ev2);
    EXPECT_FALSE(ev2.isOpen());
    EXPECT_TRUE(ev3.isOpen());

    ev3.close();
}

TEST_F(SharedMemoryTest, FullIPCPipeline) {
    // Simulate the full IPC pipeline: producer writes to shared memory,
    // signals event, consumer reads from shared memory.

    size_t size = calculateSharedMemorySize(kCapacity, kChannels);

    SharedMemory producerShm;
    ASSERT_TRUE(producerShm.create(kTestShmName, size));

    RingBuffer producer;
    producer.initAsProducer(producerShm.getData(), kCapacity, kChannels, kSampleRate);

    NamedEvent event;
    ASSERT_TRUE(event.create(kTestEventName));

    SharedMemory consumerShm;
    ASSERT_TRUE(consumerShm.open(kTestShmName, size));

    RingBuffer consumer;
    ASSERT_TRUE(consumer.attachAsConsumer(consumerShm.getData()));

    constexpr uint32_t kFramesPerBlock = 128;
    constexpr int kBlocks = 100;

    std::atomic<int> blocksReceived{0};

    // Consumer thread
    std::thread consumerThread([&]() {
        std::vector<float> readBuf(kFramesPerBlock * kChannels);
        while (blocksReceived.load(std::memory_order_relaxed) < kBlocks) {
            if (event.wait(500)) {
                uint32_t read = consumer.read(readBuf.data(), kFramesPerBlock);
                if (read > 0) {
                    blocksReceived.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    // Producer writes and signals
    std::vector<float> writeBuf(kFramesPerBlock * kChannels, 0.5f);
    for (int i = 0; i < kBlocks; ++i) {
        while (producer.availableWrite() < kFramesPerBlock) {
            std::this_thread::yield();
        }
        producer.write(writeBuf.data(), kFramesPerBlock);
        event.signal();
    }

    consumerThread.join();
    EXPECT_EQ(blocksReceived.load(), kBlocks);

    producerShm.close();
    consumerShm.close();
    event.close();
}
