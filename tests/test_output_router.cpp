// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/OutputRouter.h"

using namespace directpipe;

class OutputRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        router_.initialize(48000.0, 512);
    }

    OutputRouter router_;
};

TEST_F(OutputRouterTest, VolumeClamp) {
    router_.setVolume(OutputRouter::Output::Monitor, 1.5f);
    EXPECT_FLOAT_EQ(router_.getVolume(OutputRouter::Output::Monitor), 1.0f);

    router_.setVolume(OutputRouter::Output::Monitor, -0.5f);
    EXPECT_FLOAT_EQ(router_.getVolume(OutputRouter::Output::Monitor), 0.0f);

    router_.setVolume(OutputRouter::Output::Monitor, 0.75f);
    EXPECT_FLOAT_EQ(router_.getVolume(OutputRouter::Output::Monitor), 0.75f);
}

TEST_F(OutputRouterTest, VolumeZeroSkipsMonitor) {
    router_.setVolume(OutputRouter::Output::Monitor, 0.0f);
    router_.setEnabled(OutputRouter::Output::Monitor, true);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    // No MonitorOutput set — should not crash even with enabled + zero volume
    router_.routeAudio(buffer, 512);
    SUCCEED();
}

TEST_F(OutputRouterTest, EnableDisableToggle) {
    router_.setEnabled(OutputRouter::Output::Monitor, true);
    EXPECT_TRUE(router_.isEnabled(OutputRouter::Output::Monitor));

    router_.setEnabled(OutputRouter::Output::Monitor, false);
    EXPECT_FALSE(router_.isEnabled(OutputRouter::Output::Monitor));
}

TEST_F(OutputRouterTest, BufferTruncatedFlag) {
    router_.initialize(48000.0, 128);
    router_.setEnabled(OutputRouter::Output::Monitor, true);

    juce::AudioBuffer<float> buffer(2, 256);
    buffer.clear();

    router_.routeAudio(buffer, 256);
    EXPECT_TRUE(router_.checkAndClearBufferTruncated());
    EXPECT_FALSE(router_.checkAndClearBufferTruncated());
}

TEST_F(OutputRouterTest, MonoToStereoRouting) {
    router_.setEnabled(OutputRouter::Output::Monitor, true);
    router_.setVolume(OutputRouter::Output::Monitor, 1.0f);

    juce::AudioBuffer<float> buffer(1, 512);
    for (int i = 0; i < 512; ++i)
        buffer.setSample(0, i, 0.5f);

    router_.routeAudio(buffer, 512);
    SUCCEED();
}

TEST_F(OutputRouterTest, UninitializedEarlyReturn) {
    OutputRouter uninitRouter;

    juce::AudioBuffer<float> buffer(2, 128);
    buffer.clear();

    uninitRouter.routeAudio(buffer, 128);
    EXPECT_TRUE(uninitRouter.checkAndClearBufferTruncated());
}
