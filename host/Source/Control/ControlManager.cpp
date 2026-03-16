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
 * @file ControlManager.cpp
 * @brief Control manager implementation
 */

#include "ControlManager.h"

namespace directpipe {

ControlManager::ControlManager(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster,
                               AudioEngine& engine)
    : dispatcher_(dispatcher),
      broadcaster_(broadcaster),
      engine_(engine),
      hotkeyHandler_(dispatcher),
      midiHandler_(dispatcher)
{
    webSocketServer_ = std::make_unique<WebSocketServer>(dispatcher_, broadcaster_);
    httpApiServer_ = std::make_unique<HttpApiServer>(dispatcher_, broadcaster_, engine_, &midiHandler_);
}

ControlManager::~ControlManager()
{
    shutdown();
}

void ControlManager::initialize(bool enableExternalControls)
{
    if (initialized_) return;

    // Load configuration
    currentConfig_ = configStore_.load();

    if (enableExternalControls) {
        // Initialize hotkey handler
        hotkeyHandler_.initialize();
        hotkeyHandler_.loadFromMappings(currentConfig_.hotkeys);

        // Initialize MIDI handler
        midiHandler_.initialize();
        midiHandler_.loadFromMappings(currentConfig_.midiMappings);

        // Start WebSocket server
        if (currentConfig_.server.websocketEnabled) {
            webSocketServer_->start(currentConfig_.server.websocketPort);
        }

        // Start HTTP API server
        if (currentConfig_.server.httpEnabled) {
            httpApiServer_->start(currentConfig_.server.httpPort);
        }
    }

    initialized_ = true;
    externalControlsActive_ = enableExternalControls;
    juce::Logger::writeToLog("[CONTROL] Initialized"
        + juce::String(enableExternalControls ? "" : " (audio-only mode — external controls disabled)"));
}

void ControlManager::shutdown()
{
    if (!initialized_) return;

    httpApiServer_->stop();
    webSocketServer_->stop();
    midiHandler_.shutdown();
    hotkeyHandler_.shutdown();

    initialized_ = false;
    juce::Logger::writeToLog("[CONTROL] Shut down");
}

void ControlManager::reloadConfig()
{
    bool wasActive = externalControlsActive_;
    shutdown();
    currentConfig_ = configStore_.load();
    initialized_ = false;
    initialize(wasActive);
}

void ControlManager::saveConfig()
{
    // Export current state from handlers
    currentConfig_.hotkeys = hotkeyHandler_.exportMappings();
    currentConfig_.midiMappings = midiHandler_.exportMappings();

    // Save server ports (keep user's enabled intent, not runtime state)
    currentConfig_.server.websocketPort = webSocketServer_->getPort();
    // Don't overwrite enabled flag with isRunning() — a transient port conflict
    // would permanently disable servers on next launch
    currentConfig_.server.httpPort = httpApiServer_->getPort();

    configStore_.save(currentConfig_);
}

void ControlManager::applyConfig(const ControlConfig& config)
{
    bool wasActive = externalControlsActive_;
    shutdown();
    currentConfig_ = config;
    configStore_.save(config);
    initialized_ = false;
    initialize(wasActive);
}

} // namespace directpipe
