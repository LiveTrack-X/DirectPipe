/**
 * @file AudioRecorder.cpp
 * @brief Lock-free audio recorder implementation
 */

#include "AudioRecorder.h"

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
        juce::Logger::writeToLog("AudioRecorder: Failed to open file for writing");
        return false;
    }

    auto* writer = wavFormat.createWriterFor(
        outputStream,
        sampleRate,
        static_cast<unsigned int>(numChannels),
        24, {}, 0);

    if (!writer) {
        juce::Logger::writeToLog("AudioRecorder: Failed to create WAV writer");
        return false;
    }

    // ThreadedWriter takes ownership of the writer
    // FIFO size: 32768 samples (~0.68s at 48kHz)
    threadedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer, writerThread_, 32768);

    recording_.store(true, std::memory_order_release);
    juce::Logger::writeToLog("AudioRecorder: Started recording to " + file.getFullPathName());
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
        juce::Logger::writeToLog("AudioRecorder: Stopped. File: " + currentFile_.getFullPathName()
            + " (" + juce::String(getRecordedSeconds(), 1) + "s)");
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
