/**
 * @file WebSocketServer.h
 * @brief WebSocket server for Stream Deck integration
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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace directpipe {

/**
 * @brief Lightweight WebSocket server for Stream Deck and external clients.
 *
 * Protocol:
 * - Client sends JSON action requests
 * - Server pushes JSON state updates on change
 * - Default port: 8765
 *
 * Uses JUCE StreamingSocket for basic WebSocket implementation.
 */
class WebSocketServer : public StateListener {
public:
    WebSocketServer(ActionDispatcher& dispatcher, StateBroadcaster& broadcaster);
    ~WebSocketServer() override;

    /**
     * @brief Start the WebSocket server.
     * @param port Port to listen on (default 8765).
     * @return true if started successfully.
     */
    bool start(int port = 8765);

    /**
     * @brief Stop the server and disconnect all clients.
     */
    void stop();

    /**
     * @brief Check if the server is running.
     */
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the number of connected clients.
     */
    int getClientCount() const { return clientCount_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the port the server is listening on.
     */
    int getPort() const { return port_; }

    // StateListener
    void onStateChanged(const AppState& state) override;

private:
    void serverThread();
    void clientThread(juce::StreamingSocket* client);
    void processMessage(const std::string& message);
    void broadcastToClients(const std::string& message);

    ActionDispatcher& dispatcher_;
    StateBroadcaster& broadcaster_;

    std::unique_ptr<juce::StreamingSocket> serverSocket_;
    std::thread serverThread_;
    std::atomic<bool> running_{false};
    std::atomic<int> clientCount_{0};
    int port_ = 8765;

    // Connected client sockets
    struct ClientConnection {
        std::unique_ptr<juce::StreamingSocket> socket;
        std::thread thread;
    };
    std::mutex clientsMutex_;
    std::vector<std::unique_ptr<ClientConnection>> clients_;
};

} // namespace directpipe
