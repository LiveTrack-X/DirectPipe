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
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
#include <functional>
#endif
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

    [[nodiscard]] bool startRecording(const juce::File& file, double sampleRate, int numChannels);
    void stopRecording();

    /** Write audio samples from the real-time callback. RT-safe. */
    void writeBlock(const juce::AudioBuffer<float>& buffer, int numSamples);  // [RT thread only — ThreadedWriter lock-free FIFO]

    bool isRecording() const { return recording_.load(std::memory_order_acquire); }
    juce::File getRecordingFile() const;
    juce::File getLastCompletedFile() const;
    static juce::File findLatestRecordingFile(const juce::File& folder);
    double getRecordedSeconds() const;
    uint64_t getDroppedBlockCount() const { return droppedBlocks_.load(std::memory_order_relaxed); }

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    void setBeforeRecordingPublishHookForTest(std::function<void()> hook);
    void setBeforeCompletionPublishHookForTest(std::function<void()> hook);
    void withWriterLockHeldForTest(const std::function<void()>& callback);
    bool hasWriterForTest() const;
    bool isWriterLockHeldForTest() const;
#endif

private:
    std::atomic<bool> recording_{false};
    mutable juce::SpinLock writerLock_;  ///< Linearizes writer publication/teardown; RT only try-locks
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    mutable juce::CriticalSection fileStateLock_;
    juce::File currentFile_;
    juce::File lastCompletedFile_;
    juce::TimeSliceThread writerThread_{"Audio Writer"};
    std::atomic<double> sampleRate_{48000.0};
    std::atomic<int64_t> samplesWritten_{0};
    std::atomic<uint64_t> droppedBlocks_{0};
    std::atomic<uint64_t> writerGeneration_{0};

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    std::function<void()> beforeRecordingPublishForTest_;
    std::function<void()> beforeCompletionPublishForTest_;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};

} // namespace directpipe
