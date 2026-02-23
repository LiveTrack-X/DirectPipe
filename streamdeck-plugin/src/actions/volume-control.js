/**
 * @file volume-control.js
 * @brief Volume control action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const TARGET_NAMES = { input: "Input", monitor: "Monitor", virtual_mic: "V-Mic" };

class VolumeControlAction extends SingletonAction {
    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        const target = ev.payload.settings?.target || "monitor";
        dpClient.sendAction("toggle_mute", { target });
    }

    onDialRotate(ev) {
        const { dpClient, getCurrentState } = require("../plugin");
        const target = ev.payload.settings?.target || "monitor";
        const ticks = ev.payload.ticks || 0;
        const delta = ticks * 0.05;

        const state = getCurrentState();
        const volumes = state?.data?.volumes;
        const current = volumes?.[target] ?? 1.0;
        const newValue = Math.max(0, Math.min(1, current + delta));

        dpClient.sendAction("set_volume", { target, value: newValue });
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
        const target = settings?.target || "monitor";
        const volumes = state.data.volumes;
        if (!volumes) return;

        const volume = volumes[target];
        const isMuted = state.data.muted === true;
        const displayName = TARGET_NAMES[target] || target;

        let title;
        if (isMuted) {
            title = `${displayName}\nMUTED`;
        } else if (volume !== undefined) {
            title = `${displayName}\n${Math.round(volume * 100)}%`;
        } else {
            title = displayName;
        }

        action.setState(isMuted ? 1 : 0);
        action.setTitle(title);
    }
}

VolumeControlAction.UUID = "com.directpipe.volume-control";

module.exports = { VolumeControlAction };
