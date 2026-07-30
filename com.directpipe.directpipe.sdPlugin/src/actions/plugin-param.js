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
const { RenderCache } = require("./render-cache");
const { getPluginParams } = require("../utils/host-api");

const DIAL_SEND_THROTTLE_MS = 50;
const LOCAL_OVERRIDE_MS = 300;

class PluginParamAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.plugin-param";
    _settingsCache = new Map();
    _localOverrideUntil = new Map();
    _lastValues = new Map();
    _defaultValues = new Map();
    _dialSendTimers = new Map();
    _pendingValues = new Map();
    _refreshTokens = new Map();
    _rotationQueues = new Map();
    _actionGenerations = new Map();
    _connectionGeneration = 0;
    _lastStateSignature = null;
    _renderCache = new RenderCache();

    onDialRotate(ev) {
        const settings = this._resolveSettings(ev);
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const ticks = ev.payload.ticks || 0;
        const sendKey = this._sendKey(ev.action.id, pluginIndex, paramIndex);
        const actionGeneration = this._actionGeneration(ev.action.id);
        const connectionGeneration = this._connectionGeneration;
        const previous = this._rotationQueues.get(sendKey) ?? Promise.resolve();
        const next = previous
            .catch(() => {})
            .then(() => this._rotateFromCurrentHostValue(
                ev.action, settings, ticks, sendKey,
                actionGeneration, connectionGeneration));
        this._rotationQueues.set(sendKey, next);
        return next.finally(() => {
            if (this._rotationQueues.get(sendKey) === next)
                this._rotationQueues.delete(sendKey);
        });
    }

    async _rotateFromCurrentHostValue(
        action, settings, ticks, sendKey,
        actionGeneration, connectionGeneration) {
        const { dpClient } = require("../plugin");
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const parameterKey = this._parameterKey(pluginIndex, paramIndex);

        if (!this._isTaskCurrent(
            action.id, actionGeneration, connectionGeneration)) return;

        // Rebase the first turn after an idle period on the current host value.
        // A plug-in editor, preset automation, or another controller may have
        // changed the parameter since the last Stream Deck refresh. Subsequent
        // turns in the same gesture use the local override so every tick is
        // accumulated without another HTTP round trip.
        const overrideUntil = this._localOverrideUntil.get(parameterKey) ?? 0;
        if (Date.now() >= overrideUntil)
            await this._refreshParameter(action, settings, true);

        if (!this._isTaskCurrent(
            action.id, actionGeneration, connectionGeneration)) return;

        const current = this._lastValues.get(parameterKey);
        if (!Number.isFinite(current)) {
            if (typeof action.showAlert === "function") action.showAlert();
            return;
        }

        const delta = ticks * 0.02;
        const newValue = Math.max(0, Math.min(1.0, current + delta));

        this._applyLocalValue(action, settings, newValue);

        if (!this._dialSendTimers.has(sendKey)) {
            dpClient.sendAction("set_plugin_parameter", { pluginIndex, paramIndex, value: newValue });
            const timer = setTimeout(() => {
                this._dialSendTimers.delete(sendKey);
                const pending = this._pendingValues.get(sendKey);
                if (pending) {
                    dpClient.sendAction("set_plugin_parameter", pending);
                    this._pendingValues.delete(sendKey);
                }
            }, DIAL_SEND_THROTTLE_MS);
            this._dialSendTimers.set(sendKey, timer);
        } else {
            this._pendingValues.set(sendKey, { pluginIndex, paramIndex, value: newValue });
        }
    }

    onDialDown(ev) {
        const settings = this._resolveSettings(ev);
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const sendKey = this._sendKey(ev.action.id, pluginIndex, paramIndex);
        const actionGeneration = this._actionGeneration(ev.action.id);
        const connectionGeneration = this._connectionGeneration;
        const previous = this._rotationQueues.get(sendKey) ?? Promise.resolve();
        const next = previous
            .catch(() => {})
            .then(() => this._resetToDefault(
                ev.action, settings, sendKey,
                actionGeneration, connectionGeneration));
        this._rotationQueues.set(sendKey, next);
        return next.finally(() => {
            if (this._rotationQueues.get(sendKey) === next)
                this._rotationQueues.delete(sendKey);
        });
    }

    async _resetToDefault(
        action, settings, sendKey,
        actionGeneration, connectionGeneration) {
        const { dpClient } = require("../plugin");
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const parameterKey = this._parameterKey(pluginIndex, paramIndex);

        if (!this._isTaskCurrent(
            action.id, actionGeneration, connectionGeneration)) return;

        if (!Number.isFinite(this._defaultValues.get(parameterKey)))
            await this._refreshParameter(action, settings);

        if (!this._isTaskCurrent(
            action.id, actionGeneration, connectionGeneration)) return;

        const defaultValue = this._defaultValues.get(parameterKey);
        if (!Number.isFinite(defaultValue)) {
            if (typeof action.showAlert === "function") action.showAlert();
            return;
        }

        // A coalesced rotate value must not fire after Reset and overwrite the
        // plug-in default. Reset is the final command for this control.
        this._clearSendTimer(sendKey);
        this._applyLocalValue(action, settings, defaultValue);
        dpClient.sendAction("set_plugin_parameter", {
            pluginIndex,
            paramIndex,
            value: defaultValue,
        });
    }

    onWillAppear(ev) {
        this._bumpActionGeneration(ev.action.id);
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        this._showLoading(ev.action, ev.payload.settings ?? {});
        return this._refreshParameter(ev.action, ev.payload.settings ?? {}, true);
    }

    onDidReceiveSettings(ev) {
        this._clearActionTimers(ev.action.id);
        this._bumpActionGeneration(ev.action.id);
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        this._showLoading(ev.action, ev.payload.settings ?? {});
        return this._refreshParameter(ev.action, ev.payload.settings ?? {}, true);
    }

    onWillDisappear(ev) {
        this._clearActionTimers(ev.action.id);
        this._settingsCache.delete(ev.action.id);
        this._bumpActionGeneration(ev.action.id);
        // Increment instead of deleting so an old request cannot acquire the
        // same token after a quick disappear/reappear cycle (ABA race).
        this._refreshTokens.set(
            ev.action.id, (this._refreshTokens.get(ev.action.id) ?? 0) + 1);
        this._renderCache.delete(ev.action);
    }

    updateAllFromState(state) {
        const signature = this._stateSignature(state);
        if (signature === this._lastStateSignature) return Promise.resolve();
        this._lastStateSignature = signature;

        const refreshes = [];
        for (const action of this.actions) {
            const settings = this._settingsCache.get(action.id) ?? {};
            refreshes.push(this._refreshParameter(action, settings, true));
        }
        return Promise.all(refreshes);
    }

    setDisconnectedState() {
        this._connectionGeneration++;
        this._clearAllSendTimers();
        this._invalidateRefreshes();
        this._localOverrideUntil.clear();
        this._lastValues.clear();
        this._defaultValues.clear();
        this._lastStateSignature = null;
        this._renderCache.clear();
        for (const action of this.actions) {
            if (typeof action.setFeedback === "function")
                this._renderCache.apply(action, { feedback: { title: "Disconnected", value: "", indicator: { enabled: false } } });
        }
    }
    setConnectingState() {
        this._connectionGeneration++;
        this._clearAllSendTimers();
        this._invalidateRefreshes();
        this._localOverrideUntil.clear();
        this._lastValues.clear();
        this._defaultValues.clear();
        this._lastStateSignature = null;
        this._renderCache.clear();
        for (const action of this.actions) {
            if (typeof action.setFeedback === "function")
                this._renderCache.apply(action, { feedback: { title: "Connecting...", value: "", indicator: { enabled: false } } });
        }
    }
    alertAll() { for (const action of this.actions) action.showAlert(); }

    _resolveSettings(ev) {
        const payloadSettings = ev?.payload?.settings;
        if (payloadSettings && Object.keys(payloadSettings).length > 0) {
            this._settingsCache.set(ev.action.id, payloadSettings);
            return payloadSettings;
        }
        return this._settingsCache.get(ev.action.id) ?? {};
    }

    _parameterIndices(settings) {
        return {
            pluginIndex: Number(settings.pluginIndex) || 0,
            paramIndex: Number(settings.paramIndex) || 0,
        };
    }

    _parameterKey(pluginIndex, paramIndex) {
        return `${pluginIndex}_${paramIndex}`;
    }

    _sendKey(actionId, pluginIndex, paramIndex) {
        return `${actionId}:${pluginIndex}:${paramIndex}`;
    }

    _clearActionTimers(actionId) {
        const prefix = `${actionId}:`;
        for (const key of this._dialSendTimers.keys()) {
            if (!key.startsWith(prefix)) continue;
            this._clearSendTimer(key);
        }
    }

    _clearAllSendTimers() {
        for (const key of [...this._dialSendTimers.keys()])
            this._clearSendTimer(key);
    }

    _clearSendTimer(sendKey) {
        const timer = this._dialSendTimers.get(sendKey);
        if (timer) clearTimeout(timer);
        this._dialSendTimers.delete(sendKey);
        this._pendingValues.delete(sendKey);
    }

    _invalidateRefreshes() {
        for (const action of this.actions) {
            const actionId = action.id;
            this._refreshTokens.set(
                actionId, (this._refreshTokens.get(actionId) ?? 0) + 1);
        }
    }

    _actionGeneration(actionId) {
        return this._actionGenerations.get(actionId) ?? 0;
    }

    _bumpActionGeneration(actionId) {
        this._actionGenerations.set(
            actionId, this._actionGeneration(actionId) + 1);
    }

    _isTaskCurrent(actionId, actionGeneration, connectionGeneration) {
        return this._settingsCache.has(actionId)
            && this._actionGeneration(actionId) === actionGeneration
            && this._connectionGeneration === connectionGeneration;
    }

    _applyLocalValue(action, settings, value) {
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const parameterKey = this._parameterKey(pluginIndex, paramIndex);
        this._lastValues.set(parameterKey, value);
        this._localOverrideUntil.set(parameterKey, Date.now() + LOCAL_OVERRIDE_MS);
        this._renderValue(action, settings, value);
    }

    async _refreshParameter(action, settings, force = false) {
        const actionId = action.id;
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const parameterKey = this._parameterKey(pluginIndex, paramIndex);
        const overrideUntil = this._localOverrideUntil.get(parameterKey) ?? 0;
        if (force || Date.now() >= overrideUntil) {
            this._lastValues.delete(parameterKey);
            this._defaultValues.delete(parameterKey);
        }
        const token = (this._refreshTokens.get(actionId) ?? 0) + 1;
        this._refreshTokens.set(actionId, token);

        const params = await getPluginParams(pluginIndex);
        if (this._refreshTokens.get(actionId) !== token) return;
        if (!this._settingsCache.has(actionId)) return;

        const currentSettings = this._settingsCache.get(actionId);
        const currentIndices = this._parameterIndices(currentSettings);
        if (currentIndices.pluginIndex !== pluginIndex || currentIndices.paramIndex !== paramIndex)
            return;

        const parameter = Array.isArray(params)
            ? params.find((candidate) => Number(candidate?.index) === paramIndex)
            : null;
        const value = typeof parameter?.value === "number"
            ? parameter.value
            : Number.NaN;
        const defaultValue = typeof parameter?.defaultValue === "number"
            ? parameter.defaultValue
            : Number.NaN;

        if (Number.isFinite(defaultValue))
            this._defaultValues.set(parameterKey, Math.max(0, Math.min(1.0, defaultValue)));

        if (!Number.isFinite(value)) {
            this._showUnavailable(action, currentSettings);
            return;
        }

        if (!force && Date.now() < overrideUntil) return;

        const normalizedValue = Math.max(0, Math.min(1.0, value));
        this._lastValues.set(parameterKey, normalizedValue);
        this._renderValue(action, currentSettings, normalizedValue, parameter?.name);
    }

    _renderValue(action, settings, value, apiParamName = "") {
        const { pluginIndex, paramIndex } = this._parameterIndices(settings);
        const pluginName = settings.pluginName || `Plugin ${pluginIndex + 1}`;
        const paramName = settings.paramName || apiParamName || `Param ${paramIndex}`;
        const percent = Math.round(value * 100);
        if (typeof action.setFeedback === "function") {
            this._renderCache.apply(action, { feedback: {
                title: pluginName,
                value: `${paramName}: ${percent}%`,
                indicator: { value: percent, enabled: true },
            } });
        }
    }

    _showLoading(action, settings) {
        const { pluginIndex } = this._parameterIndices(settings);
        const pluginName = settings.pluginName || `Plugin ${pluginIndex + 1}`;
        if (typeof action.setFeedback === "function") {
            this._renderCache.apply(action, { feedback: {
                title: pluginName,
                value: "Loading...",
                indicator: { value: 0, enabled: false },
            } });
        }
    }

    _showUnavailable(action, settings) {
        const { pluginIndex } = this._parameterIndices(settings);
        const pluginName = settings.pluginName || `Plugin ${pluginIndex + 1}`;
        const paramName = settings.paramName || "Select param";
        if (typeof action.setFeedback === "function") {
            this._renderCache.apply(action, { feedback: {
                title: pluginName,
                value: paramName,
                indicator: { value: 0, enabled: false },
            } });
        }
    }

    _stateSignature(state) {
        const data = state?.data ?? {};
        const plugins = Array.isArray(data.plugins)
            ? data.plugins.map((plugin) => [plugin?.name, plugin?.loaded])
            : [];
        return JSON.stringify([
            data.active_slot,
            data.auto_slot_active,
            data.preset,
            plugins,
        ]);
    }
}

PluginParamAction.UUID = "com.directpipe.directpipe.plugin-param";
module.exports = { PluginParamAction };
