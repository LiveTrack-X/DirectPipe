// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include "Audio/AudioRecorder.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace directpipe;

namespace {

class AudioRecorderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tempDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getNonexistentChildFile("DirectPipeAudioRecorderTest", {}, false);
        ASSERT_TRUE(tempDir_.createDirectory().wasOk());
    }

    void TearDown() override
    {
        tempDir_.deleteRecursively();
    }

    juce::File tempDir_;
};

TEST_F(AudioRecorderTest, DirectStopPublishesCompletedRecording)
{
    AudioRecorder recorder;
    auto output = tempDir_.getChildFile("DirectPipe_20260711_120000.wav");

    EXPECT_FALSE(recorder.getLastCompletedFile().existsAsFile());
    ASSERT_TRUE(recorder.startRecording(output, 48000.0, 2));

    recorder.stopRecording();

    EXPECT_EQ(recorder.getLastCompletedFile(), output);
    EXPECT_TRUE(output.existsAsFile());
}

TEST_F(AudioRecorderTest, ConcurrentStopCannotLeavePublishedStateWithoutWriter)
{
    AudioRecorder recorder;
    auto output = tempDir_.getChildFile("DirectPipe_race.wav");

    std::mutex mutex;
    std::condition_variable cv;
    bool beforePublish = false;
    bool releaseStart = false;

    recorder.setBeforeRecordingPublishHookForTest([&] {
        std::unique_lock lock(mutex);
        beforePublish = true;
        cv.notify_all();
        cv.wait(lock, [&] { return releaseStart; });
    });

    bool startResult = false;
    std::thread starter([&] {
        startResult = recorder.startRecording(output, 48000.0, 2);
    });

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return beforePublish; });
    }

    std::thread stopper([&] { recorder.stopRecording(); });
    {
        const std::lock_guard lock(mutex);
        releaseStart = true;
    }
    cv.notify_all();

    starter.join();
    stopper.join();

    EXPECT_TRUE(startResult);
    EXPECT_FALSE(recorder.isRecording());
    EXPECT_FALSE(recorder.hasWriterForTest());
    EXPECT_EQ(recorder.getLastCompletedFile(), output);
}

TEST_F(AudioRecorderTest, CompletionPublicationIsOrderedAcrossRapidRestart)
{
    AudioRecorder recorder;
    auto firstOutput = tempDir_.getChildFile("DirectPipe_first.wav");
    auto secondOutput = tempDir_.getChildFile("DirectPipe_second.wav");
    ASSERT_TRUE(recorder.startRecording(firstOutput, 48000.0, 2));

    std::mutex mutex;
    std::condition_variable cv;
    bool firstStopBeforePublish = false;
    bool releaseFirstStop = false;
    std::atomic<int> completionHookCalls{0};
    std::atomic<bool> completionHookHeldWriterLock{false};

    recorder.setBeforeCompletionPublishHookForTest([&] {
        if (completionHookCalls.fetch_add(1, std::memory_order_acq_rel) != 0)
            return;

        completionHookHeldWriterLock.store(
            recorder.isWriterLockHeldForTest(), std::memory_order_release);
        std::unique_lock lock(mutex);
        firstStopBeforePublish = true;
        cv.notify_all();
        cv.wait(lock, [&] { return releaseFirstStop; });
    });

    std::thread firstStopper([&] { recorder.stopRecording(); });
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return firstStopBeforePublish; });
    }

    std::atomic<bool> secondStartAttempted{false};
    bool secondStartResult = false;
    std::thread secondStarter([&] {
        secondStartAttempted.store(true, std::memory_order_release);
        secondStartResult = recorder.startRecording(secondOutput, 48000.0, 2);
        if (secondStartResult)
            recorder.stopRecording();
    });

    while (!secondStartAttempted.load(std::memory_order_acquire))
        std::this_thread::yield();

    {
        const std::lock_guard lock(mutex);
        releaseFirstStop = true;
    }
    cv.notify_all();

    firstStopper.join();
    secondStarter.join();

    ASSERT_TRUE(secondStartResult);
    EXPECT_TRUE(completionHookHeldWriterLock.load(std::memory_order_acquire));
    EXPECT_EQ(recorder.getLastCompletedFile(), secondOutput);
    EXPECT_EQ(recorder.getRecordingFile(), secondOutput);
}

TEST_F(AudioRecorderTest, DurationCountsOnlyBlocksAcceptedByThreadedWriter)
{
    AudioRecorder recorder;
    auto output = tempDir_.getChildFile("DirectPipe_accounting.wav");
    ASSERT_TRUE(recorder.startRecording(output, 48000.0, 2));

    juce::AudioBuffer<float> block(2, 256);
    block.clear();
    std::thread writer([&] { recorder.writeBlock(block, block.getNumSamples()); });
    writer.join();

    EXPECT_DOUBLE_EQ(recorder.getRecordedSeconds(), 256.0 / 48000.0);
    EXPECT_EQ(recorder.getDroppedBlockCount(), 0u);
    recorder.stopRecording();
}

TEST_F(AudioRecorderTest, WriterLockContentionDropsWithoutInflatingDuration)
{
    AudioRecorder recorder;
    auto output = tempDir_.getChildFile("DirectPipe_drop.wav");
    ASSERT_TRUE(recorder.startRecording(output, 48000.0, 2));

    std::mutex mutex;
    std::condition_variable cv;
    bool lockHeld = false;
    bool releaseLock = false;

    std::thread holder([&] {
        recorder.withWriterLockHeldForTest([&] {
            std::unique_lock lock(mutex);
            lockHeld = true;
            cv.notify_all();
            cv.wait(lock, [&] { return releaseLock; });
        });
    });

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return lockHeld; });
    }

    juce::AudioBuffer<float> block(2, 256);
    block.clear();
    std::thread writer([&] { recorder.writeBlock(block, block.getNumSamples()); });
    writer.join();

    {
        const std::lock_guard lock(mutex);
        releaseLock = true;
    }
    cv.notify_all();
    holder.join();

    EXPECT_DOUBLE_EQ(recorder.getRecordedSeconds(), 0.0);
    EXPECT_EQ(recorder.getDroppedBlockCount(), 1u);
    recorder.stopRecording();
}

TEST_F(AudioRecorderTest, FindsNewestDirectPipeRecordingForStartupRestore)
{
    auto older = tempDir_.getChildFile("DirectPipe_20260711_120000.wav");
    auto newer = tempDir_.getChildFile("DirectPipe_20260711_120100.wav");
    auto unrelated = tempDir_.getChildFile("other.wav");
    ASSERT_TRUE(older.replaceWithText("older"));
    ASSERT_TRUE(newer.replaceWithText("newer"));
    ASSERT_TRUE(unrelated.replaceWithText("unrelated"));
    ASSERT_TRUE(older.setLastModificationTime(juce::Time(1000)));
    ASSERT_TRUE(newer.setLastModificationTime(juce::Time(3000)));
    ASSERT_TRUE(unrelated.setLastModificationTime(juce::Time(5000)));

    EXPECT_EQ(AudioRecorder::findLatestRecordingFile(tempDir_), newer);
}

TEST_F(AudioRecorderTest, StartupRestoreReturnsEmptyWhenFolderHasNoRecording)
{
    EXPECT_EQ(AudioRecorder::findLatestRecordingFile(tempDir_), juce::File{});
}

} // namespace
