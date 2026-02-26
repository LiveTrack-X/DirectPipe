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
