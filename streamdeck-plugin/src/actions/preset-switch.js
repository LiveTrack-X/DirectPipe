/**
 * @file preset-switch.js
 * @brief Preset switch action for the Stream Deck.
 *
 * Switches between Quick Preset Slots (A-E) or cycles through presets.
 * Shows the current active slot on the button.
 *
 * Button states:
 *   State 0 — This is NOT the active slot
 *   (single-state button, title indicates active status)
 */

const SLOT_LABELS = ["A", "B", "C", "D", "E"];

/**
 * Preset Switch action handler for the Stream Deck.
 */
class PresetSwitchAction {
    /**
     * @param {object} plugin - Reference to the DirectPipePlugin instance.
     */
    constructor(plugin) {
        /** @type {object} */
        this.plugin = plugin;
    }

    /**
     * Called when the button key is pressed down.
     * Switches to the configured slot or cycles to next preset.
     */
    onKeyDown(context, settings, payload) {
        if (settings && settings.slotIndex !== undefined) {
            // Switch to specific preset slot (A-E)
            this.plugin.client.send({
                type: "action",
                action: "switch_preset_slot",
                params: { slot: settings.slotIndex },
            });
        } else if (settings && settings.presetIndex !== undefined) {
            // Load preset by index (legacy)
            this.plugin.client.send({
                type: "action",
                action: "load_preset",
                params: { index: settings.presetIndex },
            });
        } else {
            // Cycle to next preset
            this.plugin.client.send({
                type: "action",
                action: "next_preset",
                params: {},
            });
        }
    }

    /**
     * Called when the button key is released.
     */
    onKeyUp(context, settings, payload) {
        // No action needed
    }

    /**
     * Called when a button with this action appears on the Stream Deck.
     */
    onWillAppear(context, settings, payload) {
        if (this.plugin.currentState) {
            this.onStateUpdate(context, this.plugin.currentState);
        }
    }

    /**
     * Called when a button with this action disappears.
     */
    onWillDisappear(context, settings, payload) {
        // No cleanup needed
    }

    /**
     * Called when settings change via Property Inspector.
     */
    onSettingsChanged(context, settings) {
        if (this.plugin.currentState) {
            this.onStateUpdate(context, this.plugin.currentState);
        }
    }

    /**
     * Called when a DirectPipe state update is received.
     * Updates button to show slot label and active state.
     */
    onStateUpdate(context, state) {
        if (!state || !state.data) return;

        const contextInfo = this.plugin.activeContexts.get(context);
        const settings = contextInfo ? contextInfo.settings : {};

        const activeSlot = state.data.active_slot;

        if (settings && settings.slotIndex !== undefined) {
            const slotIdx = settings.slotIndex;
            const label = SLOT_LABELS[slotIdx] || `${slotIdx + 1}`;
            const isActive = activeSlot === slotIdx;

            const title = isActive ? `[${label}]\nActive` : `${label}`;
            this.plugin.setTitle(context, title);

            // Use state 0 always (single-state action in manifest)
            // Active indication is via the title
        } else {
            // Cycle mode — show current active slot
            const label = SLOT_LABELS[activeSlot] || "?";
            this.plugin.setTitle(context, `Slot ${label}`);
        }
    }
}

module.exports = { PresetSwitchAction };
