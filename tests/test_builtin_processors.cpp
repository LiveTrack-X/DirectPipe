// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include <gtest/gtest.h>
#include "../host/Source/Audio/BuiltinFilter.h"
#include <cmath>

using namespace directpipe;

// Helper: generate sine wave at given frequency
static void fillSine(juce::AudioBuffer<float>& buffer, float freq, double sampleRate) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, std::sin(2.0f * juce::MathConstants<float>::pi * freq * i / static_cast<float>(sampleRate)));
}

// Helper: compute RMS of buffer
static float computeRMS(const juce::AudioBuffer<float>& buffer, int channel = 0) {
    float sum = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        float s = buffer.getSample(channel, i);
        sum += s * s;
    }
    return std::sqrt(sum / static_cast<float>(buffer.getNumSamples()));
}

class BuiltinFilterTest : public ::testing::Test {
protected:
    BuiltinFilter filter;
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSize = 512;

    void SetUp() override {
        filter.prepareToPlay(kSampleRate, kBlockSize);
    }
};

TEST_F(BuiltinFilterTest, DefaultState) {
    EXPECT_TRUE(filter.isHPFEnabled());
    EXPECT_FLOAT_EQ(filter.getHPFFrequency(), 60.0f);
    EXPECT_FALSE(filter.isLPFEnabled());
    EXPECT_FLOAT_EQ(filter.getLPFFrequency(), 16000.0f);
    EXPECT_EQ(filter.getLatencySamples(), 0);
    EXPECT_EQ(filter.getName(), juce::String("Filter"));
}

TEST_F(BuiltinFilterTest, HPFAttenuatesLowFrequency) {
    filter.setHPFEnabled(true);
    filter.setHPFFrequency(100.0f);
    filter.setLPFEnabled(false);

    // 30Hz sine — well below HPF cutoff
    juce::AudioBuffer<float> buf(2, 4096);
    fillSine(buf, 30.0f, kSampleRate);
    float inputRMS = computeRMS(buf);

    juce::MidiBuffer midi;
    filter.processBlock(buf, midi);
    float outputRMS = computeRMS(buf);

    // Should be significantly attenuated
    EXPECT_LT(outputRMS, inputRMS * 0.5f);
}

TEST_F(BuiltinFilterTest, HPFPassesHighFrequency) {
    filter.setHPFEnabled(true);
    filter.setHPFFrequency(100.0f);
    filter.setLPFEnabled(false);

    // 1kHz sine — well above HPF cutoff
    juce::AudioBuffer<float> buf(2, 4096);
    fillSine(buf, 1000.0f, kSampleRate);
    float inputRMS = computeRMS(buf);

    juce::MidiBuffer midi;
    filter.processBlock(buf, midi);
    float outputRMS = computeRMS(buf);

    // Should pass through mostly unchanged
    EXPECT_GT(outputRMS, inputRMS * 0.9f);
}

TEST_F(BuiltinFilterTest, LPFAttenuatesHighFrequency) {
    filter.setHPFEnabled(false);
    filter.setLPFEnabled(true);
    filter.setLPFFrequency(8000.0f);

    // 15kHz sine — above LPF cutoff
    juce::AudioBuffer<float> buf(2, 4096);
    fillSine(buf, 15000.0f, kSampleRate);
    float inputRMS = computeRMS(buf);

    juce::MidiBuffer midi;
    filter.processBlock(buf, midi);
    float outputRMS = computeRMS(buf);

    EXPECT_LT(outputRMS, inputRMS * 0.5f);
}

TEST_F(BuiltinFilterTest, BothDisabledPassthrough) {
    filter.setHPFEnabled(false);
    filter.setLPFEnabled(false);

    juce::AudioBuffer<float> buf(2, 512);
    fillSine(buf, 440.0f, kSampleRate);
    float inputRMS = computeRMS(buf);

    juce::MidiBuffer midi;
    filter.processBlock(buf, midi);
    float outputRMS = computeRMS(buf);

    EXPECT_NEAR(outputRMS, inputRMS, 0.001f);
}

TEST_F(BuiltinFilterTest, FrequencyClamp) {
    filter.setHPFFrequency(5.0f);  // below min
    EXPECT_FLOAT_EQ(filter.getHPFFrequency(), 20.0f);

    filter.setHPFFrequency(500.0f);  // above max
    EXPECT_FLOAT_EQ(filter.getHPFFrequency(), 300.0f);

    filter.setLPFFrequency(1000.0f);  // below min
    EXPECT_FLOAT_EQ(filter.getLPFFrequency(), 4000.0f);

    filter.setLPFFrequency(25000.0f);  // above max
    EXPECT_FLOAT_EQ(filter.getLPFFrequency(), 20000.0f);
}

TEST_F(BuiltinFilterTest, StateRoundtrip) {
    filter.setHPFEnabled(false);
    filter.setHPFFrequency(120.0f);
    filter.setLPFEnabled(true);
    filter.setLPFFrequency(8000.0f);

    // Save state
    juce::MemoryBlock state;
    filter.getStateInformation(state);

    // Create new instance, restore
    BuiltinFilter restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    EXPECT_EQ(restored.isHPFEnabled(), false);
    EXPECT_FLOAT_EQ(restored.getHPFFrequency(), 120.0f);
    EXPECT_EQ(restored.isLPFEnabled(), true);
    EXPECT_FLOAT_EQ(restored.getLPFFrequency(), 8000.0f);
}

TEST_F(BuiltinFilterTest, MonoBuffer) {
    filter.setHPFEnabled(true);
    filter.setHPFFrequency(100.0f);

    juce::AudioBuffer<float> buf(1, 2048);
    fillSine(buf, 30.0f, kSampleRate);

    juce::MidiBuffer midi;
    filter.processBlock(buf, midi);  // should not crash with 1 channel
    EXPECT_LT(computeRMS(buf), 0.5f);
}
