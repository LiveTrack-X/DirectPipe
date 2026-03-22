// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
#include <gtest/gtest.h>
#include "../host/Source/Audio/SafetyLimiter.h"

using namespace directpipe;

class SafetyLimiterTest : public ::testing::Test {
protected:
    SafetyLimiter limiter;

    void SetUp() override {
        limiter.prepareToPlay(48000.0);
    }

    juce::AudioBuffer<float> makeBuffer(float value, int numSamples = 512, int channels = 2) {
        juce::AudioBuffer<float> buf(channels, numSamples);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buf.setSample(ch, i, value);
        return buf;
    }

    float getMaxAbsSample(const juce::AudioBuffer<float>& buffer) {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                peak = std::max(peak, std::abs(buffer.getSample(ch, i)));
            }
        }
        return peak;
    }
};

TEST_F(SafetyLimiterTest, DefaultState) {
    EXPECT_TRUE(limiter.isEnabled());
    EXPECT_FLOAT_EQ(limiter.getCeilingdB(), -0.3f);
    EXPECT_FALSE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, DisabledPassthrough) {
    limiter.setEnabled(false);
    auto buf = makeBuffer(2.0f);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_FLOAT_EQ(buf.getSample(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(limiter.getCurrentGainReduction(), 0.0f);
}

TEST_F(SafetyLimiterTest, BelowCeiling) {
    auto buf = makeBuffer(0.5f);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_NEAR(buf.getSample(0, 256), 0.5f, 0.01f);
}

TEST_F(SafetyLimiterTest, AboveCeiling) {
    limiter.setCeiling(-6.0f);  // ceiling ??0.5012
    auto buf = makeBuffer(1.0f, 2048);
    limiter.process(buf, buf.getNumSamples());
    float lastSample = buf.getSample(0, buf.getNumSamples() - 1);
    EXPECT_LT(lastSample, 0.55f);
}

TEST_F(SafetyLimiterTest, CeilingRange) {
    limiter.setCeiling(-10.0f);
    EXPECT_FLOAT_EQ(limiter.getCeilingdB(), -6.0f);
    limiter.setCeiling(5.0f);
    EXPECT_FLOAT_EQ(limiter.getCeilingdB(), 0.0f);
}

TEST_F(SafetyLimiterTest, GainReductionFeedback) {
    limiter.setCeiling(-6.0f);
    auto buf = makeBuffer(1.0f, 1024);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_LT(limiter.getCurrentGainReduction(), -0.1f);
}

TEST_F(SafetyLimiterTest, IsLimiting) {
    limiter.setCeiling(-6.0f);
    auto buf = makeBuffer(1.0f, 1024);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_TRUE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, NotLimitingBelowCeiling) {
    auto buf = makeBuffer(0.1f, 512);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_FALSE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, SampleRateChange) {
    limiter.prepareToPlay(96000.0);
    EXPECT_FLOAT_EQ(limiter.getCurrentGainReduction(), 0.0f);
    limiter.prepareToPlay(44100.0);
    auto buf = makeBuffer(1.0f, 512);
    limiter.setCeiling(-6.0f);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_TRUE(limiter.isLimiting());
}

TEST_F(SafetyLimiterTest, MonoBuffer) {
    auto buf = makeBuffer(1.0f, 512, 1);
    limiter.setCeiling(-6.0f);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_LT(buf.getSample(0, buf.getNumSamples() - 1), 0.55f);
}

TEST_F(SafetyLimiterTest, ZeroCeiling) {
    limiter.setCeiling(0.0f);
    auto buf = makeBuffer(0.99f, 512);
    limiter.process(buf, buf.getNumSamples());
    EXPECT_NEAR(buf.getSample(0, 256), 0.99f, 0.02f);
}

TEST_F(SafetyLimiterTest, EnvelopeRelease) {
    limiter.setCeiling(-6.0f);
    // First: process loud signal to engage limiter
    auto loud = makeBuffer(1.0f, 1024);
    limiter.process(loud, loud.getNumSamples());
    EXPECT_TRUE(limiter.isLimiting());
    // Then: process quiet signal ??limiter should gradually release
    auto quiet = makeBuffer(0.1f, 4096);
    limiter.process(quiet, quiet.getNumSamples());
    // After release, signal should be close to original
    EXPECT_NEAR(quiet.getSample(0, quiet.getNumSamples() - 1), 0.1f, 0.02f);
}

TEST_F(SafetyLimiterTest, BrickwallCeilingNeverExceededOnHotSignal) {
    limiter.setCeiling(-6.0f);
    auto buf = makeBuffer(1.12f, 4096);
    limiter.process(buf, buf.getNumSamples());

    const float maxAbs = getMaxAbsSample(buf);
    const float ceilingLinear = juce::Decibels::decibelsToGain(-6.0f);
    EXPECT_LE(maxAbs, ceilingLinear + 1.0e-4f);
}

TEST_F(SafetyLimiterTest, ZeroDbCeilingNeverExceedsFullScale) {
    limiter.setCeiling(0.0f);
    auto buf = makeBuffer(1.5f, 2048);
    limiter.process(buf, buf.getNumSamples());

    const float maxAbs = getMaxAbsSample(buf);
    EXPECT_LE(maxAbs, 1.0f + 1.0e-4f);
}

TEST_F(SafetyLimiterTest, ZeroLatencyGuardActsOnFirstSample) {
    limiter.setCeiling(-6.0f);
    auto buf = makeBuffer(1.5f, 16);
    limiter.process(buf, buf.getNumSamples());
    const float ceilingLinear = juce::Decibels::decibelsToGain(-6.0f);
    EXPECT_LE(std::abs(buf.getSample(0, 0)), ceilingLinear + 1.0e-4f);
}
