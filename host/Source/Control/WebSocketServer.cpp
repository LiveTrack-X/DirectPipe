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
 * @file WebSocketServer.cpp
 * @brief RFC 6455 WebSocket server implementation
 */

#include "WebSocketServer.h"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace directpipe {

// ─── SHA-1 Implementation (RFC 3174) ─────────────────────────────────
// Minimal SHA-1 for WebSocket handshake. Not for cryptographic use.

static inline uint32_t sha1RotL(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::string WebSocketServer::sha1(const std::string& input)
{
    // SHA-1 constants
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
             h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // Pre-processing: add padding
    uint64_t msgBits = static_cast<uint64_t>(input.size()) * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0x00);

    // Append original length in bits as 64-bit big-endian
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((msgBits >> (i * 8)) & 0xFF));

    // Process each 64-byte block
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[offset + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
            w[i] = sha1RotL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }

            uint32_t temp = sha1RotL(a, 5) + f + e + k + w[i];
            e = d; d = c; c = sha1RotL(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    // Produce 20-byte hash
    uint8_t hash[20];
    for (int i = 0; i < 4; ++i) {
        hash[i]      = static_cast<uint8_t>((h0 >> (24 - i * 8)) & 0xFF);
        hash[i + 4]  = static_cast<uint8_t>((h1 >> (24 - i * 8)) & 0xFF);
        hash[i + 8]  = static_cast<uint8_t>((h2 >> (24 - i * 8)) & 0xFF);
        hash[i + 12] = static_cast<uint8_t>((h3 >> (24 - i * 8)) & 0xFF);
        hash[i + 16] = static_cast<uint8_t>((h4 >> (24 - i * 8)) & 0xFF);
    }

    return std::string(reinterpret_cast<char*>(hash), 20);
}

std::string WebSocketServer::base64Encode(const uint8_t* data, size_t len)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    return result;
}

// ─── WebSocket Handshake (RFC 6455 Section 4) ────────────────────────

bool WebSocketServer::performHandshake(juce::StreamingSocket* client)
{
    // Read HTTP request (up to 4KB)
    char buf[4096] = {};
    int totalRead = 0;

    // Read until we see \r\n\r\n (end of HTTP headers)
    while (totalRead < static_cast<int>(sizeof(buf) - 1)) {
        if (client->waitUntilReady(true, 5000) <= 0)
            return false;

        int n = client->read(buf + totalRead, static_cast<int>(sizeof(buf)) - 1 - totalRead, false);
        if (n <= 0) return false;
        totalRead += n;
        buf[totalRead] = '\0';

        if (std::strstr(buf, "\r\n\r\n") != nullptr)
            break;
    }

    std::string request(buf, static_cast<size_t>(totalRead));

    // Case-insensitive header matching (RFC 7230)
    std::string requestLower = request;
    std::transform(requestLower.begin(), requestLower.end(), requestLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Check for WebSocket upgrade request
    if (requestLower.find("upgrade: websocket") == std::string::npos)
        return false;

    // Extract Sec-WebSocket-Key (case-insensitive header name)
    std::string key;
    auto keyPos = requestLower.find("sec-websocket-key: ");
    if (keyPos == std::string::npos) return false;
    keyPos += 19; // length of "sec-websocket-key: "
    auto keyEnd = request.find("\r\n", keyPos);  // same offset works on original
    if (keyEnd == std::string::npos) return false;
    key = request.substr(keyPos, keyEnd - keyPos);  // extract from original (value is case-sensitive)

    // Trim whitespace
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
        key.pop_back();

    // Compute accept value: SHA-1(key + magic GUID), then base64
    static const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string hash = sha1(key + magic);
    std::string accept = base64Encode(
        reinterpret_cast<const uint8_t*>(hash.data()), hash.size());

    // Send 101 Switching Protocols response
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";

    client->write(response.c_str(), static_cast<int>(response.size()));
    return true;
}

// ─── WebSocket Frame Encoding ────────────────────────────────────────

void WebSocketServer::sendFrame(juce::StreamingSocket* client, const std::string& payload, uint8_t opcode)
{
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode); // FIN + opcode

    uint64_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len)); // no mask bit for server
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    int result = client->write(reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()));
    if (result == -1) {
        juce::Logger::writeToLog("[WS] sendFrame write error");
    }
}

// ─── WebSocket Frame Decoding ────────────────────────────────────────

static bool readExact(juce::StreamingSocket* client, uint8_t* buf, int count, int timeoutMs = 5000)
{
    int totalRead = 0;
    while (totalRead < count) {
        if (client->waitUntilReady(true, timeoutMs) <= 0)
            return false;
        int n = client->read(reinterpret_cast<char*>(buf + totalRead), count - totalRead, false);
        if (n <= 0) return false;
        totalRead += n;
    }
    return true;
}

std::string WebSocketServer::readFrame(juce::StreamingSocket* client, uint8_t& opcodeOut)
{
    uint8_t header[2];
    if (!readExact(client, header, 2))
        return {};

    opcodeOut = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
        uint8_t ext[2];
        if (!readExact(client, ext, 2)) return {};
        payloadLen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payloadLen == 127) {
        uint8_t ext[8];
        if (!readExact(client, ext, 8)) return {};
        payloadLen = 0;
        for (int i = 0; i < 8; ++i)
            payloadLen = (payloadLen << 8) | ext[i];
    }

    // Safety: reject frames > 1MB
    if (payloadLen > 1024 * 1024) return {};

    uint8_t mask[4] = {};
    if (masked) {
        if (!readExact(client, mask, 4)) return {};
    }

    std::string payload(static_cast<size_t>(payloadLen), '\0');
    if (payloadLen > 0) {
        if (!readExact(client, reinterpret_cast<uint8_t*>(&payload[0]),
                       static_cast<int>(payloadLen)))
            return {};

        // Unmask
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] ^= static_cast<char>(mask[i % 4]);
        }
    }

    return payload;
}

// ─── Server Implementation ───────────────────────────────────────────

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
        for (int fallback = port + 1; fallback <= port + 5; ++fallback) {
            if (serverSocket_->createListener(fallback, "127.0.0.1")) {
                port_ = fallback;
                break;
            }
        }
        if (!serverSocket_->isConnected()) {
            juce::Logger::writeToLog("[WS] Failed to start on any port");
            return false;
        }
    }

    running_.store(true, std::memory_order_release);
    broadcaster_.addListener(this);
    broadcastThread_ = std::thread([this] { broadcastThreadFunc(); });
    serverThread_ = std::thread([this] { serverThread(); });

    // Send UDP discovery broadcast so Stream Deck plugin can connect immediately
    sendDiscoveryBroadcast();

    juce::Logger::writeToLog("[WS] Server started on port " + juce::String(port_));
    return true;
}

void WebSocketServer::stop()
{
    running_.store(false, std::memory_order_release);
    broadcaster_.removeListener(this);

    // Wake up broadcast thread so it can exit
    broadcastCV_.notify_one();
    if (broadcastThread_.joinable())
        broadcastThread_.join();

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
    juce::Logger::writeToLog("[WS] Server stopped");
}

void WebSocketServer::serverThread()
{
    while (running_.load(std::memory_order_acquire)) {
        if (serverSocket_->waitUntilReady(true, 500) > 0) {
            if (serverSocket_->isConnected()) {
                auto accepted = serverSocket_->waitForNextConnection();
                if (accepted) {
                    // Reject if too many clients are connected
                    if (clientCount_.load(std::memory_order_relaxed) >= 16) {
                        juce::Logger::writeToLog("[WS] Max clients (16) reached, rejecting connection");
                        accepted->close();
                        delete accepted;
                        continue;
                    }

                    auto conn = std::make_unique<ClientConnection>();
                    conn->socket = std::unique_ptr<juce::StreamingSocket>(accepted);
                    auto* socketPtr = conn->socket.get();
                    conn->thread = std::thread([this, socketPtr] {
                        clientThread(socketPtr);
                    });

                    std::lock_guard<std::mutex> lock(clientsMutex_);
                    clients_.push_back(std::move(conn));
                    clientCount_.fetch_add(1, std::memory_order_relaxed);

                    juce::Logger::writeToLog("[WS] Client connected");
                }
            }
        }
    }
}

void WebSocketServer::clientThread(juce::StreamingSocket* client)
{
    if (!client) return;

    // Perform RFC 6455 WebSocket handshake
    if (!performHandshake(client)) {
        juce::Logger::writeToLog("[WS] Handshake failed");
        clientCount_.fetch_sub(1, std::memory_order_relaxed);
        return;
    }

    // Send initial state as WebSocket text frame
    auto stateJson = broadcaster_.toJSON();
    sendFrame(client, stateJson);

    // Read WebSocket frames
    while (running_.load(std::memory_order_acquire) && client->isConnected()) {
        if (client->waitUntilReady(true, 500) > 0) {
            uint8_t opcode = 0;
            auto payload = readFrame(client, opcode);

            if (opcode == 0x8) {
                // Close frame — send close back and disconnect
                sendFrame(client, "", 0x8);
                break;
            } else if (opcode == 0x9) {
                // Ping — respond with pong
                sendFrame(client, payload, 0xA);
            } else if (opcode == 0x1) {
                // Text frame — process as JSON action
                if (!payload.empty()) {
                    processMessage(payload);
                }
            } else if (opcode == 0x0 && payload.empty()) {
                // Read error or connection closed
                break;
            }
        }
    }

    clientCount_.fetch_sub(1, std::memory_order_relaxed);
    juce::Logger::writeToLog("[WS] Client disconnected");
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
    juce::Logger::writeToLog("[WS] Command: " + actionStr);
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
    } else if (actionStr == "switch_preset_slot") {
        event.action = Action::SwitchPresetSlot;
        event.intParam = params ? static_cast<int>(params->getProperty("slot")) : 0;
    } else if (actionStr == "input_mute_toggle") {
        event.action = Action::InputMuteToggle;
    } else if (actionStr == "next_preset") {
        event.action = Action::NextPreset;
    } else if (actionStr == "previous_preset") {
        event.action = Action::PreviousPreset;
    } else if (actionStr == "monitor_toggle") {
        event.action = Action::MonitorToggle;
    } else if (actionStr == "recording_toggle") {
        event.action = Action::RecordingToggle;
    } else if (actionStr == "ipc_toggle") {
        event.action = Action::IpcToggle;
    } else if (actionStr == "set_plugin_parameter") {
        event.action = Action::SetPluginParameter;
        event.intParam = params ? static_cast<int>(params->getProperty("pluginIndex")) : 0;
        event.intParam2 = params ? static_cast<int>(params->getProperty("paramIndex")) : 0;
        event.floatParam = params ? static_cast<float>(static_cast<double>(params->getProperty("value"))) : 0.0f;
    } else {
        return;  // Unknown action
    }

    dispatcher_.dispatch(event);
}

void WebSocketServer::broadcastToClients(const std::string& message)
{
    // Collect dead connections outside the lock to avoid blocking
    std::vector<std::unique_ptr<ClientConnection>> deadConns;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);

        // Sweep dead connections before broadcasting
        auto it = std::remove_if(clients_.begin(), clients_.end(),
            [&deadConns](std::unique_ptr<ClientConnection>& conn) {
                if (!conn->socket || !conn->socket->isConnected()) {
                    deadConns.push_back(std::move(conn));
                    return true;
                }
                return false;
            });
        clients_.erase(it, clients_.end());

        for (auto& conn : clients_) {
            if (conn->socket && conn->socket->isConnected()) {
                sendFrame(conn->socket.get(), message);
            }
        }
    }

    // Join dead threads outside the lock
    for (auto& conn : deadConns) {
        if (conn && conn->thread.joinable())
            conn->thread.join();
    }
}

void WebSocketServer::sendDiscoveryBroadcast()
{
    juce::DatagramSocket udp;
    if (udp.bindToPort(0)) {
        std::string msg = "DIRECTPIPE_READY:" + std::to_string(port_);
        udp.write("127.0.0.1", 8767, msg.c_str(), static_cast<int>(msg.size()));
        juce::Logger::writeToLog("[WS] UDP discovery broadcast (port " + juce::String(port_) + ")");
    }
}

void WebSocketServer::onStateChanged(const AppState& /*state*/)
{
    if (!running_.load(std::memory_order_relaxed)) return;

    // Queue broadcast — actual send happens on dedicated broadcast thread
    auto json = broadcaster_.toJSON();
    {
        std::lock_guard<std::mutex> lock(broadcastMutex_);
        pendingBroadcast_ = std::move(json);
        hasPendingBroadcast_ = true;
    }
    broadcastCV_.notify_one();
}

void WebSocketServer::broadcastThreadFunc()
{
    while (running_.load(std::memory_order_acquire)) {
        std::string message;
        {
            std::unique_lock<std::mutex> lock(broadcastMutex_);
            broadcastCV_.wait_for(lock, std::chrono::milliseconds(100),
                [this] { return hasPendingBroadcast_ || !running_.load(std::memory_order_relaxed); });
            if (!hasPendingBroadcast_) continue;
            message = std::move(pendingBroadcast_);
            hasPendingBroadcast_ = false;
        }
        broadcastToClients(message);
    }
}

} // namespace directpipe
