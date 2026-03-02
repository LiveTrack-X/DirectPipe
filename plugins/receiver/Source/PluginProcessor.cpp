// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cstring>

DirectPipeReceiverProcessor::DirectPipeReceiverProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "Parameters", createParameterLayout())
{
}

DirectPipeReceiverProcessor::~DirectPipeReceiverProcessor()
{
    disconnect();
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
    const size_t maxCh = directpipe::DEFAULT_CHANNELS;

    // Pre-allocate interleaved buffer (max block size * max channels)
    interleavedBuffer_.resize(static_cast<size_t>(samplesPerBlock) * maxCh, 0.0f);

    // Pre-allocate fade-out buffer (planar: channels * blockSize)
    lastOutputBuffer_.resize(static_cast<size_t>(samplesPerBlock) * maxCh, 0.0f);
    lastOutputSamples_ = 0;
    lastOutputChannels_ = 0;
    hadAudioLastBlock_ = false;
    fadeGain_ = 0.0f;
    blocksSinceConnect_ = 0;

    reconnectCounter_ = 0;
    tryConnect();
}

void DirectPipeReceiverProcessor::releaseResources()
{
    disconnect();
}

void DirectPipeReceiverProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& /*midiMessages*/)
{
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

    // Try reconnect periodically if not connected
    if (!connected_.load(std::memory_order_relaxed)) {
        ++reconnectCounter_;
        if (reconnectCounter_ >= kReconnectInterval) {
            reconnectCounter_ = 0;
            tryConnect();
        }
        if (hadAudioLastBlock_) {
            applyFadeOut(buffer, numSamples, numChannels);
        } else {
            buffer.clear();
        }
        return;
    }

    // Check if producer is still active
    auto* shmData = sharedMemory_.getData();
    if (shmData) {
        auto* header = static_cast<directpipe::DirectPipeHeader*>(shmData);
        if (!header->producer_active.load(std::memory_order_acquire)) {
            disconnect();
            if (hadAudioLastBlock_) {
                applyFadeOut(buffer, numSamples, numChannels);
            } else {
                buffer.clear();
            }
            return;
        }
    }

    ++blocksSinceConnect_;

    uint32_t available = ringBuffer_.availableRead();
    uint32_t channels = ringBuffer_.getChannels();

    // ── Clock drift compensation: skip excess when buffer is too full ──
    uint32_t targetFill = getTargetFillFrames();
    uint32_t highThreshold = getHighFillThreshold();

    if (blocksSinceConnect_ > kDriftCheckWarmup && available > highThreshold) {
        uint32_t excess = available - targetFill;
        uint32_t chunkFrames = static_cast<uint32_t>(interleavedBuffer_.size()) / (std::max)(channels, 1u);
        while (excess > 0) {
            uint32_t chunk = (std::min)(excess, chunkFrames);
            uint32_t actualRead = ringBuffer_.read(interleavedBuffer_.data(), chunk);
            if (actualRead == 0) break;  // Defensive: avoid infinite loop
            excess -= (std::min)(actualRead, excess);
        }
        available = ringBuffer_.availableRead();
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

    uint32_t readCount = ringBuffer_.read(interleavedBuffer_.data(), toRead);
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

void DirectPipeReceiverProcessor::tryConnect()
{
    size_t shmSize = directpipe::calculateSharedMemorySize(
        directpipe::DEFAULT_BUFFER_FRAMES, directpipe::DEFAULT_CHANNELS);

    if (!sharedMemory_.open(directpipe::SHM_NAME, shmSize))
        return;

    if (!ringBuffer_.attachAsConsumer(sharedMemory_.getData())) {
        sharedMemory_.close();
        return;
    }

    // Verify producer is active
    auto* header = static_cast<directpipe::DirectPipeHeader*>(sharedMemory_.getData());
    if (!header->producer_active.load(std::memory_order_acquire)) {
        sharedMemory_.close();
        return;
    }

    // Skip to fresh position — minimal latency on connect
    skipToFreshPosition();

    blocksSinceConnect_ = 0;
    connected_.store(true, std::memory_order_release);
}

void DirectPipeReceiverProcessor::skipToFreshPosition()
{
    // On initial connection, advance read pointer close to write pointer
    // so we start reading the freshest audio with minimal latency.
    uint32_t targetFill = getTargetFillFrames();
    uint32_t available = ringBuffer_.availableRead();
    if (available > targetFill && !interleavedBuffer_.empty()) {
        uint32_t skip = available - targetFill;
        uint32_t channels = ringBuffer_.getChannels();
        uint32_t chunkFrames = static_cast<uint32_t>(interleavedBuffer_.size()) / (std::max)(channels, 1u);
        while (skip > 0) {
            uint32_t chunk = (std::min)(skip, chunkFrames);
            uint32_t actualRead = ringBuffer_.read(interleavedBuffer_.data(), chunk);
            if (actualRead == 0) break;  // Defensive: prevent infinite loop on read failure
            skip -= (std::min)(actualRead, skip);
        }
    }
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
    if (lastOutputBuffer_.size() < needed)
        lastOutputBuffer_.resize(needed, 0.0f);

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

    // Generate a fade-out ramp
    for (int ch = 0; ch < numChannels; ++ch) {
        float* dest = buffer.getWritePointer(ch);
        float gain = fadeGain_;

        for (int i = 0; i < numSamples; ++i) {
            if (gain <= 0.0f) {
                dest[i] = 0.0f;
            } else {
                // Use the last sample of the saved buffer as the "held" value
                // and fade it out quickly
                float lastSample = 0.0f;
                if (ch < lastOutputChannels_ && lastOutputSamples_ > 0) {
                    lastSample = lastOutputBuffer_[
                        static_cast<size_t>(ch) * lastOutputSamples_ + (lastOutputSamples_ - 1)];
                }
                dest[i] = lastSample * gain;
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

void DirectPipeReceiverProcessor::disconnect()
{
    connected_.store(false, std::memory_order_release);
    sharedMemory_.close();
}

uint32_t DirectPipeReceiverProcessor::getSourceSampleRate() const
{
    if (!connected_.load(std::memory_order_relaxed))
        return 0;
    return ringBuffer_.getSampleRate();
}

uint32_t DirectPipeReceiverProcessor::getSourceChannels() const
{
    if (!connected_.load(std::memory_order_relaxed))
        return 0;
    return ringBuffer_.getChannels();
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
