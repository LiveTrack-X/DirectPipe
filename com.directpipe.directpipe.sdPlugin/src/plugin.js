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
const { IpcToggleAction } = require("./actions/ipc-toggle");
const { PerformanceMonitorAction } = require("./actions/performance-monitor");
const { PluginParamAction } = require("./actions/plugin-param");
const { PresetBarAction } = require("./actions/preset-bar");

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
const ipcAction = new IpcToggleAction();
const perfAction = new PerformanceMonitorAction();
const pluginParamAction = new PluginParamAction();
const presetBarAction = new PresetBarAction();

const allActions = [bypassAction, panicAction, volumeAction, presetAction, monitorAction, recordingAction, ipcAction, perfAction, pluginParamAction, presetBarAction];

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
    for (const action of allActions) {
        if (typeof action.setDisconnectedState === "function") {
            action.setDisconnectedState();
        }
    }
});

dpClient.on("connecting", () => {
    streamDeck.logger.info("Connecting to DirectPipe host...");
    for (const action of allActions) {
        if (typeof action.setConnectingState === "function") {
            action.setConnectingState();
        }
    }
});

// ─── Register actions & connect ─────────────────────────────────────
streamDeck.actions.registerAction(bypassAction);
streamDeck.actions.registerAction(panicAction);
streamDeck.actions.registerAction(volumeAction);
streamDeck.actions.registerAction(presetAction);
streamDeck.actions.registerAction(monitorAction);
streamDeck.actions.registerAction(recordingAction);
streamDeck.actions.registerAction(ipcAction);
streamDeck.actions.registerAction(perfAction);
streamDeck.actions.registerAction(pluginParamAction);
streamDeck.actions.registerAction(presetBarAction);

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
