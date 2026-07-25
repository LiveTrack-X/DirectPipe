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
#include <algorithm>
#include <chrono>
#include <thread>

namespace directpipe {

namespace {

class InFlightWriteGuard {
public:
    explicit InFlightWriteGuard(std::atomic<uint32_t>& count) noexcept
        : count_(count)
    {
        count_.fetch_add(1, std::memory_order_seq_cst);
    }

    ~InFlightWriteGuard()
    {
        count_.fetch_sub(1, std::memory_order_seq_cst);
    }

private:
    std::atomic<uint32_t>& count_;
};

static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "SharedMemWriter RT guard must be lock-free");

bool createFreshSharedMemory(SharedMemory& sharedMemory, size_t size)
{
    if (!sharedMemory.create(SHM_NAME, size))
        return false;

#if defined(_WIN32)
    if (!sharedMemory.createOpenedExistingObject())
        return true;

    // A crashed producer can leave the Windows mapping alive while a Receiver
    // still owns a handle. Never placement-initialize that retained object: the
    // Receiver may be reading its header on another process's audio thread.
    // Publish an inactive producer, release our view, and wait until all retained
    // handles are gone. Only a genuinely fresh kernel object is safe to initialize.
    constexpr auto retryInterval = std::chrono::milliseconds(5);
    constexpr auto handoffTimeout = std::chrono::seconds(1);
    const auto deadline = std::chrono::steady_clock::now() + handoffTimeout;

    for (;;) {
        auto* header = static_cast<DirectPipeHeader*>(sharedMemory.getData());
        if (header != nullptr && sharedMemory.getSize() >= sizeof(DirectPipeHeader))
            header->producer_active.store(false, std::memory_order_release);

        sharedMemory.close();
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::sleep_for(retryInterval);
        if (!sharedMemory.create(SHM_NAME, size))
            return false;
        if (!sharedMemory.createOpenedExistingObject())
            return true;
    }
#else
    return true;
#endif
}

} // namespace

SharedMemWriter::SharedMemWriter() = default;

SharedMemWriter::~SharedMemWriter()
{
    shutdown();
}

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
void SharedMemWriterTestAccess::setWriteBarrier(SharedMemWriter& writer,
                                                WriteBarrier barrier,
                                                void* context)
{
    writer.testWriteBarrier_ = barrier;
    writer.testWriteBarrierContext_ = context;
}
#endif

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

    // Create a genuinely fresh region. On Windows, createFreshSharedMemory()
    // first retires any crash-retained mapping and fails closed if a Receiver
    // does not release it within the bounded handoff window.
    if (!createFreshSharedMemory(sharedMemory_, shmSize)) {
        juce::Logger::writeToLog(
            "[IPC] SharedMemWriter: Failed to acquire a fresh shared memory object");
        return false;
    }

    // Initialize ring buffer in the shared memory
    ringBuffer_.initAsProducer(sharedMemory_.getData(), bufferFrames, channels, sampleRate);

    // Create named event for signaling
    if (!dataEvent_.create(EVENT_NAME)) {
        juce::Logger::writeToLog("[IPC] SharedMemWriter: Failed to create named event");
        shutdown();
        return false;
    }

    // Pre-allocate interleave buffer (max expected buffer size × channels)
    interleaveBuffer_.resize(static_cast<size_t>(bufferFrames) * channels, 0.0f);

    connected_.store(true, std::memory_order_seq_cst);
    droppedFrames_.store(0, std::memory_order_relaxed);

    juce::Logger::writeToLog("[IPC] SharedMemWriter: Initialized - " +
                             juce::String(sampleRate) + "Hz, " +
                             juce::String(channels) + "ch, " +
                             juce::String(bufferFrames) + " frames buffer");

    return true;
}

void SharedMemWriter::shutdown()
{
    // Close admission first, then wait for callbacks that already entered.
    // Sequential consistency across connected_ and inFlightWriters_ prevents
    // a writer from slipping between the final zero check and unmapping.
    connected_.store(false, std::memory_order_seq_cst);
    while (inFlightWriters_.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();

    // Signal receiver that producer is gone BEFORE unmapping shared memory.
    // The receiver checks producer_active to detect clean disconnects.
    if (ringBuffer_.isValid()) {
        auto* data = sharedMemory_.getData();
        if (data) {
            auto* header = static_cast<directpipe::DirectPipeHeader*>(data);
            header->producer_active.store(false, std::memory_order_release);
        }
    }

    // Invalidate ring buffer pointers before unmapping shared memory. All writers
    // that passed admission have already drained above.
    ringBuffer_.detach();
    dataEvent_.close();
    sharedMemory_.close();
    // Keep the allocation for reuse on the next initialize().
}

void SharedMemWriter::writeAudio(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    // RT thread only — must NOT be called from the message thread
    jassert(!juce::MessageManager::getInstanceWithoutCreating()
            || !juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (numSamples <= 0) return;

    if (!connected_.load(std::memory_order_seq_cst)) return;
    InFlightWriteGuard inFlight(inFlightWriters_);
    if (!connected_.load(std::memory_order_seq_cst)) return;

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    if (testWriteBarrier_)
        testWriteBarrier_(testWriteBarrierContext_);
#endif

    const int numChannels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channels_));
    // Clamp to both source and interleaveBuffer_ capacity to prevent overrun.
    const auto maxFrames = interleaveBuffer_.size() / (std::max)(static_cast<size_t>(channels_), size_t{1});
    const auto requestedSamples = (std::min)(static_cast<size_t>(numSamples),
                                             static_cast<size_t>(buffer.getNumSamples()));
    const auto samples = (std::min)(requestedSamples, maxFrames);
    if (samples == 0)
        return;

    if (numChannels <= 0) {
        std::fill(interleaveBuffer_.begin(),
                  interleaveBuffer_.begin() + static_cast<std::ptrdiff_t>(samples * channels_),
                  0.0f);
    }
    else
    {
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

    // Signal the consumer only when data was actually written.
    // Skipping the signal when written==0 avoids an unnecessary kernel syscall
    // (SetEvent/sem_post) on every callback, reducing DPC overhead.
    if (written > 0)
        dataEvent_.signal();
}

} // namespace directpipe
