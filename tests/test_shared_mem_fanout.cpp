// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiveTrack
// Actual host-writer integration with isolated mapping/event names. These tests
// exercise legacy coexistence and per-slot queues, not a live audio device or OBS.

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "IPC/SharedMemWriter.h"
#include "directpipe/FanOut.h"
#include "directpipe/Protocol.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace directpipe;

namespace {

struct WriterNames {
    // Short enough for POSIX names, unique per fixture on every platform.
    const std::string stem = "Local\\DPFW_"
        + juce::Uuid().toString().removeCharacters("-").substring(0, 16).toStdString();
    const std::string legacy = stem + "_l";
    const std::string event = stem + "_e";
    const std::string fanOut = stem + "_f";
};

struct LegacyReader {
    SharedMemory mapping;
    RingBuffer queue;
    ~LegacyReader() { queue.detach(); }

    bool attach(const std::string& name) {
        return mapping.open(name, 0)
            && queue.attachAsConsumer(mapping.getData(), mapping.getSize());
    }
};

struct IndependentReader {
    SharedMemory mapping;
    FanOutConsumer queue;

    bool attach(const std::string& name) {
        if (!mapping.open(name, 0)) return false;
        const auto result = queue.claim(mapping.getData(), mapping.getSize());
        return result == FanOutAttachResult::Ready || result == FanOutAttachResult::Waiting;
    }
};

template <typename Predicate>
bool waitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

struct WriteBarrier {
    // Test-only synchronization proves shutdown drains an admitted write. The
    // production FanOut audio path contains no such wait or injected callback.
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};

    static void wait(void* context) {
        auto& self = *static_cast<WriteBarrier*>(context);
        self.entered.store(true, std::memory_order_release);
        while (!self.release.load(std::memory_order_acquire)) std::this_thread::yield();
    }
};

class SharedMemFanOutTest : public ::testing::Test {
protected:
    WriterNames names;
    SharedMemWriter writer;

    void SetUp() override {
        SharedMemWriterTestAccess::configureTransportNames(
            writer, names.legacy, names.event, names.fanOut);
    }
    void TearDown() override { writer.shutdown(); }

    void writeBlock(int frames, int first = 0, int sourceChannels = 2) {
        juce::AudioBuffer<float> audio(sourceChannels, frames);
        for (int i = 0; i < frames; ++i) {
            if (sourceChannels > 0) audio.setSample(0, i, static_cast<float>(first + i));
            if (sourceChannels > 1) audio.setSample(1, i, static_cast<float>(-first - i - 1));
        }
        std::thread audioThread([&] { writer.writeAudio(audio, frames); });
        audioThread.join();
    }

    static void expectStereo(const std::vector<float>& actual, int first) {
        ASSERT_EQ(actual.size() % 2, 0u);
        for (size_t i = 0; i < actual.size() / 2; ++i) {
            EXPECT_EQ(actual[i * 2], static_cast<float>(first + static_cast<int>(i)));
            EXPECT_EQ(actual[i * 2 + 1], static_cast<float>(-first - static_cast<int>(i) - 1));
        }
    }
};

TEST_F(SharedMemFanOutTest, LegacyAndIndependentReadersReceiveIdenticalAudioAtDifferentReadSizes) {
    ASSERT_TRUE(writer.initialize(48000, 2, 256));
    LegacyReader legacy;
    IndependentReader first, second;
    ASSERT_TRUE(legacy.attach(names.legacy));
    ASSERT_TRUE(first.attach(names.fanOut));
    ASSERT_TRUE(second.attach(names.fanOut));
    NamedEvent event;
    ASSERT_TRUE(event.open(names.event));

    writeBlock(96, 100);
    ASSERT_TRUE(first.queue.isReady());
    ASSERT_TRUE(second.queue.isReady());
    EXPECT_TRUE(event.wait(100)); // The legacy notification contract remains.
    std::vector<float> legacyAudio(192), firstAudio(192), secondAudio(192);
    ASSERT_EQ(legacy.queue.read(legacyAudio.data(), 96), 96u);
    ASSERT_EQ(first.queue.read(firstAudio.data(), 31), 31u);
    ASSERT_EQ(first.queue.read(firstAudio.data() + 62, 65), 65u);
    ASSERT_EQ(second.queue.read(secondAudio.data(), 96), 96u);
    EXPECT_EQ(legacyAudio, firstAudio);
    EXPECT_EQ(firstAudio, secondAudio);
    expectStereo(firstAudio, 100);
    EXPECT_EQ(writer.getDroppedFrames(), 0u);
}

TEST_F(SharedMemFanOutTest, FullSlowReaderDoesNotDropFastReadersOrCountUnattachedLegacy) {
    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    IndependentReader slow, fast;
    ASSERT_TRUE(slow.attach(names.fanOut));
    ASSERT_TRUE(fast.attach(names.fanOut));
    std::vector<float> block(128);

    writeBlock(64, 0);
    ASSERT_EQ(fast.queue.read(block.data(), 64), 64u);
    expectStereo(block, 0);
    writeBlock(64, 64);
    ASSERT_EQ(fast.queue.read(block.data(), 64), 64u);
    expectStereo(block, 64);
    EXPECT_EQ(slow.queue.getDroppedFrames(), 64u);
    EXPECT_EQ(fast.queue.getDroppedFrames(), 0u);
    EXPECT_EQ(writer.getDroppedFrames(), 64u); // No legacy listener: no legacy drop noise.

    ASSERT_EQ(slow.queue.read(block.data(), 64), 64u);
    expectStereo(block, 0); // Full queues never overwrite unread PCM.
    writeBlock(32, 128);
    block.resize(64);
    ASSERT_EQ(slow.queue.read(block.data(), 32), 32u);
    expectStereo(block, 128);
    EXPECT_EQ(writer.getDroppedFrames(), 64u);
}

TEST_F(SharedMemFanOutTest, DropsAreSummedPerAttachedDestinationAndStopAfterDetach) {
    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    LegacyReader legacy;
    IndependentReader first, second;
    ASSERT_TRUE(legacy.attach(names.legacy));
    ASSERT_TRUE(first.attach(names.fanOut));
    ASSERT_TRUE(second.attach(names.fanOut));
    writeBlock(64);
    writeBlock(16, 64);
    EXPECT_EQ(writer.getDroppedFrames(), 48u); // Three sinks each miss 16 frames.

    legacy.queue.detach();
    first.queue.detach();
    second.queue.detach();
    writeBlock(16, 80);
    EXPECT_EQ(writer.getDroppedFrames(), 48u);
}

TEST_F(SharedMemFanOutTest, MonoSourceDuplicatesOnceForLegacyAndIndependentStereoStreams) {
    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    LegacyReader legacy;
    IndependentReader independent;
    ASSERT_TRUE(legacy.attach(names.legacy));
    ASSERT_TRUE(independent.attach(names.fanOut));
    writeBlock(32, 7, 1);
    std::vector<float> oldAudio(64), newAudio(64);
    ASSERT_EQ(legacy.queue.read(oldAudio.data(), 32), 32u);
    ASSERT_EQ(independent.queue.read(newAudio.data(), 32), 32u);
    EXPECT_EQ(oldAudio, newAudio);
    for (size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(newAudio[i * 2], static_cast<float>(7 + i));
        EXPECT_EQ(newAudio[i * 2 + 1], newAudio[i * 2]);
    }
}

TEST_F(SharedMemFanOutTest, ShutdownDrainsAdmittedWriteBeforeRetiringBothMappings) {
    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    LegacyReader legacy;
    IndependentReader independent;
    ASSERT_TRUE(legacy.attach(names.legacy));
    ASSERT_TRUE(independent.attach(names.fanOut));
    writeBlock(8);
    ASSERT_TRUE(independent.queue.isReady());

    WriteBarrier barrier;
    SharedMemWriterTestAccess::setWriteBarrier(writer, &WriteBarrier::wait, &barrier);
    juce::AudioBuffer<float> audio(2, 8);
    audio.clear();
    std::thread audioThread([&] { writer.writeAudio(audio, 8); });
    if (!waitUntil([&] { return barrier.entered.load(std::memory_order_acquire); })) {
        barrier.release.store(true, std::memory_order_release);
        audioThread.join();
        FAIL() << "Audio callback did not reach the barrier";
        return;
    }
    std::atomic<bool> stopped{false};
    std::thread controlThread([&] {
        writer.shutdown();
        stopped.store(true, std::memory_order_release);
    });
    const bool admissionClosed = waitUntil([&] { return !writer.isConnected(); });
    EXPECT_TRUE(admissionClosed);
    EXPECT_FALSE(stopped.load(std::memory_order_acquire));
    EXPECT_TRUE(legacy.queue.isProducerActive());
    EXPECT_TRUE(independent.queue.isProducerActive());
    barrier.release.store(true, std::memory_order_release);
    audioThread.join();
    controlThread.join();
    EXPECT_TRUE(stopped.load(std::memory_order_acquire));
    EXPECT_FALSE(legacy.queue.isProducerActive());
    EXPECT_FALSE(independent.queue.isProducerActive());
    SharedMemWriterTestAccess::setWriteBarrier(writer, nullptr, nullptr);
    writeBlock(8); // Closed admission cannot touch either retired view.
}

#if JUCE_WINDOWS
TEST_F(SharedMemFanOutTest, RetainedLegacyMappingDoesNotDisableIndependentTransport) {
    SharedMemory retained;
    ASSERT_TRUE(retained.create(names.legacy, calculateSharedMemorySize(128, 1)));
    RingBuffer oldProducer;
    oldProducer.initAsProducer(retained.getData(), 128, 1, 44100);
    auto* header = static_cast<DirectPipeHeader*>(retained.getData());
    const auto generation = header->producer_generation.load();
    header->read_pos.store(11);
    header->write_pos.store(17);

    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    EXPECT_TRUE(writer.isConnected());
    EXPECT_FALSE(header->producer_active.load());
    EXPECT_EQ(header->sample_rate, 44100u);
    EXPECT_EQ(header->channels, 1u);
    EXPECT_EQ(header->buffer_frames, 128u);
    EXPECT_EQ(header->producer_generation.load(), generation);
    EXPECT_EQ(header->read_pos.load(), 11u);
    EXPECT_EQ(header->write_pos.load(), 17u);
    IndependentReader receiver;
    ASSERT_TRUE(receiver.attach(names.fanOut));
    writeBlock(16, 20);
    std::vector<float> block(32);
    ASSERT_EQ(receiver.queue.read(block.data(), 16), 16u);
    expectStereo(block, 20);
}

TEST_F(SharedMemFanOutTest, RetainedIndependentMappingIsNotReinitializedAndLegacyStillWorks) {
    SharedMemory retained;
    ASSERT_TRUE(retained.create(names.fanOut, calculateFanOutMemorySize(128, 1)));
    FanOutProducer oldProducer;
    ASSERT_TRUE(oldProducer.initialize(retained.getData(), retained.getSize(), 44100, 1, 128));
    auto* header = static_cast<FanOutHeader*>(retained.getData());
    const auto generation = header->generation;
    header->slots[0].writePosition.store(19);

    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    EXPECT_FALSE(header->producerActive.load());
    EXPECT_EQ(header->generation, generation);
    EXPECT_EQ(header->sampleRate, 44100u);
    EXPECT_EQ(header->capacity, 128u);
    EXPECT_EQ(header->channels, 1u);
    EXPECT_EQ(header->slots[0].writePosition.load(), 19u);
    LegacyReader legacy;
    ASSERT_TRUE(legacy.attach(names.legacy));
    writeBlock(16, 30);
    std::vector<float> block(32);
    ASSERT_EQ(legacy.queue.read(block.data(), 16), 16u);
    expectStereo(block, 30);
    oldProducer.shutdown();
}

TEST_F(SharedMemFanOutTest, LegacyEventFailureDoesNotDisableIndependentTransport) {
    auto handle = CreateMutexA(nullptr, FALSE, names.event.c_str());
    ASSERT_NE(handle, nullptr);
    const bool initialized = writer.initialize(48000, 2, 64);
    CloseHandle(handle);
    ASSERT_TRUE(initialized);
    IndependentReader receiver;
    ASSERT_TRUE(receiver.attach(names.fanOut));
    writeBlock(16, 40);
    std::vector<float> block(32);
    ASSERT_EQ(receiver.queue.read(block.data(), 16), 16u);
    expectStereo(block, 40);
}
#endif

TEST_F(SharedMemFanOutTest, RestartAfterReleasedReadersCreatesFreshGenerationsForBothTransports) {
    ASSERT_TRUE(writer.initialize(48000, 2, 64));
    uint64_t oldLegacyGeneration = 0, oldFanOutGeneration = 0;
    {
        LegacyReader legacy;
        IndependentReader independent;
        ASSERT_TRUE(legacy.attach(names.legacy));
        ASSERT_TRUE(independent.attach(names.fanOut));
        oldLegacyGeneration = legacy.queue.getAttachedProducerGeneration();
        oldFanOutGeneration = independent.queue.getAttachedProducerGeneration();
        writer.shutdown();
    }
    ASSERT_TRUE(writer.initialize(44100, 1, 128));
    LegacyReader legacy;
    IndependentReader independent;
    ASSERT_TRUE(legacy.attach(names.legacy));
    ASSERT_TRUE(independent.attach(names.fanOut));
    EXPECT_NE(legacy.queue.getAttachedProducerGeneration(), oldLegacyGeneration);
    EXPECT_NE(independent.queue.getAttachedProducerGeneration(), oldFanOutGeneration);
    EXPECT_EQ(legacy.queue.getSampleRate(), 44100u);
    EXPECT_EQ(independent.queue.getSampleRate(), 44100u);
    writeBlock(16, 50, 1);
    std::vector<float> oldAudio(16), newAudio(16);
    ASSERT_EQ(legacy.queue.read(oldAudio.data(), 16), 16u);
    ASSERT_EQ(independent.queue.read(newAudio.data(), 16), 16u);
    EXPECT_EQ(oldAudio, newAudio);
    for (size_t i = 0; i < 16; ++i) EXPECT_EQ(newAudio[i], static_cast<float>(50 + i));
}

} // namespace
