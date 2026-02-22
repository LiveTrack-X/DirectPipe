/**
 * @file WebSocketServer.cpp
 * @brief WebSocket server implementation
 */

#include "WebSocketServer.h"

namespace directpipe {

WebSocketServer::WebSocketServer(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster)
    : dispatcher_(dispatcher), broadcaster_(broadcaster)
{
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

bool WebSocketServer::start(int port)
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
            juce::Logger::writeToLog("WebSocket: Failed to start on any port");
            return false;
        }
    }

    running_.store(true, std::memory_order_release);
    broadcaster_.addListener(this);

    serverThread_ = std::thread([this] { serverThread(); });

    juce::Logger::writeToLog("WebSocket server started on port " + juce::String(port_));
    return true;
}

void WebSocketServer::stop()
{
    running_.store(false, std::memory_order_release);
    broadcaster_.removeListener(this);

    if (serverSocket_) {
        serverSocket_->close();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& client : clients_) {
            if (client->socket) {
                client->socket->close();
            }
            if (client->thread.joinable()) {
                client->thread.join();
            }
        }
        clients_.clear();
    }

    clientCount_.store(0, std::memory_order_relaxed);
    juce::Logger::writeToLog("WebSocket server stopped");
}

void WebSocketServer::serverThread()
{
    while (running_.load(std::memory_order_acquire)) {
        auto client = std::make_unique<juce::StreamingSocket>();

        if (serverSocket_->waitUntilReady(true, 500) > 0) {
            if (serverSocket_->isConnected()) {
                auto accepted = serverSocket_->waitForNextConnection();
                if (accepted) {
                    auto clientSocket = std::make_unique<juce::StreamingSocket>();
                    // In practice, JUCE StreamingSocket::waitForNextConnection
                    // returns a raw socket. Here we use a simplified approach.
                    auto conn = std::make_unique<ClientConnection>();
                    conn->socket = std::unique_ptr<juce::StreamingSocket>(accepted);
                    conn->thread = std::thread([this, rawPtr = conn.get()] {
                        clientThread(std::move(rawPtr->socket));
                    });

                    std::lock_guard<std::mutex> lock(clientsMutex_);
                    clients_.push_back(std::move(conn));
                    clientCount_.fetch_add(1, std::memory_order_relaxed);

                    juce::Logger::writeToLog("WebSocket: Client connected");
                }
            }
        }
    }
}

void WebSocketServer::clientThread(std::unique_ptr<juce::StreamingSocket> client)
{
    if (!client) return;

    // Send initial state
    auto stateJson = broadcaster_.toJSON();
    client->write(stateJson.c_str(), static_cast<int>(stateJson.size()));

    char buffer[4096];
    while (running_.load(std::memory_order_acquire) && client->isConnected()) {
        if (client->waitUntilReady(true, 500) > 0) {
            int bytesRead = client->read(buffer, sizeof(buffer) - 1, false);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                processMessage(std::string(buffer, static_cast<size_t>(bytesRead)));
            } else if (bytesRead < 0) {
                break;  // Connection closed
            }
        }
    }

    client->close();
    clientCount_.fetch_sub(1, std::memory_order_relaxed);
    juce::Logger::writeToLog("WebSocket: Client disconnected");
}

void WebSocketServer::processMessage(const std::string& message)
{
    auto parsed = juce::JSON::parse(juce::String(message));
    if (!parsed.isObject()) return;

    auto* obj = parsed.getDynamicObject();
    if (!obj) return;

    auto type = obj->getProperty("type").toString();
    if (type != "action") return;

    auto actionStr = obj->getProperty("action").toString();
    auto* params = obj->getProperty("params").getDynamicObject();

    ActionEvent event;

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
        return;  // Unknown action
    }

    dispatcher_.dispatch(event);
}

void WebSocketServer::broadcastToClients(const std::string& message)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& conn : clients_) {
        if (conn->socket && conn->socket->isConnected()) {
            conn->socket->write(message.c_str(), static_cast<int>(message.size()));
        }
    }
}

void WebSocketServer::onStateChanged(const AppState& /*state*/)
{
    if (!running_.load(std::memory_order_relaxed)) return;

    auto json = broadcaster_.toJSON();
    broadcastToClients(json);
}

} // namespace directpipe
