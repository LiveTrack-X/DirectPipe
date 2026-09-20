// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace directpipe {

// Additive transport: the legacy DirectPipeAudio mapping and v1 ABI are unchanged.
// FANOUT_VERSION is this separate layout's version, not a legacy protocol bump.
// Layout: immutable geometry + eight slot records, followed by eight PCM queues.
inline constexpr const char* FANOUT_SHM_NAME = "Local\\DirectPipeAudioFanOutV1";
inline constexpr uint32_t FANOUT_MAX_READERS = 8;
inline constexpr uint32_t FANOUT_VERSION = 1;
inline constexpr uint32_t FANOUT_MAGIC = 0x46415044; // DPAF

struct alignas(64) FanOutSlot {
    // PID in high 32 bits, mapping-wide claim serial in low 32 bits; zero is free.
    std::atomic<uint64_t> ownerToken{0};
    uint8_t ownerPadding[56]{};
    // Only the single producer writes these fields. A new owner cannot read PCM
    // until acknowledgedToken matches its unique token.
    alignas(64) std::atomic<uint64_t> acknowledgedToken{0};
    std::atomic<uint64_t> writePosition{0};
    // Reset for a new acknowledged owner. A full queue drops only its incoming
    // frames and increments the epoch; the producer never skips unread PCM.
    std::atomic<uint64_t> droppedFrames{0};
    std::atomic<uint64_t> overflowEpoch{0};
    uint8_t writerPadding[32]{};
    // Consumer-owned after ack. Only the producer resets it before a new ack,
    // after the previous owner drained its callbacks or its process is dead.
    alignas(64) std::atomic<uint64_t> readPosition{0};
    uint8_t readerPadding[56]{};
};

struct alignas(64) FanOutHeader {
    // Published last on fresh initialization. Geometry/generation never change.
    std::atomic<uint64_t> producerActive{0};
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t headerBytes = 0;
    uint32_t slotCount = 0;
    uint32_t capacity = 0;
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t reserved = 0;
    uint64_t generation = 0;
    uint64_t mappingBytes = 0;
    uint8_t headerPadding[8]{};
    // Shared across DLLs/processes; claims fail beyond UINT32_MAX, never wrap/reuse.
    alignas(64) std::atomic<uint64_t> nextClaimId{1};
    uint8_t claimPadding[56]{};
    FanOutSlot slots[FANOUT_MAX_READERS];
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "FanOut requires lock-free 64-bit atomics");
static_assert(sizeof(FanOutSlot) == 192, "FanOut slot ABI changed");
static_assert(sizeof(FanOutHeader) == 1664, "FanOut header ABI changed");

// Returns zero for unsupported geometry or size overflow. Capacity is a power
// of two, 2..2^20 frames; channels 1..2. Sample rate is validated separately by
// initialize/claim: any nonzero device rate is accepted; no resampling occurs.
size_t calculateFanOutMemorySize(uint32_t capacity, uint32_t channels) noexcept;

// Waiting owns a slot but cannot read until ack; Full owns none. Inactive means
// no published producer. Invalid includes layout/size errors or serial exhaustion.
enum class FanOutAttachResult { Ready, Waiting, Full, Inactive, Invalid };

struct FanOutWriteResult {
    uint32_t activeReaders = 0; // Claimed slots observed, not a callback/liveness count.
    uint32_t writtenFrames = 0; // Maximum frames delivered to any one reader.
    uint64_t droppedFrames = 0; // Sum across claimed readers for this call.
};

class FanOutProducer {
public:
    FanOutProducer() = default;
    FanOutProducer(const FanOutProducer&) = delete;
    FanOutProducer& operator=(const FanOutProducer&) = delete;
    // Non-RT. Caller MUST supply a genuinely fresh, 64-byte aligned mapping.
    // Never initialize a retained mapping, even when its producer is inactive.
    // Initialization also touches all PCM pages before publishing producerActive.
    bool initialize(void* memory, size_t mappedBytes, uint32_t sampleRate,
                    uint32_t channels, uint32_t capacity) noexcept;
    // Single producer only; bounded copies/atomics, no allocations/OS calls.
    // New claims are acknowledged and can receive this same write's frames.
    // A zero-frame write still acknowledges claims; an absent producer cannot.
    FanOutWriteResult write(const float* interleaved, uint32_t frames) noexcept;
    // Caller drains all admitted writes before shutdown/unmapping.
    void shutdown() noexcept;
    bool isActive() const noexcept;
    uint64_t getGeneration() const noexcept;
    // Non-RT retirement: validates an active layout before clearing its flag;
    // already-inactive mappings need no mutation. Does not reset or unmap it.
    static bool invalidateMapping(void* memory, size_t mappedBytes) noexcept;

private:
    FanOutHeader* header_ = nullptr;
    float* audio_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t channels_ = 0;
};

class FanOutConsumer {
public:
    FanOutConsumer() = default;
    ~FanOutConsumer() { detach(); }
    FanOutConsumer(const FanOutConsumer&) = delete;
    FanOutConsumer& operator=(const FanOutConsumer&) = delete;

    // Non-RT. Claims a slot, reclaiming only owners whose process is proven
    // dead. Live idle owners, access-denied probes, and reused live PIDs are not
    // reclaimed. Retain this object AND its mapping while Waiting; the next
    // producer write acks it. No time-based eviction or timeout proof of death.
    FanOutAttachResult claim(void* memory, size_t mappedBytes) noexcept;
    // Non-claiming active-layout probe; true does not promise a free slot.
    static bool isAvailable(void* memory, size_t mappedBytes) noexcept;
    // Caller MUST drain this consumer's local callbacks before detach/unmap.
    void detach() noexcept;
    bool isReady() const noexcept;
    bool isProducerActive() const noexcept;
    uint64_t getCurrentProducerGeneration() const noexcept;
    uint64_t getAttachedProducerGeneration() const noexcept { return generation_; }
    uint32_t getSampleRate() const noexcept { return sampleRate_; }
    uint32_t getChannels() const noexcept { return channels_; }
    uint32_t getCapacity() const noexcept { return capacity_; }
    // One reader thread per consumer. Before ack or after retirement, queue
    // access returns zero. Callers must retain the mapping throughout every call.
    uint32_t availableRead() const noexcept;
    uint32_t read(float* interleaved, uint32_t frames) noexcept;
    uint32_t discard(uint32_t frames) noexcept;
    uint64_t getDroppedFrames() const noexcept;
    // Receiver compares epochs and discards its own stale backlog before fading
    // back in. The core reports loss; it does not flush or fade audio itself.
    uint64_t getOverflowEpoch() const noexcept;

private:
    FanOutHeader* header_ = nullptr;
    FanOutSlot* slot_ = nullptr;
    float* audio_ = nullptr;
    uint64_t token_ = 0;
    uint64_t generation_ = 0;
    uint32_t sampleRate_ = 0;
    uint32_t channels_ = 0;
    uint32_t capacity_ = 0;
};

} // namespace directpipe
