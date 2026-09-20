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
 * @file RingBuffer.h
 * @brief Legacy v1 SPSC (Single Producer Single Consumer) ring buffer
 *
 * Designed to be placed directly in shared memory. Uses atomic operations
 * with acquire/release semantics between one host producer and one Receiver.
 * The additive FanOut transport provides independent queues for multiple
 * Receivers; it does not turn this legacy shared read cursor into multicast.
 */
#pragma once

#include "Protocol.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace directpipe {

class RingBuffer {
public:
    RingBuffer() = default;
    ~RingBuffer() = default;

    // Non-copyable, non-movable (operates on shared memory)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * @brief Initialize the ring buffer over a pre-allocated memory region.
     *
     * The memory region must be at least calculateSharedMemorySize() bytes.
     * The host calls this off the audio thread on a genuinely fresh mapping.
     * Never placement-initialize memory still mapped by a retained Receiver.
     *
     * @param memory Pointer to the shared memory region.
     * @param capacity_frames Ring buffer size in frames (must be power of 2).
     * @param channels Number of audio channels.
     * @param sample_rate Audio sample rate in Hz.
     */
    void initAsProducer(void* memory, uint32_t capacity_frames, uint32_t channels, uint32_t sample_rate);

    /**
     * @brief Attach to an existing ring buffer in shared memory.
     *
     * Called off the audio thread to connect to an already-initialized v1 buffer.
     * Atomically sets consumer_active. Duplicate attachment still succeeds;
     * anotherConsumerWasActive() normally warns, but the historical >80%-full
     * heuristic suppresses that warning. This is not proof that an owner died
     * and does not make multiple readers safe.
     *
     * @param memory Pointer to the shared memory region.
     * @param mappedSizeBytes Mapped size in bytes (0 to skip size checks).
     * @return true if the buffer is valid and version matches.
     */
    bool attachAsConsumer(void* memory, size_t mappedSizeBytes = 0);

    /**
     * @brief Legacy duplicate-reader warning, subject to the stale-fill heuristic.
     * Not an authoritative attachment count or process-liveness check.
     */
    bool anotherConsumerWasActive() const { return anotherConsumerWasActive_; }

    /**
     * @brief Write audio frames into the ring buffer (producer side).
     *
     * Lock-free. Safe to call from the real-time audio thread.
     * If the buffer is full, frames are dropped (overrun).
     *
     * @param data Interleaved float PCM samples (frames × channels).
     * @param frames Number of frames to write.
     * @return Number of frames actually written.
     */
    uint32_t write(const float* data, uint32_t frames);

    /**
     * @brief Read audio frames from the ring buffer (consumer side).
     *
     * Lock-free. Returns 0 if no data is available (underrun).
     *
     * @param data Output buffer for interleaved float PCM samples.
     * @param frames Maximum number of frames to read.
     * @return Number of frames actually read.
     */
    uint32_t read(float* data, uint32_t frames);

    /** Advance the consumer position without copying audio samples. */
    uint32_t discard(uint32_t frames);

    /**
     * @brief Number of frames available for reading.
     */
    uint32_t availableRead() const;

    /**
     * @brief Number of frames that can be written without overrun.
     */
    uint32_t availableWrite() const;

    /**
     * @brief Reset read and write positions to zero.
     * Only safe when both producer and consumer are stopped.
     */
    void reset();

    /**
     * @brief Get the number of channels.
     */
    uint32_t getChannels() const;

    /**
     * @brief Get the sample rate.
     */
    uint32_t getSampleRate() const;

    /**
     * @brief Get the buffer capacity in frames.
     */
    uint32_t getCapacity() const;

    /** Producer generation captured when this consumer attached. */
    uint64_t getAttachedProducerGeneration() const { return attachedGeneration_; }

    /** Current generation in the mapped header (may differ after a restart). */
    uint64_t getCurrentProducerGeneration() const {
        return header_ ? header_->producer_generation.load(std::memory_order_acquire) : 0;
    }

    bool isProducerActive() const {
        return header_ && header_->producer_active.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if the buffer has been initialized.
     */
    bool isValid() const { return header_ != nullptr && data_ != nullptr; }

    /**
     * @brief Detach from the shared memory region.
     *
     * Caller must first drain all local reads/writes using this view. Clears the
     * same-generation consumer_active flag and resets pointers; call before
     * closing the mapping. The flag is not a reference count for duplicate readers.
     */
    void detach() {
        detached_.store(true, std::memory_order_release);
        if (header_) {
            // Compatibility guard for an older producer that reinitialized a
            // retained mapping. Current hosts require a genuinely fresh object.
            const auto currentGeneration =
                header_->producer_generation.load(std::memory_order_acquire);
            if (currentGeneration == attachedGeneration_)
                header_->consumer_active.store(false, std::memory_order_release);
        }
        header_ = nullptr;
        data_ = nullptr;
        mask_ = 0;
        attachedGeneration_ = 0;
        anotherConsumerWasActive_ = false;
    }

private:
    DirectPipeHeader* header_ = nullptr;
    float* data_ = nullptr;
    uint32_t mask_ = 0;  // capacity - 1 for power-of-2 modulo
    bool anotherConsumerWasActive_ = false;  // Legacy warning after stale-fill heuristic.
    uint64_t attachedGeneration_ = 0;
    std::atomic<bool> detached_{false};  // Early-out flag; not a substitute for caller-side draining.
};

} // namespace directpipe
