/**
 * @file HttpApiServer.cpp
 * @brief HTTP REST API server implementation
 */

#include "HttpApiServer.h"

namespace directpipe {

HttpApiServer::HttpApiServer(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster)
    : dispatcher_(dispatcher), broadcaster_(broadcaster)
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
            juce::Logger::writeToLog("HTTP API: Failed to start on any port");
            return false;
        }
    }

    running_.store(true, std::memory_order_release);
    serverThread_ = std::thread([this] { serverThread(); });

    juce::Logger::writeToLog("HTTP API server started on port " + juce::String(port_));
    return true;
}

void HttpApiServer::stop()
{
    running_.store(false, std::memory_order_release);

    if (serverSocket_) {
        serverSocket_->close();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    juce::Logger::writeToLog("HTTP API server stopped");
}

void HttpApiServer::serverThread()
{
    while (running_.load(std::memory_order_acquire)) {
        if (serverSocket_->waitUntilReady(true, 500) > 0) {
            auto* client = serverSocket_->waitForNextConnection();
            if (client) {
                auto clientPtr = std::unique_ptr<juce::StreamingSocket>(client);
                handleClient(std::move(clientPtr));
            }
        }
    }
}

void HttpApiServer::handleClient(std::unique_ptr<juce::StreamingSocket> client)
{
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

    std::string responseBody = processRequest(method, path);
    std::string response = makeResponse(200, responseBody);

    client->write(response.c_str(), static_cast<int>(response.size()));
    client->close();
}

std::string HttpApiServer::processRequest(const std::string& method, const std::string& path)
{
    if (method != "GET") {
        return R"({"error": "Method not allowed"})";
    }

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
        return R"({"error": "Not found"})";
    }

    if (segments.size() < 2) {
        return R"({"info": "DirectPipe API v1.0"})";
    }

    const auto& action = segments[1];

    // GET /api/status
    if (action == "status") {
        return broadcaster_.toJSON();
    }

    // GET /api/bypass/:index/toggle
    if (action == "bypass" && segments.size() >= 3) {
        if (segments[2] == "master") {
            dispatcher_.masterBypass();
            return R"({"ok": true, "action": "master_bypass"})";
        }
        int index = std::atoi(segments[2].c_str());
        dispatcher_.pluginBypass(index);
        return R"({"ok": true, "action": "plugin_bypass", "index": )" +
               std::to_string(index) + "}";
    }

    // GET /api/mute/toggle or /api/mute/panic
    if (action == "mute" && segments.size() >= 3) {
        if (segments[2] == "panic") {
            dispatcher_.panicMute();
            return R"({"ok": true, "action": "panic_mute"})";
        }
        if (segments[2] == "toggle") {
            dispatcher_.toggleMute("all");
            return R"({"ok": true, "action": "toggle_mute"})";
        }
    }

    // GET /api/volume/:target/:value
    if (action == "volume" && segments.size() >= 4) {
        float value = static_cast<float>(std::atof(segments[3].c_str()));
        if (value < 0.0f || value > 1.0f)
            return R"({"error": "value must be 0.0-1.0"})";
        dispatcher_.setVolume(segments[2], value);
        return R"({"ok": true, "action": "set_volume", "target": ")" +
               segments[2] + R"(", "value": )" +
               std::to_string(value) + "}";
    }

    // GET /api/preset/:index
    if (action == "preset" && segments.size() >= 3) {
        int index = std::atoi(segments[2].c_str());
        dispatcher_.loadPreset(index);
        return R"({"ok": true, "action": "load_preset", "index": )" +
               std::to_string(index) + "}";
    }

    // GET /api/gain/:delta
    if (action == "gain" && segments.size() >= 3) {
        float delta = static_cast<float>(std::atof(segments[2].c_str()));
        dispatcher_.inputGainAdjust(delta);
        return R"({"ok": true, "action": "input_gain", "delta": )" +
               std::to_string(delta) + "}";
    }

    // GET /api/slot/:index
    if (action == "slot" && segments.size() >= 3) {
        int index = std::atoi(segments[2].c_str());
        ActionEvent event;
        event.action = Action::SwitchPresetSlot;
        event.intParam = index;
        dispatcher_.dispatch(event);
        return R"({"ok": true, "action": "switch_preset_slot", "slot": )" +
               std::to_string(index) + "}";
    }

    // GET /api/input-mute/toggle
    if (action == "input-mute" && segments.size() >= 3 && segments[2] == "toggle") {
        dispatcher_.inputMuteToggle();
        return R"({"ok": true, "action": "input_mute_toggle"})";
    }

    // GET /api/monitor/toggle
    if (action == "monitor" && segments.size() >= 3 && segments[2] == "toggle") {
        ActionEvent event;
        event.action = Action::MonitorToggle;
        dispatcher_.dispatch(event);
        return R"({"ok": true, "action": "monitor_toggle"})";
    }

    return R"({"error": "Unknown endpoint"})";
}

std::string HttpApiServer::makeResponse(int statusCode, const std::string& body)
{
    std::string status = (statusCode == 200) ? "200 OK" : "400 Bad Request";
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

} // namespace directpipe
