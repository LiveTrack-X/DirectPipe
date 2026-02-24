/**
 * @file OutputRouter.cpp
 * @brief Audio output routing implementation
 */

#include "OutputRouter.h"
#include <cmath>

namespace directpipe {

OutputRouter::OutputRouter()
{
    // VirtualCable: ON by default (struct default enabled{true})
    // Monitor: OFF by default (user enables explicitly in Output tab)
    outputs_[static_cast<int>(Output::Monitor)].enabled.store(false, std::memory_order_relaxed);
}

OutputRouter::~OutputRouter()
{
    shutdown();
}

void OutputRouter::initialize(double sampleRate, int bufferSize)
{
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    // Pre-allocate the scaled buffer (stereo)
    scaledBuffer_.setSize(2, bufferSize);
    scaledBuffer_.clear();
}

void OutputRouter::shutdown()
{
}

void OutputRouter::routeAudio(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    // ── Output 1: Virtual Cable (second WASAPI device) ──
    if (outputs_[static_cast<int>(Output::VirtualCable)].enabled.load(std::memory_order_relaxed)
        && virtualMicOutput_ != nullptr)
    {
        float vol = outputs_[static_cast<int>(Output::VirtualCable)].volume.load(std::memory_order_relaxed);

        if (vol > 0.001f) {
            if (std::abs(vol - 1.0f) < 0.001f) {
                // Unity gain — write directly from buffer's channel pointers
                const float* channels[2] = {
                    buffer.getReadPointer(0),
                    numChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0)
                };
                virtualMicOutput_->writeAudio(channels, 2, numSamples);
            } else {
                // Apply volume to scaled buffer
                for (int ch = 0; ch < numChannels; ++ch) {
                    scaledBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                    scaledBuffer_.applyGain(ch, 0, numSamples, vol);
                }
                for (int ch = numChannels; ch < 2; ++ch)
                    scaledBuffer_.clear(ch, 0, numSamples);

                const float* channels[2] = {
                    scaledBuffer_.getReadPointer(0),
                    scaledBuffer_.getReadPointer(1)
                };
                virtualMicOutput_->writeAudio(channels, 2, numSamples);
            }
        }

        // Update level meter
        if (numChannels > 0) {
            float rms = buffer.getRMSLevel(0, 0, numSamples) * vol;
            outputs_[static_cast<int>(Output::VirtualCable)].level.store(rms, std::memory_order_relaxed);
        }
    }

    // ── Output 2: Monitor → Headphones ──
    // Actual write happens in AudioEngine::audioDeviceIOCallbackWithContext.
    // Here we only track the level.
    if (outputs_[static_cast<int>(Output::Monitor)].enabled.load(std::memory_order_relaxed)) {
        float vol = outputs_[static_cast<int>(Output::Monitor)].volume.load(std::memory_order_relaxed);
        if (numChannels > 0) {
            float rms = buffer.getRMSLevel(0, 0, numSamples) * vol;
            outputs_[static_cast<int>(Output::Monitor)].level.store(rms, std::memory_order_relaxed);
        }
    }
}

void OutputRouter::setVolume(Output output, float volume)
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        outputs_[idx].volume.store(juce::jlimit(0.0f, 1.0f, volume), std::memory_order_relaxed);
}

float OutputRouter::getVolume(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        return outputs_[idx].volume.load(std::memory_order_relaxed);
    return 0.0f;
}

void OutputRouter::setEnabled(Output output, bool enabled)
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        outputs_[idx].enabled.store(enabled, std::memory_order_relaxed);
}

bool OutputRouter::isEnabled(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        return outputs_[idx].enabled.load(std::memory_order_relaxed);
    return false;
}

float OutputRouter::getLevel(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount)
        return outputs_[idx].level.load(std::memory_order_relaxed);
    return 0.0f;
}

bool OutputRouter::isVirtualCableActive() const
{
    return virtualMicOutput_ != nullptr && virtualMicOutput_->isActive();
}

} // namespace directpipe
