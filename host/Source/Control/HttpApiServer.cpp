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
 * @file HttpApiServer.cpp
 * @brief HTTP REST API server implementation
 */

#include "HttpApiServer.h"
#include "Log.h"
#include <algorithm>
#include <mutex>

namespace directpipe {

// Locale-safe float-to-string (always uses '.' decimal separator)
static std::string floatToString(float value)
{
    return juce::String(value).toStdString();
}

/// Safe integer parsing — returns -1 on overflow or non-numeric input.
/// Prevents undefined behavior from std::atoi on huge digit strings.
static int safeAtoi(const std::string& s)
{
    if (s.empty() || s.size() > 9) return -1;  // max 999,999,999
    return std::atoi(s.c_str());
}

HttpApiServer::HttpApiServer(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster,
                             AudioEngine& engine, MidiHandler* midiHandler)
    : dispatcher_(dispatcher), broadcaster_(broadcaster), engine_(engine), midiHandler_(midiHandler)
{
}

HttpApiServer::~HttpApiServer()
{
    stop();
}

bool HttpApiServer::start(int port)
{
    if (running_.load()) return true;

    port_ = port;
    serverSocket_ = std::make_unique<juce::StreamingSocket>();

    if (!serverSocket_->createListener(port, "127.0.0.1")) {
        // Try fallback ports
        for (int fallback = port + 1; fallback <= port + 5; ++fallback) {
            if (serverSocket_->createListener(fallback, "127.0.0.1")) {
                port_ = fallback;
                break;
            }
        }
        if (!serverSocket_->isConnected()) {
            Log::error("HTTP", "Failed to start on any port (tried " + juce::String(port) + "-" + juce::String(port + 5) + ")");
            return false;
        }
    }

    running_.store(true, std::memory_order_release);
    serverThread_ = std::thread([this] { serverThread(); });

    Log::info("HTTP", "Server started on port " + juce::String(port_));
    return true;
}

void HttpApiServer::stop()
{
    running_.store(false, std::memory_order_release);
    alive_->store(false);

    if (serverSocket_) {
        serverSocket_->close();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    // Join all tracked handler threads (closing server socket above
    // will cause client reads to fail, unblocking the handlers).
    // Move threads out before joining to avoid deadlock (same pattern as WebSocketServer).
    std::vector<std::thread> toJoin;
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        for (auto& h : handlerThreads_)
            toJoin.push_back(std::move(h.thread));
        handlerThreads_.clear();
    }
    for (auto& t : toJoin)
        if (t.joinable()) t.join();

    Log::info("HTTP", "Server stopped");
}

void HttpApiServer::serverThread()
{
    while (running_.load(std::memory_order_acquire)) {
        if (serverSocket_->waitUntilReady(true, 500) > 0) {
            auto* client = serverSocket_->waitForNextConnection();
            if (client) {
                auto aliveFlag = alive_;
                // Clean up finished handler threads — join outside mutex
                std::vector<std::thread> toJoin;
                {
                    std::lock_guard<std::mutex> lock(handlersMutex_);
                    handlerThreads_.erase(
                        std::remove_if(handlerThreads_.begin(), handlerThreads_.end(),
                            [&toJoin](HandlerThread& h) {
                                if (h.done->load()) {
                                    toJoin.push_back(std::move(h.thread));
                                    return true;
                                }
                                return false;
                            }),
                        handlerThreads_.end());
                }
                for (auto& t : toJoin)
                    if (t.joinable()) t.join();
                std::lock_guard<std::mutex> lock(handlersMutex_);
                if (!running_.load()) {
                    delete client;  // stop() already ran — clean up and exit
                    break;
                }
                auto doneFlag = std::make_shared<std::atomic<bool>>(false);
                handlerThreads_.push_back({
                    std::thread([this, client, aliveFlag, doneFlag]() {
                        auto clientPtr = std::unique_ptr<juce::StreamingSocket>(client);
                        if (aliveFlag->load())
                            handleClient(std::move(clientPtr));
                        doneFlag->store(true, std::memory_order_release);
                    }),
                    doneFlag
                });
            }
        }
    }
}

void HttpApiServer::handleClient(std::unique_ptr<juce::StreamingSocket> client)
{
    if (!alive_->load()) return;
    if (!client || !client->isConnected()) return;

    // Timeout: wait up to 3 seconds for data to prevent blocking the accept loop
    if (client->waitUntilReady(true, 3000) <= 0) return;

    char buffer[4096] = {};
    int bytesRead = client->read(buffer, sizeof(buffer) - 1, false);
    if (bytesRead <= 0) return;

    buffer[bytesRead] = '\0';
    std::string request(buffer, static_cast<size_t>(bytesRead));

    // Parse HTTP request line: GET /path HTTP/1.1
    std::string method, path;
    size_t methodEnd = request.find(' ');
    if (methodEnd != std::string::npos) {
        method = request.substr(0, methodEnd);
        size_t pathEnd = request.find(' ', methodEnd + 1);
        if (pathEnd != std::string::npos) {
            path = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
        }
    }

    // Handle CORS preflight (OPTIONS) — browser sends this before PUT/DELETE/etc.
    if (method == "OPTIONS") {
        std::string response = makePreflightResponse();
        client->write(response.c_str(), static_cast<int>(response.size()));
        client->close();
        return;
    }

    auto [statusCode, responseBody] = processRequest(method, path);
    Log::info("HTTP", juce::String(method) + " " + juce::String(path) + " -> " + juce::String(statusCode));
    Log::audit("HTTP", "Response: " + juce::String(responseBody).substring(0, 200));
    std::string response = makeResponse(statusCode, responseBody);

    client->write(response.c_str(), static_cast<int>(response.size()));
    client->close();
}

std::pair<int, std::string> HttpApiServer::processRequest(const std::string& method, std::string path)
{
    if (method != "GET") {
        return {405, R"({"error": "Method not allowed"})"};
    }

    // Strip query string before parsing path segments
    auto queryPos = path.find('?');
    if (queryPos != std::string::npos)
        path = path.substr(0, queryPos);

    // Parse path segments
    std::vector<std::string> segments;
    std::string segment;
    for (char c : path) {
        if (c == '/') {
            if (!segment.empty()) {
                segments.push_back(segment);
                segment.clear();
            }
        } else {
            segment += c;
        }
    }
    if (!segment.empty()) segments.push_back(segment);

    // Route: /api/...
    if (segments.empty() || segments[0] != "api") {
        return {404, R"({"error": "Not found"})"};
    }

    if (segments.size() < 2) {
        return {200, R"({"info": "DirectPipe API v1.0"})"};
    }

    const auto& action = segments[1];

    // GET /api/status — full application state
    if (action == "status" && segments.size() == 2) {
        return {200, broadcaster_.toJSON()};
    }

    // GET /api/perf — performance/latency data from LatencyMonitor
    if (action == "perf" && segments.size() == 2) {
        auto& monitor = engine_.getLatencyMonitor();
        auto obj = new juce::DynamicObject();
        obj->setProperty("latencyMs", monitor.getTotalLatencyVirtualMicMs());
        obj->setProperty("cpuPercent", monitor.getCpuUsagePercent());
        obj->setProperty("sampleRate", monitor.getSampleRate());
        obj->setProperty("bufferSize", monitor.getBufferSize());
        obj->setProperty("xrunCount", engine_.getRecentXRunCount());
        return {200, juce::JSON::toString(juce::var(obj), true).toStdString()};
    }

    // GET /api/xrun/reset — reset xrun counter
    if (action == "xrun" && segments.size() >= 3 && segments[2] == "reset") {
        engine_.requestXRunReset();
        return {200, R"({"ok": true, "action": "xrun_reset"})"};
    }

    // GET /api/limiter/toggle — toggle safety limiter on/off
    if (action == "limiter" && segments.size() >= 3 && segments[2] == "toggle") {
        dispatcher_.dispatch({Action::SafetyLimiterToggle});
        return {200, R"({"ok": true, "action": "safety_limiter_toggle"})"};
    }

    // GET /api/limiter/ceiling/:value — set safety limiter ceiling (-6.0 ~ 0.0 dBFS)
    if (action == "limiter" && segments.size() >= 4 && segments[2] == "ceiling") {
        auto valueStr = juce::String(segments[3]);
        if (valueStr.isEmpty() || valueStr.indexOfAnyOf("-0123456789.") < 0)
            return {400, R"({"error": "value must be a number"})"};
        float value = valueStr.getFloatValue();
        if (value < -6.0f || value > 0.0f)
            return {400, R"({"error": "ceiling must be -6.0 to 0.0 dBFS"})"};
        dispatcher_.dispatch({Action::SetSafetyLimiterCeiling, 0, value});
        return {200, R"({"ok": true, "action": "set_limiter_ceiling", "value": )" +
               std::to_string(static_cast<double>(value)) + "}"};
    }

    // GET /api/auto/add -- add Filter+NoiseRemoval+AutoGain processors
    if (action == "auto" && segments.size() >= 3 && segments[2] == "add") {
        dispatcher_.dispatch({Action::AutoProcessorsAdd});
        return {200, R"({"ok": true, "action": "auto_processors_add"})"};
    }

    // GET /api/bypass/:index/toggle
    if (action == "bypass" && segments.size() >= 3) {
        if (segments[2] == "master") {
            dispatcher_.masterBypass();
            return {200, R"({"ok": true, "action": "master_bypass"})"};
        }
        if (segments[2].find_first_not_of("0123456789") != std::string::npos)
            return {400, R"({"error": "Invalid index"})"};
        int index = safeAtoi(segments[2]);
        if (index < 0)
            return {400, R"({"error": "Invalid index"})"};
        dispatcher_.pluginBypass(index);
        return {200, R"({"ok": true, "action": "plugin_bypass", "index": )" +
               std::to_string(index) + "}"};
    }

    // GET /api/mute/toggle or /api/mute/panic
    if (action == "mute" && segments.size() >= 3) {
        if (segments[2] == "panic") {
            dispatcher_.panicMute();
            return {200, R"({"ok": true, "action": "panic_mute"})"};
        }
        if (segments[2] == "toggle") {
            dispatcher_.toggleMute("all");
            return {200, R"({"ok": true, "action": "toggle_mute"})"};
        }
    }

    // GET /api/volume/:target/:value
    if (action == "volume" && segments.size() >= 4) {
        const auto& target = segments[2];
        if (target != "monitor" && target != "input" && target != "output")
            return {400, "{\"error\": \"Unknown volume target, use monitor, input, or output\"}"};
        // Validate numeric input — getFloatValue() silently returns 0.0 for non-numeric strings
        auto valueStr = juce::String(segments[3]);
        if (valueStr.isEmpty() || valueStr.indexOfAnyOf("0123456789.") < 0)
            return {400, R"({"error": "value must be a number"})"};
        float value = valueStr.getFloatValue();
        // Input gain range is 0.0-2.0 (multiplier), others are 0.0-1.0
        float maxValue = (target == "input") ? 2.0f : 1.0f;
        if (value < 0.0f || value > maxValue)
            return {400, R"({"error": "value out of range"})"};
        dispatcher_.setVolume(target, value);
        return {200, R"({"ok": true, "action": "set_volume", "target": ")" +
               target + R"(", "value": )" +
               floatToString(value) + "}"};
    }

    // GET /api/preset/:index (0-4 = A-E, 5 = Auto)
    if (action == "preset" && segments.size() >= 3) {
        if (segments[2].find_first_not_of("0123456789") != std::string::npos)
            return {400, R"({"error": "Invalid index"})"};
        int index = safeAtoi(segments[2]);
        if (index < 0 || index > 5)
            return {400, "{\"error\": \"Preset index out of range 0-5 (0-4=A-E, 5=Auto)\"}"};
        dispatcher_.loadPreset(index);
        return {200, R"({"ok": true, "action": "load_preset", "index": )" +
               std::to_string(index) + "}"};
    }

    // GET /api/gain/:delta — delta is actual gain change (e.g., 0.1 = +0.1 gain)
    // InputGainAdjust handler applies *0.1f (designed for hotkey steps ±1),
    // so scale by 10 to get correct gain change.
    if (action == "gain" && segments.size() >= 3) {
        auto valueStr = juce::String(segments[2]);
        if (valueStr.isEmpty() || valueStr.indexOfAnyOf("0123456789.-") < 0)
            return {400, R"({"error": "delta must be a number"})"};
        float delta = valueStr.getFloatValue();
        dispatcher_.inputGainAdjust(delta * 10.0f);
        return {200, R"({"ok": true, "action": "input_gain", "delta": )" +
               floatToString(delta) + "}"};
    }

    // GET /api/slot/:index (0-4 = A-E, 5 = Auto)
    if (action == "slot" && segments.size() >= 3) {
        if (segments[2].find_first_not_of("0123456789") != std::string::npos)
            return {400, R"({"error": "Invalid index"})"};
        int index = safeAtoi(segments[2]);
        if (index < 0 || index > 5)
            return {400, "{\"error\": \"Slot index out of range 0-5 (0-4=A-E, 5=Auto)\"}"};
        ActionEvent event;
        event.action = Action::SwitchPresetSlot;
        event.intParam = index;
        dispatcher_.dispatch(event);
        return {200, R"({"ok": true, "action": "switch_preset_slot", "slot": )" +
               std::to_string(index) + "}"};
    }

    // GET /api/input-mute/toggle
    if (action == "input-mute" && segments.size() >= 3 && segments[2] == "toggle") {
        dispatcher_.inputMuteToggle();
        return {200, R"({"ok": true, "action": "input_mute_toggle"})"};
    }

    // GET /api/monitor/toggle
    if (action == "monitor" && segments.size() >= 3 && segments[2] == "toggle") {
        ActionEvent event;
        event.action = Action::MonitorToggle;
        dispatcher_.dispatch(event);
        return {200, R"({"ok": true, "action": "monitor_toggle"})"};
    }

    // GET /api/plugins — list loaded plugins with metadata
    if (action == "plugins" && segments.size() == 2) {
        auto& chain = engine_.getVSTChain();
        juce::Array<juce::var> arr;
        int count = chain.getPluginCount();
        auto latencies = chain.getPluginLatencies();
        for (int i = 0; i < count; ++i) {
            auto* slot = chain.getPluginSlot(i);
            auto obj = new juce::DynamicObject();
            obj->setProperty("index", i);
            obj->setProperty("name", slot ? juce::String(slot->name) : "");
            obj->setProperty("bypassed", slot ? slot->bypassed : false);
            obj->setProperty("loaded", slot && slot->getProcessor() != nullptr);
            obj->setProperty("parameterCount", chain.getPluginParameterCount(i));
            obj->setProperty("latencySamples",
                (static_cast<size_t>(i) < latencies.size()) ? latencies[static_cast<size_t>(i)].latencySamples : 0);
            arr.add(juce::var(obj));
        }
        return {200, juce::JSON::toString(juce::var(arr), true).toStdString()};
    }

    // GET /api/plugin/:idx/params — list parameter names for a plugin
    if (action == "plugin" && segments.size() == 4 && segments[3] == "params") {
        int pluginIndex = safeAtoi(segments[2]);
        auto& chain = engine_.getVSTChain();
        if (pluginIndex < 0 || chain.getPluginSlot(pluginIndex) == nullptr)
            return {404, R"({"error": "Plugin not found"})"};
        int paramCount = chain.getPluginParameterCount(pluginIndex);
        juce::Array<juce::var> arr;
        for (int p = 0; p < paramCount; ++p) {
            auto obj = new juce::DynamicObject();
            obj->setProperty("index", p);
            obj->setProperty("name", chain.getPluginParameterName(pluginIndex, p));
            obj->setProperty("value", static_cast<double>(chain.getPluginParameter(pluginIndex, p)));
            arr.add(juce::var(obj));
        }
        return {200, juce::JSON::toString(juce::var(arr), true).toStdString()};
    }

    // GET /api/plugin/:pluginIndex/param/:paramIndex/:value
    if (action == "plugin" && segments.size() >= 6 && segments[3] == "param") {
        if (segments[2].find_first_not_of("0123456789") != std::string::npos ||
            segments[4].find_first_not_of("0123456789") != std::string::npos)
            return {400, R"({"error": "Invalid index"})"};
        int pluginIndex = safeAtoi(segments[2]);
        int paramIndex = safeAtoi(segments[4]);
        if (pluginIndex < 0 || paramIndex < 0)
            return {400, R"({"error": "Invalid index"})"};

        // Validate numeric input — getFloatValue() silently returns 0.0 for non-numeric strings
        auto valueStr = juce::String(segments[5]);
        if (valueStr.isEmpty() || valueStr.indexOfAnyOf("0123456789.") < 0)
            return {400, R"({"error": "value must be a number 0.0-1.0"})"};
        float value = valueStr.getFloatValue();
        if (value < 0.0f || value > 1.0f)
            return {400, R"({"error": "value must be 0.0-1.0"})"};
        ActionEvent event;
        event.action = Action::SetPluginParameter;
        event.intParam = pluginIndex;
        event.intParam2 = paramIndex;
        event.floatParam = value;
        dispatcher_.dispatch(event);
        return {200, R"({"ok": true, "action": "set_plugin_parameter"})"};
    }

    // GET /api/ipc/toggle
    if (action == "ipc" && segments.size() >= 3 && segments[2] == "toggle") {
        ActionEvent event;
        event.action = Action::IpcToggle;
        dispatcher_.dispatch(event);
        return {200, R"({"ok": true, "action": "ipc_toggle"})"};
    }

    // GET /api/recording/toggle
    if (action == "recording" && segments.size() >= 3 && segments[2] == "toggle") {
        ActionEvent event;
        event.action = Action::RecordingToggle;
        dispatcher_.dispatch(event);
        return {200, R"({"ok": true, "action": "recording_toggle"})"};
    }

    // GET /api/midi/cc/:channel/:number/:value — inject test CC message
    // GET /api/midi/note/:channel/:number/:velocity — inject test Note message
    if (action == "midi" && segments.size() >= 5 && midiHandler_) {
        const auto& msgType = segments[2];
        if (segments[3].find_first_not_of("0123456789") != std::string::npos ||
            segments[4].find_first_not_of("0123456789") != std::string::npos)
            return {400, R"({"error": "Invalid number"})"};
        int ch = safeAtoi(segments[3]);
        int num = safeAtoi(segments[4]);
        if (ch < 1 || ch > 16 || num < 0 || num > 127)
            return {400, R"({"error": "channel 1-16, number 0-127"})"};

        if (msgType == "cc" && segments.size() >= 6) {
            int val = safeAtoi(segments[5]);
            if (val < 0 || val > 127)
                return {400, R"({"error": "value 0-127"})"};
            auto msg = juce::MidiMessage::controllerEvent(ch, num, val);
            midiHandler_->injectTestMessage(msg);
            return {200, R"({"ok": true, "action": "midi_cc", "cc": )" + std::to_string(num) +
                   ", \"ch\": " + std::to_string(ch) + ", \"value\": " + std::to_string(val) + "}"};
        }
        if (msgType == "note") {
            int vel = (segments.size() >= 6) ? safeAtoi(segments[5]) : 127;
            if (vel < 0 || vel > 127)
                return {400, R"({"error": "velocity 0-127"})"};
            auto msg = juce::MidiMessage::noteOn(ch, num, static_cast<juce::uint8>(vel));
            midiHandler_->injectTestMessage(msg);
            return {200, R"({"ok": true, "action": "midi_note", "note": )" + std::to_string(num) +
                   ", \"ch\": " + std::to_string(ch) + ", \"velocity\": " + std::to_string(vel) + "}"};
        }
        return {400, R"({"error": "Use /api/midi/cc/:ch/:num/:val or /api/midi/note/:ch/:num/:vel"})"};
    }

    return {404, R"({"error": "Unknown endpoint"})"};
}

std::string HttpApiServer::makePreflightResponse()
{
    return "HTTP/1.1 204 No Content\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
           "Access-Control-Max-Age: 86400\r\n"
           "Connection: close\r\n"
           "\r\n";
}

std::string HttpApiServer::makeResponse(int statusCode, const std::string& body)
{
    std::string status;
    switch (statusCode) {
        case 200: status = "200 OK"; break;
        case 400: status = "400 Bad Request"; break;
        case 404: status = "404 Not Found"; break;
        case 405: status = "405 Method Not Allowed"; break;
        default:  status = std::to_string(statusCode) + " Error"; break;
    }
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

} // namespace directpipe
