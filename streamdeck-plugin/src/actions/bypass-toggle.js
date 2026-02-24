/**
 * @file bypass-toggle.js
 * @brief Bypass toggle action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const LONG_PRESS_THRESHOLD_MS = 500;

class BypassToggleAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.bypass-toggle";
    /** @type {Map<string, number>} keyDown timestamps per action id */
    _keyDownTimes = new Map();

    onKeyDown(ev) {
        this._keyDownTimes.set(ev.action.id, Date.now());
    }

    onKeyUp(ev) {
        const { dpClient } = require("../plugin");
        const downTime = this._keyDownTimes.get(ev.action.id) || Date.now();
        const duration = Date.now() - downTime;
        this._keyDownTimes.delete(ev.action.id);

        if (duration >= LONG_PRESS_THRESHOLD_MS) {
            dpClient.sendAction("master_bypass");
        } else {
            const settings = ev.payload.settings ?? {};
            const index = (Number(settings.pluginNumber) || 1) - 1;
            dpClient.sendAction("plugin_bypass", { index });
        }
    }

    onWillAppear(ev) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, ev.payload.settings, state);
    }

    onDidReceiveSettings(ev) {
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, ev.payload.settings, state);
    }

    onWillDisappear(ev) {
        this._keyDownTimes.delete(ev.action.id);
    }

    /** Broadcast from plugin.js on DirectPipe state change */
    updateAllFromState(state) {
        for (const action of this.actions) {
            const settings = action.getSettings?.() ?? {};
            this._updateDisplay(action, settings, state);
        }
    }

    alertAll() {
        for (const action of this.actions) {
            action.showAlert();
        }
    }

    _updateDisplay(action, settings, state) {
        if (!state?.data) return;
        const pluginIndex = (Number(settings?.pluginNumber) || 1) - 1;
        const plugins = state.data.plugins;
        const masterBypassed = state.data.master_bypassed;

        let title = "Bypass";
        let isBypassed = false;

        if (masterBypassed) {
            title = "MASTER\nBypassed";
            isBypassed = true;
        } else if (plugins?.[pluginIndex]) {
            const p = plugins[pluginIndex];
            const name = p.name || `Plugin ${pluginIndex + 1}`;
            if (p.bypass) {
                title = `${name}\nBypassed`;
                isBypassed = true;
            } else {
                title = `${name}\nActive`;
            }
        } else {
            title = `Slot ${pluginIndex + 1}\nEmpty`;
        }

        action.setState(isBypassed ? 0 : 1);
        action.setTitle(title);
    }
}

BypassToggleAction.UUID = "com.directpipe.directpipe.bypass-toggle";

module.exports = { BypassToggleAction };
