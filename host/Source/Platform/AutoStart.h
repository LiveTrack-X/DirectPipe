// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file AutoStart.h
 * @brief Platform-specific auto-start (login item) management
 *
 * Windows:  Registry HKCU\...\Run
 * macOS:    ~/Library/LaunchAgents plist
 * Linux:    ~/.config/autostart .desktop file (XDG)
 */
#pragma once

namespace directpipe {
namespace Platform {

/** Check if DirectPipe is configured to start with the OS. */
bool isAutoStartEnabled();

/** Enable or disable auto-start with the OS. Returns false on failure. */
bool setAutoStartEnabled(bool enable);

/** Whether this platform supports auto-start configuration. */
bool isAutoStartSupported();

} // namespace Platform
} // namespace directpipe
