// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <directpipe/SharedMemory.h>
#include <directpipe/RingBuffer.h>
#include <directpipe/Constants.h>
#include <directpipe/Protocol.h>
#include <atomic>
#include <vector>

class DirectPipeReceiverProcessor : public juce::AudioProcessor {
public:
    DirectPipeReceiverProcessor();
    ~DirectPipeReceiverProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DirectPipe Receiver"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }

    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }
    uint32_t getSourceSampleRate() const;
    uint32_t getSourceChannels() const;

private:
    directpipe::SharedMemory sharedMemory_;
    directpipe::RingBuffer ringBuffer_;

    std::atomic<bool> connected_{false};
    int reconnectCounter_ = 0;
    static constexpr int kReconnectInterval = 100;

    std::vector<float> interleavedBuffer_;

    // Fade-out buffer: stores last block's output for smooth underrun handling
    std::vector<float> lastOutputBuffer_;   // planar, numChannels * blockSize
    int lastOutputSamples_ = 0;
    int lastOutputChannels_ = 0;
    bool hadAudioLastBlock_ = false;        // true if previous block had real data
    float fadeGain_ = 0.0f;                 // current fade-out level (1.0 → 0.0)
    static constexpr float kFadeStep = 0.05f;   // per-sample, ~20 samples to silence

    // Clock drift compensation
    int blocksSinceConnect_ = 0;
    static constexpr int kDriftCheckWarmup = 50;  // ignore first N blocks

    // Buffer presets: { targetFill, highThreshold }
    // Index matches "buffer" AudioParameterChoice
    static constexpr int kNumBufferPresets = 5;
    static constexpr uint32_t kBufferPresets[kNumBufferPresets][2] = {
        {  256,   768 },  // 0: Ultra Low  (256 samples)
        {  512,  1536 },  // 1: Low        (512 samples)
        { 1024,  3072 },  // 2: Medium     (1024 samples)
        { 2048,  6144 },  // 3: High       (2048 samples)
        { 4096, 12288 },  // 4: Safe       (4096 samples)
    };
public:
    uint32_t getTargetFillFrames() const;
private:
    uint32_t getHighFillThreshold() const;

    juce::AudioProcessorValueTreeState apvts_;

    void tryConnect();
    void disconnect();
    void skipToFreshPosition();
    void saveLastOutput(const juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);
    void applyFadeOut(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DirectPipeReceiverProcessor)
};
