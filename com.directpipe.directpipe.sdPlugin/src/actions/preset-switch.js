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
 * @file preset-switch.js
 * @brief Preset switch action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const SLOT_LABELS = ["A", "B", "C", "D", "E"];

class PresetSwitchAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.preset-switch";
    /** @type {Map<string, object>} action.id -> cached settings */
    _settingsCache = new Map();

    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        const settings = ev.payload.settings ?? {};

        if (settings.slotIndex !== undefined && settings.slotIndex !== "cycle") {
            dpClient.sendAction("switch_preset_slot", { slot: Number(settings.slotIndex) });
        } else if (settings.presetIndex !== undefined) {
            dpClient.sendAction("load_preset", { index: settings.presetIndex });
        } else {
            dpClient.sendAction("next_preset");
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

    updateAllFromState(state) {
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

    setDisconnectedState() {
        for (const action of this.actions) {
            action.setTitle("Disconnected");
            if (typeof action.setState === "function") action.setState(0);
        }
    }

    setConnectingState() {
        for (const action of this.actions) {
            action.setTitle("Connecting...");
        }
    }

    _updateDisplay(action, settings, state) {
        if (!state?.data) return;
        const activeSlot = state.data.active_slot;
        const slotNames = state.data.slot_names || [];
        const autoActive = state.data.auto_slot_active === true;

        if (settings?.slotIndex !== undefined && settings.slotIndex !== "cycle") {
            const idx = Number(settings.slotIndex);
            const slotLabel = SLOT_LABELS[idx] || "?";
            const slotName = slotNames[idx] || "";
            let isActive;

            if (autoActive) {
                // Auto slot active — no regular slot is highlighted
                isActive = false;
            } else if (activeSlot === -1 || activeSlot === undefined || activeSlot === null) {
                isActive = false;
            } else {
                isActive = activeSlot === idx;
            }

            let title;
            if (slotName) {
                title = isActive ? `▶ ${slotLabel}|${slotName}` : `${slotLabel}|${slotName}`;
            } else {
                title = isActive ? `▶ Slot ${slotLabel}` : `Slot ${slotLabel}`;
            }
            action.setTitle(title);
        } else {
            // "cycle" mode — show the currently active slot
            let label;
            let activeName;

            if (autoActive) {
                // Override display for cycle mode
                label = "Auto";
                activeName = "Auto";
            } else if (activeSlot === -1 || activeSlot === undefined || activeSlot === null) {
                // No slot active — show dash
                label = "\u2014";
                activeName = "";
            } else {
                label = SLOT_LABELS[activeSlot] || "?";
                activeName = slotNames[activeSlot] || "";
            }

            if (activeName) {
                action.setTitle(`${label}|${activeName}`);
            } else {
                action.setTitle(`Slot ${label}`);
            }
        }
    }
}

PresetSwitchAction.UUID = "com.directpipe.directpipe.preset-switch";

module.exports = { PresetSwitchAction };
