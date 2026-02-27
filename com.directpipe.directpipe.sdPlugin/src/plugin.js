/**
 * @file plugin.js
 * @brief DirectPipe Stream Deck plugin entry point (@elgato/streamdeck SDK).
 */

const streamDeck = require("@elgato/streamdeck").default;
const dgram = require("dgram");
const { DirectPipeClient } = require("./websocket-client");
const { BypassToggleAction } = require("./actions/bypass-toggle");
const { PanicMuteAction } = require("./actions/panic-mute");
const { VolumeControlAction } = require("./actions/volume-control");
const { PresetSwitchAction } = require("./actions/preset-switch");
const { MonitorToggleAction } = require("./actions/monitor-toggle");
const { RecordingToggleAction } = require("./actions/recording-toggle");

// ─── DirectPipe host connection ─────────────────────────────────────
const DIRECTPIPE_WS_URL = "ws://localhost:8765";
const dpClient = new DirectPipeClient(DIRECTPIPE_WS_URL, { autoReconnect: false });
let currentState = null;

// ─── Action instances ───────────────────────────────────────────────
const bypassAction = new BypassToggleAction();
const panicAction = new PanicMuteAction();
const volumeAction = new VolumeControlAction();
const presetAction = new PresetSwitchAction();
const monitorAction = new MonitorToggleAction();
const recordingAction = new RecordingToggleAction();

const allActions = [bypassAction, panicAction, volumeAction, presetAction, monitorAction, recordingAction];

// ─── DirectPipe state broadcasting ──────────────────────────────────
function broadcastState(state) {
    if (!state) return;
    for (const action of allActions) {
        if (typeof action.updateAllFromState === "function") {
            try {
                action.updateAllFromState(state);
            } catch (err) {
                streamDeck.logger.error(`Error updating ${action.manifestId}: ${err.message}`);
            }
        }
    }
}

function alertAll() {
    for (const action of allActions) {
        if (typeof action.alertAll === "function") {
            action.alertAll();
        }
    }
}

dpClient.on("state", (state) => {
    // Preserve locally-overridden volume values (dial/key optimistic updates)
    if (state?.data?.volumes && currentState?.data?.volumes) {
        const overrides = volumeAction.getLocalOverrides();
        for (const [target, until] of overrides) {
            if (Date.now() < until) {
                state.data.volumes[target] = currentState.data.volumes[target];
            }
        }
    }
    currentState = state;
    broadcastState(state);
});

dpClient.on("connected", () => {
    streamDeck.logger.info("Connected to DirectPipe host");
    broadcastState(currentState);
});

dpClient.on("disconnected", () => {
    streamDeck.logger.info("Disconnected from DirectPipe host");
    alertAll();
});

// ─── Register actions & connect ─────────────────────────────────────
streamDeck.actions.registerAction(bypassAction);
streamDeck.actions.registerAction(panicAction);
streamDeck.actions.registerAction(volumeAction);
streamDeck.actions.registerAction(presetAction);
streamDeck.actions.registerAction(monitorAction);
streamDeck.actions.registerAction(recordingAction);

streamDeck.connect();
dpClient.connect();

// ─── UDP discovery listener ─────────────────────────────────────────
// DirectPipe sends a UDP packet to port 8767 when its WebSocket server
// is ready. This lets us connect instantly instead of waiting for the
// next backoff retry.
const discoverySocket = dgram.createSocket("udp4");
discoverySocket.on("message", (msg) => {
    const text = msg.toString();
    if (text.startsWith("DIRECTPIPE_READY")) {
        streamDeck.logger.info(`UDP discovery received: ${text}`);
        dpClient.reconnectNow();
    }
});
discoverySocket.on("error", (err) => {
    streamDeck.logger.error(`UDP discovery error: ${err.message}`);
    discoverySocket.close();
});
discoverySocket.bind(8767, "127.0.0.1");

// Exports for action modules to access
module.exports = { dpClient, getCurrentState: () => currentState };
