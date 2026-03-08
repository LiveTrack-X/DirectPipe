// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file WindowsMultiInstanceLock.cpp
 * @brief Named Mutex-based multi-instance external control priority (Windows)
 *
 * Two mutexes coordinate which instance owns external controls:
 *   DirectPipe_NormalRunning   — held by normal (non-portable) instance
 *   DirectPipe_ExternalControl — held by whichever instance owns hotkeys/MIDI/WS/HTTP
 */

#include "../MultiInstanceLock.h"

#if defined(_WIN32)

#include <Windows.h>

namespace directpipe {
namespace Platform {

static HANDLE g_normalRunningMutex = nullptr;
static HANDLE g_externalControlMutex = nullptr;

int acquireExternalControlPriority(bool isPortable)
{
    if (!isPortable) {
        // Normal mode: check if a portable instance already owns external controls
        HANDLE probe = OpenMutexW(SYNCHRONIZE, FALSE, L"DirectPipe_ExternalControl");
        if (probe) {
            CloseHandle(probe);
            return -1;  // Portable has control -> must quit
        }

        // Claim both: signal that normal mode is running + own external controls
        g_normalRunningMutex = CreateMutexW(nullptr, TRUE, L"DirectPipe_NormalRunning");
        if (!g_normalRunningMutex) return -1;
        g_externalControlMutex = CreateMutexW(nullptr, TRUE, L"DirectPipe_ExternalControl");
        if (!g_externalControlMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            // Portable grabbed control between our OpenMutex check and CreateMutex
            if (g_externalControlMutex) { CloseHandle(g_externalControlMutex); g_externalControlMutex = nullptr; }
            CloseHandle(g_normalRunningMutex); g_normalRunningMutex = nullptr;
            return -1;
        }
        return 1;  // Full control
    }

    // Portable mode: check if normal mode is running
    HANDLE probe = OpenMutexW(SYNCHRONIZE, FALSE, L"DirectPipe_NormalRunning");
    if (probe) {
        CloseHandle(probe);
        return 0;  // Normal mode is running -> audio only
    }

    // Try to claim external controls (first portable wins)
    g_externalControlMutex = CreateMutexW(nullptr, TRUE, L"DirectPipe_ExternalControl");
    if (g_externalControlMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_externalControlMutex);
        g_externalControlMutex = nullptr;
        return 0;  // Another portable already has control -> audio only
    }

    return (g_externalControlMutex != nullptr) ? 1 : 0;
}

void releaseExternalControlPriority()
{
    if (g_externalControlMutex) { CloseHandle(g_externalControlMutex); g_externalControlMutex = nullptr; }
    if (g_normalRunningMutex)   { CloseHandle(g_normalRunningMutex);   g_normalRunningMutex = nullptr; }
}

} // namespace Platform
} // namespace directpipe

#endif // defined(_WIN32)
