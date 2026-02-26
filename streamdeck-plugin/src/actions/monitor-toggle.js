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
