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
