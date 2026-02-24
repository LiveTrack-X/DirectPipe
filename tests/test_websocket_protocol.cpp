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

#include <string>
#include <vector>

using namespace directpipe;

// ─── Helpers ────────────────────────────────────────────────────────

/**
 * Parse a JSON action message string and extract the action event,
 * mirroring the logic in WebSocketServer::processMessage.
 *
 * @param message JSON string to parse.
 * @param[out] event Populated with the parsed action.
 * @return true if parsing succeeded and the message was a valid action.
 */
static bool parseActionMessage(const std::string& message, ActionEvent& event)
{
    auto parsed = juce::JSON::parse(juce::String(message));
    if (!parsed.isObject()) return false;

    auto* obj = parsed.getDynamicObject();
    if (!obj) return false;

    auto type = obj->getProperty("type").toString();
    if (type != "action") return false;

    auto actionStr = obj->getProperty("action").toString();
    auto* params = obj->getProperty("params").getDynamicObject();

    if (actionStr == "plugin_bypass") {
        event.action = Action::PluginBypass;
        event.intParam = params ? static_cast<int>(params->getProperty("index")) : 0;
    } else if (actionStr == "master_bypass") {
        event.action = Action::MasterBypass;
    } else if (actionStr == "set_volume") {
        event.action = Action::SetVolume;
        event.stringParam = params ? params->getProperty("target").toString().toStdString() : "monitor";
        event.floatParam = params ? static_cast<float>(static_cast<double>(params->getProperty("value"))) : 1.0f;
    } else if (actionStr == "toggle_mute") {
        event.action = Action::ToggleMute;
        event.stringParam = params ? params->getProperty("target").toString().toStdString() : "";
    } else if (actionStr == "load_preset") {
        event.action = Action::LoadPreset;
        event.intParam = params ? static_cast<int>(params->getProperty("index")) : 0;
    } else if (actionStr == "panic_mute") {
        event.action = Action::PanicMute;
    } else if (actionStr == "input_gain") {
        event.action = Action::InputGainAdjust;
        event.floatParam = params ? static_cast<float>(static_cast<double>(params->getProperty("delta"))) : 1.0f;
    } else {
        return false;
    }

    return true;
}

// ─── Valid Action Message Tests ─────────────────────────────────────

class WebSocketProtocolTest : public ::testing::Test {
protected:
    ActionEvent event{};
};

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

TEST_F(WebSocketProtocolTest, ParseSetVolumeVirtualMic) {
    std::string msg = R"({"type":"action","action":"set_volume","params":{"target":"virtual_mic","value":0.5}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::SetVolume);
    EXPECT_EQ(event.stringParam, "virtual_mic");
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

TEST_F(WebSocketProtocolTest, ParseLoadPresetAction) {
    std::string msg = R"({"type":"action","action":"load_preset","params":{"index":5}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::LoadPreset);
    EXPECT_EQ(event.intParam, 5);
}

TEST_F(WebSocketProtocolTest, ParsePanicMuteAction) {
    std::string msg = R"({"type":"action","action":"panic_mute","params":{}})";

    ASSERT_TRUE(parseActionMessage(msg, event));
    EXPECT_EQ(event.action, Action::PanicMute);
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
        state.virtualMicVolume = 0.6f;
        state.monitorVolume = 0.4f;
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    auto* volumes = data->getProperty("volumes").getDynamicObject();
    ASSERT_NE(volumes, nullptr);

    EXPECT_NEAR(static_cast<double>(volumes->getProperty("input")), 0.8, 0.001);
    EXPECT_NEAR(static_cast<double>(volumes->getProperty("virtual_mic")), 0.6, 0.001);
    EXPECT_NEAR(static_cast<double>(volumes->getProperty("monitor")), 0.4, 0.001);
}

TEST_F(StateSerializationTest, StateContainsBooleanFlags) {
    broadcaster->updateState([](AppState& state) {
        state.masterBypassed = true;
        state.muted = true;
        state.inputMuted = false;
        state.driverConnected = true;
    });

    std::string json = broadcaster->toJSON();
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* data = parsed.getDynamicObject()->getProperty("data").getDynamicObject();
    ASSERT_NE(data, nullptr);

    EXPECT_TRUE(static_cast<bool>(data->getProperty("master_bypassed")));
    EXPECT_TRUE(static_cast<bool>(data->getProperty("muted")));
    EXPECT_FALSE(static_cast<bool>(data->getProperty("input_muted")));
    EXPECT_TRUE(static_cast<bool>(data->getProperty("driver_connected")));
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
    std::string clientMessage = R"({"type":"action","action":"set_volume","params":{"target":"virtual_mic","value":0.42}})";

    ActionEvent event;
    ASSERT_TRUE(parseActionMessage(clientMessage, event));

    EXPECT_EQ(event.action, Action::SetVolume);
    EXPECT_EQ(event.stringParam, "virtual_mic");
    EXPECT_FLOAT_EQ(event.floatParam, 0.42f);
}
