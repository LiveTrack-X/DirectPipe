// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file icon-renderer.js
 * @brief SVG-based dynamic icon generator for Stream Deck buttons.
 */

function createRecordingIcon(elapsedSeconds, blinkOn) {
    const mm = String(Math.floor(elapsedSeconds / 60)).padStart(2, "0");
    const ss = String(Math.floor(elapsedSeconds % 60)).padStart(2, "0");
    const dotColor = blinkOn ? "#FF3333" : "#661111";

    return "data:image/svg+xml," + encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" width="144" height="144" viewBox="0 0 144 144">' +
        '<rect width="144" height="144" rx="16" fill="#1A1A2E"/>' +
        '<circle cx="72" cy="50" r="20" fill="' + dotColor + '"/>' +
        '<text x="72" y="110" font-family="Arial,sans-serif" font-size="28" fill="white" text-anchor="middle" font-weight="bold">' + mm + ':' + ss + '</text>' +
        '</svg>'
    );
}

function createVolumeIcon(percent, muted) {
    const barHeight = Math.round((percent / 100) * 100);
    const barColor = muted ? "#666" : "#4CAF50";
    const label = muted ? "MUTED" : percent + "%";

    return "data:image/svg+xml," + encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" width="144" height="144" viewBox="0 0 144 144">' +
        '<rect width="144" height="144" rx="16" fill="#1A1A2E"/>' +
        '<rect x="54" y="' + (120 - barHeight) + '" width="36" height="' + barHeight + '" rx="4" fill="' + barColor + '" opacity="0.8"/>' +
        '<text x="72" y="30" font-family="Arial,sans-serif" font-size="20" fill="white" text-anchor="middle">' + label + '</text>' +
        '</svg>'
    );
}

module.exports = { createRecordingIcon, createVolumeIcon };
