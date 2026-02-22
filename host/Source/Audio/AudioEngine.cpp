/**
 * @file AudioEngine.cpp
 * @brief Core audio engine implementation
 */

#include "AudioEngine.h"
#include <cmath>

namespace directpipe {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialize()
{
    // Initialize device manager with default settings
    auto result = deviceManager_.initialiseWithDefaultDevices(
        2,  // numInputChannels
        2   // numOutputChannels
    );

    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("AudioEngine init error: " + result);
        return false;
    }

    // Configure for WASAPI Exclusive mode with low latency
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup(setup);

        setup.bufferSize = currentBufferSize_;
        setup.sampleRate = currentSampleRate_;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, 2, true);
        setup.outputChannels.setRange(0, 2, true);

        auto setupResult = deviceManager_.setAudioDeviceSetup(setup, true);
        if (setupResult.isNotEmpty()) {
            juce::Logger::writeToLog("AudioEngine setup error: " + setupResult);
        }
    }

    // Register as the audio callback
    deviceManager_.addAudioCallback(this);

    // Initialize the output router
    outputRouter_.initialize(currentSampleRate_, currentBufferSize_);

    running_ = true;
    return true;
}

void AudioEngine::shutdown()
{
    if (!running_) return;

    running_ = false;
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
    outputRouter_.shutdown();
    vstChain_.releaseResources();
}

bool AudioEngine::setInputDevice(const juce::String& deviceName)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.inputDeviceName = deviceName;

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("Failed to set input device: " + result);
        return false;
    }
    return true;
}

bool AudioEngine::setOutputDevice(const juce::String& deviceName)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.outputDeviceName = deviceName;

    auto result = deviceManager_.setAudioDeviceSetup(setup, true);
    if (result.isNotEmpty()) {
        juce::Logger::writeToLog("Failed to set output device: " + result);
        return false;
    }
    return true;
}

void AudioEngine::setBufferSize(int bufferSize)
{
    currentBufferSize_ = bufferSize;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.bufferSize = bufferSize;
    deviceManager_.setAudioDeviceSetup(setup, true);
}

void AudioEngine::setSampleRate(double sampleRate)
{
    currentSampleRate_ = sampleRate;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    setup.sampleRate = sampleRate;
    deviceManager_.setAudioDeviceSetup(setup, true);
}

juce::StringArray AudioEngine::getAvailableInputDevices() const
{
    juce::StringArray devices;
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        devices = type->getDeviceNames(true);  // input devices
    }
    return devices;
}

juce::StringArray AudioEngine::getAvailableOutputDevices() const
{
    juce::StringArray devices;
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        devices = type->getDeviceNames(false);  // output devices
    }
    return devices;
}

// ═══════════════════════════════════════════════════════════════════
// Real-time audio callback — NO allocations, NO locks, NO I/O
// ═══════════════════════════════════════════════════════════════════

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    latencyMonitor_.markCallbackStart();

    // 1. Copy input data into a working buffer
    juce::AudioBuffer<float> buffer(
        juce::jmax(numInputChannels, numOutputChannels),
        numSamples);
    buffer.clear();

    for (int ch = 0; ch < numInputChannels; ++ch) {
        if (inputChannelData[ch] != nullptr) {
            buffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);
        }
    }

    // Measure input level (RMS)
    if (numInputChannels > 0 && inputChannelData[0] != nullptr) {
        float rms = calculateRMS(inputChannelData[0], numSamples);
        inputLevel_.store(rms, std::memory_order_relaxed);
    }

    // 2. Process through VST plugin chain (inline, zero additional latency)
    vstChain_.processBlock(buffer, numSamples);

    // 3. Route processed audio to all outputs
    outputRouter_.routeAudio(buffer, numSamples);

    // 4. Local monitor output (copy to this callback's output)
    if (monitorEnabled_) {
        for (int ch = 0; ch < numOutputChannels && ch < buffer.getNumChannels(); ++ch) {
            std::memcpy(outputChannelData[ch],
                        buffer.getReadPointer(ch),
                        sizeof(float) * static_cast<size_t>(numSamples));
        }
    } else {
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            std::memset(outputChannelData[ch], 0,
                        sizeof(float) * static_cast<size_t>(numSamples));
        }
    }

    // Measure output level
    if (numOutputChannels > 0) {
        float rms = calculateRMS(buffer.getReadPointer(0), numSamples);
        outputLevel_.store(rms, std::memory_order_relaxed);
    }

    latencyMonitor_.markCallbackEnd();
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (!device) return;

    currentSampleRate_ = device->getCurrentSampleRate();
    currentBufferSize_ = device->getCurrentBufferSizeSamples();

    vstChain_.prepareToPlay(currentSampleRate_, currentBufferSize_);
    outputRouter_.initialize(currentSampleRate_, currentBufferSize_);
    latencyMonitor_.reset(currentSampleRate_, currentBufferSize_);

    juce::Logger::writeToLog(
        "Audio device started: " + device->getName() +
        " @ " + juce::String(currentSampleRate_) + "Hz" +
        " / " + juce::String(currentBufferSize_) + " samples");
}

void AudioEngine::audioDeviceStopped()
{
    vstChain_.releaseResources();
    outputRouter_.shutdown();
    juce::Logger::writeToLog("Audio device stopped");
}

void AudioEngine::audioDeviceError(const juce::String& errorMessage)
{
    juce::Logger::writeToLog("Audio device error: " + errorMessage);
}

float AudioEngine::calculateRMS(const float* data, int numSamples)
{
    if (numSamples <= 0) return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        sum += data[i] * data[i];
    }
    return std::sqrt(sum / static_cast<float>(numSamples));
}

} // namespace directpipe
