// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>

#define private public
#include "Audio/MonitorOutput.h"
#undef private

using namespace directpipe;

namespace {

void prepareActiveMonitor(MonitorOutput& monitor, int producerBlock, int consumerBlock)
{
    monitor.ringBuffer_.initialize(8192, 2);
    monitor.capacityFrames_.store(8192, std::memory_order_relaxed);
    monitor.status_.store(VirtualCableStatus::Active, std::memory_order_release);
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

} // namespace

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
