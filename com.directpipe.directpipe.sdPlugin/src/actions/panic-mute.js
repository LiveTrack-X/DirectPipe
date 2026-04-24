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

const streamDeck = require("@elgato/streamdeck").default;
const { SingletonAction } = require("@elgato/streamdeck");

class PanicMuteAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.panic-mute";
    _lastKnownMuted = null;
    _lastLoggedMuted = null;
    _pendingTarget = null;
    _retryTimer = null;

    onKeyDown(ev) {
        const { dpClient, getCurrentState } = require("../plugin");
        const state = getCurrentState();

        let currentMuted = this._lastKnownMuted;
        let source = "cache";
        if (currentMuted === null && state?.data) {
            currentMuted = state.data.muted === true;
            source = "state";
        } else if (currentMuted === null) {
            source = "unknown";
        }

        if (currentMuted !== null) {
            const targetMuted = !currentMuted;
            this._lastKnownMuted = targetMuted;
            this._sendTarget(dpClient, targetMuted,
                `keyDown action=${ev?.action?.id || "n/a"} source=${source} current=${currentMuted}`);
        } else {
            // Panic mute must be idempotent; never send legacy toggle without known state.
            streamDeck.logger.warn(`[panic-mute] ignored keyDown until state sync action=${ev?.action?.id || "n/a"}`);
            ev?.action?.showAlert?.();
            ev?.action?.setTitle?.("Syncing...");
        }
    }

    onKeyUp(ev) {
        streamDeck.logger.info(`[panic-mute] keyUp action=${ev?.action?.id || "n/a"}`);
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
        this._resetTransientState();
    }

    updateAllFromState(state) {
        if (state?.data) {
            const serverMuted = state.data.muted === true;

            if (this._pendingTarget === null) {
                // No in-flight target: trust server state.
                this._lastKnownMuted = serverMuted;
            } else if (this._pendingTarget === serverMuted) {
                // Ack: server reached our target.
                this._lastKnownMuted = serverMuted;
                streamDeck.logger.info(`[panic-mute] ack target=${this._pendingTarget}`);
                this._pendingTarget = null;
                this._clearRetry();
            } else if (this._lastKnownMuted === null) {
                // Pending exists but no optimistic cache yet.
                this._lastKnownMuted = serverMuted;
            }

            const effectiveMuted =
                (this._pendingTarget !== null && this._lastKnownMuted !== null)
                    ? this._lastKnownMuted
                    : serverMuted;

            if (this._lastLoggedMuted !== effectiveMuted) {
                streamDeck.logger.info(
                    `[panic-mute] stateUpdate muted=${effectiveMuted}`
                );
                this._lastLoggedMuted = effectiveMuted;
            }

            for (const action of this.actions) {
                this._updateDisplay(action, effectiveMuted);
            }
            return;
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
        this._resetTransientState();
        streamDeck.logger.info("[panic-mute] disconnected");
        for (const action of this.actions) {
            action.setTitle("Disconnected");
            if (typeof action.setState === "function") action.setState(0);
        }
    }

    setConnectingState() {
        this._resetTransientState();
        for (const action of this.actions) {
            action.setTitle("Connecting...");
        }
    }

    _updateDisplay(action, stateOrMuted) {
        let isMuted;
        if (typeof stateOrMuted === "boolean") {
            isMuted = stateOrMuted;
        } else {
            if (!stateOrMuted?.data) return;
            isMuted = stateOrMuted.data.muted === true;
        }
        if (typeof action.setState === "function") {
            action.setState(isMuted ? 1 : 0);
        }
        action.setTitle(isMuted ? "MUTED" : "MUTE");
    }

    _sendTarget(dpClient, targetMuted, context) {
        const sent = dpClient.sendAction("panic_mute", { muted: targetMuted });
        this._pendingTarget = targetMuted;
        this._clearRetry();
        this._retryTimer = setTimeout(() => {
            if (this._pendingTarget !== targetMuted) return;
            const retrySent = dpClient.sendAction("panic_mute", { muted: targetMuted });
            streamDeck.logger.info(
                `[panic-mute] retry ${context} target=${targetMuted} sent=${retrySent ? "immediate" : "queued"}`
            );
            this._retryTimer = setTimeout(() => {
                if (this._pendingTarget !== targetMuted) return;
                streamDeck.logger.warn(`[panic-mute] target timeout ${context} target=${targetMuted}; resyncing`);
                this._pendingTarget = null;
                this._lastKnownMuted = null;
                this._lastLoggedMuted = null;
                for (const action of this.actions) {
                    action.setTitle("Syncing...");
                }
            }, 1200);
        }, 180);
        streamDeck.logger.info(
            `[panic-mute] ${context} target=${targetMuted} sent=${sent ? "immediate" : "queued"}`
        );
    }

    _clearRetry() {
        if (this._retryTimer) {
            clearTimeout(this._retryTimer);
            this._retryTimer = null;
        }
    }

    _resetTransientState() {
        this._lastKnownMuted = null;
        this._lastLoggedMuted = null;
        this._pendingTarget = null;
        this._clearRetry();
    }

}

PanicMuteAction.UUID = "com.directpipe.directpipe.panic-mute";

module.exports = { PanicMuteAction };
