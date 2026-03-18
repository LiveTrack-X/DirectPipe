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
 * @file host-api.js
 * @brief HTTP API client for DirectPipe host, with port fallback.
 */

const BASE_PORTS = [8766, 8768, 8769, 8770, 8771];
let cachedPort = null;

async function findPort() {
    if (cachedPort) {
        try {
            const res = await fetch(`http://localhost:${cachedPort}/api/perf`, { signal: AbortSignal.timeout(500) });
            if (res.ok) { return cachedPort; }
        } catch {}
        cachedPort = null;
    }
    for (const port of BASE_PORTS) {
        try {
            const res = await fetch(`http://localhost:${port}/api/perf`, { signal: AbortSignal.timeout(500) });
            if (res.ok) { cachedPort = port; return port; }
        } catch {}
    }
    return null;
}

async function apiGet(path) {
    const port = await findPort();
    if (!port) return null;
    try {
        const res = await fetch(`http://localhost:${port}${path}`, { signal: AbortSignal.timeout(2000) });
        if (!res.ok) return null;
        return await res.json();
    } catch { return null; }
}

async function getPlugins() { return apiGet("/api/plugins"); }
async function getPluginParams(pluginIndex) { return apiGet(`/api/plugin/${pluginIndex}/params`); }
async function getPerformance() { return apiGet("/api/perf"); }

module.exports = { findPort, apiGet, getPlugins, getPluginParams, getPerformance };
