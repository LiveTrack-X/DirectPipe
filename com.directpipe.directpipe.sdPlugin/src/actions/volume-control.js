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
 * @file volume-control.js
 * @brief Volume control action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const TARGET_NAMES = { input: "Input", output: "Output", monitor: "Monitor" };

// Input gain: 0.0-2.0 (multiplier). Monitor/output: 0.0-1.0 (percentage).
const TARGET_MAX = { input: 2.0, output: 1.0, monitor: 1.0 };

// Throttle interval for dial rotation WebSocket sends (ms)
const DIAL_SEND_THROTTLE_MS = 50;
// How long to suppress incoming state broadcasts after local dial/key change (ms)
const LOCAL_OVERRIDE_MS = 300;

class VolumeControlAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.volume-control";

    /** @type {NodeJS.Timeout|null} */
    _dialSendTimer = null;
    /** @type {{target: string, value: number}|null} */
    _pendingDialValue = null;
    /** @type {Map<string, number>} target -> timestamp of last local override */
    _localOverrideUntil = new Map();
    /** @type {Map<string, object>} action.id -> cached settings (avoid async getSettings) */
    _settingsCache = new Map();

    onKeyDown(ev) {
        const { dpClient, getCurrentState } = require("../plugin");
        const settings = ev.payload.settings ?? {};
        const target = settings.target || "monitor";
        const mode = settings.mode || "mute";

        if (mode === "volume_up" || mode === "volume_down") {
            const step = (settings.step || 5) / 100;
            const delta = mode === "volume_up" ? step : -step;
            const state = getCurrentState();
            const current = state?.data?.volumes?.[target] ?? 1.0;
            const max = TARGET_MAX[target] || 1.0;
            const newValue = Math.max(0, Math.min(max, current + delta));
            dpClient.sendAction("set_volume", { target, value: newValue });
            // Instant local update
            this._applyLocal(ev.action, settings, target, newValue);
        } else {
            dpClient.sendAction("toggle_mute", { target });
        }
    }

    onDialDown(ev) {
        const { dpClient } = require("../plugin");
        const target = ev.payload.settings?.target || "monitor";
        dpClient.sendAction("toggle_mute", { target });
    }

    onDialRotate(ev) {
        const { dpClient, getCurrentState } = require("../plugin");
        const settings = ev.payload.settings ?? {};
        const target = settings.target || "monitor";
        const ticks = ev.payload.ticks || 0;
        const delta = ticks * 0.05;

        const state = getCurrentState();
        const volumes = state?.data?.volumes;
        const current = volumes?.[target] ?? 1.0;
        const max = TARGET_MAX[target] || 1.0;
        const newValue = Math.max(0, Math.min(max, current + delta));

        // Instant local update — dial-only fast path (1 SDK call instead of 3)
        this._applyLocal(ev.action, settings, target, newValue, true);

        // Throttled WebSocket send
        this._pendingDialValue = { target, value: newValue };
        if (!this._dialSendTimer) {
            dpClient.sendAction("set_volume", { target, value: newValue });
            this._dialSendTimer = setTimeout(() => {
                this._dialSendTimer = null;
                if (this._pendingDialValue) {
                    dpClient.sendAction("set_volume", this._pendingDialValue);
                    this._pendingDialValue = null;
                }
            }, DIAL_SEND_THROTTLE_MS);
        }
    }

    /**
     * Synchronous local state + display update. No async, no round-trip.
     * @param {boolean} [dialOnly=false] — If true, only update LCD feedback (skip setTitle/setState)
     */
    _applyLocal(action, settings, target, newValue, dialOnly = false) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state?.data?.volumes) {
            state.data.volumes[target] = newValue;
        }
        // Mark this target as locally overridden — suppress incoming broadcasts
        this._localOverrideUntil.set(target, Date.now() + LOCAL_OVERRIDE_MS);
        if (dialOnly && typeof action.setFeedback === "function") {
            // Fast path: only update dial LCD — minimal SDK calls
            const isInput = target === "input";
            const max = TARGET_MAX[target] || 1.0;
            const pct = Math.round((newValue / max) * 100);
            action.setFeedback({
                value: isInput ? `x${newValue.toFixed(2)}` : `${pct}%`,
                indicator: { value: pct },
            });
        } else {
            this._updateDisplay(action, settings, state);
        }
    }

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

    onWillDisappear(ev) {
        this._settingsCache.delete(ev.action.id);
    }

    /** Returns the local override map for plugin.js to preserve values during state replacement */
    getLocalOverrides() {
        return this._localOverrideUntil;
    }

    updateAllFromState(state) {
        const now = Date.now();
        for (const [, until] of this._localOverrideUntil) {
            if (now < until) return;
        }
        this._localOverrideUntil.clear();
        for (const action of this.actions) {
            const settings = this._settingsCache.get(action.id) ?? {};
            this._updateDisplay(action, settings, state);
        }
    }

    alertAll() {
        for (const action of this.actions) {
            action.showAlert();
        }
    }

    _updateDisplay(action, settings, state) {
        if (!state?.data) return;
        const target = settings?.target || "monitor";
        const mode = settings?.mode || "mute";
        const volumes = state.data.volumes;
        if (!volumes) return;

        const volume = volumes[target];
        const isMuted = state.data.muted === true ||
            (target === "output" && state.data.output_muted === true) ||
            (target === "monitor" && !state.data.monitor_enabled) ||
            (target === "input" && state.data.input_muted === true);
        const displayName = TARGET_NAMES[target] || target;
        const isInput = target === "input";

        const formatValue = (v) => {
            if (v === undefined) return "";
            return isInput ? `x${v.toFixed(2)}` : `${Math.round(v * 100)}%`;
        };

        let title;
        if (mode === "volume_up") {
            title = `${displayName}\n${formatValue(volume)} +`;
        } else if (mode === "volume_down") {
            title = `${displayName}\n- ${formatValue(volume)}`;
        } else if (isMuted) {
            title = `${displayName}\nMUTED`;
        } else if (volume !== undefined) {
            title = `${displayName}\n${formatValue(volume)}`;
        } else {
            title = displayName;
        }

        if (typeof action.setState === "function") {
            action.setState(isMuted ? 1 : 0);
        }
        action.setTitle(title);

        // Update dial LCD display (Encoder only)
        if (typeof action.setFeedback === "function") {
            const max = TARGET_MAX[target] || 1.0;
            const pct = volume !== undefined ? Math.round((volume / max) * 100) : 0;
            if (isMuted) {
                action.setFeedback({
                    title: displayName,
                    value: "MUTED",
                    indicator: { value: 0, enabled: false },
                });
            } else {
                action.setFeedback({
                    title: displayName,
                    value: isInput ? `x${(volume ?? 1).toFixed(2)}` : `${pct}%`,
                    indicator: { value: pct, enabled: true },
                });
            }
        }
    }
}

VolumeControlAction.UUID = "com.directpipe.directpipe.volume-control";

module.exports = { VolumeControlAction };
