/**
 * @file plugin.js
 * @brief Main entry point for the DirectPipe Stream Deck plugin.
 *
 * Connects to the Elgato Stream Deck SDK via stdin/stdout and to the
 * DirectPipe host application via WebSocket (ws://localhost:8765).
 * Routes Stream Deck button events to the appropriate action handlers.
 */

const { DirectPipeClient } = require("./websocket-client");
const { BypassToggleAction } = require("./actions/bypass-toggle");
const { PanicMuteAction } = require("./actions/panic-mute");

// ─── Constants ──────────────────────────────────────────────────────
const DIRECTPIPE_WS_URL = "ws://localhost:8765";

// ─── Action UUIDs ───────────────────────────────────────────────────
const ACTION_BYPASS_TOGGLE = "com.directpipe.bypass-toggle";
const ACTION_VOLUME_CONTROL = "com.directpipe.volume-control";
const ACTION_PRESET_SWITCH = "com.directpipe.preset-switch";
const ACTION_PANIC_MUTE = "com.directpipe.panic-mute";

/**
 * DirectPipe Stream Deck Plugin
 *
 * Manages the lifecycle of the plugin: connects to the Stream Deck
 * WebSocket, creates action handlers, and routes events.
 */
class DirectPipePlugin {
    constructor() {
        /** @type {DirectPipeClient} */
        this.client = new DirectPipeClient(DIRECTPIPE_WS_URL);

        /** @type {object|null} Stream Deck SDK WebSocket */
        this.sdWebSocket = null;

        /** @type {string} Stream Deck plugin UUID */
        this.pluginUUID = "";

        /** @type {Map<string, object>} Registered action handlers keyed by UUID */
        this.actionHandlers = new Map();

        /** @type {Map<string, object>} Active action instances keyed by context */
        this.activeContexts = new Map();

        /** @type {object|null} Current DirectPipe state */
        this.currentState = null;

        this._registerActionHandlers();
        this._setupDirectPipeClient();
    }

    /**
     * Register all action handler classes.
     */
    _registerActionHandlers() {
        this.actionHandlers.set(
            ACTION_BYPASS_TOGGLE,
            new BypassToggleAction(this)
        );
        this.actionHandlers.set(ACTION_PANIC_MUTE, new PanicMuteAction(this));

        // Volume and preset actions use generic handlers for now
        this.actionHandlers.set(ACTION_VOLUME_CONTROL, {
            onKeyDown: (context, settings, payload) => {
                const target = (settings && settings.target) || "monitor";
                this.client.send({
                    type: "action",
                    action: "toggle_mute",
                    params: { target },
                });
            },
            onDialRotate: (context, settings, payload) => {
                const target = (settings && settings.target) || "monitor";
                const ticks = (payload && payload.ticks) || 0;
                const delta = ticks * 0.05;
                const current =
                    this._getVolumeForTarget(target) || 1.0;
                const newValue = Math.max(0, Math.min(1, current + delta));
                this.client.send({
                    type: "action",
                    action: "set_volume",
                    params: { target, value: newValue },
                });
            },
            onStateUpdate: (context, state) => {
                /* volume state reflected through title updates */
            },
        });

        this.actionHandlers.set(ACTION_PRESET_SWITCH, {
            onKeyDown: (context, settings, _payload) => {
                if (settings && settings.presetIndex !== undefined) {
                    this.client.send({
                        type: "action",
                        action: "load_preset",
                        params: { index: settings.presetIndex },
                    });
                } else {
                    this.client.send({
                        type: "action",
                        action: "next_preset",
                        params: {},
                    });
                }
            },
            onStateUpdate: (_context, _state) => {},
        });
    }

    /**
     * Set up DirectPipe WebSocket client event handlers.
     */
    _setupDirectPipeClient() {
        this.client.on("state", (state) => {
            this.currentState = state;
            this._broadcastStateToActions(state);
        });

        this.client.on("connected", () => {
            console.log("[DirectPipe] Connected to host application");
            this._updateAllContexts();
        });

        this.client.on("disconnected", () => {
            console.log("[DirectPipe] Disconnected from host application");
            this._setAllContextsAlert();
        });
    }

    /**
     * Connect to the Elgato Stream Deck SDK WebSocket.
     *
     * Called by the SDK with registration info when the plugin loads.
     *
     * @param {number} port - The port to connect to.
     * @param {string} pluginUUID - The unique identifier for this plugin instance.
     * @param {string} registerEvent - The event name for registration.
     * @param {string} info - JSON string with Stream Deck info.
     */
    connectToStreamDeck(port, pluginUUID, registerEvent, info) {
        this.pluginUUID = pluginUUID;

        const WebSocket = require("ws");
        this.sdWebSocket = new WebSocket(`ws://127.0.0.1:${port}`);

        this.sdWebSocket.on("open", () => {
            // Register the plugin with the Stream Deck
            const registerMsg = JSON.stringify({
                event: registerEvent,
                uuid: pluginUUID,
            });
            this.sdWebSocket.send(registerMsg);

            console.log("[StreamDeck] Plugin registered");

            // Connect to DirectPipe host
            this.client.connect();
        });

        this.sdWebSocket.on("message", (data) => {
            try {
                const message = JSON.parse(data.toString());
                this._handleStreamDeckEvent(message);
            } catch (err) {
                console.error("[StreamDeck] Failed to parse message:", err);
            }
        });

        this.sdWebSocket.on("close", () => {
            console.log("[StreamDeck] Connection closed");
            this.client.disconnect();
        });

        this.sdWebSocket.on("error", (err) => {
            console.error("[StreamDeck] WebSocket error:", err.message);
        });
    }

    /**
     * Handle incoming Stream Deck events and route to action handlers.
     *
     * @param {object} message - Parsed Stream Deck event object.
     */
    _handleStreamDeckEvent(message) {
        const { event, action, context, payload } = message;
        const settings = payload && payload.settings;

        switch (event) {
            case "keyDown": {
                const handler = this.actionHandlers.get(action);
                if (handler && handler.onKeyDown) {
                    handler.onKeyDown(context, settings, payload);
                }
                break;
            }

            case "keyUp": {
                const handler = this.actionHandlers.get(action);
                if (handler && handler.onKeyUp) {
                    handler.onKeyUp(context, settings, payload);
                }
                break;
            }

            case "dialRotate": {
                const handler = this.actionHandlers.get(action);
                if (handler && handler.onDialRotate) {
                    handler.onDialRotate(context, settings, payload);
                }
                break;
            }

            case "willAppear": {
                this.activeContexts.set(context, { action, settings });
                const handler = this.actionHandlers.get(action);
                if (handler && handler.onWillAppear) {
                    handler.onWillAppear(context, settings, payload);
                }
                // Push current state to new button
                if (this.currentState && handler && handler.onStateUpdate) {
                    handler.onStateUpdate(context, this.currentState);
                }
                break;
            }

            case "willDisappear": {
                this.activeContexts.delete(context);
                const handler = this.actionHandlers.get(action);
                if (handler && handler.onWillDisappear) {
                    handler.onWillDisappear(context, settings, payload);
                }
                break;
            }

            case "didReceiveSettings": {
                this.activeContexts.set(context, { action, settings });
                const handler = this.actionHandlers.get(action);
                if (handler && handler.onSettingsChanged) {
                    handler.onSettingsChanged(context, settings);
                }
                break;
            }

            default:
                break;
        }
    }

    /**
     * Send an event to the Stream Deck SDK.
     *
     * @param {string} event - The event name.
     * @param {string} context - The button context.
     * @param {object} [payload] - Optional event payload.
     */
    sendToStreamDeck(event, context, payload) {
        if (!this.sdWebSocket || this.sdWebSocket.readyState !== 1) {
            return;
        }

        const message = { event, context };
        if (payload !== undefined) {
            message.payload = payload;
        }

        this.sdWebSocket.send(JSON.stringify(message));
    }

    /**
     * Set the button title on a specific context.
     *
     * @param {string} context - The button context.
     * @param {string} title - The title text.
     */
    setTitle(context, title) {
        this.sendToStreamDeck("setTitle", context, { title, target: 0 });
    }

    /**
     * Set the button state (0 or 1) on a specific context.
     *
     * @param {string} context - The button context.
     * @param {number} state - The state index (0 or 1).
     */
    setState(context, state) {
        this.sendToStreamDeck("setState", context, { state });
    }

    /**
     * Show an alert indicator on a button.
     *
     * @param {string} context - The button context.
     */
    showAlert(context) {
        this.sendToStreamDeck("showAlert", context);
    }

    /**
     * Broadcast a DirectPipe state update to all active action instances.
     *
     * @param {object} state - The DirectPipe application state.
     */
    _broadcastStateToActions(state) {
        for (const [context, info] of this.activeContexts) {
            const handler = this.actionHandlers.get(info.action);
            if (handler && handler.onStateUpdate) {
                handler.onStateUpdate(context, state);
            }
        }
    }

    /**
     * Force-refresh all active button contexts with current state.
     */
    _updateAllContexts() {
        if (!this.currentState) return;
        this._broadcastStateToActions(this.currentState);
    }

    /**
     * Show alert on all active button contexts (e.g., on disconnect).
     */
    _setAllContextsAlert() {
        for (const [context] of this.activeContexts) {
            this.showAlert(context);
        }
    }

    /**
     * Get the current volume for a named target from cached state.
     *
     * @param {string} target - Volume target name (input, virtual_mic, monitor).
     * @returns {number|null} Volume value 0.0-1.0, or null if unavailable.
     */
    _getVolumeForTarget(target) {
        if (!this.currentState || !this.currentState.data) return null;
        const volumes = this.currentState.data.volumes;
        if (!volumes) return null;
        return volumes[target] !== undefined ? volumes[target] : null;
    }
}

// ─── Stream Deck SDK Entry Point ────────────────────────────────────

// The Stream Deck SDK passes connection parameters via command-line args:
//   -port <port> -pluginUUID <uuid> -registerEvent <event> -info <json>
function parseArgs(argv) {
    const args = {};
    for (let i = 2; i < argv.length; i += 2) {
        const key = argv[i].replace(/^-+/, "");
        const value = argv[i + 1];
        args[key] = value;
    }
    return args;
}

const args = parseArgs(process.argv);
const plugin = new DirectPipePlugin();

if (args.port && args.pluginUUID && args.registerEvent) {
    plugin.connectToStreamDeck(
        parseInt(args.port, 10),
        args.pluginUUID,
        args.registerEvent,
        args.info || "{}"
    );
} else {
    console.log(
        "[DirectPipe] Running in standalone mode (no Stream Deck connection)"
    );
    console.log("[DirectPipe] Connecting to DirectPipe host...");
    plugin.client.connect();
}

module.exports = { DirectPipePlugin };
