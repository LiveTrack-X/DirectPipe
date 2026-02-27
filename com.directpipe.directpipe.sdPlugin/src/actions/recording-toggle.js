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
 * @file recording-toggle.js
 * @brief Recording toggle action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

class RecordingToggleAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.recording-toggle";

    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        dpClient.sendAction("recording_toggle");
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
        const recording = state.data.recording === true;

        if (typeof action.setState === "function") {
            action.setState(recording ? 1 : 0);
        }

        if (recording) {
            const secs = Math.floor(state.data.recording_seconds || 0);
            const mm = String(Math.floor(secs / 60)).padStart(2, "0");
            const ss = String(secs % 60).padStart(2, "0");
            action.setTitle(`REC\n${mm}:${ss}`);
        } else {
            action.setTitle("REC");
        }
    }
}

RecordingToggleAction.UUID = "com.directpipe.directpipe.recording-toggle";

module.exports = { RecordingToggleAction };
