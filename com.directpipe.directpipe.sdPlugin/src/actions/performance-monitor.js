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
 * @file performance-monitor.js
 * @brief Performance monitor action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

class PerformanceMonitorAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.performance-monitor";
    _settingsCache = new Map();

    onWillAppear(ev) {
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, ev.payload.settings ?? {}, state);
    }

    onDidReceiveSettings(ev) {
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, ev.payload.settings ?? {}, state);
    }

    onWillDisappear(ev) { this._settingsCache.delete(ev.action.id); }

    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        dpClient.sendAction("xrun_reset");
    }

    onDialDown(ev) {
        const { dpClient } = require("../plugin");
        dpClient.sendAction("xrun_reset");
    }

    updateAllFromState(state) {
        for (const action of this.actions) {
            const settings = this._settingsCache.get(action.id) ?? {};
            this._updateDisplay(action, settings, state);
        }
    }

    setDisconnectedState() {
        for (const action of this.actions) action.setTitle("Disconnected");
    }
    setConnectingState() {
        for (const action of this.actions) action.setTitle("Connecting...");
    }
    alertAll() { for (const action of this.actions) action.showAlert(); }

    _updateDisplay(action, settings, state) {
        if (!state?.data) return;
        const d = state.data;
        const show = settings.displayMode || "all";

        let title;
        if (show === "latency") {
            title = `${(d.latency_ms ?? 0).toFixed(1)}ms`;
        } else if (show === "cpu") {
            title = `CPU ${(d.cpu_percent ?? 0).toFixed(0)}%`;
        } else {
            title = `${(d.latency_ms ?? 0).toFixed(1)}ms\nCPU ${(d.cpu_percent ?? 0).toFixed(0)}%`;
            if (d.xrun_count > 0) title += `\nXR:${d.xrun_count}`;
        }
        action.setTitle(title);

        if (typeof action.setFeedback === "function") {
            action.setFeedback({
                title: `${d.sample_rate ?? 48000}Hz / ${d.buffer_size ?? 480}`,
                value: `${(d.latency_ms ?? 0).toFixed(1)}ms  CPU ${(d.cpu_percent ?? 0).toFixed(0)}%`,
                indicator: { value: Math.min(100, Math.round(d.cpu_percent ?? 0)), enabled: true },
            });
        }
    }
}

PerformanceMonitorAction.UUID = "com.directpipe.directpipe.performance-monitor";
module.exports = { PerformanceMonitorAction };
