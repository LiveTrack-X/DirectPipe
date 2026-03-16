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
 * @file plugin-param.js
 * @brief Plugin parameter control action (SD+ Encoder) — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const DIAL_SEND_THROTTLE_MS = 50;
const LOCAL_OVERRIDE_MS = 300;

class PluginParamAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.plugin-param";
    _settingsCache = new Map();
    _localOverrideUntil = new Map();
    _lastValues = new Map();
    _dialSendTimer = null;
    _pendingValue = null;

    onDialRotate(ev) {
        const { dpClient } = require("../plugin");
        const settings = ev.payload.settings ?? {};
        const pluginIndex = Number(settings.pluginIndex) || 0;
        const paramIndex = Number(settings.paramIndex) || 0;
        const ticks = ev.payload.ticks || 0;
        const delta = ticks * 0.02;

        const key = `${pluginIndex}_${paramIndex}`;
        let current = this._lastValues.get(key) ?? 0.5;
        const newValue = Math.max(0, Math.min(1.0, current + delta));

        this._lastValues.set(key, newValue);
        this._localOverrideUntil.set(key, Date.now() + LOCAL_OVERRIDE_MS);

        if (typeof ev.action.setFeedback === "function") {
            const pluginName = settings.pluginName || `Plugin ${pluginIndex + 1}`;
            const paramName = settings.paramName || `Param ${paramIndex}`;
            ev.action.setFeedback({
                title: pluginName,
                value: `${paramName}: ${Math.round(newValue * 100)}%`,
                indicator: { value: Math.round(newValue * 100), enabled: true },
            });
        }

        this._pendingValue = { pluginIndex, paramIndex, value: newValue };
        if (!this._dialSendTimer) {
            dpClient.sendAction("set_plugin_parameter", { pluginIndex, paramIndex, value: newValue });
            this._dialSendTimer = setTimeout(() => {
                this._dialSendTimer = null;
                if (this._pendingValue) {
                    dpClient.sendAction("set_plugin_parameter", this._pendingValue);
                    this._pendingValue = null;
                }
            }, DIAL_SEND_THROTTLE_MS);
        }
    }

    onDialDown(ev) {
        const { dpClient } = require("../plugin");
        const settings = ev.payload.settings ?? {};
        const pluginIndex = Number(settings.pluginIndex) || 0;
        const paramIndex = Number(settings.paramIndex) || 0;
        dpClient.sendAction("set_plugin_parameter", { pluginIndex, paramIndex, value: 0.5 });
        this._lastValues.set(`${pluginIndex}_${paramIndex}`, 0.5);
    }

    onWillAppear(ev) {
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        this._updateDisplay(ev.action, ev.payload.settings ?? {});
    }

    onDidReceiveSettings(ev) {
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        this._updateDisplay(ev.action, ev.payload.settings ?? {});
    }

    onWillDisappear(ev) { this._settingsCache.delete(ev.action.id); }

    updateAllFromState(_state) {
        for (const action of this.actions) {
            const settings = this._settingsCache.get(action.id) ?? {};
            this._updateDisplay(action, settings);
        }
    }

    setDisconnectedState() {
        for (const action of this.actions) {
            if (typeof action.setFeedback === "function")
                action.setFeedback({ title: "Disconnected", value: "", indicator: { enabled: false } });
        }
    }
    setConnectingState() {
        for (const action of this.actions) {
            if (typeof action.setFeedback === "function")
                action.setFeedback({ title: "Connecting...", value: "", indicator: { enabled: false } });
        }
    }
    alertAll() { for (const action of this.actions) action.showAlert(); }

    _updateDisplay(action, settings) {
        const pluginName = settings.pluginName || `Plugin ${(Number(settings.pluginIndex) || 0) + 1}`;
        const paramName = settings.paramName || "Select param";
        if (typeof action.setFeedback === "function") {
            action.setFeedback({
                title: pluginName,
                value: paramName,
                indicator: { value: 50, enabled: true },
            });
        }
    }
}

PluginParamAction.UUID = "com.directpipe.directpipe.plugin-param";
module.exports = { PluginParamAction };
