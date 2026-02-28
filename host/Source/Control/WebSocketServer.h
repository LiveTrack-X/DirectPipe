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
 * @file WebSocketServer.h
 * @brief RFC 6455 WebSocket server for Stream Deck integration
 *
 * Provides a JSON-based WebSocket API for external control.
 * Stream Deck plugins connect to this server to send actions
 * and receive real-time state updates.
 */
#pragma once

#include <JuceHeader.h>
#include "ActionDispatcher.h"
#include "StateBroadcaster.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace directpipe {

/**
 * @brief RFC 6455 WebSocket server for Stream Deck and external clients.
 *
 * Protocol:
 * - HTTP Upgrade handshake (RFC 6455 Section 4)
 * - Client sends JSON action requests in WebSocket text frames
 * - Server pushes JSON state updates on change
 * - Default port: 8765
 */
class WebSocketServer : public StateListener {
public:
    WebSocketServer(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster);
    ~WebSocketServer() override;

    bool start(int port = 8765);
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    int getClientCount() const { return clientCount_.load(std::memory_order_relaxed); }
    int getPort() const { return port_; }

    // StateListener
    void onStateChanged(const AppState& state) override;

private:
    void serverThread();
    void clientThread(juce::StreamingSocket* client);
    void processMessage(const std::string& message);
    void broadcastToClients(const std::string& message);
    void sendDiscoveryBroadcast();

    // RFC 6455 WebSocket helpers
    static bool performHandshake(juce::StreamingSocket* client);
    static void sendFrame(juce::StreamingSocket* client, const std::string& payload, uint8_t opcode = 0x1);
    static std::string readFrame(juce::StreamingSocket* client, uint8_t& opcodeOut);

    // SHA-1 for WebSocket handshake (RFC 6455 requires it)
    static std::string sha1(const std::string& input);
    static std::string base64Encode(const uint8_t* data, size_t len);

    ActionDispatcher& dispatcher_;
    StateBroadcaster& broadcaster_;

    std::unique_ptr<juce::StreamingSocket> serverSocket_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};
    std::atomic<int> clientCount_{0};
    int port_ = 8765;

    struct ClientConnection {
        std::unique_ptr<juce::StreamingSocket> socket;
        std::thread thread;
    };
    std::mutex clientsMutex_;
    std::vector<std::unique_ptr<ClientConnection>> clients_;

    // Async broadcast: queue JSON on any thread, send from dedicated thread
    std::thread broadcastThread_;
    std::mutex broadcastMutex_;
    std::condition_variable broadcastCV_;
    std::string pendingBroadcast_;
    bool hasPendingBroadcast_ = false;
    void broadcastThreadFunc();
};

} // namespace directpipe
