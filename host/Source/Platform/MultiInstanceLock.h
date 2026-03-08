// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file MultiInstanceLock.h
 * @brief Platform-specific multi-instance external control priority
 *
 * Coordinates which DirectPipe instance owns external controls
 * (hotkeys, MIDI, WebSocket, HTTP) when multiple instances run
 * simultaneously (normal + portable mode).
 *
 * Windows:  Named Mutexes (DirectPipe_NormalRunning, DirectPipe_ExternalControl)
 * macOS/Linux:  JUCE InterProcessLock or flock (future)
 */
#pragma once

namespace directpipe {
namespace Platform {

/**
 * @brief Acquire external control priority for this instance.
 *
 * @param isPortable  true if running in portable mode
 * @return  1 = full control (enable hotkeys/MIDI/WS/HTTP)
 *          0 = audio only (another instance has control)
 *         -1 = blocked, must quit (normal mode blocked by portable)
 */
int acquireExternalControlPriority(bool isPortable);

/**
 * @brief Release external control priority (call in shutdown).
 */
void releaseExternalControlPriority();

} // namespace Platform
} // namespace directpipe
