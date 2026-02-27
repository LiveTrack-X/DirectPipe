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
 * @file AudioRecorder.h
 * @brief Lock-free audio recorder using JUCE's ThreadedWriter
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace directpipe {

/**
 * @brief Records processed audio to WAV files, lock-free from audio callback.
 *
 * Uses AudioFormatWriter::ThreadedWriter internally:
 * - Audio callback writes to a lock-free FIFO (no allocation, no mutex)
 * - Background thread flushes FIFO to disk
 */
class AudioRecorder {
public:
    AudioRecorder();
    ~AudioRecorder();

    bool startRecording(const juce::File& file, double sampleRate, int numChannels);
    void stopRecording();

    /** Write audio samples from the real-time callback. RT-safe. */
    void writeBlock(const juce::AudioBuffer<float>& buffer, int numSamples);

    bool isRecording() const { return recording_.load(std::memory_order_relaxed); }
    juce::File getRecordingFile() const;
    double getRecordedSeconds() const;

private:
    std::atomic<bool> recording_{false};
    juce::SpinLock writerLock_;  ///< RT-safe lock protecting threadedWriter_ teardown
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    juce::File currentFile_;
    juce::TimeSliceThread writerThread_{"Audio Writer"};
    double sampleRate_ = 48000.0;
    std::atomic<int64_t> samplesWritten_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};

} // namespace directpipe
