// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <directpipe/SharedMemory.h>
#include <directpipe/RingBuffer.h>
#include <directpipe/Constants.h>
#include <directpipe/Protocol.h>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class DirectPipeReceiverProcessor : public juce::AudioProcessor,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AsyncUpdater {
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
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }
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
    bool hasMultiConsumerWarning() const { return multiConsumerWarning_.load(std::memory_order_relaxed); }
    uint32_t getSourceSampleRate() const;
    uint32_t getSourceChannels() const;

private:
    struct Connection {
        ~Connection() {
            ringBuffer.detach();
            sharedMemory.close();
        }

        directpipe::SharedMemory sharedMemory;
        directpipe::RingBuffer ringBuffer;
    };

    class ConnectionLease {
    public:
        explicit ConnectionLease(DirectPipeReceiverProcessor& owner) noexcept;
        ~ConnectionLease();
        Connection* get() const noexcept { return connection_; }

        ConnectionLease(const ConnectionLease&) = delete;
        ConnectionLease& operator=(const ConnectionLease&) = delete;

    private:
        DirectPipeReceiverProcessor& owner_;
        Connection* connection_ = nullptr;
    };

    // The worker exclusively owns mappings. The audio thread only borrows the
    // published raw pointer while counted in connectionUsers_.
    std::unique_ptr<Connection> workerConnection_;
    std::atomic<Connection*> activeConnection_{nullptr};
    std::atomic<uint32_t> connectionUsers_{0};
    std::atomic<bool> connectionAccessEnabled_{false};
    std::atomic<bool> connectionWorkerStopRequested_{false};
    std::atomic<bool> reconnectRequested_{false};
    std::thread connectionThread_;
    std::atomic<uint64_t> connectionSerial_{0};
    uint64_t rtConnectionSerial_ = 0;                  // [RT thread only]

    std::atomic<bool> connected_{false};               // [Worker write, GUI read]
    std::atomic<bool> multiConsumerWarning_{false};    // [Worker write, GUI read]
    std::atomic<uint32_t> cachedSampleRate_{0};        // [Worker write, GUI read]
    std::atomic<uint32_t> cachedChannels_{0};          // [Worker write, GUI read]

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

    // Buffer presets: { targetFill, highThreshold, lowThreshold }
    // Index matches "buffer" AudioParameterChoice
    static constexpr int kNumBufferPresets = 5;
    static constexpr uint32_t kBufferPresets[kNumBufferPresets][3] = {
        {  256,   768,   64 },  // 0: Ultra Low  (256 samples)
        {  512,  1536,  128 },  // 1: Low        (512 samples)
        { 1024,  3072,  256 },  // 2: Medium     (1024 samples)
        { 2048,  6144,  512 },  // 3: High       (2048 samples)
        { 4096, 12288, 1024 },  // 4: Safe       (4096 samples)
    };
public:
    uint32_t getTargetFillFrames() const;
private:
    uint32_t getHighFillThreshold() const;
    uint32_t getLowFillThreshold() const;

    juce::AudioProcessorValueTreeState apvts_;
    std::atomic<int> requestedLatencySamples_{0};

    void startConnectionWorker();
    void stopConnectionWorker();
    void connectionWorkerLoop();
    std::unique_ptr<Connection> openConnection();
    void publishConnection(std::unique_ptr<Connection> connection);
    void retireConnection();
    Connection* acquireConnection() noexcept;
    void releaseConnection() noexcept;
    void skipToFreshPosition(Connection& connection);

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void saveLastOutput(const juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);
    void applyFadeOut(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DirectPipeReceiverProcessor)
};
