// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.

/**
 * @file render-cache.js
 * @brief Small guard for skipping duplicate Stream Deck SDK render calls.
 */

class RenderCache {
    constructor() {
        this._byAction = new Map();
    }

    clear() {
        this._byAction.clear();
    }

    delete(actionOrId) {
        this._byAction.delete(this._key(actionOrId));
    }

    apply(action, next) {
        if (!action || !next) return;

        const key = this._key(action);
        const prev = this._byAction.get(key) ?? {};
        const current = { ...prev };

        if (Object.prototype.hasOwnProperty.call(next, "state") && prev.state !== next.state) {
            if (typeof action.setState === "function") action.setState(next.state);
            current.state = next.state;
        }

        if (Object.prototype.hasOwnProperty.call(next, "title") && prev.title !== next.title) {
            action.setTitle(next.title);
            current.title = next.title;
        }

        if (Object.prototype.hasOwnProperty.call(next, "image") && prev.image !== next.image) {
            if (typeof action.setImage === "function") action.setImage(next.image);
            current.image = next.image;
        }

        if (Object.prototype.hasOwnProperty.call(next, "feedback")) {
            const feedbackKey = JSON.stringify(next.feedback);
            if (prev.feedbackKey !== feedbackKey) {
                if (typeof action.setFeedback === "function") action.setFeedback(next.feedback);
                current.feedbackKey = feedbackKey;
            }
        }

        this._byAction.set(key, current);
    }

    _key(actionOrId) {
        if (typeof actionOrId === "string") return actionOrId;
        return actionOrId?.id || actionOrId?.context || "singleton";
    }
}

module.exports = { RenderCache };
