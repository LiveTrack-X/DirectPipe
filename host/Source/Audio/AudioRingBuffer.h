/**
 * @file AudioRingBuffer.h
 * @brief SPSC lock-free ring buffer for inter-thread audio transfer.
 *
 * Producer (main audio callback) writes non-interleaved float frames.
 * Consumer (virtual cable callback) reads them.
 * Used to bridge two independent WASAPI callback threads.
 */
#pragma once

#include <atomic>
#include <cstring>
#include <vector>

namespace directpipe {

class AudioRingBuffer {
public:
    AudioRingBuffer() = default;

    /**
     * Initialize with given capacity and channel count.
     * Must NOT be called from a real-time thread.
     * @param capacityFrames Must be power of 2.
     * @param numChannels 1 or 2.
     */
    void initialize(uint32_t capacityFrames, int numChannels)
    {
        capacity_ = capacityFrames;
        mask_ = capacityFrames - 1;
        channels_ = numChannels;

        data_.resize(static_cast<size_t>(numChannels));
        for (auto& ch : data_)
            ch.resize(capacityFrames, 0.0f);

        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }

    /**
     * Write frames. RT-safe (no allocation).
     * @return Number of frames actually written (< numFrames on overflow).
     */
    int write(const float* const* channelData, int numChannels, int numFrames)
    {
        const uint64_t wp = writePos_.load(std::memory_order_relaxed);
        const uint64_t rp = readPos_.load(std::memory_order_acquire);
        const int available = static_cast<int>(capacity_ - (wp - rp));
        const int toWrite = (numFrames < available) ? numFrames : available;

        if (toWrite <= 0) return 0;

        const int chCount = (numChannels < channels_) ? numChannels : channels_;
        const uint32_t startIdx = static_cast<uint32_t>(wp & mask_);

        // Check if write wraps around
        const uint32_t firstPart = capacity_ - startIdx;

        if (static_cast<uint32_t>(toWrite) <= firstPart) {
            // No wrap
            for (int ch = 0; ch < chCount; ++ch)
                std::memcpy(&data_[ch][startIdx], channelData[ch],
                            static_cast<size_t>(toWrite) * sizeof(float));
        } else {
            // Wrap around
            const int second = toWrite - static_cast<int>(firstPart);
            for (int ch = 0; ch < chCount; ++ch) {
                std::memcpy(&data_[ch][startIdx], channelData[ch],
                            firstPart * sizeof(float));
                std::memcpy(&data_[ch][0], channelData[ch] + firstPart,
                            static_cast<size_t>(second) * sizeof(float));
            }
        }

        // Fill extra channels with first channel (mono → stereo expansion)
        for (int ch = chCount; ch < channels_; ++ch) {
            if (static_cast<uint32_t>(toWrite) <= firstPart) {
                std::memcpy(&data_[ch][startIdx], &data_[0][startIdx],
                            static_cast<size_t>(toWrite) * sizeof(float));
            } else {
                const int second = toWrite - static_cast<int>(firstPart);
                std::memcpy(&data_[ch][startIdx], &data_[0][startIdx],
                            firstPart * sizeof(float));
                std::memcpy(&data_[ch][0], &data_[0][0],
                            static_cast<size_t>(second) * sizeof(float));
            }
        }

        writePos_.store(wp + static_cast<uint64_t>(toWrite), std::memory_order_release);
        return toWrite;
    }

    /**
     * Read frames. RT-safe (no allocation).
     * @return Number of frames actually read (< numFrames on underrun).
     */
    int read(float* const* channelData, int numChannels, int numFrames)
    {
        const uint64_t rp = readPos_.load(std::memory_order_relaxed);
        const uint64_t wp = writePos_.load(std::memory_order_acquire);
        const int available = static_cast<int>(wp - rp);
        const int toRead = (numFrames < available) ? numFrames : available;

        if (toRead <= 0) return 0;

        const int chCount = (numChannels < channels_) ? numChannels : channels_;
        const uint32_t startIdx = static_cast<uint32_t>(rp & mask_);
        const uint32_t firstPart = capacity_ - startIdx;

        if (static_cast<uint32_t>(toRead) <= firstPart) {
            for (int ch = 0; ch < chCount; ++ch)
                std::memcpy(channelData[ch], &data_[ch][startIdx],
                            static_cast<size_t>(toRead) * sizeof(float));
        } else {
            const int second = toRead - static_cast<int>(firstPart);
            for (int ch = 0; ch < chCount; ++ch) {
                std::memcpy(channelData[ch], &data_[ch][startIdx],
                            firstPart * sizeof(float));
                std::memcpy(channelData[ch] + firstPart, &data_[ch][0],
                            static_cast<size_t>(second) * sizeof(float));
            }
        }

        // Fill extra output channels (mono ring → stereo output)
        for (int ch = chCount; ch < numChannels; ++ch) {
            if (chCount > 0)
                std::memcpy(channelData[ch], channelData[0],
                            static_cast<size_t>(toRead) * sizeof(float));
        }

        readPos_.store(rp + static_cast<uint64_t>(toRead), std::memory_order_release);
        return toRead;
    }

    int availableRead() const
    {
        return static_cast<int>(
            writePos_.load(std::memory_order_acquire) -
            readPos_.load(std::memory_order_relaxed));
    }

    int availableWrite() const
    {
        return static_cast<int>(capacity_ -
            (writePos_.load(std::memory_order_relaxed) -
             readPos_.load(std::memory_order_acquire)));
    }

    void reset()
    {
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<std::vector<float>> data_;
    uint32_t capacity_ = 0;
    uint32_t mask_ = 0;
    int channels_ = 0;
    alignas(64) std::atomic<uint64_t> writePos_{0};
    alignas(64) std::atomic<uint64_t> readPos_{0};
};

} // namespace directpipe
