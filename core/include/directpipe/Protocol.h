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
 * @file Protocol.h
 * @brief DirectPipe IPC protocol definitions
 *
 * Defines the shared memory header structure used for communication
 * between the DirectPipe host application and the OBS source plugin.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace directpipe {

/// Protocol version — increment when header layout changes
constexpr uint32_t PROTOCOL_VERSION = 1;

/**
 * @brief Shared memory header placed at the start of the mapped region.
 *
 * Layout:
 *   [Header (64-byte aligned fields)] [Ring buffer PCM data]
 *
 * write_pos and read_pos are on separate cache lines to prevent
 * false sharing between producer and consumer.
 */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif
struct DirectPipeHeader {
    /// Write position in frames (producer increments)
    alignas(64) std::atomic<uint64_t> write_pos{0};

    /// Read position in frames (consumer increments)
    alignas(64) std::atomic<uint64_t> read_pos{0};

    /// Audio sample rate (e.g., 48000)
    uint32_t sample_rate{0};

    /// Number of audio channels (1 = mono, 2 = stereo)
    uint32_t channels{0};

    /// Ring buffer capacity in frames (must be power of 2)
    uint32_t buffer_frames{0};

    /// Protocol version for compatibility checking
    uint32_t version{PROTOCOL_VERSION};

    /// Whether the producer (JUCE host) is actively writing
    std::atomic<bool> producer_active{false};

    /// Reserved padding to ensure header is cache-line aligned
    uint8_t reserved[64 - sizeof(std::atomic<bool>) - 4 * sizeof(uint32_t)]{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Ensure header fields are properly aligned
static_assert(alignof(DirectPipeHeader) >= 64,
              "DirectPipeHeader must be at least 64-byte aligned");

/**
 * @brief Calculate the total shared memory size needed.
 * @param buffer_frames Number of frames in the ring buffer (power of 2).
 * @param channels Number of audio channels.
 * @return Total bytes needed for header + ring buffer data.
 */
constexpr size_t calculateSharedMemorySize(uint32_t buffer_frames, uint32_t channels) {
    return sizeof(DirectPipeHeader) + (static_cast<size_t>(buffer_frames) * channels * sizeof(float));
}

} // namespace directpipe
