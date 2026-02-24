/**
 * @file plugin.js
 * @brief DirectPipe Stream Deck plugin entry point (@elgato/streamdeck SDK).
 */

const streamDeck = require("@elgato/streamdeck").default;
const { DirectPipeClient } = require("./websocket-client");
const { BypassToggleAction } = require("./actions/bypass-toggle");
const { PanicMuteAction } = require("./actions/panic-mute");
const { VolumeControlAction } = require("./actions/volume-control");
const { PresetSwitchAction } = require("./actions/preset-switch");

// ─── DirectPipe host connection ─────────────────────────────────────
const DIRECTPIPE_WS_URL = "ws://localhost:8765";
const dpClient = new DirectPipeClient(DIRECTPIPE_WS_URL);
let currentState = null;

// ─── Action instances ───────────────────────────────────────────────
const bypassAction = new BypassToggleAction();
const panicAction = new PanicMuteAction();
const volumeAction = new VolumeControlAction();
const presetAction = new PresetSwitchAction();

const allActions = [bypassAction, panicAction, volumeAction, presetAction];

// ─── DirectPipe state broadcasting ──────────────────────────────────
function broadcastState(state) {
    if (!state) return;
    for (const action of allActions) {
        if (typeof action.updateAllFromState === "function") {
            action.updateAllFromState(state);
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

streamDeck.connect();
dpClient.connect();

// Exports for action modules to access
module.exports = { dpClient, getCurrentState: () => currentState };
