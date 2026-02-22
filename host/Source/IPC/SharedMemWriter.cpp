/**
 * @file SharedMemWriter.cpp
 * @brief Producer-side shared memory writer implementation
 */

#include "SharedMemWriter.h"
#include "directpipe/Protocol.h"

namespace directpipe {

SharedMemWriter::SharedMemWriter() = default;

SharedMemWriter::~SharedMemWriter()
{
    shutdown();
}

bool SharedMemWriter::initialize(uint32_t sampleRate, uint32_t channels, uint32_t bufferFrames)
{
    shutdown();  // Clean up any previous state

    channels_ = channels;

    // Calculate shared memory size
    size_t shmSize = calculateSharedMemorySize(bufferFrames, channels);

    // Create shared memory region
    if (!sharedMemory_.create(SHM_NAME, shmSize)) {
        juce::Logger::writeToLog("SharedMemWriter: Failed to create shared memory");
        return false;
    }

    // Initialize ring buffer in the shared memory
    ringBuffer_.initAsProducer(sharedMemory_.getData(), bufferFrames, channels, sampleRate);

    // Create named event for signaling
    if (!dataEvent_.create(EVENT_NAME)) {
        juce::Logger::writeToLog("SharedMemWriter: Failed to create named event");
        sharedMemory_.close();
        return false;
    }

    // Pre-allocate interleave buffer (max expected buffer size × channels)
    interleaveBuffer_.resize(static_cast<size_t>(bufferFrames) * channels, 0.0f);

    connected_.store(true, std::memory_order_release);
    droppedFrames_.store(0, std::memory_order_relaxed);

    juce::Logger::writeToLog("SharedMemWriter: Initialized — " +
                             juce::String(sampleRate) + "Hz, " +
                             juce::String(channels) + "ch, " +
                             juce::String(bufferFrames) + " frames buffer");

    return true;
}

void SharedMemWriter::shutdown()
{
    connected_.store(false, std::memory_order_release);
    dataEvent_.close();
    sharedMemory_.close();
    interleaveBuffer_.clear();
}

void SharedMemWriter::writeAudio(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (numSamples <= 0) return;

    const int numChannels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channels_));
    const auto samples = static_cast<size_t>(numSamples);

    // Convert from JUCE's non-interleaved format to interleaved
    // JUCE: [L0 L1 L2 ...][R0 R1 R2 ...]
    // Ring buffer: [L0 R0 L1 R1 L2 R2 ...]
    if (channels_ == 1) {
        // Mono: just copy channel 0
        const float* src = buffer.getReadPointer(0);
        std::memcpy(interleaveBuffer_.data(), src, samples * sizeof(float));
    } else {
        // Stereo: interleave channels
        const float* left = buffer.getReadPointer(0);
        const float* right = numChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0);

        for (size_t i = 0; i < samples; ++i) {
            interleaveBuffer_[i * 2] = left[i];
            interleaveBuffer_[i * 2 + 1] = right[i];
        }
    }

    // Write to ring buffer (lock-free)
    uint32_t written = ringBuffer_.write(interleaveBuffer_.data(),
                                          static_cast<uint32_t>(numSamples));

    if (written < static_cast<uint32_t>(numSamples)) {
        // Buffer overrun — some frames were dropped
        droppedFrames_.fetch_add(
            static_cast<uint32_t>(numSamples) - written,
            std::memory_order_relaxed);
    }

    // Signal the consumer (OBS plugin) that new data is available
    dataEvent_.signal();
}

} // namespace directpipe
