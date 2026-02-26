/**
 * @file websocket-client.js
 * @brief WebSocket client for connecting to the DirectPipe host application.
 *
 * Maintains a persistent connection to the DirectPipe WebSocket server
 * (ws://localhost:8765). Sends JSON action requests and receives real-time
 * state updates. Automatically reconnects on disconnect with exponential
 * backoff.
 */

const WebSocket = require("ws");
const { EventEmitter } = require("events");

// ─── Constants ──────────────────────────────────────────────────────
const DEFAULT_RECONNECT_INTERVAL_MS = 2000;
const MAX_RECONNECT_INTERVAL_MS = 10000;
const RECONNECT_BACKOFF_FACTOR = 1.5;
const PING_INTERVAL_MS = 15000;

/**
 * WebSocket client that connects to the DirectPipe host application.
 *
 * Events emitted:
 *   - "connected"       — Connection established
 *   - "disconnected"    — Connection lost
 *   - "state"           — State update received (payload: state object)
 *   - "error"           — WebSocket error (payload: Error)
 *   - "message"         — Raw message received (payload: parsed JSON)
 *
 * @extends EventEmitter
 */
class DirectPipeClient extends EventEmitter {
    /**
     * @param {string} url - WebSocket URL to connect to (e.g., "ws://localhost:8765").
     * @param {object} [options] - Optional configuration.
     * @param {boolean} [options.autoReconnect=true] - Automatically reconnect on disconnect.
     * @param {number} [options.reconnectInterval=2000] - Initial reconnect delay in ms.
     * @param {number} [options.maxReconnectInterval=30000] - Maximum reconnect delay in ms.
     */
    constructor(url, options = {}) {
        super();

        /** @type {string} */
        this.url = url;

        /** @type {boolean} */
        this.autoReconnect =
            options.autoReconnect !== undefined ? options.autoReconnect : true;

        /** @type {number} */
        this.reconnectInterval =
            options.reconnectInterval || DEFAULT_RECONNECT_INTERVAL_MS;

        /** @type {number} */
        this.maxReconnectInterval =
            options.maxReconnectInterval || MAX_RECONNECT_INTERVAL_MS;

        /** @type {WebSocket|null} */
        this._ws = null;

        /** @type {NodeJS.Timeout|null} */
        this._reconnectTimer = null;

        /** @type {NodeJS.Timeout|null} */
        this._pingTimer = null;

        /** @type {number} Current reconnect delay (increases with backoff) */
        this._currentReconnectDelay = this.reconnectInterval;

        /** @type {boolean} Whether the client has been intentionally disconnected */
        this._intentionalClose = false;

        /** @type {number} Number of reconnection attempts since last successful connection */
        this._reconnectAttempts = 0;

        /** @type {boolean} Whether the client is currently connected */
        this._connected = false;

        /** @type {Array<object>} Queue of messages to send once connected */
        this._pendingMessages = [];
    }

    /**
     * Connect to the DirectPipe WebSocket server.
     *
     * If already connected, this is a no-op. Safe to call multiple times.
     */
    connect() {
        if (this._ws && this._ws.readyState === WebSocket.OPEN) {
            return;
        }

        this._intentionalClose = false;
        this._createConnection();
    }

    /**
     * Disconnect from the DirectPipe WebSocket server.
     *
     * Stops auto-reconnect. Call connect() again to re-establish.
     */
    disconnect() {
        this._intentionalClose = true;
        this._stopReconnectTimer();
        this._stopPingTimer();

        if (this._ws) {
            this._ws.close(1000, "Client disconnect");
            this._ws = null;
        }

        if (this._connected) {
            this._connected = false;
            this.emit("disconnected");
        }
    }

    /**
     * Send a JSON message to the DirectPipe host.
     *
     * If not currently connected, the message is queued and sent
     * once the connection is established.
     *
     * @param {object} message - The message object to send.
     * @returns {boolean} true if sent immediately, false if queued.
     */
    send(message) {
        if (!message || typeof message !== "object") {
            return false;
        }

        const json = JSON.stringify(message);

        if (this._ws && this._ws.readyState === WebSocket.OPEN) {
            this._ws.send(json);
            return true;
        }

        // Queue message for when connection is restored (cap at 50 to prevent memory growth)
        this._pendingMessages.push(json);
        if (this._pendingMessages.length > 50) {
            this._pendingMessages.shift(); // drop oldest
        }

        // User interaction while disconnected — try to reconnect immediately
        this.reconnectNow();

        return false;
    }

    /**
     * Send an action request to the DirectPipe host.
     *
     * Convenience wrapper that builds the standard action message format.
     *
     * @param {string} action - The action name (e.g., "plugin_bypass", "panic_mute").
     * @param {object} [params={}] - Action parameters.
     * @returns {boolean} true if sent immediately, false if queued.
     */
    sendAction(action, params = {}) {
        return this.send({
            type: "action",
            action: action,
            params: params,
        });
    }

    /**
     * Check if the client is currently connected.
     *
     * @returns {boolean} true if the WebSocket connection is open.
     */
    isConnected() {
        return this._connected && this._ws !== null && this._ws.readyState === WebSocket.OPEN;
    }

    /**
     * Get the number of reconnection attempts since the last successful connection.
     *
     * @returns {number}
     */
    getReconnectAttempts() {
        return this._reconnectAttempts;
    }

    /**
     * Immediately attempt reconnection, resetting the backoff delay.
     *
     * Useful when an external signal (e.g., UDP discovery or user interaction)
     * indicates the server is likely available now.
     */
    reconnectNow() {
        if (this._connected) return;
        // Avoid duplicate connections if already attempting
        if (this._ws && this._ws.readyState === WebSocket.CONNECTING) return;

        this._stopReconnectTimer();
        this._currentReconnectDelay = this.reconnectInterval;
        this._createConnection();
    }

    // ─── Internal Methods ───────────────────────────────────────────

    /**
     * Create a new WebSocket connection.
     * @private
     */
    _createConnection() {
        this._stopReconnectTimer();

        try {
            this._ws = new WebSocket(this.url);
        } catch (err) {
            console.error("[DirectPipeClient] Failed to create WebSocket:", err.message);
            this._scheduleReconnect();
            return;
        }

        this._ws.on("open", () => {
            this._connected = true;
            this._reconnectAttempts = 0;
            this._currentReconnectDelay = this.reconnectInterval;

            console.log(`[DirectPipeClient] Connected to ${this.url}`);
            this.emit("connected");

            // Flush pending messages
            this._flushPendingMessages();

            // Start keepalive pings
            this._startPingTimer();
        });

        this._ws.on("message", (data) => {
            this._handleMessage(data);
        });

        this._ws.on("close", (code, reason) => {
            const wasConnected = this._connected;
            this._connected = false;
            this._stopPingTimer();

            const reasonStr = reason ? reason.toString() : "unknown";
            console.log(
                `[DirectPipeClient] Connection closed (code=${code}, reason=${reasonStr})`
            );

            if (wasConnected) {
                this.emit("disconnected");
            }

            if (!this._intentionalClose && this.autoReconnect) {
                this._scheduleReconnect();
            }
        });

        this._ws.on("error", (err) => {
            console.error("[DirectPipeClient] WebSocket error:", err.message);
            this.emit("error", err);

            // The "close" event will fire after "error", which triggers reconnect
        });

        this._ws.on("pong", () => {
            // Server responded to ping — connection is alive
        });
    }

    /**
     * Handle an incoming WebSocket message.
     * @param {Buffer|string} data - Raw WebSocket message data.
     * @private
     */
    _handleMessage(data) {
        let parsed;
        try {
            parsed = JSON.parse(data.toString());
        } catch (err) {
            console.error("[DirectPipeClient] Failed to parse message:", err.message);
            return;
        }

        // Emit raw message event
        this.emit("message", parsed);

        // Route by message type
        if (parsed.type === "state" && parsed.data) {
            this.emit("state", parsed);
        }
    }

    /**
     * Flush queued messages that were sent while disconnected.
     * @private
     */
    _flushPendingMessages() {
        if (this._pendingMessages.length === 0) return;

        const pending = this._pendingMessages.splice(0);
        for (const json of pending) {
            if (this._ws && this._ws.readyState === WebSocket.OPEN) {
                this._ws.send(json);
            }
        }
    }

    /**
     * Schedule a reconnection attempt with exponential backoff.
     * @private
     */
    _scheduleReconnect() {
        if (this._intentionalClose || !this.autoReconnect) return;

        this._reconnectAttempts++;

        console.log(
            `[DirectPipeClient] Reconnecting in ${this._currentReconnectDelay}ms ` +
            `(attempt #${this._reconnectAttempts})`
        );

        this._reconnectTimer = setTimeout(() => {
            this._reconnectTimer = null;
            this._createConnection();
        }, this._currentReconnectDelay);

        // Exponential backoff
        this._currentReconnectDelay = Math.min(
            this._currentReconnectDelay * RECONNECT_BACKOFF_FACTOR,
            this.maxReconnectInterval
        );
    }

    /**
     * Stop any pending reconnection timer.
     * @private
     */
    _stopReconnectTimer() {
        if (this._reconnectTimer) {
            clearTimeout(this._reconnectTimer);
            this._reconnectTimer = null;
        }
    }

    /**
     * Start the keepalive ping timer.
     * @private
     */
    _startPingTimer() {
        this._stopPingTimer();
        this._pingTimer = setInterval(() => {
            if (this._ws && this._ws.readyState === WebSocket.OPEN) {
                this._ws.ping();
            }
        }, PING_INTERVAL_MS);
    }

    /**
     * Stop the keepalive ping timer.
     * @private
     */
    _stopPingTimer() {
        if (this._pingTimer) {
            clearInterval(this._pingTimer);
            this._pingTimer = null;
        }
    }
}

module.exports = { DirectPipeClient };
