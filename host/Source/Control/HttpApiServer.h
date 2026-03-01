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
 * @file HttpApiServer.h
 * @brief Simple REST API server for universal control
 *
 * Provides HTTP GET endpoints for controlling DirectPipe.
 * Stream Deck's "Open Website" action can call these directly.
 */
#pragma once

#include <JuceHeader.h>
#include "ActionDispatcher.h"
#include "StateBroadcaster.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace directpipe {

/**
 * @brief Lightweight HTTP server providing REST API endpoints.
 *
 * Endpoints:
 * - GET /api/status                → Full state JSON
 * - GET /api/bypass/:index/toggle  → Toggle plugin bypass
 * - GET /api/bypass/master/toggle  → Toggle master bypass
 * - GET /api/mute/toggle           → Toggle mute
 * - GET /api/mute/panic            → Panic mute
 * - GET /api/volume/:target/:value → Set volume
 * - GET /api/preset/:index         → Load preset
 * - GET /api/gain/:delta           → Adjust input gain
 */
class HttpApiServer {
public:
    HttpApiServer(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster);
    ~HttpApiServer();

    /**
     * @brief Start the HTTP server.
     * @param port Port to listen on (default 8766).
     * @return true if started successfully.
     */
    bool start(int port = 8766);

    /**
     * @brief Stop the server.
     */
    void stop();

    /**
     * @brief Check if running.
     */
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the port.
     */
    int getPort() const { return port_; }

private:
    void serverThread();
    void handleClient(std::unique_ptr<juce::StreamingSocket> client);
    std::pair<int, std::string> processRequest(const std::string& method, std::string path);
    std::string makeResponse(int statusCode, const std::string& body);

    ActionDispatcher& dispatcher_;
    StateBroadcaster& broadcaster_;

    std::unique_ptr<juce::StreamingSocket> serverSocket_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};
    int port_ = 8766;

    // Lifetime guard for detached client handler threads
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

} // namespace directpipe
