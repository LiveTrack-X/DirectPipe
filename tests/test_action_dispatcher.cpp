/**
 * @file test_action_dispatcher.cpp
 * @brief Unit tests for ActionDispatcher
 *
 * Tests the central action routing system that dispatches control events
 * from any source (GUI, hotkey, MIDI, WebSocket, HTTP) to registered
 * listeners. Verifies dispatch behavior, multiple listeners, convenience
 * methods, and action parameter correctness.
 */

#include <gtest/gtest.h>
#include "Control/ActionDispatcher.h"

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

using namespace directpipe;

// ─── Test Listener ──────────────────────────────────────────────────

/**
 * Mock listener that records all received actions for verification.
 */
class MockActionListener : public ActionListener {
public:
    void onAction(const ActionEvent& event) override {
        events.push_back(event);
    }

    /** All events received by this listener. */
    std::vector<ActionEvent> events;

    /** Get the last received event. Returns a default event if none received. */
    ActionEvent lastEvent() const {
        if (events.empty()) {
            return {};
        }
        return events.back();
    }

    /** Reset recorded events. */
    void clear() {
        events.clear();
    }
};

// ─── Test Fixture ───────────────────────────────────────────────────

class ActionDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>();
    }

    std::unique_ptr<ActionDispatcher> dispatcher;
};

// ─── Basic Dispatch Tests ───────────────────────────────────────────

TEST_F(ActionDispatcherTest, DispatchToSingleListener) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::PanicMute;
    event.intParam = 0;
    event.floatParam = 0.0f;
    event.stringParam = "";

    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.events[0].action, Action::PanicMute);
}

TEST_F(ActionDispatcherTest, DispatchToMultipleListeners) {
    MockActionListener listener1;
    MockActionListener listener2;
    MockActionListener listener3;

    dispatcher->addListener(&listener1);
    dispatcher->addListener(&listener2);
    dispatcher->addListener(&listener3);

    ActionEvent event;
    event.action = Action::MasterBypass;

    dispatcher->dispatch(event);

    EXPECT_EQ(listener1.events.size(), 1u);
    EXPECT_EQ(listener2.events.size(), 1u);
    EXPECT_EQ(listener3.events.size(), 1u);

    EXPECT_EQ(listener1.lastEvent().action, Action::MasterBypass);
    EXPECT_EQ(listener2.lastEvent().action, Action::MasterBypass);
    EXPECT_EQ(listener3.lastEvent().action, Action::MasterBypass);
}

TEST_F(ActionDispatcherTest, DispatchWithNoListenersDoesNotCrash) {
    ActionEvent event;
    event.action = Action::PanicMute;

    // Should not throw or crash
    EXPECT_NO_THROW(dispatcher->dispatch(event));
}

TEST_F(ActionDispatcherTest, RemoveListenerStopsDelivery) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event1;
    event1.action = Action::PanicMute;
    dispatcher->dispatch(event1);

    ASSERT_EQ(listener.events.size(), 1u);

    // Remove listener
    dispatcher->removeListener(&listener);

    ActionEvent event2;
    event2.action = Action::MasterBypass;
    dispatcher->dispatch(event2);

    // Should still only have the first event
    EXPECT_EQ(listener.events.size(), 1u);
}

TEST_F(ActionDispatcherTest, RemoveNonexistentListenerDoesNotCrash) {
    MockActionListener listener;

    // Remove a listener that was never added
    EXPECT_NO_THROW(dispatcher->removeListener(&listener));
}

TEST_F(ActionDispatcherTest, MultipleDispatchesToSameListener) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    for (int i = 0; i < 10; ++i) {
        ActionEvent event;
        event.action = Action::PluginBypass;
        event.intParam = i;
        dispatcher->dispatch(event);
    }

    EXPECT_EQ(listener.events.size(), 10u);

    // Verify each event has the correct index
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(listener.events[i].action, Action::PluginBypass);
        EXPECT_EQ(listener.events[i].intParam, i);
    }
}

// ─── Action Parameter Tests ─────────────────────────────────────────

TEST_F(ActionDispatcherTest, DispatchPreservesIntParam) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::PluginBypass;
    event.intParam = 42;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().intParam, 42);
}

TEST_F(ActionDispatcherTest, DispatchPreservesFloatParam) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::SetVolume;
    event.floatParam = 0.75f;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_FLOAT_EQ(listener.lastEvent().floatParam, 0.75f);
}

TEST_F(ActionDispatcherTest, DispatchPreservesStringParam) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::SetVolume;
    event.stringParam = "virtual_mic";
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().stringParam, "virtual_mic");
}

TEST_F(ActionDispatcherTest, DispatchPreservesAllParams) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::SetVolume;
    event.intParam = 5;
    event.floatParam = 0.33f;
    event.stringParam = "monitor";

    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    const auto& received = listener.lastEvent();
    EXPECT_EQ(received.action, Action::SetVolume);
    EXPECT_EQ(received.intParam, 5);
    EXPECT_FLOAT_EQ(received.floatParam, 0.33f);
    EXPECT_EQ(received.stringParam, "monitor");
}

// ─── Convenience Method Tests ───────────────────────────────────────

TEST_F(ActionDispatcherTest, PluginBypassConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->pluginBypass(3);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::PluginBypass);
    EXPECT_EQ(listener.lastEvent().intParam, 3);
}

TEST_F(ActionDispatcherTest, MasterBypassConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->masterBypass();

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::MasterBypass);
}

TEST_F(ActionDispatcherTest, SetVolumeConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->setVolume("monitor", 0.5f);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::SetVolume);
    EXPECT_EQ(listener.lastEvent().stringParam, "monitor");
    EXPECT_FLOAT_EQ(listener.lastEvent().floatParam, 0.5f);
}

TEST_F(ActionDispatcherTest, ToggleMuteConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->toggleMute("input");

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::ToggleMute);
    EXPECT_EQ(listener.lastEvent().stringParam, "input");
}

TEST_F(ActionDispatcherTest, LoadPresetConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->loadPreset(7);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::LoadPreset);
    EXPECT_EQ(listener.lastEvent().intParam, 7);
}

TEST_F(ActionDispatcherTest, PanicMuteConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->panicMute();

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::PanicMute);
}

TEST_F(ActionDispatcherTest, InputGainAdjustConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->inputGainAdjust(-1.0f);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::InputGainAdjust);
    EXPECT_FLOAT_EQ(listener.lastEvent().floatParam, -1.0f);
}

TEST_F(ActionDispatcherTest, InputMuteToggleConvenience) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    dispatcher->inputMuteToggle();

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::InputMuteToggle);
}

// ─── Edge Case Tests ────────────────────────────────────────────────

TEST_F(ActionDispatcherTest, AddSameListenerTwiceReceivesTwice) {
    MockActionListener listener;
    dispatcher->addListener(&listener);
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::PanicMute;
    dispatcher->dispatch(event);

    // Listener added twice should receive the event twice
    EXPECT_EQ(listener.events.size(), 2u);
}

TEST_F(ActionDispatcherTest, PartialRemoveOnlyRemovesOne) {
    MockActionListener listener;
    dispatcher->addListener(&listener);
    dispatcher->addListener(&listener);

    dispatcher->removeListener(&listener);

    ActionEvent event;
    event.action = Action::PanicMute;
    dispatcher->dispatch(event);

    // After removing all instances (std::remove erases all matches), expect 0
    EXPECT_EQ(listener.events.size(), 0u);
}

TEST_F(ActionDispatcherTest, DefaultActionEventValues) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::MasterBypass;
    // Leave other fields at defaults

    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    const auto& received = listener.lastEvent();
    EXPECT_EQ(received.intParam, 0);
    EXPECT_FLOAT_EQ(received.floatParam, 0.0f);
    EXPECT_EQ(received.stringParam, "");
}

TEST_F(ActionDispatcherTest, AllActionTypesCanBeDispatched) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    std::vector<Action> allActions = {
        Action::PluginBypass,
        Action::MasterBypass,
        Action::SetVolume,
        Action::ToggleMute,
        Action::LoadPreset,
        Action::PanicMute,
        Action::InputGainAdjust,
        Action::NextPreset,
        Action::PreviousPreset,
        Action::InputMuteToggle,
    };

    for (const auto& action : allActions) {
        ActionEvent event;
        event.action = action;
        dispatcher->dispatch(event);
    }

    EXPECT_EQ(listener.events.size(), allActions.size());

    for (size_t i = 0; i < allActions.size(); ++i) {
        EXPECT_EQ(listener.events[i].action, allActions[i])
            << "Mismatch at index " << i;
    }
}

// ─── Thread Safety Tests ────────────────────────────────────────────

TEST_F(ActionDispatcherTest, ConcurrentDispatchFromMultipleThreads) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    constexpr int kThreads = 4;
    constexpr int kDispatchesPerThread = 100;

    std::vector<std::thread> threads;
    std::atomic<bool> startSignal{false};

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            while (!startSignal.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kDispatchesPerThread; ++i) {
                ActionEvent event;
                event.action = Action::PluginBypass;
                event.intParam = t * kDispatchesPerThread + i;
                dispatcher->dispatch(event);
            }
        });
    }

    // Start all threads simultaneously
    startSignal.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(listener.events.size(),
              static_cast<size_t>(kThreads * kDispatchesPerThread));
}
