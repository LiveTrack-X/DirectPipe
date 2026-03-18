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
 * @file preset-bar.js
 * @brief Preset bar action (SD+ Encoder) — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

class PresetBarAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.preset-bar";

    onDialRotate(ev) {
        const { dpClient } = require("../plugin");
        const ticks = ev.payload.ticks || 0;
        if (ticks > 0) dpClient.sendAction("next_preset");
        else if (ticks < 0) dpClient.sendAction("previous_preset");
    }

    onDialDown(ev) {
        // No-op — dial press does nothing for preset bar
    }

    onTouchTap(ev) {
        const { dpClient } = require("../plugin");
        dpClient.sendAction("next_preset");
    }

    updateAllFromState(state) {
        if (!state?.data) return;
        const slotNames = state.data.slot_names || [];
        const activeSlot = state.data.active_slot;
        const autoActive = state.data.auto_slot_active === true;
        const labels = ["A", "B", "C", "D", "E"];

        for (const action of this.actions) {
            let display;
            let indicatorValue;

            if (autoActive) {
                // Auto slot active — show [Auto] indicator
                display = "[Auto]";
                indicatorValue = 100;
            } else if (activeSlot === -1 || activeSlot === undefined || activeSlot === null) {
                // No slot active — show dash
                display = "\u2014";
                indicatorValue = 0;
            } else {
                const label = labels[activeSlot] || "?";
                const name = slotNames[activeSlot] || "";
                display = name ? `${label}|${name}` : `Slot ${label}`;
                indicatorValue = (activeSlot + 1) * 20;
            }

            if (typeof action.setFeedback === "function") {
                action.setFeedback({
                    title: "Preset",
                    value: display,
                    indicator: { value: indicatorValue, enabled: true },
                });
            }
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
}

PresetBarAction.UUID = "com.directpipe.directpipe.preset-bar";
module.exports = { PresetBarAction };
