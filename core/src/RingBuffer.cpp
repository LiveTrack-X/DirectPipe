/**
 * @file RingBuffer.cpp
 * @brief SPSC lock-free ring buffer implementation
 */

#include "directpipe/RingBuffer.h"
#include "directpipe/Constants.h"
#include <cassert>
#include <algorithm>

namespace directpipe {

void RingBuffer::initAsProducer(void* memory, uint32_t capacity_frames,
                                 uint32_t channels, uint32_t sample_rate)
{
    assert(memory != nullptr);
    assert(isPowerOfTwo(capacity_frames));
    assert(channels > 0 && channels <= 2);
    assert(sample_rate > 0);

    // Place the header at the start of the memory region
    header_ = new (memory) DirectPipeHeader{};
    header_->write_pos.store(0, std::memory_order_relaxed);
    header_->read_pos.store(0, std::memory_order_relaxed);
    header_->sample_rate = sample_rate;
    header_->channels = channels;
    header_->buffer_frames = capacity_frames;
    header_->version = PROTOCOL_VERSION;
    header_->producer_active.store(true, std::memory_order_release);

    // PCM data starts right after the header
    data_ = reinterpret_cast<float*>(static_cast<uint8_t*>(memory) + sizeof(DirectPipeHeader));
    mask_ = capacity_frames - 1;

    // Zero out the audio buffer
    std::memset(data_, 0, static_cast<size_t>(capacity_frames) * channels * sizeof(float));
}

bool RingBuffer::attachAsConsumer(void* memory)
{
    if (!memory) return false;

    header_ = static_cast<DirectPipeHeader*>(memory);

    // Validate protocol version
    if (header_->version != PROTOCOL_VERSION) {
        header_ = nullptr;
        return false;
    }

    // Validate buffer parameters
    if (!isPowerOfTwo(header_->buffer_frames) ||
        header_->channels == 0 || header_->channels > 2 ||
        header_->sample_rate == 0) {
        header_ = nullptr;
        return false;
    }

    data_ = reinterpret_cast<float*>(static_cast<uint8_t*>(memory) + sizeof(DirectPipeHeader));
    mask_ = header_->buffer_frames - 1;

    return true;
}

uint32_t RingBuffer::write(const float* data, uint32_t frames)
{
    if (!isValid() || frames == 0) return 0;

    const uint32_t channels = header_->channels;
    const uint32_t capacity = header_->buffer_frames;
    const uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
    const uint64_t read_pos = header_->read_pos.load(std::memory_order_acquire);

    // Calculate available space
    const uint64_t used = write_pos - read_pos;
    const uint32_t available = capacity - static_cast<uint32_t>(used);
    const uint32_t to_write = std::min(frames, available);

    if (to_write == 0) return 0;

    // Write data in up to two segments (wrap-around)
    const uint32_t write_index = static_cast<uint32_t>(write_pos) & mask_;
    const uint32_t first_chunk = std::min(to_write, capacity - write_index);
    const uint32_t second_chunk = to_write - first_chunk;

    // First segment
    std::memcpy(data_ + static_cast<size_t>(write_index) * channels,
                data,
                static_cast<size_t>(first_chunk) * channels * sizeof(float));

    // Second segment (after wrap-around)
    if (second_chunk > 0) {
        std::memcpy(data_,
                    data + static_cast<size_t>(first_chunk) * channels,
                    static_cast<size_t>(second_chunk) * channels * sizeof(float));
    }

    // Publish the new write position with release semantics
    // so the consumer sees the written data
    header_->write_pos.store(write_pos + to_write, std::memory_order_release);

    return to_write;
}

uint32_t RingBuffer::read(float* data, uint32_t frames)
{
    if (!isValid() || frames == 0) return 0;

    const uint32_t channels = header_->channels;
    const uint32_t capacity = header_->buffer_frames;
    const uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
    const uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);

    // Calculate available data
    const uint64_t available64 = write_pos - read_pos;
    const uint32_t available = static_cast<uint32_t>(std::min(available64, static_cast<uint64_t>(capacity)));
    const uint32_t to_read = std::min(frames, available);

    if (to_read == 0) return 0;

    // Read data in up to two segments (wrap-around)
    const uint32_t read_index = static_cast<uint32_t>(read_pos) & mask_;
    const uint32_t first_chunk = std::min(to_read, capacity - read_index);
    const uint32_t second_chunk = to_read - first_chunk;

    // First segment
    std::memcpy(data,
                data_ + static_cast<size_t>(read_index) * channels,
                static_cast<size_t>(first_chunk) * channels * sizeof(float));

    // Second segment (after wrap-around)
    if (second_chunk > 0) {
        std::memcpy(data + static_cast<size_t>(first_chunk) * channels,
                    data_ + 0,
                    static_cast<size_t>(second_chunk) * channels * sizeof(float));
    }

    // Publish the new read position with release semantics
    header_->read_pos.store(read_pos + to_read, std::memory_order_release);

    return to_read;
}

uint32_t RingBuffer::availableRead() const
{
    if (!isValid()) return 0;

    const uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
    const uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);
    const uint64_t available = write_pos - read_pos;
    return static_cast<uint32_t>(std::min(available, static_cast<uint64_t>(header_->buffer_frames)));
}

uint32_t RingBuffer::availableWrite() const
{
    if (!isValid()) return 0;

    const uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
    const uint64_t read_pos = header_->read_pos.load(std::memory_order_acquire);
    const uint64_t used = write_pos - read_pos;
    return header_->buffer_frames - static_cast<uint32_t>(used);
}

void RingBuffer::reset()
{
    if (!isValid()) return;
    header_->write_pos.store(0, std::memory_order_relaxed);
    header_->read_pos.store(0, std::memory_order_relaxed);
}

uint32_t RingBuffer::getChannels() const
{
    return isValid() ? header_->channels : 0;
}

uint32_t RingBuffer::getSampleRate() const
{
    return isValid() ? header_->sample_rate : 0;
}

uint32_t RingBuffer::getCapacity() const
{
    return isValid() ? header_->buffer_frames : 0;
}

} // namespace directpipe
