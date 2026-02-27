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
 * @file monitor-toggle.js
 * @brief Monitor output toggle action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

class MonitorToggleAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.monitor-toggle";

    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        dpClient.sendAction("monitor_toggle");
    }

    onWillAppear(ev) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, state);
    }

    onDidReceiveSettings(ev) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, state);
    }

    updateAllFromState(state) {
        for (const action of this.actions) {
            this._updateDisplay(action, state);
        }
    }

    alertAll() {
        for (const action of this.actions) {
            action.showAlert();
        }
    }

    _updateDisplay(action, state) {
        if (!state?.data) return;
        const enabled = state.data.monitor_enabled === true;
        if (typeof action.setState === "function") {
            action.setState(enabled ? 0 : 1);
        }
        action.setTitle(enabled ? "MON ON" : "MON OFF");
    }
}

MonitorToggleAction.UUID = "com.directpipe.directpipe.monitor-toggle";

module.exports = { MonitorToggleAction };
