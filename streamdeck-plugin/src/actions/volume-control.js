/**
 * @file volume-control.js
 * @brief Volume control action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const TARGET_NAMES = { input: "Input", monitor: "Monitor" };

class VolumeControlAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.volume-control";

    onKeyDown(ev) {
        const { dpClient, getCurrentState } = require("../plugin");
        const settings = ev.payload.settings ?? {};
        const target = settings.target || "monitor";
        const mode = settings.mode || "mute";

        if (mode === "volume_up" || mode === "volume_down") {
            const step = (settings.step || 5) / 100;
            const delta = mode === "volume_up" ? step : -step;
            const state = getCurrentState();
            const current = state?.data?.volumes?.[target] ?? 1.0;
            const newValue = Math.max(0, Math.min(1, current + delta));
            dpClient.sendAction("set_volume", { target, value: newValue });
        } else {
            dpClient.sendAction("toggle_mute", { target });
        }
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

    async updateAllFromState(state) {
        for (const action of this.actions) {
            const settings = await action.getSettings().catch(() => ({}));
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
        const mode = settings?.mode || "mute";
        const volumes = state.data.volumes;
        if (!volumes) return;

        const volume = volumes[target];
        // Global panic mute, or input-specific mute for input target
        const isMuted = state.data.muted === true ||
            (target === "input" && state.data.input_muted === true);
        const displayName = TARGET_NAMES[target] || target;

        let title;
        if (mode === "volume_up") {
            const pct = volume !== undefined ? `${Math.round(volume * 100)}%` : "";
            title = `${displayName}\n${pct} +`;
        } else if (mode === "volume_down") {
            const pct = volume !== undefined ? `${Math.round(volume * 100)}%` : "";
            title = `${displayName}\n- ${pct}`;
        } else if (isMuted) {
            title = `${displayName}\nMUTED`;
        } else if (volume !== undefined) {
            title = `${displayName}\n${Math.round(volume * 100)}%`;
        } else {
            title = displayName;
        }

        if (typeof action.setState === "function") {
            action.setState(isMuted ? 1 : 0);
        }
        action.setTitle(title);
    }
}

VolumeControlAction.UUID = "com.directpipe.directpipe.volume-control";

module.exports = { VolumeControlAction };
