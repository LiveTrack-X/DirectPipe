// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "BuiltinFilter.h"
#include "../UI/FilterEditPanel.h"

namespace directpipe {

juce::AudioProcessorEditor* BuiltinFilter::createEditor()
{
    return new FilterEditPanel(*this);
}

BuiltinFilter::BuiltinFilter()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void BuiltinFilter::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate_ = sampleRate;
    lastHPFFreq_ = 0.0f;  // force coefficient update
    lastLPFFreq_ = 0.0f;
    hpfL_.reset();
    hpfR_.reset();
    lpfL_.reset();
    lpfR_.reset();
    updateFilterCoeffs();
}

void BuiltinFilter::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Check if frequencies changed (atomic read)
    float hpfF = hpfFreq_.load(std::memory_order_relaxed);
    float lpfF = lpfFreq_.load(std::memory_order_relaxed);
    if (hpfF != lastHPFFreq_ || lpfF != lastLPFFreq_)
        updateFilterCoeffs();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // HPF
    if (hpfEnabled_.load(std::memory_order_relaxed)) {
        if (numChannels > 0)
            hpfL_.processSamples(buffer.getWritePointer(0), numSamples);
        if (numChannels > 1)
            hpfR_.processSamples(buffer.getWritePointer(1), numSamples);
    }

    // LPF
    if (lpfEnabled_.load(std::memory_order_relaxed)) {
        if (numChannels > 0)
            lpfL_.processSamples(buffer.getWritePointer(0), numSamples);
        if (numChannels > 1)
            lpfR_.processSamples(buffer.getWritePointer(1), numSamples);
    }
}

void BuiltinFilter::updateFilterCoeffs()
{
    float hpfF = hpfFreq_.load(std::memory_order_relaxed);
    float lpfF = lpfFreq_.load(std::memory_order_relaxed);

    if (currentSampleRate_ > 0.0) {
        auto hpfCoeffs = juce::IIRCoefficients::makeHighPass(currentSampleRate_, static_cast<double>(hpfF));
        hpfL_.setCoefficients(hpfCoeffs);
        hpfR_.setCoefficients(hpfCoeffs);

        auto lpfCoeffs = juce::IIRCoefficients::makeLowPass(currentSampleRate_, static_cast<double>(lpfF));
        lpfL_.setCoefficients(lpfCoeffs);
        lpfR_.setCoefficients(lpfCoeffs);
    }

    lastHPFFreq_ = hpfF;
    lastLPFFreq_ = lpfF;
}

void BuiltinFilter::setHPFEnabled(bool enabled)
{
    hpfEnabled_.store(enabled, std::memory_order_relaxed);
}

void BuiltinFilter::setHPFFrequency(float hz)
{
    hpfFreq_.store(juce::jlimit(20.0f, 300.0f, hz), std::memory_order_relaxed);
}

void BuiltinFilter::setLPFEnabled(bool enabled)
{
    lpfEnabled_.store(enabled, std::memory_order_relaxed);
}

void BuiltinFilter::setLPFFrequency(float hz)
{
    lpfFreq_.store(juce::jlimit(4000.0f, 20000.0f, hz), std::memory_order_relaxed);
}

void BuiltinFilter::getStateInformation(juce::MemoryBlock& destData)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("hpfEnabled", isHPFEnabled());
    obj->setProperty("hpfFrequency", static_cast<double>(getHPFFrequency()));
    obj->setProperty("lpfEnabled", isLPFEnabled());
    obj->setProperty("lpfFrequency", static_cast<double>(getLPFFrequency()));

    auto json = juce::JSON::toString(juce::var(obj.release()));
    destData.replaceWith(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BuiltinFilter::setStateInformation(const void* data, int sizeInBytes)
{
    auto json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    auto parsed = juce::JSON::parse(json);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("hpfEnabled"))
            setHPFEnabled(static_cast<bool>(obj->getProperty("hpfEnabled")));
        if (obj->hasProperty("hpfFrequency"))
            setHPFFrequency(static_cast<float>(static_cast<double>(obj->getProperty("hpfFrequency"))));
        if (obj->hasProperty("lpfEnabled"))
            setLPFEnabled(static_cast<bool>(obj->getProperty("lpfEnabled")));
        if (obj->hasProperty("lpfFrequency"))
            setLPFFrequency(static_cast<float>(static_cast<double>(obj->getProperty("lpfFrequency"))));
    }
}

} // namespace directpipe
