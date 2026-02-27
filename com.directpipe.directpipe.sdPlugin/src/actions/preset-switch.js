/**
 * @file preset-switch.js
 * @brief Preset switch action — SingletonAction (@elgato/streamdeck SDK).
 */

const { SingletonAction } = require("@elgato/streamdeck");

const SLOT_LABELS = ["A", "B", "C", "D", "E"];

class PresetSwitchAction extends SingletonAction {
    manifestId = "com.directpipe.directpipe.preset-switch";
    /** @type {Map<string, object>} action.id -> cached settings */
    _settingsCache = new Map();

    onKeyDown(ev) {
        const { dpClient } = require("../plugin");
        const settings = ev.payload.settings ?? {};

        if (settings.slotIndex !== undefined && settings.slotIndex !== "cycle") {
            dpClient.sendAction("switch_preset_slot", { slot: Number(settings.slotIndex) });
        } else if (settings.presetIndex !== undefined) {
            dpClient.sendAction("load_preset", { index: settings.presetIndex });
        } else {
            dpClient.sendAction("next_preset");
        }
    }

    onWillAppear(ev) {
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, ev.payload.settings ?? {}, state);
    }

    onDidReceiveSettings(ev) {
        this._settingsCache.set(ev.action.id, ev.payload.settings ?? {});
        const { getCurrentState } = require("../plugin");
        const state = getCurrentState();
        if (state) this._updateDisplay(ev.action, ev.payload.settings ?? {}, state);
    }

    onWillDisappear(ev) {
        this._settingsCache.delete(ev.action.id);
    }

    updateAllFromState(state) {
        for (const action of this.actions) {
            const settings = this._settingsCache.get(action.id) ?? {};
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
        const activeSlot = state.data.active_slot;

        if (settings?.slotIndex !== undefined && settings.slotIndex !== "cycle") {
            const idx = Number(settings.slotIndex);
            const label = SLOT_LABELS[idx] || `${idx + 1}`;
            const isActive = activeSlot === idx;
            action.setTitle(isActive ? `[${label}]\nActive` : label);
        } else {
            const label = SLOT_LABELS[activeSlot] || "?";
            action.setTitle(`Slot ${label}`);
        }
    }
}

PresetSwitchAction.UUID = "com.directpipe.directpipe.preset-switch";

module.exports = { PresetSwitchAction };
