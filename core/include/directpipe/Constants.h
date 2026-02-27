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
 * @file Constants.h
 * @brief Shared constants for DirectPipe IPC
 *
 * Constants used by both the host application and OBS plugin
 * for shared memory naming, default audio parameters, etc.
 */
#pragma once

#include <cstdint>

namespace directpipe {

// ─── Shared Memory Names ────────────────────────────────────────
/// Name of the shared memory region (Windows Local namespace)
constexpr const char* SHM_NAME = "Local\\DirectPipeAudio";

/// Name of the Named Event for data-ready signaling
constexpr const char* EVENT_NAME = "Local\\DirectPipeDataReady";

// ─── Default Audio Parameters ───────────────────────────────────
/// Default ring buffer size in frames (must be power of 2)
constexpr uint32_t DEFAULT_BUFFER_FRAMES = 4096;

/// Default audio sample rate in Hz
constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;

/// Default number of audio channels
constexpr uint32_t DEFAULT_CHANNELS = 2;

/// Default WASAPI buffer size in samples
constexpr uint32_t DEFAULT_WASAPI_BUFFER_SIZE = 128;

// ─── Timing Constants ───────────────────────────────────────────
/// Timeout in milliseconds for waiting on the data event
constexpr uint32_t EVENT_TIMEOUT_MS = 500;

/// Reconnection attempt interval in milliseconds
constexpr uint32_t RECONNECT_INTERVAL_MS = 1000;

// ─── Validation Helpers ─────────────────────────────────────────
/// Check if a value is a power of 2
constexpr bool isPowerOfTwo(uint32_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

static_assert(isPowerOfTwo(DEFAULT_BUFFER_FRAMES),
              "DEFAULT_BUFFER_FRAMES must be a power of 2");

} // namespace directpipe
