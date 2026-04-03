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
    _pressLocked = false;
    _pressLockUntil = 0;
    _pressLockTimeoutMs = 1000;

    onKeyDown(ev) {
        const now = Date.now();
        // One toggle per press cycle; recover automatically if key-up is missed.
        if (this._pressLocked && now < this._pressLockUntil) return;
        this._pressLocked = true;
        this._pressLockUntil = now + this._pressLockTimeoutMs;

        const { dpClient } = require("../plugin");
        dpClient.sendAction("panic_mute");
    }

    onKeyUp(_ev) {
        this._pressLocked = false;
        this._pressLockUntil = 0;
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

    onWillDisappear(_ev) {
        this._pressLocked = false;
        this._pressLockUntil = 0;
    }

    updateAllFromState(state) {
        if (this._pressLocked && Date.now() >= this._pressLockUntil) {
            this._pressLocked = false;
            this._pressLockUntil = 0;
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

}

PanicMuteAction.UUID = "com.directpipe.directpipe.panic-mute";

module.exports = { PanicMuteAction };
