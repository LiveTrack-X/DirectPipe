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
 * @brief SPSC (Single Producer Single Consumer) lock-free ring buffer
 *
 * Designed to be placed directly in shared memory. Uses atomic operations
 * with acquire/release semantics for thread-safe communication between
 * the DirectPipe host (producer) and OBS plugin (consumer).
 */
#pragma once

#include "Protocol.h"
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
     * This is called by the producer (host) to set up the shared memory layout.
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
     * Called by the consumer (OBS plugin) to connect to an already-initialized buffer.
     *
     * @param memory Pointer to the shared memory region.
     * @return true if the buffer is valid and version matches.
     */
    bool attachAsConsumer(void* memory);

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

    /**
     * @brief Check if the buffer has been initialized.
     */
    bool isValid() const { return header_ != nullptr && data_ != nullptr; }

private:
    DirectPipeHeader* header_ = nullptr;
    float* data_ = nullptr;
    uint32_t mask_ = 0;  // capacity - 1 for power-of-2 modulo
};

} // namespace directpipe
