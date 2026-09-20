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

bool createFreshSharedMemory(SharedMemory& sharedMemory, const std::string& name,
                             size_t size, bool fanOut = false)
{
    if (!sharedMemory.create(name, size))
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
        if (fanOut) {
            // Retire only a validated transport. Never placement-initialize
            // retained state or touch an incompatible layout.
            FanOutProducer::invalidateMapping(sharedMemory.getData(), sharedMemory.getSize());
        } else {
            auto* header = static_cast<DirectPipeHeader*>(sharedMemory.getData());
            if (header != nullptr && sharedMemory.getSize() >= sizeof(DirectPipeHeader))
                header->producer_active.store(false, std::memory_order_release);
        }

        sharedMemory.close();
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        std::this_thread::sleep_for(retryInterval);
        if (!sharedMemory.create(name, size))
            return false;
        if (!sharedMemory.createOpenedExistingObject())
            return true;
    }
#else
    (void)fanOut;
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

void SharedMemWriterTestAccess::configureTransportNames(SharedMemWriter& writer,
                                                        const std::string& legacyName,
                                                        const std::string& eventName,
                                                        const std::string& fanOutName)
{
    jassert(!writer.isConnected());
    writer.testLegacyName_ = legacyName;
    writer.testEventName_ = eventName;
    writer.testFanOutName_ = fanOutName;
    writer.testTransportNamesConfigured_ = true;
}
#endif

bool SharedMemWriter::initialize(uint32_t sampleRate, uint32_t channels, uint32_t bufferFrames)
{
    const bool wasConnected = connected_.load(std::memory_order_relaxed);
    shutdown();  // Drain prior writes and publish inactive on both old transports.

    // Brief control-thread pause lets Receivers notice inactive producers and
    // release old mappings. This pause alone never
    // grants permission to reinitialize one: fresh creation is required below.
    if (wasConnected)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    const auto fanOutSize = calculateFanOutMemorySize(bufferFrames, channels);
    if (sampleRate == 0 || channels == 0 || channels > 2 || fanOutSize == 0) {
        juce::Logger::writeToLog("[IPC] SharedMemWriter: Invalid stream geometry");
        return false;
    }
    channels_ = channels;
    // Allocate before publishing either endpoint. The callback interleaves once.
    interleaveBuffer_.resize(static_cast<size_t>(bufferFrames) * channels, 0.0f);

    const size_t shmSize = calculateSharedMemorySize(bufferFrames, channels);

    // Create a genuinely fresh region. On Windows, createFreshSharedMemory()
    // first retires any crash-retained mapping and fails closed if a Receiver
    // does not release it within the bounded handoff window.
    const std::string legacyName =
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
        testLegacyName_;
#else
        SHM_NAME;
#endif
    const std::string eventName =
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
        testEventName_;
#else
        EVENT_NAME;
#endif
    const std::string fanOutName =
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
        testTransportNamesConfigured_ ? testFanOutName_ : FANOUT_SHM_NAME;
#else
        FANOUT_SHM_NAME;
#endif

    if (!fanOutName.empty()) {
        if (!createFreshSharedMemory(fanOutMemory_, fanOutName, fanOutSize, true)
            || !fanOutProducer_.initialize(fanOutMemory_.getData(), fanOutMemory_.getSize(),
                                           sampleRate, channels, bufferFrames)) {
            fanOutProducer_.shutdown();
            fanOutMemory_.close();
            juce::Logger::writeToLog(
                "[IPC] SharedMemWriter: Independent Receiver transport unavailable; "
                "legacy output will be attempted. Receivers must not bypass a retained "
                "or incompatible independent transport.");
        }
    }

    if (!createFreshSharedMemory(sharedMemory_, legacyName, shmSize)) {
        juce::Logger::writeToLog(
            "[IPC] SharedMemWriter: Legacy Receiver mapping unavailable; "
            "independent Receiver transport is unaffected");
    } else {
        ringBuffer_.initAsProducer(sharedMemory_.getData(), bufferFrames, channels, sampleRate);
        if (!dataEvent_.create(eventName)) {
            juce::Logger::writeToLog(
                "[IPC] SharedMemWriter: Legacy Receiver event unavailable; "
                "independent Receiver transport is unaffected");
            shutdownLegacy();
        }
    }

    if (!ringBuffer_.isValid() && !fanOutProducer_.isActive()) {
        shutdown();
        return false;
    }

    droppedFrames_.store(0, std::memory_order_relaxed);
    connected_.store(true, std::memory_order_seq_cst);

    juce::Logger::writeToLog("[IPC] SharedMemWriter: Initialized - " +
                             juce::String(sampleRate) + "Hz, " +
                             juce::String(channels) + "ch, " +
                             juce::String(bufferFrames) + " frames buffer; legacy=" +
                             (ringBuffer_.isValid() ? "ready" : "unavailable") +
                             ", independent=" +
                             (fanOutProducer_.isActive() ? "ready" : "unavailable"));

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

    shutdownLegacy();
    fanOutProducer_.shutdown();
    fanOutMemory_.close();
    // Keep the allocation for reuse on the next initialize().
}

void SharedMemWriter::shutdownLegacy()
{
    // Mark the legacy producer inactive BEFORE unmapping (no event signal here).
    // Receivers inspect producer_active to detect clean disconnects.
    if (ringBuffer_.isValid()) {
        auto* data = sharedMemory_.getData();
        if (data) {
            auto* header = static_cast<directpipe::DirectPipeHeader*>(data);
            header->producer_active.store(false, std::memory_order_release);
        }
    }

    // Invalidate ring buffer pointers before unmapping shared memory. All writers
    // that passed admission have drained, or admission has not opened yet.
    ringBuffer_.detach();
    dataEvent_.close();
    sharedMemory_.close();
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
        // Both transports: [L0 R0 L1 R1 L2 R2 ...]
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

    const auto frames = static_cast<uint32_t>(samples);
    uint64_t dropped = 0;
    if (ringBuffer_.isValid()) {
        const auto* header = static_cast<const DirectPipeHeader*>(sharedMemory_.getData());
        const bool legacyListener = header->consumer_active.load(std::memory_order_acquire);
        const auto written = ringBuffer_.write(interleaveBuffer_.data(), frames);
        if (legacyListener)
            dropped += frames - written;

        // Preserve the legacy event contract. This pre-existing signal is the
        // only OS call here; the independent transport needs no named event.
        if (written > 0)
            dataEvent_.signal();
    }

    dropped += fanOutProducer_.write(interleaveBuffer_.data(), frames).droppedFrames;
    if (dropped != 0)
        droppedFrames_.fetch_add(dropped, std::memory_order_relaxed);
}

} // namespace directpipe
