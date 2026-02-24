/**
 * @file panic-mute.js
 * @brief Panic mute action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

class PanicMuteAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.panic-mute";

    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        dpClient.sendAction("panic_mute");
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
        const isMuted = state.data.muted === true;
        action.setState(isMuted ? 1 : 0);
        action.setTitle(isMuted ? "MUTED" : "MUTE");
    }
}

PanicMuteAction.UUID = "com.directpipe.directpipe.panic-mute";

module.exports = { PanicMuteAction };
