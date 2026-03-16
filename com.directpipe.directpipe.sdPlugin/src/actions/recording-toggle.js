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
const { createRecordingIcon } = require("../utils/icon-renderer");

class RecordingToggleAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.recording-toggle";
    _blinkTimer = null;
    _blinkOn = true;

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
            action.setTitle(`REC ${mm}:${ss}`);

            // Start blink timer if not running
            if (!this._blinkTimer) {
                this._blinkOn = true;
                this._blinkTimer = setInterval(() => {
                    this._blinkOn = !this._blinkOn;
                    const { getCurrentState } = require("../plugin");
                    const s = getCurrentState();
                    if (!s?.data?.recording) {
                        clearInterval(this._blinkTimer);
                        this._blinkTimer = null;
                        return;
                    }
                    for (const a of this.actions) {
                        const icon = createRecordingIcon(s.data.recording_seconds || 0, this._blinkOn);
                        a.setImage(icon);
                    }
                }, 1000);
            }

            const icon = createRecordingIcon(secs, this._blinkOn);
            action.setImage(icon);
        } else {
            // Stop blink timer
            if (this._blinkTimer) {
                clearInterval(this._blinkTimer);
                this._blinkTimer = null;
            }
            // Clear custom image (revert to manifest default)
            action.setImage(undefined);
            action.setTitle("REC");
        }
    }
}

RecordingToggleAction.UUID = "com.directpipe.directpipe.recording-toggle";

module.exports = { RecordingToggleAction };
