// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <directpipe/SharedMemory.h>
#include <directpipe/RingBuffer.h>
#include <directpipe/Constants.h>
#include <directpipe/Protocol.h>
#include <directpipe/FanOut.h>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// One Receiver instance consumes one FanOut slot (up to eight per host stream).
// Older hosts use the unchanged legacy SPSC queue, with its one-reader limit.
// No transport resampling: source and containing audio host rates must match.
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
    enum class ConnectionState { Disconnected, Waiting, Connected, Legacy, LimitReached, Incompatible };
    ConnectionState getConnectionState() const { return connectionState_.load(std::memory_order_relaxed); }
    uint32_t getSourceSampleRate() const;
    uint32_t getSourceChannels() const;

private:
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    friend class DirectPipeReceiverProcessorTestAccess;
    bool isolatedConnectionForTest_ = false;
#endif
    // Only the connection worker reads these; tests can substitute unique names
    // before starting it, keeping production mappings out of automated tests.
    std::string legacyMappingName_ = directpipe::SHM_NAME;
    std::string fanOutMappingName_ = directpipe::FANOUT_SHM_NAME;

    // Non-owning transport views must detach before their owning mapping closes.
    // The worker drains published callback leases before destroying a Connection.
    struct Connection {
        ~Connection() {
            fanOut.detach();
            ringBuffer.detach();
            sharedMemory.close();
        }

        directpipe::SharedMemory sharedMemory;
        directpipe::RingBuffer ringBuffer;
        directpipe::FanOutConsumer fanOut;
        bool usesFanOut = false;

        bool isReady() const { return !usesFanOut || fanOut.isReady(); }
        bool isProducerActive() const { return usesFanOut ? fanOut.isProducerActive() : ringBuffer.isProducerActive(); }
        uint64_t getCurrentProducerGeneration() const { return usesFanOut ? fanOut.getCurrentProducerGeneration() : ringBuffer.getCurrentProducerGeneration(); }
        uint64_t getAttachedProducerGeneration() const { return usesFanOut ? fanOut.getAttachedProducerGeneration() : ringBuffer.getAttachedProducerGeneration(); }
        uint32_t getSampleRate() const { return usesFanOut ? fanOut.getSampleRate() : ringBuffer.getSampleRate(); }
        uint32_t getChannels() const { return usesFanOut ? fanOut.getChannels() : ringBuffer.getChannels(); }
        uint32_t availableRead() const { return usesFanOut ? fanOut.availableRead() : ringBuffer.availableRead(); }
        uint32_t read(float* data, uint32_t frames) { return usesFanOut ? fanOut.read(data, frames) : ringBuffer.read(data, frames); }
        uint32_t discard(uint32_t frames) { return usesFanOut ? fanOut.discard(frames) : ringBuffer.discard(frames); }
        uint64_t getOverflowEpoch() const { return usesFanOut ? fanOut.getOverflowEpoch() : 0; }
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

    // The worker exclusively owns mappings, claims, OS probes, and reconnection.
    // A pending claim retains its slot/view without exposure to processBlock.
    // After ack, the worker trims startup backlog and publishes the raw pointer;
    // audio may borrow it only while counted in connectionUsers_. Retirement
    // closes admission and drains those leases before releasing a slot or mapping.
    std::unique_ptr<Connection> workerConnection_;
    std::unique_ptr<Connection> pendingConnection_;   // claimed, awaiting producer acknowledgement
    std::atomic<Connection*> activeConnection_{nullptr};
    std::atomic<uint32_t> connectionUsers_{0};
    std::atomic<bool> connectionAccessEnabled_{false};
    std::atomic<bool> connectionWorkerStopRequested_{false};
    std::atomic<bool> reconnectRequested_{false};
    std::thread connectionThread_;
    std::atomic<uint64_t> connectionSerial_{0};
    uint64_t rtConnectionSerial_ = 0;                  // [RT thread only]
    uint64_t rtOverflowEpoch_ = 0;                     // [RT thread only]

    std::atomic<bool> connected_{false};               // [Worker write, GUI read]
    std::atomic<ConnectionState> connectionState_{ConnectionState::Disconnected};
    std::atomic<bool> multiConsumerWarning_{false};    // [Worker write, GUI read]
    std::atomic<uint32_t> cachedSampleRate_{0};        // [Worker write, GUI read]
    std::atomic<uint32_t> cachedChannels_{0};          // [Worker write, GUI read]

    std::vector<float> interleavedBuffer_;

    // Event-only transitions: steady audio is untouched. Keep the saved waveform
    // for underrun tails, but join it to the last emitted sample before tapering.
    // All state is fixed-size and persists across variable-sized callbacks.
    static constexpr int kTransitionSamples = 64;
    using OutputFrame = std::array<float, directpipe::DEFAULT_CHANNELS>;
    using OutputHistory = std::array<std::array<float, kTransitionSamples>, directpipe::DEFAULT_CHANNELS>;
    OutputFrame lastOutput_{};
    OutputFrame transitionStart_{};
    OutputHistory outputHistory_{};
    OutputHistory transitionHistory_{};
    int outputHistoryPosition_ = 0;
    int transitionPosition_ = kTransitionSamples;
    bool needsFadeIn_ = true;

    // Clock drift compensation
    int blocksSinceConnect_ = 0;
    static constexpr int kDriftCheckWarmup = 50;  // ignore first N blocks

    // Buffer presets: { targetFill, highThreshold, lowThreshold }
    // Index matches the unchanged "buffer" parameter/state IDs. Runtime trimming
    // retains at least the current callback size; reported latency stays targetFill.
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
    void resetOutputState() noexcept;
    void beginTransition() noexcept;
    float nextTransitionBlend() noexcept;
    void rememberOutput(const OutputFrame& frame) noexcept;
    void applyFadeOut(juce::AudioBuffer<float>& buffer, int startSample, int numSamples, int numChannels);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DirectPipeReceiverProcessor)
};
