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
 * @file ActionResult.h
 * @brief Lightweight result type for operations that can fail with a message.
 *
 * Replaces bare bool/void returns so callers can access error details
 * without coupling to logging or NotificationBar.
 */
#pragma once

#include <JuceHeader.h>

namespace directpipe {

struct ActionResult {
    bool success;
    juce::String message;

    static ActionResult ok() { return {true, {}}; }
    static ActionResult ok(const juce::String& msg) { return {true, msg}; }
    static ActionResult fail(const juce::String& msg) { return {false, msg}; }

    explicit operator bool() const { return success; }
};

} // namespace directpipe
