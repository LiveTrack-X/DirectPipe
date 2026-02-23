/**
 * @file OutputRouter.cpp
 * @brief Audio output routing implementation
 */

#include "OutputRouter.h"
#include <cmath>

namespace directpipe {

OutputRouter::OutputRouter()
{
    // Monitor output disabled by default (user enables explicitly)
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

    // Initialize shared memory writer for OBS output
    shmWriter_.initialize(static_cast<uint32_t>(sampleRate));
}

void OutputRouter::shutdown()
{
    shmWriter_.shutdown();
}

void OutputRouter::routeAudio(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    // ── Output 1: Shared Memory → OBS Plugin ──
    if (outputs_[static_cast<int>(Output::SharedMemory)].enabled.load(std::memory_order_relaxed)) {
        float vol = outputs_[static_cast<int>(Output::SharedMemory)].volume.load(std::memory_order_relaxed);

        if (std::abs(vol - 1.0f) < 0.001f) {
            // Unity gain — write directly
            shmWriter_.writeAudio(buffer, numSamples);
        } else if (vol > 0.001f) {
            // Apply volume — only copy channels that exist in source
            for (int ch = 0; ch < numChannels; ++ch) {
                scaledBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                scaledBuffer_.applyGain(ch, 0, numSamples, vol);
            }
            // Clear unused channels to avoid stale data
            for (int ch = numChannels; ch < scaledBuffer_.getNumChannels(); ++ch) {
                scaledBuffer_.clear(ch, 0, numSamples);
            }
            shmWriter_.writeAudio(scaledBuffer_, numSamples);
        }

        // Update level meter
        if (numChannels > 0) {
            float rms = buffer.getRMSLevel(0, 0, numSamples) * vol;
            outputs_[static_cast<int>(Output::SharedMemory)].level.store(rms, std::memory_order_relaxed);
        }
    }

    // ── Output 2: Virtual Mic (WASAPI) → Discord/Zoom ──
    // Note: Virtual mic output is handled by configuring WASAPI to
    // output to the virtual audio device. The actual write happens
    // through the AudioDeviceManager's output configuration.
    // Here we just track the level.
    if (outputs_[static_cast<int>(Output::VirtualMic)].enabled.load(std::memory_order_relaxed)) {
        float vol = outputs_[static_cast<int>(Output::VirtualMic)].volume.load(std::memory_order_relaxed);
        if (numChannels > 0) {
            float rms = buffer.getRMSLevel(0, 0, numSamples) * vol;
            outputs_[static_cast<int>(Output::VirtualMic)].level.store(rms, std::memory_order_relaxed);
        }
    }

    // ── Output 3: Monitor → Headphones ──
    // Monitor output is handled in AudioEngine::audioDeviceIOCallbackWithContext
    // by copying buffer to the output channels. Here we track level.
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
    if (idx >= 0 && idx < kOutputCount) {
        outputs_[idx].volume.store(juce::jlimit(0.0f, 1.0f, volume), std::memory_order_relaxed);
    }
}

float OutputRouter::getVolume(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount) {
        return outputs_[idx].volume.load(std::memory_order_relaxed);
    }
    return 0.0f;
}

void OutputRouter::setEnabled(Output output, bool enabled)
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount) {
        outputs_[idx].enabled.store(enabled, std::memory_order_relaxed);
    }
}

bool OutputRouter::isEnabled(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount) {
        return outputs_[idx].enabled.load(std::memory_order_relaxed);
    }
    return false;
}

float OutputRouter::getLevel(Output output) const
{
    int idx = static_cast<int>(output);
    if (idx >= 0 && idx < kOutputCount) {
        return outputs_[idx].level.load(std::memory_order_relaxed);
    }
    return 0.0f;
}

bool OutputRouter::isOBSConnected() const
{
    return shmWriter_.isConnected();
}

} // namespace directpipe
