// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#define private public
#include "Audio/MonitorOutput.h"
#undef private

using namespace directpipe;

namespace {

class FakeAudioIODevice final : public juce::AudioIODevice {
public:
    FakeAudioIODevice(const juce::String& deviceName, double sampleRate, int bufferSize,
                      const juce::BigInteger& activeOutput)
        : juce::AudioIODevice(deviceName, "Fake Audio"),
          sampleRate_(sampleRate),
          bufferSize_(bufferSize),
          activeOutput_(activeOutput)
    {
        outputNames_.add("Out L");
        outputNames_.add("Out R");
    }

    juce::StringArray getOutputChannelNames() override { return outputNames_; }
    juce::StringArray getInputChannelNames() override { return {}; }

    juce::Array<double> getAvailableSampleRates() override
    {
        return { 44100.0, 48000.0 };
    }

    juce::Array<int> getAvailableBufferSizes() override
    {
        return { 128, 256, 512 };
    }

    int getDefaultBufferSize() override { return 128; }

    juce::String open(const juce::BigInteger& inputChannels,
                      const juce::BigInteger& outputChannels,
                      double sampleRate,
                      int bufferSizeSamples) override
    {
        activeInput_ = inputChannels;
        activeOutput_ = outputChannels;
        sampleRate_ = sampleRate;
        bufferSize_ = bufferSizeSamples;
        open_ = true;
        return {};
    }

    void close() override { open_ = false; }
    bool isOpen() override { return open_; }
    void start(juce::AudioIODeviceCallback*) override { playing_ = true; }
    void stop() override { playing_ = false; }
    bool isPlaying() override { return playing_; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize_; }
    double getCurrentSampleRate() override { return sampleRate_; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOutput_; }
    juce::BigInteger getActiveInputChannels() const override { return activeInput_; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    juce::StringArray outputNames_;
    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;
    juce::BigInteger activeInput_;
    juce::BigInteger activeOutput_;
    bool open_ = true;
    bool playing_ = false;
};

void prepareActiveMonitor(MonitorOutput& monitor, int producerBlock, int consumerBlock)
{
    monitor.ringBuffer_.initialize(8192, 2);
    monitor.capacityFrames_.store(8192, std::memory_order_relaxed);
    monitor.status_.store(VirtualCableStatus::Active, std::memory_order_release);
    monitor.producerWriteAdmission_.store(true, std::memory_order_seq_cst);
    monitor.actualSampleRate_.store(48000.0, std::memory_order_relaxed);
    monitor.actualBufferSize_.store(consumerBlock, std::memory_order_relaxed);
    monitor.producerBlockSize_.store(producerBlock, std::memory_order_relaxed);
    monitor.consumerBlockSize_.store(consumerBlock, std::memory_order_relaxed);
    monitor.adaptiveTargetState_ = {};
    monitor.adaptiveTargetState_.lastProducerBlock = producerBlock;
    monitor.adaptiveTargetState_.lastConsumerBlock = consumerBlock;
    monitor_drift::resetPll(monitor.pllState_);
}

void writeConstant(MonitorOutput& monitor, int frames, float value)
{
    std::vector<float> left(static_cast<size_t>(frames), value);
    std::vector<float> right(static_cast<size_t>(frames), -value);
    const float* inputs[] = { left.data(), right.data() };
    ASSERT_EQ(monitor.ringBuffer_.write(inputs, 2, frames), frames);
}

std::pair<std::vector<float>, std::vector<float>> runMonitorCallback(MonitorOutput& monitor, int frames)
{
    std::vector<float> left(static_cast<size_t>(frames), 99.0f);
    std::vector<float> right(static_cast<size_t>(frames), 99.0f);
    float* outputs[] = { left.data(), right.data() };
    juce::AudioIODeviceCallbackContext context;

    monitor.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, frames, context);

    return { std::move(left), std::move(right) };
}

void expectSilence(const std::vector<float>& left, const std::vector<float>& right)
{
    for (float sample : left)
        EXPECT_FLOAT_EQ(sample, 0.0f);
    for (float sample : right)
        EXPECT_FLOAT_EQ(sample, 0.0f);
}

struct MonitorWriteBarrier {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};

    static void wait(void* context)
    {
        auto& barrier = *static_cast<MonitorWriteBarrier*>(context);
        barrier.entered.store(true, std::memory_order_release);
        while (!barrier.release.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
};

template <typename Predicate>
bool spinWaitUntil(Predicate&& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

} // namespace

TEST(MonitorOutputTest, ShutdownWaitsForInFlightProducerBeforeReset)
{
    MonitorOutput monitor;
    prepareActiveMonitor(monitor, 64, 64);

    MonitorWriteBarrier barrier;
    monitor.testWriteBarrier_ = &MonitorWriteBarrier::wait;
    monitor.testWriteBarrierContext_ = &barrier;

    std::array<float, 64> left{};
    std::array<float, 64> right{};
    const float* channels[] = { left.data(), right.data() };
    std::thread writer([&] { monitor.writeAudio(channels, 2, 64); });

    if (!spinWaitUntil([&] { return barrier.entered.load(std::memory_order_acquire); },
                       std::chrono::seconds(2))) {
        barrier.release.store(true, std::memory_order_release);
        writer.join();
        FAIL() << "writeAudio did not reach the deterministic barrier";
        return;
    }

    std::atomic<bool> shutdownReturned{false};
    std::thread shutdown([&] {
        monitor.shutdown();
        shutdownReturned.store(true, std::memory_order_release);
    });

    if (!spinWaitUntil([&] {
            return monitor.getStatus() == VirtualCableStatus::NotConfigured;
        }, std::chrono::seconds(2))) {
        barrier.release.store(true, std::memory_order_release);
        writer.join();
        shutdown.join();
        FAIL() << "shutdown did not close producer admission";
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto waitedForWriter = !shutdownReturned.load(std::memory_order_acquire);

    barrier.release.store(true, std::memory_order_release);
    writer.join();
    shutdown.join();

    EXPECT_TRUE(waitedForWriter)
        << "shutdown reset the ring while writeAudio was still in flight";
}

TEST(MonitorOutputTest, ShutdownInvalidatesDeferredLifecycleWork)
{
    MonitorOutput monitor;
    const auto previousGeneration = monitor.lifecycleGeneration_.load(std::memory_order_acquire);

    monitor.shutdown();

    EXPECT_NE(previousGeneration,
              monitor.lifecycleGeneration_.load(std::memory_order_acquire));
    EXPECT_FALSE(monitor.activeOutputRecoveryPending_.load(std::memory_order_acquire));
}

TEST(MonitorOutputTest, DeviceRestartWaitsForInFlightProducerBeforeReset)
{
    MonitorOutput monitor;
    monitor.deviceName_ = "Monitor Device";
    monitor.sampleRate_ = 48000.0;
    monitor.bufferSize_ = 64;
    prepareActiveMonitor(monitor, 64, 64);

    MonitorWriteBarrier barrier;
    monitor.testWriteBarrier_ = &MonitorWriteBarrier::wait;
    monitor.testWriteBarrierContext_ = &barrier;

    std::array<float, 64> left{};
    std::array<float, 64> right{};
    const float* channels[] = { left.data(), right.data() };
    std::thread writer([&] { monitor.writeAudio(channels, 2, 64); });

    if (!spinWaitUntil([&] { return barrier.entered.load(std::memory_order_acquire); },
                       std::chrono::seconds(2))) {
        barrier.release.store(true, std::memory_order_release);
        writer.join();
        FAIL() << "writeAudio did not reach the deterministic barrier";
        return;
    }

    juce::BigInteger activeStereo;
    activeStereo.setRange(0, 2, true);
    FakeAudioIODevice device("Monitor Device", 48000.0, 64, activeStereo);
    std::atomic<bool> restartEntered{false};
    std::atomic<bool> restartReturned{false};
    std::thread restart([&] {
        restartEntered.store(true, std::memory_order_release);
        juce::AudioIODeviceCallback& callback = monitor;
        callback.audioDeviceAboutToStart(&device);
        restartReturned.store(true, std::memory_order_release);
    });

    if (!spinWaitUntil([&] { return restartEntered.load(std::memory_order_acquire); },
                       std::chrono::seconds(2))) {
        barrier.release.store(true, std::memory_order_release);
        writer.join();
        restart.join();
        FAIL() << "device restart thread did not start";
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto waitedForWriter = !restartReturned.load(std::memory_order_acquire);

    barrier.release.store(true, std::memory_order_release);
    writer.join();
    restart.join();

    EXPECT_TRUE(waitedForWriter)
        << "device restart reset the ring while writeAudio was still in flight";
    EXPECT_EQ(monitor.getStatus(), VirtualCableStatus::Active);
}

TEST(MonitorOutputTest, ZeroActiveOutputDoesNotReportActive)
{
    MonitorOutput monitor;
    monitor.deviceName_ = "Monitor Device";
    monitor.sampleRate_ = 48000.0;
    monitor.bufferSize_ = 128;

    juce::BigInteger noActiveOutput;
    FakeAudioIODevice device("Monitor Device", 48000.0, 128, noActiveOutput);

    juce::AudioIODeviceCallback& callback = monitor;
    callback.audioDeviceAboutToStart(&device);

    EXPECT_EQ(monitor.getStatus(), VirtualCableStatus::Error);
    EXPECT_TRUE(monitor.monitorLost_.load(std::memory_order_relaxed));
    EXPECT_TRUE(monitor.activeOutputRecoveryPending_.load(std::memory_order_relaxed));
    EXPECT_FALSE(monitor.isActive());
}

TEST(MonitorOutputTest, ActiveOutputCanReportActive)
{
    MonitorOutput monitor;
    monitor.deviceName_ = "Monitor Device";
    monitor.sampleRate_ = 48000.0;
    monitor.bufferSize_ = 128;

    juce::BigInteger activeStereo;
    activeStereo.setRange(0, 2, true);
    FakeAudioIODevice device("Monitor Device", 48000.0, 128, activeStereo);

    juce::AudioIODeviceCallback& callback = monitor;
    callback.audioDeviceAboutToStart(&device);

    EXPECT_EQ(monitor.getStatus(), VirtualCableStatus::Active);
    EXPECT_FALSE(monitor.monitorLost_.load(std::memory_order_relaxed));
    EXPECT_TRUE(monitor.isActive());
}

TEST(MonitorOutputTest, FallbackWithSampleRateMismatchRemainsRetryable)
{
    MonitorOutput monitor;
    monitor.deviceName_ = "Desired Monitor";
    monitor.sampleRate_ = 48000.0;
    monitor.bufferSize_ = 128;

    juce::BigInteger activeStereo;
    activeStereo.setRange(0, 2, true);
    FakeAudioIODevice device("Fallback Monitor", 44100.0, 128, activeStereo);

    juce::AudioIODeviceCallback& callback = monitor;
    callback.audioDeviceAboutToStart(&device);

    EXPECT_EQ(monitor.getStatus(), VirtualCableStatus::Error);
    EXPECT_TRUE(monitor.monitorLost_.load(std::memory_order_relaxed));
    EXPECT_TRUE(monitor.isDeviceLost());
    EXPECT_FALSE(monitor.isActive());
}

TEST(MonitorOutputTest, PrimingOutputsSilenceUntilAdaptiveTargetFill)
{
    MonitorOutput monitor;
    prepareActiveMonitor(monitor, 512, 128);
    monitor.priming_.store(true, std::memory_order_relaxed);
    writeConstant(monitor, 512, 0.5f);

    auto [left, right] = runMonitorCallback(monitor, 128);

    expectSilence(left, right);
    EXPECT_TRUE(monitor.isPriming());
    EXPECT_EQ(monitor.getFillFrames(), 512);
    EXPECT_EQ(monitor.getTargetFillFrames(), 640);
    EXPECT_EQ(monitor.ringBuffer_.availableRead(), 512);
}

TEST(MonitorOutputTest, BlockSizeChangeReprimesUntilNewTargetFill)
{
    MonitorOutput monitor;
    prepareActiveMonitor(monitor, 256, 128);
    monitor.priming_.store(false, std::memory_order_relaxed);
    monitor.producerBlockSize_.store(1024, std::memory_order_relaxed);
    writeConstant(monitor, 512, 0.5f);

    auto [left, right] = runMonitorCallback(monitor, 128);

    expectSilence(left, right);
    EXPECT_TRUE(monitor.isPriming());
    EXPECT_EQ(monitor.getTargetFillFrames(), 1152);
    EXPECT_EQ(monitor.ringBuffer_.availableRead(), 512);
}

TEST(MonitorOutputTest, UnderrunTriggersReprimeInsteadOfPartialZeroFill)
{
    MonitorOutput monitor;
    prepareActiveMonitor(monitor, 512, 128);
    monitor.priming_.store(false, std::memory_order_relaxed);
    writeConstant(monitor, 64, 0.5f);

    auto [left, right] = runMonitorCallback(monitor, 128);

    expectSilence(left, right);
    EXPECT_TRUE(monitor.isPriming());
    EXPECT_EQ(monitor.getUnderrunCount(), 1);
    EXPECT_EQ(monitor.ringBuffer_.availableRead(), 64);
}

TEST(MonitorOutputTest, NormalDriftDoesNotIncrementEmergencyTrimCounter)
{
    MonitorOutput monitor;
    prepareActiveMonitor(monitor, 512, 128);
    monitor.priming_.store(false, std::memory_order_relaxed);
    monitor.callbacksSinceStart_.store(51, std::memory_order_relaxed);
    writeConstant(monitor, 1024, 0.5f);

    auto [left, right] = runMonitorCallback(monitor, 128);

    EXPECT_FALSE(monitor.isPriming());
    EXPECT_EQ(monitor.getLatencyTrimmedFrames(), 0);
    EXPECT_EQ(monitor.getUnderrunCount(), 0);
    EXPECT_GT(left[0], 0.0f);
    EXPECT_LT(right[0], 0.0f);
}
