/**
 * @file bypass-toggle.js
 * @brief Handles the bypass toggle action for Stream Deck buttons.
 *
 * When pressed, toggles VST plugin bypass for the configured plugin index.
 * Shows the plugin name and bypass state on the button. A long-press
 * triggers master bypass (all plugins).
 *
 * Button states:
 *   State 0 — Plugin is bypassed (shows "Bypass" label)
 *   State 1 — Plugin is active (shows "Active" label)
 */

// ─── Constants ──────────────────────────────────────────────────────
const LONG_PRESS_THRESHOLD_MS = 500;

/**
 * Bypass Toggle action handler for the Stream Deck.
 *
 * Connects to the DirectPipe plugin to toggle individual VST plugin bypass
 * or master bypass on long-press. Updates button title with the plugin name
 * and bypass/active indicator.
 */
class BypassToggleAction {
    /**
     * @param {object} plugin - Reference to the DirectPipePlugin instance.
     */
    constructor(plugin) {
        /** @type {object} */
        this.plugin = plugin;

        /**
         * Track key-down timestamps per context for long-press detection.
         * @type {Map<string, number>}
         */
        this._keyDownTimes = new Map();
    }

    /**
     * Called when the button key is pressed down.
     *
     * Records the timestamp for long-press detection.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings from the Property Inspector.
     * @param {object} payload - Full event payload.
     */
    onKeyDown(context, settings, payload) {
        this._keyDownTimes.set(context, Date.now());
    }

    /**
     * Called when the button key is released.
     *
     * Determines whether the press was short (plugin bypass) or long
     * (master bypass) and sends the appropriate action to DirectPipe.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings from the Property Inspector.
     * @param {object} payload - Full event payload.
     */
    onKeyUp(context, settings, payload) {
        const downTime = this._keyDownTimes.get(context) || Date.now();
        const pressDuration = Date.now() - downTime;
        this._keyDownTimes.delete(context);

        if (pressDuration >= LONG_PRESS_THRESHOLD_MS) {
            // Long press: toggle master bypass
            this.plugin.client.send({
                type: "action",
                action: "master_bypass",
                params: {},
            });
        } else {
            // Short press: toggle individual plugin bypass
            const pluginIndex = (settings && settings.pluginIndex) || 0;
            this.plugin.client.send({
                type: "action",
                action: "plugin_bypass",
                params: { index: pluginIndex },
            });
        }
    }

    /**
     * Called when a button with this action appears on the Stream Deck.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings.
     * @param {object} payload - Full event payload.
     */
    onWillAppear(context, settings, payload) {
        // Apply current state immediately if available
        if (this.plugin.currentState) {
            this.onStateUpdate(context, this.plugin.currentState);
        }
    }

    /**
     * Called when a button with this action disappears from the Stream Deck.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings.
     * @param {object} payload - Full event payload.
     */
    onWillDisappear(context, settings, payload) {
        this._keyDownTimes.delete(context);
    }

    /**
     * Called when the button settings are changed via the Property Inspector.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Updated settings.
     */
    onSettingsChanged(context, settings) {
        // Re-apply state with new settings
        if (this.plugin.currentState) {
            this.onStateUpdate(context, this.plugin.currentState);
        }
    }

    /**
     * Called when a DirectPipe state update is received.
     *
     * Updates the button title and state to reflect the current bypass
     * status of the configured plugin.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} state - The full DirectPipe application state.
     */
    onStateUpdate(context, state) {
        if (!state || !state.data) return;

        const contextInfo = this.plugin.activeContexts.get(context);
        const settings = contextInfo ? contextInfo.settings : {};
        const pluginIndex = (settings && settings.pluginIndex) || 0;

        const plugins = state.data.plugins;
        const masterBypassed = state.data.master_bypassed;

        // Determine what to display on the button
        let title = "Bypass";
        let isBypassed = false;

        if (masterBypassed) {
            // Master bypass overrides individual state
            title = "MASTER\nBypassed";
            isBypassed = true;
        } else if (plugins && plugins[pluginIndex]) {
            const plugin = plugins[pluginIndex];
            const pluginName = plugin.name || `Plugin ${pluginIndex + 1}`;

            if (plugin.bypass) {
                title = `${pluginName}\nBypassed`;
                isBypassed = true;
            } else {
                title = `${pluginName}\nActive`;
                isBypassed = false;
            }
        } else {
            // No plugin at this index
            title = `Slot ${pluginIndex + 1}\nEmpty`;
            isBypassed = false;
        }

        // Update the button display
        // State 0 = bypassed appearance, State 1 = active appearance
        this.plugin.setState(context, isBypassed ? 0 : 1);
        this.plugin.setTitle(context, title);
    }
}

module.exports = { BypassToggleAction };
