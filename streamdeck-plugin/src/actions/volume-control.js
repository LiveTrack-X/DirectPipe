/**
 * @file volume-control.js
 * @brief Volume control action for the Stream Deck.
 *
 * Adjusts volume for a specific audio target (input, monitor, virtual_mic).
 * Supports both standard buttons (press to mute) and Stream Deck+ dials
 * (rotate to adjust volume).
 *
 * Button states:
 *   State 0 — Volume is active
 *   State 1 — Volume is muted
 */

/**
 * Volume Control action handler for the Stream Deck.
 */
class VolumeControlAction {
    /**
     * @param {object} plugin - Reference to the DirectPipePlugin instance.
     */
    constructor(plugin) {
        /** @type {object} */
        this.plugin = plugin;
    }

    /**
     * Called when the button key is pressed down.
     * Toggles mute for the configured target.
     */
    onKeyDown(context, settings, payload) {
        const target = (settings && settings.target) || "monitor";
        this.plugin.client.send({
            type: "action",
            action: "toggle_mute",
            params: { target },
        });
    }

    /**
     * Called when the button key is released.
     */
    onKeyUp(context, settings, payload) {
        // No action needed
    }

    /**
     * Called when the Stream Deck+ dial is rotated.
     * Adjusts volume by 5% per tick.
     */
    onDialRotate(context, settings, payload) {
        const target = (settings && settings.target) || "monitor";
        const ticks = (payload && payload.ticks) || 0;
        const delta = ticks * 0.05;
        const current = this.plugin._getVolumeForTarget(target) || 1.0;
        const newValue = Math.max(0, Math.min(1, current + delta));

        this.plugin.client.send({
            type: "action",
            action: "set_volume",
            params: { target, value: newValue },
        });
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
     * Updates button to show current volume level and mute state.
     */
    onStateUpdate(context, state) {
        if (!state || !state.data) return;

        const contextInfo = this.plugin.activeContexts.get(context);
        const settings = contextInfo ? contextInfo.settings : {};
        const target = (settings && settings.target) || "monitor";

        const volumes = state.data.volumes;
        if (!volumes) return;

        const volume = volumes[target];
        const isMuted = state.data.muted === true;

        // Display target name and volume percentage
        const targetNames = {
            input: "Input",
            monitor: "Monitor",
            virtual_mic: "V-Mic",
        };
        const displayName = targetNames[target] || target;

        let title;
        if (isMuted) {
            title = `${displayName}\nMUTED`;
        } else if (volume !== undefined) {
            const pct = Math.round(volume * 100);
            title = `${displayName}\n${pct}%`;
        } else {
            title = displayName;
        }

        this.plugin.setState(context, isMuted ? 1 : 0);
        this.plugin.setTitle(context, title);
    }
}

module.exports = { VolumeControlAction };
