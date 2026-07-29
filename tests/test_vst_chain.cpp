// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/VSTChain.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

using namespace directpipe;

class VSTChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure MessageManager exists — the first call creates it on this thread,
        // making this thread the "message thread" for jassert checks.
        juce::MessageManager::getInstance();

        chain_ = std::make_unique<VSTChain>();
        chain_->prepareToPlay(48000.0, 512);
    }

    void TearDown() override {
        chain_.reset();
    }

    // Helper: add a built-in processor and assert success
    void addBuiltin(PluginSlot::Type type) {
        auto r = chain_->addBuiltinProcessor(type);
        ASSERT_TRUE(r.success) << "addBuiltinProcessor failed: " << r.message.toStdString();
    }

    std::unique_ptr<VSTChain> chain_;
};

// Test 1: VSTChain starts with zero plugins
TEST_F(VSTChainTest, InitialEmpty) {
    EXPECT_EQ(chain_->getPluginCount(), 0);
}

// Test 2: addBuiltinProcessor adds a BuiltinFilter and count becomes 1
TEST_F(VSTChainTest, AddBuiltinFilter) {
    auto result = chain_->addBuiltinProcessor(PluginSlot::Type::BuiltinFilter);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinFilter);
}

// Test 3: Add 2 built-in processors, remove first, count goes from 2 to 1
TEST_F(VSTChainTest, AddRemovePlugin) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(chain_->getPluginCount(), 2);

    bool removed = chain_->removePlugin(0);
    EXPECT_TRUE(removed);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    // The remaining plugin should be the NR (was at index 1, now at index 0)
    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinNoiseRemoval);
}

// Test 4: movePlugin swaps order — Filter+NR → NR+Filter after move(0,1)
TEST_F(VSTChainTest, PluginStatusSnapshotSurvivesLaterChainMutation) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    chain_->setPluginBypassed(0, true);
    const int expectedParameterCount = chain_->getPluginParameterCount(0);

    const auto snapshot = chain_->getPluginStatusSnapshot();
    ASSERT_EQ(snapshot.size(), 1u);

    ASSERT_TRUE(chain_->removePlugin(0));
    EXPECT_EQ(chain_->getPluginCount(), 0);

    EXPECT_TRUE(snapshot[0].bypassed);
    EXPECT_TRUE(snapshot[0].loaded);
    EXPECT_FALSE(snapshot[0].name.isEmpty());
    EXPECT_EQ(snapshot[0].parameterCount, expectedParameterCount);
}

TEST_F(VSTChainTest, MovePlugin) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);

    bool moved = chain_->movePlugin(PluginIndex{0}, PluginIndex{1});
    EXPECT_TRUE(moved);

    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinFilter);
}

// Test 5: Bypass toggle — set true, verify, set false, verify
TEST_F(VSTChainTest, BypassToggle) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);

    EXPECT_FALSE(chain_->isPluginBypassed(0));

    chain_->setPluginBypassed(0, true);
    EXPECT_TRUE(chain_->isPluginBypassed(0));

    chain_->setPluginBypassed(0, false);
    EXPECT_FALSE(chain_->isPluginBypassed(0));
}

TEST_F(VSTChainTest, ReportedTotalLatencyTracksActiveChain) {
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);
    addBuiltin(PluginSlot::Type::BuiltinAutoGain);

    EXPECT_EQ(chain_->getTotalChainPDC(), 528);  // 480 + 48 samples

    chain_->setPluginBypassed(0, true);
    EXPECT_EQ(chain_->getTotalChainPDC(), 48);

    chain_->setPluginBypassed(1, true);
    EXPECT_EQ(chain_->getTotalChainPDC(), 0);
}

// Test 6: Each built-in type correctly reports its type via getPluginSlot
TEST_F(VSTChainTest, PluginSlotType) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);
    addBuiltin(PluginSlot::Type::BuiltinAutoGain);

    EXPECT_EQ(chain_->getPluginCount(), 3);

    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    auto* slot2 = chain_->getPluginSlot(2);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);

    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinFilter);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot2->type, PluginSlot::Type::BuiltinAutoGain);
}

// Test 7: addAutoProcessors adds all 3 in Filter+NR+AGC order
TEST_F(VSTChainTest, AutoProcessors) {
    auto result = chain_->addAutoProcessors();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chain_->getPluginCount(), 3);

    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    auto* slot2 = chain_->getPluginSlot(2);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);

    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinFilter);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot2->type, PluginSlot::Type::BuiltinAutoGain);
}

// Test 8: addAutoProcessors skips existing Filter, only adds NR+AGC
TEST_F(VSTChainTest, AutoProcessorsNoDuplicate) {
    // Manually add Filter first
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    // addAutoProcessors should add NR+AGC but not a duplicate Filter
    auto result = chain_->addAutoProcessors();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chain_->getPluginCount(), 3);

    // Verify order: addAutoProcessors inserts at front starting from insertPos=0.
    //   hasFilter=true -> skip, insertPos stays 0
    //   hasNR=false -> insert NR at 0 -> insertPos becomes 1
    //   hasAGC=false -> insert AGC at 1
    //   Result: [NR, AGC, Filter]
    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    auto* slot2 = chain_->getPluginSlot(2);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);

    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinAutoGain);
    EXPECT_EQ(slot2->type, PluginSlot::Type::BuiltinFilter);
}

// Test 9: removePlugin with out-of-range indices returns false gracefully
TEST_F(VSTChainTest, RemoveOutOfRange) {
    // Empty chain — any index is out of range
    EXPECT_FALSE(chain_->removePlugin(-1));
    EXPECT_FALSE(chain_->removePlugin(0));
    EXPECT_FALSE(chain_->removePlugin(999));

    // Add one plugin — only index 0 is valid
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    EXPECT_FALSE(chain_->removePlugin(-1));
    EXPECT_FALSE(chain_->removePlugin(1));
    EXPECT_FALSE(chain_->removePlugin(999));

    // Index 0 should succeed
    EXPECT_TRUE(chain_->removePlugin(0));
    EXPECT_EQ(chain_->getPluginCount(), 0);
}

TEST_F(VSTChainTest, PreloadedSwapRejectsMissingInstanceAndKeepsOldChain) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);

    VSTChain::PreloadedPlugin missing;
    missing.request.name = "Missing cached plugin";
    std::vector<VSTChain::PreloadedPlugin> preloaded;
    preloaded.push_back(std::move(missing));

    bool completed = false;
    bool swapped = chain_->replaceChainWithPreloaded(
        std::move(preloaded),
        [&completed] { completed = true; });

    EXPECT_FALSE(swapped);
    EXPECT_FALSE(completed);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinFilter);
}

TEST_F(VSTChainTest, PreloadedSwapStagesBuiltinsBeforeReplacingOldChain) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);

    VSTChain::PreloadedPlugin preparedBuiltin;
    preparedBuiltin.request.name = "Auto Gain";
    preparedBuiltin.request.builtinType = PluginSlot::Type::BuiltinAutoGain;
    std::vector<VSTChain::PreloadedPlugin> prepared;
    prepared.push_back(std::move(preparedBuiltin));

    bool completed = false;
    const bool swapped = chain_->replaceChainWithPreloaded(
        std::move(prepared),
        [&completed] { completed = true; });

    EXPECT_TRUE(swapped);
    EXPECT_TRUE(completed);
    ASSERT_EQ(chain_->getPluginCount(), 1);
    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinAutoGain);
}

TEST_F(VSTChainTest, DeviceLifecycleSerializesWithStructuralMutations) {
    constexpr int iterations = 40;
    std::atomic<bool> start{false};

    std::thread deviceLifecycle([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();

        for (int i = 0; i < iterations; ++i) {
            chain_->prepareToPlay((i % 2) == 0 ? 48000.0 : 44100.0,
                                  (i % 2) == 0 ? 512 : 256);
            chain_->releaseResources();
        }
    });

    start.store(true, std::memory_order_release);
    for (int i = 0; i < iterations; ++i) {
        const auto added = chain_->addBuiltinProcessor(PluginSlot::Type::BuiltinFilter);
        if (!added.success) {
            ADD_FAILURE() << added.message.toStdString();
            break;
        }
        if (!chain_->removePlugin(chain_->getPluginCount() - 1)) {
            ADD_FAILURE() << "removePlugin failed at iteration " << i;
            break;
        }
    }

    deviceLifecycle.join();

    chain_->prepareToPlay(48000.0, 512);
    EXPECT_EQ(chain_->getPluginCount(), 0);
    const auto finalAdd = chain_->addBuiltinProcessor(PluginSlot::Type::BuiltinFilter);
    EXPECT_TRUE(finalAdd.success);
    EXPECT_EQ(chain_->getPluginCount(), 1);
}

TEST_F(VSTChainTest, CancelInvalidatesPendingGenerationBeforeStaleCallbackCanApply) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    addBuiltin(PluginSlot::Type::BuiltinFilter);

    std::mutex mutex;
    std::condition_variable cv;
    bool workerEntered = false;
    bool releaseWorker = false;
    std::atomic<bool> completed{false};

    chain_->replaceChainAsync(
        {},
        [&](bool) { completed.store(true, std::memory_order_release); },
        [&] {
            std::unique_lock lock(mutex);
            workerEntered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return releaseWorker; });
        });

    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return workerEntered; });
    }

    const auto canceledGeneration = chain_->getAsyncGenerationForTest();
    ASSERT_TRUE(chain_->isLoading());
    ASSERT_TRUE(chain_->isLoadWorkerActiveForTest());
    ASSERT_TRUE(chain_->isAsyncGenerationCurrentForTest(canceledGeneration));

    chain_->cancelPendingAsyncLoad();

    EXPECT_FALSE(chain_->isLoading());
    EXPECT_FALSE(chain_->isAsyncGenerationCurrentForTest(canceledGeneration));

    {
        const std::lock_guard lock(mutex);
        releaseWorker = true;
    }
    cv.notify_all();
    chain_->waitForAsyncWorkerForTest();

    // The stale replacement callback was queued before the sentinel because
    // joining above waits until callAsync() has returned. Dispatch both so the
    // assertion below proves cancellation guards the actual callback path.
    auto sentinelDispatched = std::make_shared<std::atomic<bool>>(false);
    ASSERT_TRUE(juce::MessageManager::callAsync([sentinelDispatched] {
        sentinelDispatched->store(true, std::memory_order_release);
    }));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!sentinelDispatched->load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        MSG message;
        bool dispatchedMessage = false;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            dispatchedMessage = true;
        }
        if (!dispatchedMessage)
            std::this_thread::yield();
    }

    ASSERT_TRUE(sentinelDispatched->load(std::memory_order_acquire));
    EXPECT_FALSE(chain_->isLoadWorkerActiveForTest());
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
    EXPECT_EQ(chain_->getPluginCount(), 1);
    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinFilter);
#endif
}

TEST_F(VSTChainTest, AsyncReplacementFailureKeepsExistingChain) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    addBuiltin(PluginSlot::Type::BuiltinFilter);

    VSTChain::PluginLoadRequest missing;
    missing.name = "Definitely Missing VST";
    missing.path = "C:\\DirectPipe-Test-Missing\\missing.vst3";
    missing.desc.name = missing.name;
    missing.desc.fileOrIdentifier = missing.path;
    missing.desc.pluginFormatName = "VST3";

    std::atomic<bool> completed{false};
    std::atomic<bool> succeeded{true};
    std::vector<VSTChain::PluginLoadRequest> requests;
    requests.push_back(std::move(missing));
    chain_->replaceChainAsync(
        std::move(requests),
        [&](bool ok) {
            succeeded.store(ok, std::memory_order_release);
            completed.store(true, std::memory_order_release);
        });

    chain_->waitForAsyncWorkerForTest();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!completed.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        MSG message;
        bool dispatchedMessage = false;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            dispatchedMessage = true;
        }
        if (!dispatchedMessage)
            std::this_thread::yield();
    }

    ASSERT_TRUE(completed.load(std::memory_order_acquire));
    EXPECT_FALSE(succeeded.load(std::memory_order_acquire));
    ASSERT_EQ(chain_->getPluginCount(), 1);
    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinFilter);
#endif
}
