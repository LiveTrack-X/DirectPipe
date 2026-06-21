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
 * @file AudioRingBuffer.h
 * @brief SPSC lock-free ring buffer for inter-thread audio transfer.
 *
 * Producer (main audio callback) writes non-interleaved float frames.
 * Consumer (monitor device callback) reads them.
 * Used to bridge the main audio callback and an independent monitor device callback.
 */
#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
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
        jassert(capacityFrames > 0 && (capacityFrames & (capacityFrames - 1)) == 0);
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

        if (chCount <= 0 || channelData == nullptr) {
            if (static_cast<uint32_t>(toWrite) <= firstPart) {
                for (int ch = 0; ch < channels_; ++ch)
                    std::fill(&data_[ch][startIdx], &data_[ch][startIdx] + toWrite, 0.0f);
            } else {
                const int second = toWrite - static_cast<int>(firstPart);
                for (int ch = 0; ch < channels_; ++ch) {
                    std::fill(&data_[ch][startIdx], &data_[ch][startIdx] + firstPart, 0.0f);
                    std::fill(&data_[ch][0], &data_[ch][0] + second, 0.0f);
                }
            }

            writePos_.store(wp + static_cast<uint64_t>(toWrite), std::memory_order_release);
            return toWrite;
        }

        if (static_cast<uint32_t>(toWrite) <= firstPart) {
            // No wrap
            for (int ch = 0; ch < chCount; ++ch) {
                if (channelData[ch] != nullptr)
                    std::memcpy(&data_[ch][startIdx], channelData[ch],
                                static_cast<size_t>(toWrite) * sizeof(float));
                else
                    std::fill(&data_[ch][startIdx], &data_[ch][startIdx] + toWrite, 0.0f);
            }
        } else {
            // Wrap around
            const int second = toWrite - static_cast<int>(firstPart);
            for (int ch = 0; ch < chCount; ++ch) {
                if (channelData[ch] != nullptr) {
                    std::memcpy(&data_[ch][startIdx], channelData[ch],
                                firstPart * sizeof(float));
                    std::memcpy(&data_[ch][0], channelData[ch] + firstPart,
                                static_cast<size_t>(second) * sizeof(float));
                } else {
                    std::fill(&data_[ch][startIdx], &data_[ch][startIdx] + firstPart, 0.0f);
                    std::fill(&data_[ch][0], &data_[ch][0] + second, 0.0f);
                }
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

        if (toRead <= 0 || channelData == nullptr) return 0;

        const int chCount = (numChannels < channels_) ? numChannels : channels_;
        const uint32_t startIdx = static_cast<uint32_t>(rp & mask_);
        const uint32_t firstPart = capacity_ - startIdx;

        if (static_cast<uint32_t>(toRead) <= firstPart) {
            for (int ch = 0; ch < chCount; ++ch)
                if (channelData[ch] != nullptr)
                    std::memcpy(channelData[ch], &data_[ch][startIdx],
                                static_cast<size_t>(toRead) * sizeof(float));
        } else {
            const int second = toRead - static_cast<int>(firstPart);
            for (int ch = 0; ch < chCount; ++ch) {
                if (channelData[ch] != nullptr) {
                    std::memcpy(channelData[ch], &data_[ch][startIdx],
                                firstPart * sizeof(float));
                    std::memcpy(channelData[ch] + firstPart, &data_[ch][0],
                                static_cast<size_t>(second) * sizeof(float));
                }
            }
        }

        // Fill extra output channels (mono ring → stereo output)
        for (int ch = chCount; ch < numChannels; ++ch) {
            if (channelData[ch] == nullptr)
                continue;
            if (chCount > 0 && channelData[0] != nullptr)
                std::memcpy(channelData[ch], channelData[0],
                            static_cast<size_t>(toRead) * sizeof(float));
            else
                std::fill(channelData[ch], channelData[ch] + toRead, 0.0f);
        }

        readPos_.store(rp + static_cast<uint64_t>(toRead), std::memory_order_release);
        return toRead;
    }

    /**
     * Peek one sample relative to the current read position without advancing.
     * RT-safe for the single consumer thread. Callers must keep frameOffset
     * within the readable range returned by availableRead().
     */
    float peek(int channel, int frameOffset) const
    {
        if (capacity_ == 0 || channels_ <= 0 || frameOffset < 0)
            return 0.0f;

        const int sourceChannel = (channel >= 0 && channel < channels_) ? channel : 0;
        const uint64_t rp = readPos_.load(std::memory_order_relaxed);
        const uint32_t idx = static_cast<uint32_t>((rp + static_cast<uint64_t>(frameOffset)) & mask_);
        return data_[sourceChannel][idx];
    }

    /**
     * Advance the read position without copying.
     * RT-safe for the single consumer thread.
     */
    int advanceRead(int numFrames)
    {
        return discard(numFrames);
    }

    static int requiredFramesForInterpolatedRead(int numSamples,
                                                 double fractionalPhase,
                                                 double playbackRatio)
    {
        if (numSamples <= 0 || playbackRatio <= 0.0)
            return 0;

        const double phase = (std::max)(0.0, fractionalPhase);
        const double lastPosition = phase + playbackRatio * static_cast<double>((std::max)(0, numSamples - 1));
        return static_cast<int>(std::floor(lastPosition)) + 2;
    }

    /**
     * Fractional monitor read using linear interpolation.
     *
     * Returns 0 and does not advance if there is not enough readable data for
     * the requested interpolated block. This avoids partial audio plus zero-fill
     * at the monitor callback boundary.
     */
    int readInterpolated(float* const* channelData,
                         int numChannels,
                         int numSamples,
                         double playbackRatio,
                         double& fractionalPhase,
                         int& framesConsumed)
    {
        framesConsumed = 0;
        if (channelData == nullptr || numChannels <= 0 || numSamples <= 0 || playbackRatio <= 0.0)
            return 0;

        const int required = requiredFramesForInterpolatedRead(numSamples, fractionalPhase, playbackRatio);
        if (availableRead() < required)
            return 0;

        double position = (std::max)(0.0, fractionalPhase);
        for (int i = 0; i < numSamples; ++i) {
            const int frameIndex = static_cast<int>(std::floor(position));
            const float frac = static_cast<float>(position - static_cast<double>(frameIndex));

            for (int ch = 0; ch < numChannels; ++ch) {
                if (channelData[ch] == nullptr)
                    continue;

                const int sourceChannel = (ch < channels_) ? ch : 0;
                const float s0 = peek(sourceChannel, frameIndex);
                const float s1 = peek(sourceChannel, frameIndex + 1);
                channelData[ch][i] = s0 + frac * (s1 - s0);
            }

            position += playbackRatio;
        }

        framesConsumed = static_cast<int>(std::floor(position));
        fractionalPhase = position - static_cast<double>(framesConsumed);
        advanceRead(framesConsumed);
        return numSamples;
    }

    int availableRead() const
    {
        return static_cast<int>(
            writePos_.load(std::memory_order_acquire) -
            readPos_.load(std::memory_order_relaxed));
    }

    int getCapacityFrames() const
    {
        return static_cast<int>(capacity_);
    }

    /**
     * Drop the oldest readable frames without copying them.
     * RT-safe for the single consumer thread.
     */
    int discard(int numFrames)
    {
        const uint64_t rp = readPos_.load(std::memory_order_relaxed);
        const uint64_t wp = writePos_.load(std::memory_order_acquire);
        const int available = static_cast<int>(wp - rp);
        const int toDiscard = (numFrames < available) ? numFrames : available;

        if (toDiscard <= 0) return 0;

        readPos_.store(rp + static_cast<uint64_t>(toDiscard), std::memory_order_release);
        return toDiscard;
    }

    int availableWrite() const
    {
        return static_cast<int>(capacity_ -
            (writePos_.load(std::memory_order_relaxed) -
             readPos_.load(std::memory_order_acquire)));
    }

    void reset()
    {
        // Zero data BEFORE resetting positions (release ordering ensures
        // producer/consumer threads see cleared data when they observe pos=0).
        for (auto& ch : data_)
            std::fill(ch.begin(), ch.end(), 0.0f);
        writePos_.store(0, std::memory_order_release);
        readPos_.store(0, std::memory_order_release);
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
