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
        1));  // default: Low (512 frames, ~10.7ms at 48kHz)
    return { params.begin(), params.end() };
}

void DirectPipeReceiverProcessor::prepareToPlay(double /*sampleRate*/, int samplesPerBlock)
{
    stopConnectionWorker();

    const size_t maxCh = directpipe::DEFAULT_CHANNELS;

    // Hosts can deliver a larger callback than this hint. Read those callbacks in
    // chunks through this scratch buffer instead of allocating or dropping audio.
    interleavedBuffer_.resize(static_cast<size_t>((std::max)(samplesPerBlock, 64)) * maxCh, 0.0f);
    resetOutputState();
    blocksSinceConnect_ = 0;
    rtConnectionSerial_ = connectionSerial_.load(std::memory_order_relaxed);

    // Report the selected target fill, not queue capacity or measured end-to-end latency.
    const auto latency = static_cast<int>(getTargetFillFrames());
    requestedLatencySamples_.store(latency, std::memory_order_relaxed);
    setLatencySamples(latency);

    startConnectionWorker();
}

void DirectPipeReceiverProcessor::releaseResources()
{
    stopConnectionWorker();
    resetOutputState();
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

    // Zero-length host callbacks must not consume data or disturb a pending fade.
    if (numSamples == 0)
        return;

    buffer.clear();
    auto* muteParam = apvts_.getRawParameterValue("mute");
    const bool muted = muteParam && muteParam->load() >= 0.5f;

    ConnectionLease connectionLease(*this);
    auto* connection = connectionLease.get();
    if (connection == nullptr) {
        if (muted)
            resetOutputState();
        else
            applyFadeOut(buffer, 0, numSamples, numChannels);
        return;
    }

    auto& ringBuffer = *connection;
    if (!ringBuffer.isProducerActive()
        || ringBuffer.getCurrentProducerGeneration()
               != ringBuffer.getAttachedProducerGeneration()) {
        reconnectRequested_.store(true, std::memory_order_release);
        if (muted)
            resetOutputState();
        else
            applyFadeOut(buffer, 0, numSamples, numChannels);
        return;
    }

    const auto connectionSerial = connectionSerial_.load(std::memory_order_relaxed);
    if (connectionSerial != rtConnectionSerial_) {
        rtConnectionSerial_ = connectionSerial;
        rtOverflowEpoch_ = 0;
        blocksSinceConnect_ = 0;
        beginTransition();
        needsFadeIn_ = true;
    }
    // Only the warmup state matters; do not overflow on long-running sessions.
    if (blocksSinceConnect_ <= kDriftCheckWarmup)
        ++blocksSinceConnect_;

    const auto overflowEpoch = ringBuffer.getOverflowEpoch();
    if (overflowEpoch != rtOverflowEpoch_) {
        rtOverflowEpoch_ = overflowEpoch;
        // FanOut reports incoming-frame loss when this queue filled. Flush only
        // this acknowledged consumer's stale backlog, then fade/recover across
        // callbacks. Legacy v1 has no overflow epoch; this branch is inactive there.
        ringBuffer.discard(ringBuffer.availableRead());
        if (muted)
            resetOutputState();
        else
            applyFadeOut(buffer, 0, numSamples, numChannels);
        return;
    }

    if (muted) {
        // While the containing host calls processBlock, local mute keeps this
        // queue drained. A suspended host issues no callbacks; if its FanOut
        // queue overflowed meanwhile, the epoch triggers recovery on resumption.
        ringBuffer.discard(ringBuffer.availableRead());
        resetOutputState();
        return;
    }

    uint32_t available = ringBuffer.availableRead();
    const uint32_t channels = ringBuffer.getChannels();
    const uint32_t requested = static_cast<uint32_t>(numSamples);

    // ── Clock drift compensation: skip excess when buffer is too full ──
    const uint32_t targetFill = (std::max)(getTargetFillFrames(), requested);
    const uint32_t highThreshold = (std::max)(getHighFillThreshold(), targetFill);

    if (blocksSinceConnect_ > kDriftCheckWarmup && available > highThreshold) {
        const uint32_t excess = available - targetFill;
        ringBuffer.discard(excess);
        available = ringBuffer.availableRead();
        beginTransition();
    }

    // ── Clock drift compensation: throttle reads when buffer is running low ──
    // Dead-band: between lowThreshold/2 and lowThreshold, normal reading occurs
    // without throttling — prevents oscillation between throttle and normal mode.
    const uint32_t lowThreshold = getLowFillThreshold();
    if (blocksSinceConnect_ > kDriftCheckWarmup && available > 0
        && available < requested && available < lowThreshold / 2) {
        // A shortage may reflect drift or scheduling. Keep the historical cushion
        // only for an already-partial callback; never reduce a fully available
        // callback. Missing output frames fade continuously into silence below.
        const uint32_t cushionRead = (std::min)(available, requested / 2);
        available = cushionRead;
    }

    // De-interleave in bounded scratch chunks, including callbacks larger than
    // the prepareToPlay hint. No heap allocation or mapping work occurs here.
    const uint32_t toRead = (std::min)(available, requested);
    const uint32_t maxFrames = static_cast<uint32_t>(interleavedBuffer_.size())
                            / (std::max)(channels, 1u);
    const int outputChannels = (std::min)(numChannels, static_cast<int>(directpipe::DEFAULT_CHANNELS));
    uint32_t totalRead = 0;
    while (totalRead < toRead) {
        const uint32_t chunk = (std::min)(toRead - totalRead, maxFrames);
        const uint32_t readCount = ringBuffer.read(interleavedBuffer_.data(), chunk);
        if (readCount == 0)
            break;

        if (needsFadeIn_) {
            beginTransition();
            needsFadeIn_ = false;
        }

        for (uint32_t i = 0; i < readCount; ++i) {
            const float blend = nextTransitionBlend();
            OutputFrame frame{};
            for (int ch = 0; ch < outputChannels; ++ch) {
                const auto channel = static_cast<size_t>(ch);
                const float sample = channel < channels
                    ? interleavedBuffer_[static_cast<size_t>(i) * channels + channel] : 0.0f;
                frame[channel] = transitionStart_[channel] * (1.0f - blend) + sample * blend;
                buffer.setSample(ch, static_cast<int>(totalRead + i), frame[channel]);
            }
            rememberOutput(frame);
        }
        totalRead += readCount;
    }

    if (totalRead < requested)
        applyFadeOut(buffer, static_cast<int>(totalRead), numSamples - static_cast<int>(totalRead), numChannels);
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
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    if (isolatedConnectionForTest_)
        return;
#endif
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
            auto& ringBuffer = *workerConnection_;
            shouldReconnect = shouldReconnect
                || !ringBuffer.isProducerActive()
                || ringBuffer.getCurrentProducerGeneration()
                       != ringBuffer.getAttachedProducerGeneration();

#ifndef _WIN32
            // POSIX unlink/recreate leaves an existing mapping valid but stale.
            // Probe the name without attaching and compare the underlying object.
            directpipe::SharedMemory probe;
            const auto& mappingName = workerConnection_->usesFanOut ? fanOutMappingName_ : legacyMappingName_;
            if (probe.open(mappingName, 0)
                && probe.getObjectIdentity() != workerConnection_->sharedMemory.getObjectIdentity()) {
                shouldReconnect = true;
            }
#endif
            if (!workerConnection_->usesFanOut) {
                // Upgrade a legacy connection when the new host becomes available.
                // A non-claiming probe cannot consume another reader's slot.
                directpipe::SharedMemory probe;
                if (probe.open(fanOutMappingName_, 0)
                    && directpipe::FanOutConsumer::isAvailable(probe.getData(), probe.getSize()))
                    shouldReconnect = true;
            }
        } else if (pendingConnection_ != nullptr) {
            shouldReconnect = shouldReconnect || !pendingConnection_->isProducerActive()
                || pendingConnection_->getCurrentProducerGeneration()
                       != pendingConnection_->getAttachedProducerGeneration();
#ifndef _WIN32
            // A producer can die before acknowledging this claim. POSIX may
            // replace its named object while this pending mapping still looks
            // active, so pending and published connections need the same probe.
            directpipe::SharedMemory probe;
            if (probe.open(fanOutMappingName_, 0)
                && probe.getObjectIdentity() != pendingConnection_->sharedMemory.getObjectIdentity())
                shouldReconnect = true;
#endif
            if (!shouldReconnect && pendingConnection_->isReady())
                publishConnection(std::move(pendingConnection_));
        } else {
            shouldReconnect = true;
        }

        if (shouldReconnect) {
            retireConnection();
            if (auto connection = openConnection()) {
                if (connection->isReady())
                    publishConnection(std::move(connection));
                else {
                    pendingConnection_ = std::move(connection);
                    connectionState_.store(ConnectionState::Waiting, std::memory_order_relaxed);
                }
            }
        }

        for (int i = 0; i < 10
             && !connectionWorkerStopRequested_.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(10ms);
    }
}

std::unique_ptr<DirectPipeReceiverProcessor::Connection>
DirectPipeReceiverProcessor::openConnection()
{
    // Prefer FanOut. Retain Waiting claims until ack; Full/Invalid are explicit
    // states and must not consume the shared legacy queue as a hidden ninth slot.
    // Only absent/inactive FanOut allows the older-host compatibility path.
    auto connection = std::make_unique<Connection>();
    if (connection->sharedMemory.open(fanOutMappingName_, 0)) {
        const auto result = connection->fanOut.claim(connection->sharedMemory.getData(),
                                                    connection->sharedMemory.getSize());
        if (result == directpipe::FanOutAttachResult::Ready
            || result == directpipe::FanOutAttachResult::Waiting) {
            connection->usesFanOut = true;
            return connection;
        }
        if (result == directpipe::FanOutAttachResult::Full) {
            connectionState_.store(ConnectionState::LimitReached, std::memory_order_relaxed);
            return {};
        }
        if (result == directpipe::FanOutAttachResult::Invalid) {
            connectionState_.store(ConnectionState::Incompatible, std::memory_order_relaxed);
            return {};
        }
        connection->sharedMemory.close();
    }
    if (!connection->sharedMemory.open(legacyMappingName_, 0))
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
    // A newly acknowledged queue can fill while the ~100ms worker waits. Trim
    // before publishing so connecting does not add a worker-period of latency.
    skipToFreshPosition(*connection);
    workerConnection_ = std::move(connection);

    multiConsumerWarning_.store(!workerConnection_->usesFanOut && workerConnection_->ringBuffer.anotherConsumerWasActive(),
                                std::memory_order_relaxed);
    cachedSampleRate_.store(workerConnection_->getSampleRate(),
                            std::memory_order_relaxed);
    cachedChannels_.store(workerConnection_->getChannels(),
                          std::memory_order_relaxed);
    connectionSerial_.fetch_add(1, std::memory_order_relaxed);
    activeConnection_.store(workerConnection_.get(), std::memory_order_release);
    connected_.store(true, std::memory_order_release);
    connectionState_.store(workerConnection_->usesFanOut ? ConnectionState::Connected : ConnectionState::Legacy,
                           std::memory_order_relaxed);
    connectionAccessEnabled_.store(true, std::memory_order_seq_cst);
}

void DirectPipeReceiverProcessor::retireConnection()
{
    // Control/worker side only. Pending claims have no audio users; published
    // connections must drain local leases before their owner token is released.
    connectionAccessEnabled_.store(false, std::memory_order_seq_cst);
    while (connectionUsers_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();

    activeConnection_.store(nullptr, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    connectionState_.store(ConnectionState::Disconnected, std::memory_order_relaxed);
    multiConsumerWarning_.store(false, std::memory_order_relaxed);
    cachedSampleRate_.store(0, std::memory_order_relaxed);
    cachedChannels_.store(0, std::memory_order_relaxed);
    workerConnection_.reset();
    pendingConnection_.reset();
}

void DirectPipeReceiverProcessor::skipToFreshPosition(Connection& connection)
{
    const uint32_t targetFill = getTargetFillFrames();
    const uint32_t available = connection.availableRead();
    if (available > targetFill)
        connection.discard(available - targetFill);
}

void DirectPipeReceiverProcessor::resetOutputState() noexcept
{
    lastOutput_.fill(0.0f);
    transitionStart_.fill(0.0f);
    for (auto& channel : outputHistory_)
        channel.fill(0.0f);
    for (auto& channel : transitionHistory_)
        channel.fill(0.0f);
    outputHistoryPosition_ = 0;
    transitionPosition_ = kTransitionSamples;
    needsFadeIn_ = true;
}

void DirectPipeReceiverProcessor::beginTransition() noexcept
{
    transitionStart_ = lastOutput_;
    for (size_t ch = 0; ch < outputHistory_.size(); ++ch)
        for (int i = 0; i < kTransitionSamples; ++i)
            transitionHistory_[ch][static_cast<size_t>(i)] = outputHistory_[ch][
                static_cast<size_t>((outputHistoryPosition_ + i) % kTransitionSamples)];
    transitionPosition_ = 0;
}

float DirectPipeReceiverProcessor::nextTransitionBlend() noexcept
{
    if (transitionPosition_ >= kTransitionSamples)
        return 1.0f;
    return static_cast<float>(transitionPosition_++) / static_cast<float>(kTransitionSamples - 1);
}

void DirectPipeReceiverProcessor::rememberOutput(const OutputFrame& frame) noexcept
{
    lastOutput_ = frame;
    for (size_t ch = 0; ch < frame.size(); ++ch)
        outputHistory_[ch][static_cast<size_t>(outputHistoryPosition_)] = frame[ch];
    outputHistoryPosition_ = (outputHistoryPosition_ + 1) % kTransitionSamples;
}

void DirectPipeReceiverProcessor::applyFadeOut(juce::AudioBuffer<float>& buffer,
                                                int startSample, int numSamples, int numChannels)
{
    if (!needsFadeIn_) {
        beginTransition();
        needsFadeIn_ = true;
    }

    const int outputChannels = (std::min)(numChannels, static_cast<int>(directpipe::DEFAULT_CHANNELS));
    for (int i = 0; i < numSamples; ++i) {
        const auto historySample = static_cast<size_t>((std::min)(transitionPosition_, kTransitionSamples - 1));
        const float blend = nextTransitionBlend();
        const float gain = 1.0f - blend;
        OutputFrame frame{};
        for (int ch = 0; ch < outputChannels; ++ch) {
            const auto channel = static_cast<size_t>(ch);
            // Preserve the saved waveform-tail intent without jumping backwards
            // to its first sample. Both endpoints and the amplitude are bounded:
            // first = last emitted sample; sample 64 and later = exact silence.
            const float tail = transitionStart_[channel] * gain
                             + transitionHistory_[channel][historySample] * blend;
            frame[channel] = tail * gain;
            buffer.setSample(ch, startSample + i, frame[channel]);
        }
        rememberOutput(frame);
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
    // Host parameter notifications may arrive on an audio thread. This existing
    // JUCE async notification schedules setLatencySamples on the message thread;
    // it is separate from the allocation/OS-free transport read in processBlock.
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
    // GUI reads worker-cached metadata without dereferencing either shared mapping.
    return cachedSampleRate_.load(std::memory_order_relaxed);
}

uint32_t DirectPipeReceiverProcessor::getSourceChannels() const
{
    // GUI reads worker-cached metadata without dereferencing either shared mapping.
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
