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
 * Constants shared by the host and Receiver audio plugin (including use in OBS).
 * Legacy endpoint names live here; the independent transport is in FanOut.h.
 */
#pragma once

#include <cstdint>

namespace directpipe {

// ─── Shared Memory Names ────────────────────────────────────────
/// Legacy v1 shared memory name: one shared read cursor, one supported Receiver.
constexpr const char* SHM_NAME = "Local\\DirectPipeAudio";

/// Legacy v1 data-ready event. FanOut uses no named event.
constexpr const char* EVENT_NAME = "Local\\DirectPipeDataReady";

// ─── Default Audio Parameters ───────────────────────────────────
/// Capacity of the legacy queue and of each FanOut queue (power of 2).
/// 16384 frames = ~341ms @48kHz of storage, not the selected Receiver latency.
constexpr uint32_t DEFAULT_BUFFER_FRAMES = 16384;

/// Default audio sample rate in Hz
constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;

/// Default number of audio channels
constexpr uint32_t DEFAULT_CHANNELS = 2;

/// Default audio buffer size in samples (WASAPI on Windows)
constexpr uint32_t DEFAULT_AUDIO_BUFFER_SIZE = 128;

// ─── Timing Constants ───────────────────────────────────────────
/// Legacy event-wait default; the current Receiver callback does not wait on it.
constexpr uint32_t EVENT_TIMEOUT_MS = 500;

/// Legacy reconnect default; the current Receiver worker has its own ~100ms loop.
constexpr uint32_t RECONNECT_INTERVAL_MS = 1000;

// ─── Validation Helpers ─────────────────────────────────────────
/// Check if a value is a power of 2
constexpr bool isPowerOfTwo(uint32_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

static_assert(isPowerOfTwo(DEFAULT_BUFFER_FRAMES),
              "DEFAULT_BUFFER_FRAMES must be a power of 2");

} // namespace directpipe
