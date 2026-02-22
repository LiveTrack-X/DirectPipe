/**
 * @file panic-mute.js
 * @brief Panic mute button action for the Stream Deck.
 *
 * Immediately mutes all DirectPipe audio outputs when pressed.
 * Shows a red indicator (State 1) when muted and a normal indicator
 * (State 0) when unmuted. Pressing again while muted will unmute.
 *
 * Button states:
 *   State 0 — Audio is live (shows "MUTE" label)
 *   State 1 — Audio is muted (shows "MUTED" label, red indicator)
 */

/**
 * Panic Mute action handler for the Stream Deck.
 *
 * Provides a one-press emergency mute for all DirectPipe audio outputs.
 * The button visually indicates the current mute state with a red
 * background when muted.
 */
class PanicMuteAction {
    /**
     * @param {object} plugin - Reference to the DirectPipePlugin instance.
     */
    constructor(plugin) {
        /** @type {object} */
        this.plugin = plugin;
    }

    /**
     * Called when the button key is pressed down.
     *
     * Sends the panic_mute action to DirectPipe, which toggles the mute
     * state of all audio outputs.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings.
     * @param {object} payload - Full event payload.
     */
    onKeyDown(context, settings, payload) {
        this.plugin.client.send({
            type: "action",
            action: "panic_mute",
            params: {},
        });
    }

    /**
     * Called when the button key is released.
     *
     * No action needed on key up for panic mute.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings.
     * @param {object} payload - Full event payload.
     */
    onKeyUp(context, settings, payload) {
        // No action on key up
    }

    /**
     * Called when a button with this action appears on the Stream Deck.
     *
     * Applies the current mute state to the button display.
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} settings - Button settings.
     * @param {object} payload - Full event payload.
     */
    onWillAppear(context, settings, payload) {
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
        // No cleanup needed
    }

    /**
     * Called when a DirectPipe state update is received.
     *
     * Updates the button appearance to reflect the current mute state:
     * - State 0: Audio is live, button shows "MUTE" with normal appearance
     * - State 1: Audio is muted, button shows "MUTED" with red indicator
     *
     * @param {string} context - The Stream Deck button context.
     * @param {object} state - The full DirectPipe application state.
     */
    onStateUpdate(context, state) {
        if (!state || !state.data) return;

        const isMuted = state.data.muted === true;

        // State 0 = not muted (normal), State 1 = muted (red indicator)
        this.plugin.setState(context, isMuted ? 1 : 0);

        // Update title to reflect state
        const title = isMuted ? "MUTED" : "MUTE";
        this.plugin.setTitle(context, title);
    }
}

module.exports = { PanicMuteAction };
