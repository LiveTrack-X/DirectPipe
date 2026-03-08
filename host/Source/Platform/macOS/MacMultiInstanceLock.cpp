// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file MacMultiInstanceLock.cpp
 * @brief macOS multi-instance lock using JUCE InterProcessLock
 *
 * Uses JUCE's cross-platform InterProcessLock (file-based on POSIX)
 * to coordinate which DirectPipe instance owns external controls.
 * Same logic as Windows Named Mutex version but portable.
 */

#include "../MultiInstanceLock.h"

#if defined(__APPLE__)

#include <JuceHeader.h>
#include <memory>

namespace directpipe {
namespace Platform {

static std::unique_ptr<juce::InterProcessLock> g_normalRunningLock;
static std::unique_ptr<juce::InterProcessLock> g_externalControlLock;

int acquireExternalControlPriority(bool isPortable)
{
    if (!isPortable) {
        // Normal mode: check if another instance already owns external controls
        {
            juce::InterProcessLock probe("DirectPipe_ExternalControl");
            if (!probe.enter(0)) {
                // Another instance holds the lock
                return -1;
            }
            // probe releases on scope exit
        }

        // Claim both locks
        g_normalRunningLock = std::make_unique<juce::InterProcessLock>("DirectPipe_NormalRunning");
        if (!g_normalRunningLock->enter(0)) {
            g_normalRunningLock.reset();
            return -1;
        }

        g_externalControlLock = std::make_unique<juce::InterProcessLock>("DirectPipe_ExternalControl");
        if (!g_externalControlLock->enter(0)) {
            g_externalControlLock.reset();
            g_normalRunningLock.reset();
            return -1;
        }

        return 1;  // Full control
    }

    // Portable mode: check if normal mode is running
    {
        juce::InterProcessLock probe("DirectPipe_NormalRunning");
        if (!probe.enter(0)) {
            return 0;  // Normal mode is running -> audio only
        }
    }

    // Try to claim external controls (first portable wins)
    g_externalControlLock = std::make_unique<juce::InterProcessLock>("DirectPipe_ExternalControl");
    if (!g_externalControlLock->enter(0)) {
        g_externalControlLock.reset();
        return 0;  // Another portable already has control -> audio only
    }

    return 1;  // Full control
}

void releaseExternalControlPriority()
{
    g_externalControlLock.reset();
    g_normalRunningLock.reset();
}

} // namespace Platform
} // namespace directpipe

#endif // defined(__APPLE__)
