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
 * @file panic-mute.js
 * @brief Panic mute action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

class PanicMuteAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.panic-mute";
    _pending = false;
    _pendingBeforeMuted = null;
    _pendingAttempts = 0;
    _pendingTimer = null;
    _retryDelayMs = 140;
    _maxAttempts = 2;

    onKeyDown(_ev) {
        if (this._pending) return;

        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        this._pendingBeforeMuted = state?.data ? state.data.muted === true : null;
        this._pending = true;
        this._pendingAttempts = 0;
        this._sendToggleAttempt();
    }

    onKeyUp(_ev) {}

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

    onWillDisappear(_ev) {
        this._clearPending();
    }

    updateAllFromState(state) {
        if (this._pending && state?.data && this._pendingBeforeMuted !== null) {
            const isMuted = state.data.muted === true;
            if (isMuted !== this._pendingBeforeMuted) {
                this._clearPending();
            }
        }
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
        this._clearPending();
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
        const isMuted = state.data.muted === true;
        if (typeof action.setState === "function") {
            action.setState(isMuted ? 1 : 0);
        }
        action.setTitle(isMuted ? "MUTED" : "MUTE");
    }

    _sendToggleAttempt() {
        if (!this._pending) return;

        const { dpClient } = require("../plugin");
        dpClient.sendAction("panic_mute");
        this._pendingAttempts += 1;

        if (this._pendingTimer) {
            clearTimeout(this._pendingTimer);
            this._pendingTimer = null;
        }

        this._pendingTimer = setTimeout(() => {
            this._pendingTimer = null;
            this._onRetryTimer();
        }, this._retryDelayMs);
    }

    _onRetryTimer() {
        if (!this._pending) return;

        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state?.data && this._pendingBeforeMuted !== null) {
            const isMuted = state.data.muted === true;
            if (isMuted !== this._pendingBeforeMuted) {
                this._clearPending();
                return;
            }
        }

        if (this._pendingAttempts < this._maxAttempts) {
            this._sendToggleAttempt();
            return;
        }

        this._clearPending();
    }

    _clearPending() {
        this._pending = false;
        this._pendingBeforeMuted = null;
        this._pendingAttempts = 0;
        if (this._pendingTimer) {
            clearTimeout(this._pendingTimer);
            this._pendingTimer = null;
        }
    }

}

PanicMuteAction.UUID = "com.directpipe.directpipe.panic-mute";

module.exports = { PanicMuteAction };
