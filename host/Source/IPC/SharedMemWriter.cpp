// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file SharedMemWriter.cpp
 * @brief Producer-side shared memory writer implementation
 */

#include "SharedMemWriter.h"
#include "directpipe/Protocol.h"
#include <thread>

namespace directpipe {

SharedMemWriter::SharedMemWriter() = default;

SharedMemWriter::~SharedMemWriter()
{
    shutdown();
}

bool SharedMemWriter::initialize(uint32_t sampleRate, uint32_t channels, uint32_t bufferFrames)
{
    bool wasConnected = connected_.load(std::memory_order_relaxed);
    shutdown();  // Clean up any previous state (sets producer_active=false)

    // Brief pause after shutdown to let the consumer (Receiver VST) detect
    // producer_active=false and disconnect before we reinitialize the header.
    // Without this, initAsProducer stomps the header while the consumer reads it.
    if (wasConnected)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    channels_ = channels;

    // Calculate shared memory size
    size_t shmSize = calculateSharedMemorySize(bufferFrames, channels);

    // Create shared memory region
    if (!sharedMemory_.create(SHM_NAME, shmSize)) {
        juce::Logger::writeToLog("[IPC] SharedMemWriter: Failed to create shared memory");
        return false;
    }

    // Initialize ring buffer in the shared memory
    ringBuffer_.initAsProducer(sharedMemory_.getData(), bufferFrames, channels, sampleRate);

    // Create named event for signaling
    if (!dataEvent_.create(EVENT_NAME)) {
        juce::Logger::writeToLog("[IPC] SharedMemWriter: Failed to create named event");
        sharedMemory_.close();
        return false;
    }

    // Pre-allocate interleave buffer (max expected buffer size × channels)
    interleaveBuffer_.resize(static_cast<size_t>(bufferFrames) * channels, 0.0f);

    connected_.store(true, std::memory_order_release);
    droppedFrames_.store(0, std::memory_order_relaxed);

    juce::Logger::writeToLog("[IPC] SharedMemWriter: Initialized - " +
                             juce::String(sampleRate) + "Hz, " +
                             juce::String(channels) + "ch, " +
                             juce::String(bufferFrames) + " frames buffer");

    return true;
}

void SharedMemWriter::shutdown()
{
    connected_.store(false, std::memory_order_release);

    // Signal receiver that producer is gone BEFORE unmapping shared memory.
    // The receiver checks producer_active to detect clean disconnects.
    if (ringBuffer_.isValid()) {
        auto* data = sharedMemory_.getData();
        if (data) {
            auto* header = static_cast<directpipe::DirectPipeHeader*>(data);
            header->producer_active.store(false, std::memory_order_release);
        }
    }

    dataEvent_.close();
    sharedMemory_.close();
    interleaveBuffer_.clear();
}

void SharedMemWriter::writeAudio(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (numSamples <= 0) return;

    const int numChannels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channels_));
    // Clamp to interleaveBuffer_ capacity to prevent buffer overrun
    const auto maxFrames = interleaveBuffer_.size() / (std::max)(static_cast<size_t>(channels_), size_t{1});
    const auto samples = (std::min)(static_cast<size_t>(numSamples), maxFrames);

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
                                          static_cast<uint32_t>(samples));

    if (written < static_cast<uint32_t>(samples)) {
        // Buffer overrun — some frames were dropped
        droppedFrames_.fetch_add(
            static_cast<uint32_t>(samples) - written,
            std::memory_order_relaxed);
    }

    // Signal the consumer (OBS plugin) that new data is available
    dataEvent_.signal();
}

} // namespace directpipe
