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
#include "Control/HttpApiServer.h"

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
class MockActionListener : public directpipe::ActionListener {
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
    // dispatch() uses callAsync for off-thread callers, which requires a running
    // JUCE message loop. GTest doesn't provide one, so async events are never
    // delivered. On macOS this also SEGFAULTs (no Cocoa run loop).
    GTEST_SKIP() << "Requires JUCE message loop (not available in GTest)";

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

// ─── Additional Coverage ─────────────────────────────────────────────

TEST_F(ActionDispatcherTest, DispatchWithStringParam) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::SetVolume;
    event.stringParam = "monitor";
    event.floatParam = 0.75f;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().stringParam, "monitor");
    EXPECT_FLOAT_EQ(listener.lastEvent().floatParam, 0.75f);
    dispatcher->removeListener(&listener);
}

TEST_F(ActionDispatcherTest, DispatchSetPluginParameter) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::SetPluginParameter;
    event.intParam = 2;    // plugin index
    event.intParam2 = 5;   // param index
    event.floatParam = 0.3f;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::SetPluginParameter);
    EXPECT_EQ(listener.lastEvent().intParam, 2);
    EXPECT_EQ(listener.lastEvent().intParam2, 5);
    EXPECT_FLOAT_EQ(listener.lastEvent().floatParam, 0.3f);
    dispatcher->removeListener(&listener);
}

TEST_F(ActionDispatcherTest, DispatchRecordingToggle) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::RecordingToggle;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::RecordingToggle);
    dispatcher->removeListener(&listener);
}

TEST_F(ActionDispatcherTest, DispatchIpcToggle) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::IpcToggle;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::IpcToggle);
    dispatcher->removeListener(&listener);
}

TEST_F(ActionDispatcherTest, DispatchInputGainAdjust) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::InputGainAdjust;
    event.floatParam = -0.1f;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::InputGainAdjust);
    EXPECT_FLOAT_EQ(listener.lastEvent().floatParam, -0.1f);
    dispatcher->removeListener(&listener);
}

TEST_F(ActionDispatcherTest, DispatchMonitorToggle) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    ActionEvent event;
    event.action = Action::MonitorToggle;
    dispatcher->dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::MonitorToggle);
    dispatcher->removeListener(&listener);
}

TEST_F(ActionDispatcherTest, RemoveNonexistentListenerNoOp) {
    MockActionListener listener;
    // Remove a listener that was never added — should not crash
    dispatcher->removeListener(&listener);

    // Dispatch should still work with no listeners
    ActionEvent event;
    event.action = Action::PanicMute;
    dispatcher->dispatch(event);
}

TEST_F(ActionDispatcherTest, SequentialDispatchPreservesOrder) {
    MockActionListener listener;
    dispatcher->addListener(&listener);

    for (int i = 0; i < 20; ++i) {
        ActionEvent event;
        event.action = Action::SwitchPresetSlot;
        event.intParam = i % 5;
        dispatcher->dispatch(event);
    }

    EXPECT_EQ(listener.events.size(), 20u);
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(listener.events[i].intParam, i % 5);
    }
    dispatcher->removeListener(&listener);
}

namespace {

std::string sendHttpGet(HttpApiServer& server, const std::string& path)
{
    juce::StreamingSocket client;
    if (!client.connect("127.0.0.1", server.getPort(), 2000))
        return {};

    const auto request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (client.write(request.data(), static_cast<int>(request.size()))
        != static_cast<int>(request.size()))
        return {};

    if (client.waitUntilReady(true, 2000) <= 0)
        return {};

    char buffer[4096] = {};
    const auto bytesRead = client.read(buffer, static_cast<int>(sizeof(buffer) - 1), false);
    return bytesRead > 0 ? std::string(buffer, static_cast<size_t>(bytesRead)) : std::string{};
}

} // namespace

TEST(HttpApiServerTest, HandlesRequestsAfterStopAndRestart)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    MidiHandler midi(dispatcher);
    HttpApiServer server(dispatcher, broadcaster, engine, &midi);

    ASSERT_TRUE(server.start(48766));
    EXPECT_NE(sendHttpGet(server, "/api").find("200 OK"), std::string::npos);
    server.stop();

    ASSERT_TRUE(server.start(48766));
    EXPECT_NE(sendHttpGet(server, "/api").find("200 OK"), std::string::npos);
    server.stop();
}

TEST(HttpApiServerTest, RejectsNonNumericMidiValue)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    MidiHandler midi(dispatcher);
    HttpApiServer server(dispatcher, broadcaster, engine, &midi);

    const auto [status, body] = server.processRequestForTest(
        "GET", "/api/midi/cc/1/2/abc");
    EXPECT_EQ(status, 400);
}

TEST(HttpApiServerTest, RejectsMissingPluginBypassTarget)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    HttpApiServer server(dispatcher, broadcaster, engine);

    const auto [status, body] = server.processRequestForTest(
        "GET", "/api/bypass/0/toggle");
    EXPECT_EQ(status, 404);
}

TEST(HttpApiServerTest, QueuedMutationsReturnAcceptedWhileQueriesRemainOk)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    MidiHandler midi(dispatcher);
    ASSERT_TRUE(engine.getVSTChain().addBuiltinProcessor(
        PluginSlot::Type::BuiltinNoiseRemoval));
    HttpApiServer server(dispatcher, broadcaster, engine, &midi);

    const std::vector<std::string> queuedMutationPaths {
        "/api/limiter/toggle",
        "/api/limiter/ceiling/-0.3",
        "/api/auto/add",
        "/api/bypass/master/toggle",
        "/api/bypass/0/toggle",
        "/api/mute/panic",
        "/api/mute/toggle",
        "/api/volume/monitor/0.5",
        "/api/preset/0",
        "/api/gain/0.1",
        "/api/slot/0",
        "/api/input-mute/toggle",
        "/api/monitor/toggle",
        "/api/plugin/0/param/0/0.5",
        "/api/ipc/toggle",
        "/api/recording/toggle",
        "/api/midi/cc/1/7/127",
        "/api/midi/note/1/60/127",
    };

    for (const auto& path : queuedMutationPaths) {
        const auto [status, body] = server.processRequestForTest("GET", path);
        EXPECT_EQ(status, 202) << path;

        const auto parsed = juce::JSON::parse(juce::String(body));
        ASSERT_TRUE(parsed.isObject()) << path;
        EXPECT_TRUE(static_cast<bool>(
            parsed.getDynamicObject()->getProperty("accepted"))) << path;
        EXPECT_TRUE(static_cast<bool>(
            parsed.getDynamicObject()->getProperty("ok"))) << path;
    }

    EXPECT_EQ(server.processRequestForTest("GET", "/api").first, 200);
    EXPECT_EQ(server.processRequestForTest("GET", "/api/status").first, 200);
    EXPECT_EQ(server.processRequestForTest("GET", "/api/perf").first, 200);
    EXPECT_EQ(server.processRequestForTest("GET", "/api/plugins").first, 200);
    EXPECT_EQ(server.processRequestForTest("GET", "/api/plugin/0/params").first, 200);
    EXPECT_EQ(server.processRequestForTest("GET", "/api/xrun/reset").first, 200);
}

TEST(HttpApiServerTest, QueuedMutationUsesAcceptedHttpStatusLine)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    HttpApiServer server(dispatcher, broadcaster, engine);

    ASSERT_TRUE(server.start(48767));
    const auto response = sendHttpGet(server, "/api/recording/toggle");
    EXPECT_NE(response.find("HTTP/1.1 202 Accepted"), std::string::npos);
    EXPECT_NE(response.find(R"({"ok": true, "accepted": true)"), std::string::npos);
    server.stop();
}

TEST(HttpApiServerTest, PerformanceLatencyIncludesActiveChainPDC)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    HttpApiServer server(dispatcher, broadcaster, engine);

    engine.getLatencyMonitor().reset(48000.0, 480);
    auto& chain = engine.getVSTChain();
    chain.prepareToPlay(48000.0, 480);
    const auto added =
        chain.addBuiltinProcessor(PluginSlot::Type::BuiltinNoiseRemoval);
    ASSERT_TRUE(added.success);
    ASSERT_EQ(chain.getTotalChainPDC(), 480);

    const auto [status, body] =
        server.processRequestForTest("GET", "/api/perf");
    ASSERT_EQ(status, 200);

    const auto parsed = juce::JSON::parse(juce::String(body));
    ASSERT_TRUE(parsed.isObject());
    EXPECT_NEAR(static_cast<double>(
                    parsed.getDynamicObject()->getProperty("latencyMs")),
                30.0, 0.01);
}

TEST(HttpApiServerTest, RejectsInvalidListenPorts)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    AudioEngine engine;
    HttpApiServer server(dispatcher, broadcaster, engine);

    EXPECT_FALSE(server.start(0));
    EXPECT_FALSE(server.start(70000));
    EXPECT_FALSE(server.isRunning());
}
