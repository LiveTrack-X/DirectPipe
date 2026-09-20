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
 * @file SharedMemWriter.h
 * @brief Producer-side shared memory writer for legacy and independent Receivers
 *
 * Interleaves processed PCM once, then feeds the legacy ring buffer and the
 * eight independent Receiver queues. Both carry the same processed stream;
 * only the legacy endpoint uses an event signal and supports just one reader.
 */
#pragma once

#include <JuceHeader.h>
#include "directpipe/RingBuffer.h"
#include "directpipe/SharedMemory.h"
#include "directpipe/Constants.h"
#include "directpipe/FanOut.h"

#include <atomic>

namespace directpipe {

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
class SharedMemWriterTestAccess;
#endif

/**
 * @brief Writes the same processed stream to independent and legacy Receivers.
 *
 * Creates and manages both shared mappings and the legacy named event.
 * writeAudio is single-audio-thread only. initialize/shutdown run on a control
 * thread; shutdown closes admission and drains entered writes before unmapping.
 */
class SharedMemWriter {
public:
    SharedMemWriter();
    ~SharedMemWriter();

    /**
     * @brief Initialize both transports off the audio thread.
     * A failed legacy mapping/event does not disable FanOut, or vice versa.
     * This producer-side independence does not authorize a Receiver to bypass
     * an available full or incompatible FanOut mapping by using legacy v1.
     * @param sampleRate Audio sample rate.
     * @param channels Number of channels.
     * @param bufferFrames Capacity of the legacy queue and each independent queue.
     * @return true if at least one transport initialized successfully.
     */
    [[nodiscard]] bool initialize(uint32_t sampleRate,
                                  uint32_t channels,
                                  uint32_t bufferFrames);

    /**
     * @brief Close write admission, drain admitted callbacks, then retire both mappings.
     * Control thread only; waiting here must never occur inside writeAudio.
     */
    void shutdown();

    /**
     * @brief Interleave once and write the same audio to both initialized transports.
     *
     * Called from the real-time audio thread. No allocation or mutex acquisition.
     * FanOut uses bounded atomics/copies only; the retained legacy event signal
     * is an OS call. Test-only builds can inject a blocking write barrier.
     *
     * @param buffer JUCE audio buffer with processed audio.
     * @param numSamples Number of samples to write.
     */
    void writeAudio(const juce::AudioBuffer<float>& buffer, int numSamples);  // [RT thread only — no alloc, no lock]

    /**
     * @brief Whether at least one transport is ready to accept writes.
     * This does not indicate that a Receiver is currently attached.
     */
    bool isConnected() const { return connected_.load(std::memory_order_acquire); }

    /**
     * @brief Cumulative delivery frames dropped across attached Receiver sinks.
     *
     * Drops are summed per claimed destination, so one frame missed by two
     * Receivers counts twice. An unattached legacy queue does not contribute.
     * FanOut slots remain claimed while idle or until dead-owner reclaim;
     * this is not a live-callback count, device XRun, or single-stream statistic.
     */
    uint64_t getDroppedFrames() const { return droppedFrames_.load(std::memory_order_relaxed); }

private:
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    friend class SharedMemWriterTestAccess;
    using TestWriteBarrier = void (*)(void* context);
    TestWriteBarrier testWriteBarrier_ = nullptr;
    void* testWriteBarrierContext_ = nullptr;
    std::string testLegacyName_ = SHM_NAME;
    std::string testEventName_ = EVENT_NAME;
    std::string testFanOutName_;
    bool testTransportNamesConfigured_ = false;
#endif

    // Control-side only: admission must be closed and admitted writes drained.
    void shutdownLegacy();

    SharedMemory sharedMemory_;
    NamedEvent dataEvent_;
    RingBuffer ringBuffer_;
    SharedMemory fanOutMemory_;
    FanOutProducer fanOutProducer_;

    // Pre-allocated once per initialize: one JUCE planar-to-interleaved conversion
    // is shared by the legacy queue and every claimed independent queue.
    std::vector<float> interleaveBuffer_;

    std::atomic<bool> connected_{false};
    std::atomic<uint32_t> inFlightWriters_{0};
    std::atomic<uint64_t> droppedFrames_{0};

    uint32_t channels_ = DEFAULT_CHANNELS;
};

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
class SharedMemWriterTestAccess {
public:
    using WriteBarrier = void (*)(void* context);

    static void setWriteBarrier(SharedMemWriter& writer,
                                WriteBarrier barrier,
                                void* context);

    // Configure only while disconnected. Empty fanOutName exercises the exact
    // legacy-only lifecycle without touching either production endpoint.
    static void configureTransportNames(SharedMemWriter& writer,
                                        const std::string& legacyName,
                                        const std::string& eventName,
                                        const std::string& fanOutName);
};
#endif

} // namespace directpipe
