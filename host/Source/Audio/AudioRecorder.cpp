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

    sampleRate_ = sampleRate;
    currentFile_ = file;
    samplesWritten_.store(0);

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
    threadedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer, writerThread_, 32768);

    recording_.store(true, std::memory_order_release);
    Log::info("REC", "Started recording to " + file.getFullPathName());
    Log::audit("REC", "Recording config: SR=" + juce::String(sampleRate) + " ch=" + juce::String(numChannels) + " bits=24 FIFO=32768");
    return true;
}

void AudioRecorder::stopRecording()
{
    recording_.store(false, std::memory_order_seq_cst);

    // Acquire SpinLock to ensure the RT thread has exited writeBlock
    {
        const juce::SpinLock::ScopedLockType sl(writerLock_);
        threadedWriter_.reset();
    }

    if (currentFile_.existsAsFile()) {
        auto seconds = getRecordedSeconds();
        auto fileSize = currentFile_.getSize();
        Log::info("REC", "Stopped. File: " + currentFile_.getFullPathName() + " (" + juce::String(seconds, 1) + "s)");
        Log::audit("REC", "Recording stats: duration=" + juce::String(seconds, 2) + "s fileSize=" + juce::String(fileSize) + " bytes samples=" + juce::String(samplesWritten_.load()));
    }
}

void AudioRecorder::writeBlock(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!recording_.load(std::memory_order_acquire)) return;

    const juce::SpinLock::ScopedLockType sl(writerLock_);
    if (!threadedWriter_) return;

    threadedWriter_->write(buffer.getArrayOfReadPointers(), numSamples);
    samplesWritten_.fetch_add(numSamples, std::memory_order_relaxed);
}

juce::File AudioRecorder::getRecordingFile() const
{
    return currentFile_;
}

double AudioRecorder::getRecordedSeconds() const
{
    if (sampleRate_ <= 0.0) return 0.0;
    return static_cast<double>(samplesWritten_.load(std::memory_order_relaxed)) / sampleRate_;
}

} // namespace directpipe
