// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

DirectPipeReceiverProcessor::DirectPipeReceiverProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "Parameters", createParameterLayout())
{
    requestedLatencySamples_.store(static_cast<int>(getTargetFillFrames()),
                                   std::memory_order_relaxed);
    apvts_.addParameterListener("buffer", this);
}

DirectPipeReceiverProcessor::~DirectPipeReceiverProcessor()
{
    apvts_.removeParameterListener("buffer", this);
    cancelPendingUpdate();
    stopConnectionWorker();
}

juce::AudioProcessorValueTreeState::ParameterLayout
DirectPipeReceiverProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"mute", 1}, "Mute", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"buffer", 1}, "Buffer",
        juce::StringArray{"Ultra Low (256)", "Low (512)", "Medium (1024)", "High (2048)", "Safe (4096)"},
        1));  // default: Low (~10ms)
    return { params.begin(), params.end() };
}

void DirectPipeReceiverProcessor::prepareToPlay(double /*sampleRate*/, int samplesPerBlock)
{
    stopConnectionWorker();

    const size_t maxCh = directpipe::DEFAULT_CHANNELS;

    // Pre-allocate interleaved buffer (max block size * max channels)
    interleavedBuffer_.resize(static_cast<size_t>(samplesPerBlock) * maxCh, 0.0f);

    // Pre-allocate fade-out buffer (planar: channels * blockSize)
    // Ensure at least 64 * maxCh for the fade-out tail (saveLastOutput uses min(numSamples, 64))
    size_t fadeMin = 64u * maxCh;
    size_t blockAlloc = static_cast<size_t>(samplesPerBlock) * maxCh;
    lastOutputBuffer_.resize((std::max)(blockAlloc, fadeMin), 0.0f);
    lastOutputSamples_ = 0;
    lastOutputChannels_ = 0;
    hadAudioLastBlock_ = false;
    fadeGain_ = 0.0f;
    blocksSinceConnect_ = 0;
    rtConnectionSerial_ = connectionSerial_.load(std::memory_order_relaxed);

    // Report buffering latency to the host DAW
    const auto latency = static_cast<int>(getTargetFillFrames());
    requestedLatencySamples_.store(latency, std::memory_order_relaxed);
    setLatencySamples(latency);

    startConnectionWorker();
}

void DirectPipeReceiverProcessor::releaseResources()
{
    stopConnectionWorker();
    lastOutputSamples_ = 0;
}

void DirectPipeReceiverProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& /*midiMessages*/)
{
    if (interleavedBuffer_.empty()) {
        buffer.clear();
        return;
    }

    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Check mute parameter
    auto* muteParam = apvts_.getRawParameterValue("mute");
    if (muteParam && muteParam->load() >= 0.5f) {
        buffer.clear();
        hadAudioLastBlock_ = false;
        return;
    }

    ConnectionLease connectionLease(*this);
    auto* connection = connectionLease.get();
    if (connection == nullptr) {
        if (hadAudioLastBlock_) {
            applyFadeOut(buffer, numSamples, numChannels);
        } else {
            buffer.clear();
        }
        return;
    }

    auto& ringBuffer = connection->ringBuffer;
    if (!ringBuffer.isProducerActive()
        || ringBuffer.getCurrentProducerGeneration()
               != ringBuffer.getAttachedProducerGeneration()) {
        reconnectRequested_.store(true, std::memory_order_release);
        if (hadAudioLastBlock_) {
            applyFadeOut(buffer, numSamples, numChannels);
        } else {
            buffer.clear();
        }
        return;
    }

    const auto connectionSerial = connectionSerial_.load(std::memory_order_relaxed);
    if (connectionSerial != rtConnectionSerial_) {
        rtConnectionSerial_ = connectionSerial;
        blocksSinceConnect_ = 0;
    }
    ++blocksSinceConnect_;

    uint32_t available = ringBuffer.availableRead();
    uint32_t channels = ringBuffer.getChannels();

    // ── Clock drift compensation: skip excess when buffer is too full ──
    uint32_t targetFill = getTargetFillFrames();

    uint32_t highThreshold = getHighFillThreshold();

    if (blocksSinceConnect_ > kDriftCheckWarmup && available > highThreshold) {
        uint32_t excess = available - targetFill;
        ringBuffer.discard(excess);
        available = ringBuffer.availableRead();
    }

    // ── Clock drift compensation: throttle reads when buffer is running low ──
    // Dead-band: between lowThreshold/2 and lowThreshold, normal reading occurs
    // without throttling — prevents oscillation between throttle and normal mode.
    uint32_t lowThreshold = getLowFillThreshold();
    if (blocksSinceConnect_ > kDriftCheckWarmup && available > 0 && available < lowThreshold / 2) {
        // Buffer running low — host clock is slightly slower than DAW clock.
        // Reduce read amount to leave a buffer cushion, preventing hard underrun.
        // The unread portion of the output buffer gets zero-padded (existing behavior),
        // creating micro-gaps instead of hard clicks.
        uint32_t cushionRead = (std::min)(available, static_cast<uint32_t>(numSamples) / 2);
        available = cushionRead;
    }

    // ── Read whatever is available (partial read OK — pad rest with silence) ──
    uint32_t toRead = (std::min)(available, static_cast<uint32_t>(numSamples));

    if (toRead == 0) {
        // Complete underrun — no data at all
        if (hadAudioLastBlock_) {
            applyFadeOut(buffer, numSamples, numChannels);
        } else {
            buffer.clear();
        }
        return;
    }

    // Clamp read to pre-allocated interleaved buffer capacity (no heap alloc in RT callback)
    uint32_t maxFrames = static_cast<uint32_t>(interleavedBuffer_.size()) / (std::max)(channels, 1u);
    if (toRead > maxFrames)
        toRead = maxFrames;

    uint32_t readCount = ringBuffer.read(interleavedBuffer_.data(), toRead);
    if (readCount == 0) {
        buffer.clear();
        return;
    }

    // De-interleave: [L0 R0 L1 R1 ...] → JUCE planar [L0 L1 ...][R0 R1 ...]
    int actualRead = static_cast<int>(readCount);
    for (int ch = 0; ch < numChannels && ch < static_cast<int>(channels); ++ch) {
        float* dest = buffer.getWritePointer(ch);
        for (int i = 0; i < actualRead; ++i)
            dest[i] = interleavedBuffer_[static_cast<size_t>(i) * channels + static_cast<size_t>(ch)];
    }

    // Clear remaining channels
    for (int ch = static_cast<int>(channels); ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Pad remaining samples with silence (partial read)
    if (actualRead < numSamples) {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.clear(ch, actualRead, numSamples - actualRead);
    }

    // Save state for fade-out
    saveLastOutput(buffer, numSamples, numChannels);
    hadAudioLastBlock_ = true;
    fadeGain_ = 1.0f;
}

DirectPipeReceiverProcessor::ConnectionLease::ConnectionLease(
    DirectPipeReceiverProcessor& owner) noexcept
    : owner_(owner), connection_(owner_.acquireConnection())
{
}

DirectPipeReceiverProcessor::ConnectionLease::~ConnectionLease()
{
    if (connection_ != nullptr)
        owner_.releaseConnection();
}

DirectPipeReceiverProcessor::Connection*
DirectPipeReceiverProcessor::acquireConnection() noexcept
{
    if (!connectionAccessEnabled_.load(std::memory_order_seq_cst))
        return nullptr;

    connectionUsers_.fetch_add(1, std::memory_order_seq_cst);
    if (!connectionAccessEnabled_.load(std::memory_order_seq_cst)) {
        connectionUsers_.fetch_sub(1, std::memory_order_seq_cst);
        return nullptr;
    }

    auto* connection = activeConnection_.load(std::memory_order_acquire);
    if (connection == nullptr)
        connectionUsers_.fetch_sub(1, std::memory_order_seq_cst);
    return connection;
}

void DirectPipeReceiverProcessor::releaseConnection() noexcept
{
    connectionUsers_.fetch_sub(1, std::memory_order_seq_cst);
}

void DirectPipeReceiverProcessor::startConnectionWorker()
{
    if (connectionThread_.joinable())
        return;

    connectionWorkerStopRequested_.store(false, std::memory_order_release);
    reconnectRequested_.store(true, std::memory_order_release);
    connectionThread_ = std::thread([this] { connectionWorkerLoop(); });
}

void DirectPipeReceiverProcessor::stopConnectionWorker()
{
    connectionWorkerStopRequested_.store(true, std::memory_order_release);
    if (connectionThread_.joinable())
        connectionThread_.join();
    retireConnection();
}

void DirectPipeReceiverProcessor::connectionWorkerLoop()
{
    using namespace std::chrono_literals;

    while (!connectionWorkerStopRequested_.load(std::memory_order_acquire)) {
        bool shouldReconnect = reconnectRequested_.exchange(false, std::memory_order_acq_rel);

        if (workerConnection_ != nullptr) {
            auto& ringBuffer = workerConnection_->ringBuffer;
            shouldReconnect = shouldReconnect
                || !ringBuffer.isProducerActive()
                || ringBuffer.getCurrentProducerGeneration()
                       != ringBuffer.getAttachedProducerGeneration();

#ifndef _WIN32
            // POSIX unlink/recreate leaves an existing mapping valid but stale.
            // Probe the name without attaching and compare the underlying object.
            directpipe::SharedMemory probe;
            if (probe.open(directpipe::SHM_NAME, 0)
                && probe.getObjectIdentity() != workerConnection_->sharedMemory.getObjectIdentity()) {
                shouldReconnect = true;
            }
#endif
        } else {
            shouldReconnect = true;
        }

        if (shouldReconnect) {
            retireConnection();
            if (auto connection = openConnection())
                publishConnection(std::move(connection));
        }

        for (int i = 0; i < 10
             && !connectionWorkerStopRequested_.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(10ms);
    }
}

std::unique_ptr<DirectPipeReceiverProcessor::Connection>
DirectPipeReceiverProcessor::openConnection()
{
    auto connection = std::make_unique<Connection>();
    if (!connection->sharedMemory.open(directpipe::SHM_NAME, 0))
        return {};
    if (!connection->ringBuffer.attachAsConsumer(connection->sharedMemory.getData(),
                                                  connection->sharedMemory.getSize()))
        return {};
    if (!connection->ringBuffer.isProducerActive())
        return {};

    skipToFreshPosition(*connection);
    return connection;
}

void DirectPipeReceiverProcessor::publishConnection(std::unique_ptr<Connection> connection)
{
    jassert(workerConnection_ == nullptr);
    workerConnection_ = std::move(connection);

    multiConsumerWarning_.store(workerConnection_->ringBuffer.anotherConsumerWasActive(),
                                std::memory_order_relaxed);
    cachedSampleRate_.store(workerConnection_->ringBuffer.getSampleRate(),
                            std::memory_order_relaxed);
    cachedChannels_.store(workerConnection_->ringBuffer.getChannels(),
                          std::memory_order_relaxed);
    connectionSerial_.fetch_add(1, std::memory_order_relaxed);
    activeConnection_.store(workerConnection_.get(), std::memory_order_release);
    connected_.store(true, std::memory_order_release);
    connectionAccessEnabled_.store(true, std::memory_order_seq_cst);
}

void DirectPipeReceiverProcessor::retireConnection()
{
    connectionAccessEnabled_.store(false, std::memory_order_seq_cst);
    while (connectionUsers_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();

    activeConnection_.store(nullptr, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    multiConsumerWarning_.store(false, std::memory_order_relaxed);
    cachedSampleRate_.store(0, std::memory_order_relaxed);
    cachedChannels_.store(0, std::memory_order_relaxed);
    workerConnection_.reset();
}

void DirectPipeReceiverProcessor::skipToFreshPosition(Connection& connection)
{
    const uint32_t targetFill = getTargetFillFrames();
    const uint32_t available = connection.ringBuffer.availableRead();
    if (available > targetFill)
        connection.ringBuffer.discard(available - targetFill);
}

void DirectPipeReceiverProcessor::saveLastOutput(const juce::AudioBuffer<float>& buffer,
                                                  int numSamples, int numChannels)
{
    // Store the tail of the output buffer for fade-out on underrun
    // Keep last 64 samples max (enough for a smooth fade)
    int samplesToSave = (std::min)(numSamples, 64);
    int offset = numSamples - samplesToSave;
    int chToSave = (std::min)(numChannels, static_cast<int>(directpipe::DEFAULT_CHANNELS));

    size_t needed = static_cast<size_t>(samplesToSave) * static_cast<size_t>(chToSave);
    jassert(lastOutputBuffer_.size() >= needed);  // pre-allocated in prepareToPlay
    juce::ignoreUnused(needed);

    for (int ch = 0; ch < chToSave; ++ch) {
        const float* src = buffer.getReadPointer(ch) + offset;
        float* dst = lastOutputBuffer_.data() + static_cast<size_t>(ch) * samplesToSave;
        std::memcpy(dst, src, static_cast<size_t>(samplesToSave) * sizeof(float));
    }
    lastOutputSamples_ = samplesToSave;
    lastOutputChannels_ = chToSave;
}

void DirectPipeReceiverProcessor::applyFadeOut(juce::AudioBuffer<float>& buffer,
                                                int numSamples, int numChannels)
{
    // Fade out from the last known audio to avoid clicks/pops
    if (fadeGain_ <= 0.0f || lastOutputSamples_ <= 0) {
        buffer.clear();
        hadAudioLastBlock_ = false;
        return;
    }

    // Generate a fade-out ramp using saved buffer data (not just the last sample)
    for (int ch = 0; ch < numChannels; ++ch) {
        float* dest = buffer.getWritePointer(ch);
        float gain = fadeGain_;

        for (int i = 0; i < numSamples; ++i) {
            if (gain <= 0.0f) {
                dest[i] = 0.0f;
            } else {
                float sample = 0.0f;
                if (ch < lastOutputChannels_ && lastOutputSamples_ > 0) {
                    // Use saved buffer data, clamping index to available range
                    int srcIdx = (std::min)(i, lastOutputSamples_ - 1);
                    sample = lastOutputBuffer_[
                        static_cast<size_t>(ch) * lastOutputSamples_ + srcIdx];
                }
                dest[i] = sample * gain;
                gain -= kFadeStep;
                if (gain < 0.0f) gain = 0.0f;
            }
        }
    }

    fadeGain_ = fadeGain_ - kFadeStep * static_cast<float>(numSamples);
    if (fadeGain_ <= 0.0f) {
        fadeGain_ = 0.0f;
        hadAudioLastBlock_ = false;
    }
}

uint32_t DirectPipeReceiverProcessor::getTargetFillFrames() const
{
    auto* param = apvts_.getRawParameterValue("buffer");
    int idx = param ? static_cast<int>(param->load()) : 1;
    if (idx < 0 || idx >= kNumBufferPresets) idx = 1;
    return kBufferPresets[idx][0];
}

uint32_t DirectPipeReceiverProcessor::getHighFillThreshold() const
{
    auto* param = apvts_.getRawParameterValue("buffer");
    int idx = param ? static_cast<int>(param->load()) : 1;
    if (idx < 0 || idx >= kNumBufferPresets) idx = 1;
    return kBufferPresets[idx][1];
}

uint32_t DirectPipeReceiverProcessor::getLowFillThreshold() const
{
    auto* param = apvts_.getRawParameterValue("buffer");
    int idx = param ? static_cast<int>(param->load()) : 1;
    if (idx < 0 || idx >= kNumBufferPresets) idx = 1;
    return kBufferPresets[idx][2];
}

void DirectPipeReceiverProcessor::parameterChanged(const juce::String& parameterID,
                                                    float newValue)
{
    if (parameterID != "buffer")
        return;

    int index = static_cast<int>(std::lround(newValue));
    if (index < 0 || index >= kNumBufferPresets)
        index = 1;
    requestedLatencySamples_.store(static_cast<int>(kBufferPresets[index][0]),
                                   std::memory_order_release);
    triggerAsyncUpdate();
}

void DirectPipeReceiverProcessor::handleAsyncUpdate()
{
    const int latency = requestedLatencySamples_.load(std::memory_order_acquire);
    if (latency != getLatencySamples())
        setLatencySamples(latency);
}

uint32_t DirectPipeReceiverProcessor::getSourceSampleRate() const
{
    // Return cached value — safe to call from GUI thread without touching ringBuffer_
    return cachedSampleRate_.load(std::memory_order_relaxed);
}

uint32_t DirectPipeReceiverProcessor::getSourceChannels() const
{
    // Return cached value — safe to call from GUI thread without touching ringBuffer_
    return cachedChannels_.load(std::memory_order_relaxed);
}

void DirectPipeReceiverProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DirectPipeReceiverProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* DirectPipeReceiverProcessor::createEditor()
{
    return new DirectPipeReceiverEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DirectPipeReceiverProcessor();
}
