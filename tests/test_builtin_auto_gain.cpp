// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include <gtest/gtest.h>
#include "../host/Source/Audio/BuiltinAutoGain.h"
#include <cmath>

using namespace directpipe;

// Helper: generate sine wave at given frequency and amplitude
static void fillSine(juce::AudioBuffer<float>& buffer, float freq, double sampleRate, float amplitude = 1.0f) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * freq * i / static_cast<float>(sampleRate)));
}

// Helper: compute RMS of entire buffer (all channels averaged)
static float computeRMS(const juce::AudioBuffer<float>& buffer) {
    float sum = 0.0f;
    int totalSamples = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            float s = buffer.getSample(ch, i);
            sum += s * s;
        }
        totalSamples += buffer.getNumSamples();
    }
    return (totalSamples > 0) ? std::sqrt(sum / static_cast<float>(totalSamples)) : 0.0f;
}

class BuiltinAutoGainTest : public ::testing::Test {
protected:
    BuiltinAutoGain agc;
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSize = 512;

    void SetUp() override {
        agc.prepareToPlay(kSampleRate, kBlockSize);
    }

    // Process multiple blocks of a sine wave with given amplitude, return total output RMS
    float processBlocks(float amplitude, int numBlocks) {
        juce::MidiBuffer midi;
        float outputSum = 0.0f;
        int outputSamples = 0;

        for (int b = 0; b < numBlocks; ++b) {
            juce::AudioBuffer<float> buf(2, kBlockSize);
            fillSine(buf, 440.0f, kSampleRate, amplitude);
            agc.processBlock(buf, midi);

            // Accumulate output RMS from latter half to let AGC settle
            if (b >= numBlocks / 2) {
                for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
                    for (int i = 0; i < buf.getNumSamples(); ++i) {
                        float s = buf.getSample(ch, i);
                        outputSum += s * s;
                    }
                    outputSamples += buf.getNumSamples();
                }
            }
        }

        return (outputSamples > 0) ? std::sqrt(outputSum / static_cast<float>(outputSamples)) : 0.0f;
    }
};

TEST_F(BuiltinAutoGainTest, DefaultState) {
    EXPECT_FLOAT_EQ(agc.getTargetLUFS(), -15.0f);
    EXPECT_FLOAT_EQ(agc.getLowCorrect(), 0.50f);
    EXPECT_FLOAT_EQ(agc.getHighCorrect(), 0.90f);
    EXPECT_FLOAT_EQ(agc.getMaxGaindB(), 22.0f);
    EXPECT_FLOAT_EQ(agc.getFreezeLevel(), -45.0f);
    EXPECT_EQ(agc.getLatencySamples(), 0);
    EXPECT_EQ(agc.getName(), juce::String("AutoGain"));
}

TEST_F(BuiltinAutoGainTest, QuietSignalBoost) {
    // Feed a quiet sine (~amplitude 0.03 which is roughly -30 dBFS)
    const float quietAmplitude = 0.03f;
    const float inputRMS = quietAmplitude / std::sqrt(2.0f); // RMS of sine = amplitude / sqrt(2)

    float outputRMS = processBlocks(quietAmplitude, 200);

    // AGC should boost the quiet signal — output RMS should be higher than input RMS
    EXPECT_GT(outputRMS, inputRMS)
        << "Output RMS (" << outputRMS << ") should be higher than input RMS ("
        << inputRMS << ") for quiet signal boost";
}

TEST_F(BuiltinAutoGainTest, LoudSignalCut) {
    // Feed a loud sine (~amplitude 0.5 which is roughly -6 dBFS)
    const float loudAmplitude = 0.5f;
    const float inputRMS = loudAmplitude / std::sqrt(2.0f);

    float outputRMS = processBlocks(loudAmplitude, 200);

    // AGC should cut the loud signal — output RMS should be lower than input RMS
    EXPECT_LT(outputRMS, inputRMS)
        << "Output RMS (" << outputRMS << ") should be lower than input RMS ("
        << inputRMS << ") for loud signal cut";
}

TEST_F(BuiltinAutoGainTest, FreezeLevel) {
    // Feed very quiet signal (below -45 dBFS freeze threshold)
    // amplitude 0.003 => ~-50 dBFS RMS, well below -45 dBFS freeze level
    const float veryQuietAmplitude = 0.003f;
    juce::MidiBuffer midi;

    // Process enough blocks to let AGC try to boost
    float lastGaindB = 0.0f;
    for (int b = 0; b < 100; ++b) {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        fillSine(buf, 440.0f, kSampleRate, veryQuietAmplitude);
        agc.processBlock(buf, midi);
        lastGaindB = agc.getCurrentGaindB();
    }

    // Gain should NOT increase significantly — freeze should prevent boosting
    // Allow small tolerance for envelope follower smoothing from initial state
    EXPECT_LT(lastGaindB, 1.0f)
        << "Gain (" << lastGaindB << " dB) should not increase when signal is below freeze level";
}

TEST_F(BuiltinAutoGainTest, MaxGainClamp) {
    // Feed extremely quiet signal — AGC would want to boost a LOT
    const float tinyAmplitude = 0.001f;
    const float maxGaindB = agc.getMaxGaindB(); // 24 dB default
    const float maxGainLinear = std::pow(10.0f, maxGaindB / 20.0f); // ~15.85x

    // Disable freeze level so AGC tries to boost even very quiet signals
    agc.setFreezeLevel(-66.0f); // below -65 dBFS bypasses freeze gate

    juce::MidiBuffer midi;
    float peakGainLinear = 0.0f;

    for (int b = 0; b < 300; ++b) {
        juce::AudioBuffer<float> buf(2, kBlockSize);
        fillSine(buf, 440.0f, kSampleRate, tinyAmplitude);

        // Measure input peak
        float inputPeak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                inputPeak = std::max(inputPeak, std::abs(buf.getSample(ch, i)));

        agc.processBlock(buf, midi);

        // Measure output peak
        float outputPeak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                outputPeak = std::max(outputPeak, std::abs(buf.getSample(ch, i)));

        if (inputPeak > 1.0e-8f) {
            float effectiveGain = outputPeak / inputPeak;
            peakGainLinear = std::max(peakGainLinear, effectiveGain);
        }
    }

    // Peak gain should not exceed maxGain (24 dB = ~15.85x) with some tolerance for ramp
    EXPECT_LT(peakGainLinear, maxGainLinear * 1.1f)
        << "Peak gain (" << peakGainLinear << "x) should not exceed max gain ("
        << maxGainLinear << "x = " << maxGaindB << " dB)";
}

TEST_F(BuiltinAutoGainTest, StateRoundtrip) {
    // Set custom parameters
    agc.setTargetLUFS(-20.0f);
    agc.setLowCorrect(0.80f);
    agc.setHighCorrect(1.20f);
    agc.setMaxGaindB(18.0f);
    agc.setFreezeLevel(-50.0f);

    // Save state
    juce::MemoryBlock state;
    agc.getStateInformation(state);

    // Create new instance, restore state
    BuiltinAutoGain restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    EXPECT_FLOAT_EQ(restored.getTargetLUFS(), -20.0f);
    EXPECT_FLOAT_EQ(restored.getLowCorrect(), 0.80f);
    EXPECT_FLOAT_EQ(restored.getHighCorrect(), 1.20f);
    EXPECT_FLOAT_EQ(restored.getMaxGaindB(), 18.0f);
    EXPECT_FLOAT_EQ(restored.getFreezeLevel(), -50.0f);
}

TEST_F(BuiltinAutoGainTest, ZeroLatency) {
    EXPECT_EQ(agc.getLatencySamples(), 0);
}
