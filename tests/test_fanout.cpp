// SPDX-License-Identifier: GPL-3.0-or-later
// Core transport invariants in aligned process-local memory. Windows-only cases
// below use uniquely named test mappings and a separate child process; neither
// production endpoint nor an installed Receiver is opened by this suite.
#include <gtest/gtest.h>
#include "directpipe/FanOut.h"
#include "directpipe/SharedMemory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace directpipe;

namespace {
class FanOutTest : public ::testing::Test {
protected:
    static constexpr uint32_t capacity = 1024;
    static constexpr uint32_t channels = 2;
    void SetUp() override {
        bytes = calculateFanOutMemorySize(capacity, channels);
        storage.resize(bytes + 64);
        size_t space = storage.size();
        void* raw = storage.data();
        memory = std::align(64, bytes, raw, space);
        ASSERT_NE(memory, nullptr);
        ASSERT_TRUE(producer.initialize(memory, bytes, 48000, channels, capacity));
    }
    void TearDown() override { producer.shutdown(); }
    void connect(FanOutConsumer& consumer) {
        ASSERT_EQ(consumer.claim(memory, bytes), FanOutAttachResult::Waiting);
        producer.write(nullptr, 0);
        ASSERT_TRUE(consumer.isReady());
    }
    std::vector<float> signal(uint32_t first, uint32_t count) {
        std::vector<float> values(static_cast<size_t>(count) * channels);
        for (uint32_t i = 0; i < count; ++i) {
            values[i * 2] = static_cast<float>(first + i);
            values[i * 2 + 1] = -static_cast<float>(first + i);
        }
        return values;
    }
    void expectSignal(const float* data, uint32_t first, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            ASSERT_EQ(data[i * 2], static_cast<float>(first + i));
            ASSERT_EQ(data[i * 2 + 1], -static_cast<float>(first + i));
        }
    }
    std::vector<uint8_t> storage;
    size_t bytes = 0;
    void* memory = nullptr;
    FanOutProducer producer;
};
} // namespace

TEST_F(FanOutTest, ClaimWaitsForWriterAcknowledgement) {
    auto data = signal(1, 64);
    EXPECT_EQ(producer.write(data.data(), 64).activeReaders, 0u);
    FanOutConsumer consumer;
    ASSERT_EQ(consumer.claim(memory, bytes), FanOutAttachResult::Waiting);
    EXPECT_FALSE(consumer.isReady());
    EXPECT_EQ(consumer.read(data.data(), 64), 0u);
    EXPECT_TRUE(FanOutConsumer::isAvailable(memory, bytes));
    EXPECT_EQ(producer.write(nullptr, 0).activeReaders, 1u);
    EXPECT_TRUE(consumer.isReady());
    EXPECT_EQ(consumer.availableRead(), 0u); // Pre-claim audio is never replayed.
    EXPECT_EQ(consumer.getSampleRate(), 48000u);
    EXPECT_EQ(consumer.getChannels(), channels);
    EXPECT_EQ(consumer.getCapacity(), capacity);
    EXPECT_EQ(consumer.getAttachedProducerGeneration(), producer.getGeneration());
}

TEST_F(FanOutTest, IndependentMixedReadsRemainBitExactThroughWrap) {
    FanOutConsumer a, b;
    connect(a); connect(b);
    std::array<float, 256> out{};
    for (uint32_t block = 0; block < 80; ++block) {
        const uint32_t first = block * 128 + 1;
        auto data = signal(first, 128);
        const auto result = producer.write(data.data(), 128);
        ASSERT_EQ(result.activeReaders, 2u);
        ASSERT_EQ(result.droppedFrames, 0u);
        ASSERT_EQ(a.read(out.data(), 128), 128u);
        expectSignal(out.data(), first, 128);
        uint32_t consumed = 0;
        while (consumed < 128) {
            const auto n = b.read(out.data(), (std::min)(31u, 128 - consumed));
            ASSERT_GT(n, 0u);
            expectSignal(out.data(), first + consumed, n);
            consumed += n;
        }
    }
}

TEST_F(FanOutTest, SlowReaderDropsOnlyItsOwnFramesAndSignalsFreshnessLoss) {
    FanOutConsumer fast, paused;
    connect(fast); connect(paused);
    std::array<float, 256> out{};
    for (uint32_t block = 0; block < 12; ++block) {
        auto data = signal(block * 128 + 1, 128);
        auto result = producer.write(data.data(), 128);
        EXPECT_EQ(result.droppedFrames, block < 8 ? 0u : 128u);
        ASSERT_EQ(fast.read(out.data(), 128), 128u);
        expectSignal(out.data(), block * 128 + 1, 128);
    }
    EXPECT_EQ(fast.getDroppedFrames(), 0u);
    EXPECT_EQ(paused.getDroppedFrames(), 512u);
    EXPECT_EQ(paused.getOverflowEpoch(), 4u);
    EXPECT_EQ(paused.discard((std::numeric_limits<uint32_t>::max)()), capacity);
    auto latest = signal(90000, 128);
    producer.write(latest.data(), 128);
    ASSERT_EQ(paused.read(out.data(), 128), 128u);
    expectSignal(out.data(), 90000, 128);
    EXPECT_EQ(fast.availableRead(), 128u); // Discard affects only paused.
}

TEST_F(FanOutTest, EightReadersAreBoundedAndLiveIdleOwnersAreNeverStolen) {
    std::array<FanOutConsumer, FANOUT_MAX_READERS> readers;
    for (auto& reader : readers) connect(reader);
    FanOutConsumer ninth;
    for (int retry = 0; retry < 50; ++retry)
        EXPECT_EQ(ninth.claim(memory, bytes), FanOutAttachResult::Full);
    auto data = signal(1, 64);
    EXPECT_EQ(producer.write(data.data(), 64).activeReaders, FANOUT_MAX_READERS);
    for (auto& reader : readers) EXPECT_EQ(reader.availableRead(), 64u);
}

TEST_F(FanOutTest, ReusedSlotWaitsForNewAckAndDoesNotResetOtherReaders) {
    FanOutConsumer old, other, replacement;
    connect(old); connect(other);
    auto data = signal(1, 100);
    producer.write(data.data(), 100);
    const auto priorAck = static_cast<FanOutHeader*>(memory)->slots[0].acknowledgedToken.load();
    old.detach();
    ASSERT_EQ(replacement.claim(memory, bytes), FanOutAttachResult::Waiting);
    EXPECT_FALSE(replacement.isReady());
    EXPECT_NE(static_cast<FanOutHeader*>(memory)->slots[0].ownerToken.load(), priorAck);
    old.detach(); // Idempotent old owner cannot release replacement.
    producer.write(nullptr, 0);
    EXPECT_TRUE(replacement.isReady());
    EXPECT_EQ(replacement.availableRead(), 0u);
    EXPECT_EQ(other.availableRead(), 100u);
    data = signal(500, 100);
    producer.write(data.data(), 100);
    std::array<float, 200> out{};
    ASSERT_EQ(replacement.read(out.data(), 100), 100u);
    expectSignal(out.data(), 500, 100);
    ASSERT_EQ(other.read(out.data(), 100), 100u);
    expectSignal(out.data(), 1, 100);
}

TEST_F(FanOutTest, SimultaneousClaimsNeverShareSlots) {
    std::array<FanOutConsumer, 16> readers;
    std::array<FanOutAttachResult, 16> results{};
    std::array<std::thread, 16> threads;
    std::atomic<bool> go{false};
    for (size_t i = 0; i < threads.size(); ++i)
        threads[i] = std::thread([&, i] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            results[i] = readers[i].claim(memory, bytes);
        });
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(std::count(results.begin(), results.end(), FanOutAttachResult::Waiting), 8);
    EXPECT_EQ(std::count(results.begin(), results.end(), FanOutAttachResult::Full), 8);
    EXPECT_EQ(producer.write(nullptr, 0).activeReaders, 8u);
    EXPECT_EQ(std::count_if(readers.begin(), readers.end(), [](const auto& c) { return c.isReady(); }), 8);
}

TEST_F(FanOutTest, ConcurrentReadersReceiveIndependentStereoStreams) {
    constexpr uint32_t total = 32768;
    std::array<FanOutConsumer, 8> readers;
    for (auto& reader : readers) connect(reader);
    std::array<std::thread, 8> threads;
    std::atomic<bool> failure{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (size_t index = 0; index < readers.size(); ++index) {
        threads[index] = std::thread([&, index] {
            std::array<float, 254> samples{};
            uint32_t position = 0;
            while (position < total && !failure.load()) {
                const auto count = readers[index].read(samples.data(), static_cast<uint32_t>(17 + index * 13));
                for (uint32_t i = 0; i < count; ++i)
                    if (samples[i * 2] != static_cast<float>(position + i + 1)
                        || samples[i * 2 + 1] != -static_cast<float>(position + i + 1)) failure.store(true);
                position += count;
                if (std::chrono::steady_clock::now() > deadline) failure.store(true);
                if (count == 0) std::this_thread::yield();
            }
        });
    }
    for (uint32_t position = 0; position < total && !failure.load();) {
        bool room = true;
        for (const auto& reader : readers) room = room && reader.availableRead() <= capacity - 128;
        if (room) {
            auto data = signal(position + 1, 128);
            if (producer.write(data.data(), 128).droppedFrames != 0) failure.store(true);
            position += 128;
        } else std::this_thread::yield();
        if (std::chrono::steady_clock::now() > deadline) failure.store(true);
    }
    for (auto& thread : threads) thread.join();
    EXPECT_FALSE(failure.load());
}

TEST_F(FanOutTest, RetirementStopsReadsWithoutReinitializingRetainedMapping) {
    FanOutConsumer reader;
    connect(reader);
    const auto generation = reader.getAttachedProducerGeneration();
    auto data = signal(1, 64);
    producer.write(data.data(), 64);
    producer.shutdown();
    EXPECT_FALSE(reader.isProducerActive());
    EXPECT_FALSE(reader.isReady());
    EXPECT_EQ(reader.read(data.data(), 64), 0u);
    EXPECT_EQ(reader.getCurrentProducerGeneration(), generation);
    FanOutConsumer another;
    EXPECT_EQ(another.claim(memory, bytes), FanOutAttachResult::Inactive);
    EXPECT_FALSE(FanOutConsumer::isAvailable(memory, bytes));
    reader.detach();
}

TEST_F(FanOutTest, MalformedMappingsAndSizesFailBeforeAnyClaimOrPcmAccess) {
    FanOutConsumer reader;
    EXPECT_EQ(reader.claim(nullptr, bytes), FanOutAttachResult::Invalid);
    EXPECT_EQ(reader.claim(static_cast<uint8_t*>(memory) + 1, bytes), FanOutAttachResult::Invalid);
    EXPECT_EQ(reader.claim(memory, sizeof(FanOutHeader) - 1), FanOutAttachResult::Invalid);
    EXPECT_EQ(reader.claim(memory, bytes - 1), FanOutAttachResult::Invalid);
    auto* header = static_cast<FanOutHeader*>(memory);
    const auto check = [&](uint32_t& field, uint32_t value) {
        const auto saved = field;
        field = value;
        EXPECT_EQ(reader.claim(memory, bytes), FanOutAttachResult::Invalid);
        field = saved;
    };
    check(header->magic, 0);
    check(header->version, FANOUT_VERSION + 1);
    check(header->headerBytes, 1);
    check(header->slotCount, 9);
    check(header->capacity, 0);
    check(header->capacity, 3);
    check(header->capacity, (std::numeric_limits<uint32_t>::max)());
    check(header->channels, 0);
    check(header->channels, 3);
    check(header->channels, 9);
    check(header->sampleRate, 0);
    header->mappingBytes = (std::numeric_limits<uint64_t>::max)();
    EXPECT_EQ(reader.claim(memory, bytes), FanOutAttachResult::Invalid);
    EXPECT_EQ(header->nextClaimId.load(), 1u);
}

TEST_F(FanOutTest, ClaimSerialExhaustionFailsClosedWithoutTokenWrap) {
    auto* header = static_cast<FanOutHeader*>(memory);
    header->nextClaimId.store((std::numeric_limits<uint32_t>::max)());
    FanOutConsumer last, exhausted;
    EXPECT_EQ(last.claim(memory, bytes), FanOutAttachResult::Waiting);
    EXPECT_EQ(exhausted.claim(memory, bytes), FanOutAttachResult::Invalid);
    header->nextClaimId.store((std::numeric_limits<uint64_t>::max)());
    EXPECT_EQ(exhausted.claim(memory, bytes), FanOutAttachResult::Invalid);
    EXPECT_EQ(header->nextClaimId.load(), (std::numeric_limits<uint64_t>::max)());
}

TEST_F(FanOutTest, CorruptPositionsFailClosedAndLargeWriteDropsAreBounded) {
    FanOutConsumer reader;
    connect(reader);
    auto data = signal(1, capacity + 10);
    const auto result = producer.write(data.data(), capacity + 10);
    EXPECT_EQ(result.writtenFrames, capacity);
    EXPECT_EQ(result.droppedFrames, 10u);
    auto* header = static_cast<FanOutHeader*>(memory);
    header->slots[0].readPosition.store(capacity + 1);
    EXPECT_EQ(reader.availableRead(), 0u);
    EXPECT_EQ(reader.read(data.data(), 1), 0u);
    EXPECT_EQ(producer.write(data.data(), 1).droppedFrames, 1u);
}

#ifdef _WIN32
namespace {
std::string uniqueFanOutName() {
    static std::atomic<unsigned> sequence{0};
    return "Local\\DirectPipeFanOutTest_" + std::to_string(GetCurrentProcessId()) + "_"
        + std::to_string(GetTickCount64()) + "_" + std::to_string(sequence.fetch_add(1));
}

struct ChildProcess {
    // Retain the exact spawned process handle; never find/kill by executable name.
    // RAII cleanup also runs after a fatal assertion returns from a test body.
    HANDLE process = nullptr;
    DWORD exitCode() const {
        DWORD code = 0;
        return GetExitCodeProcess(process, &code) ? code : GetLastError();
    }
    ~ChildProcess() {
        if (process) {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process, 99);
                WaitForSingleObject(process, 5000);
            }
            CloseHandle(process);
        }
    }
    bool start(const std::string& mapping, const std::string& mode) {
        char module[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, module, MAX_PATH)) return false;
        std::string path(module);
        path.resize(path.find_last_of("/\\") + 1);
        path += "fanout-test-child.exe";
        std::string command = "\"" + path + "\" \"" + mapping + "\" " + mode;
        std::vector<char> args(command.begin(), command.end());
        args.push_back(0);
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessA(nullptr, args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &info)) return false;
        process = info.hProcess;
        CloseHandle(info.hThread);
        return true;
    }
};
} // namespace

TEST(FanOutCrossProcess, ChildReadsSameStereoFramesAsIndependentLocalReader) {
    const auto name = uniqueFanOutName();
    const size_t bytes = calculateFanOutMemorySize(1024, 2);
    SharedMemory mapping;
    ASSERT_TRUE(mapping.create(name, bytes));
    ASSERT_FALSE(mapping.createOpenedExistingObject());
    FanOutProducer producer;
    ASSERT_TRUE(producer.initialize(mapping.getData(), mapping.getSize(), 48000, 2, 1024));
    NamedEvent ready, proceed;
    ASSERT_TRUE(ready.create(name + "_ready"));
    ASSERT_TRUE(proceed.create(name + "_go"));
    FanOutConsumer local;
    ASSERT_EQ(local.claim(mapping.getData(), mapping.getSize()), FanOutAttachResult::Waiting);
    ChildProcess child;
    ASSERT_TRUE(child.start(name, "read")) << "Build fanout-test-child target: " << GetLastError();
    ASSERT_TRUE(ready.wait(5000)) << "Child exit code (259 means running): " << child.exitCode();
    std::array<float, 512> audio{};
    for (size_t i = 0; i < audio.size(); ++i) audio[i] = static_cast<float>(i + 1);
    EXPECT_EQ(producer.write(audio.data(), 256).activeReaders, 2u);
    proceed.signal();
    std::array<float, 512> received{};
    EXPECT_EQ(local.read(received.data(), 256), 256u);
    EXPECT_EQ(received, audio);
    ASSERT_EQ(WaitForSingleObject(child.process, 5000), WAIT_OBJECT_0);
    DWORD exitCode = 99;
    ASSERT_TRUE(GetExitCodeProcess(child.process, &exitCode));
    EXPECT_EQ(exitCode, 0u);
    producer.shutdown();
}

TEST(FanOutCrossProcess, LiveChildIsNotReclaimedButTerminatedChildSlotIsReusable) {
    const auto name = uniqueFanOutName();
    const size_t bytes = calculateFanOutMemorySize(1024, 2);
    SharedMemory mapping;
    ASSERT_TRUE(mapping.create(name, bytes));
    ASSERT_FALSE(mapping.createOpenedExistingObject());
    FanOutProducer producer;
    ASSERT_TRUE(producer.initialize(mapping.getData(), mapping.getSize(), 48000, 2, 1024));
    std::array<FanOutConsumer, 7> local;
    for (auto& reader : local)
        ASSERT_EQ(reader.claim(mapping.getData(), mapping.getSize()), FanOutAttachResult::Waiting);
    NamedEvent ready, proceed;
    ASSERT_TRUE(ready.create(name + "_ready"));
    ASSERT_TRUE(proceed.create(name + "_go"));
    ChildProcess child;
    ASSERT_TRUE(child.start(name, "idle"));
    ASSERT_TRUE(ready.wait(5000)) << "Child exit code (259 means running): " << child.exitCode();
    EXPECT_EQ(producer.write(nullptr, 0).activeReaders, 8u);
    FanOutConsumer replacement;
    EXPECT_EQ(replacement.claim(mapping.getData(), mapping.getSize()), FanOutAttachResult::Full);
    ASSERT_TRUE(TerminateProcess(child.process, 17));
    ASSERT_EQ(WaitForSingleObject(child.process, 5000), WAIT_OBJECT_0);
    ASSERT_EQ(replacement.claim(mapping.getData(), mapping.getSize()), FanOutAttachResult::Waiting);
    EXPECT_FALSE(replacement.isReady());
    const std::array<float, 2> audio{0.25f, -0.75f};
    EXPECT_EQ(producer.write(audio.data(), 1).activeReaders, 8u);
    std::array<float, 2> out{};
    EXPECT_EQ(replacement.read(out.data(), 1), 1u);
    EXPECT_EQ(out, audio);
    for (auto& reader : local) EXPECT_EQ(reader.availableRead(), 1u);
    producer.shutdown();
}
#endif
