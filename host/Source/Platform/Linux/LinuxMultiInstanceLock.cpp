// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file LinuxMultiInstanceLock.cpp
 * @brief Linux multi-instance lock using JUCE InterProcessLock
 *
 * Same implementation as macOS — JUCE InterProcessLock uses
 * file-based locking (flock/fcntl) on POSIX platforms.
 */

#include "../MultiInstanceLock.h"

#if defined(__linux__)

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
                return -1;
            }
        }

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

        return 1;
    }

    // Portable mode: check if normal mode is running
    {
        juce::InterProcessLock probe("DirectPipe_NormalRunning");
        if (!probe.enter(0)) {
            return 0;
        }
    }

    g_externalControlLock = std::make_unique<juce::InterProcessLock>("DirectPipe_ExternalControl");
    if (!g_externalControlLock->enter(0)) {
        g_externalControlLock.reset();
        return 0;
    }

    return 1;
}

void releaseExternalControlPriority()
{
    g_externalControlLock.reset();
    g_normalRunningLock.reset();
}

} // namespace Platform
} // namespace directpipe

#endif // defined(__linux__)
