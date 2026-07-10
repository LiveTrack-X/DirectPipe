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
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

bool WebSocketServer::sendFrame(juce::StreamingSocket* client, const std::string& payload, uint8_t opcode)
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
    int expected = static_cast<int>(frame.size());
    int result = client->write(reinterpret_cast<const char*>(frame.data()), expected);
    if (result != expected) {
        return false;  // Partial write or error — frame is corrupted on the wire
    }
    return true;
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
    if (!readExact(client, header, 2)) {
        opcodeOut = 0xFF;  // Sentinel: read error
        return {};
    }

    opcodeOut = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
        uint8_t ext[2];
        if (!readExact(client, ext, 2)) { opcodeOut = 0xFF; return {}; }
        payloadLen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payloadLen == 127) {
        uint8_t ext[8];
        if (!readExact(client, ext, 8)) { opcodeOut = 0xFF; return {}; }
        payloadLen = 0;
        for (int i = 0; i < 8; ++i)
            payloadLen = (payloadLen << 8) | ext[i];
    }

    // Safety: reject frames > 1MB — send RFC 6455 close frame (1009 = Message Too Big)
    if (payloadLen > 1024 * 1024) {
        uint8_t closePayload[2] = { 0x03, 0xF1 };  // 1009 in network byte order
        sendFrame(client, std::string(reinterpret_cast<char*>(closePayload), 2), 0x8);
        opcodeOut = 0xFF;
        return {};
    }

    uint8_t mask[4] = {};
    if (masked) {
        if (!readExact(client, mask, 4)) { opcodeOut = 0xFF; return {}; }
    }

    std::string payload(static_cast<size_t>(payloadLen), '\0');
    if (payloadLen > 0) {
        if (!readExact(client, reinterpret_cast<uint8_t*>(&payload[0]),
                       static_cast<int>(payloadLen))) {
            opcodeOut = 0xFF;
            return {};
        }

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
    if (port < 1 || port > 65535) {
        Log::error("WS", "Invalid listen port: " + juce::String(port));
        return false;
    }

    port_ = port;
    serverSocket_ = std::make_unique<juce::StreamingSocket>();

    if (!serverSocket_->createListener(port, "127.0.0.1")) {
        const int lastFallback = (std::min)(port + 5, 65535);
        for (int fallback = port + 1; fallback <= lastFallback; ++fallback) {
            if (serverSocket_->createListener(fallback, "127.0.0.1")) {
                port_ = fallback;
                break;
            }
        }
        if (!serverSocket_->isConnected()) {
            Log::error("WS", "Failed to start on any port (tried " + juce::String(port) + "-"
                + juce::String((std::min)(port + 5, 65535)) + ")");
            return false;
        }
    }

    running_.store(true, std::memory_order_release);
    broadcaster_.addListener(this);
    broadcastThread_ = std::thread([this] { broadcastThreadFunc(); });
    serverThread_ = std::thread([this] { serverThread(); });

    // Send UDP discovery broadcast so Stream Deck plugin can connect immediately
    sendDiscoveryBroadcast();

    Log::info("WS", "Server started on port " + juce::String(port_));
    return true;
}

void WebSocketServer::stop()
{
    running_.store(false, std::memory_order_release);
    broadcaster_.removeListener(this);

    // Wake all workers, then close the listener before joining the accept loop.
    // This also prevents a late accepted client from appearing after the close pass.
    broadcastCV_.notify_one();
    if (serverSocket_)
        serverSocket_->close();

    if (serverThread_.joinable())
        serverThread_.join();

    // A broadcast write can block indefinitely when a peer stops reading. Close
    // every client before joining the broadcast sender so the OS write is
    // interrupted. Never take sendMutex here: the blocked sender owns it.
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& client : clients_)
            client->requestSocketClose();
    }

    broadcastCV_.notify_one();
    if (broadcastThread_.joinable())
        broadcastThread_.join();

    // Move registry ownership only after the broadcast worker exits. Shared
    // snapshots keep any in-flight connection alive until its send completes.
    std::vector<std::shared_ptr<ClientConnection>> toJoin;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        toJoin = std::move(clients_);
        clients_.clear();
    }
    for (auto& conn : toJoin) {
        if (conn && conn->thread.joinable())
            conn->thread.join();
    }

    clientCount_.store(0, std::memory_order_relaxed);
    Log::info("WS", "Server stopped");
}

void WebSocketServer::serverThread()
{
    auto lastDiscoveryBroadcastMs = juce::Time::getMillisecondCounter();
    while (running_.load(std::memory_order_acquire)) {
        // Reclaim completed client threads even when no state change occurs.
        // Without this periodic sweep, failed handshakes accumulate until the
        // next broadcast (which may never arrive while the app is idle).
        sweepFinishedClients();

        if (serverSocket_->waitUntilReady(true, 500) > 0) {
            if (serverSocket_->isConnected()) {
                auto accepted = serverSocket_->waitForNextConnection();
                if (accepted) {
                    std::lock_guard<std::mutex> lock(clientsMutex_);

                    // Atomic check-and-increment under same lock — prevents TOCTOU race
                    // 32 concurrent WS clients is generous for SD + dashboards + scripts
                    static constexpr int kMaxClients = 32;
                    if (clientCount_.load(std::memory_order_relaxed) >= kMaxClients) {
                        Log::warn("WS", "Max WebSocket clients reached, rejecting connection");
                        accepted->close();
                        delete accepted;
                        continue;
                    }
                    clientCount_.fetch_add(1, std::memory_order_relaxed);

                    auto conn = std::make_shared<ClientConnection>();
                    conn->socket = std::unique_ptr<juce::StreamingSocket>(accepted);
                    auto* connPtr = conn.get();
                    conn->thread = std::thread([this, connPtr] {
                        clientThread(connPtr);
                    });

                    clients_.push_back(std::move(conn));
                    int count = clientCount_.load(std::memory_order_relaxed);

                    Log::info("WS", "Client connected (total=" + juce::String(count) + ")");
                }
            }
        }

        // The Stream Deck plugin may start after the host (or miss the first
        // datagram while binding). Repeat the localhost announcement so a
        // fallback WebSocket port remains discoverable without scanning ports.
        const auto nowMs = juce::Time::getMillisecondCounter();
        if (static_cast<uint32_t>(nowMs - lastDiscoveryBroadcastMs) >= 2000u) {
            sendDiscoveryBroadcast(false);
            lastDiscoveryBroadcastMs = nowMs;
        }
    }
}

void WebSocketServer::clientThread(ClientConnection* conn)
{
    if (!conn || !conn->socket) return;
    struct CompletionFlag {
        ClientConnection* connection = nullptr;
        ~CompletionFlag()
        {
            if (connection)
                connection->threadFinished.store(true, std::memory_order_release);
        }
    } completion{conn};

    auto* client = conn->socket.get();

    // Perform RFC 6455 WebSocket handshake
    if (!performHandshake(client)) {
        Log::warn("WS", "Handshake failed");
        conn->requestSocketClose();
        clientCount_.fetch_sub(1, std::memory_order_relaxed);
        return;
    }

    // Send initial state as WebSocket text frame
    auto stateJson = broadcaster_.toJSON();
    {
        std::lock_guard<std::mutex> sl(conn->sendMutex);
        if (!sendFrame(client, stateJson)) {
            Log::warn("WS", "Failed to send initial state to client");
            conn->requestSocketClose();
            clientCount_.fetch_sub(1, std::memory_order_relaxed);
            return;
        }

        // Coordinate activation with the pending-broadcast queue. Once ready
        // becomes visible, any already-dequeued worker waits on sendMutex and
        // re-snapshots current state before sending; later changes queue only
        // after this activation section releases broadcastMutex_.
        std::string catchUpState;
        {
            std::lock_guard<std::mutex> broadcastLock(broadcastMutex_);
            catchUpState = broadcaster_.toJSON();
            conn->readyForBroadcast.store(true, std::memory_order_release);
        }
        if (catchUpState != stateJson && !sendFrame(client, catchUpState)) {
            conn->readyForBroadcast.store(false, std::memory_order_release);
            Log::warn("WS", "Failed to send catch-up state to client");
            conn->requestSocketClose();
            clientCount_.fetch_sub(1, std::memory_order_relaxed);
            return;
        }
    }
    Log::audit("WS", "Initial state sent (" + juce::String(stateJson.size()) + " bytes)");

    // Read WebSocket frames
    while (running_.load(std::memory_order_acquire) && client->isConnected()) {
        if (client->waitUntilReady(true, 500) > 0) {
            uint8_t opcode = 0;
            auto payload = readFrame(client, opcode);

            if (opcode == 0x8) {
                // Close frame — send close back and disconnect
                std::lock_guard<std::mutex> sl(conn->sendMutex);
                sendFrame(client, "", 0x8);
                break;
            } else if (opcode == 0x9) {
                // Ping — respond with pong
                std::lock_guard<std::mutex> sl(conn->sendMutex);
                sendFrame(client, payload, 0xA);
            } else if (opcode == 0x0) {
                // Continuation frame — fragmented messages not supported, discard payload
                // (readFrame already consumed the payload bytes, so stream position is correct)
            } else if (opcode == 0x1) {
                // Text frame — process as JSON action
                if (!payload.empty()) {
                    processMessage(payload);
                }
            } else if (opcode == 0xFF) {
                // Read error or connection closed
                break;
            }
        }
    }

    // A peer may send a WebSocket Close frame but keep the TCP connection open.
    // Always close our side so the sweeper can reclaim this finished thread and
    // the client limit cannot be bypassed with dead-but-connected sockets.
    conn->readyForBroadcast.store(false, std::memory_order_release);
    conn->requestSocketClose();
    int remaining = clientCount_.fetch_sub(1, std::memory_order_relaxed) - 1;
    Log::info("WS", "Client disconnected (remaining=" + juce::String(remaining) + ")");
}

namespace {

bool readIntParameter(juce::DynamicObject* params,
                      const juce::Identifier& name,
                      int defaultValue,
                      int minimum,
                      int maximum,
                      int& result)
{
    if (!params || !params->hasProperty(name)) {
        result = defaultValue;
        return true;
    }

    const auto value = params->getProperty(name);
    if (!value.isInt() && !value.isInt64())
        return false;

    const auto integer = static_cast<juce::int64>(value);
    if (integer < minimum || integer > maximum)
        return false;

    result = static_cast<int>(integer);
    return true;
}

bool readFloatParameter(juce::DynamicObject* params,
                        const juce::Identifier& name,
                        float defaultValue,
                        double minimum,
                        double maximum,
                        float& result)
{
    if (!params || !params->hasProperty(name)) {
        result = defaultValue;
        return true;
    }

    const auto value = params->getProperty(name);
    if (!value.isDouble() && !value.isInt() && !value.isInt64())
        return false;

    const auto number = static_cast<double>(value);
    if (!std::isfinite(number) || number < minimum || number > maximum)
        return false;

    result = static_cast<float>(number);
    return true;
}

bool readBoolParameter(juce::DynamicObject* params,
                       const juce::Identifier& name,
                       bool defaultValue,
                       bool& result)
{
    if (!params || !params->hasProperty(name)) {
        result = defaultValue;
        return true;
    }

    const auto value = params->getProperty(name);
    if (!value.isBool())
        return false;

    result = static_cast<bool>(value);
    return true;
}

bool readStringParameter(juce::DynamicObject* params,
                         const juce::Identifier& name,
                         const juce::String& defaultValue,
                         juce::String& result)
{
    if (!params || !params->hasProperty(name)) {
        result = defaultValue;
        return true;
    }

    const auto value = params->getProperty(name);
    if (!value.isString())
        return false;

    result = value.toString();
    return result.length() <= 64;
}

} // namespace

void WebSocketServer::processMessage(const std::string& message)
{
    auto parsed = juce::JSON::parse(juce::String(message));
    if (!parsed.isObject()) return;

    auto* obj = parsed.getDynamicObject();
    if (!obj
        || !obj->getProperty("type").isString()
        || obj->getProperty("type").toString() != "action"
        || !obj->getProperty("action").isString()) {
        return;
    }

    const auto actionStr = obj->getProperty("action").toString();
    Log::info("WS", "Command: " + actionStr);
    Log::audit("WS", "Message payload: " + juce::String(message));

    const auto paramsValue = obj->getProperty("params");
    if (obj->hasProperty("params") && !paramsValue.isVoid() && !paramsValue.isObject()) {
        Log::warn("WS", "Rejected non-object params for action: " + actionStr);
        return;
    }
    auto* params = paramsValue.getDynamicObject();

    const auto rejectParameters = [&actionStr] {
        Log::warn("WS", "Rejected invalid parameters for action: " + actionStr);
    };

    ActionEvent event;

    if (actionStr == "plugin_bypass") {
        event.action = Action::PluginBypass;
        if (!readIntParameter(params, "index", 0, 0,
                              (std::numeric_limits<int>::max)(), event.intParam)) {
            rejectParameters();
            return;
        }
    } else if (actionStr == "master_bypass") {
        event.action = Action::MasterBypass;
    } else if (actionStr == "set_volume") {
        event.action = Action::SetVolume;
        juce::String target;
        if (!params || !params->hasProperty("value")
            || !readStringParameter(params, "target", "monitor", target)
            || (target != "monitor" && target != "input" && target != "output")) {
            rejectParameters();
            return;
        }
        const double maximum = target == "input" ? 2.0 : 1.0;
        if (!readFloatParameter(params, "value", 1.0f, 0.0, maximum, event.floatParam)) {
            rejectParameters();
            return;
        }
        event.stringParam = target.toStdString();
    } else if (actionStr == "toggle_mute") {
        juce::String target;
        if (!readStringParameter(params, "target", {}, target)
            || (target.isNotEmpty() && target != "all" && target != "monitor"
                && target != "output" && target != "input")) {
            rejectParameters();
            return;
        }
        if (target == "input") {
            event.action = Action::InputMuteToggle;
        } else {
            event.action = Action::ToggleMute;
            event.stringParam = target.toStdString();
        }
    } else if (actionStr == "load_preset") {
        event.action = Action::LoadPreset;
        if (!params || !params->hasProperty("index")
            || !readIntParameter(params, "index", 0, 0, 4, event.intParam)) {
            rejectParameters();
            return;
        }
    } else if (actionStr == "panic_mute") {
        event.action = Action::PanicMute;
        // Optional explicit-set mode for idempotent clients.
        if (params && params->hasProperty("muted")) {
            bool muted = false;
            if (!readBoolParameter(params, "muted", false, muted)) {
                rejectParameters();
                return;
            }
            event.stringParam = "set";
            event.intParam = muted ? 1 : 0;
        }
    } else if (actionStr == "input_gain") {
        event.action = Action::InputGainAdjust;
        if (!readFloatParameter(params, "delta", 1.0f, -20.0, 20.0, event.floatParam)) {
            rejectParameters();
            return;
        }
    } else if (actionStr == "switch_preset_slot") {
        event.action = Action::SwitchPresetSlot;
        if (!params || !params->hasProperty("slot")
            || !readIntParameter(params, "slot", 0, 0, 4, event.intParam)) {
            rejectParameters();
            return;
        }
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
        if (!params
            || !params->hasProperty("pluginIndex")
            || !params->hasProperty("paramIndex")
            || !params->hasProperty("value")
            || !readIntParameter(params, "pluginIndex", 0, 0,
                              (std::numeric_limits<int>::max)(), event.intParam)
            || !readIntParameter(params, "paramIndex", 0, 0,
                                 (std::numeric_limits<int>::max)(), event.intParam2)
            || !readFloatParameter(params, "value", 0.0f, 0.0, 1.0, event.floatParam)) {
            rejectParameters();
            return;
        }
    } else if (actionStr == "xrun_reset") {
        event.action = Action::XRunReset;
    } else if (actionStr == "safety_limiter_toggle") {
        event.action = Action::SafetyLimiterToggle;
    } else if (actionStr == "set_safety_limiter_ceiling") {
        event.action = Action::SetSafetyLimiterCeiling;
        if (!params || !params->hasProperty("value")
            || !readFloatParameter(params, "value", -0.3f, -6.0, 0.0, event.floatParam)) {
            rejectParameters();
            return;
        }
    } else if (actionStr == "auto_processors_add") {
        event.action = Action::AutoProcessorsAdd;
    } else {
        return;  // Unknown action
    }

    dispatcher_.dispatch(event);
}

void WebSocketServer::broadcastToClients(const std::string& /*message*/)
{
    // WARNING: 죽은 클라이언트의 thread.join()은 반드시 clientsMutex_ 바깥에서 실행
    // (교착 방지: clientThread가 clientsMutex_ 잡으려 할 수 있음)

    sweepFinishedClients();

    std::vector<std::shared_ptr<ClientConnection>> liveSnapshot;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& client : clients_) {
            if (client
                && client->readyForBroadcast.load(std::memory_order_acquire)
                && !client->threadFinished.load(std::memory_order_acquire)
                && client->socket
                && client->socket->isConnected()) {
                liveSnapshot.push_back(client);
            }
        }
    }
    // clientsMutex_ released — socket writes cannot block other operations

    for (const auto& conn : liveSnapshot) {
        std::lock_guard<std::mutex> sl(conn->sendMutex);
        // The queued payload can predate a just-completed client activation.
        // Snapshot after acquiring sendMutex so a delayed worker can never
        // overwrite the catch-up frame with older state.
        const auto latestState = broadcaster_.toJSON();
        if (!sendFrame(conn->socket.get(), latestState)) {
            // Write failed — close socket so next sweep removes it
            conn->requestSocketClose();
        }
    }
}

void WebSocketServer::sweepFinishedClients()
{
    std::vector<std::shared_ptr<ClientConnection>> finished;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            if (*it && (*it)->threadFinished.load(std::memory_order_acquire)) {
                finished.push_back(std::move(*it));
                it = clients_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // A finished flag is published at function exit. join() still provides the
    // definitive lifetime boundary before the connection object is reclaimed.
    for (auto& connection : finished) {
        if (connection && connection->thread.joinable())
            connection->thread.join();
    }
}

void WebSocketServer::sendDiscoveryBroadcast(bool logEvent)
{
    juce::DatagramSocket udp;
    if (udp.bindToPort(0)) {
        std::string msg = "DIRECTPIPE_READY:" + std::to_string(port_);
        udp.write("127.0.0.1", 8767, msg.c_str(), static_cast<int>(msg.size()));
        if (logEvent)
            Log::info("WS", "UDP discovery broadcast (port " + juce::String(port_) + ")");
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
