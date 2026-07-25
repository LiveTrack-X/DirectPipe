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
 * @file AudioRecorder.cpp
 * @brief Lock-free audio recorder implementation
 */

#include "AudioRecorder.h"
#include "../Control/Log.h"

namespace directpipe {

AudioRecorder::AudioRecorder()
{
    writerThread_.startThread(juce::Thread::Priority::normal);
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    writerThread_.stopThread(2000);
}

bool AudioRecorder::startRecording(const juce::File& file, double sampleRate, int numChannels)
{
    if (recording_.load()) stopRecording();

    auto parentDir = file.getParentDirectory();
    if (!parentDir.exists())
        parentDir.createDirectory();

    juce::WavAudioFormat wavFormat;
    auto* outputStream = new juce::FileOutputStream(file);
    if (outputStream->failedToOpen()) {
        delete outputStream;
        Log::error("REC", "Failed to open file for writing: " + file.getFullPathName());
        return false;
    }

    auto* writer = wavFormat.createWriterFor(
        outputStream,
        sampleRate,
        static_cast<unsigned int>(numChannels),
        24, {}, 0);

    if (!writer) {
        delete outputStream;
        Log::error("REC", "Failed to create WAV writer (SR=" + juce::String(sampleRate) + " ch=" + juce::String(numChannels) + ")");
        return false;
    }

    // ThreadedWriter takes ownership of the writer
    // FIFO size: 32768 samples (~0.68s at 48kHz)
    auto newThreadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer, writerThread_, 32768);

    {
        // Install the writer and publish recording=true as one linearized
        // transition. stopRecording() uses the same lock, so it can never reset
        // a newly installed writer before the corresponding state is visible.
        const juce::SpinLock::ScopedLockType writerStateLock(writerLock_);
        threadedWriter_ = std::move(newThreadedWriter);
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        samplesWritten_.store(0, std::memory_order_relaxed);
        droppedBlocks_.store(0, std::memory_order_relaxed);
        writerGeneration_.fetch_add(1, std::memory_order_acq_rel);

        {
            const juce::ScopedLock fileStateLock(fileStateLock_);
            currentFile_ = file;
        }

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
        if (beforeRecordingPublishForTest_)
            beforeRecordingPublishForTest_();
#endif

        recording_.store(true, std::memory_order_release);
    }

    Log::info("REC", "Started recording to " + file.getFullPathName());
    Log::audit("REC", "Recording config: SR=" + juce::String(sampleRate) + " ch=" + juce::String(numChannels) + " bits=24 FIFO=32768");
    return true;
}

void AudioRecorder::stopRecording()
{
    bool wasRecording = false;
    juce::File completedFile;
    double completedSampleRate = 0.0;
    int64_t completedSamples = 0;
    uint64_t completedDroppedBlocks = 0;
    {
        // recording=false, generation invalidation, writer teardown, completion
        // publication, and the statistics snapshot are one ordered transition.
        // RT writeBlock() only try-locks and therefore never waits.
        const juce::SpinLock::ScopedLockType sl(writerLock_);
        wasRecording = recording_.exchange(false, std::memory_order_acq_rel);
        writerGeneration_.fetch_add(1, std::memory_order_acq_rel);
        threadedWriter_.reset();

        completedSampleRate = sampleRate_.load(std::memory_order_relaxed);
        completedSamples = samplesWritten_.load(std::memory_order_relaxed);
        completedDroppedBlocks = droppedBlocks_.load(std::memory_order_relaxed);

        {
            const juce::ScopedLock stateLock(fileStateLock_);
            completedFile = currentFile_;
        }

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
        if (wasRecording && beforeCompletionPublishForTest_)
            beforeCompletionPublishForTest_();
#endif

        if (wasRecording) {
            const juce::ScopedLock stateLock(fileStateLock_);
            lastCompletedFile_ = completedFile;
        }
    }

    if (wasRecording && completedFile.existsAsFile()) {
        const auto seconds = completedSampleRate > 0.0
            ? static_cast<double>(completedSamples) / completedSampleRate
            : 0.0;
        auto fileSize = completedFile.getSize();
        Log::info("REC", "Stopped. File: " + completedFile.getFullPathName() + " (" + juce::String(seconds, 1) + "s)");
        Log::audit("REC", "Recording stats: duration=" + juce::String(seconds, 2)
            + "s fileSize=" + juce::String(fileSize)
            + " bytes samples=" + juce::String(completedSamples)
            + " droppedBlocks=" + juce::String(completedDroppedBlocks));
    }
}

void AudioRecorder::writeBlock(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    // RT thread only — must NOT be called from the message thread
    jassert(!juce::MessageManager::getInstanceWithoutCreating()
            || !juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (!recording_.load(std::memory_order_acquire)) return;
    const auto generation = writerGeneration_.load(std::memory_order_acquire);
    const int samplesToWrite = juce::jlimit(0, buffer.getNumSamples(), numSamples);
    if (samplesToWrite <= 0 || buffer.getNumChannels() <= 0) return;

    const juce::SpinLock::ScopedTryLockType sl(writerLock_);
    if (!sl.isLocked()) {
        // Count only a drop belonging to the still-current recording. A block
        // that raced a stop/restart belongs to the retired generation.
        if (recording_.load(std::memory_order_acquire)
            && writerGeneration_.load(std::memory_order_acquire) == generation) {
            droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    if (!recording_.load(std::memory_order_acquire)
        || writerGeneration_.load(std::memory_order_acquire) != generation
        || !threadedWriter_) {
        return;
    }

    if (threadedWriter_->write(buffer.getArrayOfReadPointers(), samplesToWrite))
        samplesWritten_.fetch_add(samplesToWrite, std::memory_order_relaxed);
    else
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
}

juce::File AudioRecorder::getRecordingFile() const
{
    const juce::ScopedLock stateLock(fileStateLock_);
    return currentFile_;
}

juce::File AudioRecorder::getLastCompletedFile() const
{
    const juce::ScopedLock stateLock(fileStateLock_);
    return lastCompletedFile_;
}

juce::File AudioRecorder::findLatestRecordingFile(const juce::File& folder)
{
    if (!folder.isDirectory())
        return {};

    juce::Array<juce::File> recordings;
    folder.findChildFiles(recordings, juce::File::findFiles, false, "DirectPipe_*.wav");

    juce::File latest;
    for (const auto& candidate : recordings) {
        if (!latest.existsAsFile()
            || candidate.getLastModificationTime().toMilliseconds()
                   > latest.getLastModificationTime().toMilliseconds()
            || (candidate.getLastModificationTime() == latest.getLastModificationTime()
                && candidate.getFileName().compareNatural(latest.getFileName()) > 0)) {
            latest = candidate;
        }
    }
    return latest;
}

double AudioRecorder::getRecordedSeconds() const
{
    const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
    if (sampleRate <= 0.0) return 0.0;
    return static_cast<double>(samplesWritten_.load(std::memory_order_relaxed)) / sampleRate;
}

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
void AudioRecorder::setBeforeRecordingPublishHookForTest(std::function<void()> hook)
{
    beforeRecordingPublishForTest_ = std::move(hook);
}

void AudioRecorder::setBeforeCompletionPublishHookForTest(std::function<void()> hook)
{
    beforeCompletionPublishForTest_ = std::move(hook);
}

void AudioRecorder::withWriterLockHeldForTest(const std::function<void()>& callback)
{
    const juce::SpinLock::ScopedLockType sl(writerLock_);
    callback();
}

bool AudioRecorder::hasWriterForTest() const
{
    const juce::SpinLock::ScopedLockType sl(writerLock_);
    return threadedWriter_ != nullptr;
}

bool AudioRecorder::isWriterLockHeldForTest() const
{
    const juce::SpinLock::ScopedTryLockType sl(writerLock_);
    return !sl.isLocked();
}
#endif

} // namespace directpipe
