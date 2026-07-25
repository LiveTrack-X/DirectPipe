/**
 * @file test_websocket_protocol.cpp
 * @brief Tests for WebSocket JSON protocol parsing and state serialization
 *
 * Tests the JSON message format used between the DirectPipe WebSocket server
 * and clients (Stream Deck plugin, etc.). Validates action message parsing,
 * error handling for invalid messages, and state JSON serialization format.
 *
 * Note: These tests validate the protocol logic without requiring a running
 * WebSocket server. They test the JSON parsing/serialization layer that
 * WebSocketServer::processMessage and StateBroadcaster::toJSON implement.
 */

#include <gtest/gtest.h>
#include <JuceHeader.h>
#include "Control/ActionDispatcher.h"
#include "Control/StateBroadcaster.h"
#include "Control/WebSocketServer.h"

#include <array>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
#include <winsock2.h>
#endif

using namespace directpipe;

#if JUCE_WINDOWS
namespace directpipe {

class WebSocketServerTestAccess {
public:
    static std::mutex* installClient(WebSocketServer& server,
                                     std::unique_ptr<juce::StreamingSocket> socket,
                                     bool readyForBroadcast = true)
    {
        auto connection = std::make_shared<WebSocketServer::ClientConnection>();
        connection->socket = std::move(socket);
        connection->readyForBroadcast.store(readyForBroadcast, std::memory_order_release);
        auto* sendMutex = &connection->sendMutex;
        server.clients_.push_back(std::move(connection));
        server.clientCount_.store(1, std::memory_order_relaxed);
        return sendMutex;
    }

    static void setClientReady(WebSocketServer& server, bool ready)
    {
        std::lock_guard<std::mutex> lock(server.clientsMutex_);
        ASSERT_EQ(server.clients_.size(), 1u);
        server.clients_.front()->readyForBroadcast.store(ready, std::memory_order_release);
    }

    static void broadcastNow(WebSocketServer& server, const std::string& message)
    {
        server.broadcastToClients(message);
    }

    static size_t trackedClientCount(WebSocketServer& server)
    {
        std::lock_guard<std::mutex> lock(server.clientsMutex_);
        return server.clients_.size();
    }

    static void disableDiscovery(WebSocketServer& server)
    {
        server.discoveryEnabled_ = false;
    }

    static void startBroadcast(WebSocketServer& server, std::string message)
    {
        server.running_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(server.broadcastMutex_);
            server.pendingBroadcast_ = std::move(message);
            server.hasPendingBroadcast_ = true;
        }
        server.broadcastThread_ = std::thread([&server] { server.broadcastThreadFunc(); });
        server.broadcastCV_.notify_one();
    }
};

} // namespace directpipe
#endif

#if JUCE_WINDOWS
namespace {

struct LoopbackSocketPair {
    std::unique_ptr<juce::StreamingSocket> listener;
    std::unique_ptr<juce::StreamingSocket> sender;
    std::unique_ptr<juce::StreamingSocket> receiver;
};

LoopbackSocketPair makeLoopbackSocketPair()
{
    LoopbackSocketPair pair;
    juce::StreamingSocket initialiseSocketRuntime;
    const auto senderOptions = juce::SocketOptions{}.withSendBufferSize(1024);

    for (int port = 43000; port < 43100; ++port) {
        auto listener = std::make_unique<juce::StreamingSocket>(senderOptions);
        if (!listener->createListener(port, "127.0.0.1"))
            continue;

        auto receiver = std::make_unique<juce::StreamingSocket>(
            juce::SocketOptions{}.withReceiveBufferSize(1024));
        if (!receiver->connect("127.0.0.1", port, 1000))
            continue;

        std::unique_ptr<juce::StreamingSocket> sender(listener->waitForNextConnection());
        if (!sender)
            continue;

        pair.listener = std::move(listener);
        pair.sender = std::move(sender);
        pair.receiver = std::move(receiver);
        return pair;
    }

    return pair;
}

bool saturateSocketSendBuffer(juce::StreamingSocket& socket)
{
    const auto handle = static_cast<SOCKET>(socket.getRawSocketHandle());
    u_long nonBlocking = 1;
    if (ioctlsocket(handle, FIONBIO, &nonBlocking) != 0)
        return false;

    std::array<char, 16 * 1024> payload{};
    int consecutiveWouldBlock = 0;
    std::size_t bytesSent = 0;
    while (bytesSent < 64u * 1024u * 1024u && consecutiveWouldBlock < 5) {
        const auto sent = ::send(handle,
                                 payload.data(),
                                 static_cast<int>(payload.size()),
                                 0);
        if (sent > 0) {
            bytesSent += static_cast<std::size_t>(sent);
            consecutiveWouldBlock = 0;
            continue;
        }

        if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            ++consecutiveWouldBlock;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        break;
    }

    u_long blocking = 0;
    const auto restoredBlocking = ioctlsocket(handle, FIONBIO, &blocking) == 0;
    return restoredBlocking && consecutiveWouldBlock == 5;
}

} // namespace
#endif

// ─── Helpers ────────────────────────────────────────────────────────

/** Parse through the production WebSocketServer::processMessage path. */
static bool parseActionMessage(const std::string& message, ActionEvent& event)
{
    struct CaptureListener final : directpipe::ActionListener {
        void onAction(const ActionEvent& received) override { events.push_back(received); }
        std::vector<ActionEvent> events;
    } listener;

    // Ensure ActionDispatcher treats this GTest thread as the JUCE message
    // thread, so production dispatch remains synchronous and deterministic.
    juce::MessageManager::getInstance();

    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    WebSocketServer server(dispatcher, broadcaster);
    dispatcher.addListener(&listener);
    server.processMessageForTest(message);
    dispatcher.removeListener(&listener);

    if (listener.events.size() != 1)
        return false;

    event = listener.events.front();
    return true;
}

// ─── Valid Action Message Tests ─────────────────────────────────────

class WebSocketProtocolTest : public ::testing::Test {
protected:
    ActionEvent event{};
};

#if JUCE_WINDOWS
TEST(WebSocketServerShutdownTest, ClosesClientSocketBeforeJoiningBlockedBroadcast)
{
    auto sockets = makeLoopbackSocketPair();
    ASSERT_NE(sockets.sender, nullptr);
    ASSERT_NE(sockets.receiver, nullptr);
    ASSERT_TRUE(saturateSocketSendBuffer(*sockets.sender));

    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    WebSocketServer server(dispatcher, broadcaster);

    auto* sendMutex = WebSocketServerTestAccess::installClient(server,
                                                               std::move(sockets.sender));
    WebSocketServerTestAccess::startBroadcast(server, std::string(64 * 1024, 'x'));

    bool senderEntered = false;
    const auto senderDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < senderDeadline) {
        if (!sendMutex->try_lock()) {
            senderEntered = true;
            break;
        }
        sendMutex->unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!senderEntered) {
        sockets.receiver->close();
        server.stop();
        FAIL() << "broadcast sender did not enter the blocking write";
    }

    auto stopResult = std::async(std::launch::async, [&server] { server.stop(); });
    const auto stoppedBeforePeerClosed =
        stopResult.wait_for(std::chrono::milliseconds(750)) == std::future_status::ready;

    // Always release the old shutdown order after recording the result, so a
    // failing regression test cannot strand its worker threads.
    if (!stoppedBeforePeerClosed)
        sockets.receiver->close();

    const auto cleanupCompleted =
        stopResult.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    EXPECT_TRUE(cleanupCompleted);
    if (cleanupCompleted)
        stopResult.get();

    EXPECT_TRUE(stoppedBeforePeerClosed)
        << "stop() waited for the blocked broadcast sender before closing its socket";
}

TEST(WebSocketServerLifecycleTest, BroadcastWaitsForInitialStateCompletion)
{
    auto sockets = makeLoopbackSocketPair();
    ASSERT_NE(sockets.sender, nullptr);
    ASSERT_NE(sockets.receiver, nullptr);

    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    WebSocketServer server(dispatcher, broadcaster);
    WebSocketServerTestAccess::installClient(server, std::move(sockets.sender), false);

    broadcaster.updateState([](AppState& state) { state.currentPreset = "after-ready"; });

    WebSocketServerTestAccess::broadcastNow(server, "before-ready");
    EXPECT_LE(sockets.receiver->waitUntilReady(true, 100), 0);

    WebSocketServerTestAccess::setClientReady(server, true);
    WebSocketServerTestAccess::broadcastNow(server, "after-ready");
    ASSERT_GT(sockets.receiver->waitUntilReady(true, 1000), 0);

    std::array<char, 256> bytes{};
    const auto count = sockets.receiver->read(bytes.data(), static_cast<int>(bytes.size()), false);
    ASSERT_GT(count, 0);
    const std::string frame(bytes.data(), static_cast<size_t>(count));
    EXPECT_NE(frame.find("after-ready"), std::string::npos);

    server.stop();
}

TEST(WebSocketServerLifecycleTest, DelayedBroadcastResnapshotsLatestState)
{
    auto sockets = makeLoopbackSocketPair();
    ASSERT_NE(sockets.sender, nullptr);
    ASSERT_NE(sockets.receiver, nullptr);

    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    WebSocketServer server(dispatcher, broadcaster);
    WebSocketServerTestAccess::installClient(server, std::move(sockets.sender), true);
    broadcaster.updateState([](AppState& state) { state.currentPreset = "Newest"; });

    WebSocketServerTestAccess::broadcastNow(
        server, R"({"type":"state","data":{"preset":"Stale"}})");
    ASSERT_GT(sockets.receiver->waitUntilReady(true, 1000), 0);

    std::array<char, 4096> bytes{};
    const auto count = sockets.receiver->read(bytes.data(), static_cast<int>(bytes.size()), false);
    ASSERT_GT(count, 0);
    const std::string frame(bytes.data(), static_cast<size_t>(count));
    EXPECT_NE(frame.find("Newest"), std::string::npos);
    EXPECT_EQ(frame.find("Stale"), std::string::npos);

    server.stop();
}

TEST(WebSocketServerLifecycleTest, FailedHandshakeIsSweptWithoutBroadcast)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    WebSocketServer server(dispatcher, broadcaster);
    WebSocketServerTestAccess::disableDiscovery(server);
    ASSERT_TRUE(server.start(43150));

    juce::StreamingSocket client;
    ASSERT_TRUE(client.connect("127.0.0.1", server.getPort(), 1000));

    const auto acceptedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < acceptedDeadline
           && WebSocketServerTestAccess::trackedClientCount(server) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(WebSocketServerTestAccess::trackedClientCount(server), 1u);

    const std::string invalidHandshake =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    ASSERT_EQ(client.write(invalidHandshake.data(),
                           static_cast<int>(invalidHandshake.size())),
              static_cast<int>(invalidHandshake.size()));
    client.close();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline
           && WebSocketServerTestAccess::trackedClientCount(server) != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    EXPECT_EQ(WebSocketServerTestAccess::trackedClientCount(server), 0u)
        << "completed handshake thread remained tracked while no broadcasts occurred";
    server.stop();
}
#endif

TEST(WebSocketServerTest, RejectsInvalidListenPorts)
{
    ActionDispatcher dispatcher;
    StateBroadcaster broadcaster;
    WebSocketServer server(dispatcher, broadcaster);

    EXPECT_FALSE(server.start(0));
    EXPECT_FALSE(server.start(70000));
    EXPECT_FALSE(server.isRunning());
}

TEST_F(WebSocketProtocolTest, ParsePluginBypassAction) {
    std::string msg = R"({"type":"action","action":"plugin_bypass","params":{"index":2}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PluginBypass);
    EXPECT_EQ(event.intParam, 2);
}

TEST_F(WebSocketProtocolTest, ParsePluginBypassDefaultIndex) {
    std::string msg = R"({"type":"action","action":"plugin_bypass","params":{}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PluginBypass);
    EXPECT_EQ(event.intParam, 0);
}

TEST_F(WebSocketProtocolTest, ParseMasterBypassAction) {
    std::string msg = R"({"type":"action","action":"master_bypass","params":{}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::MasterBypass);
}

TEST_F(WebSocketProtocolTest, ParseSetVolumeAction) {
    std::string msg = R"({"type":"action","action":"set_volume","params":{"target":"monitor","value":0.75}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::SetVolume);
    EXPECT_EQ(event.stringParam, "monitor");
    EXPECT_FLOAT_EQ(event.floatParam, 0.75f);
}

TEST_F(WebSocketProtocolTest, ParseSetVolumeOutput) {
    std::string msg = R"({"type":"action","action":"set_volume","params":{"target":"output","value":0.5}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::SetVolume);
    EXPECT_EQ(event.stringParam, "output");
    EXPECT_FLOAT_EQ(event.floatParam, 0.5f);
}

TEST_F(WebSocketProtocolTest, ParseSetVolumeMinMax) {
    // Volume at 0
    std::string msgMin = R"({"type":"action","action":"set_volume","params":{"target":"input","value":0.0}})";
    ASSERT_TRUE(parseActionMessage(msgMin, event));
    EXPECT_FLOAT_EQ(event.floatParam, 0.0f);

    // Volume at 1
    std::string msgMax = R"({"type":"action","action":"set_volume","params":{"target":"input","value":1.0}})";
    ASSERT_TRUE(parseActionMessage(msgMax, event));
    EXPECT_FLOAT_EQ(event.floatParam, 1.0f);
}

TEST_F(WebSocketProtocolTest, ParseToggleMuteAction) {
    std::string msg = R"({"type":"action","action":"toggle_mute","params":{"target":"monitor"}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::ToggleMute);
    EXPECT_EQ(event.stringParam, "monitor");
}

TEST_F(WebSocketProtocolTest, ParseInputTargetMuteAsInputMuteToggle) {
    std::string msg = R"({"type":"action","action":"toggle_mute","params":{"target":"input"}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::InputMuteToggle);
}

TEST_F(WebSocketProtocolTest, ParseLoadPresetAction) {
    std::string msg = R"({"type":"action","action":"load_preset","params":{"index":4}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::LoadPreset);
    EXPECT_EQ(event.intParam, 4);
}

TEST_F(WebSocketProtocolTest, ParsePanicMuteAction) {
    std::string msg = R"({"type":"action","action":"panic_mute","params":{}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PanicMute);
}

TEST_F(WebSocketProtocolTest, ParsePanicMuteSetAction) {
    std::string msg = R"({"type":"action","action":"panic_mute","params":{"muted":true}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PanicMute);
    EXPECT_EQ(event.stringParam, "set");
    EXPECT_EQ(event.intParam, 1);
}

TEST_F(WebSocketProtocolTest, ParseInputGainAction) {
    std::string msg = R"({"type":"action","action":"input_gain","params":{"delta":-1.0}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::InputGainAdjust);
    EXPECT_FLOAT_EQ(event.floatParam, -1.0f);
}

TEST_F(WebSocketProtocolTest, ParseInputGainPositiveDelta) {
    std::string msg = R"({"type":"action","action":"input_gain","params":{"delta":3.5}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_FLOAT_EQ(event.floatParam, 3.5f);
}

// ─── Invalid Message Tests ──────────────────────────────────────────

TEST_F(WebSocketProtocolTest, RejectEmptyString) {
    EXPECT_FALSE(parseActionMessage("", event));
}

TEST_F(WebSocketProtocolTest, RejectMalformedJson) {
    EXPECT_FALSE(parseActionMessage("{not valid json!!}", event));
}

TEST_F(WebSocketProtocolTest, RejectNonObjectJson) {
    EXPECT_FALSE(parseActionMessage("[1, 2, 3]", event));
    EXPECT_FALSE(parseActionMessage("\"hello\"", event));
    EXPECT_FALSE(parseActionMessage("42", event));
    EXPECT_FALSE(parseActionMessage("null", event));
}

TEST_F(WebSocketProtocolTest, RejectMissingTypeField) {
    std::string msg = R"({"action":"panic_mute","params":{}})";
    EXPECT_FALSE(parseActionMessage(msg, event));
}

TEST_F(WebSocketProtocolTest, RejectWrongTypeField) {
    std::string msg = R"({"type":"state","action":"panic_mute","params":{}})";
    EXPECT_FALSE(parseActionMessage(msg, event));
}

TEST_F(WebSocketProtocolTest, RejectUnknownAction) {
    std::string msg = R"({"type":"action","action":"unknown_action","params":{}})";
    EXPECT_FALSE(parseActionMessage(msg, event));
}

TEST_F(WebSocketProtocolTest, RejectEmptyActionField) {
    std::string msg = R"({"type":"action","action":"","params":{}})";
    EXPECT_FALSE(parseActionMessage(msg, event));
}

TEST_F(WebSocketProtocolTest, ParseActionWithMissingParams) {
    // plugin_bypass without params should still parse, using default index 0
    std::string msg = R"({"type":"action","action":"plugin_bypass"})";
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PluginBypass);
    EXPECT_EQ(event.intParam, 0);
}

TEST_F(WebSocketProtocolTest, ParseActionWithNullParams) {
    std::string msg = R"({"type":"action","action":"panic_mute","params":null})";
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PanicMute);
}

TEST_F(WebSocketProtocolTest, RejectUnsafeOrWronglyTypedProductionParameters) {
    const std::vector<std::string> invalidMessages {
        R"({"type":"action","action":"plugin_bypass","params":{"index":1e300}})",
        R"({"type":"action","action":"plugin_bypass","params":{"index":"1"}})",
        R"({"type":"action","action":"plugin_bypass","params":{"index":-1}})",
        R"({"type":"action","action":"load_preset","params":{}})",
        R"({"type":"action","action":"load_preset","params":{"index":5}})",
        R"({"type":"action","action":"switch_preset_slot","params":{}})",
        R"({"type":"action","action":"switch_preset_slot","params":{"slot":99}})",
        R"({"type":"action","action":"set_volume","params":{"target":"monitor"}})",
        R"({"type":"action","action":"set_volume","params":{"target":42,"value":0.5}})",
        R"({"type":"action","action":"set_volume","params":{"target":"unknown","value":0.5}})",
        R"({"type":"action","action":"set_volume","params":{"target":"virtual_mic","value":0.5}})",
        R"({"type":"action","action":"set_volume","params":{"target":"output","value":"0.5"}})",
        R"({"type":"action","action":"set_volume","params":{"target":"output","value":1e300}})",
        R"({"type":"action","action":"set_volume","params":{"target":"output","value":1e999}})",
        R"({"type":"action","action":"set_volume","params":{"target":"output","value":2.0}})",
        R"({"type":"action","action":"panic_mute","params":{"muted":1}})",
        R"({"type":"action","action":"input_gain","params":{"delta":1e999}})",
        R"({"type":"action","action":"set_plugin_parameter","params":{"paramIndex":0,"value":0.5}})",
        R"({"type":"action","action":"set_plugin_parameter","params":{"pluginIndex":0,"value":0.5}})",
        R"({"type":"action","action":"set_plugin_parameter","params":{"pluginIndex":0,"paramIndex":0}})",
        R"({"type":"action","action":"set_plugin_parameter","params":{"pluginIndex":-1,"paramIndex":0,"value":0.5}})",
        R"({"type":"action","action":"set_plugin_parameter","params":{"pluginIndex":0,"paramIndex":0,"value":NaN}})",
        R"({"type":"action","action":"set_plugin_parameter","params":{"pluginIndex":0,"paramIndex":0,"value":1.5}})",
        R"({"type":"action","action":"set_safety_limiter_ceiling","params":{}})",
        R"({"type":"action","action":"set_safety_limiter_ceiling","params":{"value":-7.0}})",
        R"({"type":"action","action":"monitor_toggle","params":"bad"})",
    };

    for (const auto& message : invalidMessages) {
        ActionEvent rejected;
        EXPECT_FALSE(parseActionMessage(message, rejected)) << message;
    }
}

// ─── State JSON Serialization Tests ─────────────────────────────────

class StateSerializationTest : public ::testing::Test {
protected:
    void SetUp() override {
        broadcaster = std::make_unique<StateBroadcaster>();
    }

    std::unique_ptr<StateBroadcaster> broadcaster;
};

TEST_F(StateSerializationTest, DefaultStateSerializesToValidJson) {
    std::string json = broadcaster->toJSON();

    auto parsed = juce::JSON::parse(juce::String(json));
    ASSERT_TRUE(parsed.isObject()) << "State JSON is not a valid object";

    auto* root = parsed.getDynamicObject();
    ASSERT_NE(root, nullptr);

    // Must have "type" field set to "state"
    EXPECT_EQ(root->getProperty("type").toString(), juce::String("state"));

    // Must have "data" object
    EXPECT_TRUE(root->getProperty("data").isObject());
}

TEST_F(StateSerializationTest, StateContainsVolumeFields) {
    broadcaster->updateState([](AppState& state) {
        state.inputGain = 0.8f;
        state.monitorVolume = 0.4f;
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    auto* volumes = data->getProperty("volumes").getDynamicObject();
    ASSERT_NE(volumes, nullptr);

    EXPECT_NEAR(static_cast<double>(volumes->getProperty("input")), 0.8, 0.001);
    EXPECT_NEAR(static_cast<double>(volumes->getProperty("monitor")), 0.4, 0.001);
}

TEST_F(StateSerializationTest, StateContainsBooleanFlags) {
    broadcaster->updateState([](AppState& state) {
        state.masterBypassed = true;
        state.muted = true;
        state.inputMuted = false;
        state.ipcEnabled = true;
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    EXPECT_TRUE(static_cast<bool>(data->getProperty("master_bypassed")));
    EXPECT_TRUE(static_cast<bool>(data->getProperty("muted")));
    EXPECT_FALSE(static_cast<bool>(data->getProperty("input_muted")));
    EXPECT_TRUE(static_cast<bool>(data->getProperty("ipc_enabled")));
}

TEST_F(StateSerializationTest, StateContainsAudioParams) {
    broadcaster->updateState([](AppState& state) {
        state.latencyMs = 5.2f;
        state.inputLevelDb = -12.5f;
        state.cpuPercent = 3.7f;
        state.sampleRate = 48000.0;
        state.bufferSize = 128;
        state.channelMode = 2;
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    EXPECT_NEAR(static_cast<double>(data->getProperty("latency_ms")), 5.2, 0.1);
    EXPECT_NEAR(static_cast<double>(data->getProperty("level_db")), -12.5, 0.1);
    EXPECT_NEAR(static_cast<double>(data->getProperty("cpu_percent")), 3.7, 0.1);
    EXPECT_DOUBLE_EQ(static_cast<double>(data->getProperty("sample_rate")), 48000.0);
    EXPECT_EQ(static_cast<int>(data->getProperty("buffer_size")), 128);
    EXPECT_EQ(static_cast<int>(data->getProperty("channel_mode")), 2);
}

TEST_F(StateSerializationTest, StateContainsSafetyLimiterHeadroomFields) {
    broadcaster->updateState([](AppState& state) {
        state.limiterEnabled = true;
        state.limiterCeilingdB = -0.3f;
        state.safetyHeadroomEnabled = false;
        state.safetyHeadroomdB = -1.2f;
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    auto* limiter = data->getProperty("safety_limiter").getDynamicObject();
    ASSERT_NE(limiter, nullptr);

    EXPECT_TRUE(static_cast<bool>(limiter->getProperty("enabled")));
    EXPECT_NEAR(static_cast<double>(limiter->getProperty("ceiling_dB")), -0.3, 0.001);
    EXPECT_FALSE(static_cast<bool>(limiter->getProperty("headroom_enabled")));
    EXPECT_NEAR(static_cast<double>(limiter->getProperty("headroom_dB")), -1.2, 0.001);
}

TEST_F(StateSerializationTest, StateContainsPresetName) {
    broadcaster->updateState([](AppState& state) {
        state.currentPreset = "Streaming Vocal";
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->getProperty("preset").toString(),
              juce::String("Streaming Vocal"));
}

TEST_F(StateSerializationTest, StateContainsPluginArray) {
    broadcaster->updateState([](AppState& state) {
        state.plugins.clear();
        state.plugins.push_back({"ReaComp", true, true});
        state.plugins.push_back({"ReaEQ", false, true});
        state.plugins.push_back({"", false, false});
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    auto plugins = *data->getProperty("plugins").getArray();
    ASSERT_EQ(plugins.size(), 3);

    // First plugin: ReaComp, bypassed, loaded
    auto* p0 = plugins[0].getDynamicObject();
    EXPECT_EQ(p0->getProperty("name").toString(), juce::String("ReaComp"));
    EXPECT_TRUE(static_cast<bool>(p0->getProperty("bypass")));
    EXPECT_TRUE(static_cast<bool>(p0->getProperty("loaded")));

    // Second plugin: ReaEQ, active, loaded
    auto* p1 = plugins[1].getDynamicObject();
    EXPECT_EQ(p1->getProperty("name").toString(), juce::String("ReaEQ"));
    EXPECT_FALSE(static_cast<bool>(p1->getProperty("bypass")));
    EXPECT_TRUE(static_cast<bool>(p1->getProperty("loaded")));

    // Third plugin: empty slot
    auto* p2 = plugins[2].getDynamicObject();
    EXPECT_EQ(p2->getProperty("name").toString(), juce::String(""));
    EXPECT_FALSE(static_cast<bool>(p2->getProperty("bypass")));
    EXPECT_FALSE(static_cast<bool>(p2->getProperty("loaded")));
}

TEST_F(StateSerializationTest, EmptyPluginArraySerializesCorrectly) {
    // Default state has no plugins
    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    auto plugins = *data->getProperty("plugins").getArray();
    EXPECT_EQ(plugins.size(), 0);
}

TEST_F(StateSerializationTest, StateJsonIsReproducible) {
    broadcaster->updateState([](AppState& state) {
        state.masterBypassed = true;
        state.muted = false;
        state.monitorVolume = 0.9f;
        state.currentPreset = "Test";
    });

    // Calling toJSON twice should produce the same result
    std::string json1 = broadcaster->toJSON();
    std::string json2 = broadcaster->toJSON();

    EXPECT_EQ(json1, json2);
}

// ─── Round-Trip Protocol Tests ──────────────────────────────────────

/**
 * Mock listener used in round-trip tests.
 */
class MockActionListener : public directpipe::ActionListener {
public:
    void onAction(const ActionEvent& e) override { events.push_back(e); }
    std::vector<ActionEvent> events;
    ActionEvent lastEvent() const { return events.empty() ? ActionEvent{} : events.back(); }
};

TEST_F(WebSocketProtocolTest, RoundTripPluginBypass) {
    // Simulate a Stream Deck plugin sending an action and verify the full chain
    std::string clientMessage = R"({"type":"action","action":"plugin_bypass","params":{"index":1}})";

    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(clientMessage, event));

    // Feed the parsed event to the dispatcher
    ActionDispatcher dispatcher;
    MockActionListener listener;
    dispatcher.addListener(&listener);
    dispatcher.dispatch(event);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.lastEvent().action, Action::PluginBypass);
    EXPECT_EQ(listener.lastEvent().intParam, 1);

    dispatcher.removeListener(&listener);
}

TEST_F(WebSocketProtocolTest, RoundTripSetVolume) {
    std::string clientMessage = R"({"type":"action","action":"set_volume","params":{"target":"output","value":0.42}})";

    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(clientMessage, event));

    EXPECT_EQ(event.action, Action::SetVolume);
    EXPECT_EQ(event.stringParam, "output");
    EXPECT_FLOAT_EQ(event.floatParam, 0.42f);
}

// ─── Additional Protocol Tests ───────────────────────────────────────

TEST_F(WebSocketProtocolTest, ParseRecordingToggle) {
    std::string msg = R"({"type":"action","action":"recording_toggle"})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::RecordingToggle);
}

TEST_F(WebSocketProtocolTest, ParseIpcToggle) {
    std::string msg = R"({"type":"action","action":"ipc_toggle"})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::IpcToggle);
}

TEST_F(WebSocketProtocolTest, ParseMonitorToggle) {
    std::string msg = R"({"type":"action","action":"monitor_toggle"})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::MonitorToggle);
}

TEST_F(WebSocketProtocolTest, ParseInputGainAdjust) {
    std::string msg = R"({"type":"action","action":"input_gain","params":{"delta":0.1}})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::InputGainAdjust);
    EXPECT_FLOAT_EQ(event.floatParam, 0.1f);
}

TEST_F(WebSocketProtocolTest, ParseInputMuteToggle) {
    std::string msg = R"({"type":"action","action":"input_mute_toggle"})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::InputMuteToggle);
}

TEST_F(WebSocketProtocolTest, ParseSetPluginParameter) {
    std::string msg = R"({"type":"action","action":"set_plugin_parameter","params":{"pluginIndex":1,"paramIndex":3,"value":0.6}})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::SetPluginParameter);
    EXPECT_EQ(event.intParam, 1);
    EXPECT_EQ(event.intParam2, 3);
    EXPECT_FLOAT_EQ(event.floatParam, 0.6f);
}

TEST_F(WebSocketProtocolTest, ParseSwitchPresetSlot) {
    std::string msg = R"({"type":"action","action":"switch_preset_slot","params":{"slot":3}})";
    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::SwitchPresetSlot);
    EXPECT_EQ(event.intParam, 3);
}

TEST_F(WebSocketProtocolTest, RejectNonObjectMessage) {
    ActionEvent event;
    EXPECT_FALSE(parseActionMessage("just a string", event));
    EXPECT_FALSE(parseActionMessage("42", event));
    EXPECT_FALSE(parseActionMessage("null", event));
    EXPECT_FALSE(parseActionMessage("[1,2,3]", event));
}

TEST_F(WebSocketProtocolTest, RejectEmptyActionString) {
    std::string msg = R"({"type":"action","action":""})";
    ActionEvent event;
    EXPECT_FALSE(parseActionMessage(msg, event));
}

// ─── Additional State Serialization Tests ────────────────────────────

TEST_F(StateSerializationTest, StateJsonIncludesDeviceLostFields) {
    auto state = juce::String(broadcaster->toJSON());
    auto parsed = juce::JSON::parse(state);
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();

    EXPECT_TRUE(data->hasProperty("device_lost"));
    EXPECT_TRUE(data->hasProperty("monitor_lost"));
    EXPECT_EQ(static_cast<bool>(data->getProperty("device_lost")), false);
    EXPECT_EQ(static_cast<bool>(data->getProperty("monitor_lost")), false);
}

TEST_F(StateSerializationTest, StateJsonIncludesSlotNames) {
    auto state = juce::String(broadcaster->toJSON());
    auto parsed = juce::JSON::parse(state);
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();

    ASSERT_TRUE(data->hasProperty("slot_names"));
    auto* names = data->getProperty("slot_names").getArray();
    ASSERT_NE(names, nullptr);
    EXPECT_EQ(names->size(), 6);  // A-E + Auto
}

TEST_F(StateSerializationTest, StateJsonIncludesRecordingFields) {
    auto state = juce::String(broadcaster->toJSON());
    auto parsed = juce::JSON::parse(state);
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();

    EXPECT_TRUE(data->hasProperty("recording"));
    EXPECT_TRUE(data->hasProperty("recording_seconds"));
    EXPECT_EQ(static_cast<bool>(data->getProperty("recording")), false);
}

TEST_F(StateSerializationTest, StateJsonIncludesIpcEnabled) {
    auto state = juce::String(broadcaster->toJSON());
    auto parsed = juce::JSON::parse(state);
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();

    EXPECT_TRUE(data->hasProperty("ipc_enabled"));
    EXPECT_EQ(static_cast<bool>(data->getProperty("ipc_enabled")), false);
}
