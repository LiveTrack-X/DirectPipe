// SPDX-License-Identifier: GPL-3.0-or-later
// Calls the actual Receiver callback using in-memory legacy/FanOut sources and
// selected real-worker cases with unique test mapping names. The installed plugin,
// production IPC endpoints, external hosts and audio devices are not exercised.
#include <gtest/gtest.h>
#include "../plugins/receiver/Source/PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

class DirectPipeReceiverProcessorTestAccess {
public:
    static void prepare(DirectPipeReceiverProcessor& processor, int blockSize) {
        processor.isolatedConnectionForTest_ = true;
        processor.setRateAndBufferSizeDetails(48000.0, blockSize);
        processor.prepareToPlay(48000.0, blockSize);
    }

    static bool connect(DirectPipeReceiverProcessor& processor, void* memory, size_t size) {
        processor.retireConnection();
        auto connection = std::make_unique<DirectPipeReceiverProcessor::Connection>();
        if (!connection->ringBuffer.attachAsConsumer(memory, size))
            return false;
        processor.skipToFreshPosition(*connection);
        processor.publishConnection(std::move(connection));
        return true;
    }

    static bool reconnectRequested(const DirectPipeReceiverProcessor& processor) {
        return processor.reconnectRequested_.load();
    }

    static bool connectFanOut(DirectPipeReceiverProcessor& processor, void* memory,
                             size_t size, directpipe::FanOutProducer& producer, int prefill = 0) {
        processor.retireConnection();
        auto connection = std::make_unique<DirectPipeReceiverProcessor::Connection>();
        const auto result = connection->fanOut.claim(memory, size);
        if (result != directpipe::FanOutAttachResult::Waiting)
            return false;
        connection->usesFanOut = true;
        producer.write(nullptr, 0);
        if (!connection->isReady())
            return false;
        if (prefill > 0) {
            std::vector<float> audio(static_cast<size_t>(prefill) * 2, 0.5f);
            producer.write(audio.data(), static_cast<uint32_t>(prefill));
        }
        processor.publishConnection(std::move(connection));
        return true;
    }

    static uint32_t available(const DirectPipeReceiverProcessor& processor) {
        return processor.workerConnection_ ? processor.workerConnection_->availableRead() : 0;
    }

    static void startNamedWorker(DirectPipeReceiverProcessor& processor, const std::string& fanOutName,
                                 const std::string& legacyName) {
        processor.fanOutMappingName_ = fanOutName;
        processor.legacyMappingName_ = legacyName;
        processor.isolatedConnectionForTest_ = false;
        processor.startConnectionWorker();
    }

    static void disconnect(DirectPipeReceiverProcessor& processor) { processor.retireConnection(); }

    static bool openNamed(DirectPipeReceiverProcessor& processor, const std::string& fanOutName,
                          const std::string& legacyName, directpipe::FanOutProducer* producer = nullptr) {
        processor.retireConnection();
        processor.fanOutMappingName_ = fanOutName;
        processor.legacyMappingName_ = legacyName;
        auto connection = processor.openConnection();
        if (!connection)
            return false;
        if (producer)
            producer->write(nullptr, 0);
        if (!connection->isReady())
            return false;
        processor.publishConnection(std::move(connection));
        return true;
    }
};

class ReceiverProcessorTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 16384;
    static constexpr size_t kMemorySize = directpipe::calculateSharedMemorySize(kCapacity, 2);
    juce::ScopedJuceInitialiser_GUI juceInitialiser_;
    std::vector<uint8_t> memory_;
    void* alignedMemory_ = nullptr;
    directpipe::RingBuffer producer_;
    DirectPipeReceiverProcessor processor_;
    juce::MidiBuffer midi_;

    void SetUp() override {
        memory_.resize(kMemorySize + 64, 0);
        void* raw = memory_.data();
        auto space = memory_.size();
        alignedMemory_ = std::align(64, kMemorySize, raw, space);
        ASSERT_NE(alignedMemory_, nullptr);
        producer_.initAsProducer(alignedMemory_, kCapacity, 2, 48000);
        DirectPipeReceiverProcessorTestAccess::prepare(processor_, 128);
        ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::connect(processor_, alignedMemory_, kMemorySize));
    }

    uint32_t write(float value, int samples) {
        std::vector<float> input(static_cast<size_t>(samples) * 2, value);
        return producer_.write(input.data(), static_cast<uint32_t>(samples));
    }

    juce::AudioBuffer<float> render(int samples) {
        juce::AudioBuffer<float> output(2, samples);
        // A nonzero sentinel catches paths that leave host buffer contents untouched.
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill(output.getWritePointer(ch), 0.9f, samples);
        processor_.processBlock(output, midi_);
        return output;
    }

    void warmUp(float value = 0.25f) {
        for (int i = 0; i < 60; ++i) {
            ASSERT_EQ(write(value, 128), 128u);
            render(128);
        }
    }

    void mute(bool value) {
        processor_.getAPVTS().getParameter("mute")->setValueNotifyingHost(value ? 1.0f : 0.0f);
    }
};

TEST_F(ReceiverProcessorTest, LowBufferTrimLeavesEnoughForLargerCallback) {
    warmUp(0.5f);
    ASSERT_EQ(write(0.5f, 2048), 2048u);
    auto output = render(1024);
    for (int i = 64; i < 1024; ++i)
        ASSERT_FLOAT_EQ(output.getSample(0, i), 0.5f) << "sample " << i;
    EXPECT_EQ(producer_.availableRead(), 0u);
}

TEST_F(ReceiverProcessorTest, OversizedAndMixedCallbacksConsumeEveryAvailableFrame) {
    warmUp(0.4f);
    for (const int samples : { 32, 1, 256, 2048, 64, 1024, 17 }) {
        ASSERT_EQ(write(0.4f, samples), static_cast<uint32_t>(samples));
        auto output = render(samples);
        for (int i = 0; i < samples; ++i)
            ASSERT_FLOAT_EQ(output.getSample(0, i), 0.4f) << "block " << samples << ", sample " << i;
        EXPECT_EQ(producer_.availableRead(), 0u);
    }
}

TEST_F(ReceiverProcessorTest, SafeBufferDoesNotThrottleAFullyAvailableSmallBlock) {
    warmUp(0.4f);
    processor_.getAPVTS().getParameter("buffer")->setValueNotifyingHost(1.0f);
    ASSERT_EQ(write(0.4f, 32), 32u);
    auto output = render(32);
    for (int i = 0; i < 32; ++i)
        EXPECT_FLOAT_EQ(output.getSample(0, i), 0.4f);
    EXPECT_EQ(producer_.availableRead(), 0u);
}

TEST_F(ReceiverProcessorTest, MutedReceiverDrainsOldSpeechAndResumesCurrentAudio) {
    warmUp();
    mute(true);
    for (int i = 0; i < 3750; ++i) {
        write(0.25f, 128);
        auto output = render(128);
        ASSERT_FLOAT_EQ(output.getMagnitude(0, 128), 0.0f);
    }
    EXPECT_EQ(producer_.availableRead(), 0u);
    mute(false);
    ASSERT_EQ(write(-0.5f, 128), 128u);
    auto resumed = render(128);
    EXPECT_FLOAT_EQ(resumed.getSample(0, 127), -0.5f);
    EXPECT_EQ(producer_.availableRead(), 0u);
}

TEST_F(ReceiverProcessorTest, PartialUnderrunFadesFromLastDeliveredSample) {
    warmUp(0.5f);
    ASSERT_EQ(write(0.5f, 32), 32u);
    auto output = render(128);
    EXPECT_FLOAT_EQ(output.getSample(0, 31), 0.5f);
    EXPECT_FLOAT_EQ(output.getSample(0, 32), output.getSample(0, 31));
    EXPECT_GT(output.getSample(0, 33), 0.0f);
    EXPECT_FLOAT_EQ(output.getSample(0, 127), 0.0f);
}

TEST_F(ReceiverProcessorTest, TotalUnderrunDoesNotReplayAnEarlierTail) {
    warmUp();
    std::vector<float> input(128 * 2);
    for (int i = 0; i < 128; ++i)
        input[static_cast<size_t>(i) * 2] = input[static_cast<size_t>(i) * 2 + 1]
            = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 1000.0f * static_cast<float>(i) / 48000.0f);
    ASSERT_EQ(producer_.write(input.data(), 128), 128u);
    auto before = render(128);
    auto after = render(128);
    EXPECT_FLOAT_EQ(after.getSample(0, 0), before.getSample(0, 127));
    EXPECT_FLOAT_EQ(after.getSample(0, 127), 0.0f);
    EXPECT_LE(after.getMagnitude(0, 128), 0.5f);
}

TEST_F(ReceiverProcessorTest, FadeSpansSmallCallbacksAndRecoveryStartsContinuously) {
    warmUp(0.5f);
    float previous = 0.5f;
    for (int block = 0; block < 8; ++block) {
        auto output = render(8);
        for (int i = 0; i < 8; ++i) {
            EXPECT_LE(output.getSample(0, i), previous);
            EXPECT_GE(output.getSample(0, i), 0.0f);
            previous = output.getSample(0, i);
        }
    }
    EXPECT_FLOAT_EQ(previous, 0.0f);
    ASSERT_EQ(write(-0.5f, 128), 128u);
    auto resumed = render(128);
    EXPECT_FLOAT_EQ(resumed.getSample(0, 0), previous);
    EXPECT_FLOAT_EQ(resumed.getSample(0, 127), -0.5f);
}

TEST_F(ReceiverProcessorTest, ProducerReplacementRequestsReconnectAndUsesNewGeneration) {
    warmUp(0.5f);
    // Simulate an older producer's in-place restart between synchronous callbacks.
    // Current host transport creation must instead wait for a fresh mapping.
    producer_.initAsProducer(alignedMemory_, kCapacity, 2, 48000);
    auto retiring = render(128);
    EXPECT_TRUE(DirectPipeReceiverProcessorTestAccess::reconnectRequested(processor_));
    EXPECT_FLOAT_EQ(retiring.getSample(0, 127), 0.0f);
    ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::connect(processor_, alignedMemory_, kMemorySize));
    ASSERT_EQ(write(-0.25f, 128), 128u);
    auto resumed = render(128);
    EXPECT_FLOAT_EQ(resumed.getSample(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(resumed.getSample(0, 127), -0.25f);
}

TEST_F(ReceiverProcessorTest, ReconnectDuringFadeStaysContinuousEvenBeforeNewAudioArrives) {
    warmUp(0.5f);
    const auto fading = render(8);
    // Same isolated legacy-restart simulation; no concurrent reader runs here.
    producer_.initAsProducer(alignedMemory_, kCapacity, 2, 48000);
    ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::connect(processor_, alignedMemory_, kMemorySize));
    auto waiting = render(8);
    EXPECT_FLOAT_EQ(waiting.getSample(0, 0), fading.getSample(0, 7));
    EXPECT_LE(waiting.getMagnitude(0, 8), 0.5f);
    ASSERT_EQ(write(-0.5f, 128), 128u);
    auto resumed = render(128);
    EXPECT_FLOAT_EQ(resumed.getSample(0, 0), waiting.getSample(0, 7));
    EXPECT_FLOAT_EQ(resumed.getSample(0, 127), -0.5f);
    EXPECT_LE(resumed.getMagnitude(0, 128), 0.5f);
}

TEST_F(ReceiverProcessorTest, ChunkedReadsPreserveStereoAndSupportedMonoOutput) {
    warmUp();
    std::vector<float> input(257 * 2);
    for (int i = 0; i < 257; ++i) {
        input[static_cast<size_t>(i) * 2] = static_cast<float>(i) / 512.0f;
        input[static_cast<size_t>(i) * 2 + 1] = -static_cast<float>(i) / 1024.0f;
    }
    ASSERT_EQ(producer_.write(input.data(), 257), 257u);
    auto stereo = render(257);
    for (int i = 0; i < 257; ++i) {
        EXPECT_FLOAT_EQ(stereo.getSample(0, i), input[static_cast<size_t>(i) * 2]);
        EXPECT_FLOAT_EQ(stereo.getSample(1, i), input[static_cast<size_t>(i) * 2 + 1]);
    }

    auto layout = processor_.getBusesLayout();
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    ASSERT_TRUE(processor_.setBusesLayout(layout));
    ASSERT_EQ(producer_.write(input.data(), 257), 257u);
    juce::AudioBuffer<float> mono(1, 257);
    processor_.processBlock(mono, midi_);
    for (int i = 0; i < 257; ++i)
        EXPECT_FLOAT_EQ(mono.getSample(0, i), input[static_cast<size_t>(i) * 2]);
    EXPECT_EQ(producer_.availableRead(), 0u);
}

TEST_F(ReceiverProcessorTest, EmptyCallbackDoesNotConsumeOrResetAudioState) {
    warmUp(0.5f);
    ASSERT_EQ(write(0.5f, 128), 128u);
    render(0);
    EXPECT_EQ(producer_.availableRead(), 128u);
    auto output = render(128);
    EXPECT_FLOAT_EQ(output.getSample(0, 0), 0.5f);
    EXPECT_FLOAT_EQ(output.getSample(0, 127), 0.5f);
}

class ReceiverFanOutTest : public ::testing::Test {
protected:
    static constexpr uint32_t kCapacity = 4096;
    juce::ScopedJuceInitialiser_GUI juceInitialiser_;
    std::vector<uint8_t> memory_;
    void* aligned_ = nullptr;
    size_t bytes_ = 0;
    directpipe::FanOutProducer producer_;
    DirectPipeReceiverProcessor first_, second_;
    juce::MidiBuffer midi_;

    void SetUp() override {
        bytes_ = directpipe::calculateFanOutMemorySize(kCapacity, 2);
        memory_.resize(bytes_ + 64);
        void* raw = memory_.data();
        auto space = memory_.size();
        aligned_ = std::align(64, bytes_, raw, space);
        ASSERT_NE(aligned_, nullptr);
        ASSERT_TRUE(producer_.initialize(aligned_, bytes_, 48000, 2, kCapacity));
        for (auto* processor : { &first_, &second_ }) {
            DirectPipeReceiverProcessorTestAccess::prepare(*processor, 64);
            ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::connectFanOut(*processor, aligned_, bytes_, producer_));
        }
        write(0.25f, 128);
        render(first_, 128);
        render(second_, 128);
    }

    directpipe::FanOutWriteResult write(float value, int frames) {
        std::vector<float> input(static_cast<size_t>(frames) * 2, value);
        return producer_.write(input.data(), static_cast<uint32_t>(frames));
    }

    juce::AudioBuffer<float> render(DirectPipeReceiverProcessor& processor, int frames) {
        juce::AudioBuffer<float> output(2, frames);
        processor.processBlock(output, midi_);
        return output;
    }

    void expectConstant(const juce::AudioBuffer<float>& output, float expected, int begin = 0) {
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            for (int i = begin; i < output.getNumSamples(); ++i)
                ASSERT_FLOAT_EQ(output.getSample(ch, i), expected) << ch << ":" << i;
    }
};

TEST_F(ReceiverFanOutTest, DifferentCallbacksReceiveTheSameCompleteStream) {
    for (int block = 0; block < 20; ++block) {
        const float value = static_cast<float>(block) / 32.0f;
        EXPECT_EQ(write(value, 256).activeReaders, 2u);
        for (int i = 0; i < 4; ++i)
            expectConstant(render(first_, 64), value);
        expectConstant(render(second_, 256), value);
    }
    EXPECT_EQ(first_.getConnectionState(), DirectPipeReceiverProcessor::ConnectionState::Connected);
    EXPECT_FALSE(first_.hasMultiConsumerWarning());
    EXPECT_FALSE(second_.hasMultiConsumerWarning());
}

TEST_F(ReceiverFanOutTest, MutingOneReceiverDoesNotConsumeTheOtherReceiversAudio) {
    second_.getAPVTS().getParameter("mute")->setValueNotifyingHost(1.0f);
    for (int block = 0; block < 80; ++block) {
        const float value = block % 2 ? 0.5f : -0.5f;
        EXPECT_EQ(write(value, 128).droppedFrames, 0u);
        expectConstant(render(second_, 128), 0.0f);
        expectConstant(render(first_, 128), value);
    }
    second_.getAPVTS().getParameter("mute")->setValueNotifyingHost(0.0f);
    write(-0.75f, 128);
    expectConstant(render(first_, 128), -0.75f);
    expectConstant(render(second_, 128), -0.75f, 64);
}

TEST_F(ReceiverFanOutTest, SuspendedFullReceiverDiscardsOldSpeechWithoutDisruptingActiveReader) {
    uint64_t dropped = 0;
    for (int block = 0; block < 80; ++block) {
        dropped += write(0.75f, 256).droppedFrames;
        expectConstant(render(first_, 256), 0.75f);
    }
    EXPECT_GT(dropped, 0u);
    // The old queued 0.75 speech must not return. A bounded tail from the last
    // actually emitted 0.25 sample is permitted, then exact silence.
    const auto resumed = render(second_, 128);
    EXPECT_LE(resumed.getMagnitude(0, 128), 0.25f);
    expectConstant(resumed, 0.0f, 64);
    write(-0.5f, 128);
    expectConstant(render(first_, 128), -0.5f);
    expectConstant(render(second_, 128), -0.5f, 64);
}

TEST_F(ReceiverFanOutTest, DisconnectAndReplacementLeaveTheOtherReaderContinuous) {
    DirectPipeReceiverProcessorTestAccess::disconnect(second_);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(write(0.4f, 128).activeReaders, 1u);
        expectConstant(render(first_, 128), 0.4f);
    }
    ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::connectFanOut(second_, aligned_, bytes_, producer_));
    EXPECT_EQ(write(-0.6f, 128).activeReaders, 2u);
    expectConstant(render(first_, 128), -0.6f);
    expectConstant(render(second_, 128), -0.6f, 64);
}

TEST_F(ReceiverFanOutTest, FullTransportNeverFallsBackToTheLegacyQueue) {
    const auto suffix = juce::Uuid().toString().removeCharacters("-").substring(0, 12).toStdString();
    const auto fanOutName = std::string("Local\\DPRF_") + suffix;
    const auto legacyName = std::string("Local\\DPRL_") + suffix;
    directpipe::SharedMemory shared, legacy;
    ASSERT_TRUE(shared.create(fanOutName, bytes_));
    const auto legacyBytes = directpipe::calculateSharedMemorySize(kCapacity, 2);
    ASSERT_TRUE(legacy.create(legacyName, legacyBytes));
    directpipe::FanOutProducer namedProducer;
    directpipe::RingBuffer legacyProducer;
    ASSERT_TRUE(namedProducer.initialize(shared.getData(), shared.getSize(), 48000, 2, kCapacity));
    legacyProducer.initAsProducer(legacy.getData(), kCapacity, 2, 48000);
    std::array<directpipe::FanOutConsumer, directpipe::FANOUT_MAX_READERS> readers;
    for (auto& reader : readers)
        ASSERT_EQ(reader.claim(shared.getData(), shared.getSize()), directpipe::FanOutAttachResult::Waiting);
    namedProducer.write(nullptr, 0);
    DirectPipeReceiverProcessor extra;
    DirectPipeReceiverProcessorTestAccess::prepare(extra, 128);
    EXPECT_FALSE(DirectPipeReceiverProcessorTestAccess::openNamed(extra, fanOutName, legacyName, &namedProducer));
    EXPECT_EQ(extra.getConnectionState(), DirectPipeReceiverProcessor::ConnectionState::LimitReached);
    auto* legacyHeader = static_cast<directpipe::DirectPipeHeader*>(legacy.getData());
    EXPECT_FALSE(legacyHeader->consumer_active.load());
    readers[0].detach();
    EXPECT_TRUE(DirectPipeReceiverProcessorTestAccess::openNamed(extra, fanOutName, legacyName, &namedProducer));
    EXPECT_EQ(extra.getConnectionState(), DirectPipeReceiverProcessor::ConnectionState::Connected);
    EXPECT_FALSE(legacyHeader->consumer_active.load());
}

TEST_F(ReceiverFanOutTest, OldHostFallsBackWithoutChangingSavedParameters) {
    const auto suffix = juce::Uuid().toString().removeCharacters("-").substring(0, 12).toStdString();
    const auto legacyName = std::string("Local\\DPRO_") + suffix;
    directpipe::SharedMemory legacy;
    const auto bytes = directpipe::calculateSharedMemorySize(kCapacity, 2);
    ASSERT_TRUE(legacy.create(legacyName, bytes));
    directpipe::RingBuffer oldProducer;
    oldProducer.initAsProducer(legacy.getData(), kCapacity, 2, 48000);
    DirectPipeReceiverProcessor receiver;
    DirectPipeReceiverProcessorTestAccess::prepare(receiver, 128);
    receiver.getAPVTS().getParameter("buffer")->setValueNotifyingHost(0.5f);
    juce::MemoryBlock before, after;
    receiver.getStateInformation(before);
    ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::openNamed(receiver, legacyName + "-absent", legacyName));
    EXPECT_EQ(receiver.getConnectionState(), DirectPipeReceiverProcessor::ConnectionState::Legacy);
    receiver.getStateInformation(after);
    EXPECT_EQ(before, after);
    std::vector<float> samples(256, 0.5f);
    ASSERT_EQ(oldProducer.write(samples.data(), 128), 128u);
    expectConstant(render(receiver, 128), 0.5f, 64);
}

TEST_F(ReceiverFanOutTest, PendingPublicationDoesNotAddWorkerPeriodToBufferingLatency) {
    DirectPipeReceiverProcessor receiver;
    DirectPipeReceiverProcessorTestAccess::prepare(receiver, 128);
    ASSERT_TRUE(DirectPipeReceiverProcessorTestAccess::connectFanOut(receiver, aligned_, bytes_, producer_, 2048));
    EXPECT_EQ(DirectPipeReceiverProcessorTestAccess::available(receiver), receiver.getTargetFillFrames());
    EXPECT_EQ(receiver.getLatencySamples(), 512);
    // Trimming the new connection does not trim either already-published one.
    EXPECT_EQ(DirectPipeReceiverProcessorTestAccess::available(first_), 2048u);
    EXPECT_EQ(DirectPipeReceiverProcessorTestAccess::available(second_), 2048u);
}

TEST_F(ReceiverFanOutTest, WorkerUpgradesLegacyAndWaitsForNewProducerAcknowledgement) {
    const auto suffix = juce::Uuid().toString().removeCharacters("-").substring(0, 12).toStdString();
    const auto legacyName = std::string("Local\\DPWU_l_") + suffix;
    const auto fanOutName = std::string("Local\\DPWU_f_") + suffix;
    directpipe::SharedMemory legacy, independent;
    ASSERT_TRUE(legacy.create(legacyName, directpipe::calculateSharedMemorySize(kCapacity, 2)));
    directpipe::RingBuffer oldProducer;
    oldProducer.initAsProducer(legacy.getData(), kCapacity, 2, 48000);
    DirectPipeReceiverProcessor receiver;
    DirectPipeReceiverProcessorTestAccess::prepare(receiver, 128);
    DirectPipeReceiverProcessorTestAccess::startNamedWorker(receiver, fanOutName, legacyName);
    const auto waitFor = [&](DirectPipeReceiverProcessor::ConnectionState state) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (receiver.getConnectionState() != state && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return receiver.getConnectionState() == state;
    };
    ASSERT_TRUE(waitFor(DirectPipeReceiverProcessor::ConnectionState::Legacy));
    ASSERT_TRUE(independent.create(fanOutName, bytes_));
    directpipe::FanOutProducer newProducer;
    ASSERT_TRUE(newProducer.initialize(independent.getData(), independent.getSize(), 48000, 2, kCapacity));
    ASSERT_TRUE(waitFor(DirectPipeReceiverProcessor::ConnectionState::Waiting));
    EXPECT_FALSE(receiver.isConnected());
    EXPECT_FALSE(static_cast<directpipe::DirectPipeHeader*>(legacy.getData())->consumer_active.load());
    newProducer.write(nullptr, 0);
    ASSERT_TRUE(waitFor(DirectPipeReceiverProcessor::ConnectionState::Connected));
    std::vector<float> samples(256, 0.6f);
    newProducer.write(samples.data(), 128);
    expectConstant(render(receiver, 128), 0.6f, 64);
    receiver.releaseResources();
}
