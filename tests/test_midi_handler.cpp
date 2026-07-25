// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include "Control/MidiHandler.h"
#include "Control/ActionDispatcher.h"

#if JUCE_WINDOWS
#include <Windows.h>
#endif

using namespace directpipe;

class MidiHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::MessageManager::getInstance();
        dispatcher_ = std::make_unique<ActionDispatcher>();
        handler_ = std::make_unique<MidiHandler>(*dispatcher_);
    }

    void TearDown() override {
        handler_->shutdown();
        handler_.reset();
        dispatcher_.reset();
    }

    std::unique_ptr<ActionDispatcher> dispatcher_;
    std::unique_ptr<MidiHandler> handler_;
};

TEST_F(MidiHandlerTest, AddBinding) {
    MidiBinding binding;
    binding.cc = 7;
    binding.channel = 1;
    binding.type = MidiMappingType::Toggle;
    binding.action = ActionEvent{Action::ToggleMute};

    handler_->addBinding(binding);
    auto bindings = handler_->getBindings();
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].cc, 7);
    EXPECT_EQ(bindings[0].channel, 1);
}

TEST_F(MidiHandlerTest, RemoveBinding) {
    MidiBinding binding;
    binding.cc = 10;
    binding.action = ActionEvent{Action::ToggleMute};
    handler_->addBinding(binding);

    handler_->removeBinding(0);
    auto bindings = handler_->getBindings();
    EXPECT_TRUE(bindings.empty());
}

TEST_F(MidiHandlerTest, SerializeDeserialize) {
    MidiBinding b;
    b.cc = 7;
    b.channel = 1;
    b.type = MidiMappingType::Continuous;
    ActionEvent act;
    act.action = Action::InputGainAdjust;
    act.floatParam = 0.5f;
    b.action = act;
    handler_->addBinding(b);

    auto mappings = handler_->exportMappings();
    ASSERT_EQ(mappings.size(), 1u);
    EXPECT_EQ(mappings[0].cc, 7);
    EXPECT_EQ(mappings[0].channel, 1);

    handler_->removeBinding(0);
    EXPECT_TRUE(handler_->getBindings().empty());

    handler_->loadFromMappings(mappings);
    auto reloaded = handler_->getBindings();
    ASSERT_EQ(reloaded.size(), 1u);
    EXPECT_EQ(reloaded[0].cc, 7);
}

TEST_F(MidiHandlerTest, LearnStartComplete) {
    bool learnCompleted = false;
    int learnedCC = -1;

    handler_->startLearn([&](int cc, int /*note*/, int /*channel*/, const juce::String& /*device*/) {
        learnedCC = cc;
        learnCompleted = true;
    });
    EXPECT_TRUE(handler_->isLearning());

    auto msg = juce::MidiMessage::controllerEvent(1, 64, 127);
    handler_->injectTestMessage(msg);

    EXPECT_TRUE(learnCompleted);
    EXPECT_EQ(learnedCC, 64);
    EXPECT_FALSE(handler_->isLearning());
}

TEST_F(MidiHandlerTest, LearnCompletionFromMidiThreadRunsOnMessageThread) {
#if JUCE_WINDOWS
    std::atomic<bool> learnCompleted{false};
    std::atomic<bool> ranOnMessageThread{false};

    handler_->startLearn([&](int, int, int, const juce::String&) {
        ranOnMessageThread.store(
            juce::MessageManager::getInstance()->isThisTheMessageThread(),
            std::memory_order_release);
        learnCompleted.store(true, std::memory_order_release);
    });

    std::thread midiThread([&] {
        handler_->injectTestMessage(juce::MidiMessage::controllerEvent(1, 65, 127));
    });
    midiThread.join();

    // A timeout event already queued at the 30-second boundary must not
    // invalidate a completion that the MIDI thread claimed first.
    EXPECT_FALSE(handler_->expireCurrentLearnForTest());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!learnCompleted.load(std::memory_order_acquire)
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

    ASSERT_TRUE(learnCompleted.load(std::memory_order_acquire));
    EXPECT_TRUE(ranOnMessageThread.load(std::memory_order_acquire));
    EXPECT_FALSE(handler_->isLearning());
#else
    GTEST_SKIP() << "Message-loop pumping for this regression is Windows-specific";
#endif
}

TEST_F(MidiHandlerTest, SupersededQueuedLearnCannotCancelNewSession) {
#if JUCE_WINDOWS
    std::atomic<int> oldCompletionCount{0};
    std::atomic<int> newCompletionCount{0};

    handler_->startLearn([&](int, int, int, const juce::String&) {
        oldCompletionCount.fetch_add(1, std::memory_order_acq_rel);
    });

    std::thread midiThread([&] {
        handler_->injectTestMessage(juce::MidiMessage::controllerEvent(1, 66, 127));
    });
    midiThread.join();

    // Supersede the captured-but-not-yet-dispatched completion before pumping
    // the message queue. The stale callback must not retire this new timer.
    handler_->startLearn([&](int, int, int, const juce::String&) {
        newCompletionCount.fetch_add(1, std::memory_order_acq_rel);
    });

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
    EXPECT_EQ(oldCompletionCount.load(std::memory_order_acquire), 0);
    EXPECT_TRUE(handler_->isLearning());

    handler_->injectTestMessage(juce::MidiMessage::controllerEvent(1, 67, 127));
    EXPECT_EQ(newCompletionCount.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(handler_->isLearning());
#else
    GTEST_SKIP() << "Message-loop pumping for this regression is Windows-specific";
#endif
}

TEST_F(MidiHandlerTest, RestartedLifetimeDropsOldCompletionAndAcceptsNewLearn) {
#if JUCE_WINDOWS && defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    std::atomic<int> oldCompletionCount{0};
    std::atomic<int> newCompletionCount{0};

    handler_->startLearn([&](int, int, int, const juce::String&) {
        oldCompletionCount.fetch_add(1, std::memory_order_acq_rel);
    });
    std::thread midiThread([&] {
        handler_->injectTestMessage(juce::MidiMessage::controllerEvent(1, 68, 127));
    });
    midiThread.join();

    handler_->shutdown();
    handler_->beginLifetimeForTest();
    handler_->startLearn([&](int, int, int, const juce::String&) {
        newCompletionCount.fetch_add(1, std::memory_order_acq_rel);
    });

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
    EXPECT_EQ(oldCompletionCount.load(std::memory_order_acquire), 0);
    EXPECT_TRUE(handler_->isLearning());

    handler_->injectTestMessage(juce::MidiMessage::controllerEvent(1, 69, 127));
    EXPECT_EQ(newCompletionCount.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(handler_->isLearning());
#else
    GTEST_SKIP() << "Message-loop pumping for this regression is Windows-specific";
#endif
}

TEST_F(MidiHandlerTest, LearnCancel) {
    bool learnCompleted = false;
    handler_->startLearn([&](int, int, int, const juce::String&) {
        learnCompleted = true;
    });
    EXPECT_TRUE(handler_->isLearning());

    handler_->stopLearn();
    EXPECT_FALSE(handler_->isLearning());
    EXPECT_FALSE(learnCompleted);
}

TEST_F(MidiHandlerTest, LearnTimeout) {
    bool learnCompleted = false;
    handler_->startLearn([&](int, int, int, const juce::String&) {
        learnCompleted = true;
    });
    EXPECT_TRUE(handler_->isLearning());

    EXPECT_TRUE(handler_->expireCurrentLearnForTest());
    EXPECT_FALSE(handler_->isLearning());
    EXPECT_FALSE(learnCompleted);

    // Retire the still-owned Timer object without waiting 30 seconds.
    handler_->stopLearn();
    EXPECT_FALSE(handler_->isLearning());
    EXPECT_FALSE(learnCompleted);
}

TEST_F(MidiHandlerTest, DuplicateBindingOverwrite) {
    MidiBinding b1, b2;
    b1.cc = 7;
    b1.channel = 0;
    b1.action = ActionEvent{Action::ToggleMute};

    b2.cc = 7;
    b2.channel = 0;
    b2.action = ActionEvent{Action::MonitorToggle};

    handler_->addBinding(b1);
    handler_->addBinding(b2);

    auto bindings = handler_->getBindings();
    // Duplicate CC/channel should overwrite, not add a new entry
    EXPECT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].action.action, Action::MonitorToggle);
}

TEST_F(MidiHandlerTest, DispatchOutsideLock) {
    MidiBinding b;
    b.cc = 1;
    b.type = MidiMappingType::Toggle;
    b.action = ActionEvent{Action::ToggleMute};
    handler_->addBinding(b);

    auto msg = juce::MidiMessage::controllerEvent(1, 1, 127);
    handler_->injectTestMessage(msg);
    SUCCEED();
}
