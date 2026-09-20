// SPDX-License-Identifier: GPL-3.0-or-later
#include "directpipe/FanOut.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace directpipe {
namespace {
constexpr uint32_t kMaximumCapacity = 1u << 20;

bool alignedMemory(const void* memory) noexcept
{
    return memory && (reinterpret_cast<uintptr_t>(memory) % alignof(FanOutHeader)) == 0;
}

// Match the legacy transport's device-rate contract; rate does not size memory.
bool validRate(uint32_t rate) noexcept { return rate != 0; }

FanOutAttachResult inspect(void* memory, size_t bytes, bool requireActive) noexcept
{
    if (!alignedMemory(memory) || bytes < sizeof(FanOutHeader))
        return FanOutAttachResult::Invalid;
    const auto* header = static_cast<const FanOutHeader*>(memory);
    // Fresh mappings start zeroed. No immutable fields may be inspected before
    // the producer's release publication. Retirement never changes geometry.
    const uint64_t active = header->producerActive.load(std::memory_order_acquire);
    if (active == 0 && requireActive) return FanOutAttachResult::Inactive;
    if (active > 1) return FanOutAttachResult::Invalid;
    const size_t expected = calculateFanOutMemorySize(header->capacity, header->channels);
    if (header->magic != FANOUT_MAGIC || header->version != FANOUT_VERSION
        || header->headerBytes != sizeof(FanOutHeader)
        || header->slotCount != FANOUT_MAX_READERS || !validRate(header->sampleRate)
        || header->generation == 0 || expected == 0
        || header->mappingBytes != expected || expected > bytes)
        return FanOutAttachResult::Invalid;
    return active == 1 ? FanOutAttachResult::Ready : FanOutAttachResult::Inactive;
}

uint32_t currentPid() noexcept
{
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

bool provenDead(uint32_t pid) noexcept
{
    // Non-RT claim path only. A stopped callback or failed permission probe is
    // never evidence of death; retaining an uncertain slot is intentionally safe.
    if (pid == 0 || pid == currentPid()) return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() == ERROR_INVALID_PARAMETER;
    const bool dead = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    CloseHandle(process);
    return dead;
#else
    if (pid > static_cast<uint32_t>((std::numeric_limits<pid_t>::max)())) return false;
    return kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
}

uint64_t freshGeneration() noexcept
{
    // Seed once, then increment: XORing each clock sample with a counter can
    // repeat when their deltas cancel. Old mappings are never reinitialized.
    static std::atomic<uint64_t> next{[] {
        const auto wall = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        const auto steady = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const uint64_t seed = wall ^ (steady + 0x9e3779b97f4a7c15ULL + (wall << 6) + (wall >> 2))
            ^ (static_cast<uint64_t>(currentPid()) << 32);
        return seed == 0 ? 1 : seed;
    }()};
    auto value = next.fetch_add(1, std::memory_order_relaxed);
    if (value == 0) value = next.fetch_add(1, std::memory_order_relaxed);
    return value;
}

uint32_t readable(const FanOutSlot& slot, uint32_t capacity) noexcept
{
    const uint64_t read = slot.readPosition.load(std::memory_order_relaxed);
    const uint64_t write = slot.writePosition.load(std::memory_order_acquire);
    const uint64_t available = write - read;
    return available <= capacity ? static_cast<uint32_t>(available) : 0;
}

} // namespace

size_t calculateFanOutMemorySize(uint32_t capacity, uint32_t channels) noexcept
{
    if (capacity < 2 || capacity > kMaximumCapacity || (capacity & (capacity - 1)) != 0
        || channels == 0 || channels > 2)
        return 0;
    const uint64_t bytes = sizeof(FanOutHeader)
        + static_cast<uint64_t>(FANOUT_MAX_READERS) * capacity * channels * sizeof(float);
    if (bytes > (std::numeric_limits<size_t>::max)()) return 0;
    return static_cast<size_t>(bytes);
}

bool FanOutProducer::initialize(void* memory, size_t mappedBytes, uint32_t sampleRate,
                                 uint32_t channels, uint32_t capacity) noexcept
{
    const size_t bytes = calculateFanOutMemorySize(capacity, channels);
    if (header_ || !alignedMemory(memory) || bytes == 0 || mappedBytes < bytes || !validRate(sampleRate))
        return false;
    auto* header = new (memory) FanOutHeader{};
    header->magic = FANOUT_MAGIC;
    header->version = FANOUT_VERSION;
    header->headerBytes = sizeof(FanOutHeader);
    header->slotCount = FANOUT_MAX_READERS;
    header->capacity = capacity;
    header->channels = channels;
    header->sampleRate = sampleRate;
    header->generation = freshGeneration();
    header->mappingBytes = bytes;
    header_ = header;
    capacity_ = capacity;
    channels_ = channels;
    audio_ = reinterpret_cast<float*>(static_cast<uint8_t*>(memory) + sizeof(FanOutHeader));
    // Touch every PCM page off the audio thread, before publication, so a new
    // producer's first write to a reader does not incur initial page faults.
    // This pre-touch does not pin pages against later OS paging.
    std::memset(audio_, 0, bytes - sizeof(FanOutHeader));
    header_->producerActive.store(1, std::memory_order_release);
    return true;
}

FanOutWriteResult FanOutProducer::write(const float* interleaved, uint32_t frames) noexcept
{
    FanOutWriteResult result;
    if (!isActive() || (frames != 0 && interleaved == nullptr)) return result;
    for (uint32_t i = 0; i < FANOUT_MAX_READERS; ++i) {
        auto& slot = header_->slots[i];
        const uint64_t owner = slot.ownerToken.load(std::memory_order_acquire);
        if (owner == 0) continue;
        ++result.activeReaders;
        if (slot.acknowledgedToken.load(std::memory_order_relaxed) != owner) {
            // This producer is the only writer. Any earlier write to this slot
            // has completed. The previous reader drained before releasing its
            // token (or its process is dead); the new reader waits for this ack.
            slot.readPosition.store(0, std::memory_order_relaxed);
            slot.writePosition.store(0, std::memory_order_relaxed);
            slot.droppedFrames.store(0, std::memory_order_relaxed);
            slot.overflowEpoch.store(0, std::memory_order_relaxed);
            slot.acknowledgedToken.store(owner, std::memory_order_release);
        }
        const uint64_t write = slot.writePosition.load(std::memory_order_relaxed);
        const uint64_t read = slot.readPosition.load(std::memory_order_acquire);
        const uint64_t used = write - read;
        const uint32_t space = used <= capacity_ ? capacity_ - static_cast<uint32_t>(used) : 0;
        const uint32_t count = (std::min)(frames, space);
        if (count != 0) {
            float* queue = audio_ + static_cast<size_t>(i) * capacity_ * channels_;
            const auto offset = static_cast<uint32_t>(write & (capacity_ - 1));
            const uint32_t first = (std::min)(count, capacity_ - offset);
            std::memcpy(queue + static_cast<size_t>(offset) * channels_, interleaved,
                        static_cast<size_t>(first) * channels_ * sizeof(float));
            if (count > first)
                std::memcpy(queue, interleaved + static_cast<size_t>(first) * channels_,
                            static_cast<size_t>(count - first) * channels_ * sizeof(float));
            slot.writePosition.store(write + count, std::memory_order_release);
            result.writtenFrames = (std::max)(result.writtenFrames, count);
        }
        const uint32_t dropped = frames - count;
        if (dropped != 0) {
            // Publish loss after any accepted frames. The consumer observes the
            // epoch and chooses its own freshness recovery; no read cursor theft.
            slot.droppedFrames.fetch_add(dropped, std::memory_order_relaxed);
            slot.overflowEpoch.fetch_add(1, std::memory_order_release);
            result.droppedFrames += dropped;
        }
    }
    return result;
}

void FanOutProducer::shutdown() noexcept
{
    if (header_) header_->producerActive.store(0, std::memory_order_release);
    header_ = nullptr;
    audio_ = nullptr;
    capacity_ = channels_ = 0;
}

bool FanOutProducer::isActive() const noexcept
{
    return header_ && header_->producerActive.load(std::memory_order_acquire) == 1;
}

uint64_t FanOutProducer::getGeneration() const noexcept { return header_ ? header_->generation : 0; }

bool FanOutProducer::invalidateMapping(void* memory, size_t mappedBytes) noexcept
{
    const auto state = inspect(memory, mappedBytes, true);
    if (state == FanOutAttachResult::Inactive) return true;
    if (state != FanOutAttachResult::Ready) return false;
    static_cast<FanOutHeader*>(memory)->producerActive.store(0, std::memory_order_release);
    return true;
}

FanOutAttachResult FanOutConsumer::claim(void* memory, size_t mappedBytes) noexcept
{
    if (slot_) {
        if (header_ != memory) return FanOutAttachResult::Invalid;
        return isReady() ? FanOutAttachResult::Ready
                         : (isProducerActive() ? FanOutAttachResult::Waiting : FanOutAttachResult::Inactive);
    }
    const auto state = inspect(memory, mappedBytes, true);
    if (state != FanOutAttachResult::Ready) return state;
    auto* header = static_cast<FanOutHeader*>(memory);
    uint64_t serial = header->nextClaimId.load(std::memory_order_relaxed);
    for (;;) {
        if (serial == 0 || serial > (std::numeric_limits<uint32_t>::max)())
            return FanOutAttachResult::Invalid; // No token reuse, including across DLLs.
        if (header->nextClaimId.compare_exchange_weak(serial, serial + 1, std::memory_order_relaxed)) break;
    }
    const uint64_t token = (static_cast<uint64_t>(currentPid()) << 32) | serial;
    for (unsigned pass = 0; pass != 2; ++pass) {
        for (uint32_t i = 0; i < FANOUT_MAX_READERS; ++i) {
            auto& slot = header->slots[i];
            uint64_t owner = slot.ownerToken.load(std::memory_order_acquire);
            if (owner != 0) {
                if (pass == 0 || !provenDead(static_cast<uint32_t>(owner >> 32))) continue;
                // Only release the exact token whose process was checked.
                if (!slot.ownerToken.compare_exchange_strong(owner, 0, std::memory_order_acq_rel)) continue;
            }
            uint64_t empty = 0;
            if (!slot.ownerToken.compare_exchange_strong(empty, token, std::memory_order_acq_rel)) continue;
            header_ = header;
            slot_ = &slot;
            token_ = token;
            generation_ = header->generation;
            sampleRate_ = header->sampleRate;
            channels_ = header->channels;
            capacity_ = header->capacity;
            audio_ = reinterpret_cast<float*>(static_cast<uint8_t*>(memory) + sizeof(FanOutHeader))
                + static_cast<size_t>(i) * capacity_ * channels_;
            if (!isProducerActive()) {
                detach();
                return FanOutAttachResult::Inactive;
            }
            return isReady() ? FanOutAttachResult::Ready : FanOutAttachResult::Waiting;
        }
    }
    return FanOutAttachResult::Full;
}

bool FanOutConsumer::isAvailable(void* memory, size_t mappedBytes) noexcept
{
    return inspect(memory, mappedBytes, true) == FanOutAttachResult::Ready;
}

void FanOutConsumer::detach() noexcept
{
    if (slot_) {
        uint64_t expected = token_;
        slot_->ownerToken.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    }
    header_ = nullptr;
    slot_ = nullptr;
    audio_ = nullptr;
    token_ = generation_ = 0;
    sampleRate_ = channels_ = capacity_ = 0;
}

bool FanOutConsumer::isReady() const noexcept
{
    return slot_ && isProducerActive()
        && slot_->ownerToken.load(std::memory_order_acquire) == token_
        && slot_->acknowledgedToken.load(std::memory_order_acquire) == token_;
}

bool FanOutConsumer::isProducerActive() const noexcept
{
    return header_ && header_->producerActive.load(std::memory_order_acquire) == 1
        && header_->generation == generation_;
}

uint64_t FanOutConsumer::getCurrentProducerGeneration() const noexcept
{
    return header_ ? header_->generation : 0;
}

uint32_t FanOutConsumer::availableRead() const noexcept
{
    return isReady() ? readable(*slot_, capacity_) : 0;
}

uint32_t FanOutConsumer::read(float* interleaved, uint32_t frames) noexcept
{
    if (interleaved == nullptr || frames == 0 || !isReady()) return 0;
    const uint32_t count = (std::min)(frames, readable(*slot_, capacity_));
    if (count == 0) return 0;
    const uint64_t read = slot_->readPosition.load(std::memory_order_relaxed);
    const auto offset = static_cast<uint32_t>(read & (capacity_ - 1));
    const uint32_t first = (std::min)(count, capacity_ - offset);
    std::memcpy(interleaved, audio_ + static_cast<size_t>(offset) * channels_,
                static_cast<size_t>(first) * channels_ * sizeof(float));
    if (count > first)
        std::memcpy(interleaved + static_cast<size_t>(first) * channels_, audio_,
                    static_cast<size_t>(count - first) * channels_ * sizeof(float));
    slot_->readPosition.store(read + count, std::memory_order_release);
    return count;
}

uint32_t FanOutConsumer::discard(uint32_t frames) noexcept
{
    if (frames == 0 || !isReady()) return 0;
    const uint32_t count = (std::min)(frames, readable(*slot_, capacity_));
    const uint64_t read = slot_->readPosition.load(std::memory_order_relaxed);
    slot_->readPosition.store(read + count, std::memory_order_release);
    return count;
}

uint64_t FanOutConsumer::getDroppedFrames() const noexcept
{
    return isReady() ? slot_->droppedFrames.load(std::memory_order_acquire) : 0;
}

uint64_t FanOutConsumer::getOverflowEpoch() const noexcept
{
    return isReady() ? slot_->overflowEpoch.load(std::memory_order_acquire) : 0;
}
} // namespace directpipe
