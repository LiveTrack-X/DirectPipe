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
 * @file ControlManager.h
 * @brief Top-level manager for all external control inputs
 *
 * Orchestrates the initialization, lifecycle, and configuration of
 * all control handlers: Hotkeys, MIDI, WebSocket, and HTTP.
 */
#pragma once

#include <JuceHeader.h>
#include "ActionDispatcher.h"
#include "StateBroadcaster.h"
#include "HotkeyHandler.h"
#include "MidiHandler.h"
#include "WebSocketServer.h"
#include "HttpApiServer.h"
#include "ControlMapping.h"

#include <memory>

namespace directpipe {

/**
 * @brief Owns and manages all external control subsystems.
 *
 * Lifecycle:
 * 1. Construct with references to dispatcher and broadcaster
 * 2. Call initialize() to load config and start handlers
 * 3. Call shutdown() to stop everything
 */
class ControlManager {
public:
    ControlManager(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster);
    ~ControlManager();

    /**
     * @brief Initialize all control handlers.
     * Loads configuration from disk and starts all enabled handlers.
     */
    void initialize();

    /**
     * @brief Shut down all control handlers.
     */
    void shutdown();

    /**
     * @brief Reload configuration from disk.
     */
    void reloadConfig();

    /**
     * @brief Save current configuration to disk.
     */
    void saveConfig();

    // ─── Access to individual handlers ───

    HotkeyHandler& getHotkeyHandler() { return hotkeyHandler_; }
    MidiHandler& getMidiHandler() { return midiHandler_; }
    WebSocketServer& getWebSocketServer() { return *webSocketServer_; }
    HttpApiServer& getHttpApiServer() { return *httpApiServer_; }

    /**
     * @brief Get the current control configuration.
     */
    ControlConfig getConfig() const { return currentConfig_; }

    /**
     * @brief Apply a new control configuration.
     */
    void applyConfig(const ControlConfig& config);

private:
    ActionDispatcher& dispatcher_;
    StateBroadcaster& broadcaster_;

    HotkeyHandler hotkeyHandler_;
    MidiHandler midiHandler_;
    std::unique_ptr<WebSocketServer> webSocketServer_;
    std::unique_ptr<HttpApiServer> httpApiServer_;

    ControlMappingStore configStore_;
    ControlConfig currentConfig_;

    bool initialized_ = false;
};

} // namespace directpipe
