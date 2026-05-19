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
    _lastRenderedByAction = new Map();
    _activePressContexts = new Set();
    _lastKeyDownAt = 0;
    _keypressDebounceMs = 120;

    // Use explicit set semantics to avoid accidental double-toggles if response
    // latency is higher than expected.
    _inFlight = false;
    _expectedMuted = null;

    onKeyDown(ev) {
        const { dpClient, getCurrentState } = require("../plugin");
        const context = ev?.action?.context || ev?.context || "n/a";
        const now = Date.now();
        const timeSinceLast = now - this._lastKeyDownAt;

        if (
            (this._activePressContexts.has(context) && context !== "n/a") ||
            timeSinceLast < this._keypressDebounceMs
        ) {
            streamDeck.logger.info(
                `[panic-mute] ignore duplicate keyDown action=${ev?.action?.id || "n/a"} context=${context} since=${timeSinceLast}ms`
            );
            return;
        }

        if (context !== "n/a") {
            this._activePressContexts.add(context);
        }

        this._lastKeyDownAt = now;
        const state = getCurrentState();
        const hasCurrentState = !!(state?.data && state.data.muted !== undefined);
        const currentMuted = hasCurrentState ? state.data.muted === true : this._lastKnownMuted;
        if (currentMuted === null) {
            streamDeck.logger.warn(
                `[panic-mute] ignore keyDown due unknown mute state action=${ev?.action?.id || "n/a"} context=${context}`
            );
            return;
        }
        const source = hasCurrentState ? "state" : (this._lastKnownMuted !== null ? "cache" : "unknown");
        const targetMuted = !currentMuted;

        this._inFlight = true;
        this._expectedMuted = targetMuted;

        this._sendPanicSet(
            dpClient,
            targetMuted,
            `source=${source} current=${String(currentMuted)} target=${String(targetMuted)}`
        );
    }

    onKeyUp(ev) {
        const context = ev?.action?.context || ev?.context;
        if (context) {
            this._activePressContexts.delete(context);
        }
        streamDeck.logger.info(`[panic-mute] keyUp action=${ev?.action?.id || "n/a"}`);
    }

    onWillAppear(ev) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, state, true);
    }

    onDidReceiveSettings(ev) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, state, true);
    }

    onWillDisappear(_ev) {
        this._resetTransientState();
    }

    updateAllFromState(state) {
        if (state?.data) {
            const latestMuted = state.data.muted === true;
            const shouldLogStateUpdate = this._lastKnownMuted !== latestMuted;
            this._lastKnownMuted = latestMuted;

            if (
                this._inFlight &&
                this._expectedMuted !== null &&
                latestMuted === this._expectedMuted
            ) {
                streamDeck.logger.info(
                    `[panic-mute] ack target=${this._expectedMuted}`
                );
                this._inFlight = false;
                this._expectedMuted = null;
            }

            for (const action of this.actions) {
                this._updateDisplay(action, state);
            }
            if (shouldLogStateUpdate) {
                streamDeck.logger.info(`[panic-mute] stateUpdate muted=${latestMuted}`);
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
            this._applyVisual(action, false, "Disconnected");
        }
    }

    setConnectingState() {
        this._resetTransientState();
        for (const action of this.actions) {
            this._applyVisual(action, false, "Connecting...");
        }
    }

    _updateDisplay(action, stateOrMuted, force = false) {
        let isMuted;
        if (typeof stateOrMuted === "boolean") {
            isMuted = stateOrMuted;
        } else {
            if (!stateOrMuted?.data) return;
            isMuted = stateOrMuted.data.muted === true;
        }

        const actionKey = action?.id || "singleton";
        if (!force && this._lastRenderedByAction.get(actionKey) === isMuted) return;

        this._lastRenderedByAction.set(actionKey, isMuted);
        this._applyVisual(action, isMuted, isMuted ? "MUTED" : "MUTE");
    }

    _applyVisual(action, isMuted, title) {
        if (typeof action.setState === "function") {
            // Existing Stream Deck profiles can retain the panic image slots inverted.
            action.setState(isMuted ? 0 : 1);
        }
        if (typeof action.setImage === "function") {
            action.setImage(isMuted ? "images/panic-on.png" : "images/panic-off.png");
        }
        action.setTitle(title);
    }

    _sendPanicSet(dpClient, targetMuted, context) {
        const sent = dpClient.sendAction("panic_mute", { muted: targetMuted });
        streamDeck.logger.info(
            `[panic-mute] panic_mute set=${targetMuted} ${context} sent=${sent ? "immediate" : "queued"}`
        );
    }

    _resetTransientState() {
        this._lastKnownMuted = null;
        this._lastRenderedByAction.clear();
        this._activePressContexts.clear();
        this._inFlight = false;
        this._expectedMuted = null;
    }
}

PanicMuteAction.UUID = "com.directpipe.directpipe.panic-mute";

module.exports = { PanicMuteAction };
