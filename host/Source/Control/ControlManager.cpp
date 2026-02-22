/**
 * @file ControlManager.cpp
 * @brief Control manager implementation
 */

#include "ControlManager.h"

namespace directpipe {

ControlManager::ControlManager(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster)
    : dispatcher_(dispatcher),
      broadcaster_(broadcaster),
      hotkeyHandler_(dispatcher),
      midiHandler_(dispatcher)
{
    webSocketServer_ = std::make_unique<WebSocketServer>(dispatcher_, broadcaster_);
    httpApiServer_ = std::make_unique<HttpApiServer>(dispatcher_, broadcaster_);
}

ControlManager::~ControlManager()
{
    shutdown();
}

void ControlManager::initialize()
{
    if (initialized_) return;

    // Load configuration
    currentConfig_ = configStore_.load();

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

    initialized_ = true;
    juce::Logger::writeToLog("ControlManager initialized");
}

void ControlManager::shutdown()
{
    if (!initialized_) return;

    httpApiServer_->stop();
    webSocketServer_->stop();
    midiHandler_.shutdown();
    hotkeyHandler_.shutdown();

    initialized_ = false;
    juce::Logger::writeToLog("ControlManager shut down");
}

void ControlManager::reloadConfig()
{
    shutdown();
    currentConfig_ = configStore_.load();
    initialized_ = false;
    initialize();
}

void ControlManager::saveConfig()
{
    // Export current state from handlers
    currentConfig_.hotkeys = hotkeyHandler_.exportMappings();
    currentConfig_.midiMappings = midiHandler_.exportMappings();

    // Save server ports
    currentConfig_.server.websocketPort = webSocketServer_->getPort();
    currentConfig_.server.websocketEnabled = webSocketServer_->isRunning();
    currentConfig_.server.httpPort = httpApiServer_->getPort();
    currentConfig_.server.httpEnabled = httpApiServer_->isRunning();

    configStore_.save(currentConfig_);
}

void ControlManager::applyConfig(const ControlConfig& config)
{
    shutdown();
    currentConfig_ = config;
    configStore_.save(config);
    initialized_ = false;
    initialize();
}

} // namespace directpipe
