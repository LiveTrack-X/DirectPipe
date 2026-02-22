/**
 * @file SharedMemWriter.h
 * @brief Producer-side shared memory writer for OBS IPC
 *
 * Writes processed PCM audio into the shared ring buffer
 * and signals the OBS plugin that new data is available.
 */
#pragma once

#include <JuceHeader.h>
#include "directpipe/RingBuffer.h"
#include "directpipe/SharedMemory.h"
#include "directpipe/Constants.h"

#include <atomic>

namespace directpipe {

/**
 * @brief Writes audio to shared memory for the OBS plugin to read.
 *
 * Creates and manages the shared memory region and named event.
 * Safe to call write methods from the real-time audio thread.
 */
class SharedMemWriter {
public:
    SharedMemWriter();
    ~SharedMemWriter();

    /**
     * @brief Initialize shared memory and event objects.
     * @param sampleRate Audio sample rate.
     * @param channels Number of channels (default 2).
     * @param bufferFrames Ring buffer capacity in frames.
     * @return true if initialization succeeded.
     */
    bool initialize(uint32_t sampleRate,
                    uint32_t channels = DEFAULT_CHANNELS,
                    uint32_t bufferFrames = DEFAULT_BUFFER_FRAMES);

    /**
     * @brief Shut down and release shared memory resources.
     */
    void shutdown();

    /**
     * @brief Write audio data to the shared ring buffer.
     *
     * Called from the real-time audio thread. No allocations, no locks.
     *
     * @param buffer JUCE audio buffer with processed audio.
     * @param numSamples Number of samples to write.
     */
    void writeAudio(const juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Check if the shared memory is active and connected.
     */
    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the number of frames dropped due to buffer overrun.
     */
    uint64_t getDroppedFrames() const { return droppedFrames_.load(std::memory_order_relaxed); }

private:
    SharedMemory sharedMemory_;
    NamedEvent dataEvent_;
    RingBuffer ringBuffer_;

    // Pre-allocated interleave buffer (for converting JUCE's
    // non-interleaved format to interleaved for the ring buffer)
    std::vector<float> interleaveBuffer_;

    std::atomic<bool> connected_{false};
    std::atomic<uint64_t> droppedFrames_{0};

    uint32_t channels_ = DEFAULT_CHANNELS;
};

} // namespace directpipe
